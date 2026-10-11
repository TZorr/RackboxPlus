//
//  PluginProcessor.cpp
//  Rackbox Plus
//

#include "PluginProcessor.h"

#include "Engine/EffectCatalog.h"
#include "PluginEditor.h"

namespace
{
    juce::String hertzText (float hz)
    {
        return hz >= 1000.0f ? juce::String (hz / 1000.0f, 2) + " kHz"
                             : juce::String (juce::roundToInt (hz)) + " Hz";
    }

    juce::String percentText (float x)  { return juce::String (juce::roundToInt (100.0f * x)) + " %"; }

}

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout RackboxProcessor::makeParameterLayout()
{
    using namespace juce;
    using namespace RackParams;

    std::vector<std::unique_ptr<RangedAudioParameter>> layout;

    const auto cutoff = [] (float defaultHz)
    {
        NormalisableRange<float> range (20.0f, 20000.0f);
        range.setSkewForCentre (1000.0f);
        return std::make_pair (range, defaultHz);
    };

    const auto frequencyAttributes = AudioParameterFloatAttributes()
        .withStringFromValueFunction ([] (float v, int) { return hertzText (v); })
        .withValueFromStringFunction ([] (const String& text)
        {
            const auto value = text.getFloatValue();
            return text.containsIgnoreCase ("k") ? value * 1000.0f : value;
        });

    const auto percentAttributes = AudioParameterFloatAttributes()
        .withStringFromValueFunction ([] (float v, int) { return percentText (v); })
        .withValueFromStringFunction ([] (const String& text) { return text.getFloatValue() / 100.0f; });

    {
        const auto [range, def] = cutoff (20000.0f);
        layout.push_back (std::make_unique<AudioParameterFloat> (ParameterID { RackParams::lowpass, stateVersion }, "Low-Pass", range, def, frequencyAttributes));
    }
    {
        const auto [range, def] = cutoff (20.0f);
        layout.push_back (std::make_unique<AudioParameterFloat> (ParameterID { RackParams::highpass, stateVersion }, "High-Pass", range, def, frequencyAttributes));
    }

    layout.push_back (std::make_unique<AudioParameterFloat> (ParameterID { RackParams::dryWet, stateVersion }, "Dry/Wet",
                                                             NormalisableRange<float> (0.0f, 1.0f), 1.0f, percentAttributes));

    StringArray categories;
    for (int c = 0; c < EffectCatalog::numCategories; ++c)
        categories.add (EffectCatalog::categoryName (c));

    // The default chain is the one in the brief: Reverb Plate, Modulation Chorus, Delay Tape.
    // The fourth block was added later and starts bypassed, so the chain sounds as it did
    // before it existed (and so does a session saved without it).
    constexpr int defaultCategory[]   { 1, 0, 2, 3 };
    constexpr int defaultSubtype[]    { 0, 0, 1, 0 };
    constexpr float defaultMix[]      { 0.35f, 0.5f, 0.4f, 0.5f };
    constexpr bool defaultBypass[]    { false, false, false, true };

    for (int b = 0; b < numBlocks; ++b)
    {
        const auto name = "Block " + String (b + 1) + " ";

        layout.push_back (std::make_unique<AudioParameterChoice> (ParameterID { blockId (b, Block::type), stateVersion },
                                                                  name + "Type", categories, defaultCategory[b]));
        layout.push_back (std::make_unique<AudioParameterInt> (ParameterID { blockId (b, Block::sub), stateVersion },
                                                               name + "Subtype", 0, EffectCatalog::maxSubtypes - 1, defaultSubtype[b]));
        layout.push_back (std::make_unique<AudioParameterBool> (ParameterID { blockId (b, Block::bypass), stateVersion },
                                                                name + "Bypass", defaultBypass[b]));
        layout.push_back (std::make_unique<AudioParameterBool> (ParameterID { blockId (b, Block::sync), stateVersion },
                                                                name + "Sync", false));
        layout.push_back (std::make_unique<AudioParameterInt> (ParameterID { blockId (b, Block::pre), stateVersion },
                                                               name + "Pre-Delay", 0, Dsp::Sync::numPreDelays - 1, 0,
                                                               AudioParameterIntAttributes().withStringFromValueFunction ([] (int v, int)
                                                               {
                                                                   return String (Dsp::Sync::preDelayNames[jlimit (0, Dsp::Sync::numPreDelays - 1, v)]);
                                                               })));
        // Each knob starts where the block's default effect wants it.
        const auto& startEffect = EffectCatalog::info (defaultCategory[b], defaultSubtype[b]);

        for (int k = 0; k < Block::numKnobs; ++k)
            layout.push_back (std::make_unique<AudioParameterFloat> (ParameterID { blockId (b, Block::knobs[k]), stateVersion },
                                                                     name + "Knob " + String (k + 1), NormalisableRange<float> (0.0f, 1.0f),
                                                                     startEffect.knobs[k].defaultValue, percentAttributes));
        layout.push_back (std::make_unique<AudioParameterFloat> (ParameterID { blockId (b, Block::mix), stateVersion },
                                                                 name + "Mix", NormalisableRange<float> (0.0f, 1.0f), defaultMix[b], percentAttributes));
    }

    return { layout.begin(), layout.end() };
}

