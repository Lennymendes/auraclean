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

    constexpr int kCardW  = 640;
    constexpr int kCardH  = 430;
    constexpr int kPad    = 32;
    constexpr int kLogo   = 96;
    constexpr int kTabW   = 156;

    float easeOut (float t) { t = juce::jlimit (0.0f, 1.0f, t); return 1.0f - std::pow (1.0f - t, 3.0f); }

    /** Géométrie commune de la carte, partagée par paint() et resized(). */
    struct Layout
    {
        juce::Rectangle<int> card, header, tabs, content, copyright;
        int dividerY = 0;

        explicit Layout (juce::Rectangle<int> c) : card (c)
        {
            auto r = c.reduced (kPad, 0);
            header    = r.removeFromTop (68);
            dividerY  = r.getY();
            r.removeFromTop (18);
            tabs      = r.removeFromTop (36).withWidth (kTabW * 3 + 8);
            r.removeFromTop (22);
            copyright = c.withTop (c.getBottom() - 40).reduced (kPad, 0);
            content   = r.withBottom (copyright.getY() - 4);
        }
    };

    struct LicenseLayout
    {
        juce::Rectangle<int> label, status, buttons, message;

        explicit LicenseLayout (juce::Rectangle<int> r)
        {
            label   = r.removeFromTop (22);  r.removeFromTop (6);
            status  = r.removeFromTop (88);  r.removeFromTop (16);
            buttons = r.removeFromTop (36);  r.removeFromTop (12);
            message = r.removeFromTop (40);
        }
    };

    struct UpdatesLayout
    {
        juce::Rectangle<int> label, status, buttons, toggle;

        explicit UpdatesLayout (juce::Rectangle<int> r)
        {
            label   = r.removeFromTop (22);  r.removeFromTop (6);
            status  = r.removeFromTop (64);  r.removeFromTop (14);
            buttons = r.removeFromTop (36);  r.removeFromTop (14);
            toggle  = r.removeFromTop (30);
        }
    };

    struct AboutLayout
    {
        juce::Rectangle<int> label, logo, text;

        explicit AboutLayout (juce::Rectangle<int> r)
        {
            label = r.removeFromTop (22);  r.removeFromTop (12);
            auto block = r.removeFromTop (kLogo + 16);
            logo  = block.withWidth (kLogo).withHeight (kLogo).withY (block.getY() + 4);
            text  = block.withTrimmedLeft (kLogo + 22);
        }
    };

    void drawInset (juce::Graphics& g, juce::Rectangle<int> r)
    {
        g.setColour (kInset);
        g.fillRoundedRectangle (r.toFloat(), 9.0f);
        g.setColour (kCardBorder);
        g.drawRoundedRectangle (r.toFloat().reduced (0.5f), 9.0f, 1.0f);
    }

    void drawSectionLabel (juce::Graphics& g, const juce::String& label, juce::Rectangle<int> r)
    {
        g.setColour (Theme::textDim);
        g.setFont (Theme::font (12.0f, true));
        g.drawText (label, r, juce::Justification::centredLeft, false);
    }
} // namespace

