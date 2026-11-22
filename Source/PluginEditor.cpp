//
//  PluginEditor.cpp
//  Rackbox Plus
//

#include "PluginEditor.h"

#include "UI/Palette.h"
#include "TextUtf8.h"

namespace
{
    /** The panel's theme and the window's size are the user's, not the
        session's: they live in ~/Library/Application Support/Rackbox Plus/
        Rackbox Plus.settings, shared by every instance and every project, and
        never travel with a session. */
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
    constexpr const char* scaleKey    = "panelScale";

    // The mockup's measures.
    constexpr int margin = 12;
    constexpr int headerHeight = 65;      // 64 and its hairline
    constexpr int footerHeight = 37;      // 36 and its hairline
    constexpr int topHeight = 120;
    constexpr int inputWidth = 200;
    constexpr int blockHeight = 128;
    constexpr int gap = 6;

    const juce::String moonIcon = "M13.2 9.6A5.6 5.6 0 1 1 6.4 2.8a4.5 4.5 0 0 0 6.8 6.8z";
    const juce::String sunIcon  = "M11 8a3 3 0 1 1-6 0a3 3 0 1 1 6 0M8 1.5v1.6M8 12.9v1.6M1.5 8h1.6M12.9 8h1.6"
                                  "M3.4 3.4l1.1 1.1M11.5 11.5l1.1 1.1M3.4 12.6l1.1-1.1M11.5 4.5l1.1-1.1";

    /** The meter's scale: -48 dB at the bottom, +3 at the top. */
    float meterFraction (float gain)
    {
        const auto db = juce::Decibels::gainToDecibels (gain, -100.0f);
        return juce::jlimit (0.0f, 1.0f, juce::jmap (db, -48.0f, 3.0f, 0.0f, 1.0f));
    }

    int textWidth (const juce::Font& font, const juce::String& text)
    {
        return (int) std::ceil (juce::GlyphArrangement::getStringWidth (font, text));
    }
}

RackboxEditor::RackboxEditor (RackboxProcessor& p)
    : AudioProcessorEditor (&p), rackbox (p), display (p), presetButton (p), settings (openSettings())
{
    addAndMakeVisible (canvas);

    lowpass.attach (p.parameters, RackParams::lowpass);
    highpass.attach (p.parameters, RackParams::highpass);
    dryWet.attach (p.parameters, RackParams::dryWet);

    for (auto* knob : { &lowpass, &highpass, &dryWet })
    {
        knob->setZone (Palette::ZoneId::neutral);
        canvas.addAndMakeVisible (knob);
    }

    lowpass.setDialSize (42);
    highpass.setDialSize (42);

    // The saved theme first, so the first paint is already the right one.
    // Dark is the mockup's, and so the default.
    Palette::setDark (settings->getBoolValue (darkModeKey, true));
    shownDark = Palette::dark;
    themeButton.onClick = [this] { setDarkMode (! Palette::dark); };
    refreshThemeIcon();
    canvas.addAndMakeVisible (themeButton);

    prevPresetButton.setTooltip ("Previous preset");
    nextPresetButton.setTooltip ("Next preset");
    loadPresetButton.setTooltip ("Load a preset file");
    savePresetButton.setTooltip ("Save the setting as a preset");
    prevPresetButton.onClick = [this] { presetButton.step (-1); };
    nextPresetButton.onClick = [this] { presetButton.step (+1); };
    loadPresetButton.onClick = [this] { presetButton.choose (false); };
    savePresetButton.onClick = [this] { presetButton.choose (true); };

    for (auto* button : { (juce::Button*) &prevPresetButton, (juce::Button*) &nextPresetButton, (juce::Button*) &presetButton,
                          (juce::Button*) &loadPresetButton, (juce::Button*) &savePresetButton })
        canvas.addAndMakeVisible (button);

    canvas.addAndMakeVisible (display);

    for (int b = 0; b < RackParams::numBlocks; ++b)
    {
        panels[(size_t) b] = std::make_unique<BlockPanel> (p, b);
        canvas.addAndMakeVisible (*panels[(size_t) b]);
    }

    canvas.setSize (panelWidth, panelHeight);
    layoutCanvas();

    // Any size at the panel's proportions; 85 % the first time.
    const auto scale = juce::jlimit (minScale, maxScale, settings->getDoubleValue (scaleKey, defaultScale));
    setResizable (true, true);
    setResizeLimits (juce::roundToInt (panelWidth * minScale), juce::roundToInt (panelHeight * minScale),
                     juce::roundToInt (panelWidth * maxScale), juce::roundToInt (panelHeight * maxScale));
    getConstrainer()->setFixedAspectRatio ((double) panelWidth / (double) panelHeight);
    setSize (juce::roundToInt (panelWidth * scale), juce::roundToInt (panelHeight * scale));

    startTimerHz (30);
}

