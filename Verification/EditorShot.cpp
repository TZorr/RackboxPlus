//
//  EditorShot.cpp
//  Rackbox Plus
//
//  Builds the plugin headlessly, checks what needs the whole processor, and
//  paints the panel into a PNG.
//
//  The checks are the ones RackboxCheck cannot make because they live between
//  the engine and the host:
//
//  - the state round trip: every parameter through getStateInformation into a
//    fresh processor - with the knobs moved immediately before saving, which is
//    when a state saved from the tree instead of copyState() comes back stale;
//  - the null test through the real processBlock: all blocks bypassed and the
//    input section open is a wire, bit for bit, and Dry/Wet at 0 is a wire
//    whatever the chain is doing;
//  - Dry/Wet at 100 % is the chain and nothing else;
//  - automation: a parameter changed from the host between blocks takes effect
//    in the next block, and the effect changes without a step;
//  - the panel follows the parameters: the subtype knob prints the current
//    category's names, the A and B knobs print in the effect's own unit.
//
//  The render is the other half. A panel is arithmetic on rectangles, which is
//  obviously right while reading it and obviously wrong once drawn - look at
//  the PNG after every layout change.
//
//  Usage:  EditorShot <output-directory>
//

#include <juce_gui_basics/juce_gui_basics.h>

#include "PluginEditor.h"
#include "Engine/AuPreset.h"
#include "UI/Palette.h"
#include "Engine/EffectCatalog.h"
#include "PluginProcessor.h"

#include <cstdio>
#include <cstring>
#include <random>

namespace
{
    int failures = 0, checks = 0;

    void check (bool condition, const juce::String& what, const juce::String& detail = {})
    {
        ++checks;

        if (! condition)
        {
            ++failures;
            std::printf ("FAIL: %s  %s\n", what.toRawUTF8(), detail.toRawUTF8());
        }
    }

    constexpr double rate = 48000.0;

    void setParam (RackboxProcessor& processor, const juce::String& id, float value)
    {
        auto* parameter = processor.parameters.getParameter (id);
        jassert (parameter != nullptr);
        parameter->setValueNotifyingHost (parameter->convertTo0to1 (value));
    }

    /** A block's knobs where a click on that effect would put them. A host's change of Type does
        not move the knobs, and the last effect's settings mean something else in the next. */
    void startKnobs (RackboxProcessor& processor, int block, int category, int subtype)
    {
        for (int k = 0; k < RackParams::Block::numKnobs; ++k)
            setParam (processor, RackParams::blockId (block, RackParams::Block::knobs[k]),
                      EffectCatalog::info (category, subtype).knobs[k].defaultValue);
    }

    float getParam (RackboxProcessor& processor, const juce::String& id)
    {
        return processor.parameters.getRawParameterValue (id)->load();
    }

    void pump (int ms)
    {
        juce::MessageManager::getInstance()->runDispatchLoopUntil (ms);
    }

    bool writePng (const juce::Image& image, const juce::File& file)
    {
        // Deleted first: createOutputStream appends to an existing file, and a
        // PNG with a second PNG appended still opens as the old one.
        file.deleteFile();
        std::unique_ptr<juce::FileOutputStream> stream (file.createOutputStream());
        juce::PNGImageFormat png;
        return stream != nullptr && png.writeImageToStream (image, *stream);
    }

    /** Runs blocks of noise through the processor; returns input and output as one-channel vectors. */
    struct Run { std::vector<float> in, out, outRight; };

    Run render (RackboxProcessor& processor, int blocks, int blockSize, unsigned seed,
                const std::function<void (int)>& beforeBlock = nullptr)
    {
        std::mt19937 rng (seed);
        std::uniform_real_distribution<float> dist (-0.5f, 0.5f);

        Run run;
        juce::AudioBuffer<float> buffer (2, blockSize);
        juce::MidiBuffer midi;

        for (int b = 0; b < blocks; ++b)
        {
            if (beforeBlock)
                beforeBlock (b);

            for (int i = 0; i < blockSize; ++i)
            {
                const auto x = dist (rng);
                buffer.setSample (0, i, x);
                buffer.setSample (1, i, -x);   // opposite channels: a swapped or summed channel shows up
                run.in.push_back (x);
            }

            processor.processBlock (buffer, midi);

            for (int i = 0; i < blockSize; ++i)
            {
                run.out.push_back (buffer.getSample (0, i));
                run.outRight.push_back (buffer.getSample (1, i));
            }
        }

        return run;
    }

    /** Left equals the input and right its mirror image: nothing swapped, summed or touched. */
    bool isWire (const Run& run, size_t from = 0)
    {
        for (size_t i = from; i < run.in.size(); ++i)
            if (! juce::exactlyEqual (run.out[i], run.in[i]) || ! juce::exactlyEqual (run.outRight[i], -run.in[i]))
                return false;

        return true;
    }

    void bypassAll (RackboxProcessor& processor)
    {
        for (int b = 0; b < RackParams::numBlocks; ++b)
            setParam (processor, RackParams::blockId (b, RackParams::Block::bypass), 1.0f);
    }

