// ==============================================================================
//  UninstallerMac.mm — AuraClean (NextDSP)   (compilé avec -fobjc-arc)
// ==============================================================================

#import <AppKit/AppKit.h>
#import <Foundation/Foundation.h>

#include "UninstallerMac.h"

#include <cerrno>
#include <climits>
#include <cstdlib>
#include <cstring>
#include <map>
#include <set>
#include <sys/stat.h>
#include <unistd.h>

#if ! __has_feature(objc_arc)
 #error "UninstallerMac.mm doit être compilé avec -fobjc-arc"
#endif

namespace aura
{
namespace
{
    //--------------------------------------------------------------------------
    NSString* toNS (const juce::String& s)
    {
        NSString* result = [NSString stringWithUTF8String: s.toRawUTF8()];
        return result != nil ? result : @"";
    }

    juce::String fromNS (NSString* s)
    {
        return s != nil ? juce::String::fromUTF8 ([s UTF8String]) : juce::String();
    }

    NSURL* toURL (const juce::File& f)
    {
        return [NSURL fileURLWithPath: toNS (f.getFullPathName())];
    }

    juce::String txt (const char* utf8) { return juce::String::fromUTF8 (utf8); }

    juce::String resolvedPath (const juce::String& path)
    {
        char buffer[PATH_MAX];

        if (::realpath (path.toRawUTF8(), buffer) != nullptr)
            return juce::String::fromUTF8 (buffer);

        return {};
    }

    bool existsNoFollow (const juce::File& f)
    {
        struct stat st {};
        return ::lstat (f.getFullPathName().toRawUTF8(), &st) == 0;
    }

    bool canWrite (const juce::File& f)
    {
        return ::access (f.getFullPathName().toRawUTF8(), W_OK) == 0;
    }

    bool hasControlCharacters (const juce::String& s)
    {
        for (auto p = s.getCharPointer(); ! p.isEmpty();)
        {
            const auto c = p.getAndAdvance();
            if (c < 0x20 || c == 0x7f)
                return true;
        }
        return false;
    }

    juce::String posixMessage (int error)
    {
        return juce::String::fromUTF8 (std::strerror (error));
    }

    /** Quote POSIX sûr : tout entre apostrophes, les apostrophes internes deviennent '\'' */
    juce::String shellQuote (const juce::String& s)
    {
        return "'" + s.replace ("'", "'\\''") + "'";
    }

    juce::String appleScriptEscape (const juce::String& s)
    {
        return s.replace ("\\", "\\\\").replace ("\"", "\\\"");
    }

    bool isPermissionError (NSError* error)
    {
        if (error == nil)
            return false;

        auto isPosixPermission = [] (NSError* e)
        {
            return e != nil && [e.domain isEqualToString: NSPOSIXErrorDomain] && (e.code == EACCES || e.code == EPERM);
        };

        if ([error.domain isEqualToString: NSCocoaErrorDomain]
            && (error.code == NSFileWriteNoPermissionError || error.code == NSFileReadNoPermissionError))
            return true;

        return isPosixPermission (error) || isPosixPermission (error.userInfo[NSUnderlyingErrorKey]);
    }

    bool isDestinationAllowed (const juce::File& folder, const std::vector<juce::File>& roots)
    {
        for (const auto& root : roots)
            if (folder.getFullPathName() == root.getFullPathName())
                return true;

        return false;
    }

    //--------------------------------------------------------------------------
    //  Exécution privilégiée groupée (une seule demande de mot de passe)
    //--------------------------------------------------------------------------
    struct PrivilegedResult
    {
        bool         executed  = false;
        bool         cancelled = false;
        juce::String output, error;
    };

    PrivilegedResult runPrivilegedShell (const juce::String& script)
    {
        PrivilegedResult  result;
        PrivilegedResult* resultPtr = &result;

        NSString* source = toNS ("do shell script \"" + appleScriptEscape (script)
                                 + "\" with administrator privileges without altering line endings");

        // NSAppleScript doit tourner sur le main thread ; la fenêtre d'authentification
        // affiche alors le nom "AuraClean" (et non "osascript").
        void (^execute) (void) = ^{
            @autoreleasepool
            {
                NSDictionary* errorInfo = nil;
                NSAppleScript* appleScript = [[NSAppleScript alloc] initWithSource: source];
                NSAppleEventDescriptor* descriptor = [appleScript executeAndReturnError: &errorInfo];

                if (descriptor != nil)
                {
                    resultPtr->executed = true;
                    resultPtr->output   = fromNS ([descriptor stringValue]);
                }
                else
                {
                    NSNumber* code       = errorInfo[NSAppleScriptErrorNumber];
                    resultPtr->cancelled = code != nil && code.intValue == -128;  // userCanceledErr
                    resultPtr->error     = fromNS (errorInfo[NSAppleScriptErrorMessage]);
                }
            }
        };

        if ([NSThread isMainThread])
            execute();
        else
            dispatch_sync (dispatch_get_main_queue(), execute);

        return result;
    }

