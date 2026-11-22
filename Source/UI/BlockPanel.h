//
//  BlockPanel.h
//  Rackbox Plus
//
//  One effect block's row of the panel (since 1.3.0 the "8 Regler pro Effekt"
//  mockup):
//
//      |B| REVERB . PLATE     [PRE OFF] | (o) (o) (o) (o) (o) (o) (o) (o) | (o) |
//      |L| [MOD][REVERB][DELAY][DIST]   | DECAY DAMP PRE-DELAY ...        | MIX |
//      |K| [PLATE ][ ROOM ][ HALL ]     |                                 |     |
//
//  Down the left edge the block's name on an upright strip, which is its
//  bypass: a click switches the block off and on, and the strip goes grey.
//  Then the loaded effect's name - with Sync at its right when the effect has
//  a time or rate knob that can follow the host tempo, or, on a reverb, a PRE
//  button in Sync's place that steps a tempo-synced pre-delay - over a row of
//  Type buttons and a row of Subtype buttons; up to eight knobs whose names
//  follow the effect, and the block's Mix in a darker column at the far
//  right. Everything takes the family's colour; a bypassed block's knobs fade
//  back.
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

    /** The first block rounds its top corners, the last its bottom ones: the
        four rows make one panel. */
    void setCorners (bool top, bool bottom) { roundTop = top; roundBottom = bottom; repaint(); }

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
    /** The upright strip down the left edge: the block's name and its bypass. */
    class Power : public juce::Button
    {
    public:
        explicit Power (int blockIndex);
        Palette::ZoneId zone = Palette::ZoneId::none;
        void paintButton (juce::Graphics&, bool highlighted, bool down) override;

    private:
        juce::String title;
    };

    void placeStep();

    RackboxProcessor& processor;
    int block;

    void setChoice (const char* suffix, int value);

    /** A click on a Type or Subtype button: the choice, and the new effect's knobs at their start. */
    void choose (int category, int subtype, const char* changedSuffix, int value);

    ChoiceButtons type, sub;
    std::array<std::unique_ptr<Knob>, RackParams::Block::numKnobs> knobs;
    Knob mix { "Mix" };
    Power bypass;
    FlatToggle sync { "Sync" };
    StepButton pre;   // a reverb's pre-delay, in Sync's place
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> bypassAttachment, syncAttachment;

    int shownCategory = -1, shownSubtype = -1;
    juce::String shownTip, shownName;
    bool shownBypass = false, shownSync = false;
    bool roundTop = false, roundBottom = false;
    juce::Rectangle<int> nameArea, knobArea, mixArea;
};
