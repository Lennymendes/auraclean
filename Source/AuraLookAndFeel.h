#pragma once
// ==============================================================================
//  AuraLookAndFeel.h — AuraClean (NextDSP)
//  Thème sombre AuraSuite : anthracite #0D0E12, accents cyan / violet.
// ==============================================================================

#include <juce_gui_basics/juce_gui_basics.h>

#include "PluginScanner.h"

namespace aura
{
namespace Theme
{
    inline const juce::Colour background { 0xff0d0e12 };
    inline const juce::Colour panel      { 0xff13151b };
    inline const juce::Colour panelAlt   { 0xff181b23 };
    inline const juce::Colour raised     { 0xff20242e };
    inline const juce::Colour border     { 0xff2a2f3b };
    inline const juce::Colour text       { 0xffe8eaf0 };
    inline const juce::Colour textDim    { 0xff8b92a5 };
    inline const juce::Colour textFaint  { 0xff596073 };
    inline const juce::Colour cyan       { 0xff22d3ee };
    inline const juce::Colour violet     { 0xff8b5cf6 };
    inline const juce::Colour blue       { 0xff60a5fa };
    inline const juce::Colour success    { 0xff34d399 };
    inline const juce::Colour warning    { 0xfff59e0b };
    inline const juce::Colour danger     { 0xffef4444 };

    inline juce::Colour formatColour (PluginFormat f)
    {
        switch (f)
        {
            case PluginFormat::VST3: return cyan;
            case PluginFormat::AU:   return violet;
            case PluginFormat::AAX:  return blue;
        }
        return text;
    }

    inline juce::Colour archColour (ArchClass c)
    {
        switch (c)
        {
            case ArchClass::Universal:    return success;
            case ArchClass::AppleSilicon: return cyan;
            case ArchClass::IntelOnly:    return warning;
            case ArchClass::Unknown:      break;
        }
        return textFaint;
    }

    /** Majuscules françaises fiables : toUpperCase() dépend de la locale C et laisse « é » en minuscule. */
    inline juce::String upper (const juce::String& source)
    {
        juce::String out;
        out.preallocateBytes ((size_t) source.getNumBytesAsUTF8() + 1);

        for (auto p = source.getCharPointer(); ! p.isEmpty();)
        {
            auto c = p.getAndAdvance();

            if (c >= 0xe0 && c <= 0xfe && c != 0xf7)  // à…þ (Latin-1) → À…Þ
                c -= 0x20;
            else if (c == 0x153)                     // œ → Œ
                c = 0x152;
            else
                c = juce::CharacterFunctions::toUpperCase (c);

            out += c;
        }

        return out;
    }

    inline juce::Font font (float height, bool bold = false)
    {
        return juce::Font (juce::FontOptions (height, bold ? juce::Font::bold : juce::Font::plain));
    }

    //==========================================================================
    //  Dégradés : TOUJOURS via ces deux fonctions.
    //
    //  Sous macOS (CoreGraphicsContext, JUCE 8), un remplissage non uni est fait en
    //  découpant (CGContextClip) sur la forme puis en peignant le dégradé SANS bornes.
    //  Si la forme est vide — un rectangle de largeur nulle, ou le glyphe d'une ESPACE
    //  dans un drawText() en dégradé — le découpage n'a aucun effet et le dégradé
    //  inonde tout le composant. D'où ces garde-fous :
    //    - on refuse les formes vides ;
    //    - le texte est converti en UN SEUL Path (jamais glyphe par glyphe).
    //==========================================================================
    inline void fillPathWithGradient (juce::Graphics& g, const juce::Path& path, const juce::ColourGradient& gradient)
    {
        const auto bounds = path.getBounds();

        if (path.isEmpty() || bounds.isEmpty() || ! bounds.isFinite())
            return;

        juce::Graphics::ScopedSaveState state (g);
        g.setGradientFill (gradient);
        g.fillPath (path);
    }

