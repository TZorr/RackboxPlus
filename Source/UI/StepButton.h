//
//  StepButton.h
//  Rackbox Plus
//
//  A flat button that steps through a list: a click goes to the next entry, a
//  right click to the previous one, and both wrap. Used for a reverb's pre-delay,
//  in the slot where the other blocks have Sync.
//
//  A list of nine values does not earn a row of nine buttons, and a knob would
//  hide the current one behind a pointer angle; one button that always says
//  what it is set to, and that the next click changes, is the smallest thing
//  that shows the value and lets you get to every other one.
//
//  It holds no value. The panel sets the text, whether it is lit (a value other
//  than the first) and the tooltip, and hears the click through onStep(+1 / -1).
//

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "Palette.h"

class StepButton : public juce::Component, public juce::SettableTooltipClient
{
public:
    std::function<void (int)> onStep;

    void setText (const juce::String& newText, bool isLit)
    {
        if (newText == text && isLit == lit)
            return;

        text = newText;
        lit = isLit;
        repaint();
    }

    const juce::String& getText() const noexcept    { return text; }

    /** The width the text needs, with 9 points either side. */
    int getIdealWidth() const
    {
        return (int) std::ceil (juce::GlyphArrangement::getStringWidth (font(), text.toUpperCase())) + 18 + 2;
    }

    void paint (juce::Graphics& g) override
    {
        // As SYNC beside it on other blocks: a dark field, orange while set.
        const auto bounds = getLocalBounds().toFloat().reduced (0.5f);

        g.setColour (hovered ? Palette::field.brighter (0.07f) : Palette::field);
        g.fillRoundedRectangle (bounds, 3.0f);
        g.setColour (lit ? Palette::accent : Palette::fieldLine);
        g.drawRoundedRectangle (bounds, 3.0f, 1.0f);

        g.setColour (lit ? Palette::accent : Palette::inkDim);
        g.setFont (font());
        g.drawFittedText (text.toUpperCase(), getLocalBounds().reduced (3, 0), juce::Justification::centred, 1, 0.7f);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (onStep)
            onStep (e.mods.isPopupMenu() ? -1 : 1);
    }

    void mouseEnter (const juce::MouseEvent&) override  { hovered = true;  repaint(); }
    void mouseExit  (const juce::MouseEvent&) override  { hovered = false; repaint(); }

private:
    static juce::Font font() { return Palette::font (10.5f, true, 0.06f); }

    juce::String text;
    bool lit = false, hovered = false;
};
