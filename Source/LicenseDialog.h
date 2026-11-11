#pragma once
// ==============================================================================
//  LicenseDialog.h — AuraClean (NextDSP)
//  Modale « Activer AuraClean » : champ de clé + Coller, Activer (avec état de
//  chargement), Acheter (Checkout Lemon Squeezy), messages d'état en français.
//  Même présentation que les autres modales : voile uni + carte #11131A.
// ==============================================================================

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

#include "AuraWidgets.h"
#include "LicenseManager.h"

namespace aura
{
class LicenseDialog : public juce::Component,
                      private juce::Timer
{
public:
    LicenseDialog (LicenseManager&, juce::Image logo);
    ~LicenseDialog() override;

    std::function<void()> onClose;
    std::function<void()> onActivated;  // appelé ~0,9 s après le message de succès

    void dismiss (std::function<void()> onDone);

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void visibilityChanged() override;

private:
    enum class Phase { Idle, Loading, Success, Error };

    void timerCallback() override;
    void startActivation();
    void setPhase (Phase, const juce::String& message = {});
    juce::Rectangle<int> cardBounds() const;

    LicenseManager& license;
    juce::Image     logo;

    IconButton       closeButton { IconButton::Glyph::Close, juce::String::fromUTF8 ("Fermer (Échap)") };
    juce::TextEditor keyField;
    juce::TextButton pasteButton, activateButton, buyButton;

    Phase        phase = Phase::Idle;
    juce::String statusMessage;
    float        spinnerAngle = 0.0f;
    double       successAtMs  = 0.0;

    double                animationStartMs = 0.0;
    bool                  fadingIn   = true;
    bool                  dismissing = false;
    std::function<void()> dismissDone;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LicenseDialog)
};

} // namespace aura