    /** Une commande par élément : en cas d'échec, imprime "FAIL:<index>:<message sur une ligne>". */
    juce::String guardedCommand (int index, const juce::String& commands)
    {
        return "out=$( (" + commands + ") 2>&1 ) || printf 'FAIL:%s:%s\\n' " + juce::String (index)
             + " \"$(printf '%s' \"$out\" | /usr/bin/tr '\\n' ' ')\"; ";
    }

    std::map<int, juce::String> parseFailures (const juce::String& output)
    {
        std::map<int, juce::String> failures;

        for (const auto& line : juce::StringArray::fromLines (output))
        {
            if (! line.startsWith ("FAIL:"))
                continue;

            const auto rest = line.substring (5);
            failures[rest.upToFirstOccurrenceOf (":", false, false).getIntValue()]
                = rest.fromFirstOccurrenceOf (":", false, false).trim();
        }

        return failures;
    }

    void reportPrivilegeFailure (OperationReport& report, const PrivilegedResult& pr, size_t count)
    {
        if (pr.cancelled)
        {
            report.authorisationCancelled = true;
            report.errors.add (txt ("Authentification annulée : ") + juce::String ((int) count)
                               + txt (" plugin(s) système non traité(s)."));
        }
        else
        {
            report.errors.add (txt ("Échec de l'élévation administrateur : ") + pr.error);
        }
    }

    /** Destination libre dans 'folder' : "Nom.vst3", sinon "Nom (2).vst3"... */
    juce::File uniqueDestination (const juce::File& folder, const juce::String& fileName,
                                  std::set<juce::String>& reserved, bool allowRename, bool& conflict)
    {
        conflict = false;

        const bool   hasExt = fileName.containsChar ('.');
        const auto   stem   = hasExt ? fileName.upToLastOccurrenceOf (".", false, false) : fileName;
        const auto   ext    = hasExt ? fileName.fromLastOccurrenceOf (".", true, false) : juce::String();

        for (int n = 1; n < 1000; ++n)
        {
            const auto candidate = folder.getChildFile (n == 1 ? fileName : stem + " (" + juce::String (n) + ")" + ext);

            if (! existsNoFollow (candidate) && reserved.count (candidate.getFullPathName()) == 0)
            {
                reserved.insert (candidate.getFullPathName());
                return candidate;
            }

            if (! allowRename)
            {
                conflict = true;
                return candidate;
            }
        }

        conflict = true;
        return {};
    }

    juce::File makeStagingFolder()
    {
        const auto base = juce::File::getSpecialLocation (juce::File::userHomeDirectory)
                              .getChildFile ("Library/Caches/com.nextdsp.auraclean/Staging");

        const auto folder = base.getChildFile (juce::Time::getCurrentTime().formatted ("%Y%m%d-%H%M%S") + "-"
                                               + juce::String::toHexString (juce::Random::getSystemRandom().nextInt()));
        folder.createDirectory();
        return folder;
    }

