//
//  PluginProcessor.h
//  Rackbox Plus
//
//  Input -> LPF/HPF -> block 1 -> block 2 -> block 3 -> dry/wet -> output.
//
//  The processor owns the parameters, the input filters and the three blocks,
//  and does nothing clever: read the parameters, run the chain, mix. Everything with a decision in it
//  lives in EffectBlock (the switching) or the effects themselves.
//
//  The input filters belong to the effect path only. "Dry" in the dry/wet knob is
//  the raw input, taken before the filters; only what goes into the chain - the
//  wet side - is filtered. So a high-pass keeps the bass out of the reverb and
//  the delays without thinning the direct sound, and a low-pass tames what the
//  effects are fed without dulling it. (It was the other way round at first, with
//  the dry signal taken after the filters; that made them tone controls for the
//  whole output, which is not what an effect's input filters are for.)
//
//  Channels: stereo in / stereo out, mono in / stereo out, mono in / mono out. The chain is
//  stereo inside in every case (see processBlock for how a mono buffer is expanded and
//  folded).
//
//  All state is the parameter tree. There is nothing else to save.
//

#pragma once

#include <array>
#include <atomic>

#include <juce_audio_processors/juce_audio_processors.h>

#include "Engine/EffectBlock.h"
#include "Engine/InputFilters.h"
#include "ParameterIds.h"

class RackboxProcessor : public juce::AudioProcessor
{
public:
    RackboxProcessor();
    ~RackboxProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 12.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    static juce::AudioProcessorValueTreeState::ParameterLayout makeParameterLayout();

    /** The current settings of a block, read from the parameters. */
    EffectBlock::Settings getBlockSettings (int block) const noexcept;

    /** The tempo and bar length the host last reported, for the tooltip on the pre-delay button. */
    double getHostBpm() const noexcept          { return hostBpm.load(); }
    double getHostBeatsPerBar() const noexcept  { return hostBeatsPerBar.load(); }

    juce::AudioProcessorValueTreeState parameters;

private:
    InputFilters filters;
    std::array<EffectBlock, RackParams::numBlocks> blocks;
    std::vector<float> dryLeft, dryRight;
    std::vector<float> monoRight;   // the right side of the chain when the host buffer is mono

    struct BlockPointers
    {
        std::atomic<float> *type = nullptr, *sub = nullptr, *bypass = nullptr, *sync = nullptr, *pre = nullptr, *mix = nullptr;
        std::array<std::atomic<float>*, RackParams::Block::numKnobs> knobs {};
    };

    std::array<BlockPointers, RackParams::numBlocks> blockParams {};
    std::atomic<float> *lowpass = nullptr, *highpass = nullptr, *dryWet = nullptr;

    std::atomic<double> hostBpm { 120.0 }, hostBeatsPerBar { 4.0 };

    juce::SmoothedValue<float> wetSmoothed { 1.0f };
    int maxBlock = 512;
    double sampleRate = 48000.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RackboxProcessor)
};
