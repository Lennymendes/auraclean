// ==============================================================================
//  SettingsDialog.cpp — AuraClean (NextDSP)
// ==============================================================================

#include "SettingsDialog.h"
#include "AppInfo.h"

#include <cmath>

namespace aura
{
namespace
{
    juce::String txt (const char* utf8) { return juce::String::fromUTF8 (utf8); }

    constexpr double kFadeInMs  = 150.0;
    constexpr double kFadeOutMs = 120.0;

    const juce::Colour kVeil       { 0xd807080c };
    const juce::Colour kCard       { 0xff11131a };
    const juce::Colour kCardBorder { 0xff252836 };
    const juce::Colour kInset      { 0xff161922 };

    constexpr int kCardW   = 620;
    constexpr int kCardH   = 540;
    constexpr int kPad     = 32;
    constexpr int kLogo    = 96;

    float easeOut (float t) { t = juce::jlimit (0.0f, 1.0f, t); return 1.0f - std::pow (1.0f - t, 3.0f); }

    /** Toute la géométrie de la carte, partagée par paint() et resized(). */
    struct Layout
    {
        juce::Rectangle<int> card, header, updatesLabel, status, buttons, toggle, aboutLabel, about, logo, aboutText, copyright;
        int dividerY1 = 0, dividerY2 = 0;

