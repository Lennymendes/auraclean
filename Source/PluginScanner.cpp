// ==============================================================================
//  PluginScanner.cpp — AuraClean (NextDSP)
// ==============================================================================

#include "PluginScanner.h"

#include <CoreFoundation/CoreFoundation.h>
#include <fts.h>
#include <sys/attr.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <set>
#include <thread>
#include <unordered_map>

namespace aura
{
//==============================================================================
//  Petites fonctions publiques
//==============================================================================
juce::String formatLabel (PluginFormat f)
{
    switch (f)
    {
        case PluginFormat::VST3: return "VST3";
        case PluginFormat::AU:   return "AU";
        case PluginFormat::AAX:  return "AAX";
        case PluginFormat::VST2: return "VST";
        case PluginFormat::CLAP: return "CLAP";
    }
    return {};
}

juce::String formatExtension (PluginFormat f)
{
    switch (f)
    {
        case PluginFormat::VST3: return ".vst3";
        case PluginFormat::AU:   return ".component";
        case PluginFormat::AAX:  return ".aaxplugin";
        case PluginFormat::VST2: return ".vst";
        case PluginFormat::CLAP: return ".clap";
    }
    return {};
}

ArchClass classifyArch (uint32_t flags) noexcept
{
    const bool arm   = (flags & macho::archARM64) != 0;
    const bool intel = (flags & (macho::archX86_64 | macho::archI386)) != 0;

    if (arm && intel) return ArchClass::Universal;
    if (arm)          return ArchClass::AppleSilicon;
    if (intel)        return ArchClass::IntelOnly;
    return ArchClass::Unknown;
}

juce::String archLabel (ArchClass c)
{
    switch (c)
    {
        case ArchClass::Universal:    return "Universal";
        case ArchClass::AppleSilicon: return "Apple Silicon";
        case ArchClass::IntelOnly:    return "Intel";
        case ArchClass::Unknown:      break;
    }
    return "Inconnue";
}

juce::String archDetail (uint32_t flags)
{
    return juce::String::fromUTF8 (macho::describe (flags).c_str());
}

juce::String PluginScanner::unknownVendorKey()  { return "~unknown"; }
juce::String PluginScanner::unknownVendorName() { return juce::String::fromUTF8 ("Éditeur inconnu"); }

//==============================================================================
namespace
{
    constexpr int kMaxFolderDepth = 4;

    //--------------------------------------------------------------------------
    //  Property lists via CoreFoundation (gère XML ET binaire) → juce::var
    //--------------------------------------------------------------------------
    juce::var cfToVar (CFTypeRef ref, int depth = 0)
    {
        if (ref == nullptr || depth > 32)
            return {};

        const auto type = CFGetTypeID (ref);

        if (type == CFStringGetTypeID())
            return juce::String::fromCFString ((CFStringRef) ref);

        if (type == CFNumberGetTypeID())
        {
            auto number = (CFNumberRef) ref;

            if (CFNumberIsFloatType (number))
            {
                double d = 0.0;
                CFNumberGetValue (number, kCFNumberDoubleType, &d);
                return d;
            }

            juce::int64 i = 0;
            CFNumberGetValue (number, kCFNumberSInt64Type, &i);
            return i;
        }

        if (type == CFBooleanGetTypeID())
            return CFBooleanGetValue ((CFBooleanRef) ref) != 0;

        if (type == CFArrayGetTypeID())
        {
            auto array = (CFArrayRef) ref;
            juce::Array<juce::var> out;
            const auto count = CFArrayGetCount (array);

            for (CFIndex i = 0; i < count; ++i)
                out.add (cfToVar (CFArrayGetValueAtIndex (array, i), depth + 1));

            return out;
        }

        if (type == CFDictionaryGetTypeID())
        {
            auto dict = (CFDictionaryRef) ref;
            const auto count = CFDictionaryGetCount (dict);
            std::vector<const void*> keys ((size_t) count), values ((size_t) count);
            CFDictionaryGetKeysAndValues (dict, keys.data(), values.data());

            juce::DynamicObject::Ptr object = new juce::DynamicObject();

            for (size_t i = 0; i < (size_t) count; ++i)
            {
                if (keys[i] == nullptr || CFGetTypeID (keys[i]) != CFStringGetTypeID())
                    continue;

                const auto key = juce::String::fromCFString ((CFStringRef) keys[i]);

                if (key.isNotEmpty())
                    object->setProperty (key, cfToVar (values[i], depth + 1));
            }

            return juce::var (object.get());
        }

        return {};  // CFData, CFDate... non utilisés
    }

    juce::var readPlistFile (const juce::File& file)
    {
        juce::MemoryBlock block;

        if (! file.existsAsFile() || ! file.loadFileAsData (block) || block.getSize() == 0)
            return {};

        CFDataRef data = CFDataCreateWithBytesNoCopy (kCFAllocatorDefault,
                                                      static_cast<const UInt8*> (block.getData()),
                                                      (CFIndex) block.getSize(),
                                                      kCFAllocatorNull);
        if (data == nullptr)
            return {};

        CFPropertyListRef plist = CFPropertyListCreateWithData (kCFAllocatorDefault, data,
                                                                kCFPropertyListImmutable, nullptr, nullptr);
        CFRelease (data);

        if (plist == nullptr)
            return {};

        auto result = cfToVar (plist);
        CFRelease (plist);
        return result;
    }

