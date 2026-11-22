//
//  PluginEditor.h
//  Rackbox Plus
//
//  The panel, since 1.3.0 the "Rackbox Plus - 8 Regler pro Effekt" mockup
//  (Kitbox 0.10's "Variante B"):
//
//      RACKBOX PLUS  [<][>][ preset  .RACKBOXPLUS  v] LOAD SAVE   (o) Dry/Wet ||  [moon]
//      [ SIGNAL CHAIN | IN > LPF/HPF > [1] > [2] > [3] > [4] > OUT ]  |I| LOW  HIGH
//      +-----------------------------------------------------------------------+
//      | block 1: name, type, subtype | eight knobs                    | MIX  |
//      | block 2 ...                                                           |
//      +-----------------------------------------------------------------------+
//      RACKBOX PLUS 1.3.0 | hints                         Stereo in / out  Host 120 BPM
//
//  The blocks are the rows of one panel, split by hairlines, in the order
//  the audio passes through them, each in the colour of the effect family it
//  holds.
//
//  The panel is laid out once at the mockup's 1416 x 758 and scaled as a
//  whole: the window resizes freely at that aspect ratio, opens at 85 % the
//  first time, and remembers the size per user. Everything on it is drawn
//  from Palette and laid out from constants, so the render EditorShot makes
//  is the panel exactly as a host shows it.
//

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "PluginProcessor.h"
#include "UI/BlockPanel.h"
#include "UI/ChainDisplay.h"
#include "UI/Controls.h"
#include "UI/Knob.h"
#include "UI/PresetButton.h"

class RackboxEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit RackboxEditor (RackboxProcessor&);
    ~RackboxEditor() override;

    /** The panel's own size; the window shows it scaled. */
    static constexpr int panelWidth  = 1416;
    static constexpr int panelHeight = 758;
    static constexpr double defaultScale = 0.85, minScale = 0.6, maxScale = 1.4;

    void paint (juce::Graphics&) override;
    void resized() override;

    BlockPanel& getPanel (int block)    { return *panels[(size_t) block]; }

private:
    /** The 1416 x 758 panel: everything on it is its child, and it is drawn
        through the editor's paintCanvas. */
    struct Canvas : public juce::Component
    {
        explicit Canvas (RackboxEditor& ownerToUse) : owner (ownerToUse) { setOpaque (true); }
        void paint (juce::Graphics& g) override { owner.paintCanvas (g); }
        RackboxEditor& owner;
    };

    void timerCallback() override;
    void setDarkMode (bool shouldBeDark);
    void refreshThemeIcon();
    void paintCanvas (juce::Graphics&);
    void layoutCanvas();

    RackboxProcessor& rackbox;
    Canvas canvas { *this };
    juce::TooltipWindow tooltipWindow { this, 500 };   // the pre-delay button's milliseconds
    ChainDisplay display;
    std::array<std::unique_ptr<BlockPanel>, RackParams::numBlocks> panels;
    Knob lowpass { "Low-Pass" }, highpass { "High-Pass" };
    Knob dryWet { "Dry / Wet", false, Knob::Layout::beside };

    PresetButton presetButton;
    PanelButton prevPresetButton { {}, "M10 3.5 5.5 8l4.5 4.5" }, nextPresetButton { {}, "M6 3.5 10.5 8 6 12.5" };
    PanelButton loadPresetButton { "Load", "M2 4.5A1.5 1.5 0 0 1 3.5 3H6l1.5 1.5h5A1.5 1.5 0 0 1 14 6v5.5a1.5 1.5 0 0 1-1.5 1.5h-9A1.5 1.5 0 0 1 2 11.5z" };
    PanelButton savePresetButton { "Save", "M8 2.5v7.5M4.8 7 8 10.2 11.2 7M3 13.5h10" };
    PanelButton themeButton { {} };

    std::unique_ptr<juce::PropertiesFile> settings;
    bool shownDark = false;
    std::array<float, 2> meterLevels {};
    juce::String shownTempo;

    juce::Rectangle<int> headerArea, wordmarkArea, metersArea, inputArea, blocksArea, footerArea;
    std::array<juce::Rectangle<int>, 2> headerDividers;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RackboxEditor)
};