    inline void fillRoundedRectWithGradient (juce::Graphics& g, juce::Rectangle<float> r, float corner,
                                             const juce::ColourGradient& gradient)
    {
        if (r.getWidth() < 0.5f || r.getHeight() < 0.5f)
            return;

        juce::Path p;
        p.addRoundedRectangle (r, juce::jmin (corner, r.getWidth() * 0.5f, r.getHeight() * 0.5f));
        fillPathWithGradient (g, p, gradient);
    }

    /** Texte en dégradé horizontal (c1 → c2 sur la largeur réelle du texte), sûr sous CoreGraphics. */
    inline void drawGradientText (juce::Graphics& g, const juce::String& label, const juce::Font& f,
                                  juce::Rectangle<float> area, juce::Justification justification,
                                  juce::Colour c1, juce::Colour c2)
    {
        juce::GlyphArrangement glyphs;
        glyphs.addFittedText (f, label, area.getX(), area.getY(), area.getWidth(), area.getHeight(), justification, 1);

        juce::Path path;
        glyphs.createPath (path);

        const auto b = path.getBounds();
        fillPathWithGradient (g, path, juce::ColourGradient (c1, b.getX(), 0.0f, c2, b.getRight(), 0.0f, false));
    }
} // namespace Theme

//==============================================================================
/** Styles de bouton, via button.getProperties().set ("auraStyle", ...) :
      "primary" (violet), "danger" (rouge), "chip" (toggle à accent), "ghost", défaut.
    Couleur d'accent des chips : propriété "auraAccent" (ARGB en int64). */
class AuraLookAndFeel : public juce::LookAndFeel_V4
{
public:
    AuraLookAndFeel()
        : juce::LookAndFeel_V4 (juce::LookAndFeel_V4::ColourScheme (
              Theme::background, Theme::panel, Theme::panelAlt, Theme::border, Theme::text,
              Theme::raised, Theme::text, Theme::cyan, Theme::text))
    {
        using namespace juce;

        setColour (ResizableWindow::backgroundColourId, Theme::background);
        setColour (DocumentWindow::backgroundColourId, Theme::background);

        setColour (TextEditor::backgroundColourId, Theme::raised);
        setColour (TextEditor::outlineColourId, Theme::border);
        setColour (TextEditor::focusedOutlineColourId, Theme::cyan.withAlpha (0.8f));
        setColour (TextEditor::textColourId, Theme::text);
        setColour (TextEditor::highlightColourId, Theme::cyan.withAlpha (0.28f));
        setColour (TextEditor::highlightedTextColourId, Theme::text);
        setColour (CaretComponent::caretColourId, Theme::cyan);

        setColour (ComboBox::backgroundColourId, Theme::raised);
        setColour (ComboBox::outlineColourId, Theme::border);
        setColour (ComboBox::textColourId, Theme::text);
        setColour (ComboBox::arrowColourId, Theme::textDim);
        setColour (ComboBox::focusedOutlineColourId, Theme::cyan);

        setColour (PopupMenu::backgroundColourId, Theme::panelAlt);
        setColour (PopupMenu::textColourId, Theme::text);
        setColour (PopupMenu::headerTextColourId, Theme::textDim);
        setColour (PopupMenu::highlightedBackgroundColourId, Theme::cyan.withAlpha (0.18f));
        setColour (PopupMenu::highlightedTextColourId, Colours::white);

        setColour (ListBox::backgroundColourId, Colours::transparentBlack);
        setColour (ListBox::outlineColourId, Colours::transparentBlack);
        setColour (ListBox::textColourId, Theme::text);

        setColour (TableHeaderComponent::textColourId, Theme::textDim);
        setColour (TableHeaderComponent::backgroundColourId, Theme::panel);
        setColour (TableHeaderComponent::outlineColourId, Theme::border);
        setColour (TableHeaderComponent::highlightColourId, Theme::raised);

        setColour (ScrollBar::thumbColourId, Theme::border);
        setColour (ScrollBar::trackColourId, Colours::transparentBlack);

        setColour (TextButton::buttonColourId, Theme::raised);
        setColour (TextButton::buttonOnColourId, Theme::cyan);
        setColour (TextButton::textColourOffId, Theme::text);
        setColour (TextButton::textColourOnId, Theme::background);

        setColour (Label::textColourId, Theme::text);

        setColour (AlertWindow::backgroundColourId, Theme::panelAlt);
        setColour (AlertWindow::textColourId, Theme::text);
        setColour (AlertWindow::outlineColourId, Theme::border);

        setColour (TooltipWindow::backgroundColourId, Theme::raised);
        setColour (TooltipWindow::textColourId, Theme::text);
        setColour (TooltipWindow::outlineColourId, Theme::border);
    }

