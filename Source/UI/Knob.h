//
//  Knob.h
//  Rackbox Plus (Kitbox 0.10's knob)
//
//  A rotary knob with its name and its value printed with it - Kitbox 0.10's
//  knob: a light metal cap inside a ring in the colour of the knob's zone, a
//  dot where the value starts from, a dark pointer on the cap.
//
//  Two layouts: stacked - the value over the knob and the name under it, as
//  in the blocks (44 points, Mix 48) - and beside, a 38-point knob with name and
//  value to its right, as Dry/Wet in the header.
//
//  A stacked knob lights its value while the mouse is over it or turns it,
//  and for a second after. A change the host makes - automation - leaves it
//  dim, so a playing automation lane does not set every value flickering.
//
//  Bipolar knobs - a frequency shift, a delay's offset - draw their arc from
//  twelve o'clock, so "nothing" looks like nothing. Double-click returns a
//  knob to its default.
//

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "Palette.h"

class Knob : public juce::Slider,
             private juce::Timer
{
public:
    enum class Layout { stacked, beside };

    Knob (const juce::String& labelText, bool isBipolar = false, Layout layoutToUse = Layout::stacked);
    ~Knob() override;

    /** Binds to a parameter, replacing any previous binding. */
    void attach (juce::AudioProcessorValueTreeState& state, const juce::String& parameterId);

    /** The name printed with the knob; an effect's A and B are renamed by the effect loaded. */
    void setLabel (const juce::String& newLabel);

    /** The colour zone the knob sits in: its ring takes the zone's colours. */
    void setZone (Palette::ZoneId zoneToUse);

    void paint (juce::Graphics&) override;
    void valueChanged() override;
    void mouseEnter (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

    /** Whether a stacked knob lights its value. */
    bool isShowingValue() const { return isMouseOverOrDragging() || isTimerRunning(); }

    /** Signed values draw from twelve o'clock: set by the effect loaded. */
    void setBipolar (bool isBipolar)    { if (bipolar != isBipolar) { bipolar = isBipolar; repaint(); } }

    /** A stacked knob's dial, 46 points unless set. */
    void setDialSize (int points) { dialSize = points; repaint(); }

    /** Height of a stacked knob: value 14, 1, the dial, 1, name 14. */
    static constexpr int stackedHeightFor (int dial) { return 14 + 1 + dial + 1 + 14; }
    int getStackedHeight() const noexcept { return stackedHeightFor (dialSize); }

    /** How long the value stays after the mouse has left or let go. */
    static constexpr int valueHoldMs = 1000;

    /** Draws a dial of the mockup's geometry - 52 units, ring radius 22 -
        scaled into `area`. `big` is the header's: a heavier cap without the
        highlight and without the origin dot. */
    static void drawDial (juce::Graphics&, juce::Rectangle<float> area, float proportion, bool bipolar,
                          bool hover, juce::Colour value, juce::Colour track, bool big = false);

private:
    void timerCallback() override;
    void holdValue();

    juce::String label;
    bool bipolar;
    Layout layout;
    Palette::ZoneId zone = Palette::ZoneId::none;
    int dialSize = 46;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
};
