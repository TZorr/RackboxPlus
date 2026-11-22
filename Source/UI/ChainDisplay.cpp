//
//  ChainDisplay.cpp
//  Rackbox Plus
//

#include "ChainDisplay.h"

#include "Engine/EffectCatalog.h"
#include "Palette.h"
#include "TextUtf8.h"

namespace
{
    juce::String hertz (float hz)
    {
        return hz >= 1000.0f ? juce::String (hz / 1000.0f, 1) + " kHz"
                             : juce::String (juce::roundToInt (hz)) + " Hz";
    }
}

juce::String ChainDisplay::filterText() const
{
    return "LPF " + hertz (processor.parameters.getRawParameterValue (RackParams::lowpass)->load())
         + utf8 (" \xc2\xb7 HPF ") + hertz (processor.parameters.getRawParameterValue (RackParams::highpass)->load());
}

void ChainDisplay::refresh()
{
    const auto filters = filterText();
    auto changed = filters != shownFilters;
    shownFilters = filters;

    for (int b = 0; b < RackParams::numBlocks; ++b)
    {
        const auto s = processor.getBlockSettings (b);
        const auto family = juce::String (EffectCatalog::categoryName (s.category)).toUpperCase();
        const auto effect = juce::String (EffectCatalog::info (s.category, s.subtype).name).toUpperCase();

        changed = changed || family != shownFamily[b] || effect != shownEffect[b] || s.bypass != shownBypass[b]
                          || s.category != shownCategory[b];
        shownFamily[b] = family;
        shownEffect[b] = effect;
        shownBypass[b] = s.bypass;
        shownCategory[b] = s.category;
    }

    if (changed)
        repaint();
}

