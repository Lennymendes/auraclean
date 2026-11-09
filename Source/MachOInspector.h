#pragma once
// ==============================================================================
//  MachOInspector.h — AuraClean (NextDSP)
//
//  Lecture PASSIVE de l'en-tête d'un exécutable Mach-O : on lit au plus 4 Ko,
//  on ne mappe ni ne charge jamais le binaire (pas de dlopen, pas de dyld).
//
//  Formats reconnus :
//    - Thin Mach-O 32/64 bits, little ou big endian (MH_MAGIC, MH_MAGIC_64 + CIGAM)
//    - Fat / Universal (FAT_MAGIC, FAT_MAGIC_64), toujours stocké big endian
//
//  Header autonome, sans dépendance JUCE : testable en isolation.
// ==============================================================================

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>

namespace aura::macho
{
enum ArchFlag : uint32_t
{
    archNone   = 0,
    archARM64  = 1u << 0,  // arm64 / arm64e
    archX86_64 = 1u << 1,
    archI386   = 1u << 2,
    archARM32  = 1u << 3,
    archPPC    = 1u << 4,
    archPPC64  = 1u << 5,
    archOther  = 1u << 6
};

enum class Container
{
    Unknown,
    Thin,
    Fat
};

struct Info
{
    uint32_t  flags      = archNone;
    Container container  = Container::Unknown;
    uint32_t  sliceCount = 0;

    bool isValid() const noexcept { return container != Container::Unknown; }
    bool hasARM64() const noexcept { return (flags & archARM64) != 0; }
    bool hasX86_64() const noexcept { return (flags & archX86_64) != 0; }
};

namespace detail
{
    // <mach-o/loader.h> et <mach-o/fat.h> recopiés ici pour rester portable
    constexpr uint32_t mhMagic    = 0xfeedfaceu;
    constexpr uint32_t mhMagic64  = 0xfeedfacfu;
    constexpr uint32_t fatMagic   = 0xcafebabeu;
    constexpr uint32_t fatMagic64 = 0xcafebabfu;

    constexpr int32_t cpuArchABI64 = 0x01000000;
    constexpr int32_t cpuTypeX86   = 7;
    constexpr int32_t cpuTypeARM   = 12;
    constexpr int32_t cpuTypePPC   = 18;

    constexpr size_t fatHeaderSize  = 8;   // magic + nfat_arch
    constexpr size_t fatArchSize    = 20;  // struct fat_arch
    constexpr size_t fatArch64Size  = 32;  // struct fat_arch_64
    constexpr uint32_t maxFatSlices = 32;  // garde-fou (et filtre les .class Java)

    inline uint32_t readBE32 (const uint8_t* p) noexcept
    {
        return (uint32_t (p[0]) << 24) | (uint32_t (p[1]) << 16) | (uint32_t (p[2]) << 8) | uint32_t (p[3]);
    }

    inline uint32_t readLE32 (const uint8_t* p) noexcept
    {
        return (uint32_t (p[3]) << 24) | (uint32_t (p[2]) << 16) | (uint32_t (p[1]) << 8) | uint32_t (p[0]);
    }

    inline uint32_t flagForCpuType (int32_t cpuType) noexcept
    {
        switch (cpuType)
        {
            case cpuTypeARM | cpuArchABI64: return archARM64;
            case cpuTypeX86 | cpuArchABI64: return archX86_64;
            case cpuTypeX86:                return archI386;
            case cpuTypeARM:                return archARM32;
            case cpuTypePPC:                return archPPC;
            case cpuTypePPC | cpuArchABI64: return archPPC64;
            default:                        return archOther;  // ex. arm64_32
        }
    }
} // namespace detail

/** Analyse un tampon contenant le début d'un fichier. */
inline Info inspectBuffer (const uint8_t* data, size_t size) noexcept
{
    using namespace detail;
    Info info;

    if (data == nullptr || size < 8)
        return info;

    const uint32_t be = readBE32 (data);
    const uint32_t le = readLE32 (data);

    // --- Universal / Fat (toujours big endian sur disque)
    if (be == fatMagic || be == fatMagic64)
    {
        const bool     is64   = (be == fatMagic64);
        const uint32_t slices = readBE32 (data + 4);

        // Les .class Java partagent 0xCAFEBABE : leur second mot (version) vaut >= 45.
        if (slices == 0 || slices > maxFatSlices)
            return info;

        const size_t stride = is64 ? fatArch64Size : fatArchSize;

        if (fatHeaderSize + size_t (slices) * stride > size)
            return info;

        for (uint32_t i = 0; i < slices; ++i)
            info.flags |= flagForCpuType (int32_t (readBE32 (data + fatHeaderSize + i * stride)));

        info.container  = Container::Fat;
        info.sliceCount = slices;
        return info;
    }

    // --- Thin little endian (cas standard : arm64 / x86_64)
    if (le == mhMagic || le == mhMagic64)
    {
        info.flags      = flagForCpuType (int32_t (readLE32 (data + 4)));
        info.container  = Container::Thin;
        info.sliceCount = 1;
        return info;
    }

    // --- Thin big endian (PowerPC historique)
    if (be == mhMagic || be == mhMagic64)
    {
        info.flags      = flagForCpuType (int32_t (readBE32 (data + 4)));
        info.container  = Container::Thin;
        info.sliceCount = 1;
        return info;
    }

    return info;
}

/** Lit au plus 4 Ko en tête de fichier. Ne lève jamais d'exception. */
inline Info inspectFile (const std::string& path) noexcept
{
    std::FILE* file = std::fopen (path.c_str(), "rb");

    if (file == nullptr)
        return {};

    uint8_t buffer[4096];
    const size_t bytesRead = std::fread (buffer, 1, sizeof (buffer), file);
    std::fclose (file);

    return inspectBuffer (buffer, bytesRead);
}

/** Description lisible des slices présentes, ex. "ARM64 + x86_64". */
inline std::string describe (uint32_t flags)
{
    struct Name { uint32_t flag; const char* text; };
    static constexpr Name names[] = { { archARM64, "ARM64" }, { archX86_64, "x86_64" }, { archI386, "i386" },
                                      { archARM32, "ARM32" }, { archPPC, "PPC" },       { archPPC64, "PPC64" },
                                      { archOther, "autre" } };
    std::string out;

    for (const auto& n : names)
    {
        if ((flags & n.flag) == 0)
            continue;

        if (! out.empty())
            out += " + ";

        out += n.text;
    }

    return out.empty() ? std::string ("inconnue") : out;
}

} // namespace aura::macho