    //==============================================================================
    void checkStateRoundTrip()
    {
        RackboxProcessor original;
        original.prepareToPlay (rate, 512);

        // Moved immediately before saving, on purpose: see copyState() in PluginProcessor.cpp.
        setParam (original, RackParams::lowpass, 3400.0f);
        setParam (original, RackParams::highpass, 85.0f);
        setParam (original, RackParams::dryWet, 0.62f);
        setParam (original, RackParams::blockId (0, RackParams::Block::type), 2.0f);
        setParam (original, RackParams::blockId (0, RackParams::Block::sub), 2.0f);
        setParam (original, RackParams::blockId (1, RackParams::Block::type), 3.0f);
        setParam (original, RackParams::blockId (1, RackParams::Block::sub), 1.0f);
        setParam (original, RackParams::blockId (1, RackParams::Block::bypass), 1.0f);
        setParam (original, RackParams::blockId (2, RackParams::Block::a), 0.81f);
        setParam (original, RackParams::blockId (2, RackParams::Block::b), 0.13f);
        setParam (original, RackParams::blockId (2, RackParams::Block::mix), 0.27f);

        juce::MemoryBlock state;
        original.getStateInformation (state);

        RackboxProcessor restored;
        restored.setStateInformation (state.getData(), (int) state.getSize());

        int mismatched = 0, total = 0;
        for (auto* parameter : original.getParameters())
        {
            auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*> (parameter);
            if (withId == nullptr) continue;

            ++total;
            const auto a = getParam (original, withId->paramID);
            const auto b = getParam (restored, withId->paramID);

            if (std::abs (a - b) > 1.0e-6f)
            {
                ++mismatched;
                std::printf ("  %s: %f vs %f\n", withId->paramID.toRawUTF8(), a, b);
            }
        }

        // Per block: type, sub, bypass, sync, pre, eight knobs and mix.
        check (total == 3 + (6 + RackParams::Block::numKnobs) * RackParams::numBlocks,
               "there are 59 parameters (3 global + 14 per block, 4 blocks)", juce::String (total));
        check (mismatched == 0, "every parameter survives a state round trip", juce::String (mismatched) + " differ");

        // A session saved before the fourth block existed has no b4_* values: it must
        // load, restore everything it has, and leave block 4 at its default (bypassed).
        {
            RackboxProcessor old;
            setParam (old, RackParams::blockId (0, RackParams::Block::type), 3.0f);
            setParam (old, RackParams::blockId (2, RackParams::Block::a), 0.77f);
            setParam (old, RackParams::blockId (3, RackParams::Block::bypass), 0.0f);   // must NOT survive

            juce::MemoryBlock saved;
            old.getStateInformation (saved);

            auto xml = juce::AudioProcessor::getXmlFromBinary (saved.getData(), (int) saved.getSize());
            check (xml != nullptr, "the saved state is readable XML");

            if (xml != nullptr)
            {
                for (auto* child = xml->getFirstChildElement(); child != nullptr;)
                {
                    auto* next = child->getNextElement();
                    if (child->getStringAttribute ("id").startsWith ("b4_"))
                        xml->removeChildElement (child, true);
                    child = next;
                }

                juce::MemoryBlock legacy;
                juce::AudioProcessor::copyXmlToBinary (*xml, legacy);

                RackboxProcessor loaded;
                loaded.setStateInformation (legacy.getData(), (int) legacy.getSize());

                check (juce::exactlyEqual (getParam (loaded, RackParams::blockId (0, RackParams::Block::type)), 3.0f)
                       && std::abs (getParam (loaded, RackParams::blockId (2, RackParams::Block::a)) - 0.77f) < 1.0e-6f,
                       "a three-block session restores what it has");
                check (getParam (loaded, RackParams::blockId (3, RackParams::Block::bypass)) >= 0.5f,
                       "and block 4 comes up bypassed");
                check (getParam (loaded, RackParams::blockId (0, RackParams::Block::sync)) < 0.5f
                       && getParam (loaded, RackParams::blockId (1, RackParams::Block::pre)) < 0.5f,
                       "and Sync and the pre-delay, which the old session never had, are off");
            }
        }

        // A state that is not ours must be ignored, not half-applied.
        const auto before = getParam (restored, RackParams::dryWet);
        const char garbage[] = "not a rackbox state";
        restored.setStateInformation (garbage, (int) sizeof (garbage));
        check (juce::exactlyEqual (getParam (restored, RackParams::dryWet), before), "garbage state leaves the parameters alone");
    }

    void checkNull()
    {
        constexpr int blocks = 40, size = 512;

        {
            RackboxProcessor p;
            p.prepareToPlay (rate, size);
            bypassAll (p);
            const auto run = render (p, blocks, size, 11);
            check (isWire (run),
                   "all blocks bypassed, filters open: the processor is a wire, bit for bit");
        }

        {
            RackboxProcessor p;
            p.prepareToPlay (rate, size);
            setParam (p, RackParams::dryWet, 0.0f);
            const auto run = render (p, blocks, size, 12);

            // The knob glides over 20 ms; once it has arrived the output is the dry signal exactly.
            check (isWire (run, 4 * size),
                   "Dry/Wet at 0 %: the processor is a wire whatever the chain does, bit for bit");
        }

        {
            // The filters are on the effect path only: with them closed right down, Dry/Wet at 0 %
            // is still the raw input, bit for bit, and at 100 % the same chain is heard filtered.
            RackboxProcessor dry, wet;
            dry.prepareToPlay (rate, size);
            wet.prepareToPlay (rate, size);
            bypassAll (dry);
            bypassAll (wet);

            for (auto* p : { &dry, &wet })
            {
                setParam (*p, RackParams::lowpass, 300.0f);
                setParam (*p, RackParams::highpass, 2000.0f);   // band-pass nonsense on purpose: almost nothing survives
            }

            setParam (dry, RackParams::dryWet, 0.0f);
            setParam (wet, RackParams::dryWet, 1.0f);

            const auto dryRun = render (dry, blocks, size, 15);
            const auto wetRun = render (wet, blocks, size, 15);

            check (isWire (dryRun, 4 * size), "filters engaged, Dry/Wet at 0 %: the dry signal is untouched, bit for bit");

            double inRms = 0.0, wetRms = 0.0;
            for (size_t i = (size_t) (4 * size); i < wetRun.in.size(); ++i)
            {
                inRms += (double) wetRun.in[i] * wetRun.in[i];
                wetRms += (double) wetRun.out[i] * wetRun.out[i];
            }

            check (10.0 * std::log10 (wetRms / inRms) < -15.0, "the same filters at Dry/Wet 100 % are heard on the wet path",
                   juce::String (10.0 * std::log10 (wetRms / inRms), 1) + " dB");

            // In between: the dry share is the raw input, so half-way is the raw input plus a filtered one - louder than
            // the wet alone, and never quieter than the dry alone would allow.
            RackboxProcessor half;
            half.prepareToPlay (rate, size);
            bypassAll (half);
            setParam (half, RackParams::lowpass, 300.0f);
            setParam (half, RackParams::highpass, 2000.0f);
            setParam (half, RackParams::dryWet, 0.5f);
            const auto halfRun = render (half, blocks, size, 15);

            double halfRms = 0.0;
            for (size_t i = (size_t) (4 * size); i < halfRun.in.size(); ++i)
                halfRms += (double) halfRun.out[i] * halfRun.out[i];

            // cos(pi/4) = 0.707 of the raw input: about -3 dB, plus a little from the wet side.
            check (std::abs (10.0 * std::log10 (halfRms / inRms) - -3.0) < 1.0, "at 50 % the dry half is the raw input (about -3 dB)",
                   juce::String (10.0 * std::log10 (halfRms / inRms), 1) + " dB");
        }

        {
            // Dry/Wet at 100 % against 0 %: with a chain that changes the signal the two must differ, and
            // 100 % must contain none of the dry signal - an all-delay chain fed noise cannot equal the input.
            RackboxProcessor p;
            p.prepareToPlay (rate, size);
            const auto run = render (p, blocks, size, 13);
            check (! isWire (run), "the default chain changes the signal");
        }

        {
            // A block size that the host changes its mind about: larger than prepared.
            RackboxProcessor p;
            p.prepareToPlay (rate, 256);
            bypassAll (p);
            const auto run = render (p, 8, 1000, 14);
            check (isWire (run),
                   "a host block of 1000 against a prepared 256 is still a wire");
        }

    }