    juce::var prop (const juce::var& object, const char* key)
    {
        if (auto* o = object.getDynamicObject())
            return o->getProperty (juce::Identifier (key));

        return {};
    }

    //--------------------------------------------------------------------------
    //  moduleinfo.json est du JSON5 : on retire commentaires et virgules finales
    //--------------------------------------------------------------------------
    juce::String sanitiseJson5 (const juce::String& input)
    {
        const std::string in = input.toStdString();
        std::string noComments;
        noComments.reserve (in.size());

        bool inString = false;

        for (size_t i = 0; i < in.size(); ++i)
        {
            const char c = in[i];

            if (inString)
            {
                noComments += c;

                if (c == '\\' && i + 1 < in.size())
                    noComments += in[++i];
                else if (c == '"')
                    inString = false;

                continue;
            }

            if (c == '"')
            {
                inString = true;
                noComments += c;
                continue;
            }

            if (c == '/' && i + 1 < in.size() && in[i + 1] == '/')
            {
                while (i < in.size() && in[i] != '\n')
                    ++i;
                noComments += '\n';
                continue;
            }

            if (c == '/' && i + 1 < in.size() && in[i + 1] == '*')
            {
                i += 2;
                while (i + 1 < in.size() && ! (in[i] == '*' && in[i + 1] == '/'))
                    ++i;
                ++i;
                continue;
            }

            noComments += c;
        }

        // Deuxième passe : virgules finales avant } ou ]
        std::string out;
        out.reserve (noComments.size());
        inString = false;

        for (size_t i = 0; i < noComments.size(); ++i)
        {
            const char c = noComments[i];

            if (inString)
            {
                out += c;

                if (c == '\\' && i + 1 < noComments.size())
                    out += noComments[++i];
                else if (c == '"')
                    inString = false;

                continue;
            }

            if (c == '"')
            {
                inString = true;
                out += c;
                continue;
            }

            if (c == ',')
            {
                size_t j = i + 1;
                while (j < noComments.size() && std::isspace ((unsigned char) noComments[j]))
                    ++j;

                if (j < noComments.size() && (noComments[j] == '}' || noComments[j] == ']'))
                    continue;
            }

            out += c;
        }

        return juce::String::fromUTF8 (out.c_str());
    }

    //--------------------------------------------------------------------------
    //  Système de fichiers
    //--------------------------------------------------------------------------
    bool isSymlink (const std::string& path)
    {
        struct stat st {};
        return ::lstat (path.c_str(), &st) == 0 && S_ISLNK (st.st_mode);
    }

    /** Taille logique totale via fts(3) — nettement plus rapide que juce::DirectoryIterator. */
    juce::int64 computeBundleSize (const std::string& path)
    {
        char* paths[] = { const_cast<char*> (path.c_str()), nullptr };

        // FTS_COMFOLLOW : suit un éventuel lien symbolique racine (bundle "lié")
        FTS* fts = ::fts_open (paths, FTS_PHYSICAL | FTS_NOCHDIR | FTS_COMFOLLOW, nullptr);

        if (fts == nullptr)
            return 0;

        juce::int64 total = 0;

        while (FTSENT* entry = ::fts_read (fts))
        {
            switch (entry->fts_info)
            {
                case FTS_F:
                case FTS_SL:
                case FTS_SLNONE:
                case FTS_DEFAULT:
                    if (entry->fts_statp != nullptr)
                        total += (juce::int64) entry->fts_statp->st_size;
                    break;

                default:
                    break;
            }
        }

        ::fts_close (fts);
        return total;
    }

    /** "Date d'ajout" du Finder (ATTR_CMN_ADDEDTIME), repli sur la date de création. */
    juce::Time installDateFor (const std::string& path)
    {
        struct attrlist request {};
        request.bitmapcount = ATTR_BIT_MAP_COUNT;
        request.commonattr  = ATTR_CMN_RETURNED_ATTRS | ATTR_CMN_ADDEDTIME;

        struct
        {
            uint32_t        length;
            attribute_set_t returned;
            struct timespec added;
        } __attribute__ ((aligned (4), packed)) reply {};

        if (::getattrlist (path.c_str(), &request, &reply, sizeof (reply),
                           FSOPT_NOFOLLOW) == 0
            && (reply.returned.commonattr & ATTR_CMN_ADDEDTIME) != 0
            && reply.added.tv_sec > 0)
        {
            return juce::Time ((juce::int64) reply.added.tv_sec * 1000 + reply.added.tv_nsec / 1000000);
        }

        struct stat st {};

        if (::lstat (path.c_str(), &st) == 0)
            return juce::Time ((juce::int64) st.st_birthtimespec.tv_sec * 1000
                               + st.st_birthtimespec.tv_nsec / 1000000);

        return {};
    }

    bool endsWithIgnoreCase (const std::string& s, const std::string& suffixLower)
    {
        if (s.size() < suffixLower.size())
            return false;

        for (size_t i = 0; i < suffixLower.size(); ++i)
            if (std::tolower ((unsigned char) s[s.size() - suffixLower.size() + i]) != suffixLower[i])
                return false;

        return true;
    }