        explicit Layout (juce::Rectangle<int> c) : card (c)
        {
            auto r = c.reduced (kPad, 0);
            header = r.removeFromTop (68);
            dividerY1 = r.getY();

            r.removeFromTop (18);
            updatesLabel = r.removeFromTop (22);
            r.removeFromTop (6);
            status = r.removeFromTop (64);
            r.removeFromTop (14);
            buttons = r.removeFromTop (36);
            r.removeFromTop (12);
            toggle = r.removeFromTop (30);
            r.removeFromTop (20);
            dividerY2 = r.getY();

            r.removeFromTop (18);
            aboutLabel = r.removeFromTop (22);
            r.removeFromTop (10);
            about = r.removeFromTop (kLogo + 8);
            logo = about.withWidth (kLogo).withHeight (kLogo).withY (about.getY() + 4);
            aboutText = about.withTrimmedLeft (kLogo + 22);
            copyright = c.withTop (c.getBottom() - 40).reduced (kPad, 0);
        }
    };
} // namespace

//==============================================================================
SettingsDialog::SettingsDialog (juce::Image logoImage, juce::String currentVersion, bool autoCheckEnabled)
    : logo (std::move (logoImage)), version (std::move (currentVersion))
{
    lastInfo.currentVersion = version;

    closeButton.onClick = [this] { if (onClose) onClose(); };
    addAndMakeVisible (closeButton);

    checkButton.setButtonText (txt ("Rechercher des mises à jour"));
    checkButton.getProperties().set ("auraStyle", "secondary");
    checkButton.onClick = [this] { if (onCheckForUpdates) onCheckForUpdates(); };
    addAndMakeVisible (checkButton);

    downloadButton.getProperties().set ("auraStyle", "primary");
    downloadButton.onClick = [this]
    {
        if (onDownload && UpdateChecker::isTrustedDownloadUrl (lastInfo.downloadUrl))
            onDownload (lastInfo.downloadUrl);
    };
    addChildComponent (downloadButton);

    autoCheckToggle.setButtonText (txt ("Rechercher automatiquement au lancement"));
    autoCheckToggle.setToggleState (autoCheckEnabled, juce::dontSendNotification);
    autoCheckToggle.onClick = [this] { if (onAutoCheckChanged) onAutoCheckChanged (autoCheckToggle.getToggleState()); };
    addAndMakeVisible (autoCheckToggle);

    websiteButton.setButtonText (txt ("Visiter ") + AppInfo::websiteLabel);
    websiteButton.getProperties().set ("auraStyle", "secondary");
    websiteButton.setMouseCursor (juce::MouseCursor::PointingHandCursor);
    websiteButton.onClick = [] { AppInfo::openWebsite(); };
    addAndMakeVisible (websiteButton);

    setWantsKeyboardFocus (true);
    setAlpha (0.0f);
    animationStartMs = juce::Time::getMillisecondCounterHiRes();
    startTimerHz (60);
}

SettingsDialog::~SettingsDialog()
{
    stopTimer();
}

//==============================================================================
void SettingsDialog::setChecking()
{
    state = UpdateState::Checking;
    checkButton.setEnabled (false);
    layoutUpdateButtons();
    repaint();
}

void SettingsDialog::showUpdateResult (const UpdateInfo& info)
{
    lastInfo = info;
    state = info.status == UpdateInfo::Status::UpdateAvailable ? UpdateState::Available
          : info.status == UpdateInfo::Status::UpToDate        ? UpdateState::UpToDate
                                                               : UpdateState::Error;
    checkButton.setEnabled (true);
    downloadButton.setButtonText (txt ("Télécharger la ") + info.latestVersion);
    layoutUpdateButtons();
    repaint();
}

void SettingsDialog::layoutUpdateButtons()
{
    const Layout l (cardBounds());
    auto row = l.buttons;

    checkButton.setBounds (row.removeFromLeft (250));
    row.removeFromLeft (10);
    downloadButton.setBounds (row.removeFromLeft (210));
    downloadButton.setVisible (state == UpdateState::Available);
}

//==============================================================================
void SettingsDialog::dismiss (std::function<void()> onDone)
{
    if (dismissing)
        return;

    dismissing  = true;
    dismissDone = std::move (onDone);
    setEnabled (false);
    animationStartMs = juce::Time::getMillisecondCounterHiRes();
    startTimerHz (60);
}

void SettingsDialog::timerCallback()
{
    const auto elapsed = juce::Time::getMillisecondCounterHiRes() - animationStartMs;

    if (! dismissing)
    {
        const auto t = (float) (elapsed / kFadeInMs);
        setAlpha (easeOut (t));

        if (t >= 1.0f)
        {
            setAlpha (1.0f);
            stopTimer();
        }
        return;
    }

    const auto t = (float) (elapsed / kFadeOutMs);
    setAlpha (1.0f - easeOut (t));

    if (t >= 1.0f)
    {
        stopTimer();
        setVisible (false);

        if (auto done = std::move (dismissDone))
            done();
    }
}

//==============================================================================
juce::Rectangle<int> SettingsDialog::cardBounds() const
{
    return getLocalBounds().withSizeKeepingCentre (juce::jmin (kCardW, getWidth() - 60), juce::jmin (kCardH, getHeight() - 40));
}

juce::Rectangle<int> SettingsDialog::logoBounds() const
{
    return Layout (cardBounds()).logo;
}

void SettingsDialog::resized()
{
    const Layout l (cardBounds());

    auto header = l.header;
    closeButton.setBounds (header.removeFromRight (34).withSizeKeepingCentre (34, 34));
    layoutUpdateButtons();
    autoCheckToggle.setBounds (l.toggle);

    auto text = l.aboutText;
    websiteButton.setBounds (text.removeFromBottom (32).withWidth (190));
}

void SettingsDialog::paint (juce::Graphics& g)
{
    // 1. Voile uni — 2. carte unie (aucun dégradé de fond)
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

    // ---- En-tête
    auto header = l.header;
    const auto gearArea = header.removeFromLeft (26).toFloat().withSizeKeepingCentre (22.0f, 22.0f);
    g.setColour (Theme::cyan);
    g.fillPath (Icons::gear (gearArea));

    header.removeFromLeft (12);
    g.setColour (Theme::text);
    g.setFont (Theme::font (22.0f, true));
    g.drawText (txt ("Réglages"), header, juce::Justification::centredLeft, false);

    g.setColour (kCardBorder);
    g.fillRect (l.card.getX() + 1, l.dividerY1, l.card.getWidth() - 2, 1);
    g.fillRect (l.card.getX() + kPad, l.dividerY2, l.card.getWidth() - 2 * kPad, 1);

    // ---- Section « Mises à jour »
    g.setColour (Theme::textDim);
    g.setFont (Theme::font (12.0f, true));
    g.drawText (txt ("MISES À JOUR"), l.updatesLabel, juce::Justification::centredLeft, false);

    g.setColour (kInset);
    g.fillRoundedRectangle (l.status.toFloat(), 9.0f);
    g.setColour (kCardBorder);
    g.drawRoundedRectangle (l.status.toFloat().reduced (0.5f), 9.0f, 1.0f);

    juce::String title, detail;
    juce::Colour dot = Theme::textFaint;

    switch (state)
    {
        case UpdateState::Idle:
            title  = txt ("Version installée : ") + version;
            detail = txt ("Cliquez sur « Rechercher des mises à jour » pour vérifier maintenant.");
            break;
        case UpdateState::Checking:
            title  = txt ("Recherche en cours…");
            detail = txt ("Connexion au serveur de mise à jour NextDSP.");
            dot    = Theme::violet;
            break;
        case UpdateState::UpToDate:
            title  = txt ("AuraClean est à jour");
            detail = txt ("Vous utilisez la dernière version (") + version + ").";
            dot    = Theme::success;
            break;
        case UpdateState::Available:
            title  = txt ("AuraClean ") + lastInfo.latestVersion + txt (" est disponible");
            detail = lastInfo.releaseNotes.isNotEmpty() ? lastInfo.releaseNotes.upToFirstOccurrenceOf ("\n", false, false)
                                                        : txt ("Vous utilisez la version ") + version + ".";
            dot    = Theme::cyan;
            break;
        case UpdateState::Error:
            title  = txt ("Vérification impossible");
            detail = lastInfo.errorMessage;
            dot    = Theme::warning;
            break;
    }

    auto statusInner = l.status.reduced (18, 12);
    const auto dotArea = statusInner.removeFromLeft (10).toFloat().withSizeKeepingCentre (8.0f, 8.0f).translated (0.0f, -9.0f);
    g.setColour (dot);
    g.fillEllipse (dotArea);
    statusInner.removeFromLeft (12);

    g.setColour (Theme::text);
    g.setFont (Theme::font (15.0f, true));
    g.drawText (title, statusInner.removeFromTop (statusInner.getHeight() / 2), juce::Justification::centredLeft, true);
    g.setColour (Theme::textDim);
    g.setFont (Theme::font (13.0f));
    g.drawText (detail, statusInner, juce::Justification::centredLeft, true);

    // ---- Section « À propos »
    g.setColour (Theme::textDim);
    g.setFont (Theme::font (12.0f, true));
    g.drawText (txt ("À PROPOS"), l.aboutLabel, juce::Justification::centredLeft, false);

    const auto logoF = l.logo.toFloat();

    if (logoHovered)
    {
        g.setColour (Theme::cyan.withAlpha (0.55f));
        g.drawRoundedRectangle (logoF.expanded (4.0f), 24.0f, 1.5f);
    }

    g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
    g.drawImage (logo, logoF, juce::RectanglePlacement::centred);

    auto text = l.aboutText;
    text.removeFromBottom (32);  // bouton du site

    const auto titleFont = Theme::font (26.0f, true);
    const auto titleRow  = text.removeFromTop (32).toFloat();
    const float auraW    = juce::GlyphArrangement::getStringWidth (titleFont, "Aura ");
    g.setFont (titleFont);
    g.setColour (Theme::text);
    g.drawText ("Aura", titleRow, juce::Justification::centredLeft, false);
    Theme::drawGradientText (g, "Clean", titleFont, titleRow.withTrimmedLeft (auraW),
                             juce::Justification::centredLeft, Theme::cyan, Theme::violet);

    g.setColour (Theme::textDim);
    g.setFont (Theme::font (13.0f));
    g.drawText (txt ("Version ") + version + txt ("  ·  Gestionnaire de plugins VST3 · AU · AAX"),
                text.removeFromTop (20), juce::Justification::centredLeft, true);

    // « Développé avec ♥ par ZEYN » — cœur vectoriel (pas de dépendance à une police emoji)
    const auto creditFont = Theme::font (14.0f, true);
    const auto before = txt ("Développé avec ");
    const auto after  = txt (" par ") + AppInfo::developerName;
    auto credit = text.removeFromTop (24).toFloat();

    g.setFont (creditFont);
    g.setColour (Theme::text);
    const float beforeW = juce::GlyphArrangement::getStringWidth (creditFont, before);
    g.drawText (before, credit, juce::Justification::centredLeft, false);

    const auto heartArea = juce::Rectangle<float> (credit.getX() + beforeW + 1.0f, credit.getCentreY() - 6.0f, 14.0f, 12.0f);
    g.setColour (Theme::violet);
    g.fillPath (Icons::heart (heartArea));

    g.setColour (Theme::text);
    g.drawText (after, credit.withTrimmedLeft (beforeW + 16.0f), juce::Justification::centredLeft, false);

    // ---- Pied
    g.setColour (Theme::textFaint);
    g.setFont (Theme::font (12.0f));
    g.drawText (txt (AppInfo::copyrightLine), l.copyright, juce::Justification::centred, false);
}

//==============================================================================
void SettingsDialog::mouseMove (const juce::MouseEvent& e)
{
    const bool over = logoBounds().contains (e.getPosition());

    if (over != logoHovered)
    {
        logoHovered = over;
        setMouseCursor (over ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
        repaint (logoBounds().expanded (8));
    }
}

void SettingsDialog::mouseExit (const juce::MouseEvent&)
{
    if (logoHovered)
    {
        logoHovered = false;
        setMouseCursor (juce::MouseCursor::NormalCursor);
        repaint (logoBounds().expanded (8));
    }
}

void SettingsDialog::mouseUp (const juce::MouseEvent& e)
{
    if (dismissing)
        return;

    if (logoBounds().contains (e.getPosition()))
    {
        AppInfo::openWebsite();
        return;
    }

    // Clic sur le voile, hors de la carte : fermer
    if (! cardBounds().contains (e.getPosition()) && e.mouseWasClicked() && onClose)
        onClose();
}

bool SettingsDialog::keyPressed (const juce::KeyPress& key)
{
    if (dismissing)
        return true;

    if (key == juce::KeyPress::escapeKey || key == juce::KeyPress ('w', juce::ModifierKeys::commandModifier, 0))
    {
        if (onClose)
            onClose();
        return true;
    }

    return true;  // modal : rien ne passe à la fenêtre principale
}

} // namespace aura