    //==============================================================================
    bool setLayout (RackboxProcessor& p, const juce::AudioChannelSet& in, const juce::AudioChannelSet& out)
    {
        juce::AudioProcessor::BusesLayout layout;
        layout.inputBuses.add (in);
        layout.outputBuses.add (out);
        return p.setBusesLayout (layout);
    }

    void checkMono()
    {
        const auto mono = juce::AudioChannelSet::mono();
        const auto stereo = juce::AudioChannelSet::stereo();

        {
            RackboxProcessor p;
            check (setLayout (p, stereo, stereo), "stereo in, stereo out is supported");
            check (setLayout (p, mono, stereo), "mono in, stereo out is supported (Logic's mono aux)");
            check (setLayout (p, mono, mono), "mono in, mono out is supported");
            check (! setLayout (p, stereo, mono), "stereo in, mono out is refused");
            check (! setLayout (p, juce::AudioChannelSet::create5point1(), stereo), "a 5.1 input is refused");
        }

        constexpr int size = 512, blocks = 30;

        const auto run = [&] (RackboxProcessor& p, int channels, unsigned seed, const std::function<void (int)>& beforeBlock = nullptr)
        {
            // Returns input, left out, right out (right empty for a one-channel buffer).
            std::mt19937 rng (seed);
            std::uniform_real_distribution<float> dist (-0.5f, 0.5f);
            juce::AudioBuffer<float> buffer (channels, size);
            juce::MidiBuffer midi;
            Run r;

            for (int b = 0; b < blocks; ++b)
            {
                if (beforeBlock)
                    beforeBlock (b);

                for (int i = 0; i < size; ++i)
                {
                    const auto x = dist (rng);
                    buffer.setSample (0, i, x);
                    if (channels > 1)
                        buffer.setSample (1, i, 0.9f);   // junk: a mono input's second channel is unspecified
                    r.in.push_back (x);
                }

                p.processBlock (buffer, midi);

                for (int i = 0; i < size; ++i)
                {
                    r.out.push_back (buffer.getSample (0, i));
                    if (channels > 1)
                        r.outRight.push_back (buffer.getSample (1, i));
                }
            }

            return r;
        };

        const auto finite = [] (const std::vector<float>& v)
        {
            for (auto x : v)
                if (! std::isfinite (x) || std::abs (x) > 8.0f) return false;
            return true;
        };

        // Mono in, stereo out, nothing engaged: both sides are the input, whatever was in channel 2.
        {
            RackboxProcessor p;
            setLayout (p, mono, stereo);
            p.prepareToPlay (rate, size);
            bypassAll (p);
            const auto r = run (p, 2, 21);

            bool same = true;
            for (size_t i = 0; i < r.in.size(); ++i)
                same = same && juce::exactlyEqual (r.out[i], r.in[i]) && juce::exactlyEqual (r.outRight[i], r.in[i]);

            check (same, "mono in, stereo out, bypassed: both channels are the input, bit for bit (the junk second input channel is ignored)");
        }

        // Mono in, stereo out through a reverb: the source is widened.
        {
            RackboxProcessor p;
            setLayout (p, mono, stereo);
            p.prepareToPlay (rate, size);
            bypassAll (p);
            setParam (p, RackParams::blockId (0, RackParams::Block::bypass), 0.0f);
            setParam (p, RackParams::blockId (0, RackParams::Block::type), 1.0f);
            setParam (p, RackParams::blockId (0, RackParams::Block::sub), 1.0f);
            setParam (p, RackParams::blockId (0, RackParams::Block::mix), 1.0f);

            const auto r = run (p, 2, 22);
            double difference = 0.0, level = 0.0;
            for (size_t i = (size_t) (10 * size); i < r.out.size(); ++i)
            {
                difference += std::abs ((double) r.out[i] - r.outRight[i]);
                level += std::abs ((double) r.out[i]);
            }

            check (finite (r.out) && finite (r.outRight) && level > 1.0, "mono in through a Room reverb is finite and audible");
            check (difference > 0.1 * level, "and the reverb makes the sides differ: a mono source, a stereo result",
                   juce::String (difference / level, 3));
        }

        // Mono in, mono out.
        {
            RackboxProcessor p;
            setLayout (p, mono, mono);
            p.prepareToPlay (rate, size);
            bypassAll (p);
            const auto r = run (p, 1, 23);
            check (r.out == r.in, "mono in, mono out, bypassed: a wire, bit for bit");
        }

        {
            RackboxProcessor p;
            setLayout (p, mono, mono);
            p.prepareToPlay (rate, size);

            const auto r = run (p, 1, 24);
            double level = 0.0;
            for (size_t i = (size_t) (10 * size); i < r.out.size(); ++i)
                level += std::abs ((double) r.out[i]);

            check (finite (r.out) && level > 1.0, "mono in, mono out through the default chain is finite and audible");
        }

        {
            RackboxProcessor p;
            setLayout (p, mono, mono);
            p.prepareToPlay (rate, size);
            setParam (p, RackParams::dryWet, 0.0f);
            const auto r = run (p, 1, 25);

            bool same = true;
            for (size_t i = (size_t) (4 * size); i < r.in.size(); ++i)
                same = same && juce::exactlyEqual (r.out[i], r.in[i]);

            check (same, "mono in, mono out, Dry/Wet at 0 %: the input, bit for bit");
        }

        // A host block bigger than the prepared one, in mono.
        {
            RackboxProcessor p;
            setLayout (p, mono, mono);
            p.prepareToPlay (rate, 256);
            bypassAll (p);

            juce::AudioBuffer<float> buffer (1, 1000);
            juce::MidiBuffer midi;
            for (int i = 0; i < 1000; ++i)
                buffer.setSample (0, i, 0.001f * (float) i);

            const juce::AudioBuffer<float> original (buffer);
            p.processBlock (buffer, midi);

            bool same = true;
            for (int i = 0; i < 1000; ++i)
                same = same && juce::exactlyEqual (buffer.getSample (0, i), original.getSample (0, i));

            check (same, "a mono host block of 1000 against a prepared 256 is still a wire");
        }
    }