RackboxEditor::~RackboxEditor()
{
    stopTimer();
}

void RackboxEditor::setDarkMode (bool shouldBeDark)
{
    Palette::setDark (shouldBeDark);
    settings->setValue (darkModeKey, shouldBeDark);
    settings->saveIfNeeded();

    shownDark = shouldBeDark;
    refreshThemeIcon();
    canvas.repaint();
}

void RackboxEditor::refreshThemeIcon()
{
    themeButton.setIcon (Palette::dark ? moonIcon : sunIcon);
    themeButton.setTooltip (Palette::dark ? "Switch to the light panel" : "Switch to the dark panel");
}

void RackboxEditor::timerCallback()
{
    display.refresh();

    // Another window may have switched the theme: it is process-wide.
    if (shownDark != Palette::dark)
    {
        shownDark = Palette::dark;
        refreshThemeIcon();
        canvas.repaint();
    }

    for (auto& panel : panels)
        panel->refresh();

    // The meter: peaks held since the last tick, falling back at about 20 dB a second.
    {
        bool moved = false;
        for (int c = 0; c < 2; ++c)
        {
            const auto peak = rackbox.takeMeterPeak (c);
            const auto next = juce::jmax (peak, meterLevels[(size_t) c] * 0.86f);
            const auto shown = next < 1.0e-4f ? 0.0f : next;
            moved = moved || std::abs (meterFraction (shown) - meterFraction (meterLevels[(size_t) c])) > 0.002f;
            meterLevels[(size_t) c] = shown;
        }
        if (moved)
            canvas.repaint (metersArea);
    }

    const auto bpm = rackbox.getHostBpm();
    const auto tempo = "Host " + juce::String (bpm, std::abs (bpm - std::round (bpm)) < 0.005 ? 0 : 1) + " BPM";
    if (tempo != shownTempo)
    {
        shownTempo = tempo;
        canvas.repaint (footerArea);
    }
}

void RackboxEditor::paint (juce::Graphics& g)
{
    // Only where the scaled panel leaves a sliver at the edge.
    g.fillAll (Palette::body);
}

void RackboxEditor::resized()
{
    const auto scale = (float) getWidth() / (float) panelWidth;
    canvas.setTransform (juce::AffineTransform::scale (scale));

    // Remembered once the window is up: the constructor's own setSize is the
    // saved size, not a choice.
    if (isTimerRunning() && std::abs (settings->getDoubleValue (scaleKey, defaultScale) - scale) > 0.001)
        settings->setValue (scaleKey, (double) scale);
}

