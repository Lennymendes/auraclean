// ==============================================================================
//  LicenseDialog.cpp — AuraClean (NextDSP)
// ==============================================================================

#include "LicenseDialog.h"

#include <cmath>

namespace aura
{
namespace
{
    juce::String txt (const char* utf8) { return juce::String::fromUTF8 (utf8); }

    constexpr double kFadeInMs     = 150.0;
    constexpr double kFadeOutMs    = 120.0;
    constexpr double kSuccessHold  = 900.0;

    const juce::Colour kVeil       { 0xd807080c };
    const juce::Colour kCard       { 0xff11131a };
    const juce::Colour kCardBorder { 0xff252836 };
    const juce::Colour kField      { 0xff161922 };

    constexpr int kCardW = 580;
    constexpr int kCardH = 392;
    constexpr int kPad   = 32;

    float easeOut (float t) { t = juce::jlimit (0.0f, 1.0f, t); return 1.0f - std::pow (1.0f - t, 3.0f); }

    struct Layout
    {
        juce::Rectangle<int> card, header, subtitle, label, field, message, buttons, footer;

        explicit Layout (juce::Rectangle<int> c) : card (c)
        {
            auto r = c.reduced (kPad, 0);
            r.removeFromTop (22);
            header   = r.removeFromTop (44);
            r.removeFromTop (12);
            subtitle = r.removeFromTop (58);
            r.removeFromTop (16);
            label    = r.removeFromTop (20);
            r.removeFromTop (6);
            field    = r.removeFromTop (46);
            r.removeFromTop (8);
            message  = r.removeFromTop (40);
            r.removeFromTop (10);
            buttons  = r.removeFromTop (42);
            footer   = c.withTop (c.getBottom() - 44).reduced (kPad, 0);
        }
    };
} // namespace

//==============================================================================
LicenseDialog::LicenseDialog (LicenseManager& manager, juce::Image logoImage)
    : license (manager), logo (std::move (logoImage))
{
    closeButton.onClick = [this] { if (onClose) onClose(); };
    addAndMakeVisible (closeButton);

    keyField.setTextToShowWhenEmpty ("XXXX-XXXX-XXXX-XXXX", Theme::textFaint);
    keyField.setFont (juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 17.0f, juce::Font::plain)));  // Menlo sous macOS
    keyField.setJustification (juce::Justification::centred);
    keyField.setInputRestrictions (64, "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-");
    keyField.setColour (juce::TextEditor::backgroundColourId, kField);
    keyField.setColour (juce::TextEditor::outlineColourId, kCardBorder);
    keyField.setSelectAllWhenFocused (true);
    keyField.onReturnKey  = [this] { startActivation(); };
    keyField.onEscapeKey  = [this] { if (onClose) onClose(); };
    keyField.onTextChange = [this]
    {
        if (phase == Phase::Error)
            setPhase (Phase::Idle);
    };
    addAndMakeVisible (keyField);

    pasteButton.setButtonText (txt ("Coller"));
    pasteButton.getProperties().set ("auraStyle", "secondary");
    pasteButton.setTooltip (txt ("Coller la clé depuis le presse-papiers (⌘V)"));
    pasteButton.onClick = [this]
    {
        keyField.setText (LicenseManager::normaliseKey (juce::SystemClipboard::getTextFromClipboard()), true);
        keyField.grabKeyboardFocus();
    };
    addAndMakeVisible (pasteButton);

    activateButton.setButtonText (txt ("Activer la licence"));
    activateButton.getProperties().set ("auraStyle", "primary");
    activateButton.onClick = [this] { startActivation(); };
    addAndMakeVisible (activateButton);

    buyButton.setButtonText (txt ("Acheter une licence (") + LicenseConfig::priceLabel + ")");
    buyButton.getProperties().set ("auraStyle", "secondary");
    buyButton.setMouseCursor (juce::MouseCursor::PointingHandCursor);
    buyButton.onClick = [] { LicenseConfig::openCheckout(); };
    addAndMakeVisible (buyButton);

    setWantsKeyboardFocus (true);
    setAlpha (0.0f);
    animationStartMs = juce::Time::getMillisecondCounterHiRes();
    startTimerHz (60);
}

