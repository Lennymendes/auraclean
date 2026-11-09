// ==============================================================================
//  DeepCleanDialog.cpp — AuraClean (NextDSP)
// ==============================================================================

#include "DeepCleanDialog.h"
#include "AuraLookAndFeel.h"
#include "MainComponent.h"  // fr(), formatBytes()

#include <algorithm>

namespace aura
{
namespace
{
    constexpr int kRowHeight   = 38;
    constexpr int kHeaderH     = 104;
    constexpr int kFooterH     = 92;
    constexpr int kBadgeWidth  = 112;
    constexpr int kSizeWidth   = 92;

    enum class Tri { None, Partial, All };

    void drawCheck (juce::Graphics& g, juce::Rectangle<float> r, Tri state, bool enabled)
    {
        const float alpha = enabled ? 1.0f : 0.3f;

        if (state == Tri::None)
        {
            g.setColour (Theme::textFaint.withMultipliedAlpha (alpha));
            g.drawRoundedRectangle (r.reduced (0.5f), 4.0f, 1.3f);
            return;
        }

        g.setGradientFill (juce::ColourGradient (Theme::violet.withMultipliedAlpha (alpha), r.getX(), r.getY(),
                                                 Theme::cyan.withMultipliedAlpha (alpha), r.getRight(), r.getBottom(), false));
        g.fillRoundedRectangle (r, 4.0f);
        g.setColour (juce::Colours::white.withMultipliedAlpha (alpha));

        if (state == Tri::All)
        {
            juce::Path tick;
            tick.startNewSubPath (r.getX() + 3.6f, r.getCentreY());
            tick.lineTo (r.getX() + 6.4f, r.getBottom() - 4.0f);
            tick.lineTo (r.getRight() - 3.4f, r.getY() + 4.2f);
            g.strokePath (tick, juce::PathStrokeType (1.9f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
        else
        {
            g.fillRoundedRectangle (r.reduced (3.5f, 6.5f), 1.0f);
        }
    }

    void drawChevron (juce::Graphics& g, juce::Rectangle<float> r, bool expanded)
    {
        juce::Path p;
        const auto c = r.getCentre();

        if (expanded)
        {
            p.startNewSubPath (c.x - 4.0f, c.y - 2.0f);
            p.lineTo (c.x, c.y + 2.5f);
            p.lineTo (c.x + 4.0f, c.y - 2.0f);
        }
        else
        {
            p.startNewSubPath (c.x - 2.0f, c.y - 4.0f);
            p.lineTo (c.x + 2.5f, c.y);
            p.lineTo (c.x - 2.0f, c.y + 4.0f);
        }

        g.setColour (Theme::textDim);
        g.strokePath (p, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    Tri groupState (const CleanGroup& g)
    {
        int enabled = 0, checked = 0;

        for (const auto& it : g.items)
        {
            if (! it.enabled)
                continue;

            ++enabled;
            checked += it.checked ? 1 : 0;
        }

        if (checked == 0)
            return Tri::None;

        return checked == enabled ? Tri::All : Tri::Partial;
    }
} // namespace

//==============================================================================
DeepCleanDialog::DeepCleanDialog (std::vector<CleanGroup> g, juce::String warningText, bool leftoversSearched)
    : groups (std::move (g)), warning (std::move (warningText)), searched (leftoversSearched)
{
    for (auto& group : groups)
    {
        for (auto& item : group.items)
            item.checked = item.defaultChecked;

        enforceRules (group);
    }

    list.setModel (this);
    list.setRowHeight (kRowHeight);
    list.setOutlineThickness (0);
    list.setColour (juce::ListBox::backgroundColourId, Theme::panel);
    addAndMakeVisible (list);

    cancelButton.setButtonText (fr ("Annuler"));
    cancelButton.getProperties().set ("auraStyle", "ghost");
    cancelButton.onClick = [this] { if (onCancel) onCancel(); };
    addAndMakeVisible (cancelButton);

    confirmButton.getProperties().set ("auraStyle", "danger");
    confirmButton.onClick = [this]
    {
        std::vector<CleanCandidate> selected;

        for (const auto& group : groups)
            for (const auto& item : group.items)
                if (item.enabled && item.checked)
                    selected.push_back (item);

        if (! selected.empty() && onConfirm)
            onConfirm (std::move (selected));
    };
    addAndMakeVisible (confirmButton);

    expandButton.setButtonText (fr ("Tout replier"));
    expandButton.getProperties().set ("auraStyle", "ghost");
    expandButton.onClick = [this]
    {
        const bool anyExpanded = std::any_of (groups.begin(), groups.end(), [] (const CleanGroup& cg) { return cg.expanded; });

        for (auto& group : groups)
            group.expanded = ! anyExpanded;

        expandButton.setButtonText (anyExpanded ? fr ("Tout déplier") : fr ("Tout replier"));
        rebuildRows();
    };
    addAndMakeVisible (expandButton);

    setWantsKeyboardFocus (true);
    rebuildRows();
}

juce::String DeepCleanDialog::displayPath (const juce::File& f)
{
    const auto home = juce::File::getSpecialLocation (juce::File::userHomeDirectory).getFullPathName();
    const auto path = f.getFullPathName();
    return path.startsWith (home + "/") ? "~" + path.substring (home.length()) : path;
}

void DeepCleanDialog::rebuildRows()
{
    rows.clear();

    for (int gi = 0; gi < (int) groups.size(); ++gi)
    {
        rows.push_back ({ gi, -1 });

        if (groups[(size_t) gi].expanded)
            for (int ii = 0; ii < (int) groups[(size_t) gi].items.size(); ++ii)
                rows.push_back ({ gi, ii });
    }

    list.updateContent();
    list.repaint();
    updateTotals();
}

/** Règle n°2 : les résidus ne restent actifs que si TOUS les bundles du plugin sont cochés. */
void DeepCleanDialog::enforceRules (CleanGroup& group)
{
    bool allBundles = true;

    for (const auto& item : group.items)
        if (item.isBundle && ! item.checked)
            allBundles = false;

    for (auto& item : group.items)
    {
        if (item.isBundle)
            continue;

        if (! group.leftoversEligible || ! allBundles)
        {
            item.enabled = false;
            item.checked = false;
        }
        else if (! item.enabled)
        {
            item.enabled = true;
            item.checked = item.defaultChecked;
        }
    }
}

void DeepCleanDialog::toggleGroup (int gi)
{
    auto& group = groups[(size_t) gi];
    const bool select = groupState (group) != Tri::All;

    for (auto& item : group.items)
        if (item.isBundle)
            item.checked = select;

    enforceRules (group);

    for (auto& item : group.items)
        if (! item.isBundle && item.enabled)
            item.checked = select;
}

void DeepCleanDialog::updateTotals()
{
    totalBytes = leftoverBytes = 0;
    checkedCount = leftoverCount = 0;
    anyAdmin = false;

    for (const auto& group : groups)
    {
        for (const auto& item : group.items)
        {
            if (! item.enabled || ! item.checked)
                continue;

            ++checkedCount;
            totalBytes += item.sizeBytes;
            anyAdmin = anyAdmin || item.requiresAdmin;

            if (! item.isBundle)
            {
                ++leftoverCount;
                leftoverBytes += item.sizeBytes;
            }
        }
    }

    confirmButton.setEnabled (checkedCount > 0);
    confirmButton.setButtonText (checkedCount > 0 ? fr ("Mettre à la Corbeille (") + juce::String (checkedCount) + ")"
                                                  : fr ("Mettre à la Corbeille"));
    repaint();
}

//==============================================================================
juce::Rectangle<int> DeepCleanDialog::cardBounds() const
{
    return getLocalBounds().withSizeKeepingCentre (juce::jmin (980, getWidth() - 80), juce::jmin (700, getHeight() - 60));
}

void DeepCleanDialog::resized()
{
    auto card = cardBounds();
    auto header = card.removeFromTop (kHeaderH);
    auto footer = card.removeFromBottom (kFooterH);

    expandButton.setBounds (header.reduced (28, 0).removeFromRight (130).withSizeKeepingCentre (130, 30).withY (header.getY() + 24));
    list.setBounds (card.reduced (1, 0));

    auto f = footer.reduced (28, 24);
    confirmButton.setBounds (f.removeFromRight (260));
    f.removeFromRight (10);
    cancelButton.setBounds (f.removeFromRight (110));
}

void DeepCleanDialog::paint (juce::Graphics& g)
{
    g.fillAll (Theme::background.withAlpha (0.82f));

    const auto card = cardBounds();
    const auto cardF = card.toFloat();

    g.setColour (juce::Colours::black.withAlpha (0.45f));
    g.fillRoundedRectangle (cardF.translated (0.0f, 8.0f).expanded (6.0f), 20.0f);
    g.setColour (Theme::background);
    g.fillRoundedRectangle (cardF, 16.0f);
    g.setColour (Theme::border);
    g.drawRoundedRectangle (cardF.reduced (0.5f), 16.0f, 1.0f);

    // ---- En-tête
    auto header = card.withHeight (kHeaderH).reduced (28, 20);
    auto titleLine = header.removeFromTop (30);

    g.setColour (Theme::text);
    g.setFont (Theme::font (22.0f, true));
    g.drawText (fr ("Désinstallation complète"), titleLine, juce::Justification::centredLeft, false);

    int bundleCount = 0, leftoverTotal = 0;

    for (const auto& group : groups)
        for (const auto& item : group.items)
            (item.isBundle ? bundleCount : leftoverTotal) += 1;

    g.setColour (Theme::textDim);
    g.setFont (Theme::font (14.5f));
    g.drawText (juce::String ((int) groups.size()) + fr (" plugin(s)  ·  ") + juce::String (bundleCount) + fr (" bundle(s)  ·  ")
                    + juce::String (leftoverTotal) + fr (" fichier(s) résiduel(s) détecté(s)"),
                header.removeFromTop (22), juce::Justification::centredLeft, true);

    if (warning.isNotEmpty())
    {
        g.setColour (Theme::warning);
        g.setFont (Theme::font (13.0f));
        g.drawText (warning, header.removeFromTop (20), juce::Justification::centredLeft, true);
    }
    else if (! searched)
    {
        g.setColour (Theme::textFaint);
        g.setFont (Theme::font (13.0f));
        g.drawText (fr ("Résidus recherchés uniquement pour les plugins dont tous les formats sont cochés."),
                    header.removeFromTop (20), juce::Justification::centredLeft, true);
    }

    g.setColour (Theme::border);
    g.fillRect (card.getX(), card.getY() + kHeaderH - 1, card.getWidth(), 1);
    g.fillRect (card.getX(), card.getBottom() - kFooterH, card.getWidth(), 1);

    // ---- Pied : total
    auto footer = card.withTrimmedTop (card.getHeight() - kFooterH).reduced (28, 18);
    footer.removeFromRight (390);

    const auto totalText = formatBytes (totalBytes);
    auto line1 = footer.removeFromTop (32);

    g.setGradientFill (juce::ColourGradient (Theme::cyan, (float) line1.getX(), 0.0f,
                                             Theme::violet, (float) line1.getX() + 160.0f, 0.0f, false));
    g.setFont (Theme::font (26.0f, true));
    g.drawText (totalText, line1.removeFromLeft (170), juce::Justification::centredLeft, false);

    g.setColour (Theme::textDim);
    g.setFont (Theme::font (14.0f));
    g.drawText (fr ("à libérer après avoir vidé la Corbeille"), line1, juce::Justification::centredLeft, true);

    juce::String details = fr ("dont ") + formatBytes (leftoverBytes) + fr (" de fichiers résiduels (")
                         + juce::String (leftoverCount) + ")";

    if (anyAdmin)
        details << fr ("  ·  mot de passe administrateur demandé une seule fois");

    g.setColour (Theme::textFaint);
    g.setFont (Theme::font (13.0f));
    g.drawText (details, footer.removeFromTop (22), juce::Justification::centredLeft, true);
}

bool DeepCleanDialog::keyPressed (const juce::KeyPress& key)
{
    if (key == juce::KeyPress::escapeKey)
    {
        if (onCancel)
            onCancel();
        return true;
    }

    if (key == juce::KeyPress::returnKey && confirmButton.isEnabled())
    {
        confirmButton.triggerClick();
        return true;
    }

    return true;  // modal : rien ne passe à la fenêtre principale
}

//==============================================================================
int DeepCleanDialog::getNumRows()
{
    return (int) rows.size();
}

void DeepCleanDialog::paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool)
{
    if (row < 0 || row >= (int) rows.size())
        return;

    const auto ref   = rows[(size_t) row];
    const auto& group = groups[(size_t) ref.group];
    auto area = juce::Rectangle<int> (width, height);

    if (ref.item < 0)
    {
        // ---------------------------------------------- en-tête de plugin
        g.setColour (Theme::panelAlt);
        g.fillRect (area);
        g.setColour (Theme::border.withAlpha (0.6f));
        g.fillRect (0, height - 1, width, 1);

        drawChevron (g, juce::Rectangle<float> (12.0f, 0.0f, 18.0f, (float) height), group.expanded);
        drawCheck (g, juce::Rectangle<float> (36.0f, (float) (height - 18) * 0.5f, 18.0f, 18.0f), groupState (group) == Tri::All ? Tri::All
                   : (groupState (group) == Tri::Partial ? Tri::Partial : Tri::None), true);

        auto text = area.withTrimmedLeft (66).reduced (0, 4);

        int bundles = 0, leftovers = 0;
        juce::int64 bytes = 0;

        for (const auto& item : group.items)
        {
            (item.isBundle ? bundles : leftovers) += 1;
            bytes += item.sizeBytes;
        }

        auto right = text.removeFromRight (300).withTrimmedRight (18);

        if (group.note.isNotEmpty())
        {
            g.setColour (Theme::warning);
            g.setFont (Theme::font (12.5f));
            g.drawText (fr ("Résidus ignorés : formats partiels"), right, juce::Justification::centredRight, true);
        }
        else
        {
            g.setColour (Theme::textDim);
            g.setFont (Theme::font (12.5f));
            g.drawText (juce::String (bundles) + fr (" bundle(s)  ·  ") + juce::String (leftovers) + fr (" résidu(s)  ·  ")
                            + formatBytes (bytes),
                        right, juce::Justification::centredRight, true);
        }

        g.setColour (Theme::text);
        g.setFont (Theme::font (15.5f, true));
        g.drawText (group.title, text.removeFromLeft (juce::jmin (text.getWidth(), 320)), juce::Justification::centredLeft, true);

        g.setColour (Theme::textDim);
        g.setFont (Theme::font (13.5f));
        g.drawText (group.vendor, text.withTrimmedLeft (10), juce::Justification::centredLeft, true);
        return;
    }

    // -------------------------------------------------- ligne bundle / résidu
    const auto& item  = group.items[(size_t) ref.item];
    const float alpha = item.enabled ? 1.0f : 0.38f;

    g.setColour (Theme::panel);
    g.fillRect (area);
    g.setColour (Theme::border.withAlpha (0.35f));
    g.fillRect (66, height - 1, width - 66, 1);

    // Trait d'arborescence
    g.setColour (Theme::border);
    g.fillRect (44, 0, 1, ref.item == (int) group.items.size() - 1 ? height / 2 : height);
    g.fillRect (44, height / 2, 14, 1);

    drawCheck (g, juce::Rectangle<float> (66.0f, (float) (height - 16) * 0.5f, 16.0f, 16.0f),
               item.checked ? Tri::All : Tri::None, item.enabled);

    auto r = area.withTrimmedLeft (94).withTrimmedRight (18);

    // Badge catégorie / format
    const auto badge = r.removeFromLeft (kBadgeWidth).withSizeKeepingCentre (kBadgeWidth - 8, 20).toFloat();
    g.setColour (item.badgeColour.withAlpha (0.12f * alpha));
    g.fillRoundedRectangle (badge, 5.0f);
    g.setColour (item.badgeColour.withAlpha (0.6f * alpha));
    g.drawRoundedRectangle (badge.reduced (0.5f), 5.0f, 1.0f);
    g.setColour (item.badgeColour.withMultipliedAlpha (alpha));
    g.setFont (Theme::font (11.0f, true));
    g.drawText (item.badge, badge, juce::Justification::centred, false);

    // Taille
    g.setColour (Theme::text.withMultipliedAlpha (alpha * 0.9f));
    g.setFont (Theme::font (13.5f));
    g.drawText (formatBytes (item.sizeBytes), r.removeFromRight (kSizeWidth), juce::Justification::centredRight, false);

    if (item.requiresAdmin)
    {
        g.setColour (Theme::textFaint.withMultipliedAlpha (alpha));
        g.setFont (Theme::font (11.0f, true));
        g.drawText ("ADMIN", r.removeFromRight (52), juce::Justification::centredRight, false);
    }

    r.removeFromLeft (6);

    // Nom + chemin parent
    const auto name = item.file.getFileName();
    g.setColour (Theme::text.withMultipliedAlpha (alpha));
    g.setFont (Theme::font (14.0f, true));
    const auto nameArea = r.removeFromLeft (juce::jmin (240, r.getWidth() / 2));
    g.drawText (name, nameArea, juce::Justification::centredLeft, true);

    g.setColour (Theme::textDim.withMultipliedAlpha (alpha));
    g.setFont (Theme::font (12.5f));
    g.drawText (displayPath (item.file.getParentDirectory()), r.withTrimmedLeft (8), juce::Justification::centredLeft, true);
}

void DeepCleanDialog::listBoxItemClicked (int row, const juce::MouseEvent& e)
{
    if (row < 0 || row >= (int) rows.size())
        return;

    const auto ref = rows[(size_t) row];
    auto& group = groups[(size_t) ref.group];

    if (ref.item < 0)
    {
        if (e.x >= 30 && e.x < 62)
            toggleGroup (ref.group);
        else
            group.expanded = ! group.expanded;

        rebuildRows();
        return;
    }

    auto& item = group.items[(size_t) ref.item];

    if (! item.enabled)
        return;

    item.checked = ! item.checked;
    enforceRules (group);
    list.repaint();
    updateTotals();
}

juce::String DeepCleanDialog::getTooltipForRow (int row)
{
    if (row < 0 || row >= (int) rows.size())
        return {};

    const auto ref = rows[(size_t) row];
    const auto& group = groups[(size_t) ref.group];

    if (ref.item < 0)
        return group.note;

    const auto& item = group.items[(size_t) ref.item];
    auto tip = item.file.getFullPathName();

    if (item.detail.isNotEmpty())
        tip << "\n" << fr ("Origine : ") << item.detail;

    if (! item.enabled && ! item.isBundle)
        tip << "\n" << fr ("Verrouillé : cochez tous les formats du plugin pour supprimer ses fichiers résiduels.");
    else if (! item.defaultChecked)
        tip << "\n" << fr ("Presets utilisateur : décochés par défaut.");

    return tip;
}

} // namespace aura