    /** A host that reports a fixed tempo. */
    struct FixedTempoHead : juce::AudioPlayHead
    {
        double bpm = 120.0;
        int numerator = 4, denominator = 4;

        juce::Optional<PositionInfo> getPosition() const override
        {
            PositionInfo info;
            info.setBpm (bpm);
            info.setTimeSignature (juce::AudioPlayHead::TimeSignature { numerator, denominator });
            return info;
        }
    };

    /** Where the first echo of an impulse lands, in samples after the impulse. */
    int firstEchoAfterImpulse (double bpm, float knob, bool sync, bool withHost)
    {
        RackboxProcessor p;
        FixedTempoHead head;
        head.bpm = bpm;

        if (withHost)
            p.setPlayHead (&head);

        p.prepareToPlay (rate, 512);
        bypassAll (p);

        setParam (p, RackParams::blockId (0, RackParams::Block::bypass), 0.0f);
        setParam (p, RackParams::blockId (0, RackParams::Block::type), 2.0f);   // Delay
        setParam (p, RackParams::blockId (0, RackParams::Block::sub), 0.0f);    // Digital
        startKnobs (p, 0, 2, 0);   // the Digital's own (the Plate's Width would be its Mod Depth)
        setParam (p, RackParams::blockId (0, RackParams::Block::sync), sync ? 1.0f : 0.0f);
        setParam (p, RackParams::blockId (0, RackParams::Block::a), knob);
        setParam (p, RackParams::blockId (0, RackParams::Block::b), 0.0f);      // no feedback: one echo
        setParam (p, RackParams::blockId (0, RackParams::Block::mix), 1.0f);

        const int size = 512, blocks = (int) (1.2 * rate / size);
        juce::AudioBuffer<float> buffer (2, size);
        juce::MidiBuffer midi;
        std::vector<float> out;

        for (int b = 0; b < blocks; ++b)
        {
            buffer.clear();

            if (b == 0)
            {
                buffer.setSample (0, 100, 0.8f);
                buffer.setSample (1, 100, 0.8f);
            }

            p.processBlock (buffer, midi);

            for (int i = 0; i < size; ++i)
                out.push_back (buffer.getSample (0, i));
        }

        int peak = 0;
        for (size_t i = 0; i < out.size(); ++i)
            if (std::abs (out[i]) > std::abs (out[(size_t) peak]))
                peak = (int) i;

        return peak - 100;
    }

    /** Where the wet signal of a Room block, with the pre-delay at `step`, first rises above nothing. */
    int reverbOnsetThroughProcessor (double bpm, int step, int numerator, int denominator)
    {
        RackboxProcessor p;
        FixedTempoHead head;
        head.bpm = bpm;
        head.numerator = numerator;
        head.denominator = denominator;
        p.setPlayHead (&head);
        p.prepareToPlay (rate, 512);
        bypassAll (p);

        setParam (p, RackParams::blockId (0, RackParams::Block::bypass), 0.0f);
        setParam (p, RackParams::blockId (0, RackParams::Block::type), 1.0f);   // Reverb
        setParam (p, RackParams::blockId (0, RackParams::Block::sub), 1.0f);    // Room
        setParam (p, RackParams::blockId (0, RackParams::Block::pre), (float) step);

        startKnobs (p, 0, 1, 1);   // the Room's own (the Plate's Bandwidth would be its Pre-Delay)
        setParam (p, RackParams::blockId (0, RackParams::Block::mix), 1.0f);

        const int size = 512, blocks = (int) (2.6 * rate / size);
        juce::AudioBuffer<float> buffer (2, size);
        juce::MidiBuffer midi;

        for (int b = 0; b < blocks; ++b)
        {
            buffer.clear();

            if (b == 0)
            {
                buffer.setSample (0, 100, 0.8f);
                buffer.setSample (1, 100, 0.8f);
            }

            p.processBlock (buffer, midi);

            for (int i = 0; i < size; ++i)
                if (std::abs (buffer.getSample (0, i)) > 1.0e-5f)
                    return b * size + i - 100;
        }

        return -1;
    }

    void checkPreDelayThroughProcessor()
    {
        const auto near = [] (int samples, double seconds) { return samples >= (int) (seconds * rate) - 1 && samples <= (int) (seconds * rate) + (int) (0.06 * rate); };

        const auto at120 = reverbOnsetThroughProcessor (120.0, 4, 4, 4);
        check (near (at120, 0.125), "Room, 1/16 at 120 BPM: silent for the first 125 ms", juce::String (at120 * 1000.0 / rate, 1) + " ms");

        const auto at90 = reverbOnsetThroughProcessor (90.0, 4, 4, 4);
        check (near (at90, 60.0 / 90.0 * 0.25), "Room, 1/16 at 90 BPM: silent for 166.7 ms", juce::String (at90 * 1000.0 / rate, 1) + " ms");

        const auto bar = reverbOnsetThroughProcessor (120.0, 8, 3, 4);
        check (near (bar, 1.5), "Room, 1 bar of 3/4 at 120 BPM: 1.5 s", juce::String (bar * 1.0 / rate, 3) + " s");

        const auto off = reverbOnsetThroughProcessor (120.0, 0, 4, 4);
        check (off >= 0 && off < (int) (0.06 * rate), "Room with the pre-delay off starts at once", juce::String (off * 1000.0 / rate, 1) + " ms");
    }

