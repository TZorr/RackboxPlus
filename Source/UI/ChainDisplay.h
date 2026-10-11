//
//  ChainDisplay.h
//  Rackbox Plus
//
//  The central display, in Kitbox's near-black with orange type: what is in the
//  chain, in the order the audio passes through it.
//
//  Four entries in two columns - block number, category, effect - laid out the
//  way the blocks are below it, so entry 3 sits above block 3. A bypassed block
//  is dimmed and says so. The input filters' settings share the title line.
//
//  It once carried a scrolling waveform of the output as well; that went at
//  Thomas's request, and with it the queue the audio thread fed it through.
//  What remains reads only parameters, so refresh() costs nothing on the audio
//  side.
//
//  The editor's timer calls refresh(), which repaints only if a text changed.
//

#pragma once

#include "PluginProcessor.h"

class ChainDisplay : public juce::Component
{
public:
    explicit ChainDisplay (RackboxProcessor& processorToUse) : processor (processorToUse) {}

    void refresh();
    void paint (juce::Graphics&) override;

private:
    juce::String chainText (int block) const;
    juce::String filterText() const;

    RackboxProcessor& processor;

    juce::String shownChain[RackParams::numBlocks], shownFilters;
    bool shownBypass[RackParams::numBlocks] {};
};
