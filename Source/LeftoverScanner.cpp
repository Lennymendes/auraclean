// ==============================================================================
//  LeftoverScanner.cpp — AuraClean (NextDSP) — « Deep Clean »
// ==============================================================================

#include "LeftoverScanner.h"
#include "PluginScanner.h"
#include "UninstallerMac.h"

#include <fts.h>
#include <sys/stat.h>

#include <algorithm>
#include <climits>
#include <cstring>
#include <map>
#include <set>
#include <unordered_set>

namespace aura
{
juce::String leftoverCategoryLabel (LeftoverCategory c)
{
    switch (c)
    {
        case LeftoverCategory::ApplicationSupport: return "SUPPORT";
        case LeftoverCategory::Presets:            return "PRESETS";
        case LeftoverCategory::Preferences:        return juce::String::fromUTF8 ("PRÉFÉRENCES");
        case LeftoverCategory::Caches:             return "CACHE";
        case LeftoverCategory::Logs:               return "LOGS";
        case LeftoverCategory::Shared:             return juce::String::fromUTF8 ("PARTAGÉ");
        case LeftoverCategory::UserPresets:        return "PRESETS VST3";
        case LeftoverCategory::PluginSettings:     return juce::String::fromUTF8 ("RÉGLAGES LOGIC");
        case LeftoverCategory::Receipt:            return juce::String::fromUTF8 ("REÇU PKG");
    }
    return {};
}

namespace
{
    constexpr int kMaxOwnershipFiles = 20000;

    juce::String key (const juce::String& s) { return PluginScanner::normaliseKey (s); }

    juce::String stripExtension (const juce::String& name)
    {
        return name.containsChar ('.') ? name.upToLastOccurrenceOf (".", false, false) : name;
    }

    bool existsNoFollow (const juce::String& path)
    {
        struct stat st {};
        return ::lstat (path.toRawUTF8(), &st) == 0;
    }

    bool isPluginBundleName (const juce::String& name)
    {
        const auto lower = name.toLowerCase();
        return lower.endsWith (".vst3") || lower.endsWith (".component") || lower.endsWith (".aaxplugin")
            || lower.endsWith (".vst") || lower.endsWith (".clap");
    }

    /** Noms trop génériques pour identifier un plugin, même s'ils correspondent. */
    const std::set<juce::String>& genericKeys()
    {
        static const std::set<juce::String> keys {
            "audio", "plugins", "plugin", "presets", "preset", "settings", "data", "resources", "common", "shared",
            "library", "support", "vst", "vst3", "au", "aax", "components", "component", "default", "user", "users",
            "factory", "content", "documents", "music", "sounds", "samples", "logs", "log", "cache", "caches",
            "preferences", "temp", "tmp", "backup", "license", "licenses", "help", "manual", "manuals",
            "documentation", "midi", "impulses", "ir", "skins", "themes", "fonts", "config", "app", "application"
        };
        return keys;
    }

    /** Suffixes tolérés après le nom du plugin : « Pro-Q 3 Presets », « Serum Data »… */
    const juce::StringArray& nameSuffixes()
    {
        static const juce::StringArray suffixes { "presets", "preset", "data", "settings", "resources", "content",
                                                  "library", "factory", "support", "prefs", "preferences",
                                                  "log", "logs", "cache", "userpresets" };
        return suffixes;
    }

    //--------------------------------------------------------------------------
    struct Matcher
    {
        juce::String      vendorKey, pluginKey, comboKey;
        juce::StringArray bundleIds;    // en minuscules, >= 3 composants
        juce::StringArray domainTokens; // "fabfilter" de com.fabfilter.Pro-Q.3
        bool              nameUsable = false;

