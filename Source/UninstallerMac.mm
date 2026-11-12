// ==============================================================================
//  UninstallerMac.mm — AuraClean (NextDSP)   (compilé avec -fobjc-arc)
// ==============================================================================

#import <AppKit/AppKit.h>
#import <Foundation/Foundation.h>

#include "UninstallerMac.h"
#include "LeftoverScanner.h"
#include "PluginScanner.h"

#include <cerrno>
#include <climits>
#include <cstdlib>
#include <cstring>
#include <algorithm>
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

    if (ext != ".vst3" && ext != ".component" && ext != ".aaxplugin" && ext != ".vst" && ext != ".clap")
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
    std::vector<TrashItem> items;

    for (const auto& b : bundles)
        items.push_back ({ b, true, {}, {} });

    return moveItemsToTrash (items, roots);
}

//==============================================================================
bool UninstallerMac::isSafeLeftoverPath (const juce::File& file, const juce::String& vendorKey, juce::String& reason)
{
    const auto path = file.getFullPathName();

    if (path.isEmpty() || ! path.startsWithChar ('/') || hasControlCharacters (path)
        || path.contains ("/../") || path.endsWith ("/..") || path.contains ("/./"))
    {
        reason = txt ("chemin invalide");
        return false;
    }

    if (! existsNoFollow (file))
    {
        reason = txt ("introuvable sur le disque");
        return false;
    }

    if (LeftoverScanner::isProtectedPath (file))
    {
        reason = txt ("emplacement protégé");
        return false;
    }

    const auto parentReal = resolvedPath (file.getParentDirectory().getFullPathName());

    if (parentReal.isEmpty())
    {
        reason = txt ("dossier parent inaccessible");
        return false;
    }

    // Reçus d'installation : uniquement les .bom / .plist directement dans /var/db/receipts
    if (parentReal == resolvedPath (LeftoverScanner::receiptsFolder().getFullPathName()))
    {
        const auto ext = file.getFileExtension().toLowerCase();

        if ((ext == ".bom" || ext == ".plist") && ! file.isDirectory())
            return true;

        reason = txt ("fichier de reçu inattendu");
        return false;
    }

    bool inside = false, topLevel = false;

    for (const auto& root : LeftoverScanner::searchRoots())
    {
        const auto rootReal = resolvedPath (root.folder.getFullPathName());

        if (rootReal.isEmpty())
            continue;

        if (parentReal == rootReal)
        {
            // ~/Documents, ~/Music : rien n'est accepté au premier niveau, seulement sous <Vendor>/
            if (root.vendorOnly)
            {
                reason = txt ("élément directement dans ") + root.folder.getFileName() + txt (" : refusé");
                return false;
            }

            inside = topLevel = true;
            break;
        }

        if (parentReal.startsWith (rootReal + "/"))
            inside = true;
    }

    if (! inside)
    {
        reason = txt ("hors des emplacements de fichiers résiduels autorisés");
        return false;
    }

    // Règle anti-collision : jamais <Racine>/<Vendor>, toujours <Vendor>/<Plugin>
    if (topLevel && vendorKey.isNotEmpty() && file.isDirectory()
        && PluginScanner::normaliseKey (file.getFileName()) == vendorKey)
    {
        reason = txt ("dossier de marque partagé par d'autres plugins");
        return false;
    }

    return true;
}

namespace
{
    /** Le dossier <Vendor> doit être exactement au premier niveau d'une racine de résidus. */
    bool isSearchRoot (const juce::String& realPath)
    {
        if (realPath.isEmpty())
            return false;

        for (const auto& root : LeftoverScanner::searchRoots())
            if (realPath == resolvedPath (root.folder.getFullPathName()))
                return true;

        return false;
    }

    /** Élagable : <Racine>/<Vendor>, ou <Racine>/<Vendor>/<Presets|Settings|Data…> — rien d'autre. */
    bool isSafePruneFolder (const juce::File& folder)
    {
        if (folder == juce::File() || ! folder.isDirectory() || folder.isSymbolicLink())
            return false;

        const auto name = folder.getFileName();

        if (name.isEmpty() || name.startsWithChar ('.'))
            return false;

        const auto parent = folder.getParentDirectory();

        // Niveau 1 : le dossier de marque
        if (! LeftoverScanner::isSharedFolderName (name) && isSearchRoot (resolvedPath (parent.getFullPathName())))
            return true;

        // Niveau 2 : sous-dossier intermédiaire d'un dossier de marque
        return LeftoverScanner::isIntermediateFolderName (name)
            && ! LeftoverScanner::isSharedFolderName (parent.getFileName())
            && isSearchRoot (resolvedPath (parent.getParentDirectory().getFullPathName()));
    }

    /** Vide ou ne contient que des .DS_Store. */
    bool isEffectivelyEmpty (const juce::File& folder)
    {
        for (const auto& entry : juce::RangedDirectoryIterator (folder, false, "*", juce::File::findFilesAndDirectories))
            if (entry.getFile().getFileName() != ".DS_Store")
                return false;

        return true;
    }

