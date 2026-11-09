#pragma once
// ==============================================================================
//  LeftoverScanner.h — AuraClean (NextDSP) — « Deep Clean »
//
//  Recherche les fichiers résiduels d'un plugin dans /Library et ~/Library
//  (Application Support, Audio/Presets, Preferences + ByHost, Caches, Logs),
//  /Users/Shared, ~/Documents/VST3 Presets, ~/Music/Audio Music Apps/Plug-In
//  Settings, ainsi que les reçus d'installation /var/db/receipts (.bom + .plist)
//  dont le contenu (lsbom -fls) révèle les dossiers déposés par le .pkg.
//
//  Règles de sécurité appliquées ici ET revérifiées dans UninstallerMac.mm :
//    - jamais un dossier qui porte uniquement le nom de la marque
//      (Application Support/<Vendor>) : seulement <Vendor>/<PluginName> ;
//    - jamais une racine de recherche, un ancêtre de racine, un dossier de
//      plugins, ni un élément « com.apple.* » ou un nom partagé connu (Avid,
//      PACE, iLok, Steinberg…) ;
//    - un dossier déduit d'un .bom n'est retenu que si son nom correspond au
//      plugin OU si TOUS les fichiers qu'il contient proviennent de ce .bom ;
//    - un reçu n'est retenu que s'il référence un bundle supprimé, et aucun
//      bundle encore installé qui ne fait pas partie de la suppression.
//
//  La règle « tous les formats cochés » est appliquée par l'appelant
//  (MainComponent) : on n'interroge ce scanner que pour ces plugins-là.
// ==============================================================================

#include <juce_core/juce_core.h>

#include <atomic>
#include <vector>

namespace aura
{
enum class LeftoverCategory
{
    ApplicationSupport,
    Presets,
    Preferences,
    Caches,
    Logs,
    Shared,
    UserPresets,     // ~/Documents/VST3 Presets
    PluginSettings,  // ~/Music/Audio Music Apps/Plug-In Settings (Logic)
    Receipt          // /var/db/receipts/*.bom|*.plist
};

juce::String leftoverCategoryLabel (LeftoverCategory);

struct LeftoverQuery
{
    juce::String            pluginName;
    juce::String            vendorName;
    juce::StringArray       bundleIds;  // CFBundleIdentifier de chaque format
    std::vector<juce::File> bundles;    // bundles supprimés pour CE plugin
};

struct LeftoverItem
{
    juce::File       file;
    LeftoverCategory category = LeftoverCategory::ApplicationSupport;
    juce::int64      sizeBytes = 0;  // taille réellement occupée sur disque
    bool             isDirectory   = false;
    bool             requiresAdmin = false;
    bool             isUserContent = false;  // presets utilisateur : décochés par défaut
    juce::File       pruneIfEmpty;           // dossier <Vendor> à retirer s'il devient vide
    juce::String     vendorKey;              // clé normalisée de la marque (garde-fou)
    juce::String     detail;                 // origine : « nom », « reçu com.x.pkg »…
};

class LeftoverScanner
{
public:
    struct SearchRoot
    {
        juce::File       folder;
        LeftoverCategory category;
        bool             userDomain;
    };

    static std::vector<SearchRoot> searchRoots();
    static juce::File receiptsFolder();

    /** Un vecteur de résultats par requête (même ordre). Bloquant : hors message thread.
        @param allDeletedBundles  tous les bundles supprimés dans ce lot (suites multi-plugins). */
    static std::vector<std::vector<LeftoverItem>> scan (const std::vector<LeftoverQuery>& queries,
                                                        const std::vector<juce::File>& allDeletedBundles,
                                                        const std::atomic<bool>* shouldCancel = nullptr);

    /** Racines, ancêtres de racines, dossiers de plugins, com.apple.*, noms partagés connus. */
    static bool isProtectedPath (const juce::File&);

    /** Nom partagé à ne jamais supprimer au premier niveau d'une racine (Avid, PACE, iLok…). */
    static bool isSharedFolderName (const juce::String& name);

    /** Taille occupée sur disque (blocs alloués), récursive, sans suivre les liens. */
    static juce::int64 diskSize (const juce::File&);
};

} // namespace aura
