#pragma once
// ==============================================================================
//  LicenseManager.h — AuraClean (NextDSP)
//
//  Modèle « Scan gratuit / Nettoyage sous licence » via l'API License de
//  Lemon Squeezy :
//    - activation    POST https://api.lemonsqueezy.com/v1/licenses/activate
//                    license_key + instance_name (nom du Mac)
//    - désactivation POST https://api.lemonsqueezy.com/v1/licenses/deactivate
//                    license_key + instance_id
//  Les requêtes tournent sur un thread dédié, jamais sur le thread UI.
//
//  Hors-ligne : après activation, la clé, l'instance.id et meta.product_id sont
//  écrits dans ~/Library/Application Support/NextDSP/AuraClean/license.dat,
//  signés par SHA-256 (IOPlatformUUID du Mac + champs + sel interne). Au lancement
//  la signature est vérifiée localement : aucun accès réseau, et le fichier
//  copié sur un autre Mac est refusé.
// ==============================================================================

#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>

#include <functional>

namespace aura
{
namespace LicenseConfig
{
    inline constexpr const char* checkoutUrl   = "https://nextdsp.lemonsqueezy.com/checkout/buy/726d7600-44e5-42f5-b7bf-04f4183b0750";
    inline constexpr const char* activateUrl   = "https://api.lemonsqueezy.com/v1/licenses/activate";
    inline constexpr const char* deactivateUrl = "https://api.lemonsqueezy.com/v1/licenses/deactivate";

    /** meta.product_name doit contenir ce texte (insensible à la casse). */
    inline constexpr const char* productNameMatch = "AuraClean";

    inline constexpr const char* priceLabel  = "14,99 €";
    inline constexpr int         maxMachines = 3;
    inline constexpr int         timeoutMs   = 15000;

    inline void openCheckout() { juce::URL (checkoutUrl).launchInDefaultBrowser(); }
}

/** Contenu (signé) du fichier license.dat. */
struct StoredLicense
{
    juce::String licenseKey;
    juce::String instanceId;
    juce::String instanceName;
    juce::String productId;
    juce::String productName;
    juce::String activatedAt;  // ISO 8601
};

struct LicenseResult
{
    enum class Status
    {
        Activated,
        Deactivated,
        InvalidFormat,
        InvalidKey,
        Expired,
        Disabled,
        WrongProduct,
        LimitReached,
        NetworkError,
        ServerError,
        StorageError
    };

    Status       status = Status::ServerError;
    juce::String message;  // français, prêt à afficher

    bool ok() const noexcept { return status == Status::Activated || status == Status::Deactivated; }
};

//==============================================================================
class LicenseManager : private juce::Thread
{
public:
    using Callback = std::function<void (const LicenseResult&)>;

    /** Charge et vérifie license.dat (aucun accès réseau). */
    LicenseManager();
    ~LicenseManager() override;

    bool isActivated() const noexcept { return activated; }
    bool isBusy() const noexcept      { return isThreadRunning(); }

    /** Problème rencontré au chargement (fichier altéré ou copié d'un autre Mac), sinon vide. */
    const juce::String& loadProblem() const noexcept { return loadIssue; }

    const StoredLicense& current() const noexcept { return license; }
    juce::String maskedKey() const { return maskKey (license.licenseKey); }

    /** Activation en ligne. Le callback est appelé sur le MESSAGE THREAD. */
    void activate (const juce::String& rawKey, Callback onDone);

    /** Libère ce Mac côté Lemon Squeezy puis supprime license.dat. Callback sur le message thread. */
    void deactivate (Callback onDone);

    std::function<void()> onStateChanged;  // message thread

    //==========================================================================
    // Fonctions pures (testables sans réseau)
    static juce::String normaliseKey (const juce::String& raw);
    static bool         looksLikeKey (const juce::String& normalisedKey);
    static juce::String maskKey (const juce::String& key);

    /** Analyse la réponse /activate : contrôle activated, statut ET nom de produit. */
    static LicenseResult interpretActivation (const juce::var& json, int httpStatus, StoredLicense& out);
    static LicenseResult interpretDeactivation (const juce::var& json, int httpStatus);

    static juce::String computeSignature (const StoredLicense&, const juce::String& machineId);
    static juce::String serialise (const StoredLicense&, const juce::String& machineId);
    static bool         parseAndVerify (const juce::String& fileContent, const juce::String& machineId,
                                        StoredLicense& out, juce::String& problem);

    static juce::File   licenseFile();
    static juce::String machineIdentifier();  // IOPlatformUUID (IOKit), repli SystemStats
    static juce::String instanceName();       // nom du Mac, ex. « MacBook Pro de Zeyn »

private:
    enum class Job { None, Activate, Deactivate };

    void run() override;
    void finish (LicenseResult, bool newActivationState, StoredLicense);

    static LicenseResult post (const char* endpoint, const juce::StringPairArray& fields,
                               juce::var& json, int& status, juce::Thread& thread);

    bool          activated = false;
    StoredLicense license;
    juce::String  loadIssue;

    // Créée dans le constructeur (message thread) : JUCE initialise ce maître paresseusement, pas thread-safe
    juce::WeakReference<LicenseManager> selfRef;

    // État du job en cours (écrit sur le message thread avant startThread)
    Job          job = Job::None;
    juce::String pendingKey;
    Callback     pendingCallback;

    JUCE_DECLARE_WEAK_REFERENCEABLE (LicenseManager)
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LicenseManager)
};

} // namespace aura