    bool isOpaqueBundleName (const std::string& name)
    {
        for (const char* ext : { ".app", ".bundle", ".framework", ".plugin", ".vst", ".clap", ".dpm", ".aaxplugin", ".component", ".vst3" })
            if (endsWithIgnoreCase (name, ext))
                return true;

        return false;
    }

    /** Collecte récursive (sous-dossiers par marque), sans jamais descendre dans un bundle. */
    void collectBundles (const std::filesystem::path& dir, const std::string& extensionLower,
                         int depth, std::vector<std::filesystem::path>& out)
    {
        namespace fs = std::filesystem;
        std::error_code ec;
        fs::directory_iterator it (dir, fs::directory_options::skip_permission_denied, ec);

        if (ec)
            return;

        for (const fs::directory_iterator end {}; it != end; it.increment (ec))
        {
            if (ec)
                break;

            const auto& entry = *it;
            const auto  name  = entry.path().filename().string();

            if (name.empty() || name[0] == '.')
                continue;

            if (endsWithIgnoreCase (name, extensionLower))
            {
                out.push_back (entry.path());
                continue;
            }

            if (depth >= kMaxFolderDepth || isOpaqueBundleName (name))
                continue;

            std::error_code sec;

            if (entry.is_directory (sec) && ! entry.is_symlink (sec))
                collectBundles (entry.path(), extensionLower, depth + 1, out);
        }
    }

    juce::File findExecutable (const juce::File& contents, const juce::String& declaredName)
    {
        const auto macOSFolder = contents.getChildFile ("MacOS");

        if (declaredName.isNotEmpty())
        {
            const auto declared = macOSFolder.getChildFile (declaredName);
            if (declared.existsAsFile())
                return declared;
        }

        for (const auto& entry : juce::RangedDirectoryIterator (macOSFolder, false, "*", juce::File::findFiles))
            if (! entry.getFile().getFileName().startsWithChar ('.'))
                return entry.getFile();

        return {};
    }

    //--------------------------------------------------------------------------
    //  Heuristiques de marque
    //--------------------------------------------------------------------------
    struct Candidate
    {
        juce::String value;
        int          confidence = 0;

        void offer (const juce::String& v, int c)
        {
            const auto trimmed = v.trim();

            if (trimmed.isNotEmpty() && c > confidence)
            {
                value      = trimmed;
                confidence = c;
            }
        }
    };

    // Niveaux de confiance (plus haut = plus fiable)
    enum Confidence
    {
        confAUComponentName = 100,  // "FabFilter: Pro-Q 3"
        confModuleInfoClass = 95,
        confModuleInfoVendor = 90,
        confModuleInfoFactory = 88,
        confAUSingleName    = 85,
        confKnown4CC        = 80,
        confLearnedDomain   = 75,
        confBundleId        = 60,
        confBundleName      = 50,
        confCopyright       = 45,
        confGetInfo         = 40,
        confAdoptedByName   = 30,
        confFileName        = 10
    };

    const std::unordered_map<std::string, juce::String>& vendorAliases()
    {
        static const std::unordered_map<std::string, juce::String> aliases = []
        {
            const std::pair<const char*, const char*> table[] = {
                { "fabfilter", "FabFilter" },         { "uaudio", "Universal Audio" },
                { "universalaudio", "Universal Audio" }, { "uad", "Universal Audio" },
                { "izotope", "iZotope" },             { "waves", "Waves" },
                { "wavesaudio", "Waves" },            { "nativeinstruments", "Native Instruments" },
                { "soundtoys", "Soundtoys" },         { "avid", "Avid" },
                { "digidesign", "Avid" },             { "apple", "Apple" },
                { "pluginalliance", "Plugin Alliance" }, { "brainworx", "Brainworx" },
                { "slatedigital", "Slate Digital" },  { "arturia", "Arturia" },
                { "spectrasonics", "Spectrasonics" }, { "xferrecords", "Xfer Records" },
                { "xfer", "Xfer Records" },           { "uhe", "u-he" },
                { "valhalladsp", "Valhalla DSP" },    { "valhalla", "Valhalla DSP" },
                { "eventide", "Eventide" },           { "softube", "Softube" },
                { "kilohearts", "Kilohearts" },       { "toneboosters", "ToneBoosters" },
                { "oeksound", "oeksound" },           { "sonnox", "Sonnox" },
                { "mcdsp", "McDSP" },                 { "celemony", "Celemony" },
                { "antares", "Antares" },             { "goodhertz", "Goodhertz" },
                { "tokyodawnlabs", "Tokyo Dawn Labs" }, { "tokyodawn", "Tokyo Dawn Labs" },
                { "cableguys", "Cableguys" },         { "spitfireaudio", "Spitfire Audio" },
                { "steinberg", "Steinberg" },         { "ableton", "Ableton" },
                { "nextdsp", "NextDSP" },             { "solidstatelogic", "Solid State Logic" },
                { "meldaproduction", "MeldaProduction" }, { "melda", "MeldaProduction" },
                { "liquidsonics", "LiquidSonics" },   { "newfangledaudio", "Newfangled Audio" },
                { "sonible", "sonible" },             { "acusticaaudio", "Acustica Audio" },
                { "acustica", "Acustica Audio" },     { "dmgaudio", "DMG Audio" },
                { "airmusictech", "AIR Music Technology" }, { "airmusictechnology", "AIR Music Technology" },
                { "audiothing", "AudioThing" },       { "babyaudio", "Baby Audio" },
                { "klevgrand", "Klevgrand" },         { "polyverse", "Polyverse" },
                { "sugarbytes", "Sugar Bytes" },      { "kv331audio", "KV331 Audio" },
                { "lennardigital", "LennarDigital" }, { "revealsound", "Reveal Sound" },
                { "fxpansion", "FXpansion" },         { "robpapen", "Rob Papen" },
                { "refx", "reFX" },                   { "output", "Output" },
                { "arturiasa", "Arturia" },           { "toontrack", "Toontrack" },
                { "eastwest", "EastWest" },           { "ik", "IK Multimedia" },
                { "ikmultimedia", "IK Multimedia" },  { "neuraldsp", "Neural DSP" },
                { "plugintec", "Plugin Alliance" },   { "sslaudio", "Solid State Logic" }
            };

            std::unordered_map<std::string, juce::String> map;
            for (const auto& [key, display] : table)
                map.emplace (key, juce::String::fromUTF8 (display));
            return map;
        }();

        return aliases;
    }

