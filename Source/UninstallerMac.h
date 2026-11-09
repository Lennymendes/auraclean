#pragma once
// ==============================================================================
//  UninstallerMac.h — AuraClean (NextDSP)
//
//  Interface C++ pure ; l'implémentation (UninstallerMac.mm) utilise Foundation.
//
//  Stratégie de privilèges :
//    1. Tentative sans droits (NSFileManager trashItemAtURL / rename(2)).
//    2. Tout ce qui échoue pour cause de permissions est regroupé dans UN SEUL
//       script shell exécuté via "do shell script ... with administrator
//       privileges" → une seule demande de mot de passe pour tout le lot.
//    3. Pour la Corbeille, le script root déplace les bundles dans un dossier de
//       transit appartenant à l'utilisateur (chown), puis l'app les envoie dans
//       la Corbeille avec ses propres droits (aucun accès TCC à ~/.Trash requis).
//
//  Toute opération est confinée aux dossiers de plugins connus (allowedRoots).
// ==============================================================================

#include <juce_core/juce_core.h>

#include <vector>

namespace aura
{
struct BundleMove
{
    juce::File source;
    juce::File destinationFolder;
    bool       renameOnConflict = true;  // false : échec si la cible existe (réactivation)
};

struct OperationReport
{
    int               requested = 0;
    int               succeeded = 0;
    juce::StringArray succeededPaths;  // chemins d'ORIGINE traités avec succès
    juce::StringArray errors;          // messages lisibles (FR)
    juce::StringArray leftovers;       // éléments restés en transit (à traiter à la main)
    bool usedAdministratorPrivileges = false;
    bool authorisationCancelled      = false;
};

namespace UninstallerMac
{
    /** Envoie les bundles à la Corbeille de l'utilisateur. Bloquant, à appeler hors du message thread. */
    OperationReport moveToTrash (const std::vector<juce::File>& bundles, const std::vector<juce::File>& allowedRoots);

    /** Déplace les bundles (quarantaine ou réactivation). Bloquant, hors du message thread. */
    OperationReport moveBundles (const std::vector<BundleMove>& moves, const std::vector<juce::File>& allowedRoots);

    /** Vérifie extension, absence de "..", caractères de contrôle et appartenance à un dossier autorisé. */
    bool isSafePluginPath (const juce::File& bundle, const std::vector<juce::File>& allowedRoots, juce::String& reason);

    /** Vrai si l'opération demandera un mot de passe administrateur. */
    bool requiresAdministrator (const juce::File& bundle, const juce::File& destinationFolder = {});

    /** Force macOS à reconstruire le cache des Audio Units (AudioComponentRegistrar). */
    void refreshAudioUnitRegistrar();

    /** Barre de titre native en mode sombre, cohérente avec la AuraSuite. Message thread uniquement. */
    void applyDarkAppearance();
}

} // namespace aura