        explicit Matcher (const LeftoverQuery& q)
        {
            vendorKey  = key (q.vendorName);
            pluginKey  = key (q.pluginName);
            comboKey   = vendorKey + pluginKey;
            nameUsable = pluginKey.length() >= 3 && pluginKey != vendorKey && genericKeys().count (pluginKey) == 0;

            for (const auto& id : q.bundleIds)
            {
                const auto lower = id.trim().toLowerCase();
                auto parts = juce::StringArray::fromTokens (lower, ".", "");
                parts.removeEmptyStrings();

                if (parts.size() >= 3)
                {
                    bundleIds.addIfNotAlreadyThere (lower);

                    if (key (parts[1]).length() >= 3)
                        domainTokens.addIfNotAlreadyThere (key (parts[1]));
                }
            }
        }

        bool matchesBundleId (const juce::String& name) const
        {
            const auto lower = name.toLowerCase();

            for (const auto& id : bundleIds)
                if (lower == id || lower.startsWith (id + "."))
                    return true;

            return false;
        }

        bool matchesPluginName (const juce::String& name) const
        {
            if (! nameUsable)
                return false;

            for (const auto& k : { key (name), key (stripExtension (name)) })
            {
                if (k == pluginKey || (vendorKey.isNotEmpty() && k == comboKey))
                    return true;

                for (const auto& suffix : nameSuffixes())
                    if (k == pluginKey + suffix || (vendorKey.isNotEmpty() && k == comboKey + suffix))
                        return true;
            }

            return false;
        }

        bool matches (const juce::String& name) const { return matchesBundleId (name) || matchesPluginName (name); }

        bool isVendorFolderName (const juce::String& name) const
        {
            return vendorKey.length() >= 2 && key (name) == vendorKey;
        }

        bool receiptLooksRelated (const juce::String& receiptId) const
        {
            const auto k = key (receiptId);

            if (vendorKey.length() >= 3 && k.contains (vendorKey))
                return true;

            for (const auto& token : domainTokens)
                if (k.contains (token))
                    return true;

            return nameUsable && pluginKey.length() >= 5 && k.contains (pluginKey);
        }
    };

    //--------------------------------------------------------------------------
    struct Collector
    {
        std::vector<LeftoverItem>&       items;
        std::unordered_set<std::string>& seen;
        juce::String                     vendorKey;
        juce::File                       home;

        void add (const juce::File& file, LeftoverCategory category, const juce::Array<juce::File>& prune,
                  const juce::String& detail)
        {
            if (LeftoverScanner::isProtectedPath (file) || isPluginBundleName (file.getFileName()))
                return;

            if (! seen.insert (file.getFullPathName().toStdString()).second)
                return;

            LeftoverItem item;
            item.file          = file;
            item.category      = category;
            item.isDirectory   = file.isDirectory() && ! file.isSymbolicLink();
            item.sizeBytes     = LeftoverScanner::diskSize (file);
            item.requiresAdmin = UninstallerMac::requiresAdministrator (file);
            item.pruneIfEmpty  = prune;
            item.vendorKey     = vendorKey;
            item.detail        = detail;
            item.isUserContent = category == LeftoverCategory::UserPresets
                              || category == LeftoverCategory::PluginSettings
                              || (category == LeftoverCategory::Presets && file.isAChildOf (home));
            items.push_back (std::move (item));
        }
    };

    juce::Array<juce::File> childrenOf (const juce::File& folder)
    {
        juce::Array<juce::File> out;

        if (! folder.isDirectory())
            return out;

        for (const auto& entry : juce::RangedDirectoryIterator (folder, false, "*",
                                                                juce::File::findFilesAndDirectories | juce::File::ignoreHiddenFiles))
            out.add (entry.getFile());

        return out;
    }