    /** Supprime le dossier de transit uniquement s'il ne contient plus aucun bundle. */
    void removeIfEmpty (const juce::File& folder)
    {
        if (! folder.isDirectory())
            return;

        juce::Array<juce::File> slots;

        for (const auto& child : juce::RangedDirectoryIterator (folder, false, "*", juce::File::findDirectories))
            slots.add (child.getFile());

        for (const auto& slot : slots)
            if (slot.getNumberOfChildFiles (juce::File::findFilesAndDirectories, "*") == 0)
                slot.deleteFile();  // rmdir : échoue sans risque si non vide

        if (folder.getNumberOfChildFiles (juce::File::findFilesAndDirectories, "*") == 0)
            folder.deleteFile();
    }
} // namespace

//==============================================================================
bool UninstallerMac::isSafePluginPath (const juce::File& bundle, const std::vector<juce::File>& roots, juce::String& reason)
{
    const auto path = bundle.getFullPathName();

    if (path.isEmpty() || ! path.startsWithChar ('/') || hasControlCharacters (path)
        || path.contains ("/../") || path.endsWith ("/..") || path.contains ("/./"))
    {
        reason = txt ("chemin invalide");
        return false;
    }

    const auto ext = bundle.getFileExtension().toLowerCase();

    if (ext != ".vst3" && ext != ".component" && ext != ".aaxplugin")
    {
        reason = txt ("extension non reconnue comme plugin audio");
        return false;
    }

    if (! existsNoFollow (bundle))
    {
        reason = txt ("introuvable sur le disque");
        return false;
    }

    // On résout le PARENT (le bundle lui-même peut être un lien, qu'on déplace tel quel)
    const auto parentReal = resolvedPath (bundle.getParentDirectory().getFullPathName());

    if (parentReal.isEmpty())
    {
        reason = txt ("dossier parent inaccessible");
        return false;
    }

    for (const auto& root : roots)
    {
        const auto rootReal = resolvedPath (root.getFullPathName());

        if (rootReal.isNotEmpty() && (parentReal == rootReal || parentReal.startsWith (rootReal + "/")))
            return true;
    }

    reason = txt ("hors des dossiers de plugins autorisés");
    return false;
}

bool UninstallerMac::requiresAdministrator (const juce::File& bundle, const juce::File& destinationFolder)
{
    if (! canWrite (bundle.getParentDirectory()))
        return true;

    // Déplacer un dossier vers un autre parent exige aussi l'écriture sur le dossier
    // lui-même (mise à jour de ".."). Les bundles root:wheel 755 tombent ici.
    if (bundle.isDirectory() && ! bundle.isSymbolicLink() && ! canWrite (bundle))
        return true;

    if (destinationFolder != juce::File())
    {
        auto probe = destinationFolder;

        while (! probe.isDirectory() && probe.getParentDirectory() != probe)
            probe = probe.getParentDirectory();

        if (! canWrite (probe))
            return true;
    }

    return false;
}

//==============================================================================
OperationReport UninstallerMac::moveToTrash (const std::vector<juce::File>& bundles, const std::vector<juce::File>& roots)
{
    OperationReport report;
    report.requested = (int) bundles.size();

    NSFileManager* fm = [NSFileManager defaultManager];
    std::vector<juce::File> privileged;

    //---------------------------------------------------- 1. sans privilèges
    for (const auto& bundle : bundles)
    {
        @autoreleasepool
        {
            juce::String reason;

            if (! isSafePluginPath (bundle, roots, reason))
            {
                report.errors.add (bundle.getFileName() + " : " + reason);
                continue;
            }

            if (requiresAdministrator (bundle))
            {
                privileged.push_back (bundle);
                continue;
            }

            NSError* error = nil;

            if ([fm trashItemAtURL: toURL (bundle) resultingItemURL: nil error: &error])
            {
                ++report.succeeded;
                report.succeededPaths.add (bundle.getFullPathName());
            }
            else if (isPermissionError (error))
            {
                privileged.push_back (bundle);
            }
            else
            {
                report.errors.add (bundle.getFileName() + " : " + fromNS (error.localizedDescription));
            }
        }
    }

    if (privileged.empty())
        return report;

    //---------------------------------------------------- 2. lot root unique
    const auto staging = makeStagingFolder();
    const auto uid     = juce::String ((int) ::getuid());
    const auto gid     = juce::String ((int) ::getgid());

    std::vector<juce::File> stagedTargets;
    juce::String script;

    for (size_t i = 0; i < privileged.size(); ++i)
    {
        const auto slot   = staging.getChildFile (juce::String ((int) i));
        const auto target = slot.getChildFile (privileged[i].getFileName());
        slot.createDirectory();
        stagedTargets.push_back (target);

        script << guardedCommand ((int) i,
                                  "/bin/mv -- " + shellQuote (privileged[i].getFullPathName()) + " " + shellQuote (target.getFullPathName())
                                  + " && /usr/sbin/chown -R " + uid + ":" + gid + " " + shellQuote (target.getFullPathName()));
    }

    script << "exit 0";

    report.usedAdministratorPrivileges = true;
    const auto pr = runPrivilegedShell (script);

    if (! pr.executed)
    {
        reportPrivilegeFailure (report, pr, privileged.size());
        removeIfEmpty (staging);
        return report;
    }

    const auto failures = parseFailures (pr.output);

    //---------------------------------------------------- 3. transit → Corbeille (droits utilisateur)
    for (size_t i = 0; i < privileged.size(); ++i)
    {
        @autoreleasepool
        {
            if (const auto it = failures.find ((int) i); it != failures.end())
            {
                report.errors.add (privileged[i].getFileName() + " : " + it->second);
                continue;
            }

            NSError* error = nil;

            if ([fm trashItemAtURL: toURL (stagedTargets[i]) resultingItemURL: nil error: &error])
            {
                ++report.succeeded;
                report.succeededPaths.add (privileged[i].getFullPathName());
            }
            else
            {
                // Le plugin n'est déjà plus vu par les DAW ; on signale où il se trouve.
                ++report.succeeded;
                report.succeededPaths.add (privileged[i].getFullPathName());
                report.leftovers.add (stagedTargets[i].getFullPathName());
                report.errors.add (privileged[i].getFileName() + txt (" : retiré, mais non envoyé à la Corbeille (")
                                   + fromNS (error.localizedDescription) + ")");
            }
        }
    }

    removeIfEmpty (staging);
    return report;
}

//==============================================================================
OperationReport UninstallerMac::moveBundles (const std::vector<BundleMove>& moves, const std::vector<juce::File>& roots)
{
    OperationReport report;
    report.requested = (int) moves.size();

    struct Pending
    {
        juce::File source, destination, folder;
    };

    std::vector<Pending> privileged;
    std::set<juce::String> reserved;

    //---------------------------------------------------- 1. sans privilèges
    for (const auto& move : moves)
    {
        juce::String reason;

        if (! isSafePluginPath (move.source, roots, reason))
        {
            report.errors.add (move.source.getFileName() + " : " + reason);
            continue;
        }

        if (! isDestinationAllowed (move.destinationFolder, roots))
        {
            report.errors.add (move.source.getFileName() + txt (" : dossier de destination non autorisé"));
            continue;
        }

        bool conflict = false;
        const auto destination = uniqueDestination (move.destinationFolder, move.source.getFileName(),
                                                    reserved, move.renameOnConflict, conflict);
        if (conflict)
        {
            report.errors.add (move.source.getFileName() + txt (" : un plugin du même nom existe déjà dans « ")
                               + move.destinationFolder.getFileName() + txt (" »"));
            continue;
        }

        if (requiresAdministrator (move.source, move.destinationFolder))
        {
            privileged.push_back ({ move.source, destination, move.destinationFolder });
            continue;
        }

        if (! move.destinationFolder.isDirectory())
        {
            if (const auto created = move.destinationFolder.createDirectory(); created.failed())
            {
                privileged.push_back ({ move.source, destination, move.destinationFolder });
                continue;
            }
        }

        if (::rename (move.source.getFullPathName().toRawUTF8(), destination.getFullPathName().toRawUTF8()) == 0)
        {
            ++report.succeeded;
            report.succeededPaths.add (move.source.getFullPathName());
            continue;
        }

        const int error = errno;

        if (error == EACCES || error == EPERM)
            privileged.push_back ({ move.source, destination, move.destinationFolder });
        else
            report.errors.add (move.source.getFileName() + " : " + posixMessage (error));
    }

    if (privileged.empty())
        return report;

    //---------------------------------------------------- 2. lot root unique
    juce::String script;

    for (size_t i = 0; i < privileged.size(); ++i)
    {
        const auto& p = privileged[i];
        script << guardedCommand ((int) i,
                                  "/bin/mkdir -p " + shellQuote (p.folder.getFullPathName())
                                  + " && /bin/mv -n -- " + shellQuote (p.source.getFullPathName()) + " " + shellQuote (p.destination.getFullPathName())
                                  + " && [ ! -e " + shellQuote (p.source.getFullPathName()) + " ] && [ ! -L " + shellQuote (p.source.getFullPathName()) + " ]");
    }

    script << "exit 0";

    report.usedAdministratorPrivileges = true;
    const auto pr = runPrivilegedShell (script);

    if (! pr.executed)
    {
        reportPrivilegeFailure (report, pr, privileged.size());
        return report;
    }

    const auto failures = parseFailures (pr.output);

    for (size_t i = 0; i < privileged.size(); ++i)
    {
        if (const auto it = failures.find ((int) i); it != failures.end())
        {
            report.errors.add (privileged[i].source.getFileName() + " : "
                               + (it->second.isNotEmpty() ? it->second : txt ("déplacement refusé")));
            continue;
        }

        ++report.succeeded;
        report.succeededPaths.add (privileged[i].source.getFullPathName());
    }

    return report;
}

//==============================================================================
void UninstallerMac::refreshAudioUnitRegistrar()
{
    @autoreleasepool
    {
        NSTask* task         = [[NSTask alloc] init];
        task.executableURL   = [NSURL fileURLWithPath: @"/usr/bin/killall"];
        task.arguments       = @[ @"-9", @"AudioComponentRegistrar" ];
        task.standardOutput  = [NSFileHandle fileHandleWithNullDevice];
        task.standardError   = [NSFileHandle fileHandleWithNullDevice];

        NSError* error = nil;

        if ([task launchAndReturnError: &error])
            [task waitUntilExit];
    }
}

void UninstallerMac::applyDarkAppearance()
{
    if (@available (macOS 10.14, *))
        [NSApp setAppearance: [NSAppearance appearanceNamed: NSAppearanceNameDarkAqua]];
}

} // namespace aura
