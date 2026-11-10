// ==============================================================================
//  UpdateChecker.cpp — AuraClean (NextDSP)
// ==============================================================================

#include "UpdateChecker.h"

#include <juce_data_structures/juce_data_structures.h>

#include <algorithm>
#include <vector>

namespace aura
{
namespace
{
    juce::String txt (const char* utf8) { return juce::String::fromUTF8 (utf8); }

    struct ParsedVersion
    {
        std::vector<int>  numbers;
        juce::StringArray preRelease;  // "beta.2" → { "beta", "2" } ; vide = version finale
    };

    ParsedVersion parseVersion (juce::String v)
    {
        v = v.trim();

        if (v.startsWithIgnoreCase ("v"))
            v = v.substring (1);

        v = v.upToFirstOccurrenceOf ("+", false, false);  // métadonnées de build ignorées

        ParsedVersion p;
        const auto core = v.upToFirstOccurrenceOf ("-", false, false);

        if (v.containsChar ('-'))
        {
            p.preRelease = juce::StringArray::fromTokens (v.fromFirstOccurrenceOf ("-", false, false), ".", "");
            p.preRelease.removeEmptyStrings();
        }

        for (const auto& part : juce::StringArray::fromTokens (core, ".", ""))
            p.numbers.push_back (part.retainCharacters ("0123456789").getIntValue());

        while (p.numbers.size() > 1 && p.numbers.back() == 0)
            p.numbers.pop_back();  // "1.0.0" == "1"

        return p;
    }

    bool isNumeric (const juce::String& s)
    {
        return s.isNotEmpty() && s.containsOnly ("0123456789");
    }

    juce::PropertiesFile::Options preferenceOptions()
    {
        juce::PropertiesFile::Options o;
        o.applicationName     = "AuraClean";
        o.filenameSuffix      = ".settings";
        o.folderName          = "NextDSP";
        o.osxLibrarySubFolder = "Application Support";
        return o;
    }

