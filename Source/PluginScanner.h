#pragma once
// ==============================================================================
//  PluginScanner.h — AuraClean (NextDSP)
//
//  Scan PASSIF du système de fichiers : aucun binaire n'est chargé, aucune
//  instanciation via AudioPluginFormatManager. On lit uniquement :
//    - Contents/Info.plist                (CoreFoundation : XML ou binaire)
//    - Contents/Resources/moduleinfo.json (VST3 SDK >= 3.7.5, JSON5 toléré)
//    - les 4 premiers Ko de l'exécutable  (MachOInspector)
//    - les métadonnées du système de fichiers (taille via fts, "Date d'ajout")
// ==============================================================================

#include <juce_core/juce_core.h>

#include <array>
#include <atomic>
#include <functional>
#include <vector>

#include "MachOInspector.h"

namespace aura
{
//==============================================================================
enum class PluginFormat : int
{
    VST3 = 0,
    AU   = 1,
    AAX  = 2
};

inline constexpr int kNumFormats = 3;
inline constexpr std::array<PluginFormat, kNumFormats> kAllFormats { PluginFormat::VST3, PluginFormat::AU, PluginFormat::AAX };

juce::String formatLabel (PluginFormat);      // "VST3" / "AU" / "AAX"
juce::String formatExtension (PluginFormat);  // ".vst3" / ".component" / ".aaxplugin"

enum class InstallScope
{
    System,  // /Library/...
    User     // ~/Library/...
};

/** Classe d'architecture, ordonnée pour le tri (Intel-only = "le pire"). */
enum class ArchClass : int
{
    Unknown      = 0,
    IntelOnly    = 1,
    AppleSilicon = 2,  // ARM64 seul
    Universal    = 3   // ARM64 + Intel
};

ArchClass    classifyArch (uint32_t machoFlags) noexcept;
juce::String archLabel (ArchClass);
juce::String archDetail (uint32_t machoFlags);

//==============================================================================
struct ScanLocation
{
    juce::File   folder;          // dossier réellement scanné
    juce::File   enabledFolder;   // emplacement "actif" (lu par les DAW)
    juce::File   disabledFolder;  // emplacement de quarantaine adjacent
    PluginFormat format = PluginFormat::VST3;
    InstallScope scope  = InstallScope::System;
    bool         disabled = false;
};

/** Un bundle physique sur le disque (.vst3 / .component / .aaxplugin). */
struct PluginBundle
{
    juce::File   file;
    PluginFormat format = PluginFormat::VST3;
    InstallScope scope  = InstallScope::System;
    bool         disabled  = false;  // présent dans un dossier "(Disabled)" / "(Unused)"
    bool         isSymlink = false;

    juce::File homeFolder;        // où le bundle doit vivre quand il est actif
    juce::File quarantineFolder;  // où il part quand on le désactive

    juce::String name, vendor, version, bundleId, auManufacturerCode;
    int          nameConfidence   = 0;
    int          vendorConfidence = 0;

    juce::String vendorKey, nameKey;  // clés normalisées de regroupement
    int          entryIndex = -1;     // index dans ScanResult::entries

    juce::int64 sizeBytes = 0;
    juce::Time  installDate;
    uint32_t    archFlags = macho::archNone;
};

/** Une ligne de la table : un plugin (marque + nom), tous formats confondus. */
struct PluginEntry
{
    juce::String name, vendor, vendorKey, nameKey, version;

    // Indices dans ScanResult::bundles, par format. Plusieurs bundles possibles
    // pour un même format (copie système + copie utilisateur, actif + désactivé).
    std::array<std::vector<int>, kNumFormats> bundles;

    juce::int64 totalSize = 0;
    juce::Time  installDate;  // la plus récente
    ArchClass   archClass = ArchClass::Unknown;
    bool        allDisabled   = false;
    bool        anyDisabled   = false;
    bool        unknownVendor = false;

    bool has (PluginFormat f) const noexcept { return ! bundles[(size_t) f].empty(); }

    int formatCount() const noexcept
    {
        int n = 0;
        for (auto& b : bundles)
            n += b.empty() ? 0 : 1;
        return n;
    }
};

struct VendorSummary
{
    juce::String key, name;
    int          pluginCount = 0;
    juce::int64  totalBytes  = 0;
};

struct ScanResult
{
    std::vector<PluginBundle>  bundles;
    std::vector<PluginEntry>   entries;  // triées par marque puis nom
    std::vector<VendorSummary> vendors;  // triées par nom, "inconnu" en dernier
    double elapsedMs = 0.0;
    bool   cancelled = false;
};

//==============================================================================
class PluginScanner
{
public:
    /** Appelé depuis les threads de scan (doit être thread-safe), valeur 0..1. */
    using ProgressCallback = std::function<void (float)>;

    static std::vector<ScanLocation> defaultLocations();

    /** Tous les dossiers (actifs + quarantaine) dans lesquels AuraClean a le droit d'agir. */
    static std::vector<juce::File> allowedRoots();

    /** Scan complet, multi-threadé. Bloquant : à appeler hors du message thread. */
    static ScanResult scan (const std::atomic<bool>* shouldCancel = nullptr, const ProgressCallback& progress = {});

    static PluginBundle inspectBundle (const juce::File& bundle, const ScanLocation& location);

    /** Résolution des marques, regroupement par plugin, agrégats et résumé par marque. */
    static void buildCatalogue (ScanResult& result);

    // Heuristiques exposées (utiles pour les tests)
    static juce::String normaliseKey (const juce::String&);
    static juce::String vendorFromBundleIdentifier (const juce::String&);
    static juce::String vendorFromCopyright (const juce::String&);
    static juce::String cleanVendorName (const juce::String&);
    static juce::String cleanPluginName (const juce::String& name, const juce::String& vendor);
    static juce::String unknownVendorKey();
    static juce::String unknownVendorName();
};

} // namespace aura