LicenseDialog::~LicenseDialog()
{
    stopTimer();
}

void LicenseDialog::visibilityChanged()
{
    if (isShowing())
        keyField.grabKeyboardFocus();
}

//==============================================================================
void LicenseDialog::startActivation()
{
    if (phase == Phase::Loading || phase == Phase::Success || dismissing)
        return;

    const auto key = LicenseManager::normaliseKey (keyField.getText());

    if (! LicenseManager::looksLikeKey (key))
    {
        setPhase (Phase::Error, txt ("Format de clé invalide. Collez la clé reçue par e-mail après l'achat."));
        return;
    }

    setPhase (Phase::Loading, txt ("Vérification de la licence auprès de Lemon Squeezy…"));

    juce::Component::SafePointer<LicenseDialog> safe (this);

    license.activate (key, [safe] (const LicenseResult& result)
    {
        auto* self = safe.getComponent();

        if (self == nullptr)
            return;

        if (result.status == LicenseResult::Status::Activated)
            self->setPhase (Phase::Success, result.message);
        else
            self->setPhase (Phase::Error, result.message);
    });
}

void LicenseDialog::setPhase (Phase newPhase, const juce::String& message)
{
    phase         = newPhase;
    statusMessage = message;

    const bool loading = phase == Phase::Loading;
    const bool done    = phase == Phase::Success;

    keyField.setReadOnly (loading || done);
    pasteButton.setEnabled (! loading && ! done);
    activateButton.setEnabled (! loading && ! done);
    activateButton.setButtonText (loading ? txt ("Activation…") : done ? txt ("Licence activée") : txt ("Activer la licence"));

    if (loading || done)
    {
        successAtMs = done ? juce::Time::getMillisecondCounterHiRes() : 0.0;
        startTimerHz (60);  // spinner / attente avant fermeture
    }

    repaint();
}

//==============================================================================
void LicenseDialog::dismiss (std::function<void()> onDone)
{
    if (dismissing)
        return;

    dismissing  = true;
    dismissDone = std::move (onDone);
    setEnabled (false);
    animationStartMs = juce::Time::getMillisecondCounterHiRes();
    startTimerHz (60);
}

void LicenseDialog::timerCallback()
{
    const auto now     = juce::Time::getMillisecondCounterHiRes();
    const auto elapsed = now - animationStartMs;

    if (dismissing)
    {
        const auto t = (float) (elapsed / kFadeOutMs);
        setAlpha (1.0f - easeOut (t));

        if (t >= 1.0f)
        {
            stopTimer();
            setVisible (false);

            if (auto done = std::move (dismissDone))
                done();
        }
        return;
    }

    bool keepRunning = false;

    if (fadingIn)
    {
        const auto t = (float) (elapsed / kFadeInMs);
        setAlpha (easeOut (t));
        fadingIn = t < 1.0f;
        keepRunning = fadingIn;
    }

    if (phase == Phase::Loading)
    {
        spinnerAngle = std::fmod (spinnerAngle + 0.16f, juce::MathConstants<float>::twoPi);
        repaint (Layout (cardBounds()).message);
        keepRunning = true;
    }

    if (phase == Phase::Success)
    {
        if (now - successAtMs >= kSuccessHold)
        {
            phase = Phase::Idle;  // évite un double appel
            stopTimer();

            if (onActivated)
                onActivated();
            return;
        }

        keepRunning = true;
    }

    if (! keepRunning)
        stopTimer();
}

//==============================================================================
juce::Rectangle<int> LicenseDialog::cardBounds() const
{
    return getLocalBounds().withSizeKeepingCentre (juce::jmin (kCardW, getWidth() - 60), juce::jmin (kCardH, getHeight() - 40));
}

void LicenseDialog::resized()
{
    const Layout l (cardBounds());

    auto header = l.header;
    closeButton.setBounds (header.removeFromRight (34).withSizeKeepingCentre (34, 34));

    auto field = l.field;
    pasteButton.setBounds (field.removeFromRight (104));
    field.removeFromRight (10);
    keyField.setBounds (field);

    auto buttons = l.buttons;
    activateButton.setBounds (buttons.removeFromLeft (228));
    buttons.removeFromLeft (10);
    buyButton.setBounds (buttons.removeFromLeft (260));
}