    juce::String sanitiseNotes (const juce::String& notes)
    {
        juce::String out;

        for (auto p = notes.getCharPointer(); ! p.isEmpty();)
        {
            const auto c = p.getAndAdvance();

            if (c == '\n' || c == '\t' || c >= 0x20)
                out += (c == '\t' ? (juce::juce_wchar) ' ' : c);
        }

        out = out.trim();
        return out.length() > 600 ? out.substring (0, 600).trimEnd() + txt ("…") : out;
    }
} // namespace

//==============================================================================
int compareVersions (const juce::String& a, const juce::String& b)
{
    const auto pa = parseVersion (a);
    const auto pb = parseVersion (b);

    const auto count = std::max (pa.numbers.size(), pb.numbers.size());

    for (size_t i = 0; i < count; ++i)
    {
        const int x = i < pa.numbers.size() ? pa.numbers[i] : 0;
        const int y = i < pb.numbers.size() ? pb.numbers[i] : 0;

        if (x != y)
            return x < y ? -1 : 1;
    }

    // Même numéro : une version finale est plus récente qu'une pré-version
    if (pa.preRelease.isEmpty() != pb.preRelease.isEmpty())
        return pa.preRelease.isEmpty() ? 1 : -1;

    for (int i = 0; i < std::min (pa.preRelease.size(), pb.preRelease.size()); ++i)
    {
        const auto& x = pa.preRelease[i];
        const auto& y = pb.preRelease[i];

        if (x == y)
            continue;

        if (isNumeric (x) && isNumeric (y))
            return x.getIntValue() < y.getIntValue() ? -1 : 1;

        if (isNumeric (x) != isNumeric (y))
            return isNumeric (x) ? -1 : 1;  // numérique < alphanumérique (SemVer)

        return x.compareIgnoreCase (y) < 0 ? -1 : 1;
    }

    if (pa.preRelease.size() != pb.preRelease.size())
        return pa.preRelease.size() < pb.preRelease.size() ? -1 : 1;

    return 0;
}

//==============================================================================
UpdateChecker::UpdateChecker (juce::String version, juce::URL feedUrl)
    : juce::Thread ("AuraClean Update Checker"),
      currentVersion (std::move (version)),
      feed (std::move (feedUrl))
{
}

UpdateChecker::~UpdateChecker()
{
    // Le callback de progression interrompt la requête dès que le thread doit s'arrêter
    stopThread (UpdateConfig::timeoutMs + 2000);
}

void UpdateChecker::check (Callback onResult)
{
    if (isThreadRunning())
        return;

    callback = std::move (onResult);
    startThread (juce::Thread::Priority::low);
}

bool UpdateChecker::isTrustedDownloadUrl (const juce::URL& url)
{
    const auto text = url.toString (false).trim();

    if (! text.startsWithIgnoreCase ("https://"))
        return false;

    // Autorité = entre « https:// » et le premier « / », « ? » ou « # »
    auto authority = text.substring (8);

    for (const char* stop : { "/", "?", "#" })
        authority = authority.upToFirstOccurrenceOf (stop, false, false);

    // Refuse « https://nextdsp.com@evil.com/… », les identifiants et les caractères suspects
    if (authority.isEmpty() || authority.containsAnyOf ("@\\ %"))
        return false;

    const auto host = authority.upToFirstOccurrenceOf (":", false, false).toLowerCase().trimCharactersAtEnd (".");

    for (const auto& allowed : UpdateConfig::allowedDownloadHosts())
        if (host == allowed || host.endsWith ("." + allowed))
            return true;

    return false;
}

UpdateInfo UpdateChecker::parseFeed (const juce::String& json, const juce::String& current)
{
    UpdateInfo info;
    info.currentVersion = current;

    juce::var root;

    if (juce::JSON::parse (json, root).failed() || root.getDynamicObject() == nullptr)
    {
        info.errorMessage = txt ("Réponse du serveur de mise à jour illisible.");
        return info;
    }

    info.latestVersion = root.getProperty ("latest_version", {}).toString().trim();
    info.releaseNotes  = sanitiseNotes (root.getProperty ("release_notes", {}).toString());
    info.downloadUrl   = juce::URL (root.getProperty ("download_url", {}).toString().trim());

    if (info.latestVersion.isEmpty() || ! info.latestVersion.containsAnyOf ("0123456789") || info.latestVersion.length() > 32)
    {
        info.errorMessage = txt ("Numéro de version absent ou invalide dans la réponse du serveur.");
        return info;
    }

    if (! isTrustedDownloadUrl (info.downloadUrl))
    {
        info.errorMessage = txt ("Lien de téléchargement refusé (HTTPS et domaine NextDSP obligatoires).");
        return info;
    }

    info.status = compareVersions (info.latestVersion, current) > 0 ? UpdateInfo::Status::UpdateAvailable
                                                                    : UpdateInfo::Status::UpToDate;
    return info;
}

//==============================================================================
void UpdateChecker::run()
{
    UpdateInfo info;
    info.currentVersion = currentVersion;

    if (! feed.getScheme().equalsIgnoreCase ("https"))
    {
        info.errorMessage = txt ("L'adresse du serveur de mise à jour doit être en HTTPS.");
    }
    else
    {
        int statusCode = 0;

        const auto options = juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress)
                                 .withConnectionTimeoutMs (UpdateConfig::timeoutMs)
                                 .withNumRedirectsToFollow (3)
                                 .withStatusCode (&statusCode)
                                 .withExtraHeaders ("User-Agent: AuraClean/" + currentVersion + " (macOS)\r\n"
                                                    "Accept: application/json\r\n"
                                                    "Cache-Control: no-cache")
                                 .withProgressCallback ([this] (int, int) { return ! threadShouldExit(); });

        auto stream = feed.createInputStream (options);

        if (threadShouldExit())
            return;

        if (stream == nullptr)
        {
            info.errorMessage = txt ("Serveur de mise à jour injoignable. Vérifiez votre connexion Internet.");
        }
        else if (statusCode != 0 && (statusCode < 200 || statusCode >= 300))
        {
            info.errorMessage = txt ("Le serveur de mise à jour a répondu avec le code HTTP ") + juce::String (statusCode) + ".";
        }
        else
        {
            juce::MemoryBlock body;
            bool tooLarge = false;

            while (! stream->isExhausted() && ! threadShouldExit())
            {
                char buffer[4096];
                const auto bytesRead = stream->read (buffer, (int) sizeof (buffer));

                if (bytesRead <= 0)
                    break;

                body.append (buffer, (size_t) bytesRead);

                if (body.getSize() > (size_t) UpdateConfig::maxResponseBytes)
                {
                    tooLarge = true;
                    break;
                }
            }

            if (threadShouldExit())
                return;

            if (tooLarge)
                info.errorMessage = txt ("Réponse du serveur de mise à jour trop volumineuse.");
            else
                info = parseFeed (juce::String::fromUTF8 (static_cast<const char*> (body.getData()), (int) body.getSize()),
                                  currentVersion);
        }
    }

    if (threadShouldExit())
        return;

    // Le callback ne référence pas ce thread : il reste valable même si l'UpdateChecker est détruit
    juce::MessageManager::callAsync ([cb = callback, info]
    {
        if (cb)
            cb (info);
    });
}

//==============================================================================
juce::String UpdateChecker::skippedVersion()
{
    juce::PropertiesFile settings (preferenceOptions());
    return settings.getValue ("skippedUpdateVersion");
}

void UpdateChecker::setSkippedVersion (const juce::String& version)
{
    juce::PropertiesFile settings (preferenceOptions());
    settings.setValue ("skippedUpdateVersion", version);
    settings.saveIfNeeded();
}

bool UpdateChecker::isAutoCheckEnabled()
{
    juce::PropertiesFile settings (preferenceOptions());
    return settings.getBoolValue ("autoCheckUpdates", true);
}

void UpdateChecker::setAutoCheckEnabled (bool enabled)
{
    juce::PropertiesFile settings (preferenceOptions());
    settings.setValue ("autoCheckUpdates", enabled);
    settings.saveIfNeeded();
}

} // namespace aura