    //==========================================================================
    static juce::String styleOf (const juce::Button& b)
    {
        return b.getProperties()["auraStyle"].toString();
    }

    static juce::Colour accentOf (const juce::Button& b)
    {
        const auto& v = b.getProperties()["auraAccent"];
        return v.isVoid() ? Theme::cyan : juce::Colour ((juce::uint32) static_cast<juce::int64> (v));
    }

    void drawButtonBackground (juce::Graphics& g, juce::Button& button, const juce::Colour&,
                               bool isHighlighted, bool isDown) override
    {
        const auto bounds = button.getLocalBounds().toFloat().reduced (0.5f);
        const auto style  = styleOf (button);
        const float radius = 7.0f;

        juce::Colour fill    = Theme::raised;
        juce::Colour outline = Theme::border;

        if (style == "primary")
        {
            fill    = Theme::violet;
            outline = Theme::violet.brighter (0.25f);
        }
        else if (style == "danger")
        {
            fill    = Theme::danger.withMultipliedSaturation (0.92f);
            outline = Theme::danger.brighter (0.25f);
        }
        else if (style == "chip")
        {
            const auto accent = accentOf (button);
            const bool on     = button.getToggleState();
            fill    = on ? accent.withAlpha (0.16f) : juce::Colours::transparentBlack;
            outline = on ? accent.withAlpha (0.85f) : Theme::border;
        }
        else if (style == "ghost")
        {
            fill    = juce::Colours::transparentBlack;
            outline = Theme::border;
        }
        else if (style == "secondary")
        {
            // Bouton secondaire à fond uni, posé sur une carte #11131A
            fill    = juce::Colour (0xff1a1d27);
            outline = juce::Colour (0xff2c303d);
        }

        if (! button.isEnabled())
        {
            fill    = fill.withMultipliedAlpha (0.35f);
            outline = outline.withMultipliedAlpha (0.45f);
        }
        else if (isDown)
        {
            fill = fill.darker (0.15f);
        }
        else if (isHighlighted)
        {
            fill    = fill.isTransparent() ? Theme::raised.withAlpha (0.6f) : fill.brighter (0.10f);
            outline = outline.brighter (0.2f);
        }

        g.setColour (fill);
        g.fillRoundedRectangle (bounds, radius);
        g.setColour (outline);
        g.drawRoundedRectangle (bounds, radius, 1.0f);
    }

    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override
    {
        return Theme::font (juce::jmin (15.0f, (float) buttonHeight * 0.48f), true);
    }

    void drawButtonText (juce::Graphics& g, juce::TextButton& button, bool, bool) override
    {
        const auto style = styleOf (button);
        juce::Colour colour = Theme::text;

        if (style == "primary" || style == "danger")
            colour = juce::Colours::white;
        else if (style == "chip")
            colour = button.getToggleState() ? accentOf (button) : Theme::textDim;

        if (! button.isEnabled())
            colour = colour.withMultipliedAlpha (0.4f);

        g.setColour (colour);
        g.setFont (getTextButtonFont (button, button.getHeight()));
        g.drawFittedText (button.getButtonText(), button.getLocalBounds().reduced (8, 2),
                          juce::Justification::centred, 1, 0.9f);
    }