void RackboxEditor::layoutCanvas()
{
    auto area = juce::Rectangle<int> (0, 0, panelWidth, panelHeight);

    // Header: from the left, the wordmark, the preset, Load / Save; from the
    // right, the theme, Dry/Wet and its meter.
    headerArea = area.removeFromTop (headerHeight);
    {
        auto row = headerArea.withTrimmedBottom (1).reduced (margin, 0);
        const auto buttonRow = [] (juce::Rectangle<int> r) { return r.withSizeKeepingCentre (r.getWidth(), 36); };

        wordmarkArea = row.removeFromLeft (textWidth (Palette::font (22.0f, true, 0.16f), "RACKBOX") + 10
                                           + textWidth (Palette::font (10.0f, true, 0.14f), "PLUS") + 12 + 14
                                           + textWidth (Palette::label (12.0f), utf8 ("4-SLOT MULTI-FX \xc2\xb7 8 KNOBS PER EFFECT")));
        row.removeFromLeft (20);
        headerDividers[0] = row.removeFromLeft (1).withSizeKeepingCentre (1, 28);
        row.removeFromLeft (20);

        prevPresetButton.setBounds (buttonRow (row.removeFromLeft (36)));
        row.removeFromLeft (6);
        nextPresetButton.setBounds (buttonRow (row.removeFromLeft (36)));
        row.removeFromLeft (6);
        presetButton.setBounds (buttonRow (row.removeFromLeft (280)));
        row.removeFromLeft (20);

        for (auto* button : { &loadPresetButton, &savePresetButton })
        {
            const auto width = 14 + 16 + 8 + textWidth (Palette::label (12.0f), button->getButtonText().toUpperCase()) + 14;
            button->setBounds (buttonRow (row.removeFromLeft (width)));
            row.removeFromLeft (6);
        }

        themeButton.setBounds (buttonRow (row.removeFromRight (36)));
        row.removeFromRight (20);
        headerDividers[1] = row.removeFromRight (1).withSizeKeepingCentre (1, 28);
        row.removeFromRight (20);

        metersArea = row.removeFromRight (13).withSizeKeepingCentre (13, 34);
        row.removeFromRight (10);
        dryWet.setBounds (row.removeFromRight (38 + 10 + 70).withSizeKeepingCentre (38 + 10 + 70, 38));
    }

    footerArea = area.removeFromBottom (footerHeight);

    auto body = area.reduced (margin, 0);
    body.removeFromTop (8);

    // The signal chain and the input filters.
    {
        auto top = body.removeFromTop (topHeight);
        inputArea = top.removeFromRight (inputWidth);
        top.removeFromRight (gap);
        display.setBounds (top);

        auto knobs = inputArea.reduced (1).withTrimmedLeft (6 + 26);
        const auto height = lowpass.getStackedHeight();
        const auto half = knobs.getWidth() / 2;
        lowpass.setBounds (knobs.removeFromLeft (half).withSizeKeepingCentre (half, height));
        highpass.setBounds (knobs.withSizeKeepingCentre (knobs.getWidth(), height));
    }

    body.removeFromTop (gap);

    // The blocks: the rows of one panel, a hairline round it and between them.
    blocksArea = body.removeFromTop (1 + RackParams::numBlocks * blockHeight + (RackParams::numBlocks - 1) + 1);
    {
        auto rows = blocksArea.reduced (1);

        for (int b = 0; b < RackParams::numBlocks; ++b)
        {
            panels[(size_t) b]->setBounds (rows.removeFromTop (blockHeight));
            panels[(size_t) b]->setCorners (b == 0, b == RackParams::numBlocks - 1);
            rows.removeFromTop (1);
        }
    }
}

