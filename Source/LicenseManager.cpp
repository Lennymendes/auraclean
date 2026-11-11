// ==============================================================================
//  LicenseManager.cpp — AuraClean (NextDSP)
// ==============================================================================

#include "LicenseManager.h"

#include <juce_cryptography/juce_cryptography.h>

#include <sys/stat.h>

#if JUCE_MAC
 #include <CoreFoundation/CoreFoundation.h>
 #include <IOKit/IOKitLib.h>
#endif

namespace aura
{
namespace
{
    juce::String txt (const char* utf8) { return juce::String::fromUTF8 (utf8); }

    constexpr int kMaxResponseBytes = 64 * 1024;
    constexpr int kFileFormat       = 1;

    juce::String reversedString (const juce::String& s)
    {
        juce::String out;
        for (int i = s.length(); --i >= 0;)
            out += s[i];
        return out;
    }

    /** Sel interne, reconstitué à l'exécution (pas de chaîne lisible d'un bloc dans le binaire). */
    juce::String internalSalt()
    {
        static const char partA[] = { 'N', 'x', 'D', '-', 'a', 'U', 'r', '4', 0 };
        static const char partB[] = { 'C', 'l', '3', '@', 'n', '#', '2', '0', '2', '6', 0 };
        static const char partC[] = { 'Z', '3', 'y', 'N', '~', 's', 'H', 'a', 0 };

        juce::String salt;
        salt << partC << reversedString (partA) << partB << (int) (0x5eed ^ 0x1d0c);
        return salt;
    }

    juce::String prop (const juce::var& object, const char* key)
    {
        if (auto* o = object.getDynamicObject())
            return o->getProperty (juce::Identifier (key)).toString().trim();

        return {};
    }

    juce::var child (const juce::var& object, const char* key)
    {
        if (auto* o = object.getDynamicObject())
            return o->getProperty (juce::Identifier (key));

        return {};
    }

