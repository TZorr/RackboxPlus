//
//  PluginEditor.cpp
//  Rackbox Plus
//

#include "PluginEditor.h"

#include "UI/Palette.h"
#include "TextUtf8.h"

namespace
{
    /** The panel's theme is the user's, not the session's: it lives in
        ~/Library/Application Support/Rackbox Plus/Rackbox Plus.settings, shared
        by every instance and every project, and never travels with a session. */
    std::unique_ptr<juce::PropertiesFile> openSettings()
    {
        juce::PropertiesFile::Options options;
        options.applicationName     = JucePlugin_Name;
        options.filenameSuffix      = ".settings";
        options.folderName          = JucePlugin_Name;
        options.osxLibrarySubFolder = "Application Support";
        options.getDefaultFile().getParentDirectory().createDirectory();
        return std::make_unique<juce::PropertiesFile> (options);
    }

    constexpr const char* darkModeKey = "darkMode";

    constexpr int windowWidth = 1280, windowHeight = 628;
    constexpr int margin = 15, gap = 10;
    constexpr int topRowHeight = 96, blockHeight = 108;
    constexpr int sectionPadding = 8, titleHeight = 20;
}

RackboxEditor::RackboxEditor (RackboxProcessor& p)
    : AudioProcessorEditor (&p), display (p)
{
    lowpass.attach (p.parameters, RackParams::lowpass);
    highpass.attach (p.parameters, RackParams::highpass);
    dryWet.attach (p.parameters, RackParams::dryWet);

    // The saved theme first, so the first paint is already the right one.
    Palette::setDark (openSettings()->getBoolValue (darkModeKey, false));
    shownDark = Palette::dark;
    darkButton.setToggleState (Palette::dark, juce::dontSendNotification);
    darkButton.setTooltip ("Dark panel on / off");
    darkButton.onClick = [this] { setDarkMode (darkButton.getToggleState()); };
    addAndMakeVisible (darkButton);

    addAndMakeVisible (display);
    addAndMakeVisible (lowpass);
    addAndMakeVisible (highpass);
    addAndMakeVisible (dryWet);

    for (int b = 0; b < RackParams::numBlocks; ++b)
    {
        panels[(size_t) b] = std::make_unique<BlockPanel> (p, b);
        addAndMakeVisible (*panels[(size_t) b]);
    }

    setSize (windowWidth, windowHeight);
    setResizable (false, false);
    startTimerHz (30);
}

RackboxEditor::~RackboxEditor()
{
    stopTimer();
}

void RackboxEditor::setDarkMode (bool shouldBeDark)
{
    Palette::setDark (shouldBeDark);

    if (auto settings = openSettings())
    {
        settings->setValue (darkModeKey, shouldBeDark);
        settings->saveIfNeeded();
    }

    shownDark = shouldBeDark;
    darkButton.setToggleState (shouldBeDark, juce::dontSendNotification);
    repaint();
}

void RackboxEditor::timerCallback()
{
    display.refresh();

    // Another Rackbox Plus window may have switched the theme: it is process-wide.
    if (shownDark != Palette::dark)
    {
        shownDark = Palette::dark;
        darkButton.setToggleState (shownDark, juce::dontSendNotification);
        repaint();
    }

    for (auto& panel : panels)
        panel->refresh();
}

void RackboxEditor::resized()
{
    auto area = getLocalBounds().reduced (margin);

    auto top = area.removeFromTop (topRowHeight);
    wordmarkArea = top.removeFromLeft (175);
    top.removeFromLeft (gap);

    // Under the wordmark and its subtitle, which end 66 points down.
    darkButton.setBounds (wordmarkArea.getX(), wordmarkArea.getY() + 70, 52, 17);

    outputArea = top.removeFromRight (110);
    top.removeFromRight (gap);
    inputArea = top.removeFromRight (150);
    top.removeFromRight (gap);
    display.setBounds (top);

    const auto placeKnobs = [] (juce::Rectangle<int> section, std::initializer_list<Knob*> knobs)
    {
        auto row = section.reduced (sectionPadding, 0);
        row.removeFromTop (titleHeight);
        const auto width = row.getWidth() / (int) knobs.size();

        for (auto* knob : knobs)
            knob->setBounds (row.removeFromLeft (width));
    };

    placeKnobs (inputArea, { &lowpass, &highpass });
    placeKnobs (outputArea, { &dryWet });

    area.removeFromTop (gap + 2);

    // The four blocks as rows, top to bottom in signal order.
    for (int b = 0; b < RackParams::numBlocks; ++b)
    {
        if (b > 0)
            area.removeFromTop (gap);

        panels[(size_t) b]->setBounds (area.removeFromTop (blockHeight));
    }

    area.removeFromTop (8);
    footerArea = area;
}

void RackboxEditor::paint (juce::Graphics& g)
{
    g.fillAll (Palette::body);

    // Wordmark, as on Kitbox: orange then ink.
    {
        auto area = wordmarkArea.toFloat();
        const auto font = juce::Font (juce::FontOptions (30.0f, juce::Font::bold)).withExtraKerningFactor (0.16f);
        g.setFont (font);

        const auto rackWidth = juce::GlyphArrangement::getStringWidth (font, "RACK");
        auto nameRow = area.removeFromTop (52.0f).withTrimmedTop (10.0f);

        g.setColour (Palette::accent);
        g.drawText ("RACK", nameRow, juce::Justification::centredLeft);
        g.setColour (Palette::ink);
        g.drawText ("BOX", nameRow.withTrimmedLeft (rackWidth + 1.0f), juce::Justification::centredLeft);

        // "PLUS" in the accent colour ahead of the line Rackbox has there.
        auto line = area.removeFromTop (14.0f);
        const auto small = Palette::label (9.5f);
        g.setFont (small);
        g.setColour (Palette::accent);
        g.drawText ("PLUS", line, juce::Justification::centredLeft);
        g.setColour (Palette::inkDim);
        g.drawText ("   4-SLOT MULTI-FX", line.withTrimmedLeft (juce::GlyphArrangement::getStringWidth (small, "PLUS")),
                    juce::Justification::centredLeft);
    }

    const auto drawSection = [&] (juce::Rectangle<int> bounds, const juce::String& title)
    {
        g.setColour (Palette::section);
        g.fillRoundedRectangle (bounds.toFloat(), 6.0f);

        g.setColour (Palette::ink);
        g.setFont (Palette::label (10.0f));
        g.drawText (title.toUpperCase(), bounds.reduced (sectionPadding + 2, 0).removeFromTop (titleHeight).toFloat().withTrimmedTop (3.0f),
                    juce::Justification::centredLeft);
    };

    drawSection (inputArea, "Input");
    drawSection (outputArea, "Output");

    g.setColour (Palette::inkDim);
    g.setFont (Palette::label (9.0f));
    g.drawText (utf8 ("RACKBOX PLUS " JucePlugin_VersionString "   \xc2\xb7   INPUT \xe2\x86\x92 LPF / HPF \xe2\x86\x92 BLOCK 1 \xe2\x86\x92 2 \xe2\x86\x92 3 \xe2\x86\x92 4"
                      " \xe2\x86\x92 DRY/WET   \xc2\xb7   TYPE PICKS THE FAMILY, SUBTYPE THE EFFECT   \xc2\xb7   BYPASS FADES, IT DOES NOT CUT"),
                footerArea, juce::Justification::topLeft);
}
