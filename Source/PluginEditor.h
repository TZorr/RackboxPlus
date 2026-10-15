//
//  PluginEditor.h
//  Rackbox Plus
//
//  Kitbox's panel, rearranged for a chain: the wordmark, the display, the
//  input section and the output section across the top; the four blocks below
//  it in two rows of two, in reading order - block 1 top left, block 4 bottom right.
//
//  Fixed size. Everything on it is drawn from Palette and laid out from
//  constants, so the render EditorShot makes is the panel exactly as a host
//  shows it.
//

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "PluginProcessor.h"
#include "UI/BlockPanel.h"
#include "UI/Knob.h"
#include "UI/ChainDisplay.h"
#include "UI/FlatToggle.h"
#include "UI/PresetButton.h"

class RackboxEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit RackboxEditor (RackboxProcessor&);
    ~RackboxEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    BlockPanel& getPanel (int block)    { return *panels[(size_t) block]; }

private:
    void timerCallback() override;
    void setDarkMode (bool shouldBeDark);

    juce::TooltipWindow tooltipWindow { this, 500 };   // the pre-delay button's milliseconds
    ChainDisplay display;
    std::array<std::unique_ptr<BlockPanel>, RackParams::numBlocks> panels;
    Knob lowpass { "Low-Pass" }, highpass { "High-Pass" }, dryWet { "Dry/Wet" };
    FlatToggle darkButton { "Dark" };
    PresetButton presetButton;
    bool shownDark = false;

    juce::Rectangle<int> wordmarkArea, inputArea, outputArea, footerArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RackboxEditor)
};
