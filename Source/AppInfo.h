#pragma once
// ==============================================================================
//  AppInfo.h — AuraClean (NextDSP) : identité de l'app, liens, logo embarqué
// ==============================================================================

#include <juce_graphics/juce_graphics.h>

#include "BinaryData.h"  // généré par juce_add_binary_data (Resources/AuraCleanLogo.png)

namespace aura::AppInfo
{
    inline constexpr const char* websiteUrl     = "https://nextdsp.com";
    inline constexpr const char* websiteLabel   = "nextdsp.com";
    inline constexpr const char* developerName  = "ZEYN";
    inline constexpr const char* copyrightLine  = "© 2026 NextDSP · AuraSuite";

    /** Logo de l'app (512 px, coins transparents), mis en cache par JUCE. */
    inline juce::Image logo()
    {
        return juce::ImageCache::getFromMemory (BinaryData::AuraCleanLogo_png, BinaryData::AuraCleanLogo_pngSize);
    }

    inline void openWebsite()
    {
        juce::URL (websiteUrl).launchInDefaultBrowser();
    }
}