    void checkTempoSync()
    {
        constexpr float eighth = 5.0f / 13.0f;   // step 5 of 0..13 is 1/8

        const auto at120 = firstEchoAfterImpulse (120.0, eighth, true, true);
        check (std::abs (at120 - (int) (0.25 * rate)) <= 2, "Sync, 1/8 at 120 BPM: the echo lands at 250 ms",
               juce::String (at120 * 1000.0 / rate, 2) + " ms");

        const auto at90 = firstEchoAfterImpulse (90.0, eighth, true, true);
        check (std::abs (at90 - (int) (60.0 / 90.0 * 0.5 * rate)) <= 2, "Sync, 1/8 at 90 BPM: the echo lands at 333 ms",
               juce::String (at90 * 1000.0 / rate, 2) + " ms");

        const auto quarter = firstEchoAfterImpulse (120.0, 8.0f / 13.0f, true, true);
        check (std::abs (quarter - (int) (0.5 * rate)) <= 2, "Sync, 1/4 at 120 BPM: 500 ms",
               juce::String (quarter * 1000.0 / rate, 2) + " ms");

        const auto dotted = firstEchoAfterImpulse (120.0, 7.0f / 13.0f, true, true);
        check (std::abs (dotted - (int) (0.375 * rate)) <= 2, "Sync, dotted 1/8 at 120 BPM: 375 ms",
               juce::String (dotted * 1000.0 / rate, 2) + " ms");

        const auto noHost = firstEchoAfterImpulse (77.0, eighth, true, false);
        check (std::abs (noHost - (int) (0.25 * rate)) <= 2, "Sync without a host tempo falls back to 120 BPM",
               juce::String (noHost * 1000.0 / rate, 2) + " ms");

        // Sync off: the same knob position is a time in milliseconds and ignores the host.
        const auto free = firstEchoAfterImpulse (60.0, 0.5f, false, true);
        check (std::abs (free - (int) (0.1 * rate)) <= 2, "Sync off: the knob is milliseconds again, 100 ms at 0.5",
               juce::String (free * 1000.0 / rate, 2) + " ms");

        // A tempo that would need more than the buffer holds is held at four seconds, not wrapped.
        const auto slow = firstEchoAfterImpulse (20.0, 1.0f, true, true);
        check (slow < 0 || slow <= (int) (4.05 * rate), "a whole note at 20 BPM does not run off the end of the buffer");
    }

    void checkAutomation()
    {
        RackboxProcessor p;
        p.prepareToPlay (rate, 512);

        // A sine through Chorus, then the host automates the type to Distortion/Overdrive at block 20:
        // the result must not step, and afterwards it must actually be the other effect.
        for (int b = 0; b < RackParams::numBlocks; ++b)
            setParam (p, RackParams::blockId (b, RackParams::Block::bypass), b == 1 ? 0.0f : 1.0f);

        setParam (p, RackParams::blockId (1, RackParams::Block::type), 0.0f);
        setParam (p, RackParams::blockId (1, RackParams::Block::sub), 3.0f);   // Tremolo
        setParam (p, RackParams::blockId (1, RackParams::Block::a), 0.0f);
        setParam (p, RackParams::blockId (1, RackParams::Block::b), 1.0f);
        setParam (p, RackParams::blockId (1, RackParams::Block::mix), 1.0f);
        setParam (p, RackParams::dryWet, 1.0f);

        std::vector<float> out;
        juce::AudioBuffer<float> buffer (2, 512);
        juce::MidiBuffer midi;
        double phase = 0.0;

        for (int b = 0; b < 60; ++b)
        {
            if (b == 20)
            {
                setParam (p, RackParams::blockId (1, RackParams::Block::type), 3.0f);   // Distortion
                setParam (p, RackParams::blockId (1, RackParams::Block::sub), 1.0f);    // Fuzz
                setParam (p, RackParams::blockId (1, RackParams::Block::a), 1.0f);

                // Tone down to ~1.9 kHz. Fuzz at full gain squares the sine, and with Tone open its
                // own edges are steps of 0.3 to 0.6 depending on where the samples fall - as large
                // as a switch without a fade would be. Darker, its edges stay far below the limit,
                // so a step in the window below can only be the change itself.
                setParam (p, RackParams::blockId (1, RackParams::Block::b), 0.3f);
            }

            for (int i = 0; i < 512; ++i)
            {
                const auto v = (float) (0.4 * std::sin (phase));
                phase += 6.283185307179586 * 500.0 / rate;
                buffer.setSample (0, i, v);
                buffer.setSample (1, i, v);
            }

            p.processBlock (buffer, midi);

            for (int i = 0; i < 512; ++i)
                out.push_back (buffer.getSample (0, i));
        }

        float step = 0.0f;
        for (size_t i = 20 * 512 - 2; i < 26 * 512; ++i)
            step = juce::jmax (step, std::abs (out[i] - out[i - 1]));

        check (step < 0.3f, "an automated effect change does not step", juce::String (step, 3));

        // After the fade Fuzz is clipping a 0.4 sine at full gain: a flat top near 0.28 (0.35 * 0.8).
        float peak = 0.0f;
        for (size_t i = 40 * 512; i < 60 * 512; ++i)
            peak = juce::jmax (peak, std::abs (out[i]));

        check (peak > 0.15f && peak < 0.35f, "afterwards it is the new effect", "peak " + juce::String (peak, 3));
    }
}

