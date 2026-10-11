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
        return hz >= 1000.0f ? juce::String (hz / 1000.0f, hz >= 10000.0f ? 0 : 1) + " kHz"
                             : juce::String (juce::roundToInt (hz)) + " Hz";
    }
}

juce::String ChainDisplay::chainText (int block) const
{
    const auto s = processor.getBlockSettings (block);
    return juce::String (block + 1) + "  " + juce::String (EffectCatalog::categoryName (s.category)).toUpperCase()
         + utf8 (" \xc2\xb7 ") + juce::String (EffectCatalog::info (s.category, s.subtype).name).toUpperCase();
}

juce::String ChainDisplay::filterText() const
{
    return "LPF " + hertz (processor.parameters.getRawParameterValue (RackParams::lowpass)->load())
         + utf8 (" \xc2\xb7 HPF ") + hertz (processor.parameters.getRawParameterValue (RackParams::highpass)->load());
}

void ChainDisplay::refresh()
{
    auto changed = false;

    const auto filters = filterText();
    changed = filters != shownFilters;
    shownFilters = filters;

    for (int b = 0; b < RackParams::numBlocks; ++b)
    {
        const auto text = chainText (b);
        const auto bypass = processor.getBlockSettings (b).bypass;

        changed = changed || text != shownChain[b] || bypass != shownBypass[b];
        shownChain[b] = text;
        shownBypass[b] = bypass;
    }

    if (changed)
        repaint();
}

void ChainDisplay::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();

    g.setColour (Palette::display);
    g.fillRoundedRectangle (bounds, 6.0f);

    auto inner = bounds.reduced (14.0f, 9.0f);

    auto titleRow = inner.removeFromTop (22.0f);
    g.setColour (Palette::displayText);
    g.setFont (Palette::mono (14.0f, true));
    g.drawText ("SIGNAL CHAIN", titleRow, juce::Justification::topLeft);

    g.setColour (Palette::displayDim);
    g.setFont (Palette::mono (10.5f));
    g.drawText (shownFilters, titleRow.withTrimmedTop (2.0f), juce::Justification::topRight);

    inner.removeFromTop (2.0f);

    // Entries 1 and 2 on the top line, 3 and 4 below, as the blocks are arranged.
    const auto rowHeight = inner.getHeight() / 2.0f;
    const auto columnWidth = inner.getWidth() / 2.0f;

    for (int b = 0; b < RackParams::numBlocks; ++b)
    {
        const auto cell = juce::Rectangle<float> (inner.getX() + (float) (b % 2) * columnWidth,
                                                  inner.getY() + (float) (b / 2) * rowHeight,
                                                  columnWidth - 8.0f, rowHeight);
        const auto bypassed = shownBypass[b];

        g.setColour (bypassed ? Palette::displayDim.withAlpha (0.5f) : Palette::blockColour (b));
        g.fillEllipse (cell.getX(), cell.getCentreY() - 3.0f, 6.0f, 6.0f);

        g.setColour (bypassed ? Palette::displayDim : Palette::displayText);
        g.setFont (Palette::mono (11.5f, ! bypassed));
        g.drawFittedText (shownChain[b] + (bypassed ? "  (off)" : ""), cell.withTrimmedLeft (14.0f).toNearestInt(),
                          juce::Justification::centredLeft, 1, 0.8f);
    }
}
