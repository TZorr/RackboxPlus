//
//  BlockPanel.cpp
//  Rackbox Plus
//

#include "BlockPanel.h"

#include "Engine/Dsp.h"
#include "Engine/EffectCatalog.h"
#include "Palette.h"
#include "TextUtf8.h"

namespace
{
    // The mockup's measures.
    constexpr int stripWidth = 26, stripInset = 6;
    constexpr int choiceColumn = 340;
    constexpr int nameHeight = 22, typeHeight = 28, subHeight = 26, choiceGap = 7;
    constexpr int mixWidth = 104;
    constexpr int knobDial = 44, mixDial = 48;

    /** The full names do not fit four across; the title and the display keep them. */
    juce::String shortName (int category)
    {
        static const char* names[] { "Mod", "Reverb", "Delay", "Dist" };
        return names[juce::jlimit (0, EffectCatalog::numCategories - 1, category)];
    }
}

void BlockPanel::setChoice (const char* suffix, int value)
{
    if (auto* parameter = processor.parameters.getParameter (RackParams::blockId (block, suffix)))
    {
        parameter->beginChangeGesture();
        parameter->setValueNotifyingHost (parameter->convertTo0to1 ((float) value));
        parameter->endChangeGesture();
    }
}

void BlockPanel::choose (int category, int subtype, const char* changedSuffix, int value)
{
    const auto before = processor.getBlockSettings (block);
    const auto was = EffectCatalog::index (before.category, before.subtype);
    const auto now = EffectCatalog::index (category, subtype);

    setChoice (changedSuffix, value);

    if (now == was)
        return;

    const auto& info = EffectCatalog::info (category, subtype);

    for (int k = 0; k < RackParams::Block::numKnobs; ++k)
        if (auto* parameter = processor.parameters.getParameter (RackParams::blockId (block, RackParams::Block::knobs[k])))
        {
            parameter->beginChangeGesture();
            parameter->setValueNotifyingHost (info.knobs[k].defaultValue);
            parameter->endChangeGesture();
        }
}

BlockPanel::Power::Power (int blockIndex)
    : juce::Button ("Block " + juce::String (blockIndex + 1)), title ("BLOCK " + juce::String (blockIndex + 1))
{
    setClickingTogglesState (true);
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

void BlockPanel::Power::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    // The toggle is the bypass: on means off.
    const auto bounds = getLocalBounds().toFloat().reduced (0.5f);
    const auto on = ! getToggleState();
    const auto colours = Palette::zone (zone);

    g.setColour (highlighted || down ? Palette::powerFace.brighter (0.07f) : Palette::powerFace);
    g.fillRoundedRectangle (bounds, 3.0f);
    g.setColour (Palette::powerLine);
    g.drawRoundedRectangle (bounds, 3.0f, 1.0f);

    // The dot over the title, the title read bottom to top; centred as a pair.
    const auto font = Palette::font (11.0f, true, 0.14f);
    const auto length = juce::GlyphArrangement::getStringWidth (font, title);
    const auto top = bounds.getCentreY() - (6.0f + 6.0f + length) * 0.5f;

    g.setColour (on ? colours.line : Palette::powerOffDot);
    g.fillEllipse (bounds.getCentreX() - 3.0f, top, 6.0f, 6.0f);

    const auto centre = juce::Point<float> (bounds.getCentreX(), top + 12.0f + length * 0.5f);
    juce::Graphics::ScopedSaveState state (g);
    g.addTransform (juce::AffineTransform::rotation (-juce::MathConstants<float>::halfPi, centre.x, centre.y));
    g.setColour (on ? colours.label : Palette::powerOffInk);
    g.setFont (font);
    g.drawText (title, juce::Rectangle<float> (length + 4.0f, 14.0f).withCentre (centre), juce::Justification::centred, false);
}