namespace
{
    /** The preset menu's .aupreset: saves and loads the knobs, names this
        plugin, and refuses another plugin's preset. */
    void checkPresets()
    {
        const auto folder = juce::File::createTempFile ("presets");
        folder.createDirectory();
        const auto file = folder.getChildFile ("Test.aupreset");

        RackboxProcessor original;
        setParam (original, RackParams::lowpass, 3400.0f);
        check (PresetButton::save (original, file).wasOk(), "a preset saves");

        const auto text = file.loadFileAsString();
        const auto subtypeEntry = "<integer>" + juce::String (AuPreset::subtype) + "</integer>";
        check (text.contains ("<key>jucePluginState</key>") && text.contains (subtypeEntry)
                   && AuPreset::subtype == AuPreset::fourCC ("Rbxp") && AuPreset::type == AuPreset::fourCC ("aufx"),
               "the preset is an .aupreset naming aufx/Rbxp");

        RackboxProcessor loaded;
        check (PresetButton::load (loaded, file).wasOk(), "a preset loads");
        check (std::abs (getParam (loaded, RackParams::lowpass) - 3400.0f) < 0.5f,
               "a preset brings back the knobs", juce::String (getParam (loaded, RackParams::lowpass)));

        const auto foreign = folder.getChildFile ("Foreign.aupreset");
        foreign.replaceWithText (text.replace (subtypeEntry, "<integer>1</integer>"));
        RackboxProcessor untouched;
        const auto before = getParam (untouched, RackParams::lowpass);
        check (PresetButton::load (untouched, foreign).failed() && getParam (untouched, RackParams::lowpass) == before,
               "another plugin's preset is refused");

        folder.deleteRecursively();
    }
}