    /** Dans un dossier <Vendor> : <Vendor>/<Plugin> et <Vendor>/<Presets|Settings|Data…>/<Plugin>. */
    void searchVendorFolder (const juce::File& vendorFolder, LeftoverCategory category, const Matcher& m, Collector& out)
    {
        if (! vendorFolder.isDirectory() || vendorFolder.isSymbolicLink())
            return;

        const auto byName = juce::String::fromUTF8 ("nom du plugin");

        for (const auto& sub : childrenOf (vendorFolder))
        {
            const auto subName = sub.getFileName();

            if (m.matches (subName))
            {
                out.add (sub, category, { vendorFolder }, byName);
                continue;
            }

            // Profondeur 2 : ~/Documents/FabFilter/Presets/Pro-Q 3
            if (LeftoverScanner::isIntermediateFolderName (subName) && sub.isDirectory() && ! sub.isSymbolicLink())
                for (const auto& leaf : childrenOf (sub))
                    if (m.matches (leaf.getFileName()))
                        out.add (leaf, category, { sub, vendorFolder }, byName);
        }
    }

    /** Recherche par nom : <Root>/<Plugin>, <Root>/<bundle id>*, <Root>/<Vendor>/[<SubDir>/]<Plugin>. */
    void searchByName (const LeftoverScanner::SearchRoot& root, const Matcher& m, Collector& out)
    {
        for (const auto& child : childrenOf (root.folder))
        {
            const auto name = child.getFileName();

            if (m.isVendorFolderName (name))
            {
                // Règle anti-collision : on ne descend que pour cibler <Vendor>/…/<Plugin>
                searchVendorFolder (child, root.category, m, out);
                continue;
            }

            if (root.vendorOnly || LeftoverScanner::isSharedFolderName (name))
                continue;

            if (m.matches (name))
                out.add (child, root.category, {}, m.matchesBundleId (name) ? juce::String ("bundle id")
                                                                            : juce::String::fromUTF8 ("nom du plugin"));
        }
    }

    //--------------------------------------------------------------------------
    //  Reçus d'installation
    //--------------------------------------------------------------------------
    struct Receipt
    {
        juce::String id;
        juce::File   plist, bom;
    };

    std::vector<Receipt> listReceipts()
    {
        std::vector<Receipt> out;
        const auto folder = LeftoverScanner::receiptsFolder();

        for (const auto& entry : juce::RangedDirectoryIterator (folder, false, "*.plist", juce::File::findFiles))
        {
            Receipt r;
            r.plist = entry.getFile();
            r.id    = r.plist.getFileNameWithoutExtension();
            r.bom   = folder.getChildFile (r.id + ".bom");

            if (! r.id.startsWithIgnoreCase ("com.apple."))
                out.push_back (std::move (r));
        }

        return out;
    }

    /** Chemins absolus listés par « lsbom -fls » (fichiers + liens), préfixés par InstallPrefixPath. */
    juce::StringArray readBomPaths (const Receipt& r)
    {
        juce::StringArray paths;

        if (! r.bom.existsAsFile())
            return paths;

        auto prefix = PluginScanner::readPropertyList (r.plist).getProperty ("InstallPrefixPath", juce::var()).toString().trim();

        if (! prefix.startsWithChar ('/'))
            prefix = "/" + prefix;

        prefix = prefix.trimCharactersAtEnd ("/");

        juce::ChildProcess process;

        if (! process.start (juce::StringArray { "/usr/bin/lsbom", "-fls", r.bom.getFullPathName() },
                             juce::ChildProcess::wantStdOut))
            return paths;

        const auto output = process.readAllProcessOutput();
        process.waitForProcessToFinish (10000);

        for (auto line : juce::StringArray::fromLines (output))
        {
            line = line.trimEnd();

            if (line.isEmpty() || line == ".")
                continue;

            if (line.startsWith ("./"))
                line = line.substring (2);

            paths.add (prefix + "/" + line);
        }

        return paths;
    }

    /** "/Library/Audio/Plug-Ins/VST3/X.vst3/Contents/Info.plist" → "/Library/Audio/Plug-Ins/VST3/X.vst3" */
    juce::String bundleRootOf (const juce::String& path)
    {
        const auto lower = path.toLowerCase();

        for (const char* ext : { ".vst3", ".component", ".aaxplugin", ".clap", ".vst" })
        {
            const auto e = juce::String (ext);
            int index = lower.indexOf (e + "/");

            if (index < 0 && lower.endsWith (e))
                index = lower.length() - e.length();

            if (index >= 0)
                return path.substring (0, index + e.length());
        }

        return {};
    }

