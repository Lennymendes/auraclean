#pragma once
// ==============================================================================
//  AuraWidgets.h — AuraClean (NextDSP)
//  Petits composants réutilisables : bouton-icône (engrenage, fermer) et logo
//  cliquable. Couleurs unies uniquement, sauf via Theme::drawGradientText (sûr).
// ==============================================================================

#include <juce_gui_basics/juce_gui_basics.h>

#include "AuraLookAndFeel.h"

#include <cmath>

namespace aura
{
namespace Icons
{
    /** Engrenage plein (8 dents) avec trou central, en règle pair-impair. */
    inline juce::Path gear (juce::Rectangle<float> r)
    {
        juce::Path p;
        const auto  c      = r.getCentre();
        const float outer  = juce::jmin (r.getWidth(), r.getHeight()) * 0.5f;
        const float inner  = outer * 0.74f;
        const int   teeth  = 8;
        const float step   = juce::MathConstants<float>::twoPi / (float) teeth;

        auto point = [c] (float radius, float angle)
        {
            return juce::Point<float> (c.x + radius * std::sin (angle), c.y - radius * std::cos (angle));
        };

        for (int k = 0; k < teeth; ++k)
        {
            const float base = (float) k * step;
            const auto a = point (inner, base - 0.30f * step);
            const auto b = point (outer, base - 0.17f * step);
            const auto d = point (outer, base + 0.17f * step);
            const auto e = point (inner, base + 0.30f * step);

            if (k == 0) p.startNewSubPath (a);
            else        p.lineTo (a);

            p.lineTo (b);
            p.lineTo (d);
            p.lineTo (e);
        }

        p.closeSubPath();
        p.addEllipse (juce::Rectangle<float> (outer * 0.62f, outer * 0.62f).withCentre (c));
        p.setUsingNonZeroWinding (false);
        return p;
    }

    inline juce::Path cross (juce::Rectangle<float> r)
    {
        juce::Path p;
        p.startNewSubPath (r.getTopLeft());
        p.lineTo (r.getBottomRight());
        p.startNewSubPath (r.getTopRight());
        p.lineTo (r.getBottomLeft());
        return p;
    }

    inline juce::Path heart (juce::Rectangle<float> r)
    {
        juce::Path p;
        const float w = r.getWidth(), h = r.getHeight(), x = r.getX(), y = r.getY();
        p.startNewSubPath (x + w * 0.5f, y + h * 0.95f);
        p.cubicTo (x - w * 0.10f, y + h * 0.55f, x + w * 0.05f, y - h * 0.05f, x + w * 0.5f, y + h * 0.25f);
        p.cubicTo (x + w * 0.95f, y - h * 0.05f, x + w * 1.10f, y + h * 0.55f, x + w * 0.5f, y + h * 0.95f);
        p.closeSubPath();
        return p;
    }
} // namespace Icons

//==============================================================================
/** Bouton carré à icône vectorielle, fond discret au survol. */
class IconButton : public juce::Button
{
public:
    enum class Glyph { Gear, Close };

    explicit IconButton (Glyph g, const juce::String& tooltip = {}) : juce::Button ("icon"), glyph (g)
    {
        setTooltip (tooltip);
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
    }

    void paintButton (juce::Graphics& g, bool highlighted, bool down) override
    {
        const auto bounds = getLocalBounds().toFloat().reduced (0.5f);

        if (highlighted || down || getToggleState())
        {
            g.setColour (Theme::raised.withAlpha (down ? 1.0f : 0.8f));
            g.fillRoundedRectangle (bounds, 7.0f);
        }

        g.setColour (Theme::border);
        g.drawRoundedRectangle (bounds, 7.0f, 1.0f);

        const auto colour = highlighted || getToggleState() ? Theme::text : Theme::textDim;
        const auto iconArea = bounds.withSizeKeepingCentre (18.0f, 18.0f);
        g.setColour (colour.withMultipliedAlpha (isEnabled() ? 1.0f : 0.4f));

        if (glyph == Glyph::Gear)
            g.fillPath (Icons::gear (iconArea));
        else
            g.strokePath (Icons::cross (iconArea.reduced (3.0f)),
                          juce::PathStrokeType (1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

private:
    Glyph glyph;
};

//==============================================================================
/** Logo + titre « Aura Clean » de l'en-tête ; un clic ouvre le site. */
class LogoButton : public juce::Button
{
public:
    explicit LogoButton (juce::Image logoImage) : juce::Button ("logo"), logo (std::move (logoImage))
    {
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
    }

    void paintButton (juce::Graphics& g, bool highlighted, bool down) override
    {
        auto r = getLocalBounds().toFloat();

        if (highlighted || down)
        {
            g.setColour (Theme::raised.withAlpha (down ? 0.9f : 0.55f));
            g.fillRoundedRectangle (r, 9.0f);
        }

        r = r.reduced (6.0f, 4.0f);
        const auto iconArea = r.removeFromLeft (r.getHeight()).reduced (1.0f);

        g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
        g.drawImage (logo, iconArea, juce::RectanglePlacement::centred);

        r.removeFromLeft (10.0f);
        auto titleLine = r.removeFromTop (r.getHeight() * 0.62f);

        const auto titleFont = Theme::font (22.0f, true);
        const float auraW = juce::GlyphArrangement::getStringWidth (titleFont, "Aura ");

        g.setFont (titleFont);
        g.setColour (Theme::text);
        g.drawText ("Aura", titleLine, juce::Justification::bottomLeft, false);
        Theme::drawGradientText (g, "Clean", titleFont, titleLine.withTrimmedLeft (auraW),
                                 juce::Justification::bottomLeft, Theme::cyan, Theme::violet);

        g.setColour (Theme::textFaint);
        g.setFont (Theme::font (11.0f, true));
        g.drawText (juce::String::fromUTF8 ("NEXTDSP  ·  PLUGIN MANAGER"), r, juce::Justification::topLeft, false);
    }

private:
    juce::Image logo;
};

} // namespace aura