    //==========================================================================
    void fillTextEditorBackground (juce::Graphics& g, int width, int height, juce::TextEditor& editor) override
    {
        g.setColour (editor.findColour (juce::TextEditor::backgroundColourId));
        g.fillRoundedRectangle (juce::Rectangle<float> (0.0f, 0.0f, (float) width, (float) height), 8.0f);
    }

    void drawTextEditorOutline (juce::Graphics& g, int width, int height, juce::TextEditor& editor) override
    {
        const bool focused = editor.hasKeyboardFocus (true) && ! editor.isReadOnly();
        g.setColour (editor.findColour (focused ? juce::TextEditor::focusedOutlineColourId
                                                : juce::TextEditor::outlineColourId));
        g.drawRoundedRectangle (juce::Rectangle<float> (0.0f, 0.0f, (float) width, (float) height).reduced (0.75f),
                                8.0f, focused ? 1.5f : 1.0f);
    }

    //==========================================================================
    void drawTableHeaderBackground (juce::Graphics& g, juce::TableHeaderComponent& header) override
    {
        g.fillAll (Theme::panel);
        g.setColour (Theme::border);
        g.fillRect (0, header.getHeight() - 1, header.getWidth(), 1);
    }

    void drawTableHeaderColumn (juce::Graphics& g, juce::TableHeaderComponent&, const juce::String& columnName,
                                int /*columnId*/, int width, int height, bool isMouseOver, bool isMouseDown,
                                int columnFlags) override
    {
        if (isMouseOver || isMouseDown)
            g.fillAll (Theme::raised.withAlpha (isMouseDown ? 0.9f : 0.5f));

        auto area = juce::Rectangle<int> (width, height).reduced (10, 0);

        const bool sortedForward  = (columnFlags & juce::TableHeaderComponent::sortedForwards) != 0;
        const bool sortedBackward = (columnFlags & juce::TableHeaderComponent::sortedBackwards) != 0;

        if (sortedForward || sortedBackward)
        {
            const auto arrowArea = area.removeFromRight (12).toFloat().withSizeKeepingCentre (8.0f, 5.0f);
            juce::Path arrow;

            if (sortedForward)
                arrow.addTriangle (arrowArea.getX(), arrowArea.getY(), arrowArea.getRight(), arrowArea.getY(),
                                   arrowArea.getCentreX(), arrowArea.getBottom());
            else
                arrow.addTriangle (arrowArea.getX(), arrowArea.getBottom(), arrowArea.getRight(), arrowArea.getBottom(),
                                   arrowArea.getCentreX(), arrowArea.getY());

            g.setColour (Theme::cyan);
            g.fillPath (arrow);
        }

        g.setColour (sortedForward || sortedBackward ? Theme::text : Theme::textDim);
        g.setFont (Theme::font (12.0f, true));
        g.drawFittedText (Theme::upper (columnName), area, juce::Justification::centredLeft, 1);
    }

    //==========================================================================
    int getDefaultScrollbarWidth() override { return 10; }

    void drawScrollbar (juce::Graphics& g, juce::ScrollBar&, int x, int y, int width, int height, bool isVertical,
                        int thumbStart, int thumbSize, bool isMouseOver, bool isMouseDown) override
    {
        const auto thumb = isVertical ? juce::Rectangle<int> (x, thumbStart, width, thumbSize)
                                      : juce::Rectangle<int> (thumbStart, y, thumbSize, height);

        g.setColour (Theme::border.brighter (isMouseDown ? 0.5f : (isMouseOver ? 0.3f : 0.0f)));
        g.fillRoundedRectangle (thumb.reduced (2).toFloat(), 3.0f);
    }

    //==========================================================================
    juce::Font getPopupMenuFont() override               { return Theme::font (15.0f); }
    juce::Font getComboBoxFont (juce::ComboBox&) override { return Theme::font (14.5f); }
    juce::Font getAlertWindowMessageFont() override      { return Theme::font (15.0f); }
    juce::Font getAlertWindowTitleFont() override        { return Theme::font (18.0f, true); }
};

} // namespace aura