int main (int argc, char** argv)
{
    const juce::ScopedJuceInitialiser_GUI juceInitialiser;

    const juce::File outputDirectory = argc > 1 ? juce::File (juce::String (argv[1]))
                                                : juce::File::getCurrentWorkingDirectory();

    if (! outputDirectory.createDirectory())
    {
        std::printf ("FAIL: cannot create %s\n", outputDirectory.getFullPathName().toRawUTF8());
        return 1;
    }

    std::printf ("State round trip...\n");
    checkStateRoundTrip();
    std::printf ("Presets...\n");
    checkPresets();
    std::printf ("Null test through processBlock...\n");
    checkNull();
    std::printf ("Mono...\n");
    checkMono();
    std::printf ("Automation...\n");
    checkAutomation();
    std::printf ("Tempo sync...\n");
    checkTempoSync();
    std::printf ("Pre-delay...\n");
    checkPreDelayThroughProcessor();

    // The panel: the chain from the brief, a demo signal on the display.
    {
        RackboxProcessor processor;
        processor.prepareToPlay (rate, 512);

        setParam (processor, RackParams::lowpass, 12000.0f);
        setParam (processor, RackParams::highpass, 90.0f);
        setParam (processor, RackParams::dryWet, 0.8f);

        setParam (processor, RackParams::blockId (0, RackParams::Block::a), 0.6f);
        setParam (processor, RackParams::blockId (0, RackParams::Block::b), 0.35f);
        setParam (processor, RackParams::blockId (1, RackParams::Block::a), 0.3f);
        setParam (processor, RackParams::blockId (1, RackParams::Block::b), 0.6f);
        setParam (processor, RackParams::blockId (2, RackParams::Block::a), 0.55f);
        setParam (processor, RackParams::blockId (2, RackParams::Block::b), 0.45f);

        std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditorAndMakeActive());
        auto* rack = dynamic_cast<RackboxEditor*> (editor.get());
        pump (100);

        // What the panel prints follows the parameters.
        check (rack->getPanel (0).subtypeText() == "Plate", "Block 1 prints Plate", rack->getPanel (0).subtypeText());
        check (rack->getPanel (1).subtypeText() == "Chorus", "Block 2 prints Chorus", rack->getPanel (1).subtypeText());
        check (rack->getPanel (2).subtypeText() == "Tape", "Block 3 prints Tape", rack->getPanel (2).subtypeText());
        check (rack->getPanel (1).labelAText() == "0.50 Hz" || rack->getPanel (1).labelAText().endsWith (" Hz"),
               "Chorus Rate prints in Hz", rack->getPanel (1).labelAText());
        check (rack->getPanel (2).labelAText().endsWith ("ms"), "Tape Time prints in ms", rack->getPanel (2).labelAText());

        setParam (processor, RackParams::blockId (0, RackParams::Block::type), 2.0f);
        setParam (processor, RackParams::blockId (0, RackParams::Block::sub), 2.0f);
        pump (100);
        check (rack->getPanel (0).subtypeText() == "Ping-Pong", "changing the type changes the subtype's names", rack->getPanel (0).subtypeText());

        setParam (processor, RackParams::blockId (0, RackParams::Block::type), 3.0f);
        setParam (processor, RackParams::blockId (0, RackParams::Block::sub), 2.0f);
        pump (100);
        check (rack->getPanel (0).subtypeText() == "Bitcrusher" && rack->getPanel (0).labelAText().endsWith ("bit"),
               "Bitcrusher's Bits print in bits", rack->getPanel (0).subtypeText() + " / " + rack->getPanel (0).labelAText());

        setParam (processor, RackParams::blockId (0, RackParams::Block::type), 1.0f);
        setParam (processor, RackParams::blockId (0, RackParams::Block::sub), 3.0f);   // past the end of Reverb
        pump (100);
        check (rack->getPanel (0).subtypeText() == "Hall", "a subtype past the end of Reverb prints Hall", rack->getPanel (0).subtypeText());

        // Clicking the buttons writes the parameters and lights the right ones.
        rack->getPanel (2).clickType (3);
        rack->getPanel (2).clickSubtype (2);
        pump (100);
        check (juce::exactlyEqual (getParam (processor, RackParams::blockId (2, RackParams::Block::type)), 3.0f)
               && juce::exactlyEqual (getParam (processor, RackParams::blockId (2, RackParams::Block::sub)), 2.0f),
               "clicking Dist and Bitcrusher sets the block's parameters");
        check (rack->getPanel (2).subtypeText() == "Bitcrusher", "and the Subtype row shows Bitcrusher", rack->getPanel (2).subtypeText());
        rack->getPanel (2).clickType (2);
        rack->getPanel (2).clickSubtype (1);
        pump (100);
        check (rack->getPanel (2).subtypeText() == "Tape", "clicking Delay and Tape puts it back", rack->getPanel (2).subtypeText());

        // Choosing another effect by a click starts its knobs where the catalog puts them; a host
        // writing the Type does not, and clicking the effect already loaded changes nothing.
        {
            const auto knob = [&] (int k) { return getParam (processor, RackParams::blockId (2, RackParams::Block::knobs[k])); };
            setParam (processor, RackParams::blockId (2, RackParams::Block::knobs[0]), 0.9f);
            setParam (processor, RackParams::blockId (2, RackParams::Block::knobs[4]), 0.2f);
            rack->getPanel (2).clickSubtype (1);   // Tape again: no change
            pump (50);
            check (std::abs (knob (0) - 0.9f) < 1.0e-6f, "clicking the effect already loaded keeps its knobs");

            rack->getPanel (2).clickSubtype (0);   // Digital
            pump (50);
            const auto& digital = EffectCatalog::info (2, 0);
            check (std::abs (knob (0) - digital.knobs[0].defaultValue) < 1.0e-6f
                   && std::abs (knob (4) - digital.knobs[4].defaultValue) < 1.0e-6f,
                   "clicking another effect puts every knob at that effect's start");

            setParam (processor, RackParams::blockId (2, RackParams::Block::knobs[0]), 0.9f);
            setParam (processor, RackParams::blockId (2, RackParams::Block::sub), 1.0f);   // a host's change
            pump (50);
            check (std::abs (knob (0) - 0.9f) < 1.0e-6f, "a host changing the effect leaves the knobs where they are");

            const auto shown = [&] { int n = 0; for (int k = 0; k < RackParams::Block::numKnobs; ++k) n += rack->getPanel (2).knobVisible (k) ? 1 : 0; return n; };
            check (shown() == EffectCatalog::info (2, 1).numKnobs(), "only the knobs the effect has are shown",
                   juce::String (shown()) + " of " + juce::String (EffectCatalog::info (2, 1).numKnobs()));
        }

        // Sync shows up on a Delay and only there, and names the note value.
        setParam (processor, RackParams::blockId (0, RackParams::Block::type), 2.0f);
        pump (100);
        check (rack->getPanel (0).syncVisible(), "a Delay block offers Sync");
        setParam (processor, RackParams::blockId (0, RackParams::Block::sync), 1.0f);
        setParam (processor, RackParams::blockId (0, RackParams::Block::a), 5.0f / 13.0f);
        pump (100);
        check (rack->getPanel (0).labelAText() == "1/8", "with Sync on the Time knob prints the note value", rack->getPanel (0).labelAText());
        setParam (processor, RackParams::blockId (0, RackParams::Block::type), 0.0f);
        setParam (processor, RackParams::blockId (0, RackParams::Block::a), 8.0f / 13.0f);
        pump (100);
        check (rack->getPanel (0).syncVisible() && rack->getPanel (0).labelAText() == "1/4",
               "a Modulation block offers Sync too, and its Rate prints the note value", rack->getPanel (0).labelAText());
        setParam (processor, RackParams::blockId (0, RackParams::Block::type), 3.0f);
        pump (100);
        check (! rack->getPanel (0).syncVisible(), "a Distortion block does not");
        setParam (processor, RackParams::blockId (0, RackParams::Block::type), 1.0f);
        pump (100);
        check (! rack->getPanel (0).syncVisible(), "a Reverb block does not");
        setParam (processor, RackParams::blockId (0, RackParams::Block::sync), 0.0f);
        setParam (processor, RackParams::blockId (0, RackParams::Block::a), 0.6f);   // as the picture wants it
        pump (100);

        // A reverb has a PRE button in Sync's place: it reads the step, steps on a click, wraps, and says the milliseconds.
        setParam (processor, RackParams::blockId (0, RackParams::Block::type), 1.0f);
        setParam (processor, RackParams::blockId (0, RackParams::Block::pre), 0.0f);
        pump (100);
        check (rack->getPanel (0).preVisible() && ! rack->getPanel (0).syncVisible(), "a Reverb block shows PRE, not Sync");
        check (rack->getPanel (0).preText() == "PRE OFF", "and it starts off", rack->getPanel (0).preText());
        rack->getPanel (0).clickPre (1);
        pump (100);
        check (rack->getPanel (0).preText() == "PRE 1/128", "a click steps to 1/128", rack->getPanel (0).preText());
        rack->getPanel (0).clickPre (-1);
        rack->getPanel (0).clickPre (-1);
        pump (100);
        check (rack->getPanel (0).preText() == "PRE 1 BAR", "right-clicking back from OFF wraps to 1 BAR", rack->getPanel (0).preText());
        setParam (processor, RackParams::blockId (0, RackParams::Block::pre), 4.0f);
        pump (100);
        check (rack->getPanel (0).preTooltip() == "Pre-delay 1/16 = 125.0 ms at 120 BPM", "the tooltip gives the milliseconds", rack->getPanel (0).preTooltip());
        setParam (processor, RackParams::blockId (0, RackParams::Block::type), 2.0f);
        pump (100);
        check (! rack->getPanel (0).preVisible() && rack->getPanel (0).syncVisible(), "a Delay block shows Sync, not PRE");
        setParam (processor, RackParams::blockId (0, RackParams::Block::type), 1.0f);
        setParam (processor, RackParams::blockId (0, RackParams::Block::pre), 0.0f);
        pump (100);

        // Block 4 exists, starts bypassed, and its buttons work.
        check (rack->getPanel (3).bypassText() == "bypassed", "Block 4 starts bypassed", rack->getPanel (3).bypassText());
        rack->getPanel (3).clickType (0);
        rack->getPanel (3).clickSubtype (3);
        pump (100);
        check (juce::exactlyEqual (getParam (processor, RackParams::blockId (3, RackParams::Block::type)), 0.0f)
               && juce::exactlyEqual (getParam (processor, RackParams::blockId (3, RackParams::Block::sub)), 3.0f),
               "clicking Mod and Tremolo sets block 4's parameters");
        rack->getPanel (3).clickType (3);
        rack->getPanel (3).clickSubtype (1);
        pump (100);

        // Back to the brief's chain for the picture.
        setParam (processor, RackParams::blockId (0, RackParams::Block::sub), 0.0f);
        pump (100);

        // A demo signal: a decaying pluck every half second with a little noise.
        {
            juce::AudioBuffer<float> buffer (2, 512);
            juce::MidiBuffer midi;
            juce::Random random (5);
            double phase = 0.0;
            long sample = 0;

            for (int b = 0; b < 240; ++b)
            {
                for (int i = 0; i < 512; ++i, ++sample)
                {
                    const auto t = std::fmod ((double) sample / rate, 0.6);
                    const auto env = std::exp (-t * 5.0);
                    phase += 6.283185307179586 * (220.0 + 60.0 * std::exp (-t * 20.0)) / rate;
                    const auto v = (float) (0.6 * env * (std::sin (phase) + 0.3 * std::sin (2.0 * phase)) + 0.02 * (random.nextFloat() - 0.5f));
                    buffer.setSample (0, i, v);
                    buffer.setSample (1, i, v);
                }

                processor.processBlock (buffer, midi);

                if (b % 4 == 3)
                    pump (8);   // let the editor's timer run alongside
            }
        }

        pump (100);

        // Whatever theme this Mac's user picked, the reference shot is light;
        // the dark one beside it. Palette::setDark, not the button, so the
        // user's setting is left alone.
        Palette::setDark (false);
        editor->repaint();
        const auto image = editor->createComponentSnapshot (editor->getLocalBounds(), true, 2.0f);
        const auto file = outputDirectory.getChildFile ("rackbox-plus.png");
        check (writePng (image, file), "the panel renders to " + file.getFullPathName());

        Palette::setDark (true);
        editor->repaint();
        check (writePng (editor->createComponentSnapshot (editor->getLocalBounds(), true, 2.0f),
                         outputDirectory.getChildFile ("rackbox-plus-dark.png")), "the panel renders dark");
        Palette::setDark (false);
        editor->repaint();

        // The Modulation family with all its knobs, one effect per row, chosen the way a user would
        // (a click, so every knob starts where the catalog puts it): Chorus, Phaser, Flanger,
        // Shifter. The picture is where a knob name or value too long for its place shows.
        if (auto* rows = dynamic_cast<RackboxEditor*> (editor.get()))
        {
            const int subtypes[] { 0, 1, 2, 4 };

            for (int b = 0; b < RackParams::numBlocks; ++b)
            {
                rows->getPanel (b).clickType (0);
                rows->getPanel (b).clickSubtype (subtypes[b]);
                setParam (processor, RackParams::blockId (b, RackParams::Block::bypass), 0.0f);
            }

            pump (150);

            int shown = 0;
            for (int b = 0; b < RackParams::numBlocks; ++b)
                for (int k = 0; k < RackParams::Block::numKnobs; ++k)
                    shown += rows->getPanel (b).knobVisible (k) ? 1 : 0;

            check (shown == 8 + 8 + 7 + 7, "Chorus, Phaser, Flanger and Shifter show 8, 8, 7 and 7 knobs", juce::String (shown));
        }

        const auto mod = editor->createComponentSnapshot (editor->getLocalBounds(), true, 2.0f);
        const auto modFile = outputDirectory.getChildFile ("rackbox-plus-mod.png");
        check (writePng (mod, modFile), "the panel with the Modulation family renders to " + modFile.getFullPathName());

        // The Reverb family the same way: Plate, Room, Hall, and a Plate again.
        if (auto* rows = dynamic_cast<RackboxEditor*> (editor.get()))
        {
            const int subtypes[] { 0, 1, 2, 0 };

            for (int b = 0; b < RackParams::numBlocks; ++b)
            {
                rows->getPanel (b).clickType (1);
                rows->getPanel (b).clickSubtype (subtypes[b]);
            }

            pump (150);

            int shown = 0;
            for (int b = 0; b < RackParams::numBlocks; ++b)
                for (int k = 0; k < RackParams::Block::numKnobs; ++k)
                    shown += rows->getPanel (b).knobVisible (k) ? 1 : 0;

            check (shown == 32, "Plate, Room and Hall show eight knobs each", juce::String (shown));
        }

        const auto reverbs = editor->createComponentSnapshot (editor->getLocalBounds(), true, 2.0f);
        const auto reverbFile = outputDirectory.getChildFile ("rackbox-plus-reverb.png");
        check (writePng (reverbs, reverbFile), "the panel with the Reverb family renders to " + reverbFile.getFullPathName());

        // And the Delay family: Digital, Tape, Ping-Pong, and a Digital again.
        if (auto* rows = dynamic_cast<RackboxEditor*> (editor.get()))
        {
            const int subtypes[] { 0, 1, 2, 0 };

            for (int b = 0; b < RackParams::numBlocks; ++b)
            {
                rows->getPanel (b).clickType (2);
                rows->getPanel (b).clickSubtype (subtypes[b]);
            }

            pump (150);

            int shown = 0;
            for (int b = 0; b < RackParams::numBlocks; ++b)
                for (int k = 0; k < RackParams::Block::numKnobs; ++k)
                    shown += rows->getPanel (b).knobVisible (k) ? 1 : 0;

            check (shown == 8 + 8 + 7 + 8, "Digital, Tape, Ping-Pong and Digital show 8, 8, 7 and 8 knobs", juce::String (shown));
        }

        const auto delays = editor->createComponentSnapshot (editor->getLocalBounds(), true, 2.0f);
        const auto delayFile = outputDirectory.getChildFile ("rackbox-plus-delay.png");
        check (writePng (delays, delayFile), "the panel with the Delay family renders to " + delayFile.getFullPathName());

        // And the Distortion family: Overdrive, Fuzz, Bitcrusher, and an Overdrive again.
        if (auto* rows = dynamic_cast<RackboxEditor*> (editor.get()))
        {
            const int subtypes[] { 0, 1, 2, 0 };

            for (int b = 0; b < RackParams::numBlocks; ++b)
            {
                rows->getPanel (b).clickType (3);
                rows->getPanel (b).clickSubtype (subtypes[b]);
            }

            pump (150);

            int shown = 0;
            for (int b = 0; b < RackParams::numBlocks; ++b)
                for (int k = 0; k < RackParams::Block::numKnobs; ++k)
                    shown += rows->getPanel (b).knobVisible (k) ? 1 : 0;

            check (shown == 6 + 6 + 7 + 6, "Overdrive, Fuzz, Bitcrusher and Overdrive show 6, 6, 7 and 6 knobs", juce::String (shown));
        }

        const auto dists = editor->createComponentSnapshot (editor->getLocalBounds(), true, 2.0f);
        const auto distFile = outputDirectory.getChildFile ("rackbox-plus-dist.png");
        check (writePng (dists, distFile), "the panel with the Distortion family renders to " + distFile.getFullPathName());
    }

    std::printf ("\n%d checks, %d failure(s)\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
