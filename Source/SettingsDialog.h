#pragma once
// ==============================================================================
//  SettingsDialog.h — AuraClean (NextDSP)
//
//  Panneau « Réglages » (bouton engrenage de l'en-tête, ⌘, ou menu AuraClean) :
//    - Mises à jour : version installée, état, « Rechercher des mises à jour »,
//      « Télécharger », interrupteur « Rechercher automatiquement au lancement »
//    - À propos : logo cliquable (→ site), version, « Développé avec amour par ZEYN »
//
//  Même présentation que DeepCleanDialog : voile uni + carte #11131A, fondus.
//  Aucun setGradientFill direct (voir Theme::fillPathWithGradient).
// ==============================================================================

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

#include "AuraWidgets.h"
#include "UpdateChecker.h"

namespace aura
{
class SettingsDialog : public juce::Component,
                       private juce::Timer
{
public:
    SettingsDialog (juce::Image logo, juce::String currentVersion, bool autoCheckEnabled);
    ~SettingsDialog() override;

    std::function<void()>                  onClose;
    std::function<void()>                  onCheckForUpdates;
    std::function<void (bool)>             onAutoCheckChanged;
    std::function<void (const juce::URL&)> onDownload;

    void setChecking();
    void showUpdateResult (const UpdateInfo&);

    /** Fondu de sortie (120 ms) puis onDone. */
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
    void layoutUpdateButtons();

    juce::Image  logo;
    juce::String version;
    UpdateInfo   lastInfo;
    UpdateState  state = UpdateState::Idle;
    bool         logoHovered = false;

    IconButton         closeButton { IconButton::Glyph::Close, juce::String::fromUTF8 ("Fermer (Échap)") };
    juce::TextButton   checkButton, downloadButton, websiteButton;
    juce::ToggleButton autoCheckToggle;

    double                animationStartMs = 0.0;
    bool                  dismissing = false;
    std::function<void()> dismissDone;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SettingsDialog)
};

} // namespace aura