void RackboxEditor::paintCanvas (juce::Graphics& g)
{
    g.fillAll (Palette::body);

    // Header.
    g.setColour (Palette::header);
    g.fillRect (headerArea);
    g.setColour (Palette::line);
    g.fillRect (headerArea.withTop (headerArea.getBottom() - 1));

    {
        // RACKBOX, a framed PLUS, then what it is.
        const auto font = Palette::font (22.0f, true, 0.16f);
        const auto rackWidth = textWidth (font, "RACK");
        const auto boxWidth = textWidth (font, "RACKBOX");
        auto area = wordmarkArea;
        g.setFont (font);
        g.setColour (Palette::accent);
        g.drawText ("RACK", area, juce::Justification::centredLeft);
        g.setColour (Palette::title);
        g.drawText ("BOX", area.withTrimmedLeft (rackWidth), juce::Justification::centredLeft);
        area.removeFromLeft (boxWidth + 10);

        const auto badgeFont = Palette::font (10.0f, true, 0.14f);
        const auto badge = area.removeFromLeft (textWidth (badgeFont, "PLUS") + 12).withSizeKeepingCentre (textWidth (badgeFont, "PLUS") + 12, 18);
        g.setColour (Palette::accent);
        g.drawRoundedRectangle (badge.toFloat().reduced (0.5f), 3.0f, 1.0f);
        g.setFont (badgeFont);
        g.drawText ("PLUS", badge, juce::Justification::centred);
        area.removeFromLeft (14);

        g.setColour (Palette::inkFaint);
        g.setFont (Palette::label (12.0f));
        g.drawText (utf8 ("4-SLOT MULTI-FX \xc2\xb7 8 KNOBS PER EFFECT"), area.translated (0, 2), juce::Justification::centredLeft);
    }

    // The blocks' frame: their hairline edge, and the hairlines between them.
    g.setColour (Palette::rackEdge);
    g.fillRoundedRectangle (blocksArea.toFloat(), 5.0f);

    g.setColour (Palette::divider);
    for (const auto& divider : headerDividers)
        g.fillRect (divider);

    // The meter beside Dry/Wet: two bars, left and right.
    for (int c = 0; c < 2; ++c)
    {
        const auto bar = juce::Rectangle<float> ((float) metersArea.getX() + (float) c * 8.0f, (float) metersArea.getY(),
                                                 5.0f, (float) metersArea.getHeight());
        g.setColour (Palette::meterTrack);
        g.fillRoundedRectangle (bar, 2.0f);

        const auto peak = meterLevels[(size_t) c];
        const auto fraction = meterFraction (peak);
        if (fraction > 0.0f)
        {
            juce::Graphics::ScopedSaveState state (g);
            juce::Path clip;
            clip.addRoundedRectangle (bar, 2.0f);
            g.reduceClipRegion (clip);
            g.setColour (peak >= 1.0f ? Palette::warning : peak >= 0.7f ? Palette::accent : Palette::signal);
            g.fillRect (bar.withTop (bar.getBottom() - bar.getHeight() * fraction));
        }
    }

    // The input section: a panel with INPUT down its left edge.
    {
        const auto bounds = inputArea.toFloat();
        g.setColour (Palette::rackEdge);
        g.fillRoundedRectangle (bounds, 5.0f);
        g.setColour (Palette::rackPanel);
        g.fillRoundedRectangle (bounds.reduced (1.0f), 4.0f);

        const auto column = bounds.withWidth (6.0f + 26.0f).withTrimmedLeft (6.0f);
        const auto font = Palette::font (11.0f, true, 0.14f);
        const auto length = juce::GlyphArrangement::getStringWidth (font, "INPUT");
        const auto centre = column.getCentre();

        juce::Graphics::ScopedSaveState rotated (g);
        g.addTransform (juce::AffineTransform::rotation (-juce::MathConstants<float>::halfPi, centre.x, centre.y));
        g.setColour (Palette::inkSoft);
        g.setFont (font);
        g.drawText ("INPUT", juce::Rectangle<float> (length + 4.0f, 14.0f).withCentre (centre), juce::Justification::centred, false);
    }

    // Status bar.
    {
        auto area = footerArea;
        g.setColour (Palette::footer);
        g.fillRect (area);
        g.setColour (Palette::line.interpolatedWith (Palette::footer, 0.2f));
        g.fillRect (area.removeFromTop (1));
        area = area.reduced (margin, 0);

        const auto font = Palette::font (11.0f, false, 0.06f);
        g.setFont (font);

        const auto version = juce::String ("RACKBOX PLUS " JucePlugin_VersionString);
        g.setColour (Palette::inkSoft);
        g.drawText (version, area.removeFromLeft (textWidth (font, version)), juce::Justification::centredLeft);
        area.removeFromLeft (16);
        g.setColour (Palette::divider);
        g.fillRect (area.removeFromLeft (1).withSizeKeepingCentre (1, 14));
        area.removeFromLeft (16);

        g.setColour (Palette::inkFaint);
        g.drawText (shownTempo, area.removeFromRight (textWidth (font, shownTempo) + 2), juce::Justification::centredRight);
        area.removeFromRight (16);
        const juce::String io ("Stereo in / out");
        g.drawText (io, area.removeFromRight (textWidth (font, io) + 2), juce::Justification::centredRight);
        area.removeFromRight (16);

        const auto hints = juce::StringArray { "Type picks the family, subtype the effect", "Choosing an effect resets its knobs to the catalog start",
                                               "Click a block's side strip to bypass", "Bypass fades, it does not cut" };
        g.drawText (hints.joinIntoString (utf8 ("   \xc2\xb7   ")), area, juce::Justification::centredLeft, true);
    }
}
