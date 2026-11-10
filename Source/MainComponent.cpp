// ==============================================================================
//  MainComponent.cpp — AuraClean (NextDSP)
// ==============================================================================

#include "MainComponent.h"
#include "AuraLookAndFeel.h"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <map>
#include <set>
#include <unordered_set>

namespace aura
{
//==============================================================================
//  Utilitaires
//==============================================================================
juce::String formatBytes (juce::int64 bytes)
{
    // Base 1024, virgule décimale française, nombre de décimales FIXE.
    // (Ne jamais passer 0 à juce::String (double, int) : 0 = « précision complète », d'où un ancien « 508,005 Mo ».)
    constexpr double kilo = 1024.0;

    if (bytes < 0)
        bytes = 0;

    if (bytes < 1024)
        return juce::String (bytes) + " o";

    struct Unit { const char* name; double divisor; int decimals; };
    static constexpr Unit units[] = { { "Ko", kilo, 1 },
                                      { "Mo", kilo * kilo, 1 },
                                      { "Go", kilo * kilo * kilo, 2 },
                                      { "To", kilo * kilo * kilo * kilo, 2 } };

    size_t unit = 0;

    while (unit + 1 < std::size (units) && (double) bytes >= units[unit + 1].divisor)
        ++unit;

    // Arrondi à l'affichage : « 1023,96 Ko » doit devenir « 1,0 Mo », pas « 1024,0 Ko »
    const double scale = std::pow (10.0, units[unit].decimals);
    double value = std::round ((double) bytes / units[unit].divisor * scale) / scale;

    if (value >= kilo && unit + 1 < std::size (units))
    {
        ++unit;
        const double nextScale = std::pow (10.0, units[unit].decimals);
        value = std::round ((double) bytes / units[unit].divisor * nextScale) / nextScale;
    }

    return juce::String (value, units[unit].decimals).replaceCharacter ('.', ',') + " " + units[unit].name;
}

namespace
{
    juce::String countLabel (int n, const char* singular, const char* plural)
    {
        return juce::String (n) + " " + fr (n > 1 ? plural : singular);
    }

    juce::String scopeLabel (InstallScope s)
    {
        return s == InstallScope::System ? fr ("système") : fr ("utilisateur");
    }

    juce::Rectangle<int> formatBadgeBounds (int formatIndex, int rowHeight)
    {
        constexpr int width = 46, height = 20, gap = 6, x0 = 10;
        return { x0 + formatIndex * (width + gap), (rowHeight - height) / 2, width, height };
    }

    void drawBadge (juce::Graphics& g, juce::Rectangle<float> r, const juce::String& text, juce::Colour colour,
                    bool filled, float alpha = 1.0f, bool strikeThrough = false)
    {
        if (filled)
        {
            g.setColour (colour.withMultipliedAlpha (alpha));
            g.fillRoundedRectangle (r, 5.0f);
            g.setColour (Theme::background.withMultipliedAlpha (alpha));
        }
        else
        {
            g.setColour (colour.withAlpha (0.10f * alpha));
            g.fillRoundedRectangle (r, 5.0f);
            g.setColour (colour.withAlpha (0.55f * alpha));
            g.drawRoundedRectangle (r.reduced (0.5f), 5.0f, 1.0f);
            g.setColour (colour.withMultipliedAlpha (alpha));
        }

        g.setFont (Theme::font (12.0f, true));
        g.drawText (text, r, juce::Justification::centred, false);

        if (strikeThrough)
        {
            g.setColour (Theme::warning.withMultipliedAlpha (alpha));
            g.drawLine (r.getX() + 5.0f, r.getCentreY(), r.getRight() - 5.0f, r.getCentreY(), 1.5f);
        }
    }

    float easeOutCubic (float t)
    {
        t = juce::jlimit (0.0f, 1.0f, t);
        return 1.0f - std::pow (1.0f - t, 3.0f);
    }

    /** Calque translucide + léger glissement vertical pendant l'apparition d'une ligne. */
    struct ScopedRevealLayer
    {
        ScopedRevealLayer (juce::Graphics& graphics, float alpha) : g (graphics), active (alpha < 1.0f)
        {
            if (! active)
                return;

            g.beginTransparencyLayer (juce::jmax (0.0f, alpha));
            g.saveState();
            g.addTransform (juce::AffineTransform::translation (0.0f, (1.0f - alpha) * 6.0f));
        }

        ~ScopedRevealLayer()
        {
            if (! active)
                return;

            g.restoreState();
            g.endTransparencyLayer();
        }

        juce::Graphics& g;
        const bool      active;
    };