    bool constantTimeEquals (const juce::String& a, const juce::String& b)
    {
        const auto x = a.toStdString(), y = b.toStdString();

        if (x.size() != y.size())
            return false;

        unsigned char diff = 0;
        for (size_t i = 0; i < x.size(); ++i)
            diff |= (unsigned char) (x[i] ^ y[i]);

        return diff == 0;
    }
} // namespace

//==============================================================================
//  Fonctions pures
//==============================================================================
juce::String LicenseManager::normaliseKey (const juce::String& raw)
{
    juce::String out;

    for (auto p = raw.trim().unquoted().getCharPointer(); ! p.isEmpty();)
    {
        const auto c = p.getAndAdvance();

        if (! juce::CharacterFunctions::isWhitespace (c))
            out += c;
    }

    return out;
}

bool LicenseManager::looksLikeKey (const juce::String& key)
{
    return key.length() >= 16 && key.length() <= 64
        && key.containsOnly ("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-");
}

juce::String LicenseManager::maskKey (const juce::String& key)
{
    if (key.length() < 8)
        return txt ("••••");

    return key.substring (0, 4) + txt ("-••••-••••-") + key.getLastCharacters (4);
}

LicenseResult LicenseManager::interpretActivation (const juce::var& json, int httpStatus, StoredLicense& out)
{
    using S = LicenseResult::Status;

    if (json.getDynamicObject() == nullptr)
        return { S::ServerError, txt ("Réponse inattendue du serveur de licences")
                                     + (httpStatus > 0 ? txt (" (HTTP ") + juce::String (httpStatus) + ")." : juce::String (".")) };

    const bool activatedFlag = (bool) child (json, "activated");
    const auto error         = prop (json, "error");
    const auto lowerError    = error.toLowerCase();
    const auto keyObject     = child (json, "license_key");
    const auto keyStatus     = prop (keyObject, "status").toLowerCase();

    if (keyStatus == "expired" || lowerError.contains ("expired"))
        return { S::Expired, txt ("Cette licence a expiré.") };

    if (keyStatus == "disabled" || lowerError.contains ("disabled"))
        return { S::Disabled, txt ("Cette licence a été désactivée (remboursement ou révocation).") };

    if (! activatedFlag)
    {
        if (lowerError.contains ("activation limit"))
            return { S::LimitReached,
                     txt ("Limite atteinte : cette licence est déjà activée sur ") + juce::String (LicenseConfig::maxMachines)
                         + txt (" Mac. Désactivez un ancien Mac depuis ses Réglages (⌘,), puis réessayez.") };

        if (lowerError.contains ("not found") || httpStatus == 404)
            return { S::InvalidKey, txt ("Clé de licence introuvable. Vérifiez qu'elle a été copiée en entier.") };

        if (httpStatus >= 500)
            return { S::ServerError, txt ("Le serveur de licences est indisponible (HTTP ") + juce::String (httpStatus)
                                         + txt ("). Réessayez dans quelques minutes.") };

        return { S::InvalidKey, error.isNotEmpty() ? txt ("Activation refusée : ") + error
                                                   : txt ("Activation refusée par le serveur de licences.") };
    }

    // Sécurité multi-produits : une clé d'un autre produit Lemon Squeezy ne débloque pas AuraClean
    const auto meta        = child (json, "meta");
    const auto productName = prop (meta, "product_name");
    const auto instance    = child (json, "instance");

    out.instanceId   = prop (instance, "id");
    out.instanceName = prop (instance, "name");
    out.licenseKey   = prop (keyObject, "key");
    out.productId    = prop (meta, "product_id");
    out.productName  = productName;
    out.activatedAt  = prop (instance, "created_at");

    if (! productName.containsIgnoreCase (LicenseConfig::productNameMatch))
        return { S::WrongProduct, txt ("Cette clé appartient à un autre produit")
                                      + (productName.isNotEmpty() ? txt (" (« ") + productName + txt (" »)") : juce::String())
                                      + txt (" et ne peut pas activer AuraClean.") };

    if (out.instanceId.isEmpty())
        return { S::ServerError, txt ("Réponse incomplète du serveur de licences (identifiant d'activation manquant).") };

    if (out.activatedAt.isEmpty())
        out.activatedAt = juce::Time::getCurrentTime().toISO8601 (true);

    return { S::Activated, txt ("Licence activée — merci pour votre soutien !") };
}

LicenseResult LicenseManager::interpretDeactivation (const juce::var& json, int httpStatus)
{
    using S = LicenseResult::Status;

    if (json.getDynamicObject() == nullptr)
        return { S::ServerError, txt ("Réponse inattendue du serveur de licences (HTTP ") + juce::String (httpStatus) + ")." };

    const auto error = prop (json, "error").toLowerCase();

    // Instance déjà supprimée côté serveur : l'emplacement est libre, on nettoie localement
    if ((bool) child (json, "deactivated") || error.contains ("not found"))
        return { S::Deactivated, txt ("Ce Mac a été désactivé : un emplacement de licence est libéré.") };

    return { S::ServerError, error.isNotEmpty() ? txt ("Désactivation refusée : ") + prop (json, "error")
                                                : txt ("Désactivation refusée par le serveur de licences.") };
}

juce::String LicenseManager::computeSignature (const StoredLicense& l, const juce::String& machineId)
{
    juce::String material;
    material << internalSalt() << '|' << kFileFormat << '|' << machineId << '|' << l.licenseKey << '|'
             << l.instanceId << '|' << l.productId << '|' << reversedString (internalSalt());

    return juce::SHA256 (material.toUTF8()).toHexString();
}

juce::String LicenseManager::serialise (const StoredLicense& l, const juce::String& machineId)
{
    juce::DynamicObject::Ptr o = new juce::DynamicObject();
    o->setProperty ("format",        kFileFormat);
    o->setProperty ("license_key",   l.licenseKey);
    o->setProperty ("instance_id",   l.instanceId);
    o->setProperty ("instance_name", l.instanceName);
    o->setProperty ("product_id",    l.productId);
    o->setProperty ("product_name",  l.productName);
    o->setProperty ("activated_at",  l.activatedAt);
    o->setProperty ("signature",     computeSignature (l, machineId));
    return juce::JSON::toString (juce::var (o.get()));
}

bool LicenseManager::parseAndVerify (const juce::String& content, const juce::String& machineId,
                                     StoredLicense& out, juce::String& problem)
{
    juce::var json;

    if (juce::JSON::parse (content, json).failed() || json.getDynamicObject() == nullptr)
    {
        problem = txt ("Fichier de licence illisible. Réactivez AuraClean avec votre clé.");
        return false;
    }

    StoredLicense l;
    l.licenseKey   = prop (json, "license_key");
    l.instanceId   = prop (json, "instance_id");
    l.instanceName = prop (json, "instance_name");
    l.productId    = prop (json, "product_id");
    l.productName  = prop (json, "product_name");
    l.activatedAt  = prop (json, "activated_at");

    if (l.licenseKey.isEmpty() || l.instanceId.isEmpty() || machineId.isEmpty()
        || ! constantTimeEquals (prop (json, "signature"), computeSignature (l, machineId)))
    {
        problem = txt ("Licence associée à un autre Mac ou fichier modifié. Réactivez AuraClean sur ce Mac.");
        return false;
    }

    out = l;
    problem = {};
    return true;
}

juce::File LicenseManager::licenseFile()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
               .getChildFile ("Application Support/NextDSP/AuraClean/license.dat");
}

