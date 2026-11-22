//
//  ChainDisplay.h
//  Rackbox Plus
//
//  The card under the header (since 0.8.0 the Kitbox-Look mockup): what is
//  in the chain, in the order the audio passes through it.
//
//      SIGNAL CHAIN            IN > LPF/HPF > [1 REVERB . PLATE] > [2 ...] > OUT
//      3 of 4 blocks active
//
//  On the left the title, the input filters' settings and how many blocks
//  are on; on the right the chain itself, one card per block in its family's
//  colour. A bypassed block's card is an empty dashed outline that says OFF.
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
    juce::String filterText() const;

    RackboxProcessor& processor;

    juce::String shownFamily[RackParams::numBlocks], shownEffect[RackParams::numBlocks], shownFilters;
    int shownCategory[RackParams::numBlocks] {};
    bool shownBypass[RackParams::numBlocks] {};
};