BlockPanel::BlockPanel (RackboxProcessor& processorToUse, int blockIndex)
    : processor (processorToUse), block (blockIndex), bypass (blockIndex)
{
    auto& state = processor.parameters;
    const auto id = [this] (const char* suffix) { return RackParams::blockId (block, suffix); };

    for (int k = 0; k < RackParams::Block::numKnobs; ++k)
    {
        knobs[(size_t) k] = std::make_unique<Knob> ("Knob " + juce::String (k + 1));
        knobs[(size_t) k]->setDialSize (knobDial);
        knobs[(size_t) k]->attach (state, id (RackParams::Block::knobs[k]));
    }

    mix.attach  (state, id (RackParams::Block::mix));

    bypassAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (state, id (RackParams::Block::bypass), bypass);

    syncAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (state, id (RackParams::Block::sync), sync);

    pre.onStep = [this] (int direction)
    {
        const auto current = processor.getBlockSettings (block).preDelay;
        setChoice (RackParams::Block::pre, (current + direction + Dsp::Sync::numPreDelays) % Dsp::Sync::numPreDelays);
    };

    juce::StringArray categories;
    for (int c = 0; c < EffectCatalog::numCategories; ++c)
        categories.add (shortName (c));

    type.setLabels (categories);
    type.zoneFor = [] (int category) { return Palette::categoryZone (category); };
    sub.setStyle (ChoiceButtons::Style::outlined);
    mix.setZone (Palette::ZoneId::neutral);
    mix.setDialSize (mixDial);
    type.onSelect = [this] (int i)
    {
        const auto stored = (int) processor.parameters.getRawParameterValue (RackParams::blockId (block, RackParams::Block::sub))->load();
        choose (i, EffectCatalog::clampSubtype (i, stored), RackParams::Block::type, i);
    };

    sub.onSelect = [this] (int i)
    {
        const auto s = processor.getBlockSettings (block);
        choose (s.category, EffectCatalog::clampSubtype (s.category, i), RackParams::Block::sub, i);
    };

    // Set after attach(): the attachment installs its own text functions.
    for (int k = 0; k < RackParams::Block::numKnobs; ++k)
    {
        knobs[(size_t) k]->textFromValueFunction = [this, k] (double v)
        {
            const auto s = processor.getBlockSettings (block);
            const auto& knob = EffectCatalog::info (s.category, s.subtype).knobs[k];

            // With Sync on, the first knob's time or rate reads as the note value it will play.
            const auto timing = k == 0 && (knob.unit == EffectCatalog::Unit::TimeMs || knob.unit == EffectCatalog::Unit::RateHz);
            return s.sync && timing ? EffectCatalog::format (EffectCatalog::Unit::Division, (float) v)
                                    : EffectCatalog::format (knob, (float) v);
        };

        addAndMakeVisible (*knobs[(size_t) k]);
    }

    for (auto* c : { (juce::Component*) &type, (juce::Component*) &sub, (juce::Component*) &mix,
                     (juce::Component*) &bypass, (juce::Component*) &sync, (juce::Component*) &pre })
        addAndMakeVisible (c);

    refresh();
}

void BlockPanel::refresh()
{
    const auto s = processor.getBlockSettings (block);

    if (s.category != shownCategory)
    {
        juce::StringArray names;
        for (int i = 0; i < EffectCatalog::numSubtypes (s.category); ++i)
            names.add (EffectCatalog::info (s.category, i).name);

        sub.setLabels (names);
    }

    type.setSelected (s.category);
    sub.setSelected (s.subtype);

    if (s.category != shownCategory)
    {
        const auto zone = Palette::categoryZone (s.category);
        sub.setZone (zone);
        for (auto& knob : knobs)
            knob->setZone (zone);
        bypass.zone = zone;
        bypass.repaint();
    }

    if (s.category != shownCategory || s.subtype != shownSubtype)
    {
        const auto& info = EffectCatalog::info (s.category, s.subtype);

        for (int k = 0; k < RackParams::Block::numKnobs; ++k)
        {
            auto& knob = *knobs[(size_t) k];
            const auto has = k < info.numKnobs();
            knob.setVisible (has);

            if (has)
            {
                // Signed values (a shift, a feedback that can go negative, a skew) draw from the centre.
                const auto unit = info.knobs[k].unit;
                knob.setBipolar (unit == EffectCatalog::Unit::ShiftHz || unit == EffectCatalog::Unit::SignedHz
                                 || unit == EffectCatalog::Unit::SignedPercent
                                 || (unit == EffectCatalog::Unit::Db && info.knobs[k].range.low < 0.0f
                                     && info.knobs[k].range.high > 0.0f));
                knob.setLabel (info.knobs[k].label);
                knob.repaint();
            }
        }

        shownName = juce::String (EffectCatalog::categoryName (s.category)) + utf8 (" \xc2\xb7 ") + info.name;
        shownCategory = s.category;
        shownSubtype = s.subtype;
        repaint();
    }

    // Sync exists only where the loaded effect has a time or a rate to follow. While it is on, the
    // knob steps through the note values instead of sliding.
    const auto syncing = s.sync && EffectCatalog::canSync (s.category, s.subtype);
    sync.setVisible (EffectCatalog::canSync (s.category, s.subtype));

    // A reverb has a pre-delay where the others have Sync. The tooltip carries the milliseconds
    // the note value comes to at the host's tempo: the one number the button itself cannot show.
    const auto hasPre = EffectCatalog::hasPreDelay (s.category);
    pre.setVisible (hasPre);

    if (hasPre)
    {
        pre.setText ("PRE " + juce::String (Dsp::Sync::preDelayNames[s.preDelay]), s.preDelay > 0);

        const auto bpm = processor.getHostBpm();
        const auto ms = Dsp::Sync::preDelaySeconds (s.preDelay, bpm, processor.getHostBeatsPerBar()) * 1000.0f;
        const auto tempo = juce::String (bpm, std::abs (bpm - std::round (bpm)) < 0.005 ? 0 : 1);
        const auto tip = s.preDelay == 0 ? juce::String ("Pre-delay off. Click for 1/128 up to one bar, right-click to go back.")
                                         : "Pre-delay " + juce::String (Dsp::Sync::preDelayNames[s.preDelay]).toLowerCase()
                                           + " = " + juce::String (ms, 1) + " ms at " + tempo + " BPM";

        if (tip != shownTip)
        {
            shownTip = tip;
            pre.setTooltip (tip);
        }
    }

    if (syncing != shownSync)
    {
        shownSync = syncing;
        knobs[0]->setRange (0.0, 1.0, syncing ? 1.0 / (Dsp::Sync::numDivisions - 1) : 0.0);
        knobs[0]->repaint();
    }

    if (s.bypass != shownBypass)
    {
        // A bypassed block's knobs fade back; they still turn.
        shownBypass = s.bypass;

        for (auto& knob : knobs)
            knob->setAlpha (s.bypass ? 0.4f : 1.0f);
        mix.setAlpha (s.bypass ? 0.4f : 1.0f);

        repaint();
    }

    placeStep();
}