juce::String LicenseManager::machineIdentifier()
{
    juce::String id;

   #if JUCE_MAC
    // IOPlatformUUID : identifiant matériel stable du Mac (ne change pas après réinstallation de macOS)
    if (io_service_t expert = IOServiceGetMatchingService (MACH_PORT_NULL, IOServiceMatching ("IOPlatformExpertDevice")))
    {
        if (CFTypeRef ref = IORegistryEntryCreateCFProperty (expert, CFSTR ("IOPlatformUUID"), kCFAllocatorDefault, 0))
        {
            if (CFGetTypeID (ref) == CFStringGetTypeID())
                id = juce::String::fromCFString ((CFStringRef) ref);

            CFRelease (ref);
        }

        IOObjectRelease (expert);
    }
   #endif

    if (id.isEmpty())
        id = juce::SystemStats::getUniqueDeviceID();

    return id.trim();
}

juce::String LicenseManager::instanceName()
{
    auto name = juce::SystemStats::getComputerName().trim();
    return name.isEmpty() ? juce::String ("Mac") : name.substring (0, 100);
}

//==============================================================================
//  Cycle de vie
//==============================================================================
LicenseManager::LicenseManager() : juce::Thread ("AuraClean License")
{
    selfRef = this;

    const auto file = licenseFile();

    if (file.existsAsFile())
    {
        StoredLicense stored;

        if (parseAndVerify (file.loadFileAsString(), machineIdentifier(), stored, loadIssue))
        {
            license   = stored;
            activated = true;
        }
    }
}

LicenseManager::~LicenseManager()
{
    stopThread (LicenseConfig::timeoutMs + 2000);
}

void LicenseManager::activate (const juce::String& rawKey, Callback onDone)
{
    if (isThreadRunning())
        return;

    const auto key = normaliseKey (rawKey);

    if (! looksLikeKey (key))
    {
        juce::MessageManager::callAsync ([onDone]
        {
            if (onDone)
                onDone ({ LicenseResult::Status::InvalidFormat,
                          txt ("Format de clé invalide. Collez la clé reçue par e-mail (ex. XXXX-XXXX-XXXX-XXXX).") });
        });
        return;
    }

    job             = Job::Activate;
    pendingKey      = key;
    pendingCallback = std::move (onDone);
    startThread (juce::Thread::Priority::normal);
}

void LicenseManager::deactivate (Callback onDone)
{
    if (isThreadRunning() || ! activated)
        return;

    job             = Job::Deactivate;
    pendingKey      = license.licenseKey;
    pendingCallback = std::move (onDone);
    startThread (juce::Thread::Priority::normal);
}

