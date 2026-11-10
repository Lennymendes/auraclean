#pragma once
// ==============================================================================
//  UpdateChecker.h — AuraClean (NextDSP)
//
//  Vérification de mise à jour silencieuse, sur un thread dédié :
//    GET https://raw.githubusercontent.com/Lennymendes/auraclean-updates/main/version.json
//    { "latest_version": "1.0.1",
//      "download_url":   "https://nextdsp.com/downloads/AuraClean-1.0.1.dmg",
//      "release_notes":  "Correction de bugs" }
//
//  Sécurité : réponse plafonnée à 64 Ko, HTTPS obligatoire pour le flux ET pour
//  le lien de téléchargement, qui doit en plus pointer vers un domaine autorisé
//  (un JSON compromis ne peut donc pas envoyer l'utilisateur ailleurs).
// ==============================================================================

#include <juce_core/juce_core.h>

#include <functional>

namespace aura
{
namespace UpdateConfig
{
    /** Flux JSON statique hébergé sur GitHub (dépôt Lennymendes/auraclean-updates, fichier version.json).
        Modèle : docs/version.json. */
    inline constexpr const char* feedUrl = "https://raw.githubusercontent.com/Lennymendes/auraclean-updates/main/version.json";

    /** Domaines acceptés pour download_url (sous-domaines inclus : www.nextdsp.com, *.lemonsqueezy.com…). */
    inline const juce::StringArray& allowedDownloadHosts()
    {
        static const juce::StringArray hosts { "nextdsp.com",
                                               "lemonsqueezy.com",
                                               "paddle.com",
                                               "github.com",
                                               "raw.githubusercontent.com" };
        return hosts;
    }

    inline constexpr int timeoutMs        = 8000;
    inline constexpr int maxResponseBytes = 64 * 1024;
}

struct UpdateInfo
{
    enum class Status
    {
        UpToDate,
        UpdateAvailable,
        Error
    };

    Status       status = Status::Error;
    juce::String currentVersion;
    juce::String latestVersion;
    juce::URL    downloadUrl;
    juce::String releaseNotes;
    juce::String errorMessage;  // lisible (FR), seulement si status == Error
};

//==============================================================================
/** Compare deux versions sémantiques : "1.0.1" > "1.0.0", "1.10" > "1.9", "v2" == "2.0.0",
    "1.1.0-beta.2" < "1.1.0". Retourne -1, 0 ou 1. */
int compareVersions (const juce::String& a, const juce::String& b);

//==============================================================================
class UpdateChecker : private juce::Thread
{
public:
    /** Appelé sur le MESSAGE THREAD (via MessageManager::callAsync). */
    using Callback = std::function<void (const UpdateInfo&)>;

    UpdateChecker (juce::String currentVersion, juce::URL feedUrl = juce::URL (UpdateConfig::feedUrl));
    ~UpdateChecker() override;

    /** Lance une vérification (ignorée si une autre est déjà en cours). */
    void check (Callback onResult);

    bool isChecking() const noexcept { return isThreadRunning(); }

    /** Analyse pure du flux JSON (testable sans réseau). */
    static UpdateInfo parseFeed (const juce::String& json, const juce::String& currentVersion);

    /** HTTPS + domaine autorisé + pas d'identifiants dans l'URL. */
    static bool isTrustedDownloadUrl (const juce::URL&);

    //==========================================================================
    /** Préférences de mise à jour (version ignorée par l'utilisateur). */
    static juce::String skippedVersion();
    static void         setSkippedVersion (const juce::String& version);

    /** « Rechercher automatiquement au lancement » (activé par défaut). */
    static bool isAutoCheckEnabled();
    static void setAutoCheckEnabled (bool enabled);

private:
    void run() override;

    const juce::String currentVersion;
    const juce::URL    feed;
    Callback           callback;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (UpdateChecker)
};

} // namespace aura