//==============================================================================
SettingsDialog::SettingsDialog (juce::Image logoImage, juce::String currentVersion, bool autoCheckEnabled,
                                LicenseManager& manager, Tab initialTab)
    : logo (std::move (logoImage)), version (std::move (currentVersion)), license (manager), tab (initialTab)
{
    lastInfo.currentVersion = version;

    closeButton.onClick = [this] { if (onClose) onClose(); };
    addAndMakeVisible (closeButton);

    const char* tabNames[] = { "Licence", "Mises à jour", "À propos" };

    for (int i = 0; i < 3; ++i)
    {
        auto& b = tabButtons[(size_t) i];
        b.setButtonText (txt (tabNames[i]));
        b.getProperties().set ("auraStyle", "tab");
        b.setClickingTogglesState (true);
        b.setRadioGroupId (4242);
        b.onClick = [this, i] { selectTab ((Tab) i); };
        addAndMakeVisible (b);
    }

    // ---- Licence
    activateButton.setButtonText (txt ("Activer une licence…"));
    activateButton.getProperties().set ("auraStyle", "primary");
    activateButton.onClick = [this] { if (onActivateLicense) onActivateLicense(); };
    addChildComponent (activateButton);

    buyButton.setButtonText (txt ("Acheter (") + LicenseConfig::priceLabel + ")");
    buyButton.getProperties().set ("auraStyle", "secondary");
    buyButton.setMouseCursor (juce::MouseCursor::PointingHandCursor);
    buyButton.onClick = [] { LicenseConfig::openCheckout(); };
    addChildComponent (buyButton);

    deactivateButton.setButtonText (txt ("Désactiver ce Mac"));
    deactivateButton.getProperties().set ("auraStyle", "secondary");
    deactivateButton.onClick = [this] { confirmDeactivation(); };
    addChildComponent (deactivateButton);

    // ---- Mises à jour
    checkButton.setButtonText (txt ("Rechercher des mises à jour"));
    checkButton.getProperties().set ("auraStyle", "secondary");
    checkButton.onClick = [this] { if (onCheckForUpdates) onCheckForUpdates(); };
    addChildComponent (checkButton);

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
    addChildComponent (autoCheckToggle);

    // ---- À propos
    websiteButton.setButtonText (txt ("Visiter ") + AppInfo::websiteLabel);
    websiteButton.getProperties().set ("auraStyle", "secondary");
    websiteButton.setMouseCursor (juce::MouseCursor::PointingHandCursor);
    websiteButton.onClick = [] { AppInfo::openWebsite(); };
    addChildComponent (websiteButton);

    selectTab (initialTab);

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
void SettingsDialog::selectTab (Tab newTab)
{
    tab = newTab;
    tabButtons[(size_t) tab].setToggleState (true, juce::dontSendNotification);
    logoHovered = false;
    updateVisibility();
    repaint();
}

void SettingsDialog::updateVisibility()
{
    const bool lic    = tab == Tab::License;
    const bool upd    = tab == Tab::Updates;
    const bool abt    = tab == Tab::About;
    const bool active = license.isActivated();

    activateButton.setVisible (lic && ! active);
    buyButton.setVisible (lic && ! active);
    deactivateButton.setVisible (lic && active);
    deactivateButton.setEnabled (! deactivating);
    deactivateButton.setButtonText (deactivating ? txt ("Désactivation…") : txt ("Désactiver ce Mac"));

    checkButton.setVisible (upd);
    checkButton.setEnabled (state != UpdateState::Checking);
    downloadButton.setVisible (upd && state == UpdateState::Available);
    autoCheckToggle.setVisible (upd);

    websiteButton.setVisible (abt);
}

void SettingsDialog::refreshLicense()
{
    updateVisibility();
    repaint();
}

void SettingsDialog::confirmDeactivation()
{
    if (deactivating || ! license.isActivated())
        return;

    juce::Component::SafePointer<SettingsDialog> safe (this);

    juce::AlertWindow::showAsync (juce::MessageBoxOptions()
                                      .withIconType (juce::MessageBoxIconType::QuestionIcon)
                                      .withTitle (txt ("Désactiver AuraClean sur ce Mac ?"))
                                      .withMessage (txt ("La licence sera libérée sur ce Mac : vous pourrez l'activer sur un autre ordinateur. "
                                                         "AuraClean repassera en Mode Aperçu (scan uniquement) ici."))
                                      .withButton (txt ("Désactiver ce Mac"))
                                      .withButton (txt ("Annuler"))
                                      .withAssociatedComponent (this),
                                  [safe] (int result)
                                  {
                                      auto* self = safe.getComponent();

                                      if (self == nullptr || result != 1)
                                          return;

                                      self->deactivating   = true;
                                      self->licenseMessage = {};
                                      self->refreshLicense();

                                      self->license.deactivate ([safe] (const LicenseResult& r)
                                      {
                                          if (auto* s = safe.getComponent())
                                          {
                                              s->deactivating         = false;
                                              s->licenseMessage       = r.message;
                                              s->licenseMessageColour = r.ok() ? Theme::success : Theme::warning;
                                              s->refreshLicense();
                                          }
                                      });
                                  });
}

//==============================================================================
void SettingsDialog::setChecking()
{
    state = UpdateState::Checking;
    selectTab (Tab::Updates);
}

void SettingsDialog::showUpdateResult (const UpdateInfo& info)
{
    lastInfo = info;
    state = info.status == UpdateInfo::Status::UpdateAvailable ? UpdateState::Available
          : info.status == UpdateInfo::Status::UpToDate        ? UpdateState::UpToDate
                                                               : UpdateState::Error;
    downloadButton.setButtonText (txt ("Télécharger la ") + info.latestVersion);
    updateVisibility();
    repaint();
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
    return tab == Tab::About ? AboutLayout (Layout (cardBounds()).content).logo : juce::Rectangle<int>();
}

void SettingsDialog::resized()
{
    const Layout l (cardBounds());

    auto header = l.header;
    closeButton.setBounds (header.removeFromRight (34).withSizeKeepingCentre (34, 34));

    auto tabs = l.tabs.reduced (4);
    for (auto& b : tabButtons)
        b.setBounds (tabs.removeFromLeft (kTabW));

    {
        const LicenseLayout ll (l.content);
        auto row = ll.buttons;
        activateButton.setBounds (row.removeFromLeft (220));
        row.removeFromLeft (10);
        buyButton.setBounds (row.removeFromLeft (190));
        deactivateButton.setBounds (ll.buttons.withWidth (200));
    }

    {
        const UpdatesLayout ul (l.content);
        auto row = ul.buttons;
        checkButton.setBounds (row.removeFromLeft (250));
        row.removeFromLeft (10);
        downloadButton.setBounds (row.removeFromLeft (210));
        autoCheckToggle.setBounds (ul.toggle);
    }

    {
        const AboutLayout al (l.content);
        websiteButton.setBounds (al.text.withTop (al.text.getBottom() - 34).withWidth (190));
    }
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
    g.fillRect (l.card.getX() + 1, l.dividerY, l.card.getWidth() - 2, 1);

    // ---- Onglets : fond segmenté
    drawInset (g, l.tabs);

    switch (tab)
    {
        case Tab::License: paintLicenseTab (g, l.content); break;
        case Tab::Updates: paintUpdatesTab (g, l.content); break;
        case Tab::About:   paintAboutTab (g, l.content);   break;
    }

    g.setColour (Theme::textFaint);
    g.setFont (Theme::font (12.0f));
    g.drawText (txt (AppInfo::copyrightLine), l.copyright, juce::Justification::centred, false);
}

//==============================================================================
void SettingsDialog::paintLicenseTab (juce::Graphics& g, juce::Rectangle<int> content)
{
    const LicenseLayout ll (content);
    drawSectionLabel (g, txt ("LICENCE"), ll.label);
    drawInset (g, ll.status);

    auto inner = ll.status.reduced (18, 14);

    if (license.isActivated())
    {
        // Badge vert « Licence Active »
        const auto badge = inner.removeFromTop (22).withWidth (132).toFloat();
        g.setColour (Theme::success.withAlpha (0.14f));
        g.fillRoundedRectangle (badge, 11.0f);
        g.setColour (Theme::success.withAlpha (0.7f));
        g.drawRoundedRectangle (badge.reduced (0.5f), 11.0f, 1.0f);
        g.setColour (Theme::success);
        g.fillEllipse (juce::Rectangle<float> (7.0f, 7.0f).withCentre ({ badge.getX() + 14.0f, badge.getCentreY() }));
        g.setFont (Theme::font (11.5f, true));
        g.drawText (txt ("LICENCE ACTIVE"), badge.withTrimmedLeft (24.0f), juce::Justification::centredLeft, false);

        inner.removeFromTop (8);
        const auto& lic = license.current();

        g.setColour (Theme::text);
        g.setFont (Theme::font (15.0f, true));
        g.drawText (txt ("Clé : ") + license.maskedKey(), inner.removeFromTop (22), juce::Justification::centredLeft, true);

        auto details = txt ("Ce Mac : ") + (lic.instanceName.isNotEmpty() ? lic.instanceName : LicenseManager::instanceName());
        const auto when = juce::Time::fromISO8601 (lic.activatedAt);

        if (when.toMilliseconds() > 0)
            details << txt ("  ·  activée le ") << when.formatted ("%d/%m/%Y");

        g.setColour (Theme::textDim);
        g.setFont (Theme::font (13.0f));
        g.drawText (details, inner, juce::Justification::centredLeft, true);
    }
    else
    {
        const auto dot = inner.removeFromLeft (10).toFloat().withSizeKeepingCentre (8.0f, 8.0f).withY ((float) inner.getY() + 8.0f);
        g.setColour (Theme::violet);
        g.fillEllipse (dot);
        inner.removeFromLeft (12);

        g.setColour (Theme::text);
        g.setFont (Theme::font (15.0f, true));
        g.drawText (txt ("Mode Aperçu (Scan uniquement)"), inner.removeFromTop (24), juce::Justification::centredLeft, true);

        g.setColour (Theme::textDim);
        g.setFont (Theme::font (13.0f));
        g.drawFittedText (txt ("Scan, filtres et prévisualisation des fichiers résiduels gratuits. "
                               "La désinstallation Deep Clean et la quarantaine nécessitent une licence."),
                          inner, juce::Justification::topLeft, 2, 1.0f);
    }

    // Message : résultat d'une désactivation, fichier refusé, ou aide
    auto message = licenseMessage;
    auto colour  = licenseMessageColour;

    if (message.isEmpty() && license.loadProblem().isNotEmpty() && ! license.isActivated())
    {
        message = license.loadProblem();
        colour  = Theme::warning;
    }

    if (message.isEmpty())
    {
        message = license.isActivated()
                    ? txt ("Désactiver ce Mac libère l'un de vos ") + juce::String (LicenseConfig::maxMachines)
                          + txt (" emplacements, pour utiliser AuraClean sur un autre ordinateur.")
                    : txt ("Licence perpétuelle — ") + juce::String (LicenseConfig::maxMachines)
                          + txt (" Macs inclus, fonctionne hors-ligne après activation.");
        colour = Theme::textFaint;
    }

    g.setColour (colour);
    g.setFont (Theme::font (13.0f));
    g.drawFittedText (message, ll.message, juce::Justification::topLeft, 2, 1.0f);
}

void SettingsDialog::paintUpdatesTab (juce::Graphics& g, juce::Rectangle<int> content)
{
    const UpdatesLayout ul (content);
    drawSectionLabel (g, txt ("MISES À JOUR"), ul.label);
    drawInset (g, ul.status);

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

    auto inner = ul.status.reduced (18, 12);
    g.setColour (dot);
    g.fillEllipse (inner.removeFromLeft (10).toFloat().withSizeKeepingCentre (8.0f, 8.0f).translated (0.0f, -9.0f));
    inner.removeFromLeft (12);

    g.setColour (Theme::text);
    g.setFont (Theme::font (15.0f, true));
    g.drawText (title, inner.removeFromTop (inner.getHeight() / 2), juce::Justification::centredLeft, true);
    g.setColour (Theme::textDim);
    g.setFont (Theme::font (13.0f));
    g.drawText (detail, inner, juce::Justification::centredLeft, true);
}

void SettingsDialog::paintAboutTab (juce::Graphics& g, juce::Rectangle<int> content)
{
    const AboutLayout al (content);
    drawSectionLabel (g, txt ("À PROPOS"), al.label);

    const auto logoF = al.logo.toFloat();

    if (logoHovered)
    {
        g.setColour (Theme::cyan.withAlpha (0.55f));
        g.drawRoundedRectangle (logoF.expanded (4.0f), 24.0f, 1.5f);
    }

    g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
    g.drawImage (logo, logoF, juce::RectanglePlacement::centred);

    auto text = al.text;
    text.removeFromBottom (34);  // bouton du site

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

    g.setColour (Theme::violet);
    g.fillPath (Icons::heart ({ credit.getX() + beforeW + 1.0f, credit.getCentreY() - 6.0f, 14.0f, 12.0f }));

    g.setColour (Theme::text);
    g.drawText (after, credit.withTrimmedLeft (beforeW + 16.0f), juce::Justification::centredLeft, false);
}

//==============================================================================
void SettingsDialog::mouseMove (const juce::MouseEvent& e)
{
    const bool over = logoBounds().contains (e.getPosition());

    if (over != logoHovered)
    {
        logoHovered = over;
        setMouseCursor (over ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
        repaint();
    }
}

void SettingsDialog::mouseExit (const juce::MouseEvent&)
{
    if (logoHovered)
    {
        logoHovered = false;
        setMouseCursor (juce::MouseCursor::NormalCursor);
        repaint();
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