void ChainDisplay::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    g.setColour (Palette::card);
    g.fillRoundedRectangle (bounds, 6.0f);
    g.setColour (Palette::line);
    g.drawRoundedRectangle (bounds.reduced (0.5f), 6.0f, 1.0f);

    auto area = bounds.reduced (16.0f, 14.0f);
    auto left = area.removeFromLeft (170.0f);
    area.removeFromLeft (14.0f);

    // Left: the title, the filters, how many blocks are on.
    int active = 0;
    for (const auto bypassed : shownBypass)
        active += bypassed ? 0 : 1;

    g.setColour (Palette::accent);
    g.setFont (Palette::font (20.0f, true, 0.02f));
    g.drawText ("SIGNAL CHAIN", left.removeFromTop (24.0f), juce::Justification::centredLeft);
    left.removeFromTop (8.0f);
    g.setColour (Palette::inkSoft);
    g.setFont (Palette::font (12.0f));
    g.drawFittedText (shownFilters, left.removeFromTop (15.0f).toNearestInt(), juce::Justification::centredLeft, 1, 0.8f);
    left.removeFromTop (2.0f);
    g.setColour (Palette::inkFaint);
    g.setFont (Palette::font (11.0f));
    g.drawText (juce::String (active) + " of " + juce::String (RackParams::numBlocks) + " blocks active",
                left.removeFromTop (14.0f), juce::Justification::centredLeft);

    // Right: the chain, in a well.
    g.setColour (Palette::wave);
    g.fillRoundedRectangle (area, 5.0f);
    g.setColour (Palette::waveLine);
    g.drawRoundedRectangle (area.reduced (0.5f), 5.0f, 1.0f);

    auto row = area.reduced (12.0f, 0.0f);
    const auto wordFont = Palette::font (11.0f, true, 0.06f);

    const auto word = [&] (const juce::String& text)
    {
        g.setColour (Palette::inkFaint);
        g.setFont (wordFont);
        const auto width = std::ceil (juce::GlyphArrangement::getStringWidth (wordFont, text)) + 1.0f;
        g.drawText (text, row.removeFromLeft (width), juce::Justification::centredLeft);
        row.removeFromLeft (6.0f);
    };

    const auto arrow = [&]
    {
        const auto box = row.removeFromLeft (14.0f).withSizeKeepingCentre (14.0f, 10.0f);
        juce::Path p;
        p.startNewSubPath (box.getX() + 1.0f, box.getCentreY());
        p.lineTo (box.getX() + 12.0f, box.getCentreY());
        p.startNewSubPath (box.getX() + 8.5f, box.getY() + 1.5f);
        p.lineTo (box.getX() + 12.0f, box.getCentreY());
        p.lineTo (box.getX() + 8.5f, box.getBottom() - 1.5f);
        g.setColour (Palette::arrow);
        g.strokePath (p, juce::PathStrokeType (1.3f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        row.removeFromLeft (6.0f);
    };

    word ("IN");
    arrow();
    word ("LPF/HPF");

    // What is left after the cards' arrows and OUT, shared by the four cards.
    const auto outWidth = std::ceil (juce::GlyphArrangement::getStringWidth (wordFont, "OUT")) + 1.0f;
    const auto cardWidth = (row.getWidth() - (float) (RackParams::numBlocks + 1) * (14.0f + 6.0f) - outWidth)
                         / (float) RackParams::numBlocks + 0.0f;

    for (int b = 0; b < RackParams::numBlocks; ++b)
    {
        arrow();
        auto card = row.removeFromLeft (cardWidth - 6.0f).withSizeKeepingCentre (cardWidth - 6.0f, 60.0f);
        row.removeFromLeft (6.0f);

        const auto on = ! shownBypass[b];
        const auto zone = Palette::zone (Palette::categoryZone (shownCategory[b]));
        const auto fade = on ? 1.0f : 0.55f;

        if (on)
        {
            g.setColour (zone.soft);
            g.fillRoundedRectangle (card, 4.0f);
            g.setColour (zone.track);
            g.drawRoundedRectangle (card.reduced (0.5f), 4.0f, 1.0f);
        }
        else
        {
            juce::Path outline, dashed;
            outline.addRoundedRectangle (card.reduced (0.5f), 4.0f);
            const float dashes[] { 3.0f, 3.0f };
            juce::PathStrokeType (1.0f).createDashedStroke (dashed, outline, dashes, 2);
            g.setColour (Palette::lightOff.withMultipliedAlpha (fade));
            g.fillPath (dashed);
        }

        auto text = card.reduced (10.0f, 0.0f).withSizeKeepingCentre (card.getWidth() - 20.0f, 15.0f + 3.0f + 15.0f);
        const auto ink = (on ? zone.label : Palette::inkFaint).withMultipliedAlpha (fade);

        // 1 OFF, with the dot.
        {
            auto line = text.removeFromTop (15.0f);
            g.setColour ((on ? zone.line : Palette::powerOffDot).withMultipliedAlpha (fade));
            g.fillRoundedRectangle (line.removeFromLeft (7.0f).withSizeKeepingCentre (7.0f, 7.0f), 3.0f);
            line.removeFromLeft (6.0f);
            const auto numberFont = Palette::font (11.0f, true);
            const auto number = juce::String (b + 1);
            g.setColour (ink);
            g.setFont (numberFont);
            g.drawText (number, line.removeFromLeft (std::ceil (juce::GlyphArrangement::getStringWidth (numberFont, number)) + 1.0f),
                        juce::Justification::centredLeft);

            if (! on)
            {
                line.removeFromLeft (6.0f);
                g.setColour (Palette::inkFaint.withMultipliedAlpha (fade));
                g.drawText ("OFF", line, juce::Justification::centredLeft);
            }
        }

        // The family and the effect on one line: with eight knobs a block,
        // the chain has less room than Rackbox's.
        text.removeFromTop (3.0f);
        g.setColour (ink);
        g.setFont (Palette::font (12.0f, true, 0.04f));
        g.drawFittedText (shownFamily[b] + utf8 (" \xc2\xb7 ") + shownEffect[b], text.removeFromTop (15.0f).toNearestInt(),
                          juce::Justification::centredLeft, 1, 0.7f);
    }

    arrow();
    word ("OUT");
}