    juce::String aliasFor (const juce::String& normalisedKey)
    {
        const auto& aliases = vendorAliases();
        const auto  it      = aliases.find (normalisedKey.toStdString());
        return it != aliases.end() ? it->second : juce::String();
    }

    /** Codes fabricant AU (4CC) connus d'office ; les autres sont appris pendant le scan. */
    juce::String vendorForManufacturerCode (const juce::String& code)
    {
        if (code == "appl") return "Apple";
        return {};
    }

    int vendorTokenIndex (const juce::StringArray& parts)
    {
        static const juce::StringArray prefixes { "com", "net", "org", "de", "fr", "uk", "co", "io", "at", "ch",
                                                  "se", "nl", "it", "es", "jp", "us", "ca", "au", "dk", "no",
                                                  "fi", "be", "pl", "nz", "info", "biz" };
        int index = 0;

        while (index < parts.size() - 1 && index < 2 && prefixes.contains (parts[index], true))
            ++index;

        return index;
    }

    /** "com.fabfilter.Pro-Q.3" → "com.fabfilter" (sert à propager une marque fiable). */
    std::string bundleDomain (const juce::String& identifier)
    {
        auto parts = juce::StringArray::fromTokens (identifier.trim().toLowerCase(), ".", "");
        parts.removeEmptyStrings();

        if (parts.size() < 3)
            return {};

        const int index = vendorTokenIndex (parts);
        juce::StringArray domain;

        for (int i = 0; i <= index; ++i)
            domain.add (parts[i]);

        return domain.joinIntoString (".").toStdString();
    }