void LicenseDialog::paint (juce::Graphics& g)
{
    // Voile uni puis carte unie : aucun dégradé de fond
    g.fillAll (kVeil);

    const Layout l (cardBounds());
    const auto cardF = l.card.toFloat();

    juce::Path cardShape;
    cardShape.addRoundedRectangle (cardF, 12.0f);
    juce::DropShadow (juce::Colours::black.withAlpha (0.55f), 48, { 0, 18 }).drawForPath (g, cardShape);

    g.setColour (kCard);
    g.fillRoundedRectangle (cardF, 12.0f);
    g.setColour (kCardBorder);
    g.drawRoundedRectangle (cardF.reduced (0.5f), 12.0f, 1.0f);

    // ---- En-tête : logo + titre
    auto header = l.header;
    const auto logoArea = header.removeFromLeft (44).toFloat();
    g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
    g.drawImage (logo, logoArea, juce::RectanglePlacement::centred);

    header.removeFromLeft (14);
    g.setColour (Theme::text);
    g.setFont (Theme::font (23.0f, true));
    g.drawText (txt ("Activer AuraClean"), header.withTrimmedRight (40), juce::Justification::centredLeft, false);

    // ---- Sous-titre
    g.setColour (Theme::textDim);
    g.setFont (Theme::font (14.5f));
    g.drawFittedText (txt ("Débloquez la désinstallation Deep Clean et la quarantaine illimitée "
                           "(Licence perpétuelle — 3 Macs inclus, fonctionne hors-ligne après activation)."),
                      l.subtitle, juce::Justification::topLeft, 3, 1.0f);

    g.setColour (Theme::textDim);
    g.setFont (Theme::font (12.0f, true));
    g.drawText (txt ("CLÉ DE LICENCE"), l.label, juce::Justification::centredLeft, false);

    // ---- Message d'état
    if (statusMessage.isNotEmpty())
    {
        auto msg = l.message;
        juce::Colour colour = Theme::textDim;

        if (phase == Phase::Loading)
        {
            const auto spinner = msg.removeFromLeft (20).toFloat().withSizeKeepingCentre (14.0f, 14.0f);
            juce::Path arc;
            arc.addCentredArc (spinner.getCentreX(), spinner.getCentreY(), 6.5f, 6.5f, spinnerAngle,
                               0.0f, juce::MathConstants<float>::pi * 1.4f, true);
            g.setColour (Theme::cyan);
            g.strokePath (arc, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            msg.removeFromLeft (8);
        }
        else
        {
            colour = phase == Phase::Success ? Theme::success : phase == Phase::Error ? Theme::warning : Theme::textDim;
            const auto dot = msg.removeFromLeft (10).toFloat().withSizeKeepingCentre (8.0f, 8.0f);
            g.setColour (colour);
            g.fillEllipse (dot.withY ((float) msg.getY() + 6.0f));
            msg.removeFromLeft (10);
        }

        g.setColour (phase == Phase::Loading ? Theme::textDim : colour.interpolatedWith (Theme::text, 0.25f));
        g.setFont (Theme::font (13.5f));
        g.drawFittedText (statusMessage, msg, juce::Justification::topLeft, 2, 1.0f);
    }

    // ---- Pied
    g.setColour (Theme::textFaint);
    g.setFont (Theme::font (12.0f));
    g.drawText (txt ("Paiement sécurisé par Lemon Squeezy  ·  La clé vous est envoyée par e-mail"),
                l.footer, juce::Justification::centred, false);
}

//==============================================================================
bool LicenseDialog::keyPressed (const juce::KeyPress& key)
{
    if (dismissing)
        return true;

    if (key == juce::KeyPress::escapeKey)
    {
        if (onClose)
            onClose();
        return true;
    }

    if (key == juce::KeyPress::returnKey)
    {
        startActivation();
        return true;
    }

    return true;  // modal
}

void LicenseDialog::mouseUp (const juce::MouseEvent& e)
{
    if (! dismissing && ! cardBounds().contains (e.getPosition()) && e.mouseWasClicked() && onClose)
        onClose();
}

} // namespace aura