//==============================================================================
//  Réseau (thread dédié)
//==============================================================================
LicenseResult LicenseManager::post (const char* endpoint, const juce::StringPairArray& fields,
                                    juce::var& json, int& status, juce::Thread& thread)
{
    juce::URL url (endpoint);

    for (const auto& name : fields.getAllKeys())
        url = url.withParameter (name, fields[name]);

    status = 0;

    const auto options = juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inPostData)
                             .withConnectionTimeoutMs (LicenseConfig::timeoutMs)
                             .withNumRedirectsToFollow (0)
                             .withStatusCode (&status)
                             .withExtraHeaders ("Accept: application/json\r\n"
                                                "Content-Type: application/x-www-form-urlencoded\r\n"
                                                "User-Agent: AuraClean (macOS)")
                             .withProgressCallback ([&thread] (int, int) { return ! thread.threadShouldExit(); });

    auto stream = url.createInputStream (options);

    if (stream == nullptr)
        return { LicenseResult::Status::NetworkError,
                 txt ("Impossible de joindre le serveur de licences. Vérifiez votre connexion Internet.") };

    juce::MemoryBlock body;

    while (! stream->isExhausted() && ! thread.threadShouldExit())
    {
        char buffer[4096];
        const auto n = stream->read (buffer, (int) sizeof (buffer));

        if (n <= 0)
            break;

        body.append (buffer, (size_t) n);

        if (body.getSize() > (size_t) kMaxResponseBytes)
            return { LicenseResult::Status::ServerError, txt ("Réponse du serveur de licences trop volumineuse.") };
    }

    json = juce::JSON::parse (juce::String::fromUTF8 (static_cast<const char*> (body.getData()), (int) body.getSize()));
    return { LicenseResult::Status::ServerError, {} };  // interprété par l'appelant
}

void LicenseManager::run()
{
    using S = LicenseResult::Status;

    if (job == Job::Activate)
    {
        juce::StringPairArray fields;
        fields.set ("license_key", pendingKey);
        fields.set ("instance_name", instanceName());

        juce::var json;
        int status = 0;
        auto result = post (LicenseConfig::activateUrl, fields, json, status, *this);

        if (threadShouldExit())
            return;

        StoredLicense stored;

        if (result.status != S::NetworkError && result.message.isEmpty())
            result = interpretActivation (json, status, stored);

        // Clé d'un autre produit : on rend aussitôt l'emplacement qu'on vient de consommer
        if (result.status == S::WrongProduct && stored.instanceId.isNotEmpty())
        {
            juce::StringPairArray release;
            release.set ("license_key", pendingKey);
            release.set ("instance_id", stored.instanceId);
            juce::var ignored;
            int ignoredStatus = 0;
            post (LicenseConfig::deactivateUrl, release, ignored, ignoredStatus, *this);
        }

        if (result.status == S::Activated)
        {
            if (stored.licenseKey.isEmpty())
                stored.licenseKey = pendingKey;

            if (stored.instanceName.isEmpty())
                stored.instanceName = instanceName();

            const auto file = licenseFile();
            file.getParentDirectory().createDirectory();

            if (file.replaceWithText (serialise (stored, machineIdentifier())))
            {
                ::chmod (file.getFullPathName().toRawUTF8(), 0600);
                finish (result, true, stored);
            }
            else
            {
                finish ({ S::StorageError, txt ("Licence validée, mais impossible d'enregistrer le fichier de licence. "
                                                "Vérifiez les droits de ~/Library/Application Support.") },
                        false, {});
            }
            return;
        }

        finish (result, false, {});
        return;
    }

    if (job == Job::Deactivate)
    {
        juce::StringPairArray fields;
        fields.set ("license_key", pendingKey);
        fields.set ("instance_id", license.instanceId);

        juce::var json;
        int status = 0;
        auto result = post (LicenseConfig::deactivateUrl, fields, json, status, *this);

        if (threadShouldExit())
            return;

        if (result.status != S::NetworkError && result.message.isEmpty())
            result = interpretDeactivation (json, status);

        if (result.status == S::Deactivated)
        {
            licenseFile().deleteFile();
            finish (result, true, {});
        }
        else
        {
            finish (result, false, {});
        }
    }
}

void LicenseManager::finish (LicenseResult result, bool applyNewState, StoredLicense stored)
{
    // Lancé depuis le thread réseau : l'état n'est modifié que sur le message thread
    auto weakSelf = selfRef;  // copie (compteur atomique) d'une référence créée sur le message thread
    auto callback = pendingCallback;
    const bool nowActivated = result.status == LicenseResult::Status::Activated;

    juce::MessageManager::callAsync ([weakSelf, callback, result, applyNewState, nowActivated, stored]
    {
        if (auto* self = weakSelf.get(); self != nullptr && applyNewState)
        {
            self->activated = nowActivated;
            self->license   = nowActivated ? stored : StoredLicense {};
            self->loadIssue = {};

            if (self->onStateChanged)
                self->onStateChanged();
        }

        if (callback)
            callback (result);
    });
}

} // namespace aura
