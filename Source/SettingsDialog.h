#pragma once
// ==============================================================================
//  SettingsDialog.h — AuraClean (NextDSP)
//
//  Panneau « Réglages » (bouton engrenage, ⌘, ou menu AuraClean), à onglets :
//    - Licence      : Mode Aperçu / Licence Active, clé masquée, Activer, Acheter,
//                     Désactiver ce Mac
//    - Mises à jour : version installée, état, Rechercher, Télécharger,
//                     vérification automatique au lancement
//    - À propos     : logo cliquable (→ site), version, « Développé avec ♥ par ZEYN »
//
//  Même présentation que les autres modales. Aucun setGradientFill direct.
// ==============================================================================

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

#include "AuraWidgets.h"
#include "LicenseManager.h"
#include "UpdateChecker.h"

namespace aura
{
class SettingsDialog : public juce::Component,
                       private juce::Timer
{
public:
    enum class Tab { License = 0, Updates, About };

    SettingsDialog (juce::Image logo, juce::String currentVersion, bool autoCheckEnabled,
                    LicenseManager& license, Tab initialTab);
    ~SettingsDialog() override;

    std::function<void()>                  onClose;
    std::function<void()>                  onCheckForUpdates;
    std::function<void (bool)>             onAutoCheckChanged;
    std::function<void (const juce::URL&)> onDownload;
    std::function<void()>                  onActivateLicense;  // ouvre la modale d'activation

    void selectTab (Tab);
    void setChecking();
    void showUpdateResult (const UpdateInfo&);
    void refreshLicense();

    void dismiss (std::function<void()> onDone);

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    enum class UpdateState { Idle, Checking, UpToDate, Available, Error };

    void timerCallback() override;
    juce::Rectangle<int> cardBounds() const;
    juce::Rectangle<int> logoBounds() const;
    void updateVisibility();
    void confirmDeactivation();

    void paintLicenseTab (juce::Graphics&, juce::Rectangle<int> content);
    void paintUpdatesTab (juce::Graphics&, juce::Rectangle<int> content);
    void paintAboutTab (juce::Graphics&, juce::Rectangle<int> content);

    juce::Image     logo;
    juce::String    version;
    LicenseManager& license;
    Tab             tab;

    UpdateInfo  lastInfo;
    UpdateState state = UpdateState::Idle;
    bool        logoHovered = false;

    juce::String licenseMessage;
    juce::Colour licenseMessageColour;
    bool         deactivating = false;

    IconButton closeButton { IconButton::Glyph::Close, juce::String::fromUTF8 ("Fermer (Échap)") };
    std::array<juce::TextButton, 3> tabButtons;

    // Licence
    juce::TextButton activateButton, buyButton, deactivateButton;
    // Mises à jour
    juce::TextButton   checkButton, downloadButton;
    juce::ToggleButton autoCheckToggle;
    // À propos
    juce::TextButton websiteButton;

    double                animationStartMs = 0.0;
    bool                  dismissing = false;
    std::function<void()> dismissDone;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SettingsDialog)
};

} // namespace aura