    /** Vide une fois élagués les sous-dossiers qui sont eux-mêmes dans la liste d'élagage. */
    bool willBeEmptyAfterPruning (const juce::File& folder, const std::vector<juce::File>& pruneSet)
    {
        for (const auto& entry : juce::RangedDirectoryIterator (folder, false, "*", juce::File::findFilesAndDirectories))
        {
            const auto child = entry.getFile();

            if (child.getFileName() == ".DS_Store")
                continue;

            const bool prunable = entry.isDirectory()
                               && std::find (pruneSet.begin(), pruneSet.end(), child) != pruneSet.end()
                               && willBeEmptyAfterPruning (child, pruneSet);
            if (! prunable)
                return false;
        }

        return true;
    }

    int pathDepth (const juce::File& f)
    {
        return f.getFullPathName().retainCharacters ("/").length();
    }

    /** Version utilisateur : supprime .DS_Store puis rmdir. */
    bool pruneFolderIfEmpty (const juce::File& folder)
    {
        if (! isSafePruneFolder (folder) || ! isEffectivelyEmpty (folder))
            return false;

        folder.getChildFile (".DS_Store").deleteFile();
        return ::rmdir (folder.getFullPathName().toRawUTF8()) == 0;
    }

    /** Version root, ajoutée à la fin du script privilégié (après les déplacements). */
    juce::String privilegedPruneCommand (const juce::File& folder)
    {
        const auto q = shellQuote (folder.getFullPathName());
        return "if [ -d " + q + " ] && [ ! -L " + q + " ] && [ -z \"$(/bin/ls -A " + q
             + " | /usr/bin/grep -v '^\\.DS_Store$')\" ]; then /bin/rm -f " + q + "/.DS_Store; /bin/rmdir " + q + "; fi; ";
    }
} // namespace

//==============================================================================
OperationReport UninstallerMac::moveItemsToTrash (const std::vector<TrashItem>& items, const std::vector<juce::File>& roots)
{
    OperationReport report;
    report.requested = (int) items.size();

    NSFileManager* fm = [NSFileManager defaultManager];
    std::vector<juce::File> privileged;
    std::vector<juce::File> pruneCandidates;

    for (const auto& item : items)
        for (const auto& folder : item.pruneIfEmpty)
            if (folder != juce::File() && std::find (pruneCandidates.begin(), pruneCandidates.end(), folder) == pruneCandidates.end())
                pruneCandidates.push_back (folder);

    // Le plus profond d'abord : <Vendor>/Presets avant <Vendor>
    std::stable_sort (pruneCandidates.begin(), pruneCandidates.end(),
                      [] (const juce::File& a, const juce::File& b) { return pathDepth (a) > pathDepth (b); });

    //---------------------------------------------------- 1. sans privilèges
    for (const auto& item : items)
    {
        @autoreleasepool
        {
            const auto& file = item.file;
            juce::String reason;

            const bool safe = item.isPluginBundle ? isSafePluginPath (file, roots, reason)
                                                  : isSafeLeftoverPath (file, item.vendorKey, reason);
            if (! safe)
            {
                report.errors.add (file.getFileName() + " : " + reason);
                continue;
            }

            if (requiresAdministrator (file))
            {
                privileged.push_back (file);
                continue;
            }

            NSError* error = nil;

            if ([fm trashItemAtURL: toURL (file) resultingItemURL: nil error: &error])
            {
                ++report.succeeded;
                report.succeededPaths.add (file.getFullPathName());
            }
            else if (isPermissionError (error))
            {
                privileged.push_back (file);
            }
            else
            {
                report.errors.add (file.getFileName() + " : " + fromNS (error.localizedDescription));
            }
        }
    }

    // Dossiers <Vendor> à élaguer qui exigent root (parent non inscriptible) et sont déjà vides
    // ou le deviendront après le lot privilégié.
    std::vector<juce::File> privilegedPrunes;

    for (const auto& folder : pruneCandidates)
    {
        if (! isSafePruneFolder (folder) || canWrite (folder.getParentDirectory()))
            continue;

        if (! privileged.empty() || willBeEmptyAfterPruning (folder, pruneCandidates))
            privilegedPrunes.push_back (folder);
    }

    if (privileged.empty() && privilegedPrunes.empty())
    {
        for (const auto& folder : pruneCandidates)
            pruneFolderIfEmpty (folder);

        return report;
    }

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

    for (const auto& folder : privilegedPrunes)
        script << privilegedPruneCommand (folder);

    script << "exit 0";

    report.usedAdministratorPrivileges = true;
    const auto pr = runPrivilegedShell (script);

    if (! pr.executed)
    {
        if (! privileged.empty())
            reportPrivilegeFailure (report, pr, privileged.size());

        removeIfEmpty (staging);

        for (const auto& folder : pruneCandidates)
            pruneFolderIfEmpty (folder);

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

            ++report.succeeded;
            report.succeededPaths.add (privileged[i].getFullPathName());

            NSError* error = nil;

            if (! [fm trashItemAtURL: toURL (stagedTargets[i]) resultingItemURL: nil error: &error])
            {
                // Déjà retiré de son emplacement : on signale où il se trouve.
                report.leftovers.add (stagedTargets[i].getFullPathName());
                report.errors.add (privileged[i].getFileName() + txt (" : retiré, mais non envoyé à la Corbeille (")
                                   + fromNS (error.localizedDescription) + ")");
            }
        }
    }

    removeIfEmpty (staging);

    //---------------------------------------------------- 4. élagage des <Vendor> restants (droits utilisateur)
    for (const auto& folder : pruneCandidates)
        pruneFolderIfEmpty (folder);

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