    void drawCheckbox (juce::Graphics& g, juce::Rectangle<float> r, CheckState state)
    {
        if (state == CheckState::None)
        {
            g.setColour (Theme::textFaint);
            g.drawRoundedRectangle (r.reduced (0.5f), 4.0f, 1.3f);
            return;
        }

        Theme::fillRoundedRectWithGradient (g, r, 4.0f, juce::ColourGradient (Theme::violet, r.getX(), r.getY(),
                                                                              Theme::cyan, r.getRight(), r.getBottom(), false));
        g.setColour (juce::Colours::white);

        if (state == CheckState::All)
        {
            juce::Path tick;
            tick.startNewSubPath (r.getX() + 3.8f, r.getCentreY() + 0.2f);
            tick.lineTo (r.getX() + 6.8f, r.getBottom() - 4.2f);
            tick.lineTo (r.getRight() - 3.6f, r.getY() + 4.4f);
            g.strokePath (tick, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
        else
        {
            g.fillRoundedRectangle (r.reduced (4.0f, 7.0f), 1.0f);
        }
    }
} // namespace

//==============================================================================
//  Library
//==============================================================================
void Library::setScanResult (ScanResult newResult)
{
    // Conserve la sélection par chemin à travers les rescans
    std::unordered_set<std::string> previouslySelected;

    for (size_t i = 0; i < result.bundles.size() && i < selection.size(); ++i)
        if (selection[i] != 0)
            previouslySelected.insert (result.bundles[i].file.getFullPathName().toStdString());

    result = std::move (newResult);
    selection.assign (result.bundles.size(), 0);
    leftoverBytes.clear();

    for (size_t i = 0; i < result.bundles.size(); ++i)
        if (previouslySelected.count (result.bundles[i].file.getFullPathName().toStdString()) != 0)
            selection[i] = 1;

    if (filters.vendorKey.isNotEmpty()
        && std::none_of (result.vendors.begin(), result.vendors.end(),
                         [this] (const VendorSummary& v) { return v.key == filters.vendorKey; }))
        filters.vendorKey = {};

    rebuild();
}

void Library::setLeftoverEstimates (std::vector<juce::int64> bytesPerEntry)
{
    leftoverBytes = std::move (bytesPerEntry);
}

juce::int64 Library::leftoverEstimate (int entryIndex) const noexcept
{
    return (entryIndex >= 0 && (size_t) entryIndex < leftoverBytes.size()) ? leftoverBytes[(size_t) entryIndex] : -1;
}

bool Library::bundlePasses (int bundleIndex) const noexcept
{
    const auto& b = result.bundles[(size_t) bundleIndex];

    if (! filters.formats[(size_t) b.format])
        return false;

    if ((filters.status == StatusFilter::Active && b.disabled) || (filters.status == StatusFilter::Disabled && ! b.disabled))
        return false;

    const auto arch = classifyArch (b.archFlags);

    switch (filters.arch)
    {
        case ArchFilter::AppleSilicon: return arch == ArchClass::AppleSilicon || arch == ArchClass::Universal;
        case ArchFilter::IntelOnly:    return arch == ArchClass::IntelOnly;
        case ArchFilter::Universal:    return arch == ArchClass::Universal;
        case ArchFilter::All:          break;
    }

    return true;
}

bool Library::computeEntryVisible (int entryIndex) const
{
    const auto& e = result.entries[(size_t) entryIndex];

    if (filters.vendorKey.isNotEmpty() && e.vendorKey != filters.vendorKey)
        return false;

    if (! searchTokens.isEmpty())
    {
        auto haystack = e.name + " " + e.vendor + " " + e.version;

        for (auto f : kAllFormats)
            if (e.has (f))
                haystack << " " << formatLabel (f);

        haystack = haystack.toLowerCase();

        for (const auto& token : searchTokens)
            if (! haystack.contains (token))
                return false;
    }

    for (const auto& list : e.bundles)
        for (int b : list)
            if (bundlePasses (b))
                return true;

    return false;
}

bool Library::isEntryVisible (int entryIndex) const noexcept
{
    return entryIndex >= 0 && (size_t) entryIndex < entryVisible.size() && entryVisible[(size_t) entryIndex] != 0;
}

void Library::rebuild()
{
    searchTokens = juce::StringArray::fromTokens (filters.search.toLowerCase(), " ", "\"");
    searchTokens.removeEmptyStrings();

    entryVisible.assign (result.entries.size(), 0);
    visibleRows.clear();

    for (size_t e = 0; e < result.entries.size(); ++e)
    {
        if (computeEntryVisible ((int) e))
        {
            entryVisible[e] = 1;
            visibleRows.push_back ((int) e);
        }
    }

    sortRows();
}

void Library::setSort (int columnId, bool forwards)
{
    sortColumn  = columnId;
    sortForward = forwards;
    sortRows();
}

void Library::sortRows()
{
    const auto& entries = result.entries;

    auto compare3 = [] (auto a, auto b) { return a < b ? -1 : (b < a ? 1 : 0); };

    std::stable_sort (visibleRows.begin(), visibleRows.end(), [&] (int ia, int ib)
    {
        const auto& a = entries[(size_t) ia];
        const auto& b = entries[(size_t) ib];
        int c = 0;

        switch (sortColumn)
        {
            case colCheck:   c = compare3 ((int) entryState (ia), (int) entryState (ib)); break;
            case colName:    c = a.name.compareNatural (b.name); break;
            case colVendor:
                c = a.unknownVendor != b.unknownVendor ? (a.unknownVendor ? 1 : -1) : a.vendor.compareNatural (b.vendor);
                break;
            case colFormats: c = compare3 (a.formatCount(), b.formatCount()); break;
            case colArch:    c = compare3 ((int) a.archClass, (int) b.archClass); break;
            case colVersion: c = a.version.compareNatural (b.version); break;
            case colSize:    c = compare3 (a.totalSize, b.totalSize); break;
            case colDate:    c = compare3 (a.installDate.toMilliseconds(), b.installDate.toMilliseconds()); break;
            default: break;
        }

        if (c == 0)
            c = a.name.compareNatural (b.name);

        return sortForward ? c < 0 : c > 0;
    });
}

bool Library::isSelected (int bundleIndex) const noexcept
{
    return bundleIndex >= 0 && (size_t) bundleIndex < selection.size() && selection[(size_t) bundleIndex] != 0;
}

void Library::setSelected (int bundleIndex, bool shouldBeSelected) noexcept
{
    if (bundleIndex >= 0 && (size_t) bundleIndex < selection.size())
        selection[(size_t) bundleIndex] = shouldBeSelected ? 1 : 0;
}

CheckState Library::entryState (int entryIndex) const noexcept
{
    int visible = 0, selectedVisible = 0, selectedHidden = 0;

    for (const auto& list : result.entries[(size_t) entryIndex].bundles)
    {
        for (int b : list)
        {
            const bool sel = isSelected (b);

            if (bundlePasses (b))
            {
                ++visible;
                selectedVisible += sel ? 1 : 0;
            }
            else
            {
                selectedHidden += sel ? 1 : 0;
            }
        }
    }

    if (selectedVisible == 0)
        return selectedHidden > 0 ? CheckState::Partial : CheckState::None;

    return (selectedVisible == visible && selectedHidden == 0) ? CheckState::All : CheckState::Partial;
}

void Library::toggleEntry (int entryIndex)
{
    const bool select = entryState (entryIndex) != CheckState::All;

    for (const auto& list : result.entries[(size_t) entryIndex].bundles)
        for (int b : list)
            setSelected (b, select && bundlePasses (b));
}

CheckState Library::formatState (int entryIndex, PluginFormat format) const noexcept
{
    int visible = 0, selected = 0;

    for (int b : result.entries[(size_t) entryIndex].bundles[(size_t) format])
    {
        if (bundlePasses (b))
        {
            ++visible;
            selected += isSelected (b) ? 1 : 0;
        }
    }

    if (selected == 0)
        return CheckState::None;

    return selected == visible ? CheckState::All : CheckState::Partial;
}

void Library::toggleFormat (int entryIndex, PluginFormat format)
{
    const bool select = formatState (entryIndex, format) != CheckState::All;

    for (int b : result.entries[(size_t) entryIndex].bundles[(size_t) format])
        if (bundlePasses (b))
            setSelected (b, select);
}

void Library::selectFormatInVisible (PluginFormat format)
{
    for (int e : visibleRows)
        for (int b : result.entries[(size_t) e].bundles[(size_t) format])
            if (bundlePasses (b))
                setSelected (b, true);
}

void Library::selectVendor (const juce::String& vendorKey, bool shouldSelect)
{
    for (const auto& e : result.entries)
        if (e.vendorKey == vendorKey)
            for (const auto& list : e.bundles)
                for (int b : list)
                    if (! shouldSelect || bundlePasses (b))
                        setSelected (b, shouldSelect);
}

void Library::selectAllVisible (bool shouldSelect)
{
    for (int e : visibleRows)
        for (const auto& list : result.entries[(size_t) e].bundles)
            for (int b : list)
                if (bundlePasses (b))
                    setSelected (b, shouldSelect);
}

void Library::clearSelection()
{
    std::fill (selection.begin(), selection.end(), 0);
}

SelectionSummary Library::summary() const
{
    SelectionSummary s;
    std::set<int> entries;

    for (size_t i = 0; i < selection.size(); ++i)
    {
        if (selection[i] == 0)
            continue;

        const auto& b = result.bundles[i];
        ++s.bundles;
        entries.insert (b.entryIndex);

        if (! b.isSymlink)
            s.bytes += b.sizeBytes;

        if (b.disabled) ++s.disabledBundles;
        else            ++s.enabledBundles;

        if (! bundlePasses ((int) i) || ! isEntryVisible (b.entryIndex))
            ++s.hidden;
    }

    s.plugins = (int) entries.size();
    return s;
}

std::vector<int> Library::selectedBundles() const
{
    std::vector<int> out;

    for (size_t i = 0; i < selection.size(); ++i)
        if (selection[i] != 0)
            out.push_back ((int) i);

    return out;
}

//==============================================================================
//  VendorSidebar
//==============================================================================
VendorSidebar::VendorSidebar (Library& lib) : library (lib)
{
    list.setModel (this);
    list.onShortcut = [this] (const juce::KeyPress& key) { return onShortcut != nullptr && onShortcut (key); };
    list.setRowHeight (50);
    list.setOutlineThickness (0);
    list.setColour (juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
    addAndMakeVisible (list);
}

void VendorSidebar::refresh()
{
    list.updateContent();

    const auto& vendors = library.data().vendors;
    int row = 0;

    for (size_t i = 0; i < vendors.size(); ++i)
        if (vendors[i].key == library.filters.vendorKey)
            row = (int) i + 1;

    const juce::ScopedValueSetter<bool> svs (ignoreSelectionCallback, true);
    list.selectRow (row, false, true);
    list.repaint();
}

void VendorSidebar::setStats (const juce::String& text)
{
    stats = text;
    repaint();
}

void VendorSidebar::paint (juce::Graphics& g)
{
    g.fillAll (Theme::panel);
    g.setColour (Theme::border);
    g.fillRect (getWidth() - 1, 0, 1, getHeight());

    g.setColour (Theme::textDim);
    g.setFont (Theme::font (12.0f, true));
    g.drawText ("MARQUES", getLocalBounds().removeFromTop (40).reduced (20, 0), juce::Justification::centredLeft);

    g.setColour (Theme::border);
    g.fillRect (12, getHeight() - 36, getWidth() - 24, 1);
    g.setColour (Theme::textFaint);
    g.setFont (Theme::font (12.5f));
    g.drawText (stats, getLocalBounds().removeFromBottom (36).reduced (20, 0), juce::Justification::centredLeft, true);
}

void VendorSidebar::resized()
{
    auto area = getLocalBounds();
    area.removeFromTop (40);
    area.removeFromBottom (36);
    list.setBounds (area.withTrimmedRight (1));
}

int VendorSidebar::getNumRows()
{
    return library.data().entries.empty() ? 0 : (int) library.data().vendors.size() + 1;
}

juce::String VendorSidebar::keyForRow (int row) const
{
    const auto& vendors = library.data().vendors;
    return (row >= 1 && (size_t) row <= vendors.size()) ? vendors[(size_t) row - 1].key : juce::String();
}

void VendorSidebar::paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool selected)
{
    const auto& data = library.data();
    juce::String name;
    int          count = 0;
    juce::int64  bytes = 0;
    bool         unknown = false;

    if (row == 0)
    {
        name  = fr ("Toutes les marques");
        count = (int) data.entries.size();

        for (const auto& v : data.vendors)
            bytes += v.totalBytes;
    }
    else if ((size_t) row <= data.vendors.size())
    {
        const auto& v = data.vendors[(size_t) row - 1];
        name    = v.name;
        count   = v.pluginCount;
        bytes   = v.totalBytes;
        unknown = v.key == PluginScanner::unknownVendorKey();
    }
    else
    {
        return;
    }

    auto r = juce::Rectangle<int> (width, height).reduced (10, 3);

    if (selected)
    {
        g.setColour (Theme::cyan.withAlpha (0.10f));
        g.fillRoundedRectangle (r.toFloat(), 8.0f);
        Theme::fillRoundedRectWithGradient (g, r.removeFromLeft (3).toFloat().reduced (0.0f, 8.0f), 1.5f,
                                            juce::ColourGradient (Theme::cyan, 0.0f, (float) r.getY(),
                                                                  Theme::violet, 0.0f, (float) r.getBottom(), false));
    }

    auto inner = r.reduced (12, 7);

    // Pastille compteur
    const auto countText = juce::String (count);
    const int  pillWidth = 14 + 8 * countText.length();
    const auto pill      = inner.removeFromRight (pillWidth).withSizeKeepingCentre (pillWidth, 20).toFloat();
    g.setColour (selected ? Theme::cyan.withAlpha (0.2f) : Theme::raised);
    g.fillRoundedRectangle (pill, 10.0f);
    g.setColour (selected ? Theme::cyan : Theme::textDim);
    g.setFont (Theme::font (12.5f, true));
    g.drawText (countText, pill, juce::Justification::centred, false);

    inner.removeFromRight (8);
    g.setColour (unknown ? Theme::textDim : Theme::text);
    g.setFont (Theme::font (15.0f, true));
    g.drawText (name, inner.removeFromTop (inner.getHeight() / 2 + 1), juce::Justification::bottomLeft, true);

    g.setColour (Theme::textFaint);
    g.setFont (Theme::font (12.5f));
    g.drawText (formatBytes (bytes), inner, juce::Justification::topLeft, true);
}

void VendorSidebar::selectedRowsChanged (int lastRowSelected)
{
    if (ignoreSelectionCallback)
        return;

    if (lastRowSelected < 0)
    {
        const juce::ScopedValueSetter<bool> svs (ignoreSelectionCallback, true);
        list.selectRow (0);
        lastRowSelected = 0;
    }

    library.filters.vendorKey = keyForRow (lastRowSelected);

    if (onFilterChanged)
        onFilterChanged();
}

void VendorSidebar::listBoxItemClicked (int row, const juce::MouseEvent& e)
{
    const auto key = keyForRow (row);

    if (! e.mods.isPopupMenu() || key.isEmpty())
        return;

    const auto name = library.data().vendors[(size_t) row - 1].name;

    juce::PopupMenu menu;
    menu.addSectionHeader (name);
    menu.addItem (1, fr ("Sélectionner tous les plugins de ") + name);
    menu.addItem (2, fr ("Désélectionner tous les plugins de ") + name);

    juce::Component::SafePointer<VendorSidebar> safe (this);

    menu.showMenuAsync (juce::PopupMenu::Options().withMousePosition(), [safe, key] (int result)
    {
        if (auto* self = safe.getComponent(); self != nullptr && result > 0)
        {
            self->library.selectVendor (key, result == 1);

            if (self->onSelectionChanged)
                self->onSelectionChanged();
        }
    });
}

//==============================================================================
//  PluginTable
//==============================================================================
PluginTable::PluginTable (Library& lib) : library (lib)
{
    table.setModel (this);
    table.onShortcut = [this] (const juce::KeyPress& key) { return onShortcut != nullptr && onShortcut (key); };
    table.setRowHeight (44);
    table.setHeaderHeight (34);
    table.setMultipleSelectionEnabled (true);
    table.setOutlineThickness (0);
    table.setColour (juce::ListBox::backgroundColourId, Theme::panel);

    auto& header = table.getHeader();
    using H = juce::TableHeaderComponent;

    header.addColumn ({},                    colCheck,   44,  44,  44,  H::visible | H::sortable);
    header.addColumn ("Plugin",              colName,    250, 140, 600, H::defaultFlags);
    header.addColumn ("Marque",              colVendor,  160, 100, 360, H::defaultFlags);
    header.addColumn ("Formats",             colFormats, 172, 172, 172, H::notResizable);
    header.addColumn ("Architecture",        colArch,    130, 120, 180, H::defaultFlags);
    header.addColumn ("Version",             colVersion, 90,  70,  160, H::defaultFlags);
    header.addColumn ("Taille",              colSize,    120, 100, 180, H::defaultFlags);
    header.addColumn (fr ("Installé le"),    colDate,    105, 90,  160, H::defaultFlags);

    header.setStretchToFitActive (true);
    header.setPopupMenuActive (false);
    header.setSortColumnId (colVendor, true);

    addAndMakeVisible (table);
}

void PluginTable::startReveal()
{
    revealStartMs = juce::Time::getMillisecondCounterHiRes();
    revealing     = true;
    startTimerHz (60);
    table.repaint();
}

float PluginTable::rowRevealAlpha (int row) const noexcept
{
    if (! revealing)
        return 1.0f;

    constexpr double staggerMs = 16.0, durationMs = 240.0;
    const auto elapsed = juce::Time::getMillisecondCounterHiRes() - revealStartMs;
    const auto delay   = staggerMs * juce::jmin (row, 24);
    return easeOutCubic ((float) ((elapsed - delay) / durationMs));
}

void PluginTable::timerCallback()
{
    if (juce::Time::getMillisecondCounterHiRes() - revealStartMs > 16.0 * 24 + 240.0 + 20.0)
    {
        revealing = false;
        stopTimer();
    }

    table.repaint();
}

void PluginTable::resized()
{
    table.setBounds (getLocalBounds());
}

void PluginTable::refresh()
{
    table.updateContent();
    table.repaint();
    repaint();
}

void PluginTable::paintOverChildren (juce::Graphics& g)
{
    if (! library.visibleEntries().empty())
        return;

    g.setColour (Theme::textFaint);
    g.setFont (Theme::font (15.0f));
    g.drawText (library.data().entries.empty() ? fr ("Aucun plugin trouvé.")
                                               : fr ("Aucun plugin ne correspond aux filtres."),
                getLocalBounds().withTrimmedTop (34), juce::Justification::centred);
}

int PluginTable::getNumRows()
{
    return (int) library.visibleEntries().size();
}

int PluginTable::entryForRow (int row) const noexcept
{
    const auto& rows = library.visibleEntries();
    return (row >= 0 && (size_t) row < rows.size()) ? rows[(size_t) row] : -1;
}

void PluginTable::notifyChanged()
{
    table.repaint();

    if (onSelectionChanged)
        onSelectionChanged();
}

void PluginTable::paintRowBackground (juce::Graphics& g, int row, int width, int height, bool rowIsSelected)
{
    const int entry = entryForRow (row);

    if (entry < 0)
        return;

    const ScopedRevealLayer layer (g, rowRevealAlpha (row));

    auto background = (row % 2 == 0) ? Theme::panel : Theme::panel.brighter (0.025f);

    if (rowIsSelected)
        background = background.interpolatedWith (Theme::cyan, 0.08f);

    g.fillAll (background);

    if (library.entryState (entry) != CheckState::None)
    {
        g.setColour (Theme::violet.withAlpha (0.07f));
        g.fillAll();
        g.setColour (Theme::violet);
        g.fillRect (0, 6, 2, height - 12);
    }

    g.setColour (Theme::border.withAlpha (0.5f));
    g.fillRect (0, height - 1, width, 1);
}

void PluginTable::paintCell (juce::Graphics& g, int row, int columnId, int width, int height, bool)
{
    const int entryIndex = entryForRow (row);

    if (entryIndex < 0)
        return;

    const ScopedRevealLayer layer (g, rowRevealAlpha (row));

    const auto& data  = library.data();
    const auto& entry = data.entries[(size_t) entryIndex];
    const float alpha = entry.allDisabled ? 0.55f : 1.0f;
    auto area = juce::Rectangle<int> (width, height).reduced (10, 0);

    switch (columnId)
    {
        case colCheck:
        {
            drawCheckbox (g, juce::Rectangle<float> ((float) (width - 18) * 0.5f, (float) (height - 18) * 0.5f, 18.0f, 18.0f),
                          library.entryState (entryIndex));
            break;
        }

        case colName:
        {
            if (entry.anyDisabled)
            {
                const auto pill = area.removeFromRight (entry.allDisabled ? 78 : 66).withSizeKeepingCentre (entry.allDisabled ? 74 : 62, 18);
                drawBadge (g, pill.toFloat(), entry.allDisabled ? fr ("Désactivé") : fr ("Partiel"), Theme::warning, false);
                area.removeFromRight (6);
            }

            g.setColour (Theme::text.withMultipliedAlpha (alpha));
            g.setFont (Theme::font (15.0f, true));
            g.drawText (entry.name, area, juce::Justification::centredLeft, true);
            break;
        }

        case colVendor:
        {
            g.setColour ((entry.unknownVendor ? Theme::textFaint : Theme::textDim).withMultipliedAlpha (alpha));
            g.setFont (Theme::font (14.0f));
            g.drawText (entry.vendor, area, juce::Justification::centredLeft, true);
            break;
        }

        case colFormats:
        {
            for (int f = 0; f < kNumFormats; ++f)
            {
                const auto format = kAllFormats[(size_t) f];
                const auto badge  = formatBadgeBounds (f, height).toFloat();
                const auto colour = Theme::formatColour (format);

                if (! entry.has (format))
                {
                    g.setColour (Theme::border.withAlpha (0.6f));
                    g.drawRoundedRectangle (badge.reduced (0.5f), 5.0f, 1.0f);
                    g.setColour (Theme::textFaint.withAlpha (0.35f));
                    g.setFont (Theme::font (12.0f, true));
                    g.drawText (formatLabel (format), badge, juce::Justification::centred, false);
                    continue;
                }

                bool anyPasses = false, allDisabled = true;

                for (int b : entry.bundles[(size_t) f])
                {
                    anyPasses   = anyPasses || library.bundlePasses (b);
                    allDisabled = allDisabled && data.bundles[(size_t) b].disabled;
                }

                const auto state = library.formatState (entryIndex, format);
                drawBadge (g, badge, formatLabel (format), colour, state == CheckState::All,
                           anyPasses ? 1.0f : 0.3f, allDisabled);

                if (state == CheckState::Partial)
                {
                    g.setColour (colour);
                    g.fillEllipse (badge.getRight() - 6.0f, badge.getY() - 2.0f, 7.0f, 7.0f);
                }

                if (entry.bundles[(size_t) f].size() > 1)  // plusieurs copies (système + utilisateur…)
                {
                    g.setColour (Theme::text);
                    g.setFont (Theme::font (10.0f, true));
                    g.drawText ("x" + juce::String ((int) entry.bundles[(size_t) f].size()),
                                badge.withTrimmedTop (badge.getHeight() - 9.0f).translated (0.0f, 9.0f),
                                juce::Justification::centredRight, false);
                }
            }
            break;
        }

        case colArch:
        {
            const auto badge = area.withSizeKeepingCentre (juce::jmin (area.getWidth(), 108), 20)
                                   .withX (area.getX()).toFloat();
            drawBadge (g, badge, archLabel (entry.archClass), Theme::archColour (entry.archClass),
                       false, alpha);
            break;
        }

        case colVersion:
        {
            g.setColour (Theme::textDim.withMultipliedAlpha (alpha));
            g.setFont (Theme::font (14.0f));
            g.drawText (entry.version.isNotEmpty() ? entry.version : fr ("—"), area,
                        juce::Justification::centredLeft, true);
            break;
        }

        case colSize:
        {
            const auto extra = library.leftoverEstimate (entryIndex);

            if (extra > 0)
            {
                // Ligne 1 : binaires · Ligne 2 : presets / données détectés
                auto top = area.removeFromTop (height / 2 + 2);
                g.setColour (Theme::text.withMultipliedAlpha (alpha * 0.9f));
                g.setFont (Theme::font (14.0f));
                g.drawText (formatBytes (entry.totalSize), top, juce::Justification::bottomRight, true);

                g.setColour (Theme::textFaint.withMultipliedAlpha (alpha));
                g.setFont (Theme::font (11.5f));
                g.drawText ("+ " + formatBytes (extra) + fr (" données"), area, juce::Justification::topRight, true);
            }
            else
            {
                g.setColour (Theme::text.withMultipliedAlpha (alpha * 0.9f));
                g.setFont (Theme::font (14.0f));
                g.drawText (formatBytes (entry.totalSize), area, juce::Justification::centredRight, true);
            }
            break;
        }

        case colDate:
        {
            g.setColour (Theme::textDim.withMultipliedAlpha (alpha));
            g.setFont (Theme::font (14.0f));
            g.drawText (entry.installDate.toMilliseconds() > 0 ? entry.installDate.formatted ("%d/%m/%Y") : fr ("—"),
                        area, juce::Justification::centredLeft, true);
            break;
        }

        default:
            break;
    }
}

void PluginTable::cellClicked (int row, int columnId, const juce::MouseEvent& e)
{
    const int entryIndex = entryForRow (row);

    if (entryIndex < 0)
        return;

    if (e.mods.isPopupMenu())
    {
        showContextMenu (entryIndex);
        return;
    }

    if (columnId == colCheck)
    {
        library.toggleEntry (entryIndex);
        notifyChanged();
        return;
    }

    if (columnId == colFormats)
    {
        // L'événement est relatif à la ligne : on ramène x dans le repère de la cellule
        const auto cell  = table.getCellPosition (colFormats, row, false);
        const auto local = juce::Point<int> (e.x - cell.getX(), e.y);
        const auto& entry = library.data().entries[(size_t) entryIndex];

        for (int f = 0; f < kNumFormats; ++f)
        {
            const auto format = kAllFormats[(size_t) f];

            if (entry.has (format) && formatBadgeBounds (f, table.getRowHeight()).expanded (2).contains (local))
            {
                library.toggleFormat (entryIndex, format);
                notifyChanged();
                return;
            }
        }
    }
}

void PluginTable::cellDoubleClicked (int row, int columnId, const juce::MouseEvent&)
{
    const int entryIndex = entryForRow (row);

    if (entryIndex < 0 || columnId == colCheck || columnId == colFormats)
        return;

    for (const auto& list : library.data().entries[(size_t) entryIndex].bundles)
    {
        if (! list.empty())
        {
            library.data().bundles[(size_t) list.front()].file.revealToUser();
            return;
        }
    }
}

void PluginTable::returnKeyPressed (int)
{
    const auto rows = table.getSelectedRows();

    for (int i = 0; i < rows.size(); ++i)
        if (const int entry = entryForRow (rows[i]); entry >= 0)
            library.toggleEntry (entry);

    notifyChanged();
}

void PluginTable::sortOrderChanged (int newSortColumnId, bool isForwards)
{
    library.setSort (newSortColumnId, isForwards);
    table.updateContent();
    table.repaint();
}

juce::String PluginTable::formatTooltip (int entryIndex, PluginFormat format) const
{
    const auto& data  = library.data();
    const auto& entry = data.entries[(size_t) entryIndex];
    juce::StringArray blocks;

    for (int bi : entry.bundles[(size_t) format])
    {
        const auto& b = data.bundles[(size_t) bi];
        juce::String block;
        block << formatLabel (format) << fr ("  —  ") << b.file.getFileName() << "\n"
              << b.file.getFullPathName() << "\n"
              << fr ("Taille : ") << formatBytes (b.sizeBytes)
              << fr ("  ·  Version : ") << (b.version.isNotEmpty() ? b.version : fr ("—"))
              << fr ("  ·  ") << archDetail (b.archFlags)
              << fr ("  ·  ") << scopeLabel (b.scope);

        if (b.disabled)  block << fr ("  ·  désactivé");
        if (b.isSymlink) block << fr ("  ·  lien symbolique");
        if (b.bundleId.isNotEmpty()) block << "\n" << b.bundleId;

        blocks.add (block);
    }

    return blocks.joinIntoString ("\n\n");
}

juce::String PluginTable::sizeTooltip (int entryIndex) const
{
    const auto& entry = library.data().entries[(size_t) entryIndex];
    const auto extra  = library.leftoverEstimate (entryIndex);

    juce::String tip;
    tip << fr ("Binaires (bundles) : ") << formatBytes (entry.totalSize);

    if (extra < 0)
    {
        tip << "\n" << fr ("Presets & données : estimation en cours…");
    }
    else
    {
        tip << "\n" << fr ("Presets & données détectés : ") << formatBytes (extra)
            << "\n" << fr ("Taille réelle totale : ") << formatBytes (entry.totalSize + extra)
            << "\n\n" << fr ("Estimation hors ~/Documents et reçus d'installation :")
            << "\n" << fr ("le détail complet est calculé à la désinstallation.");
    }

    return tip;
}

juce::String PluginTable::getCellTooltip (int row, int columnId)
{
    const int entryIndex = entryForRow (row);

    if (entryIndex < 0 || columnId == colCheck)
        return {};

    const auto& entry = library.data().entries[(size_t) entryIndex];

    if (columnId == colSize)
        return sizeTooltip (entryIndex);

    if (columnId == colFormats)
    {
        // Infobulle propre au badge survolé : position souris → badge
        const auto mouse = table.getMouseXYRelative();
        const auto cell  = table.getCellPosition (colFormats, row, true);
        const juce::Point<int> local (mouse.x - cell.getX(), mouse.y - cell.getY());

        for (int f = 0; f < kNumFormats; ++f)
            if (entry.has (kAllFormats[(size_t) f]) && formatBadgeBounds (f, table.getRowHeight()).expanded (3).contains (local))
                return formatTooltip (entryIndex, kAllFormats[(size_t) f]);

        return fr ("Survolez un badge pour voir le chemin et la taille du fichier.\nCliquez dessus pour le cocher.");
    }

    juce::StringArray blocks;

    for (auto f : kAllFormats)
        if (entry.has (f))
            blocks.add (formatTooltip (entryIndex, f));

    return blocks.joinIntoString ("\n\n");
}

void PluginTable::showContextMenu (int entryIndex)
{
    const auto& data    = library.data();
    const auto& entry   = data.entries[(size_t) entryIndex];
    const bool  checked = library.entryState (entryIndex) == CheckState::All;

    enum MenuId
    {
        idToggle      = 1,
        idCopyPaths   = 2,
        idCopyAllIds  = 3,
        idUninstall   = 4,
        idRevealBase  = 1000000,
        idCopyIdBase  = 2000000
    };

    juce::PopupMenu menu;
    menu.addSectionHeader (entry.vendor + fr (" — ") + entry.name);

    // ---- Finder : un élément par format installé (et par copie)
    for (auto f : kAllFormats)
    {
        const auto& list = entry.bundles[(size_t) f];

        for (int bi : list)
        {
            const auto& b = data.bundles[(size_t) bi];
            auto text = fr ("Afficher le ") + formatLabel (f) + fr (" dans le Finder");

            if (list.size() > 1 || b.disabled)
                text << " (" << scopeLabel (b.scope) << (b.disabled ? fr (", désactivé") : juce::String()) << ")";

            menu.addItem (idRevealBase + bi, text);
        }
    }

    menu.addSeparator();

    // ---- Bundle ID : direct s'il est unique, sinon sous-menu par format
    juce::StringArray uniqueIds;
    juce::PopupMenu idMenu;

    for (auto f : kAllFormats)
    {
        for (int bi : entry.bundles[(size_t) f])
        {
            const auto& id = data.bundles[(size_t) bi].bundleId;

            if (id.isEmpty())
                continue;

            idMenu.addItem (idCopyIdBase + bi, formatLabel (f) + fr ("  —  ") + id);
            uniqueIds.addIfNotAlreadyThere (id);
        }
    }

    if (uniqueIds.size() == 1)
        menu.addItem (idCopyAllIds, fr ("Copier le Bundle ID"));
    else if (uniqueIds.size() > 1)
    {
        idMenu.addSeparator();
        idMenu.addItem (idCopyAllIds, fr ("Copier tous les Bundle ID"));
        menu.addSubMenu (fr ("Copier le Bundle ID"), idMenu);
    }
    else
        menu.addItem (idCopyAllIds, fr ("Copier le Bundle ID"), false);

    menu.addItem (idCopyPaths, fr ("Copier les chemins"));
    menu.addSeparator();
    menu.addItem (idToggle, checked ? fr ("Décocher") : fr ("Cocher"));
    menu.addSeparator();
    menu.addItem (idUninstall, fr ("Désinstaller ce plugin…"));

    juce::Component::SafePointer<PluginTable> safe (this);

    menu.showMenuAsync (juce::PopupMenu::Options().withMousePosition(), [safe, entryIndex, uniqueIds] (int result)
    {
        auto* self = safe.getComponent();

        if (self == nullptr || result == 0)
            return;

        const auto& d = self->library.data();

        if ((size_t) entryIndex >= d.entries.size())
            return;

        if (result == idToggle)
        {
            self->library.toggleEntry (entryIndex);
            self->notifyChanged();
        }
        else if (result == idCopyPaths)
        {
            juce::StringArray paths;

            for (const auto& list : d.entries[(size_t) entryIndex].bundles)
                for (int bi : list)
                    paths.add (d.bundles[(size_t) bi].file.getFullPathName());

            juce::SystemClipboard::copyTextToClipboard (paths.joinIntoString ("\n"));
        }
        else if (result == idCopyAllIds)
        {
            juce::SystemClipboard::copyTextToClipboard (uniqueIds.joinIntoString ("\n"));
        }
        else if (result == idUninstall)
        {
            if (self->onUninstallEntry)
                self->onUninstallEntry (entryIndex);
        }
        else if (result >= idCopyIdBase && (size_t) (result - idCopyIdBase) < d.bundles.size())
        {
            juce::SystemClipboard::copyTextToClipboard (d.bundles[(size_t) (result - idCopyIdBase)].bundleId);
        }
        else if (result >= idRevealBase && (size_t) (result - idRevealBase) < d.bundles.size())
        {
            d.bundles[(size_t) (result - idRevealBase)].file.revealToUser();
        }
    });
}

//==============================================================================
//  BusyOverlay
//==============================================================================
BusyOverlay::BusyOverlay()
{
    setVisible (false);
    setInterceptsMouseClicks (true, true);
}

void BusyOverlay::show (const juce::String& text, const std::atomic<float>* progressSource)
{
    message  = text;
    progress = progressSource;
    phase    = 0.0f;
    setVisible (true);
    toFront (false);
    startTimerHz (30);
}

void BusyOverlay::hide()
{
    stopTimer();
    setVisible (false);
}

void BusyOverlay::timerCallback()
{
    phase = std::fmod (phase + 0.018f, 1.0f);
    repaint();
}

void BusyOverlay::paint (juce::Graphics& g)
{
    g.fillAll (Theme::background.withAlpha (0.72f));

    const auto card = getLocalBounds().withSizeKeepingCentre (420, 124).toFloat();
    g.setColour (Theme::panelAlt);
    g.fillRoundedRectangle (card, 14.0f);
    g.setColour (Theme::border);
    g.drawRoundedRectangle (card.reduced (0.5f), 14.0f, 1.0f);

    auto inner = card.reduced (28.0f, 24.0f);
    g.setColour (Theme::text);
    g.setFont (Theme::font (16.0f, true));
    g.drawText (message, inner.removeFromTop (26.0f), juce::Justification::centredLeft, true);

    inner.removeFromTop (14.0f);
    const auto track = inner.removeFromTop (6.0f);
    g.setColour (Theme::raised);
    g.fillRoundedRectangle (track, 3.0f);

    const float value = progress != nullptr ? progress->load() : -1.0f;
    juce::Rectangle<float> bar;

    if (value > 0.0f)
    {
        bar = track.withWidth (track.getWidth() * juce::jlimit (0.0f, 1.0f, value));
    }
    else  // indéterminé
    {
        const float w = track.getWidth() * 0.28f;
        bar = track.withWidth (w).withX (track.getX() - w + (track.getWidth() + w) * phase)
                   .getIntersection (track);
    }

    // Barre indéterminée : peut être vide en bout de course → helper sûr (sinon inondation sous CoreGraphics)
    Theme::fillRoundedRectWithGradient (g, bar, 3.0f, juce::ColourGradient (Theme::violet, track.getX(), 0.0f,
                                                                            Theme::cyan, track.getRight(), 0.0f, false));

    if (value > 0.0f)
    {
        g.setColour (Theme::textDim);
        g.setFont (Theme::font (12.5f));
        g.drawText (juce::String (juce::roundToInt (value * 100.0f)) + " %", inner.withTrimmedTop (6.0f),
                    juce::Justification::topRight, false);
    }
}

//==============================================================================
//  UpdateBanner
//==============================================================================
namespace
{
    constexpr double kBannerAnimMs = 220.0;
    const juce::Colour kBannerBackground { 0xff12151d };
    const juce::Colour kBannerBorder     { 0xff252836 };
}

UpdateBanner::UpdateBanner()
{
    setVisible (false);

    downloadButton.setButtonText (fr ("Télécharger la mise à jour"));
    downloadButton.getProperties().set ("auraStyle", "primary");
    downloadButton.onClick = [this]
    {
        if (onDownload)
            onDownload (info.downloadUrl);
        hide();
    };

    laterButton.setButtonText (fr ("Plus tard"));
    laterButton.getProperties().set ("auraStyle", "secondary");
    laterButton.onClick = [this] { hide(); };

    skipButton.setButtonText (fr ("Ignorer cette version"));
    skipButton.getProperties().set ("auraStyle", "ghost");
    skipButton.onClick = [this]
    {
        if (onSkipVersion)
            onSkipVersion (info.latestVersion);
        hide();
    };

    for (auto* b : { &downloadButton, &laterButton, &skipButton })
        addAndMakeVisible (b);
}

void UpdateBanner::show (const UpdateInfo& newInfo)
{
    info = newInfo;
    setTooltip (info.releaseNotes.isNotEmpty() ? fr ("Nouveautés :\n") + info.releaseNotes : juce::String());
    setVisible (true);
    startValue       = reveal;
    target           = 1.0f;
    animationStartMs = juce::Time::getMillisecondCounterHiRes();
    startTimerHz (60);
    repaint();
}

void UpdateBanner::hide()
{
    startValue       = reveal;
    target           = 0.0f;
    animationStartMs = juce::Time::getMillisecondCounterHiRes();
    startTimerHz (60);
}

int UpdateBanner::getCurrentHeight() const noexcept
{
    return juce::roundToInt ((float) fullHeight * reveal);
}

void UpdateBanner::timerCallback()
{
    const auto t = (float) ((juce::Time::getMillisecondCounterHiRes() - animationStartMs) / kBannerAnimMs);
    reveal = startValue + (target - startValue) * easeOutCubic (t);

    if (t >= 1.0f)
    {
        reveal = target;
        stopTimer();

        if (target <= 0.0f)
            setVisible (false);
    }

    if (onLayoutChanged)
        onLayoutChanged();
}

void UpdateBanner::resized()
{
    // Le contenu est calé sur le BAS : il glisse depuis le haut pendant l'animation
    auto r = juce::Rectangle<int> (0, getHeight() - fullHeight, getWidth(), fullHeight).reduced (20, 13);

    downloadButton.setBounds (r.removeFromRight (230));
    r.removeFromRight (8);
    laterButton.setBounds (r.removeFromRight (104));
    r.removeFromRight (8);
    skipButton.setBounds (r.removeFromRight (168));
}

void UpdateBanner::paint (juce::Graphics& g)
{
    // Couleurs unies uniquement
    g.fillAll (kBannerBackground);
    g.setColour (kBannerBorder);
    g.fillRect (0, getHeight() - 1, getWidth(), 1);

    const auto content = juce::Rectangle<int> (0, getHeight() - fullHeight, getWidth(), fullHeight);

    g.setColour (Theme::cyan);
    g.fillRect (content.getX(), content.getY() + 12, 3, fullHeight - 24);

    // Pastille avec flèche de téléchargement
    const auto badge = juce::Rectangle<float> (22.0f, (float) content.getY() + 15.0f, 28.0f, 28.0f);
    g.setColour (Theme::cyan.withAlpha (0.14f));
    g.fillEllipse (badge);

    juce::Path arrow;
    const auto c = badge.getCentre();
    arrow.startNewSubPath (c.x, c.y - 6.5f);
    arrow.lineTo (c.x, c.y + 4.5f);
    arrow.startNewSubPath (c.x - 4.5f, c.y + 0.5f);
    arrow.lineTo (c.x, c.y + 5.0f);
    arrow.lineTo (c.x + 4.5f, c.y + 0.5f);
    g.setColour (Theme::cyan);
    g.strokePath (arrow, juce::PathStrokeType (1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    auto text = content.withTrimmedLeft (64).withTrimmedRight (20 + 230 + 8 + 104 + 8 + 168 + 16).reduced (0, 10);

    g.setColour (Theme::text);
    g.setFont (Theme::font (15.0f, true));
    g.drawText (fr ("AuraClean ") + info.latestVersion + fr (" est disponible"),
                text.removeFromTop (text.getHeight() / 2 + 1), juce::Justification::bottomLeft, true);

    auto subtitle = fr ("Vous utilisez la version ") + info.currentVersion;

    if (info.releaseNotes.isNotEmpty())
        subtitle << fr ("  ·  ") << info.releaseNotes.upToFirstOccurrenceOf ("\n", false, false);

    g.setColour (Theme::textDim);
    g.setFont (Theme::font (13.0f));
    g.drawText (subtitle, text, juce::Justification::topLeft, true);
}

//==============================================================================
//  IntroOverlay
//==============================================================================
namespace
{
    constexpr double kIntroMinMs   = 600.0;  // durée minimale de la barre
    constexpr double kIntroFadeMs  = 220.0;  // fondu de sortie
    constexpr double kTitleInMs    = 260.0;  // apparition du titre
}

IntroOverlay::IntroOverlay()
{
    setVisible (false);
    setInterceptsMouseClicks (true, true);
}

void IntroOverlay::start (const std::atomic<float>* scanProgress)
{
    progress    = scanProgress;
    startMs     = juce::Time::getMillisecondCounterHiRes();
    lastTickMs  = startMs;
    shown       = 0.0f;
    opacity     = 1.0f;
    finished    = false;
    fading      = false;

    setVisible (true);
    toFront (false);
    startTimerHz (60);
    repaint();
}

void IntroOverlay::finish()
{
    finished = true;
}

void IntroOverlay::timerCallback()
{
    const auto now     = juce::Time::getMillisecondCounterHiRes();
    const auto elapsed = now - startMs;
    const auto dt      = juce::jlimit (0.0, 100.0, now - lastTickMs);
    lastTickMs = now;

    // Cible : progression temporelle (easing) ; tant que le scan n'est pas fini, plafond à 92 %
    const float timeTarget = easeOutCubic ((float) (elapsed / kIntroMinMs));
    const float scanValue  = progress != nullptr ? progress->load (std::memory_order_relaxed) : 0.0f;
    const float target     = finished ? timeTarget : 0.92f * juce::jmax (timeTarget, scanValue);

    // Lissage exponentiel indépendant de la cadence d'affichage
    shown += (target - shown) * (float) (1.0 - std::exp (-dt / 55.0));

    if (finished && ! fading && elapsed >= kIntroMinMs && shown > 0.985f)
    {
        shown       = 1.0f;
        fading      = true;
        fadeStartMs = now;

        if (onRevealStart)
            onRevealStart();
    }

    if (fading)
    {
        opacity = 1.0f - easeOutCubic ((float) ((now - fadeStartMs) / kIntroFadeMs));

        if (opacity <= 0.001f)
        {
            stopTimer();
            setVisible (false);

            if (onHidden)
                onHidden();

            return;
        }
    }

    repaint();
}

void IntroOverlay::paint (juce::Graphics& g)
{
    g.fillAll (Theme::background.withAlpha (opacity));

    const auto elapsed   = juce::Time::getMillisecondCounterHiRes() - startMs;
    const float appear   = easeOutCubic ((float) (elapsed / kTitleInMs));
    const float content  = appear * opacity;
    const float lift     = (1.0f - appear) * 10.0f + (1.0f - opacity) * -6.0f;

    if (content <= 0.0f)
        return;

    const auto centre = getLocalBounds().getCentre().toFloat().translated (0.0f, lift - 18.0f);

    // ---- Titre « Aura Clean »
    const auto titleFont  = Theme::font (40.0f, true);
    const auto auraText   = juce::String ("Aura");
    const auto cleanText  = juce::String ("Clean");
    const float auraW     = juce::GlyphArrangement::getStringWidth (titleFont, auraText);
    const float spaceW    = juce::GlyphArrangement::getStringWidth (titleFont, " ");
    const float cleanW    = juce::GlyphArrangement::getStringWidth (titleFont, cleanText);
    const float totalW    = auraW + spaceW + cleanW;
    const float x0        = centre.x - totalW * 0.5f;
    const juce::Rectangle<float> titleRow (x0, centre.y - 28.0f, totalW + 4.0f, 48.0f);

    g.setFont (titleFont);
    g.setColour (Theme::text.withAlpha (content));
    g.drawText (auraText, titleRow.withWidth (auraW + 2.0f), juce::Justification::centredLeft, false);

    Theme::drawGradientText (g, cleanText, titleFont, titleRow.withTrimmedLeft (auraW + spaceW),
                             juce::Justification::centredLeft,
                             Theme::cyan.withAlpha (content), Theme::violet.withAlpha (content));

    g.setColour (Theme::textFaint.withAlpha (content));
    g.setFont (Theme::font (11.5f, true));
    g.drawText (fr ("NEXTDSP  ·  PLUGIN MANAGER"),
                juce::Rectangle<float> (centre.x - 160.0f, centre.y + 22.0f, 320.0f, 16.0f),
                juce::Justification::centred, false);

    // ---- Barre de chargement minimale
    const juce::Rectangle<float> track (centre.x - 120.0f, centre.y + 54.0f, 240.0f, 3.0f);
    g.setColour (juce::Colour (0xff1b1e27).withAlpha (content));
    g.fillRoundedRectangle (track, 1.5f);

    const auto bar = track.withWidth (juce::jmax (3.0f, track.getWidth() * juce::jlimit (0.0f, 1.0f, shown)));
    Theme::fillRoundedRectWithGradient (g, bar, 1.5f,
                                        juce::ColourGradient (Theme::cyan.withAlpha (0.85f * content), track.getX(), 0.0f,
                                                              Theme::violet.withAlpha (0.85f * content), track.getRight(), 0.0f, false));

    // Halo discret en tête de barre
    g.setColour (Theme::cyan.withAlpha (0.18f * content));
    g.fillEllipse (juce::Rectangle<float> (10.0f, 10.0f).withCentre ({ bar.getRight(), bar.getCentreY() }));

    g.setColour (Theme::textFaint.withAlpha (content * 0.9f));
    g.setFont (Theme::font (12.5f));
    g.drawText (finished ? fr ("Prêt") : fr ("Analyse des plugins…"),
                juce::Rectangle<float> (centre.x - 160.0f, track.getBottom() + 12.0f, 320.0f, 18.0f),
                juce::Justification::centred, false);
}

//==============================================================================
//  MainComponent
//==============================================================================
MainComponent::MainComponent()
{
    // ---------------------------------------------------------------- Header
    searchBox.setTextToShowWhenEmpty (fr ("Rechercher un plugin, une marque, une version…"), Theme::textFaint);
    searchBox.setFont (Theme::font (15.0f));
    searchBox.setIndents (14, 0);
    searchBox.setJustification (juce::Justification::centredLeft);
    searchBox.onTextChange = [this] { library.filters.search = searchBox.getText(); filtersChanged(); };
    searchBox.onEscapeKey  = [this]
    {
        if (searchBox.getText().isNotEmpty())
            searchBox.setText ({}, true);
        else
            grabKeyboardFocus();  // Échap une 2e fois : quitte la recherche
    };
    addAndMakeVisible (searchBox);

    for (int f = 0; f < kNumFormats; ++f)
    {
        const auto format = kAllFormats[(size_t) f];
        auto& chip = formatChips[(size_t) f];
        setupChip (chip, formatLabel (format), Theme::formatColour (format));
        chip.setTooltip (fr ("Afficher / masquer les plugins ") + formatLabel (format));
        chip.onClick = [this, f] { library.filters.formats[(size_t) f] = formatChips[(size_t) f].getToggleState(); filtersChanged(); };
    }

    archBox.addItem (fr ("Toutes architectures"), (int) ArchFilter::All);
    archBox.addItem (fr ("Apple Silicon (natif)"), (int) ArchFilter::AppleSilicon);
    archBox.addItem (fr ("Intel uniquement"),      (int) ArchFilter::IntelOnly);
    archBox.addItem (fr ("Universal"),             (int) ArchFilter::Universal);
    archBox.setSelectedId ((int) ArchFilter::All, juce::dontSendNotification);
    archBox.onChange = [this] { library.filters.arch = (ArchFilter) archBox.getSelectedId(); filtersChanged(); };
    addAndMakeVisible (archBox);

    statusBox.addItem (fr ("Tous les états"), (int) StatusFilter::All);
    statusBox.addItem (fr ("Actifs"),         (int) StatusFilter::Active);
    statusBox.addItem (fr ("Désactivés"),     (int) StatusFilter::Disabled);
    statusBox.setSelectedId ((int) StatusFilter::All, juce::dontSendNotification);
    statusBox.onChange = [this] { library.filters.status = (StatusFilter) statusBox.getSelectedId(); filtersChanged(); };
    addAndMakeVisible (statusBox);

    setupButton (rescanButton, fr ("Rescanner"), "ghost");
    rescanButton.setTooltip (fr ("Relancer l'analyse (⌘R)"));
    rescanButton.onClick = [this] { startScan(); };

    // ------------------------------------------------------- Sélection rapide
    auto selectFormat = [this] (PluginFormat format)
    {
        // Rend le format visible s'il était filtré, sinon l'action serait sans effet
        auto& chip = formatChips[(size_t) format];

        if (! chip.getToggleState())
        {
            chip.setToggleState (true, juce::dontSendNotification);
            library.filters.formats[(size_t) format] = true;
            library.rebuild();
            table.refresh();
        }

        library.selectFormatInVisible (format);
        selectionChanged();
    };

    setupButton (selectVst3, fr ("Tous les VST3"));
    setupButton (selectAu,   fr ("Tous les AU"));
    setupButton (selectAax,  fr ("Tous les AAX"));
    setupButton (selectVendor,  fr ("Toute la marque"));
    setupButton (selectVisible, fr ("Tout cocher"));
    setupButton (selectNone,    fr ("Aucun"), "ghost");

    selectVst3.onClick    = [selectFormat] { selectFormat (PluginFormat::VST3); };
    selectAu.onClick      = [selectFormat] { selectFormat (PluginFormat::AU); };
    selectAax.onClick     = [selectFormat] { selectFormat (PluginFormat::AAX); };
    selectVendor.onClick  = [this] { library.selectVendor (library.filters.vendorKey, true); selectionChanged(); };
    selectVisible.onClick = [this] { library.selectAllVisible (true); selectionChanged(); };
    selectNone.onClick    = [this] { library.clearSelection(); selectionChanged(); };

    selectVst3.setTooltip (fr ("Coche tous les VST3 visibles (respecte la marque, la recherche et les filtres)"));
    selectAu.setTooltip   (fr ("Coche tous les Audio Units visibles"));
    selectAax.setTooltip  (fr ("Coche tous les AAX visibles"));
    selectVendor.setTooltip (fr ("Coche tous les plugins de la marque choisie à gauche (clic droit sur une marque : idem)"));

    // ------------------------------------------------------------ Vues
    sidebar.onFilterChanged    = [this] { filtersChanged(); };
    sidebar.onSelectionChanged = [this] { selectionChanged(); };
    table.onSelectionChanged   = [this] { selectionChanged(); };
    table.onUninstallEntry     = [this] (int entryIndex) { uninstallEntry (entryIndex); };
    table.onShortcut           = [this] (const juce::KeyPress& key) { return handleShortcut (key); };
    sidebar.onShortcut         = [this] (const juce::KeyPress& key) { return handleShortcut (key); };
    addAndMakeVisible (sidebar);
    addAndMakeVisible (table);

    // ------------------------------------------------------- Barre d'actions
    setupButton (enableButton,    fr ("Réactiver"), "ghost");
    setupButton (disableButton,   fr ("Désactiver"));
    setupButton (uninstallButton, fr ("Désinstaller la sélection"), "danger");

    enableButton.setTooltip    (fr ("Remet les plugins désactivés à leur emplacement d'origine"));
    disableButton.setTooltip   (fr ("Déplace les plugins dans « VST3 (Disabled) », « Components (Disabled) » ou « Plug-Ins (Unused) » : invisibles pour les DAW, rien n'est supprimé"));
    uninstallButton.setTooltip (fr ("Envoie les bundles sélectionnés à la Corbeille"));

    enableButton.onClick    = [this] { requestOperation (Operation::Enable); };
    disableButton.onClick   = [this] { requestOperation (Operation::Disable); };
    uninstallButton.onClick = [this] { requestOperation (Operation::Uninstall); };

    addChildComponent (overlay);

    intro.onRevealStart = [this]
    {
        table.startReveal();
        juce::Desktop::getInstance().getAnimator().fadeIn (&sidebar, 260);
    };
    intro.onHidden = [this] { introHidden(); };
    addChildComponent (intro);

    // ---- Mises à jour
    updateBanner.onLayoutChanged = [this] { resized(); };
    updateBanner.onDownload      = [] (const juce::URL& url)
    {
        if (UpdateChecker::isTrustedDownloadUrl (url))
            url.launchInDefaultBrowser();
    };
    updateBanner.onSkipVersion   = [] (const juce::String& version) { UpdateChecker::setSkippedVersion (version); };
    addChildComponent (updateBanner);

    // Vérification silencieuse peu après le lancement (laisse l'intro se jouer)
    juce::Component::SafePointer<MainComponent> safeSelf (this);
    juce::Timer::callAfterDelay (1500, [safeSelf]
    {
        if (auto* self = safeSelf.getComponent())
            self->checkForUpdates (false);
    });

    setWantsKeyboardFocus (true);
    setSize (1380, 840);
    updateButtons();
    startScan();
}

MainComponent::~MainComponent()
{
    cancelScan = true;
    stopLeftoverEstimate();
    joinWorker();
}

void MainComponent::setupChip (juce::TextButton& chip, const juce::String& text, juce::Colour accent)
{
    chip.setButtonText (text);
    chip.setClickingTogglesState (true);
    chip.setToggleState (true, juce::dontSendNotification);
    chip.getProperties().set ("auraStyle", "chip");
    chip.getProperties().set ("auraAccent", (juce::int64) accent.getARGB());
    addAndMakeVisible (chip);
}

void MainComponent::setupButton (juce::TextButton& button, const juce::String& text, const char* style)
{
    button.setButtonText (text);
    button.getProperties().set ("auraStyle", juce::String (style));
    addAndMakeVisible (button);
}

void MainComponent::joinWorker()
{
    if (worker.joinable())
        worker.join();
}

bool MainComponent::canQuit()
{
    if (operationRunning)
    {
        juce::AlertWindow::showAsync (juce::MessageBoxOptions()
                                          .withIconType (juce::MessageBoxIconType::InfoIcon)
                                          .withTitle ("AuraClean")
                                          .withMessage (fr ("Une opération sur les plugins est en cours. Patientez jusqu'à la fin avant de quitter."))
                                          .withButton ("OK")
                                          .withAssociatedComponent (this),
                                      nullptr);
        return false;
    }

    cancelScan     = true;
    cancelEstimate = true;
    return true;
}

//==============================================================================
void MainComponent::paint (juce::Graphics& g)
{
    g.fillAll (Theme::background);

    // ---- Header
    g.setColour (Theme::panel);
    g.fillRect (headerArea);
    g.setColour (Theme::border);
    g.fillRect (headerArea.getX(), headerArea.getBottom() - 1, headerArea.getWidth(), 1);

    auto logo = headerArea.withWidth (270).reduced (22, 12);
    auto titleLine = logo.removeFromTop (28);

    g.setFont (Theme::font (24.0f, true));
    g.setColour (Theme::text);
    g.drawText ("Aura", titleLine, juce::Justification::bottomLeft, false);
    Theme::drawGradientText (g, "Clean", Theme::font (24.0f, true), titleLine.withTrimmedLeft (57).toFloat(),
                             juce::Justification::bottomLeft, Theme::cyan, Theme::violet);

    g.setColour (Theme::textFaint);
    g.setFont (Theme::font (12.0f, true));
    g.drawText (fr ("NEXTDSP  ·  PLUGIN MANAGER"), logo, juce::Justification::topLeft, false);

    // ---- Barre de sélection rapide
    g.setColour (Theme::panelAlt);
    g.fillRect (bulkArea);
    g.setColour (Theme::border);
    g.fillRect (bulkArea.getX(), bulkArea.getBottom() - 1, bulkArea.getWidth(), 1);
    g.setColour (Theme::textDim);
    g.setFont (Theme::font (12.0f, true));
    g.drawText (fr ("SÉLECTION RAPIDE"), bulkArea.withTrimmedLeft (16).withWidth (130), juce::Justification::centredLeft, false);

    // ---- Barre inférieure
    g.setColour (Theme::panel);
    g.fillRect (bottomArea);
    g.setColour (Theme::border);
    g.fillRect (bottomArea.getX(), bottomArea.getY(), bottomArea.getWidth(), 1);

    auto s = summaryArea;

    if (summary.bundles == 0)
    {
        g.setColour (Theme::textFaint);
        g.setFont (Theme::font (15.0f));
        g.drawText (fr ("Aucune sélection — cochez des plugins ou utilisez la sélection rapide."), s,
                    juce::Justification::centredLeft, true);
        return;
    }

    auto line1 = s.removeFromTop (s.getHeight() / 2 + 2);

    g.setColour (Theme::text);
    g.setFont (Theme::font (16.5f, true));
    g.drawText (countLabel (summary.plugins, "plugin sélectionné", "plugins sélectionnés") + fr ("  —  ")
                    + formatBytes (summary.bytes) + fr (" à libérer"),
                line1, juce::Justification::bottomLeft, true);

    juce::StringArray details;
    details.add (countLabel (summary.bundles, "fichier", "fichiers"));

    if (summary.disabledBundles > 0)
        details.add (countLabel (summary.disabledBundles, "déjà désactivé", "déjà désactivés"));

    if (summary.hidden > 0)
        details.add (countLabel (summary.hidden, "masqué par les filtres", "masqués par les filtres"));

    g.setColour (summary.hidden > 0 ? Theme::warning : Theme::textDim);
    g.setFont (Theme::font (13.0f));
    g.drawText (details.joinIntoString (fr ("  ·  ")), s.withTrimmedTop (2), juce::Justification::topLeft, true);
}

void MainComponent::resized()
{
    auto area = getLocalBounds();
    overlay.setBounds (area);
    intro.setBounds (area);

    if (deepCleanDialog != nullptr)
        deepCleanDialog->setBounds (area);

    if (closingDialog != nullptr)
        closingDialog->setBounds (area);

    if (const int bannerHeight = updateBanner.getCurrentHeight(); bannerHeight > 0)
        updateBanner.setBounds (area.removeFromTop (bannerHeight));
    else
        updateBanner.setBounds ({});

    headerArea = area.removeFromTop (68);
    bottomArea = area.removeFromBottom (68);
    sidebar.setBounds (area.removeFromLeft (270));
    bulkArea = area.removeFromTop (50);
    table.setBounds (area);

    // ---- Header (de droite à gauche)
    auto h = headerArea.reduced (16, 15).withTrimmedLeft (270 - 16);
    rescanButton.setBounds (h.removeFromRight (118));
    h.removeFromRight (10);
    statusBox.setBounds (h.removeFromRight (150));
    h.removeFromRight (8);
    archBox.setBounds (h.removeFromRight (196));
    h.removeFromRight (14);

    for (int f = kNumFormats - 1; f >= 0; --f)
    {
        formatChips[(size_t) f].setBounds (h.removeFromRight (64));
        h.removeFromRight (6);
    }

    h.removeFromRight (8);
    searchBox.setBounds (h);

    // ---- Sélection rapide
    auto b = bulkArea.reduced (12, 9).withTrimmedLeft (134);

    for (auto* button : { &selectVst3, &selectAu, &selectAax })
    {
        button->setBounds (b.removeFromLeft (118));
        b.removeFromLeft (8);
    }

    b.removeFromLeft (8);
    selectVendor.setBounds (b.removeFromLeft (138));
    b.removeFromLeft (8);
    selectVisible.setBounds (b.removeFromLeft (110));
    b.removeFromLeft (8);
    selectNone.setBounds (b.removeFromLeft (80));

    // ---- Barre inférieure
    auto bottom = bottomArea.reduced (20, 13);
    uninstallButton.setBounds (bottom.removeFromRight (232));
    bottom.removeFromRight (10);
    disableButton.setBounds (bottom.removeFromRight (128));
    bottom.removeFromRight (10);
    enableButton.setBounds (bottom.removeFromRight (120));
    bottom.removeFromRight (20);
    summaryArea = bottom;
}

bool MainComponent::keyPressed (const juce::KeyPress& key)
{
    return handleShortcut (key);
}

bool MainComponent::handleShortcut (const juce::KeyPress& key)
{
    const auto cmd = juce::ModifierKeys::commandModifier;

    // Modale ouverte : elle seule reçoit le clavier (Échap ferme, Entrée confirme)
    if (deepCleanDialog != nullptr)
        return deepCleanDialog->keyPressed (key);

    if (closingDialog != nullptr || intro.isActive())
        return true;

    const bool searchFocused = searchBox.hasKeyboardFocus (false);

    if (key == juce::KeyPress ('f', cmd, 0))
    {
        searchBox.grabKeyboardFocus();
        searchBox.selectAll();
        return true;
    }

    if (key == juce::KeyPress ('r', cmd, 0))
    {
        startScan();
        return true;
    }

    if (searchFocused)
        return false;  // ⌘A, Suppr, Échap gardent leur sens d'édition de texte

    if (key == juce::KeyPress ('a', cmd, 0))
    {
        library.selectAllVisible (true);
        selectionChanged();
        return true;
    }

    if (key == juce::KeyPress::escapeKey)
    {
        library.clearSelection();
        table.deselectAllRows();
        selectionChanged();
        return true;
    }

    if (key == juce::KeyPress::backspaceKey || key == juce::KeyPress::deleteKey
        || key == juce::KeyPress (juce::KeyPress::backspaceKey, cmd, 0))
    {
        if (! busy && summary.bundles > 0)
            requestOperation (Operation::Uninstall);

        return true;
    }

    return false;
}

void MainComponent::uninstallEntry (int entryIndex)
{
    if (busy || entryIndex < 0 || (size_t) entryIndex >= library.data().entries.size())
        return;

    std::vector<int> targets;

    for (const auto& list : library.data().entries[(size_t) entryIndex].bundles)
        targets.insert (targets.end(), list.begin(), list.end());

    if (! targets.empty())
        startDeepClean (std::move (targets), 0);
}

//==============================================================================
void MainComponent::filtersChanged()
{
    library.rebuild();
    table.refresh();
    selectionChanged();
}

void MainComponent::selectionChanged()
{
    summary = library.summary();
    table.refresh();
    updateButtons();
    repaint (bottomArea);
}

void MainComponent::updateButtons()
{
    const bool idle = ! busy;
    enableButton.setEnabled (idle && summary.disabledBundles > 0);
    disableButton.setEnabled (idle && summary.enabledBundles > 0);
    uninstallButton.setEnabled (idle && summary.bundles > 0);
    rescanButton.setEnabled (idle);
    selectVendor.setEnabled (idle && library.filters.vendorKey.isNotEmpty());

    uninstallButton.setButtonText (summary.bundles > 0
                                       ? fr ("Désinstaller (") + formatBytes (summary.bytes) + ")"
                                       : fr ("Désinstaller la sélection"));
}

//==============================================================================
void MainComponent::startScan (bool withIntro)
{
    if (busy)
        return;

    stopLeftoverEstimate();
    joinWorker();
    busy = true;
    cancelScan = false;
    progress = 0.0f;
    updateButtons();

    if (withIntro)
        intro.start (&progress);

    juce::Component::SafePointer<MainComponent> safe (this);

    worker = std::thread ([this, safe]
    {
        auto result = std::make_shared<ScanResult> (
            PluginScanner::scan (&cancelScan, [this] (float p) { progress.store (p, std::memory_order_relaxed); }));

        juce::MessageManager::callAsync ([safe, result]
        {
            if (auto* self = safe.getComponent())
                self->scanFinished (std::move (*result));
        });
    });
}

void MainComponent::scanFinished (ScanResult result)
{
    busy = false;

    if (result.cancelled)
    {
        intro.finish();
        updateButtons();
        return;
    }

    const int bundleCount = (int) result.bundles.size();
    const int pluginCount = (int) result.entries.size();
    const auto duration   = result.elapsedMs < 1000.0
                              ? juce::String (juce::jmax (1, juce::roundToInt (result.elapsedMs))) + " ms"
                              : juce::String (result.elapsedMs / 1000.0, 2).replaceCharacter ('.', ',') + " s";

    library.setScanResult (std::move (result));
    sidebar.refresh();
    sidebar.setStats (countLabel (pluginCount, "plugin", "plugins") + fr ("  ·  ")
                      + countLabel (bundleCount, "bundle", "bundles") + fr ("  ·  ") + duration);
    filtersChanged();

    ++scanGeneration;
    startLeftoverEstimate();

    // Les données sont en place derrière l'intro : elle peut terminer sa barre et s'effacer
    if (intro.isActive())
        intro.finish();
}

void MainComponent::introHidden()
{
    grabKeyboardFocus();

    if (hasPendingUpdate)
    {
        hasPendingUpdate = false;
        updateBanner.show (pendingUpdate);
    }
}

//==============================================================================
//  Mises à jour
//==============================================================================
juce::String MainComponent::applicationVersion()
{
    if (auto* app = juce::JUCEApplicationBase::getInstance())
        return app->getApplicationVersion();

    return "0.0.0";
}

void MainComponent::checkForUpdates (bool userInitiated)
{
    if (updateChecker.isChecking())
        return;

    juce::Component::SafePointer<MainComponent> safe (this);

    updateChecker.check ([safe, userInitiated] (const UpdateInfo& info)
    {
        if (auto* self = safe.getComponent())
            self->handleUpdateResult (info, userInitiated);
    });
}

void MainComponent::handleUpdateResult (const UpdateInfo& info, bool userInitiated)
{
    using Status = UpdateInfo::Status;

    if (info.status == Status::UpdateAvailable)
    {
        // Au lancement, on respecte « Ignorer cette version » ; depuis le menu, on montre toujours.
        if (! userInitiated && info.latestVersion == UpdateChecker::skippedVersion())
            return;

        if (intro.isActive())
        {
            pendingUpdate    = info;
            hasPendingUpdate = true;
        }
        else
        {
            updateBanner.show (info);
        }
        return;
    }

    if (! userInitiated)
        return;  // vérification silencieuse : ni « à jour » ni erreur réseau à l'écran

    const bool upToDate = info.status == Status::UpToDate;

    juce::AlertWindow::showAsync (juce::MessageBoxOptions()
                                      .withIconType (upToDate ? juce::MessageBoxIconType::InfoIcon
                                                              : juce::MessageBoxIconType::WarningIcon)
                                      .withTitle (upToDate ? fr ("AuraClean est à jour")
                                                           : fr ("Vérification impossible"))
                                      .withMessage (upToDate ? fr ("Vous utilisez la dernière version (") + info.currentVersion + ")."
                                                             : info.errorMessage)
                                      .withButton ("OK")
                                      .withAssociatedComponent (this),
                                  nullptr);
}

void MainComponent::showAbout()
{
    juce::Component::SafePointer<MainComponent> safe (this);

    juce::AlertWindow::showAsync (juce::MessageBoxOptions()
                                      .withIconType (juce::MessageBoxIconType::NoIcon)
                                      .withTitle ("AuraClean")
                                      .withMessage (fr ("Version ") + applicationVersion()
                                                    + fr ("\n\nGestionnaire et désinstalleur de plugins audio\n"
                                                          "VST3 · Audio Units · AAX\n\n© 2026 NextDSP — AuraSuite"))
                                      .withButton (fr ("Rechercher des mises à jour"))
                                      .withButton (fr ("Fermer"))
                                      .withAssociatedComponent (this),
                                  [safe] (int result)
                                  {
                                      if (result == 1)
                                          if (auto* self = safe.getComponent())
                                              self->checkForUpdates (true);
                                  });
}

//==============================================================================
void MainComponent::startLeftoverEstimate()
{
    stopLeftoverEstimate();

    const auto& data = library.data();
    std::vector<LeftoverQuery> queries;
    queries.reserve (data.entries.size());

    for (const auto& entry : data.entries)
    {
        LeftoverQuery q;
        q.pluginName = entry.name;
        q.vendorName = entry.unknownVendor ? juce::String() : entry.vendor;

        for (const auto& list : entry.bundles)
        {
            for (int bi : list)
            {
                const auto& b = data.bundles[(size_t) bi];
                q.bundles.push_back (b.file);

                if (b.bundleId.isNotEmpty())
                    q.bundleIds.addIfNotAlreadyThere (b.bundleId);
            }
        }

        queries.push_back (std::move (q));
    }

    cancelEstimate = false;
    const int generation = scanGeneration;
    juce::Component::SafePointer<MainComponent> safe (this);

    estimateWorker = std::thread ([this, safe, generation, queryList = std::move (queries)]
    {
        // Sans ~/Documents (pas d'invite d'autorisation macOS au lancement) ni reçus .pkg (lsbom)
        LeftoverScanner::Options options;
        options.includeReceipts  = false;
        options.includeDocuments = false;

        const auto results = LeftoverScanner::scan (queryList, {}, &cancelEstimate, options);

        if (cancelEstimate.load())
            return;

        std::vector<juce::int64> bytes (queryList.size(), 0);

        for (size_t i = 0; i < results.size() && i < bytes.size(); ++i)
            for (const auto& item : results[i])
                bytes[i] += item.sizeBytes;

        juce::MessageManager::callAsync ([safe, generation, values = std::move (bytes)]() mutable
        {
            if (auto* self = safe.getComponent(); self != nullptr && self->scanGeneration == generation)
            {
                self->library.setLeftoverEstimates (std::move (values));
                self->table.refresh();
            }
        });
    });
}

void MainComponent::stopLeftoverEstimate()
{
    cancelEstimate = true;

    if (estimateWorker.joinable())
        estimateWorker.join();
}

//==============================================================================
void MainComponent::requestOperation (Operation op)
{
    if (busy)
        return;

    const auto& bundles = library.data().bundles;
    std::vector<int> targets;
    juce::int64 bytes = 0;
    int hidden = 0;
    bool needsAdmin = false;
    juce::StringArray names;

    for (int bi : library.selectedBundles())
    {
        const auto& b = bundles[(size_t) bi];

        if ((op == Operation::Disable && b.disabled) || (op == Operation::Enable && ! b.disabled))
            continue;

        targets.push_back (bi);

        if (! b.isSymlink)
            bytes += b.sizeBytes;

        if (! library.bundlePasses (bi) || ! library.isEntryVisible (b.entryIndex))
            ++hidden;

        const auto destination = op == Operation::Uninstall ? juce::File()
                               : (op == Operation::Disable ? b.quarantineFolder : b.homeFolder);
        needsAdmin = needsAdmin || UninstallerMac::requiresAdministrator (b.file, destination);

        const auto& entry = library.data().entries[(size_t) b.entryIndex];
        names.addIfNotAlreadyThere (entry.vendor + " " + entry.name);
    }

    if (targets.empty())
        return;

    if (op == Operation::Uninstall)
    {
        startDeepClean (std::move (targets), hidden);
        return;
    }

    juce::String title, message, confirm;
    auto icon = juce::MessageBoxIconType::QuestionIcon;

    switch (op)
    {
        case Operation::Uninstall:
            title   = fr ("Désinstaller ") + countLabel ((int) targets.size(), "bundle", "bundles") + " ?";
            message = fr ("Les bundles seront envoyés à la Corbeille (") + formatBytes (bytes)
                    + fr (").\nL'espace ne sera libéré qu'une fois la Corbeille vidée.");
            confirm = fr ("Mettre à la Corbeille");
            icon    = juce::MessageBoxIconType::WarningIcon;
            break;

        case Operation::Disable:
            title   = fr ("Désactiver ") + countLabel ((int) targets.size(), "bundle", "bundles") + " ?";
            message = fr ("Les plugins seront déplacés dans un dossier adjacent « (Disabled) » / « (Unused) ».\n"
                          "Ils n'apparaîtront plus dans vos DAW et pourront être réactivés à tout moment.");
            confirm = fr ("Désactiver");
            break;

        case Operation::Enable:
            title   = fr ("Réactiver ") + countLabel ((int) targets.size(), "bundle", "bundles") + " ?";
            message = fr ("Les plugins retrouveront leur dossier d'origine et seront de nouveau visibles par les DAW.");
            confirm = fr ("Réactiver");
            break;
    }

    message << "\n\n";

    for (int i = 0; i < juce::jmin (10, names.size()); ++i)
        message << fr ("•  ") << names[i] << "\n";

    if (names.size() > 10)
        message << fr ("…et ") << (names.size() - 10) << fr (" autre(s)\n");

    if (hidden > 0)
        message << "\n" << fr ("⚠  ") << countLabel (hidden, "élément est masqué", "éléments sont masqués")
                << fr (" par les filtres actuels mais fait partie de la sélection.\n");

    if (needsAdmin)
        message << "\n" << fr ("Un mot de passe administrateur sera demandé une seule fois pour les plugins système.");

    juce::Component::SafePointer<MainComponent> safe (this);

    juce::AlertWindow::showAsync (juce::MessageBoxOptions()
                                      .withIconType (icon)
                                      .withTitle (title)
                                      .withMessage (message)
                                      .withButton (confirm)
                                      .withButton (fr ("Annuler"))
                                      .withAssociatedComponent (this),
                                  [safe, op, targets] (int result)
                                  {
                                      if (result == 1)
                                          if (auto* self = safe.getComponent())
                                              self->runOperation (op, targets);
                                  });
}

void MainComponent::runOperation (Operation op, std::vector<int> bundleIndices)
{
    if (busy)
        return;

    const auto& bundles = library.data().bundles;
    std::vector<juce::File> files;
    std::vector<BundleMove> moves;
    bool touchesAudioUnits = false;

    for (int bi : bundleIndices)
    {
        if (bi < 0 || (size_t) bi >= bundles.size())
            continue;

        const auto& b = bundles[(size_t) bi];
        touchesAudioUnits = touchesAudioUnits || b.format == PluginFormat::AU;

        if (op == Operation::Uninstall)
            files.push_back (b.file);
        else if (op == Operation::Disable)
            moves.push_back ({ b.file, b.quarantineFolder, true });
        else
            moves.push_back ({ b.file, b.homeFolder, false });
    }

    joinWorker();
    busy = operationRunning = true;
    updateButtons();

    overlay.show (op == Operation::Uninstall ? fr ("Envoi à la Corbeille…")
                : op == Operation::Disable   ? fr ("Désactivation des plugins…")
                                             : fr ("Réactivation des plugins…"),
                  nullptr);

    juce::Component::SafePointer<MainComponent> safe (this);
    auto roots = PluginScanner::allowedRoots();

    worker = std::thread ([safe, op, fileList = std::move (files), moveList = std::move (moves),
                           rootList = std::move (roots), touchesAudioUnits]
    {
        const auto report = op == Operation::Uninstall ? UninstallerMac::moveToTrash (fileList, rootList)
                                                       : UninstallerMac::moveBundles (moveList, rootList);

        if (touchesAudioUnits && report.succeeded > 0)
            UninstallerMac::refreshAudioUnitRegistrar();

        juce::MessageManager::callAsync ([safe, op, report]
        {
            if (auto* self = safe.getComponent())
                self->operationFinished (op, report);
        });
    });
}

void MainComponent::operationFinished (Operation op, const OperationReport& report, juce::int64 bytesOverride)
{
    busy = operationRunning = false;
    overlay.hide();

    // Octets réellement traités, d'après les chemins ayant réussi
    juce::int64 bytes = bytesOverride;

    if (bytes < 0)
    {
        bytes = 0;
        std::unordered_set<std::string> succeeded;

        for (const auto& p : report.succeededPaths)
            succeeded.insert (p.toStdString());

        for (const auto& b : library.data().bundles)
            if (! b.isSymlink && succeeded.count (b.file.getFullPathName().toStdString()) != 0)
                bytes += b.sizeBytes;
    }

    juce::String message;
    message << report.succeeded << fr (" sur ") << report.requested << fr (" élément(s) traité(s).");

    if (op == Operation::Uninstall && report.succeeded > 0)
        message << "\n" << formatBytes (bytes) << fr (" envoyés à la Corbeille — videz-la pour libérer l'espace.");
    else if (op == Operation::Disable && report.succeeded > 0)
        message << "\n" << fr ("Les plugins désactivés restent visibles ici avec le filtre « Désactivés ».");

    if (! report.errors.isEmpty())
    {
        message << "\n\n" << fr ("Problèmes :") << "\n";

        for (int i = 0; i < juce::jmin (8, report.errors.size()); ++i)
            message << fr ("•  ") << report.errors[i] << "\n";

        if (report.errors.size() > 8)
            message << fr ("…et ") << (report.errors.size() - 8) << fr (" autre(s).");
    }

    if (! report.leftovers.isEmpty())
        message << "\n" << fr ("Éléments restés en transit : ") << report.leftovers.joinIntoString (", ");

    const bool ok = report.errors.isEmpty();

    juce::AlertWindow::showAsync (juce::MessageBoxOptions()
                                      .withIconType (ok ? juce::MessageBoxIconType::InfoIcon
                                                        : juce::MessageBoxIconType::WarningIcon)
                                      .withTitle (ok ? fr ("Opération terminée") : fr ("Opération terminée avec des erreurs"))
                                      .withMessage (message)
                                      .withButton ("OK")
                                      .withAssociatedComponent (this),
                                  nullptr);

    startScan (false);  // rescan silencieux : pas d'intro après une opération
}

//==============================================================================
//  Deep Clean
//==============================================================================
void MainComponent::startDeepClean (std::vector<int> targets, int hiddenCount)
{
    if (busy)
        return;

    const auto& data = library.data();
    std::set<int> targetSet (targets.begin(), targets.end());
    std::map<int, std::vector<int>> byEntry;

    for (int bi : targets)
        byEntry[data.bundles[(size_t) bi].entryIndex].push_back (bi);

    auto groups = std::make_shared<std::vector<CleanGroup>>();
    std::vector<LeftoverQuery> queries;
    std::vector<size_t> queryGroup;
    std::vector<juce::File> allDeleted;
    int partialPlugins = 0;

    for (const auto& [entryIndex, bundleList] : byEntry)
    {
        const auto& entry = data.entries[(size_t) entryIndex];

        CleanGroup group;
        group.title  = entry.name;
        group.vendor = entry.vendor;

        for (int bi : bundleList)
        {
            const auto& b = data.bundles[(size_t) bi];

            CleanCandidate c;
            c.file          = b.file;
            c.badge         = formatLabel (b.format) + (b.disabled ? fr (" · OFF") : juce::String());
            c.badgeColour   = Theme::formatColour (b.format);
            c.sizeBytes     = b.isSymlink ? 0 : b.sizeBytes;
            c.isBundle      = true;
            c.isAudioUnit   = b.format == PluginFormat::AU;
            c.requiresAdmin = UninstallerMac::requiresAdministrator (b.file);
            c.detail        = scopeLabel (b.scope);
            group.items.push_back (c);
            allDeleted.push_back (b.file);
        }

        // Règle n°2 : résidus seulement si TOUS les bundles installés du plugin sont cochés
        juce::StringArray missing;

        for (auto f : kAllFormats)
            for (int bi : entry.bundles[(size_t) f])
                if (targetSet.count (bi) == 0)
                    missing.addIfNotAlreadyThere (formatLabel (f));

        group.leftoversEligible = missing.isEmpty();

        if (group.leftoversEligible)
        {
            LeftoverQuery q;
            q.pluginName = entry.name;
            q.vendorName = entry.unknownVendor ? juce::String() : entry.vendor;

            for (const auto& list : entry.bundles)
            {
                for (int bi : list)
                {
                    const auto& b = data.bundles[(size_t) bi];
                    q.bundles.push_back (b.file);

                    if (b.bundleId.isNotEmpty())
                        q.bundleIds.addIfNotAlreadyThere (b.bundleId);
                }
            }

            queries.push_back (std::move (q));
            queryGroup.push_back (groups->size());
        }
        else
        {
            group.note = fr ("Fichiers résiduels non recherchés : ") + missing.joinIntoString (" + ")
                       + fr (" reste(nt) installé(s). Cochez tous les formats pour un nettoyage complet.");
            ++partialPlugins;
        }

        groups->push_back (std::move (group));
    }

    juce::StringArray warnings;

    if (hiddenCount > 0)
        warnings.add (countLabel (hiddenCount, "élément sélectionné est masqué par les filtres",
                                  "éléments sélectionnés sont masqués par les filtres"));

    if (partialPlugins > 0)
        warnings.add (countLabel (partialPlugins, "plugin n'a pas tous ses formats cochés : résidus conservés",
                                  "plugins n'ont pas tous leurs formats cochés : résidus conservés"));

    const auto warning = warnings.joinIntoString (fr ("  ·  "));
    const bool searched = ! queries.empty();

    joinWorker();
    busy = true;
    cancelScan = false;
    updateButtons();
    overlay.show (fr ("Recherche des fichiers résiduels…"), nullptr);

    juce::Component::SafePointer<MainComponent> safe (this);

    worker = std::thread ([this, safe, groups, queryList = std::move (queries), groupOfQuery = std::move (queryGroup),
                           deletedBundles = std::move (allDeleted), warning, searched]
    {
        const auto results = LeftoverScanner::scan (queryList, deletedBundles, &cancelScan);

        for (size_t qi = 0; qi < results.size() && qi < groupOfQuery.size(); ++qi)
        {
            auto& group = (*groups)[groupOfQuery[qi]];

            for (const auto& leftover : results[qi])
            {
                CleanCandidate c;
                c.file           = leftover.file;
                c.badge          = leftoverCategoryLabel (leftover.category);
                c.badgeColour    = leftover.category == LeftoverCategory::Receipt     ? Theme::textDim
                                 : leftover.category == LeftoverCategory::Preferences ? Theme::blue
                                 : leftover.category == LeftoverCategory::Caches
                                       || leftover.category == LeftoverCategory::Logs ? Theme::success
                                 : leftover.isUserContent                             ? Theme::warning
                                                                                      : Theme::violet;
                c.sizeBytes      = leftover.sizeBytes;
                c.isBundle       = false;
                c.requiresAdmin  = leftover.requiresAdmin;
                c.pruneIfEmpty   = leftover.pruneIfEmpty;
                c.vendorKey      = leftover.vendorKey;
                c.detail         = leftover.detail;
                c.defaultChecked = ! leftover.isUserContent;
                group.items.push_back (std::move (c));
            }
        }

        juce::MessageManager::callAsync ([safe, groups, warning, searched]
        {
            if (auto* self = safe.getComponent())
            {
                self->busy = false;
                self->overlay.hide();
                self->updateButtons();

                if (! self->cancelScan.load())
                    self->showDeepClean (std::move (*groups), warning, searched);
            }
        });
    });
}

void MainComponent::showDeepClean (std::vector<CleanGroup> groups, const juce::String& warning, bool leftoversSearched)
{
    deepCleanDialog = std::make_unique<DeepCleanDialog> (std::move (groups), warning, leftoversSearched);

    juce::Component::SafePointer<MainComponent> safe (this);

    deepCleanDialog->onCancel = [safe]
    {
        if (auto* self = safe.getComponent())
            self->closeDeepClean();
    };

    deepCleanDialog->onConfirm = [safe] (std::vector<CleanCandidate> items)
    {
        if (auto* self = safe.getComponent())
        {
            self->closeDeepClean();
            self->runTrash (std::move (items));
        }
    };

    addAndMakeVisible (*deepCleanDialog);
    deepCleanDialog->setBounds (getLocalBounds());
    deepCleanDialog->toFront (true);
    deepCleanDialog->grabKeyboardFocus();
}

void MainComponent::closeDeepClean()
{
    if (deepCleanDialog == nullptr)
        return;

    // Fondu de sortie, puis destruction différée (on est peut-être dans le onClick d'un de ses boutons)
    juce::Component::SafePointer<MainComponent> safe (this);
    closingDialog = std::move (deepCleanDialog);

    auto* closing = closingDialog.get();

    closingDialog->dismiss ([safe, closing]
    {
        juce::MessageManager::callAsync ([safe, closing]
        {
            if (auto* self = safe.getComponent(); self != nullptr && self->closingDialog.get() == closing)
                self->closingDialog.reset();
        });
    });

    grabKeyboardFocus();
}

void MainComponent::runTrash (std::vector<CleanCandidate> candidates)
{
    if (busy || candidates.empty())
        return;

    std::vector<TrashItem> items;
    std::map<juce::String, juce::int64> sizes;
    bool touchesAudioUnits = false;

    for (const auto& c : candidates)
    {
        items.push_back ({ c.file, c.isBundle, c.pruneIfEmpty, c.vendorKey });
        sizes[c.file.getFullPathName()] = c.sizeBytes;
        touchesAudioUnits = touchesAudioUnits || c.isAudioUnit;
    }

    joinWorker();
    busy = operationRunning = true;
    updateButtons();
    overlay.show (fr ("Envoi à la Corbeille…"), nullptr);

    juce::Component::SafePointer<MainComponent> safe (this);
    auto roots = PluginScanner::allowedRoots();

    worker = std::thread ([safe, trashItems = std::move (items), sizeByPath = std::move (sizes),
                           rootList = std::move (roots), touchesAudioUnits]
    {
        const auto report = UninstallerMac::moveItemsToTrash (trashItems, rootList);

        if (touchesAudioUnits && report.succeeded > 0)
            UninstallerMac::refreshAudioUnitRegistrar();

        juce::int64 bytes = 0;

        for (const auto& path : report.succeededPaths)
            if (const auto it = sizeByPath.find (path); it != sizeByPath.end())
                bytes += it->second;

        juce::MessageManager::callAsync ([safe, report, bytes]
        {
            if (auto* self = safe.getComponent())
                self->operationFinished (Operation::Uninstall, report, bytes);
        });
    });
}

} // namespace aura