    /** Vrai si chaque fichier présent sous 'folder' (hors .DS_Store) est listé dans le .bom. */
    bool ownedByBom (const juce::File& folder, const std::unordered_set<std::string>& bomFiles)
    {
        const auto path = folder.getFullPathName().toStdString();
        char* paths[] = { const_cast<char*> (path.c_str()), nullptr };
        FTS* fts = ::fts_open (paths, FTS_PHYSICAL | FTS_NOCHDIR, nullptr);

        if (fts == nullptr)
            return false;

        int  count = 0;
        bool owned = true;

        while (FTSENT* e = ::fts_read (fts))
        {
            if (e->fts_info != FTS_F && e->fts_info != FTS_SL && e->fts_info != FTS_SLNONE)
                continue;

            if (std::strcmp (e->fts_name, ".DS_Store") == 0)
                continue;

            if (++count > kMaxOwnershipFiles || bomFiles.count (e->fts_path) == 0)
            {
                owned = false;
                break;
            }
        }

        ::fts_close (fts);
        return owned && count > 0;
    }

    void searchReceipts (const std::vector<LeftoverQuery>& queries,
                         const std::vector<juce::File>& allDeleted,
                         std::vector<std::vector<LeftoverItem>>& results,
                         std::unordered_set<std::string>& seen,
                         const std::atomic<bool>* cancel)
    {
        const auto receipts = listReceipts();
        const auto roots    = LeftoverScanner::searchRoots();
        const auto home     = juce::File::getSpecialLocation (juce::File::userHomeDirectory);

        std::set<juce::String> deletedPaths, deletedNames;

        for (const auto& f : allDeleted)
        {
            deletedPaths.insert (f.getFullPathName());
            deletedNames.insert (f.getFileName().toLowerCase());
        }

        std::set<juce::String> usedReceipts;

        for (size_t qi = 0; qi < queries.size(); ++qi)
        {
            const auto& q = queries[qi];
            const Matcher m (q);
            Collector out { results[qi], seen, m.vendorKey, home };

            std::set<juce::String> ownPaths, ownNames;
            for (const auto& f : q.bundles)
            {
                ownPaths.insert (f.getFullPathName());
                ownNames.insert (f.getFileName().toLowerCase());
            }

            for (const auto& r : receipts)
            {
                if (cancel != nullptr && cancel->load())
                    return;

                if (usedReceipts.count (r.id) != 0 || ! m.receiptLooksRelated (r.id))
                    continue;

                const auto paths = readBomPaths (r);

                if (paths.isEmpty())
                    continue;

                // 1. Validation par les bundles listés dans le .bom
                bool belongsToQuery = false, keepsOtherPlugin = false;
                std::set<juce::String> bundleRoots;

                for (const auto& p : paths)
                    if (const auto b = bundleRootOf (p); b.isNotEmpty())
                        bundleRoots.insert (b);

                for (const auto& b : bundleRoots)
                {
                    const auto name = b.fromLastOccurrenceOf ("/", false, false).toLowerCase();

                    if (existsNoFollow (b))
                    {
                        if (deletedPaths.count (b) == 0)
                            keepsOtherPlugin = true;  // un plugin de ce .pkg reste installé
                        else if (ownPaths.count (b) != 0)
                            belongsToQuery = true;
                    }
                    else if (ownNames.count (name) != 0)
                    {
                        belongsToQuery = true;  // bundle déplacé (ex. dossier Disabled)
                    }
                }

                if (! belongsToQuery || keepsOtherPlugin)
                    continue;

                usedReceipts.insert (r.id);

                // 2. Dossiers spécifiques déposés par le .pkg
                std::unordered_set<std::string> bomFiles;
                for (const auto& p : paths)
                    bomFiles.insert (p.toStdString());

                std::set<juce::String> evaluated;
                std::map<juce::String, bool> ownershipCache;

                auto isOwned = [&] (const juce::File& f)
                {
                    const auto [it, inserted] = ownershipCache.try_emplace (f.getFullPathName(), false);
                    if (inserted)
                        it->second = ownedByBom (f, bomFiles);
                    return it->second;
                };
                const auto detail = juce::String::fromUTF8 ("reçu ") + r.id;

                for (const auto& p : paths)
                {
                    if (bundleRootOf (p).isNotEmpty())
                        continue;

                    for (const auto& root : roots)
                    {
                        const auto rootPath = root.folder.getFullPathName();

                        if (! p.startsWith (rootPath + "/"))
                            continue;

                        auto comps = juce::StringArray::fromTokens (p.substring (rootPath.length() + 1), "/", "");
                        comps.removeEmptyStrings();

                        if (comps.isEmpty() || LeftoverScanner::isSharedFolderName (comps[0]))
                            break;

                        if (root.vendorOnly && ! m.isVendorFolderName (comps[0]))
                            break;

                        const auto level1 = root.folder.getChildFile (comps[0]);
                        juce::File candidate;
                        juce::Array<juce::File> prune;
                        bool ownershipAllowed = true;

                        if (m.isVendorFolderName (comps[0]))
                        {
                            // <Vendor>/<X> : jamais le dossier de marque lui-même
                            if (comps.size() < 2)
                                break;

                            if (comps.size() >= 3 && LeftoverScanner::isIntermediateFolderName (comps[1]))
                            {
                                // <Vendor>/Presets/<Plugin>
                                const auto intermediate = level1.getChildFile (comps[1]);
                                candidate = intermediate.getChildFile (comps[2]);
                                prune     = { intermediate, level1 };
                            }
                            else
                            {
                                candidate = level1.getChildFile (comps[1]);
                                prune     = { level1 };
                            }
                        }
                        else if (comps.size() >= 2 && ! m.matches (comps[0]) && ! isOwned (level1))
                        {
                            // Dossier mixte (d'autres produits y vivent) : uniquement un enfant nommé comme le plugin
                            candidate        = level1.getChildFile (comps[1]);
                            ownershipAllowed = false;
                        }
                        else
                        {
                            candidate = level1;
                        }

                        if (LeftoverScanner::isSharedFolderName (candidate.getFileName()))
                            break;  // <Vendor>/Shared, <Vendor>/Presets… : communs à la marque

                        if (! evaluated.insert (candidate.getFullPathName()).second)
                            break;

                        if (! existsNoFollow (candidate.getFullPathName()))
                            break;

                        const bool isDir = candidate.isDirectory() && ! candidate.isSymbolicLink();
                        const bool owned = ownershipAllowed
                                        && (isDir ? isOwned (candidate)
                                                  : bomFiles.count (candidate.getFullPathName().toStdString()) != 0);

                        if (owned || m.matches (candidate.getFileName()))
                            out.add (candidate, root.category, prune, detail);

                        break;
                    }
                }

                // 3. Le reçu lui-même (équivalent de « pkgutil --forget »)
                out.add (r.plist, LeftoverCategory::Receipt, {}, r.id);

                if (r.bom.existsAsFile())
                    out.add (r.bom, LeftoverCategory::Receipt, {}, r.id);
            }
        }
    }

