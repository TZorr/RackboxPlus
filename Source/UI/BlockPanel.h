//
//  BlockPanel.h
//  Rackbox Plus
//
//  One effect block's row of the panel: a row of Type buttons over a row of
//  Subtype buttons on the left, up to eight knobs whose names follow the effect
//  that is loaded and the block's Mix at the far right, and Bypass in the title
//  row - with Sync beside it when the effect has a time or rate knob that can
//  follow the host tempo - or, on a reverb, a PRE button in Sync's place that
//  steps a tempo-synced pre-delay.
//
//  Three things here are not plain wiring.
//
//  The eight knobs are the same eight parameters whatever the effect, so their
//  names and the way their values are printed ("0.40 Hz", "250 ms", "x8.0") are
//  taken from the catalog every time the loaded effect changes, and the knobs
//  the effect does not have are hidden. Mix keeps its place at the right, so
//  it lines up from one block to the next.
//
//  Choosing another effect on the panel puts its knobs where the catalog says
//  they start: the third knob of a reverb means something else than the third
//  knob of a chorus, and carrying a position over would start the new effect
//  somewhere arbitrary. A host or a preset that changes the Type does not do
//  this - only a click does - so automation is never rewritten behind its back.
//
//  The Subtype row shows the *current category's* list. Its parameter always
//  runs 0-3, but a category with three entries shows three buttons, not four
//  with one dead. The stored value is never rewritten; the block clamps it on
//  its own side, and the row lights the clamped one (a stored 3 in Reverb lights
//  Hall). The buttons write their parameter as a change gesture, so a host
//  records a click as automation, and refresh() reads the parameter back, so a
//  host or a preset that changes it moves the lit button.
//

#pragma once

#include "ChoiceButtons.h"
#include "Knob.h"
#include "FlatToggle.h"
#include "StepButton.h"
#include "PluginProcessor.h"

class BlockPanel : public juce::Component
{
public:
    BlockPanel (RackboxProcessor& processorToUse, int blockIndex);

    /** Follows the parameters: names, texts and the subtype range. Call from the UI timer. */
    void refresh();

    void paint (juce::Graphics&) override;
    void resized() override;

    /** What the knobs currently print; the checks read these instead of looking at pixels. */
    juce::String subtypeText()    { return sub.getSelectedText(); }
    juce::String knobText (int k)   { return knobs[(size_t) k]->getTextFromValue (knobs[(size_t) k]->getValue()); }
    bool knobVisible (int k) const  { return knobs[(size_t) k]->isVisible(); }
    juce::String labelAText()       { return knobText (0); }
    juce::String labelBText()       { return knobText (1); }
    /** What a click on the n-th Type or Subtype button does. */
    void clickType (int index)      { type.onSelect (index); }
    void clickSubtype (int index)   { sub.onSelect (index); }

    bool syncVisible() const            { return sync.isVisible(); }
    bool preVisible() const             { return pre.isVisible(); }
    juce::String preText() const        { return pre.getText(); }
    juce::String preTooltip()           { return pre.getTooltip(); }
    void clickPre (int direction)       { pre.onStep (direction); }
    juce::String bypassText()     { return bypass.getToggleState() ? "bypassed" : "active"; }

private:
    RackboxProcessor& processor;
    int block;

    void setChoice (const char* suffix, int value);

    /** A click on a Type or Subtype button: the choice, and the new effect's knobs at their start. */
    void choose (int category, int subtype, const char* changedSuffix, int value);

    ChoiceButtons type, sub;
    std::array<std::unique_ptr<Knob>, RackParams::Block::numKnobs> knobs;
    Knob mix { "Mix" };
    FlatToggle bypass { "Bypass" }, sync { "Sync" };
    StepButton pre;   // a reverb's pre-delay, in Sync's place
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> bypassAttachment, syncAttachment;

    int shownCategory = -1, shownSubtype = -1;
    juce::String shownTip;
    bool shownBypass = false, shownSync = false;
};