//==============================================================================
RackboxProcessor::RackboxProcessor()
    : AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      parameters (*this, nullptr, RackParams::stateTreeType, makeParameterLayout())
{
    lowpass  = parameters.getRawParameterValue (RackParams::lowpass);
    highpass = parameters.getRawParameterValue (RackParams::highpass);
    dryWet   = parameters.getRawParameterValue (RackParams::dryWet);

    for (int b = 0; b < RackParams::numBlocks; ++b)
    {
        const auto get = [&] (const char* suffix) { return parameters.getRawParameterValue (RackParams::blockId (b, suffix)); };
        auto& p = blockParams[(size_t) b];
        p.type = get (RackParams::Block::type);
        p.sub = get (RackParams::Block::sub);
        p.bypass = get (RackParams::Block::bypass);
        p.sync = get (RackParams::Block::sync);
        p.pre = get (RackParams::Block::pre);
        p.mix = get (RackParams::Block::mix);

        for (int k = 0; k < RackParams::Block::numKnobs; ++k)
            p.knobs[(size_t) k] = get (RackParams::Block::knobs[k]);
    }
}

//  Three layouts: stereo -> stereo, mono -> stereo (a mono source through effects that
//  widen it, which is what a mono aux send to a reverb is for) and mono -> mono.
//  Logic decides whether to list a plugin on a mono channel strip by asking for exactly
//  these pairs; stereo in with mono out has no sensible meaning and is refused.
bool RackboxProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in = layouts.getMainInputChannelSet();
    const auto out = layouts.getMainOutputChannelSet();

    if (out == juce::AudioChannelSet::stereo())
        return in == juce::AudioChannelSet::stereo() || in == juce::AudioChannelSet::mono();

    return out == juce::AudioChannelSet::mono() && in == juce::AudioChannelSet::mono();
}

void RackboxProcessor::prepareToPlay (double newSampleRate, int samplesPerBlock)
{
    sampleRate = newSampleRate;
    maxBlock = juce::jmax (1, samplesPerBlock);

    filters.prepare (sampleRate);

    for (auto& block : blocks)
        block.prepare (sampleRate, maxBlock);

    dryLeft.assign ((size_t) maxBlock, 0.0f);
    dryRight.assign ((size_t) maxBlock, 0.0f);
    monoRight.assign ((size_t) maxBlock, 0.0f);

    wetSmoothed.reset (sampleRate, 0.02);
    wetSmoothed.setCurrentAndTargetValue (dryWet->load());

    setLatencySamples (0);
}

EffectBlock::Settings RackboxProcessor::getBlockSettings (int block) const noexcept
{
    const auto& p = blockParams[(size_t) block];

    EffectBlock::Settings s;
    s.category = juce::jlimit (0, EffectCatalog::numCategories - 1, (int) p.type->load());
    s.subtype  = EffectCatalog::clampSubtype (s.category, (int) p.sub->load());
    s.bypass   = p.bypass->load() >= 0.5f;
    s.sync     = p.sync->load() >= 0.5f;
    s.preDelay = juce::jlimit (0, Dsp::Sync::numPreDelays - 1, (int) p.pre->load());
    for (size_t k = 0; k < s.knobs.size(); ++k)
        s.knobs[k] = p.knobs[k]->load();
    s.mix = p.mix->load();
    return s;
}

void RackboxProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    if (buffer.getNumChannels() < 1)
        return;

    // The chain is stereo inside, always. A one-channel buffer (a mono aux) is expanded to a
    // left and a right side for the chain and folded back at the end; a mono input on a stereo
    // output is copied into the second channel first, because JUCE leaves that channel's
    // contents unspecified when the input bus has fewer channels than the output.
    const auto stereoBuffer = buffer.getNumChannels() >= 2;

    if (stereoBuffer && getTotalNumInputChannels() == 1)
        buffer.copyFrom (1, 0, buffer, 0, 0, buffer.getNumSamples());

    auto* left = buffer.getWritePointer (0);
    auto* right = stereoBuffer ? buffer.getWritePointer (1) : nullptr;
    const auto total = buffer.getNumSamples();

    // Settings are read once per host block; the blocks and filters smooth what needs it.
    // The host tempo, once per block. Hosts that give none (a standalone app, a bounce
    // offline in some hosts) get 120, which is what most of them would show anyway.
    double bpm = 120.0, ppq = 0.0, beatsPerBar = 4.0;
    bool hasPosition = false;

    if (auto* head = getPlayHead())
        if (const auto position = head->getPosition())
        {
            if (const auto reported = position->getBpm())
                bpm = *reported;

            // "1 BAR" of pre-delay: as long as the host's bar, 4/4 when it does not say.
            if (const auto signature = position->getTimeSignature())
                beatsPerBar = signature->numerator * 4.0 / juce::jmax (1, signature->denominator);

            // The beat grid to lock a synced LFO to: only while the host is running, because a
            // stopped transport reports one fixed position and locking to that would freeze the sweep.
            if (const auto hostPpq = position->getPpqPosition(); hostPpq.hasValue() && position->getIsPlaying())
            {
                ppq = *hostPpq;
                hasPosition = true;
            }
        }

    hostBpm.store (bpm);
    hostBeatsPerBar.store (beatsPerBar);

    EffectBlock::Settings settings[RackParams::numBlocks];
    for (int b = 0; b < RackParams::numBlocks; ++b)
    {
        settings[b] = getBlockSettings (b);
        settings[b].bpm = bpm;
        settings[b].beatsPerBar = beatsPerBar;
        settings[b].hasPosition = hasPosition;
        settings[b].ppq = ppq;
    }

    const auto lowHz = lowpass->load();
    const auto highHz = highpass->load();
    wetSmoothed.setTargetValue (juce::jlimit (0.0f, 1.0f, dryWet->load()));

    for (int start = 0; start < total; start += maxBlock)
    {
        const auto n = juce::jmin (maxBlock, total - start);
        auto* l = left + start;
        auto* r = stereoBuffer ? right + start : monoRight.data();

        if (! stereoBuffer)
            juce::FloatVectorOperations::copy (r, l, n);

        // The dry signal is taken before the filters; only the wet path is filtered.
        juce::FloatVectorOperations::copy (dryLeft.data(), l, n);
        juce::FloatVectorOperations::copy (dryRight.data(), r, n);

        filters.process (l, r, n, lowHz, highHz);

        // The block starts `start` samples into the host block, so it starts that much further into the song.
        const auto startPpq = (double) start * bpm / (60.0 * sampleRate);

        for (int b = 0; b < RackParams::numBlocks; ++b)
        {
            auto piece = settings[b];
            piece.ppq += startPpq;
            blocks[(size_t) b].process (l, r, n, piece);
        }

        // Equal power, and exact at both ends: at 0 % the output is the dry
        // signal itself and at 100 % the chain itself, not a sum of both with
        // a gain that is only nearly 0.
        for (int i = 0; i < n; ++i)
        {
            const auto m = wetSmoothed.getNextValue();

            if (m <= 0.0f)
            {
                l[i] = dryLeft[(size_t) i];
                r[i] = dryRight[(size_t) i];
            }
            else if (m < 1.0f)
            {
                const auto wet = std::sin (0.5f * juce::MathConstants<float>::pi * m);
                const auto dry = std::cos (0.5f * juce::MathConstants<float>::pi * m);
                l[i] = dry * dryLeft[(size_t) i]  + wet * l[i];
                r[i] = dry * dryRight[(size_t) i] + wet * r[i];
            }
        }

        // Mono out: fold the two sides. With both equal (a chain that does nothing, or
        // Dry/Wet at 0 %) (x + x) * 0.5 is x exactly, so the wire stays a wire.
        if (! stereoBuffer)
            for (int i = 0; i < n; ++i)
                l[i] = 0.5f * (l[i] + r[i]);
    }
}

//==============================================================================
//  copyState(), never state.createXml(): a knob's live value reaches the tree
//  only when the APVTS flushes on its timer, and serialising the tree directly
//  saves whatever the last flush left there. copyState() flushes first.
void RackboxProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    const auto state = parameters.copyState();

    if (const auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void RackboxProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (const auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (parameters.state.getType()))
            parameters.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessorEditor* RackboxProcessor::createEditor()
{
    return new RackboxEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new RackboxProcessor();
}