    /** Retire les éléments contenus dans un autre élément déjà retenu. */
    void removeNested (std::vector<LeftoverItem>& items)
    {
        std::sort (items.begin(), items.end(), [] (const LeftoverItem& a, const LeftoverItem& b)
        {
            return a.file.getFullPathName() < b.file.getFullPathName();
        });

        std::vector<LeftoverItem> kept;

        for (auto& item : items)
        {
            const auto path = item.file.getFullPathName();

            if (! kept.empty() && kept.back().isDirectory && path.startsWith (kept.back().file.getFullPathName() + "/"))
                continue;

            kept.push_back (std::move (item));
        }

        items = std::move (kept);
    }
} // namespace

//==============================================================================
std::vector<LeftoverScanner::SearchRoot> LeftoverScanner::searchRoots()
{
    const auto home = juce::File::getSpecialLocation (juce::File::userHomeDirectory);
    std::vector<SearchRoot> roots;

    for (const auto& [base, user] : { std::pair<juce::File, bool> { home.getChildFile ("Library"), true },
                                      std::pair<juce::File, bool> { juce::File ("/Library"), false } })
    {
        roots.push_back ({ base.getChildFile ("Application Support"), LeftoverCategory::ApplicationSupport, user });
        roots.push_back ({ base.getChildFile ("Audio/Presets"),       LeftoverCategory::Presets,            user });
        roots.push_back ({ base.getChildFile ("Preferences/ByHost"),  LeftoverCategory::Preferences,        user });
        roots.push_back ({ base.getChildFile ("Preferences"),         LeftoverCategory::Preferences,        user });
        roots.push_back ({ base.getChildFile ("Caches"),              LeftoverCategory::Caches,             user });
        roots.push_back ({ base.getChildFile ("Logs"),                LeftoverCategory::Logs,               user });
    }

    // Traces laissées par les versions standalone et les interfaces web embarquées (par bundle id)
    for (const char* sub : { "Saved Application State", "HTTPStorages", "WebKit", "Application Scripts" })
        roots.push_back ({ home.getChildFile ("Library").getChildFile (sub), LeftoverCategory::Caches, true });

    // Contenu partagé installé hors des dossiers de plugins
    roots.push_back ({ juce::File ("/Library/Audio/Impulse Responses"),        LeftoverCategory::ApplicationSupport, false, true });
    roots.push_back ({ juce::File ("/Library/Audio/Sounds"),                   LeftoverCategory::ApplicationSupport, false, true });
    roots.push_back ({ juce::File ("/Library/Application Support/Avid/Audio/Plug-In Settings"), LeftoverCategory::Presets, false });

    roots.push_back ({ juce::File ("/Users/Shared"),                                  LeftoverCategory::Shared,         false });
    roots.push_back ({ home.getChildFile ("Documents/VST3 Presets"),                  LeftoverCategory::UserPresets,    true });
    roots.push_back ({ home.getChildFile ("Music/Audio Music Apps/Plug-In Settings"), LeftoverCategory::PluginSettings, true });

    // Dossiers de marque créés directement dans Documents / Musique (FabFilter, iZotope, NI, Xfer…).
    // Placés APRÈS les racines plus précises ci-dessus : la première racine correspondante gagne.
    roots.push_back ({ home.getChildFile ("Documents"), LeftoverCategory::Presets, true, true });
    roots.push_back ({ home.getChildFile ("Music"),     LeftoverCategory::Presets, true, true });
    return roots;
}

juce::File LeftoverScanner::receiptsFolder()
{
    return juce::File ("/var/db/receipts");
}

bool LeftoverScanner::isIntermediateFolderName (const juce::String& name)
{
    static const std::set<juce::String> intermediates { "presets", "userpresets", "pluginsettings", "settings", "data",
                                                         "factorypresets", "userdata" };
    return intermediates.count (key (name)) != 0;
}

bool LeftoverScanner::isSharedFolderName (const juce::String& name)
{
    static const std::set<juce::String> shared {
        "avid", "pace", "paceantipiracy", "ilok", "ilokmanager", "eden", "steinberg", "apple", "audio", "plugins",
        "plugin", "plugins", "presets", "components", "vst3", "vst", "aax", "mididrivers", "sounds", "coreaudio",
        "halplugins", "byhost", "crashreporter", "diagnosticreports", "applicationsupport", "nativeaccess",
        "nativeinstruments2", "receipts", "launchagents", "launchdaemons", "frameworks", "extensions"
    };

    return name.startsWithIgnoreCase ("com.apple.") || name.startsWithChar ('.')
        || shared.count (key (name)) != 0 || genericKeys().count (key (name)) != 0;
}

bool LeftoverScanner::isProtectedPath (const juce::File& file)
{
    const auto path = file.getFullPathName();

    if (path.isEmpty() || path == "/" || ! path.startsWithChar ('/'))
        return true;

    if (file.getFileName().startsWithIgnoreCase ("com.apple."))
        return true;

    auto isRootOrAncestor = [&path] (const juce::File& root)
    {
        const auto r = root.getFullPathName();
        return path == r || r.startsWith (path + "/");
    };

    for (const auto& root : searchRoots())
        if (isRootOrAncestor (root.folder))
            return true;

    if (isRootOrAncestor (receiptsFolder()))
        return true;

    // Les dossiers de plugins sont gérés par le scan principal, jamais comme résidus
    for (const auto& pluginRoot : PluginScanner::allowedRoots())
    {
        const auto r = pluginRoot.getFullPathName();
        if (path == r || path.startsWith (r + "/") || r.startsWith (path + "/"))
            return true;
    }

    // Noms partagés au premier niveau d'une racine
    const auto parent = file.getParentDirectory().getFullPathName();

    for (const auto& root : searchRoots())
        if (parent == root.folder.getFullPathName() && isSharedFolderName (file.getFileName()))
            return true;

    return false;
}

juce::int64 LeftoverScanner::diskSize (const juce::File& file)
{
    const auto path = file.getFullPathName().toStdString();
    char* paths[] = { const_cast<char*> (path.c_str()), nullptr };
    FTS* fts = ::fts_open (paths, FTS_PHYSICAL | FTS_NOCHDIR, nullptr);

    if (fts == nullptr)
        return 0;

    juce::int64 total = 0;

    while (FTSENT* e = ::fts_read (fts))
    {
        if (e->fts_info == FTS_DP || e->fts_statp == nullptr)
            continue;  // les dossiers sont comptés en pré-ordre (FTS_D)

        if (e->fts_info == FTS_F || e->fts_info == FTS_SL || e->fts_info == FTS_SLNONE
            || e->fts_info == FTS_D || e->fts_info == FTS_DEFAULT)
            total += (juce::int64) e->fts_statp->st_blocks * 512;
    }

    ::fts_close (fts);
    return total;
}

std::vector<std::vector<LeftoverItem>> LeftoverScanner::scan (const std::vector<LeftoverQuery>& queries,
                                                              const std::vector<juce::File>& allDeletedBundles,
                                                              const std::atomic<bool>* shouldCancel,
                                                              Options options)
{
    std::vector<std::vector<LeftoverItem>> results (queries.size());
    std::unordered_set<std::string> seen;
    const auto home  = juce::File::getSpecialLocation (juce::File::userHomeDirectory);
    auto roots = searchRoots();

    if (! options.includeDocuments)
    {
        const auto documents = home.getChildFile ("Documents");
        roots.erase (std::remove_if (roots.begin(), roots.end(), [&documents] (const SearchRoot& r)
                     { return r.folder == documents || r.folder.isAChildOf (documents); }),
                     roots.end());
    }

    for (size_t qi = 0; qi < queries.size(); ++qi)
    {
        if (shouldCancel != nullptr && shouldCancel->load())
            return results;

        const Matcher m (queries[qi]);
        Collector out { results[qi], seen, m.vendorKey, home };

        for (const auto& root : roots)
            searchByName (root, m, out);
    }

    if (options.includeReceipts)
        searchReceipts (queries, allDeletedBundles, results, seen, shouldCancel);

    for (auto& items : results)
        removeNested (items);

    return results;
}

} // namespace aura