void BlockPanel::placeStep()
{
    // SYNC and PRE at the right of the effect's name, as wide as their word.
    const auto slot = [this] (int width)
    {
        return juce::Rectangle<int> (nameArea.getRight() - width, nameArea.getCentreY() - 11, width, 22);
    };

    sync.setBounds (slot (sync.getIdealWidth()));
    pre.setBounds (slot (pre.getIdealWidth()));
}

void BlockPanel::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    constexpr float radius = 4.0f;

    juce::Path shape;
    shape.addRoundedRectangle (bounds.getX(), bounds.getY(), bounds.getWidth(), bounds.getHeight(), radius, radius,
                               roundTop, roundTop, roundBottom, roundBottom);
    g.setColour (Palette::rackPanel);
    g.fillPath (shape);

    // Mix's column, a step darker, and the uprights before the knobs and Mix.
    {
        juce::Graphics::ScopedSaveState state (g);
        g.reduceClipRegion (shape);
        g.setColour (Palette::mixColumn);
        g.fillRect (mixArea);
    }

    g.setColour (Palette::rackDivider);
    g.fillRect (knobArea.getX() - 1, 0, 1, getHeight());
    g.fillRect (mixArea.getX() - 1, 0, 1, getHeight());

    // The effect's name.
    const auto zone = Palette::zone (Palette::categoryZone (shownCategory));
    auto name = nameArea;
    if (sync.isVisible())
        name.setRight (sync.getX() - 8);
    if (pre.isVisible())
        name.setRight (pre.getX() - 8);

    g.setColour (shownBypass ? Palette::powerOffInk : zone.label);
    g.setFont (Palette::font (11.0f, true, 0.06f));
    g.drawFittedText (shownName.toUpperCase(), name, juce::Justification::centredLeft, 1, 0.8f);
}

void BlockPanel::resized()
{
    auto area = getLocalBounds();

    // The strip down the left edge.
    bypass.setBounds (area.removeFromLeft (stripInset + stripWidth).withTrimmedLeft (stripInset).reduced (0, 8));

    // The name and the buttons, 12 in on the left and 14 on the right, centred upright.
    auto choices = area.removeFromLeft (choiceColumn).withTrimmedLeft (12).withTrimmedRight (14);
    choices = choices.withSizeKeepingCentre (choices.getWidth(), nameHeight + choiceGap + typeHeight + choiceGap + subHeight);
    nameArea = choices.removeFromTop (nameHeight);
    choices.removeFromTop (choiceGap);
    type.setBounds (choices.removeFromTop (typeHeight));
    choices.removeFromTop (choiceGap);
    sub.setBounds (choices);

    // Mix's column at the right, after a 1-point upright; the eight knobs between.
    mixArea = area.removeFromRight (mixWidth);
    area.removeFromRight (1);
    area.removeFromLeft (1);
    knobArea = area;

    mix.setBounds (mixArea.withSizeKeepingCentre (mixArea.getWidth(), mix.getStackedHeight()));

    auto grid = area.reduced (4, 0);
    const auto height = knobs[0]->getStackedHeight();
    const auto cell = (float) grid.getWidth() / (float) RackParams::Block::numKnobs;
    const auto y = grid.getCentreY() - height / 2;

    for (int k = 0; k < RackParams::Block::numKnobs; ++k)
    {
        const auto x = grid.getX() + juce::roundToInt (cell * (float) k);
        const auto w = grid.getX() + juce::roundToInt (cell * (float) (k + 1)) - x;
        knobs[(size_t) k]->setBounds (x, y, w, height);
    }

    placeStep();
}