    juce::String titleCaseToken (const juce::String& token)
    {
        auto words = juce::StringArray::fromTokens (token.replaceCharacters ("-_", "  "), " ", "");
        words.removeEmptyStrings();

        for (auto& w : words)
            if (w == w.toLowerCase())
                w = w.substring (0, 1).toUpperCase() + w.substring (1);

        return words.joinIntoString (" ");
    }
} // namespace

juce::var PluginScanner::readPropertyList (const juce::File& file) { return readPlistFile (file); }

//==============================================================================
//  Heuristiques publiques
//==============================================================================
juce::String PluginScanner::normaliseKey (const juce::String& s)
{
    juce::String out;
    out.preallocateBytes ((size_t) s.getNumBytesAsUTF8() + 1);

    for (auto p = s.getCharPointer(); ! p.isEmpty();)
    {
        const auto c = p.getAndAdvance();

        if (juce::CharacterFunctions::isLetterOrDigit (c))
            out += juce::CharacterFunctions::toLowerCase (c);
    }

    return out;
}

juce::String PluginScanner::vendorFromBundleIdentifier (const juce::String& identifier)
{
    auto parts = juce::StringArray::fromTokens (identifier.trim(), ".", "");
    parts.removeEmptyStrings();

    if (parts.size() < 2)
        return {};

    const auto token = parts[vendorTokenIndex (parts)];

    static const juce::StringArray generic { "yourcompany", "mycompany", "company", "juce", "plugin", "plugins",
                                             "vst", "vst3", "aax", "au", "audiounit", "audiounits", "components",
                                             "manufacturer", "vendor", "example" };
    if (generic.contains (token, true))
        return {};

    if (auto alias = aliasFor (normaliseKey (token)); alias.isNotEmpty())
        return alias;

    return titleCaseToken (token);
}

juce::String PluginScanner::vendorFromCopyright (const juce::String& text)
{
    auto s = text.trim();

    if (s.isEmpty())
        return {};

    const auto copyrightSign = juce::String::fromUTF8 ("\xc2\xa9");
    int pos = -1;

    if ((pos = s.indexOfIgnoreCase ("copyright")) >= 0)
        s = s.substring (pos + 9);
    else if ((pos = s.indexOf (copyrightSign)) >= 0)
        s = s.substring (pos + copyrightSign.length());
    else if ((pos = s.indexOfIgnoreCase ("(c)")) >= 0)
        s = s.substring (pos + 3);
    else
        return {};

    // Retire ©, (c), années, tirets, "by" en tête
    for (bool changed = true; changed;)
    {
        changed = false;
        auto t  = s.trimStart();

        if (t.startsWith (copyrightSign))         { t = t.substring (copyrightSign.length()); changed = true; }
        else if (t.startsWithIgnoreCase ("(c)"))  { t = t.substring (3); changed = true; }
        else if (t.startsWithIgnoreCase ("by "))  { t = t.substring (3); changed = true; }
        else if (t.isNotEmpty() && (juce::CharacterFunctions::isDigit (t[0]) || t[0] == '-' || t[0] == ','
                                    || t[0] == 0x2013 || t[0] == 0x2014))
        {
            t = t.substring (1);
            changed = true;
        }

        s = t;
    }

    int cut = s.length();

    for (const char* stop : { ". ", ",", ";", " all rights", " - ", "\n", "\r" })
    {
        const int i = s.indexOfIgnoreCase (stop);
        if (i >= 0)
            cut = juce::jmin (cut, i);
    }

    s = s.substring (0, cut).trim().trimCharactersAtEnd (".");

    if (s.length() < 2 || s.length() > 40)
        return {};

    return s;
}

juce::String PluginScanner::cleanVendorName (const juce::String& raw)
{
    auto v = raw.trim();

    if (v.isEmpty())
        return {};

    static const char* suffixes[] = { ", inc.", ", inc", " inc.", " inc", ", llc", " llc", " ltd.", " ltd",
                                      " limited", " gmbh", " ag", " ab", " s.a.", " sa", " sas", " sarl",
                                      " corp.", " corp", " corporation", " co.", " oy", " b.v.", " bv",
                                      " pty", " s.r.l.", " srl", " kg", " ug", " aps", " as" };

    for (bool changed = true; changed;)
    {
        changed = false;
        const auto lower = v.toLowerCase();

        for (const char* suffix : suffixes)
        {
            const int len = (int) std::strlen (suffix);

            if (v.length() > len + 1 && lower.endsWith (suffix))
            {
                v = v.dropLastCharacters (len).trimEnd().trimCharactersAtEnd (",");
                changed = true;
                break;
            }
        }
    }

    v = v.trimCharactersAtEnd (".,;: ").trim();

    if (auto alias = aliasFor (normaliseKey (v)); alias.isNotEmpty())
        return alias;

    return v;
}

juce::String PluginScanner::cleanPluginName (const juce::String& rawName, const juce::String& vendor)
{
    auto name = rawName.trim();

    if (vendor.isNotEmpty() && name.startsWithIgnoreCase (vendor))
    {
        const auto rest = name.substring (vendor.length()).trimCharactersAtStart (" -_:").trim();
        if (rest.isNotEmpty())
            name = rest;
    }

    static const juce::StringArray tags { "(AAX)", "(VST3)", "(AU)", "(VST)", "(CLAP)", "(Mono)", "(Stereo)", "AAX Native", "AAX", "VST3", "VST", "CLAP",
                                          "AU", "x64", "64-bit", "64bit", "Mono", "Stereo" };

    for (bool changed = true; changed;)
    {
        changed = false;

        for (const auto& tag : tags)
        {
            if (name.length() > tag.length() + 1 && name.endsWithIgnoreCase (" " + tag))
            {
                name = name.dropLastCharacters (tag.length() + 1).trimEnd().trimCharactersAtEnd ("-_");
                changed = true;
            }
        }
    }

    return name.trim();
}

//==============================================================================
//  Emplacements
//==============================================================================
std::vector<ScanLocation> PluginScanner::defaultLocations()
{
    const auto home       = juce::File::getSpecialLocation (juce::File::userHomeDirectory);
    const auto systemPlug = juce::File ("/Library/Audio/Plug-Ins");
    const auto userPlug   = home.getChildFile ("Library/Audio/Plug-Ins");
    const auto avidAudio  = juce::File ("/Library/Application Support/Avid/Audio");

    std::vector<ScanLocation> out;

    auto addPair = [&out] (const juce::File& enabled, const juce::File& disabled, PluginFormat f, InstallScope s)
    {
        out.push_back ({ enabled, enabled, disabled, f, s, false });
        out.push_back ({ disabled, enabled, disabled, f, s, true });
    };

    addPair (systemPlug.getChildFile ("VST3"),       systemPlug.getChildFile ("VST3 (Disabled)"),       PluginFormat::VST3, InstallScope::System);
    addPair (userPlug.getChildFile ("VST3"),         userPlug.getChildFile ("VST3 (Disabled)"),         PluginFormat::VST3, InstallScope::User);
    addPair (systemPlug.getChildFile ("Components"), systemPlug.getChildFile ("Components (Disabled)"), PluginFormat::AU,   InstallScope::System);
    addPair (userPlug.getChildFile ("Components"),   userPlug.getChildFile ("Components (Disabled)"),   PluginFormat::AU,   InstallScope::User);
    addPair (systemPlug.getChildFile ("VST"),        systemPlug.getChildFile ("VST (Disabled)"),        PluginFormat::VST2, InstallScope::System);
    addPair (userPlug.getChildFile ("VST"),          userPlug.getChildFile ("VST (Disabled)"),          PluginFormat::VST2, InstallScope::User);
    addPair (systemPlug.getChildFile ("CLAP"),       systemPlug.getChildFile ("CLAP (Disabled)"),       PluginFormat::CLAP, InstallScope::System);
    addPair (userPlug.getChildFile ("CLAP"),         userPlug.getChildFile ("CLAP (Disabled)"),         PluginFormat::CLAP, InstallScope::User);

    // Convention Avid : Pro Tools ignore nativement "Plug-Ins (Unused)"
    addPair (avidAudio.getChildFile ("Plug-Ins"), avidAudio.getChildFile ("Plug-Ins (Unused)"), PluginFormat::AAX, InstallScope::System);

    return out;
}

std::vector<juce::File> PluginScanner::allowedRoots()
{
    std::vector<juce::File> roots;

    for (const auto& loc : defaultLocations())
        if (std::find (roots.begin(), roots.end(), loc.folder) == roots.end())
            roots.push_back (loc.folder);

    return roots;
}

//==============================================================================
//  Inspection d'un bundle
//==============================================================================
PluginBundle PluginScanner::inspectBundle (const juce::File& bundleFile, const ScanLocation& location)
{
    PluginBundle b;
    b.file             = bundleFile;
    b.format           = location.format;
    b.scope            = location.scope;
    b.disabled         = location.disabled;
    b.homeFolder       = location.enabledFolder;
    b.quarantineFolder = location.disabledFolder;

    const auto path = bundleFile.getFullPathName().toStdString();
    b.isSymlink   = isSymlink (path);
    b.sizeBytes   = computeBundleSize (path);
    b.installDate = installDateFor (path);

    Candidate vendor, name;
    const auto fileStem = bundleFile.getFileNameWithoutExtension();

    if (bundleFile.isDirectory())
    {
        const auto contents = bundleFile.getChildFile ("Contents");
        const auto info     = readPlistFile (contents.getChildFile ("Info.plist"));

        b.bundleId = prop (info, "CFBundleIdentifier").toString().trim();
        b.version  = prop (info, "CFBundleShortVersionString").toString().trim();

        if (b.version.isEmpty())
            b.version = prop (info, "CFBundleVersion").toString().trim();

        auto bundleName = prop (info, "CFBundleName").toString().trim();
        if (bundleName.isEmpty())
            bundleName = prop (info, "CFBundleDisplayName").toString().trim();

        //------------------------------------------------------------------ AU
        if (location.format == PluginFormat::AU)
        {
            const auto components = prop (info, "AudioComponents");

            if (const auto* array = components.getArray(); array != nullptr && ! array->isEmpty())
            {
                const auto& first    = array->getReference (0);
                b.auManufacturerCode = prop (first, "manufacturer").toString().trim();

                const auto componentName = prop (first, "name").toString();
                const bool single        = array->size() == 1;

                if (componentName.containsChar (':'))
                {
                    vendor.offer (componentName.upToFirstOccurrenceOf (":", false, false), confAUComponentName);

                    if (single)
                        name.offer (componentName.fromFirstOccurrenceOf (":", false, false), confAUComponentName);
                }
                else if (single)
                {
                    name.offer (componentName, confAUSingleName);
                }

                // Bundle multi-composants (shell) : le nom du bundle est plus parlant
                if (! single)
                    name.offer (bundleName.isNotEmpty() ? bundleName : fileStem, confAUSingleName);

                if (b.version.isEmpty())
                {
                    const auto v = prop (first, "version");

                    if (v.isInt() || v.isInt64() || v.isDouble())
                    {
                        const auto n = static_cast<juce::int64> (v);
                        b.version = juce::String ((n >> 16) & 0xffff) + "." + juce::String ((n >> 8) & 0xff)
                                  + "." + juce::String (n & 0xff);
                    }
                }
            }

            vendor.offer (vendorForManufacturerCode (b.auManufacturerCode), confKnown4CC);
        }

        //------------------------------------------------------------ VST3
        if (location.format == PluginFormat::VST3)
        {
            const auto moduleInfo = contents.getChildFile ("Resources/moduleinfo.json");

            if (moduleInfo.existsAsFile())
            {
                juce::var json;

                if (juce::JSON::parse (sanitiseJson5 (moduleInfo.loadFileAsString()), json).wasOk())
                {
                    juce::var best;

                    if (const auto* classes = prop (json, "Classes").getArray())
                    {
                        for (const auto& c : *classes)
                        {
                            if (prop (c, "Category").toString() == "Audio Module Class")
                            {
                                best = c;
                                break;
                            }
                        }

                        if (best.isVoid() && ! classes->isEmpty())
                            best = classes->getReference (0);
                    }

                    vendor.offer (prop (best, "Vendor").toString(), confModuleInfoVendor);
                    vendor.offer (prop (prop (json, "Factory Info"), "Vendor").toString(), confModuleInfoFactory);
                    name.offer (prop (best, "Name").toString(), confModuleInfoClass);
                    name.offer (prop (json, "Name").toString(), confBundleName + 5);

                    if (b.version.isEmpty())
                        b.version = prop (best, "Version").toString().trim();

                    if (b.version.isEmpty())
                        b.version = prop (json, "Version").toString().trim();
                }
            }
        }

        //------------------------------------------------- Replis communs
        vendor.offer (vendorFromBundleIdentifier (b.bundleId), confBundleId);
        vendor.offer (vendorFromCopyright (prop (info, "NSHumanReadableCopyright").toString()), confCopyright);
        vendor.offer (vendorFromCopyright (prop (info, "CFBundleGetInfoString").toString()), confGetInfo);
        name.offer (bundleName, confBundleName);

        //------------------------------------------------- Architecture
        const auto executable = findExecutable (contents, prop (info, "CFBundleExecutable").toString().trim());

        if (executable.existsAsFile())
            b.archFlags = macho::inspectFile (executable.getFullPathName().toStdString()).flags;
    }
    else if (bundleFile.existsAsFile())
    {
        // Très anciens VST3 "plats" (fichier unique)
        b.archFlags = macho::inspectFile (path).flags;
    }

    name.offer (fileStem, confFileName);

    b.vendor           = vendor.value;
    b.vendorConfidence = vendor.confidence;
    b.name             = name.value;
    b.nameConfidence   = name.confidence;
    return b;
}

//==============================================================================
//  Scan
//==============================================================================
ScanResult PluginScanner::scan (const std::atomic<bool>* shouldCancel, const ProgressCallback& progress)
{
    const auto start = juce::Time::getMillisecondCounterHiRes();
    ScanResult result;

    const auto locations = defaultLocations();

    struct Job
    {
        std::filesystem::path path;
        size_t                location;
    };

    std::vector<Job> jobs;

    for (size_t i = 0; i < locations.size(); ++i)
    {
        const auto& loc = locations[i];

        if (! loc.folder.isDirectory())
            continue;

        std::vector<std::filesystem::path> found;
        collectBundles (std::filesystem::path (loc.folder.getFullPathName().toStdString()),
                        formatExtension (loc.format).toLowerCase().toStdString(), 0, found);
        std::sort (found.begin(), found.end());

        for (auto& p : found)
            jobs.push_back ({ std::move (p), i });
    }

    const size_t total = jobs.size();
    result.bundles.resize (total);

    std::atomic<size_t> next { 0 }, done { 0 };
    auto isCancelled = [shouldCancel] { return shouldCancel != nullptr && shouldCancel->load (std::memory_order_relaxed); };

    auto worker = [&]
    {
        for (;;)
        {
            if (isCancelled())
                return;

            const size_t index = next.fetch_add (1);

            if (index >= total)
                return;

            const auto& job = jobs[index];
            result.bundles[index] = inspectBundle (juce::File (juce::String::fromUTF8 (job.path.c_str())),
                                                   locations[job.location]);

            const size_t finished = done.fetch_add (1) + 1;

            if (progress)
                progress ((float) finished / (float) total);
        }
    };

    // I/O-bound : on dépasse volontairement le nombre de coeurs performance
    const auto threadCount = (size_t) juce::jlimit (2, 8, (int) std::thread::hardware_concurrency());
    std::vector<std::thread> pool;

    for (size_t t = 1; t < std::min (threadCount, std::max<size_t> (total, 1)); ++t)
        pool.emplace_back (worker);

    worker();

    for (auto& t : pool)
        t.join();

    if (isCancelled())
    {
        result.cancelled = true;
        result.bundles.clear();
        return result;
    }

    buildCatalogue (result);
    result.elapsedMs = juce::Time::getMillisecondCounterHiRes() - start;

    if (progress)
        progress (1.0f);

    return result;
}

//==============================================================================
//  Catalogue : marques, regroupement, agrégats
//==============================================================================
void PluginScanner::buildCatalogue (ScanResult& result)
{
    auto& bundles = result.bundles;

    // 1. Décodage des codes fabricant AU (4CC) appris sur les bundles bien identifiés
    std::unordered_map<std::string, Candidate> byManufacturer;

    for (const auto& b : bundles)
        if (b.format == PluginFormat::AU && b.auManufacturerCode.isNotEmpty() && b.vendorConfidence >= confModuleInfoVendor)
            byManufacturer[b.auManufacturerCode.toStdString()].offer (b.vendor, b.vendorConfidence);

    for (auto& b : bundles)
    {
        if (b.format != PluginFormat::AU || b.vendorConfidence >= confKnown4CC || b.auManufacturerCode.isEmpty())
            continue;

        if (const auto it = byManufacturer.find (b.auManufacturerCode.toStdString()); it != byManufacturer.end())
        {
            b.vendor           = it->second.value;
            b.vendorConfidence = confKnown4CC;
        }
    }

    // 2. Propagation par domaine de CFBundleIdentifier (com.uaudio.* → "Universal Audio")
    std::unordered_map<std::string, Candidate> byDomain;

    for (const auto& b : bundles)
        if (b.vendorConfidence >= confKnown4CC)
            if (const auto domain = bundleDomain (b.bundleId); ! domain.empty())
                byDomain[domain].offer (b.vendor, b.vendorConfidence);

    for (auto& b : bundles)
    {
        if (b.vendorConfidence >= confKnown4CC)
            continue;

        if (const auto it = byDomain.find (bundleDomain (b.bundleId)); it != byDomain.end())
        {
            b.vendor           = it->second.value;
            b.vendorConfidence = confLearnedDomain;
        }
    }

    // 3. Nettoyage et clés de regroupement
    for (auto& b : bundles)
    {
        b.vendor    = cleanVendorName (b.vendor);
        b.name      = cleanPluginName (b.name, b.vendor);
        b.vendorKey = normaliseKey (b.vendor);
        b.nameKey   = normaliseKey (b.name);

        if (b.vendorKey.isEmpty())
        {
            b.vendorKey        = unknownVendorKey();
            b.vendorConfidence = 0;
        }

        if (b.nameKey.isEmpty())
            b.nameKey = normaliseKey (b.file.getFileNameWithoutExtension());
    }

    // 4. Un bundle sans marque adopte celle d'un plugin homonyme (si elle est unique)
    std::unordered_map<std::string, std::set<std::string>> vendorsByName;
    std::unordered_map<std::string, size_t>                exemplarByName;

    for (size_t i = 0; i < bundles.size(); ++i)
    {
        const auto& b = bundles[i];

        if (b.vendorKey == unknownVendorKey())
            continue;

        const auto key = b.nameKey.toStdString();
        vendorsByName[key].insert (b.vendorKey.toStdString());
        exemplarByName.emplace (key, i);
    }

    for (auto& b : bundles)
    {
        if (b.vendorKey != unknownVendorKey())
            continue;

        const auto key = b.nameKey.toStdString();

        if (const auto it = vendorsByName.find (key); it != vendorsByName.end() && it->second.size() == 1)
        {
            const auto& exemplar = bundles[exemplarByName[key]];
            b.vendor           = exemplar.vendor;
            b.vendorKey        = exemplar.vendorKey;
            b.vendorConfidence = confAdoptedByName;
        }
    }

    // 5. Nom d'affichage le plus fiable pour chaque marque
    std::unordered_map<std::string, Candidate> vendorDisplay;

    for (const auto& b : bundles)
        vendorDisplay[b.vendorKey.toStdString()].offer (b.vendor, b.vendorConfidence + 1);

    // 6. Regroupement Marque + Nom
    std::unordered_map<std::string, size_t> entryByKey;
    std::vector<PluginEntry> entries;

    for (size_t i = 0; i < bundles.size(); ++i)
    {
        const auto& b   = bundles[i];
        const auto  key = (b.vendorKey + "|" + b.nameKey).toStdString();

        auto [it, inserted] = entryByKey.try_emplace (key, entries.size());

        if (inserted)
        {
            PluginEntry e;
            e.vendorKey = b.vendorKey;
            e.nameKey   = b.nameKey;
            entries.push_back (std::move (e));
        }

        entries[it->second].bundles[(size_t) b.format].push_back ((int) i);
    }

    // 7. Agrégats par plugin
    for (auto& e : entries)
    {
        int  bestName     = -1;
        bool allDisabled  = true;
        bool anyDisabled  = false;
        bool anyKnownArch = false;
        bool anyIntelOnly = false;
        bool allUniversal = true;

        for (auto f : kAllFormats)
        {
            for (int bi : e.bundles[(size_t) f])
            {
                const auto& b = bundles[(size_t) bi];

                if (b.nameConfidence > bestName)
                {
                    bestName = b.nameConfidence;
                    e.name   = b.name;
                }

                if (! b.isSymlink)
                    e.totalSize += b.sizeBytes;

                if (b.installDate > e.installDate)
                    e.installDate = b.installDate;

                if (e.version.isEmpty() && b.version.isNotEmpty())
                    e.version = b.version;

                allDisabled = allDisabled && b.disabled;
                anyDisabled = anyDisabled || b.disabled;

                const auto ac = classifyArch (b.archFlags);

                if (ac != ArchClass::Unknown)
                {
                    anyKnownArch = true;
                    anyIntelOnly = anyIntelOnly || ac == ArchClass::IntelOnly;
                    allUniversal = allUniversal && ac == ArchClass::Universal;
                }
            }
        }

        e.allDisabled   = allDisabled;
        e.anyDisabled   = anyDisabled;
        e.unknownVendor = e.vendorKey == unknownVendorKey();
        e.vendor        = e.unknownVendor ? unknownVendorName() : vendorDisplay[e.vendorKey.toStdString()].value;
        e.archClass     = ! anyKnownArch ? ArchClass::Unknown
                        : anyIntelOnly   ? ArchClass::IntelOnly
                        : allUniversal   ? ArchClass::Universal
                                         : ArchClass::AppleSilicon;
    }

    // 8. Tri (marque puis nom, "inconnu" en dernier) et rétro-liens bundle → entrée
    std::sort (entries.begin(), entries.end(), [] (const PluginEntry& a, const PluginEntry& b)
    {
        if (a.unknownVendor != b.unknownVendor)
            return b.unknownVendor;

        if (const int c = a.vendor.compareNatural (b.vendor); c != 0)
            return c < 0;

        return a.name.compareNatural (b.name) < 0;
    });

    for (size_t ei = 0; ei < entries.size(); ++ei)
        for (const auto& list : entries[ei].bundles)
            for (int bi : list)
                bundles[(size_t) bi].entryIndex = (int) ei;

    // 9. Résumé par marque (déjà dans l'ordre grâce au tri)
    std::unordered_map<std::string, size_t> vendorIndex;
    result.vendors.clear();

    for (const auto& e : entries)
    {
        auto [it, inserted] = vendorIndex.try_emplace (e.vendorKey.toStdString(), result.vendors.size());

        if (inserted)
            result.vendors.push_back ({ e.vendorKey, e.vendor, 0, 0 });

        auto& v = result.vendors[it->second];
        ++v.pluginCount;
        v.totalBytes += e.totalSize;
    }

    result.entries = std::move (entries);
}

} // namespace aura
