//
//  RackboxPlusCheck.cpp
//  Rackbox Plus
//
//  Measures the engine against signals whose answer is known by arithmetic.
//
//  - The catalog: fourteen effects, every category/subtype pair reaches a
//    distinct one, the printed values are the values the DSP uses.
//  - Every effect, at three knob settings: finite, bounded, audible; reverbs
//    and delays hand back a tail that dies.
//  - The distortions' aliasing: a sine whose harmonics fold back past Nyquist,
//    measured line by line; the anti-aliased shapers' integrals checked against
//    their curves, and their top end against the tone filter alone.
//  - The frequency shifter: sines land where the shift puts them, up and
//    down, at their own level, with the mirror-image sideband cancelled; a
//    shift of 0 keeps every level; feedback rings but stays bounded, holds the
//    level and dies away.
//  - The reverbs' loudness across Size, because a network's level tends to
//    grow with its decay and the input scaling exists to stop that.
//  - The input filters: -3 dB at the cutoff, the Butterworth slope beyond it
//    (against the *warped* digital prototype, not the analogue one), and an
//    open section that is bit-exact.
//  - A bypassed block is a wire, bit for bit, and Mix at 0 is the dry signal.
//  - The crossfade: switching from any effect to any other, including
//    bypass, never steps - measured against the steepest slope either side
//    has on its own - and a measurement control shows the test would notice
//    a step.
//  - Switching every 10 ms for two seconds, faster than a fade takes: still
//    finite, still bounded, still no step.
//  - Chunking: a host block longer than the prepared one gives the same bits
//    as feeding it in pieces.
//
//  Exits non-zero on any failure, so build.sh stops.
//

#include <juce_events/juce_events.h>

#include "Engine/Dsp.h"
#include "Engine/EffectBlock.h"
#include "Engine/EffectCatalog.h"
#include "Engine/Effects/Bitcrusher.h"
#include "Engine/Effects/Chorus.h"
#include "Engine/Effects/DigitalDelay.h"
#include "Engine/Effects/Flanger.h"
#include "Engine/Effects/FreqShifter.h"
#include "Engine/Effects/HallVerb.h"
#include "Engine/Effects/PlateVerb.h"
#include "Engine/Effects/RoomVerb.h"
#include "Engine/Effects/TapeDelay.h"
#include "Engine/Effects/Fuzz.h"
#include "Engine/Effects/Phaser.h"
#include "Engine/Effects/Overdrive.h"
#include "Engine/Effects/PingPongDelay.h"
#include "Engine/Effects/Tremolo.h"
#include "Engine/InputFilters.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <tuple>
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
    constexpr int blockSize = 512;

    using Catalog = EffectCatalog::Unit;

    std::vector<float> noise (int n, float amplitude, unsigned seed)
    {
        std::mt19937 rng (seed);
        std::uniform_real_distribution<float> dist (-amplitude, amplitude);
        std::vector<float> v ((size_t) n);
        for (auto& x : v) x = dist (rng);
        return v;
    }

    std::vector<float> sine (int n, double hz, float amplitude, double startPhase = 0.0)
    {
        std::vector<float> v ((size_t) n);
        for (int i = 0; i < n; ++i)
            v[(size_t) i] = amplitude * (float) std::sin (6.283185307179586 * hz * i / rate + startPhase);
        return v;
    }

    double rms (const float* x, int n)
    {
        double sum = 0.0;
        for (int i = 0; i < n; ++i) sum += (double) x[i] * x[i];
        return std::sqrt (sum / juce::jmax (1, n));
    }

    double toDb (double x) { return 20.0 * std::log10 (juce::jmax (1.0e-12, x)); }

    float maxStep (const float* x, int n)
    {
        float m = 0.0f;
        for (int i = 1; i < n; ++i) m = juce::jmax (m, std::abs (x[i] - x[i - 1]));
        return m;
    }

    bool allFinite (const std::vector<float>& v, float bound)
    {
        for (auto x : v)
            if (! std::isfinite (x) || std::abs (x) > bound) return false;
        return true;
    }

    /** Runs a stereo signal (left = right = input) through a fresh effect and returns the left output. */
    std::vector<float> runEffect (int effectIndex, float a, float b, const std::vector<float>& input)
    {
        auto effect = EffectCatalog::create (effectIndex);
        effect->prepare (rate, blockSize);
        effect->setParameters (a, b);
        effect->reset();

        std::vector<float> left (input), right (input);
        for (size_t start = 0; start < input.size(); start += blockSize)
        {
            const auto n = (int) juce::jmin ((size_t) blockSize, input.size() - start);
            effect->process (left.data() + start, right.data() + start, n);
        }

        return left;
    }

    /** Runs a fresh effect with every knob at its catalog start except the ones given
        ({ knob index, position }). The right channel comes back in `right` if asked for. */
    std::vector<float> runWith (int effectIndex, std::initializer_list<std::pair<int, float>> knobs,
                                const std::vector<float>& input, std::vector<float>* rightOut = nullptr)
    {
        auto effect = EffectCatalog::create (effectIndex);
        effect->prepare (rate, blockSize);

        int c = 0, s = effectIndex;
        while (s >= EffectCatalog::numSubtypes (c)) s -= EffectCatalog::numSubtypes (c++);

        std::array<float, Effect::numKnobs> values {};
        for (size_t k = 0; k < values.size(); ++k)
            values[k] = EffectCatalog::info (c, s).knobs[k].defaultValue;
        for (const auto& [k, v] : knobs)
            values[(size_t) k] = v;

        effect->setKnobs (values);
        effect->reset();

        std::vector<float> left (input), right (input);
        for (size_t start = 0; start < input.size(); start += blockSize)
        {
            const auto n = (int) juce::jmin ((size_t) blockSize, input.size() - start);
            effect->process (left.data() + start, right.data() + start, n);
        }

        if (rightOut != nullptr)
            *rightOut = right;

        return left;
    }

    //==============================================================================
    void checkCatalog()
    {
        int total = 0;
        for (int c = 0; c < EffectCatalog::numCategories; ++c)
            total += EffectCatalog::numSubtypes (c);

        check (total == EffectCatalog::numEffects && total == 14, "the categories add up to 14 effects", juce::String (total));

        std::vector<bool> seen ((size_t) EffectCatalog::numEffects, false);
        bool distinct = true;

        for (int c = 0; c < EffectCatalog::numCategories; ++c)
            for (int s = 0; s < EffectCatalog::numSubtypes (c); ++s)
            {
                const auto i = EffectCatalog::index (c, s);
                distinct = distinct && i >= 0 && i < EffectCatalog::numEffects && ! seen[(size_t) i];
                if (i >= 0 && i < EffectCatalog::numEffects) seen[(size_t) i] = true;
                check (EffectCatalog::create (i) != nullptr, "an effect exists for index " + juce::String (i));
            }

        check (distinct, "every category/subtype pair reaches its own effect");

        check (EffectCatalog::index (1, 3) == EffectCatalog::index (1, 2) && EffectCatalog::index (1, 4) == EffectCatalog::index (1, 2),
               "a subtype past the end of Reverb means Hall");
        check (juce::String (EffectCatalog::info (0, 4).name) == "Shifter", "the frequency Shifter is the fifth Modulation effect");
        check (EffectCatalog::format (Catalog::ShiftHz, 0.5f) == "0.00 Hz" && EffectCatalog::format (Catalog::ShiftHz, 0.55f) == "+1.00 Hz"
               && EffectCatalog::format (Catalog::ShiftHz, 0.75f) == "+125 Hz" && EffectCatalog::format (Catalog::ShiftHz, 0.0f) == "-1000 Hz",
               "Shift runs -1000 to +1000 Hz, 0 in the middle", EffectCatalog::format (Catalog::ShiftHz, 0.55f));
        check (Dsp::Map::shiftHz (0.5f) == 0.0f, "the centre of Shift is exactly 0 Hz");
        check (juce::String (EffectCatalog::info (0, 0).name) == "Chorus" && juce::String (EffectCatalog::info (1, 0).name) == "Plate"
               && juce::String (EffectCatalog::info (2, 2).name) == "Ping-Pong", "the names are where the brief puts them");

        check (EffectCatalog::format (Catalog::TimeMs, 0.5f) == "100 ms", "Time 0.5 prints 100 ms (10 ms * 100^0.5)",
               EffectCatalog::format (Catalog::TimeMs, 0.5f));
        check (EffectCatalog::format (Catalog::TimeMs, 1.0f) == "1.00 s", "Time 1.0 prints 1.00 s", EffectCatalog::format (Catalog::TimeMs, 1.0f));
        check (EffectCatalog::format (Catalog::Bits, 0.0f) == "16.0 bit" && EffectCatalog::format (Catalog::Bits, 1.0f) == "2.0 bit",
               "Bits runs 16 to 2");
        check (EffectCatalog::format (Catalog::Percent, 0.35f) == "35 %", "Percent");

        // No knob of any effect ever prints more than two decimals, at any of 1001 positions.
        {
            juce::String worst;
            for (int c = 0; c < EffectCatalog::numCategories; ++c)
                for (int sub = 0; sub < EffectCatalog::numSubtypes (c); ++sub)
                    for (const auto& knob : EffectCatalog::info (c, sub).knobs)
                        if (knob.label != nullptr)
                            for (int i = 0; i <= 1000; ++i)
                            {
                                const auto text = EffectCatalog::format (knob, (float) i / 1000.0f);
                                const auto number = text.upToFirstOccurrenceOf (" ", false, false);
                                if (number.contains (".") && number.fromFirstOccurrenceOf (".", false, false).length() > 2)
                                    worst = juce::String (knob.label) + ": " + text;
                            }

            check (worst.isEmpty(), "no knob ever prints more than two decimals", worst);
        }
    }

    //==============================================================================
    void checkEveryEffect()
    {
        const auto input = noise ((int) (2.0 * rate), 0.3f, 1);
        const auto inputRms = rms (input.data(), (int) input.size());
        const float settings[][2] { { 0.0f, 0.0f }, { 0.5f, 0.5f }, { 1.0f, 1.0f } };

        for (int c = 0; c < EffectCatalog::numCategories; ++c)
            for (int s = 0; s < EffectCatalog::numSubtypes (c); ++s)
            {
                const auto index = EffectCatalog::index (c, s);
                const juce::String name = juce::String (EffectCatalog::categoryName (c)) + " " + EffectCatalog::info (c, s).name;

                for (auto& setting : settings)
                {
                    const auto out = runEffect (index, setting[0], setting[1], input);
                    const auto label = name + " at " + juce::String (setting[0], 1) + "/" + juce::String (setting[1], 1);

                    check (allFinite (out, 8.0f), label + " is finite and bounded");
                }

                const auto mid = runEffect (index, 0.5f, 0.5f, input);
                check (toDb (rms (mid.data(), (int) mid.size())) > -60.0, name + " is audible on noise",
                       juce::String (toDb (rms (mid.data(), (int) mid.size())), 1) + " dB");

                std::printf ("  %-22s  out/in %+6.1f dB\n", name.toRawUTF8(),
                             toDb (rms (mid.data(), (int) mid.size()) / inputRms));
            }
    }

    /** Energy of a real signal at an exact DFT bin (both halves of the spectrum), by Goertzel. */
    double binEnergy (const float* x, int n, int bin)
    {
        const auto w = 6.283185307179586 * bin / n;
        const auto coefficient = 2.0 * std::cos (w);
        double s1 = 0.0, s2 = 0.0;

        for (int i = 0; i < n; ++i)
        {
            const auto s0 = (double) x[i] + coefficient * s1 - s2;
            s2 = s1;
            s1 = s0;
        }

        const auto power = s1 * s1 + s2 * s2 - coefficient * s1 * s2;   // |X[bin]|^2
        return (bin == 0 ? 1.0 : 2.0) * power / n;
    }

    struct Aliasing { double all, below10k; };   // dB relative to the harmonics

    /** A drive-heavy shaper makes harmonics far above Nyquist, and the ones it cannot represent
        fold back as tones that belong to no harmonic series. A sine of a whole number of Hz gives
        a whole number of cycles in a one-second window, and then everything the shaper makes is
        a line at k times that frequency, folded back into 0..Nyquist and sitting on its own bin.
        The lines below Nyquist are the harmonics; the folded ones are the aliasing. Those below
        10 kHz are the ones heard as grit rather than masked by the harmonics' own top end. */
    Aliasing measureAliasing (int effectIndex, float a, float b, int hz)
    {
        const int window = (int) rate, nyquist = window / 2;
        const auto out = runEffect (effectIndex, a, b, sine ((int) (1.5 * rate), (double) hz, 0.5f));
        const auto* x = out.data() + (int) (0.5 * rate);   // past the filters' settling

        double harmonics = 0.0, all = 0.0, below10k = 0.0;

        for (int k = 1; k <= 400; ++k)
        {
            auto bin = (int) (((long long) k * hz) % window);
            const bool folded = (long long) k * hz >= nyquist;
            if (bin > nyquist) bin = window - bin;

            const auto energy = binEnergy (x, window, bin);

            if (! folded)        harmonics += energy;
            else                 all += energy;

            if (folded && bin < 10000)
                below10k += energy;
        }

        const auto db = [&] (double e) { return 10.0 * std::log10 (juce::jmax (1.0e-20, e) / juce::jmax (1.0e-20, harmonics)); };
        return { db (all), db (below10k) };
    }

    void checkAliasing()
    {
        const float drives[] { 0.5f, 1.0f };

        for (int s = 0; s < 2; ++s)
        {
            const auto index = EffectCatalog::index (3, s);
            const juce::String name = EffectCatalog::info (3, s).name;

            for (auto drive : drives)
            {
                // 4999 Hz shares no factor with 48 kHz, so no alias can land on a harmonic's bin.
                const auto result = measureAliasing (index, drive, 1.0f, 4999);
                std::printf ("  %-10s drive %.1f, 4999 Hz: aliases %+6.1f dB, below 10 kHz %+6.1f dB\n",
                             name.toRawUTF8(), drive, result.all, result.below10k);

                // Without anti-aliasing both sit near -16 dB at full drive; with it, below -35.
                check (result.below10k < -30.0, name + " at drive " + juce::String (drive, 1) + " folds back little below 10 kHz",
                       juce::String (result.below10k, 1) + " dB");
            }

            const auto silent = runEffect (index, 1.0f, 1.0f, std::vector<float> ((size_t) blockSize * 4, 0.0f));
            check (std::all_of (silent.begin(), silent.end(), [] (float v) { return v == 0.0f; }),
                   name + " turns silence into silence (the shaper rests at zero, bias included)");

            // Quiet enough to stay on the linear part of the curve: how much top end the shaper costs.
            const auto level = [&] (double hz)
            {
                const auto out = runEffect (index, 0.0f, 1.0f, sine ((int) rate, hz, 0.01f));
                return rms (out.data() + (int) (0.5 * rate), (int) (0.5 * rate));
            };

            const auto top = toDb (level (12000.0) / level (1000.0));
            std::printf ("  %-10s 12 kHz vs 1 kHz at drive 0, tone open: %+5.1f dB\n", name.toRawUTF8(), top);

            // The tone filter alone is -1.6 dB here; the averaging alone would add -3 dB more.
            check (top > -2.5, name + " keeps its top end (the averaging's loss is lifted back)", juce::String (top, 1) + " dB");
        }

        // The integrals are only right if their slope is the curve: compare a central difference.
        const auto slopeError = [] (auto value, auto integral)
        {
            double worst = 0.0;
            for (double x = -8.0; x <= 8.0; x += 0.0137)
            {
                const auto h = 1.0e-5;
                worst = juce::jmax (worst, std::abs ((integral (x + h) - integral (x - h)) / (2.0 * h) - value (x)));
            }
            return worst;
        };

        const auto softError = slopeError (Dsp::SoftClipShape::value, Dsp::SoftClipShape::integral);
        const auto fuzzError = slopeError (FuzzShape::value, FuzzShape::integral);
        check (softError < 1.0e-5, "softClip's integral has softClip as its slope", juce::String (softError));
        check (fuzzError < 1.0e-5, "the Fuzz curve's integral has the curve as its slope", juce::String (fuzzError));
    }

    void checkTails()
    {
        // An impulse, then silence: reverbs and delays must ring and then die.
        const int total = (int) (6.0 * rate);
        std::vector<float> impulse ((size_t) total, 0.0f);
        impulse[100] = 0.8f;

        for (int c = 1; c <= 2; ++c)
            for (int s = 0; s < EffectCatalog::numSubtypes (c); ++s)
            {
                const auto index = EffectCatalog::index (c, s);
                const juce::String name = juce::String (EffectCatalog::categoryName (c)) + " " + EffectCatalog::info (c, s).name;

                const auto a = c == 1 ? 0.3f : 0.4f;   // Size 0.3 / Time ~ 63 ms
                const auto out = runEffect (index, a, 0.4f, impulse);

                const auto early = rms (out.data() + (int) (0.05 * rate), (int) (0.5 * rate));
                const auto late  = rms (out.data() + (int) (5.0 * rate), (int) (0.5 * rate));

                check (early > 1.0e-4, name + " rings after an impulse", juce::String (toDb (early), 1) + " dB");
                check (toDb (late) < toDb (early) - 20.0, name + " has died away 20 dB by 5 s",
                       juce::String (toDb (early), 1) + " -> " + juce::String (toDb (late), 1) + " dB");
            }
    }

    //==============================================================================
    float shiftKnob (double hz)   { return (float) (0.5 + 0.5 * std::cbrt (hz / 1000.0)); }

    /** Energy in a few bins either side of a frequency (the knob's cube root makes the shift a
        hair off a whole number of Hz, so it is not quite on one bin). */
    double bandEnergy (const float* x, int n, double hz)
    {
        double e = 0.0;
        for (int b = (int) std::round (hz) - 3; b <= (int) std::round (hz) + 3; ++b)
            if (b > 0 && b < n / 2)
                e += binEnergy (x, n, b);
        return e;
    }

    void checkFreqShifter()
    {
        const auto index = EffectCatalog::index (0, 4);

        // A sine moved by the shift, at its own level, and the mirror image (what a ring modulator
        // would also make) cancelled by the quadrature pair - in the bass, the middle and the top.
        struct Case { double hz, shift; };
        double worstSideband = -1.0e9, worstLevel = 0.0;

        for (const auto c : { Case { 100.0, 40.0 }, Case { 1000.0, 100.0 }, Case { 1000.0, -300.0 },
                              Case { 8000.0, 1000.0 }, Case { 15000.0, -1000.0 } })
        {
            const auto out = runWith (index, { { 0, shiftKnob (c.shift) }, { 2, 0.0f } }, sine ((int) (1.5 * rate), c.hz, 0.5f));
            const auto* x = out.data() + (int) (0.5 * rate);
            const auto wanted = bandEnergy (x, (int) rate, c.hz + c.shift);
            const auto mirror = bandEnergy (x, (int) rate, c.hz - c.shift);
            const auto sideband = 10.0 * std::log10 (mirror / wanted);
            const auto level = 10.0 * std::log10 (wanted / (0.5 * 0.5 / 2.0 * rate));   // binEnergy sums over the window

            std::printf ("  %5.0f Hz shifted %+5.0f: %+.2f dB at %.0f Hz, mirror %+.1f dB\n", c.hz, c.shift, level, c.hz + c.shift, sideband);
            worstSideband = juce::jmax (worstSideband, sideband);
            worstLevel = juce::jmax (worstLevel, std::abs (level));
        }

        check (worstSideband < -60.0, "the mirror-image sideband is cancelled to 60 dB or more", juce::String (worstSideband, 1) + " dB");
        check (worstLevel < 0.1, "a shifted sine keeps its level", juce::String (worstLevel, 2) + " dB");

        // At 0 Hz nothing moves and nothing gets louder or quieter (the pair is allpass).
        {
            const auto in = noise ((int) rate, 0.5f, 31);
            const auto out = runWith (index, { { 0, 0.5f }, { 2, 0.0f } }, in);
            const auto db = toDb (rms (out.data(), (int) out.size()) / rms (in.data(), (int) in.size()));
            check (std::abs (db) < 0.1, "at a shift of 0 Hz the level is unchanged", juce::String (db, 2) + " dB");
        }

        // Feedback: bounded at every setting, roughly level across the knob, and a tail that dies.
        {
            const auto in = noise ((int) (2.0 * rate), 0.3f, 32);
            const auto inRms = rms (in.data(), (int) in.size());
            double low = 1.0e9, high = -1.0e9;
            bool bounded = true;

            for (const auto shift : { 0.0, 3.0, 200.0, -700.0 })
                for (const auto fb : { 0.0f, 0.5f, 1.0f })
                {
                    const auto out = runWith (index, { { 0, shiftKnob (shift) }, { 2, fb } }, in);
                    bounded = bounded && allFinite (out, 4.0f);
                    const auto db = toDb (rms (out.data() + (int) rate, (int) rate) / inRms);
                    low = juce::jmin (low, db);
                    high = juce::jmax (high, db);
                }

            std::printf ("  freq shift on noise, every shift and feedback: out/in %+.1f to %+.1f dB\n", low, high);
            check (bounded, "with feedback the shifter stays finite and bounded");
            check (low > -4.0 && high < 4.0, "feedback keeps the level within 4 dB", juce::String (low, 1) + " to " + juce::String (high, 1) + " dB");

            std::vector<float> impulse ((size_t) (3.0 * rate), 0.0f);
            impulse[100] = 0.8f;
            const auto ring = runWith (index, { { 0, shiftKnob (3.0) }, { 2, 1.0f } }, impulse);
            const auto early = rms (ring.data() + (int) (0.01 * rate), (int) (0.2 * rate));
            const auto late = rms (ring.data() + (int) (2.5 * rate), (int) (0.5 * rate));
            std::printf ("  freq shift at full feedback, impulse: %.1f dB early, %.1f dB after 2.5 s\n", toDb (early), toDb (late));
            check (toDb (late) < toDb (early) - 40.0, "at full feedback an impulse still dies away", juce::String (toDb (late), 1) + " dB");
        }
    }

    //==============================================================================
    /** Level of a run against its input, in dB, over the last `seconds` of it. */
    double levelDb (const std::vector<float>& out, const std::vector<float>& in, double seconds = 0.2)
    {
        const auto n = (int) (seconds * rate);
        const auto from = (int) out.size() - n;
        return toDb (rms (out.data() + from, n) / rms (in.data() + from, n));
    }

    void checkModulationKnobs()
    {
        const auto chorus = EffectCatalog::index (0, 0), phaser = EffectCatalog::index (0, 1), flanger = EffectCatalog::index (0, 2);
        const auto tremolo = EffectCatalog::index (0, 3), shifter = EffectCatalog::index (0, 4);

        // Every knob anywhere: finite and bounded, on loud noise, at the corners and at random.
        {
            const auto in = noise ((int) (0.5 * rate), 0.7f, 41);
            std::mt19937 random (42);
            std::uniform_real_distribution<float> unit (0.0f, 1.0f);
            bool fine = true;
            juce::String where;

            for (int e : { chorus, phaser, flanger, tremolo, shifter })
                for (int trial = 0; trial < 26; ++trial)
                {
                    std::array<float, Effect::numKnobs> k {};
                    for (auto& v : k) v = trial == 0 ? 0.0f : (trial == 1 ? 1.0f : unit (random));

                    const auto out = runWith (e, { { 0, k[0] }, { 1, k[1] }, { 2, k[2] }, { 3, k[3] },
                                                   { 4, k[4] }, { 5, k[5] }, { 6, k[6] }, { 7, k[7] } }, in);
                    if (! allFinite (out, 8.0f))
                    {
                        fine = false;
                        where = juce::String (e) + " trial " + juce::String (trial);
                    }
                }

            check (fine, "every Modulation effect stays finite and bounded at any knob setting", where);
        }

        // Chorus Delay and Voices: with no sweep, an impulse comes back after the delay, and each
        // further voice 40 % later still.
        {
            std::vector<float> impulse ((size_t) rate, 0.0f);
            impulse[100] = 1.0f;

            for (const auto ms : { 5.0f, 14.0f, 30.0f })
            {
                const auto out = runWith (chorus, { { 1, 0.0f }, { 2, Chorus::delayMs.position (ms) }, { 3, Chorus::voices.position (1.0f) } }, impulse);
                int loudest = 101;
                for (int i = 101; i < (int) out.size(); ++i)
                    if (std::abs (out[(size_t) i]) > std::abs (out[(size_t) loudest])) loudest = i;

                const auto expected = 100 + (int) std::round (ms * 0.001 * rate);
                check (std::abs (loudest - expected) <= 1, "Chorus Delay " + juce::String (ms, 0) + " ms puts the copy there",
                       juce::String ((loudest - 100) * 1000.0 / rate, 2) + " ms");
            }

            const auto four = runWith (chorus, { { 1, 0.0f }, { 2, Chorus::delayMs.position (10.0f) }, { 3, Chorus::voices.position (4.0f) } }, impulse);
            bool all = true;
            for (const auto factor : { 1.0f, 1.4f, 1.8f, 2.2f })
                all = all && std::abs (four[(size_t) (100 + (int) std::round (factor * 0.01 * rate))]) > 0.05f;
            check (all, "four Chorus voices come back at 1, 1.4, 1.8 and 2.2 times the delay");
        }

        // Chorus Tone and Low Cut act on the copies only: at their far ends the copies are gone
        // from a tone outside the band, and what is left is the dry half (-6 dB).
        {
            const auto low = sine ((int) rate, 50.0, 0.5f), high = sine ((int) rate, 12000.0, 0.5f);
            const auto lowCut = levelDb (runWith (chorus, { { 1, 0.0f }, { 7, 1.0f } }, low), low);
            const auto tone = levelDb (runWith (chorus, { { 1, 0.0f }, { 6, 0.0f } }, high), high);
            check (std::abs (lowCut + 6.02) < 1.0, "Chorus Low Cut at 1 kHz leaves a 50 Hz tone dry (-6 dB)", juce::String (lowCut, 2) + " dB");
            check (std::abs (tone + 6.02) < 1.0, "Chorus Tone at 1 kHz leaves a 12 kHz tone dry (-6 dB)", juce::String (tone, 2) + " dB");
        }

        // Phaser Center: two stages, no sweep and no feedback make one notch, at Center.
        // Stages: each pair of stages is one notch across the audio band.
        {
            const auto response = [&] (double hz, int stages, float center)
            {
                const auto in = sine ((int) (0.3 * rate), hz, 0.5f);
                return levelDb (runWith (phaser, { { 1, 0.0f }, { 2, Phaser::centerHz.position (center) },
                                                   { 3, Phaser::feedback.position (0.0f) }, { 4, Phaser::stages.position ((float) stages) } }, in), in, 0.1);
            };

            for (const auto center : { 400.0f, 1600.0f })
                check (response (center, 2, center) < -30.0 && response (center * 4.0, 2, center) > -3.0,
                       "Phaser Center " + juce::String (center, 0) + " Hz is where two stages notch",
                       juce::String (response (center, 2, center), 1) + " dB there, " + juce::String (response (center * 4.0, 2, center), 1) + " dB two octaves up");

            for (const auto stages : { 2, 6, 12 })
            {
                int notches = 0;
                double before = 0.0, previous = 0.0;
                for (int i = 0; i <= 160; ++i)
                {
                    const auto db = response (20.0 * std::pow (1000.0, i / 160.0), stages, 800.0f);
                    if (i >= 2 && previous < before && previous < db && previous < -12.0)
                        ++notches;
                    before = previous;
                    previous = db;
                }

                check (notches == stages / 2, juce::String (stages) + " Phaser stages make " + juce::String (stages / 2) + " notches",
                       juce::String (notches) + " found");
            }
        }

        // Flanger Delay: without sweep or feedback the first notch is at 1 / (2 x delay).
        // Tone: with heavy feedback, a dark loop rings out sooner.
        {
            const auto notch = sine ((int) (0.3 * rate), 500.0, 0.5f), peak = sine ((int) (0.3 * rate), 1000.0, 0.5f);
            const std::initializer_list<std::pair<int, float>> oneMs { { 1, 0.0f }, { 2, Flanger::delayMs.position (1.0f) }, { 3, Flanger::feedback.position (0.0f) } };
            const auto atNotch = levelDb (runWith (flanger, oneMs, notch), notch, 0.1);
            const auto atPeak = levelDb (runWith (flanger, oneMs, peak), peak, 0.1);
            check (atNotch < -30.0 && atPeak > -1.0, "Flanger Delay 1 ms notches 500 Hz and passes 1 kHz",
                   juce::String (atNotch, 1) + " / " + juce::String (atPeak, 1) + " dB");

            std::vector<float> impulse ((size_t) rate, 0.0f);
            impulse[100] = 1.0f;
            const auto tail = [&] (float tone)
            {
                const auto out = runWith (flanger, { { 1, 0.0f }, { 3, Flanger::feedback.position (0.95f) }, { 6, Flanger::toneHz.position (tone) } }, impulse);
                return toDb (rms (out.data() + (int) (0.1 * rate), (int) (0.1 * rate)));
            };
            check (tail (1000.0f) < tail (20000.0f) - 6.0, "Flanger Tone darkens the loop so the ring dies sooner",
                   juce::String (tail (1000.0f), 1) + " vs " + juce::String (tail (20000.0f), 1) + " dB");
        }

        // Tremolo: Stereo 100 % is an auto-pan (the two gains add to one); Square spends most of
        // its time at the ends; Skew +100 % keeps the gain on its way down for 95 % of a cycle.
        {
            const std::vector<float> ones ((size_t) rate, 1.0f);
            std::vector<float> right;
            const auto leftGain = runWith (tremolo, { { 0, 0.6f }, { 1, 1.0f }, { 3, 1.0f } }, ones, &right);
            float worst = 0.0f;
            for (size_t i = 0; i < ones.size(); ++i)
                worst = juce::jmax (worst, std::abs (leftGain[i] + right[i] - 1.0f));
            check (worst < 1.0e-3f, "Tremolo Stereo 100 % is an auto-pan: left and right add up to one", juce::String (worst, 5));

            const auto atEnds = [&] (float shape)
            {
                const auto g = runWith (tremolo, { { 0, 0.6f }, { 1, 1.0f }, { 2, shape } }, ones);
                int n = 0;
                for (auto v : g) n += (v < 0.05f || v > 0.95f) ? 1 : 0;
                return (double) n / (double) g.size();
            };
            check (atEnds (1.0f) > 0.6 && atEnds (0.0f) < 0.3, "Tremolo Square sits at the ends, Sine passes through",
                   juce::String (atEnds (1.0f), 2) + " vs " + juce::String (atEnds (0.0f), 2));

            const auto skewed = runWith (tremolo, { { 0, 0.6f }, { 1, 1.0f }, { 4, 1.0f } }, ones);
            int below = 0;
            for (auto v : skewed) below += v < 0.5f ? 1 : 0;
            const auto share = (double) below / (double) skewed.size();
            check (std::abs (share - 0.95) < 0.03, "Tremolo Skew +100 % keeps the gain low for 95 % of a cycle", juce::String (share, 3));
        }

        // Shifter: Fine adds exactly its Hz; Spread 100 % turns the right channel the other way;
        // Loop sets the echo spacing.
        {
            const auto in = sine ((int) (1.5 * rate), 1000.0, 0.5f);
            std::vector<float> right;
            const auto left = runWith (shifter, { { 0, shiftKnob (100.0) }, { 2, 0.0f }, { 4, 1.0f } }, in, &right);
            const auto* l = left.data() + (int) (0.5 * rate);
            const auto* r = right.data() + (int) (0.5 * rate);
            check (bandEnergy (l, (int) rate, 1100.0) > 100.0 * bandEnergy (l, (int) rate, 900.0)
                   && bandEnergy (r, (int) rate, 900.0) > 100.0 * bandEnergy (r, (int) rate, 1100.0),
                   "Shifter Spread 100 % moves left up and right down");

            const auto fine = runWith (shifter, { { 0, 0.5f }, { 1, FreqShifter::fineHz.position (5.0f) }, { 2, 0.0f } }, in);
            const auto* f = fine.data() + (int) (0.5 * rate);
            check (bandEnergy (f, (int) rate, 1005.0) > 100.0 * bandEnergy (f, (int) rate, 1000.0),
                   "Shifter Fine +5 Hz moves 1 kHz to 1005 Hz");

            std::vector<float> impulse ((size_t) rate, 0.0f);
            impulse[100] = 1.0f;
            const auto echoes = runWith (shifter, { { 0, 0.5f }, { 2, 0.6f }, { 3, FreqShifter::loopMs.position (50.0f) } }, impulse);
            const auto window = [&] (double at) { return rms (echoes.data() + (int) (at * rate), (int) (0.01 * rate)); };
            check (window (0.052) > 10.0 * window (0.03), "Shifter Loop 50 ms brings the first echo back after 50 ms",
                   juce::String (toDb (window (0.052)), 1) + " vs " + juce::String (toDb (window (0.03)), 1) + " dB");
        }
    }

    //==============================================================================
    /** RMS of `seconds` of a run starting at `at` seconds. */
    double windowRms (const std::vector<float>& v, double at, double seconds = 0.05)
    {
        return rms (v.data() + (int) (at * rate), (int) (seconds * rate));
    }

    /** When a response first reaches 1 % of its peak, in ms. */
    double arrivalMs (const std::vector<float>& v)
    {
        float peak = 0.0f;
        for (auto x : v) peak = juce::jmax (peak, std::abs (x));
        for (size_t i = 0; i < v.size(); ++i)
            if (std::abs (v[i]) > 0.01f * peak)
                return (double) i * 1000.0 / rate;
        return -1.0;
    }

    /** The share of a sine's energy that a reverb's modulation moved off its own frequency, in dB. */
    double smearDb (int effectIndex, std::initializer_list<std::pair<int, float>> knobs)
    {
        const auto out = runWith (effectIndex, knobs, sine ((int) (3.0 * rate), 1000.0, 0.3f));
        const auto* x = out.data() + (int) (2.0 * rate);
        double total = 0.0;
        for (int i = 0; i < (int) rate; ++i) total += (double) x[i] * x[i];
        const auto tone = bandEnergy (x, (int) rate, 1000.0);
        return 10.0 * std::log10 (juce::jmax (1.0e-20, total - tone) / total);
    }

    void checkReverbKnobs()
    {
        const auto plate = EffectCatalog::index (1, 0), room = EffectCatalog::index (1, 1), hall = EffectCatalog::index (1, 2);

        std::vector<float> impulse ((size_t) (3.0 * rate), 0.0f);
        impulse[(size_t) (0.01 * rate)] = 1.0f;

        // Every knob anywhere: finite and bounded.
        {
            const auto in = noise ((int) (0.5 * rate), 0.7f, 51);
            std::mt19937 random (52);
            std::uniform_real_distribution<float> unit (0.0f, 1.0f);
            bool fine = true;
            juce::String where;

            for (int e : { plate, room, hall })
                for (int trial = 0; trial < 20; ++trial)
                {
                    std::array<float, Effect::numKnobs> k {};
                    for (auto& v : k) v = trial == 0 ? 0.0f : (trial == 1 ? 1.0f : unit (random));
                    const auto out = runWith (e, { { 0, k[0] }, { 1, k[1] }, { 2, k[2] }, { 3, k[3] },
                                                   { 4, k[4] }, { 5, k[5] }, { 6, k[6] }, { 7, k[7] } }, in);
                    if (! allFinite (out, 8.0f)) { fine = false; where = juce::String (e) + " trial " + juce::String (trial); }
                }

            check (fine, "every Reverb stays finite and bounded at any knob setting", where);
        }

        // Decay is RT60: half of it after the start, the tail is 30 dB down.
        for (const auto& [index, seconds, range] : { std::tuple { room, 1.0f, RoomVerb::decaySeconds },
                                                     std::tuple { hall, 4.0f, HallVerb::decaySeconds } })
        {
            std::vector<float> longImpulse ((size_t) (6.0 * rate), 0.0f);
            longImpulse[100] = 1.0f;
            auto out = runWith (index, { { 1, range.position (seconds) }, { 2, 0.0f }, { 5, 0.0f } }, longImpulse);

            // Measured below 1 kHz, as RT60 usually is: the damping filter in the loop (14 kHz even
            // at Damp 0) makes the top die sooner by design, and a broadband level would count it.
            Dsp::OnePole band;
            band.setCutoff (1000.0f, rate);
            for (auto& v : out) v = band.process (v);

            const auto start = 0.15, drop = toDb (windowRms (out, start + 0.5 * seconds, 0.1) / windowRms (out, start, 0.1));
            std::printf ("  %s Decay %.1f s: %.1f dB after %.2f s\n", index == room ? "Room" : "Hall", seconds, drop, 0.5 * seconds);
            check (std::abs (drop + 30.0) < 3.0, (index == room ? "Room" : "Hall") + juce::String (" Decay ") + juce::String (seconds, 1)
                   + " s falls 30 dB in half that time", juce::String (drop, 1) + " dB");
        }

        // Size moves the first arrival by the shortest line's change (7 ms x 1.5 for the Room);
        // Pre-Delay moves it by its own milliseconds, on every reverb.
        {
            const auto small = arrivalMs (runWith (room, { { 0, 0.0f } }, impulse));
            const auto big = arrivalMs (runWith (room, { { 0, 1.0f } }, impulse));
            check (std::abs (big - small - 10.5) < 2.0, "Room Size 50 -> 200 % moves the first reflection 10.5 ms later",
                   juce::String (big - small, 1) + " ms");

            for (const auto& [index, knob, name] : { std::tuple { plate, 2, "Plate" }, std::tuple { room, 3, "Room" }, std::tuple { hall, 3, "Hall" } })
            {
                const auto none = arrivalMs (runWith (index, { { knob, 0.0f } }, impulse));
                const auto later = arrivalMs (runWith (index, { { knob, FdnReverb::preDelayMs.position (100.0f) } }, impulse));
                check (std::abs (later - none - 100.0) < 1.0, juce::String (name) + " Pre-Delay 100 ms comes 100 ms later",
                       juce::String (later - none, 2) + " ms");
            }
        }

        // Early: reflections before the network's first output.
        {
            const auto none = windowRms (runWith (room, { { 4, 0.0f } }, impulse), 0.01, 0.005);
            const auto some = windowRms (runWith (room, { { 4, 1.0f } }, impulse), 0.01, 0.005);
            check (some > 1.0e-3 && none < 1.0e-6, "Room Early puts reflections ahead of the network",
                   juce::String (toDb (none), 1) + " -> " + juce::String (toDb (some), 1) + " dB in the first 5 ms");
        }

        // Mod: a held tone is smeared off its frequency only when the read positions wander.
        for (const auto& [index, knob, name] : { std::tuple { plate, 5, "Plate" }, std::tuple { room, 5, "Room" } })
        {
            const auto still = smearDb (index, { { knob, 0.0f } }), moving = smearDb (index, { { knob, 1.0f } });
            check (moving > still + 10.0, juce::String (name) + " Mod spreads a held tone (none keeps it pure)",
                   juce::String (still, 1) + " -> " + juce::String (moving, 1) + " dB");
        }

        // Low Cut and Width on the return.
        for (const auto& [index, lowCutKnob, widthKnob, name] : { std::tuple { plate, 6, 7, "Plate" }, std::tuple { hall, 6, 7, "Hall" } })
        {
            const auto bass = sine ((int) (2.0 * rate), 50.0, 0.5f);
            const auto open = toDb (windowRms (runWith (index, {}, bass), 1.5, 0.3));
            const auto cut = toDb (windowRms (runWith (index, { { lowCutKnob, 1.0f } }, bass), 1.5, 0.3));
            check (cut < open - 20.0, juce::String (name) + " Low Cut at 1 kHz removes a 50 Hz tone from the reverb",
                   juce::String (open, 1) + " -> " + juce::String (cut, 1) + " dB");

            const auto in = noise ((int) rate, 0.5f, 53);
            std::vector<float> r0, r1, r2;
            const auto l0 = runWith (index, { { widthKnob, 0.0f } }, in, &r0);
            float worst = 0.0f;
            for (size_t i = 0; i < l0.size(); ++i) worst = juce::jmax (worst, std::abs (l0[i] - r0[i]));
            check (worst < 1.0e-6f, juce::String (name) + " Width 0 is mono");

            const auto side = [] (const std::vector<float>& l, const std::vector<float>& r)
            {
                double e = 0.0;
                for (size_t i = (size_t) (0.5 * rate); i < l.size(); ++i) e += 0.25 * (double) (l[i] - r[i]) * (l[i] - r[i]);
                return std::sqrt (e);
            };
            const auto l1 = runWith (index, {}, in, &r1);
            const auto l2 = runWith (index, { { widthKnob, 1.0f } }, in, &r2);
            const auto wider = toDb (side (l2, r2) / side (l1, r1));
            check (std::abs (wider - 3.52) < 0.1, juce::String (name) + " Width 150 % makes the sides 3.5 dB louder", juce::String (wider, 2) + " dB");
        }

        // Plate Bandwidth: a 12 kHz tone hardly reaches a 1 kHz plate.
        {
            const auto hiss = sine ((int) (2.0 * rate), 12000.0, 0.5f);
            const auto open = toDb (windowRms (runWith (plate, { { 3, 1.0f } }, hiss), 1.5, 0.3));
            const auto dark = toDb (windowRms (runWith (plate, { { 3, 0.0f } }, hiss), 1.5, 0.3));
            check (dark < open - 12.0, "Plate Bandwidth at 1 kHz keeps a 12 kHz tone out", juce::String (open, 1) + " -> " + juce::String (dark, 1) + " dB");
        }

        // Plate Diffusion: without it the first 60 ms are separate spikes, with it a smooth onset.
        {
            const auto crest = [&] (float knobValue)
            {
                const auto out = runWith (plate, { { 4, knobValue } }, impulse);
                const auto* x = out.data() + (int) (0.01 * rate);
                const int n = (int) (0.06 * rate);
                float peak = 0.0f;
                for (int i = 0; i < n; ++i) peak = juce::jmax (peak, std::abs (x[i]));
                return toDb (peak / rms (x, n));
            };
            check (crest (0.0f) > crest (1.0f) + 3.0, "Plate Diffusion smooths the onset (no diffusion is spiky)",
                   juce::String (crest (0.0f), 1) + " vs " + juce::String (crest (1.0f), 1) + " dB crest");
        }
    }

    //==============================================================================
    /** Where the loudest sample of a channel is, in ms, between two times. */
    double loudestMs (const std::vector<float>& v, double fromMs, double toMs)
    {
        auto best = (size_t) (fromMs * 0.001 * rate);
        for (auto i = best; i < (size_t) (toMs * 0.001 * rate) && i < v.size(); ++i)
            if (std::abs (v[i]) > std::abs (v[best])) best = i;
        return (double) best * 1000.0 / rate;
    }

    void checkDelayKnobs()
    {
        const auto digital = EffectCatalog::index (2, 0), tape = EffectCatalog::index (2, 1), pingPong = EffectCatalog::index (2, 2);

        // Every knob anywhere, feedback included: finite and bounded.
        {
            const auto in = noise ((int) rate, 0.7f, 61);
            std::mt19937 random (62);
            std::uniform_real_distribution<float> unit (0.0f, 1.0f);
            bool fine = true;
            juce::String where;

            for (int e : { digital, tape, pingPong })
                for (int trial = 0; trial < 20; ++trial)
                {
                    std::array<float, Effect::numKnobs> k {};
                    for (auto& v : k) v = trial == 0 ? 0.0f : (trial == 1 ? 1.0f : unit (random));
                    k[0] = juce::jmin (k[0], 0.4f);   // times up to 63 ms, so the feedback has a second to build
                    const auto out = runWith (e, { { 0, k[0] }, { 1, k[1] }, { 2, k[2] }, { 3, k[3] },
                                                   { 4, k[4] }, { 5, k[5] }, { 6, k[6] }, { 7, k[7] } }, in);
                    if (! allFinite (out, 8.0f)) { fine = false; where = juce::String (e) + " trial " + juce::String (trial); }
                }

            check (fine, "every Delay stays finite and bounded at any knob setting", where);
        }

        std::vector<float> impulse ((size_t) rate, 0.0f);
        impulse[0] = 1.0f;

        // Digital Offset +50 %: at 100 ms the left echo comes at 100 ms, the right at 150.
        {
            std::vector<float> right;
            const auto left = runWith (digital, { { 1, 0.0f }, { 4, DigitalDelay::offset.position (0.5f) } }, impulse, &right);
            check (std::abs (loudestMs (left, 50, 400) - 100.0) < 0.1 && std::abs (loudestMs (right, 50, 400) - 150.0) < 0.1,
                   "Digital Offset +50 % puts the right echo at 1.5 times the left's",
                   juce::String (loudestMs (left, 50, 400), 2) + " / " + juce::String (loudestMs (right, 50, 400), 2) + " ms");
        }

        // Ping-Pong Swing +50 %: the first bounce left at 100 ms, the second right 150 ms after it.
        {
            std::vector<float> right;
            const auto left = runWith (pingPong, { { 5, PingPongDelay::swing.position (0.5f) } }, impulse, &right);
            check (std::abs (loudestMs (left, 50, 200) - 100.0) < 0.1 && std::abs (loudestMs (right, 50, 400) - 250.0) < 0.1,
                   "Ping-Pong Swing +50 % lands the right bounce 150 ms after the left",
                   juce::String (loudestMs (left, 50, 200), 2) + " / " + juce::String (loudestMs (right, 50, 400), 2) + " ms");
        }

        // The cuts are inside the loop: every repeat of an 8 kHz burst loses the High Cut's share
        // again, so the third echo falls further behind the first than the feedback alone makes it.
        {
            std::vector<float> burst ((size_t) rate, 0.0f);
            for (int i = 0; i < (int) (0.02 * rate); ++i)
                burst[(size_t) i] = 0.5f * (float) std::sin (6.283185307179586 * 8000.0 * i / rate);

            const auto fall = [&] (float highCutKnob)
            {
                const auto out = runWith (digital, { { 1, 0.6f }, { 3, highCutKnob } }, burst);
                return toDb (windowRms (out, 0.3, 0.02) / windowRms (out, 0.1, 0.02));
            };

            // Open, two more trips at 57 % feedback: 0.57^2, -9.9 dB.
            const auto open = fall (1.0f), dark = fall (DigitalDelay::highCutHz.position (4000.0f));
            check (std::abs (open + 9.88) < 0.5 && dark < open - 6.0, "Digital High Cut darkens every repeat (third echo vs first)",
                   juce::String (open, 1) + " open, " + juce::String (dark, 1) + " dB at 4 kHz");
        }

        // Tone, Low Cut and High Cut on a single echo.
        {
            const auto high = sine ((int) rate, 12000.0, 0.5f), low = sine ((int) rate, 50.0, 0.5f);
            const auto level = [&] (int index, std::initializer_list<std::pair<int, float>> knobs, const std::vector<float>& in)
            {
                return toDb (windowRms (runWith (index, knobs, in), 0.6, 0.3));
            };

            check (level (digital, { { 1, 0.0f }, { 3, 0.0f } }, high) < level (digital, { { 1, 0.0f } }, high) - 12.0,
                   "Digital High Cut at 1 kHz takes a 12 kHz tone out of the echo");
            check (level (pingPong, { { 1, 0.0f }, { 2, 1.0f } }, low) < level (pingPong, { { 1, 0.0f } }, low) - 20.0,
                   "Ping-Pong Low Cut at 1 kHz takes a 50 Hz tone out of the echo");
            check (level (tape, { { 1, 0.0f }, { 2, 0.0f } }, high) < level (tape, { { 1, 0.0f }, { 2, 1.0f } }, high) - 12.0,
                   "Tape Tone at 1 kHz is a dark echo, at 20 kHz a bright one");
        }

        // Mod Depth, and Wow and Flutter: a held tone's echo is smeared off its pitch only when
        // the read moves.
        {
            const auto smear = [&] (int index, std::initializer_list<std::pair<int, float>> knobs)
            {
                const auto out = runWith (index, knobs, sine ((int) (2.0 * rate), 1000.0, 0.5f));
                const auto* x = out.data() + (int) rate;
                double total = 0.0;
                for (int i = 0; i < (int) rate; ++i) total += (double) x[i] * x[i];
                return 10.0 * std::log10 (juce::jmax (1.0e-20, total - bandEnergy (x, (int) rate, 1000.0)) / total);
            };

            const auto still = smear (digital, { { 1, 0.0f } }), moving = smear (digital, { { 1, 0.0f }, { 7, DigitalDelay::modDepthMs.position (2.0f) } });
            check (moving > still + 20.0, "Digital Mod Depth moves the echo's pitch", juce::String (still, 1) + " -> " + juce::String (moving, 1) + " dB");

            const auto steady = smear (tape, { { 1, 0.0f }, { 4, 0.0f }, { 5, 0.0f } }), worn = smear (tape, { { 1, 0.0f } });
            check (worn > steady + 20.0, "Tape Wow and Flutter at 0 hold the pitch still", juce::String (steady, 1) + " vs " + juce::String (worn, 1) + " dB");

            std::vector<float> r0;
            const auto l0 = runWith (digital, { { 1, 0.5f }, { 4, DigitalDelay::offset.position (0.3f) }, { 5, 0.0f } }, noise ((int) rate, 0.5f, 63), &r0);
            float worst = 0.0f;
            for (size_t i = 0; i < l0.size(); ++i) worst = juce::jmax (worst, std::abs (l0[i] - r0[i]));
            check (worst < 1.0e-6f, "Digital Width 0 is mono, offset and all");
        }

        // Tape Saturation: a moderate tone recirculating picks up harmonics as it is pushed harder.
        // (Moderate: a loud one is clipped at any setting, and more drive then mostly compresses.)
        {
            const auto harmonicsDb = [&] (float saturationKnob)
            {
                const auto out = runWith (tape, { { 0, 0.2f }, { 1, 0.7f }, { 2, 1.0f }, { 3, saturationKnob }, { 4, 0.0f }, { 5, 0.0f } },
                                          sine ((int) (2.0 * rate), 200.0, 0.1f));
                const auto* x = out.data() + (int) rate;
                double h = 0.0;
                for (int k = 2; k <= 10; ++k) h += binEnergy (x, (int) rate, 200 * k);
                return 10.0 * std::log10 (h / binEnergy (x, (int) rate, 200));
            };

            check (harmonicsDb (1.0f) > harmonicsDb (0.0f) + 10.0, "Tape Saturation grits up the recirculating echoes",
                   juce::String (harmonicsDb (0.0f), 1) + " -> " + juce::String (harmonicsDb (1.0f), 1) + " dB");
        }
    }

    //==============================================================================
    /** Energy of harmonics from..to of a tone (one-second window after half a second), in dB
        relative to the fundamental. */
    double harmonicsDb (const std::vector<float>& out, int hz, int from, int to)
    {
        const auto* x = out.data() + (int) (0.5 * rate);
        double h = 0.0;
        for (int k = from; k <= to && k * hz < (int) (0.5 * rate); ++k) h += binEnergy (x, (int) rate, k * hz);
        return 10.0 * std::log10 (juce::jmax (1.0e-30, h) / binEnergy (x, (int) rate, hz));
    }

    void checkDistortionKnobs()
    {
        const auto overdrive = EffectCatalog::index (3, 0), fuzz = EffectCatalog::index (3, 1), crusher = EffectCatalog::index (3, 2);

        // Every knob anywhere: finite and bounded.
        {
            const auto in = noise ((int) (0.5 * rate), 0.7f, 71);
            std::mt19937 random (72);
            std::uniform_real_distribution<float> unit (0.0f, 1.0f);
            bool fine = true;
            juce::String where;

            for (int e : { overdrive, fuzz, crusher })
                for (int trial = 0; trial < 26; ++trial)
                {
                    std::array<float, Effect::numKnobs> k {};
                    for (auto& v : k) v = trial == 0 ? 0.0f : (trial == 1 ? 1.0f : unit (random));
                    const auto out = runWith (e, { { 0, k[0] }, { 1, k[1] }, { 2, k[2] }, { 3, k[3] },
                                                   { 4, k[4] }, { 5, k[5] }, { 6, k[6] }, { 7, k[7] } }, in);
                    if (! allFinite (out, 8.0f)) { fine = false; where = juce::String (e) + " trial " + juce::String (trial); }
                }

            check (fine, "every Distortion stays finite and bounded at any knob setting", where);
        }

        // The integral of the Fuzz curve with a moving bias still has the curve as its slope - also
        // past 0.5, where the top no longer reaches the hard clip.
        {
            double worst = 0.0;
            for (const auto b : { 0.0, 0.3, 0.45, 0.6 })
            {
                VariableFuzzShape shape;
                shape.setBias (b);
                for (double x = -8.0; x <= 8.0; x += 0.0137)
                {
                    const auto h = 1.0e-5;
                    worst = juce::jmax (worst, std::abs ((shape.integral (x + h) - shape.integral (x - h)) / (2.0 * h) - shape.value (x)));
                }
            }
            check (worst < 1.0e-5, "the Fuzz curve's integral matches the curve at every bias", juce::String (worst));
        }

        const auto tone = sine ((int) (1.5 * rate), 200.0, 0.3f);
        const auto quiet = sine ((int) (1.5 * rate), 1000.0, 0.01f);

        // Overdrive: Asymmetry brings even harmonics, Character the flat top's high ones, Low Cut
        // keeps the bass out, Level is exact.
        {
            const auto evenFlat = harmonicsDb (runWith (overdrive, { { 0, 0.5f } }, tone), 200, 2, 2);
            const auto evenBent = harmonicsDb (runWith (overdrive, { { 0, 0.5f }, { 3, 1.0f } }, tone), 200, 2, 2);
            check (evenBent > evenFlat + 30.0, "Overdrive Asymmetry adds the even harmonics a symmetric curve has none of",
                   juce::String (evenFlat, 1) + " -> " + juce::String (evenBent, 1) + " dB (2nd)");

            const auto highSoft = harmonicsDb (runWith (overdrive, { { 0, 0.3f }, { 1, 1.0f } }, tone), 200, 7, 15);
            const auto highHard = harmonicsDb (runWith (overdrive, { { 0, 0.3f }, { 1, 1.0f }, { 4, 1.0f } }, tone), 200, 7, 15);
            check (highHard > highSoft + 3.0, "Overdrive Character hard has more high harmonics than soft",
                   juce::String (highSoft, 1) + " -> " + juce::String (highHard, 1) + " dB (7th-15th)");

            const auto bass = sine ((int) (1.5 * rate), 50.0, 0.01f);
            const auto open = levelDb (runWith (overdrive, { { 0, 0.0f } }, bass), bass);
            const auto cut = levelDb (runWith (overdrive, { { 0, 0.0f }, { 2, 1.0f } }, bass), bass);
            check (cut < open - 20.0, "Overdrive Low Cut at 1 kHz keeps 50 Hz out of the shaper", juce::String (open, 1) + " -> " + juce::String (cut, 1) + " dB");

            const auto up = levelDb (runWith (overdrive, { { 0, 0.0f }, { 5, Overdrive::levelDb.position (6.0f) } }, quiet), quiet)
                          - levelDb (runWith (overdrive, { { 0, 0.0f } }, quiet), quiet);
            check (std::abs (up - 6.0) < 0.05, "Overdrive Level +6 dB is 6 dB", juce::String (up, 3) + " dB");
        }

        // Fuzz: Bias 0 is symmetric (no even harmonics), the Gate shuts out what is below it and
        // lets a played note through.
        {
            const auto evenCentred = harmonicsDb (runWith (fuzz, { { 2, 0.0f } }, tone), 200, 2, 2);
            const auto evenBiased = harmonicsDb (runWith (fuzz, {}, tone), 200, 2, 2);
            check (evenBiased > evenCentred + 30.0, "Fuzz Bias 0 is symmetric, Rackbox's 0.3 growls an octave up",
                   juce::String (evenCentred, 1) + " vs " + juce::String (evenBiased, 1) + " dB (2nd)");

            const auto hiss = noise ((int) rate, 0.001f, 73);   // -60 dBFS
            const auto open = toDb (windowRms (runWith (fuzz, {}, hiss), 0.5, 0.4));
            const auto gated = toDb (windowRms (runWith (fuzz, { { 3, Fuzz::gateDb.position (-40.0f) } }, hiss), 0.5, 0.4));
            check (open > -50.0 && gated < -100.0, "Fuzz Gate at -40 dB shuts out -60 dB hiss the fuzz would raise",
                   juce::String (open, 1) + " -> " + juce::String (gated, 1) + " dB");

            const auto note = sine ((int) rate, 200.0, 0.3f);
            const auto played = toDb (windowRms (runWith (fuzz, { { 3, Fuzz::gateDb.position (-40.0f) } }, note), 0.5, 0.4));
            check (played > -20.0, "and lets a played note through", juce::String (played, 1) + " dB");
        }

        // Bitcrusher: Jitter varies the holds, Pre Filter takes the aliasing down, Drive clips at
        // full scale, Tone darkens.
        {
            const auto in = noise ((int) rate, 0.5f, 74);
            const auto holds = [&] (float jitter)
            {
                const auto out = runWith (crusher, { { 0, 0.0f }, { 1, 0.6f }, { 2, jitter } }, in);   // 16 bit, x8
                std::vector<int> runs;
                int length = 1;
                for (size_t i = 1; i < out.size(); ++i)
                {
                    if (juce::exactlyEqual (out[i], out[i - 1])) { ++length; continue; }
                    runs.push_back (length);
                    length = 1;
                }
                double mean = 0.0, spread = 0.0;
                for (auto r : runs) mean += r;
                mean /= (double) runs.size();
                for (auto r : runs) spread += (r - mean) * (r - mean);
                return std::sqrt (spread / (double) runs.size()) / mean;
            };
            check (holds (0.0f) < 0.15 && holds (1.0f) > 0.25, "Bitcrusher Jitter makes the hold lengths vary",
                   juce::String (holds (0.0f), 2) + " -> " + juce::String (holds (1.0f), 2) + " (spread / mean)");

            const auto high = sine ((int) (1.5 * rate), 15000.0, 0.5f);   // all of it above the x4 rate's Nyquist (6 kHz)
            const auto raw = levelDb (runWith (crusher, { { 0, 0.0f }, { 1, 0.4f } }, high), high);
            const auto filtered = levelDb (runWith (crusher, { { 0, 0.0f }, { 1, 0.4f }, { 3, 1.0f } }, high), high);
            check (filtered < raw - 10.0, "Bitcrusher Pre Filter takes the aliasing of a tone above the reduced Nyquist down",
                   juce::String (raw, 1) + " -> " + juce::String (filtered, 1) + " dB");

            const auto driven = runWith (crusher, { { 0, 0.0f }, { 1, 0.0f }, { 5, 1.0f } }, tone);
            float peak = 0.0f;
            for (auto v : driven) peak = juce::jmax (peak, std::abs (v));
            check (std::abs (peak - 1.0f) < 1.0e-3f && harmonicsDb (driven, 200, 2, 15) > -15.0,
                   "Bitcrusher Drive +24 dB clips at full scale", juce::String (peak, 4));

            const auto hiss = sine ((int) (1.5 * rate), 12000.0, 0.5f);
            check (levelDb (runWith (crusher, { { 0, 0.0f }, { 1, 0.0f }, { 4, 0.0f } }, hiss), hiss) < -12.0,
                   "Bitcrusher Tone at 1 kHz darkens a 12 kHz tone");
        }
    }

    void checkReverbLevel()
    {
        const auto input = noise ((int) (4.0 * rate), 0.3f, 2);
        const auto inputRms = rms (input.data(), (int) input.size());

        for (int s = 0; s < 3; ++s)
        {
            const auto index = EffectCatalog::index (1, s);
            const juce::String name = EffectCatalog::info (1, s).name;
            double low = 1.0e9, high = -1.0e9;

            for (float size : { 0.0f, 0.5f, 1.0f })
            {
                const auto out = runEffect (index, size, 0.4f, input);
                const auto level = toDb (rms (out.data() + (int) (2.0 * rate), (int) (2.0 * rate)) / inputRms);
                low = juce::jmin (low, level);
                high = juce::jmax (high, level);
                std::printf ("  %s Size %.1f: wet %+5.1f dB re input\n", name.toRawUTF8(), size, level);
            }

            check (high - low < 6.0, name + " loudness moves less than 6 dB across Size",
                   juce::String (high - low, 1) + " dB");
            check (high < 3.0 && low > -20.0, name + " is at a usable level", juce::String (low, 1) + " .. " + juce::String (high, 1));
        }
    }

    //==============================================================================
    /** Gain in dB of the input section for a sine at `hz`, measured after settling. */
    double filterGainDb (float lowpassHz, float highpassHz, double hz)
    {
        InputFilters filters;
        filters.prepare (rate);
        filters.reset (lowpassHz, highpassHz);

        const int n = (int) (1.0 * rate);
        auto left = sine (n, hz, 0.5f);
        auto right = left;

        for (int start = 0; start < n; start += blockSize)
        {
            const auto count = juce::jmin (blockSize, n - start);
            filters.process (left.data() + start, right.data() + start, count, lowpassHz, highpassHz);
        }

        return toDb (rms (left.data() + n / 2, n / 2) / rms (sine (n / 2, hz, 0.5f).data(), n / 2));
    }

    double expectedButterworthDb (double hz, double cutoff, bool highpass)
    {
        // The bilinear transform warps the axis: tan(pi f / fs) plays the role of f.
        const auto ratio = std::tan (3.14159265358979 * hz / rate) / std::tan (3.14159265358979 * cutoff / rate);
        const auto x = highpass ? 1.0 / ratio : ratio;
        return -10.0 * std::log10 (1.0 + std::pow (x, 4.0));
    }

    void checkFilters()
    {
        for (double cutoff : { 200.0, 1000.0, 5000.0 })
        {
            const auto lp = filterGainDb ((float) cutoff, 20.0f, cutoff);
            const auto hp = filterGainDb (20000.0f, (float) cutoff, cutoff);
            check (std::abs (lp - -3.01) < 0.15, "LPF at " + juce::String (cutoff) + " Hz is -3 dB at its cutoff", juce::String (lp, 2));
            check (std::abs (hp - -3.01) < 0.15, "HPF at " + juce::String (cutoff) + " Hz is -3 dB at its cutoff", juce::String (hp, 2));

            for (double multiple : { 0.5, 2.0, 4.0 })
            {
                const auto lpGain = filterGainDb ((float) cutoff, 20.0f, cutoff * multiple);
                const auto hpGain = filterGainDb (20000.0f, (float) cutoff, cutoff / multiple);
                const auto lpExpected = expectedButterworthDb (cutoff * multiple, cutoff, false);
                const auto hpExpected = expectedButterworthDb (cutoff / multiple, cutoff, true);

                check (std::abs (lpGain - lpExpected) < 0.25, "LPF " + juce::String (cutoff) + " Hz, x" + juce::String (multiple) + " matches the warped Butterworth",
                       juce::String (lpGain, 2) + " vs " + juce::String (lpExpected, 2));
                check (std::abs (hpGain - hpExpected) < 0.25, "HPF " + juce::String (cutoff) + " Hz, /" + juce::String (multiple) + " matches the warped Butterworth",
                       juce::String (hpGain, 2) + " vs " + juce::String (hpExpected, 2));
            }
        }

        // Open: bit for bit.
        InputFilters filters;
        filters.prepare (rate);
        const auto input = noise (4096, 0.5f, 3);
        auto left = input, right = input;

        for (size_t start = 0; start < input.size(); start += blockSize)
            filters.process (left.data() + start, right.data() + start, blockSize, InputFilters::lowpassOff, InputFilters::highpassOff);

        check (std::memcmp (left.data(), input.data(), input.size() * sizeof (float)) == 0, "an open input section is bit-exact");
    }

    //==============================================================================
    /** Runs a block over a signal in host-sized pieces; settingsAt gives the settings for each piece. */
    std::vector<float> runBlock (const std::vector<float>& input, const std::function<EffectBlock::Settings (int pieceIndex)>& settingsAt,
                                 int piece = blockSize)
    {
        EffectBlock block;
        block.prepare (rate, blockSize);

        auto left = input, right = input;
        int index = 0;

        for (size_t start = 0; start < input.size(); start += (size_t) piece, ++index)
        {
            const auto n = (int) juce::jmin ((size_t) piece, input.size() - start);
            block.process (left.data() + start, right.data() + start, n, settingsAt (index));
        }

        return left;
    }

    EffectBlock::Settings settingsFor (int effectIndex, float mix = 1.0f)
    {
        EffectBlock::Settings s;
        s.mix = mix;

        if (effectIndex == EffectBlock::drySlot)
        {
            s.bypass = true;
            return s;
        }

        for (int c = 0; c < EffectCatalog::numCategories; ++c)
            for (int sub = 0; sub < EffectCatalog::numSubtypes (c); ++sub)
                if (EffectCatalog::index (c, sub) == effectIndex)
                {
                    s.category = c;
                    s.subtype = sub;

                    for (size_t k = 0; k < s.knobs.size(); ++k)   // where the panel would start them
                        s.knobs[k] = EffectCatalog::info (c, sub).knobs[k].defaultValue;
                }

        return s;
    }

    /** The lag between an impulse and the biggest sample of the effect's output. */
    int echoAfter (int effectIndex, float a, float b, bool sync, double bpm)
    {
        auto effect = EffectCatalog::create (effectIndex);
        effect->prepare (rate, blockSize);
        effect->setParameters (a, b);
        effect->setTiming (sync, bpm);
        effect->reset();

        std::vector<float> left ((size_t) (5.0 * rate), 0.0f);
        auto right = left;
        left[100] = right[100] = 0.8f;

        for (size_t start = 0; start < left.size(); start += blockSize)
        {
            const auto n = (int) juce::jmin ((size_t) blockSize, left.size() - start);
            effect->process (left.data() + start, right.data() + start, n);
        }

        size_t peak = 0;
        for (size_t i = 0; i < left.size(); ++i)
            if (std::abs (left[i]) > std::abs (left[peak])) peak = i;

        return (int) peak - 100;
    }

    void checkTempoSync()
    {
        // The table itself: fourteen ascending lengths, quarter = 1, and the printed names.
        bool ascending = true;
        for (int i = 1; i < Dsp::Sync::numDivisions; ++i)
            ascending = ascending && Dsp::Sync::beats[i] > Dsp::Sync::beats[i - 1];

        check (ascending, "the note values ascend");
        check (juce::String (Dsp::Sync::names[8]) == "1/4" && juce::exactlyEqual (Dsp::Sync::beats[8], 1.0f), "step 8 is the quarter note");
        check (std::abs (Dsp::Sync::seconds (8.0f / 13.0f, 120.0) - 0.5f) < 1.0e-6f, "a quarter at 120 BPM is 0.5 s");
        check (std::abs (Dsp::Sync::beats[6] / Dsp::Sync::beats[8] - 2.0f / 3.0f) < 1.0e-5f && std::abs (Dsp::Sync::beats[10] / Dsp::Sync::beats[8] - 1.5f) < 1.0e-5f,
               "triplet is 2/3 and dotted is 3/2 of the straight value");
        check (EffectCatalog::format (Catalog::Division, 5.0f / 13.0f) == "1/8" && EffectCatalog::format (Catalog::Division, 1.0f) == "1/1"
               && EffectCatalog::format (Catalog::Division, 0.0f) == "1/32", "Division prints the note names");
        check (EffectCatalog::canSync (2, 0) && EffectCatalog::canSync (2, 2) && EffectCatalog::canSync (0, 0) && EffectCatalog::canSync (0, 3)
               && ! EffectCatalog::canSync (1, 0) && ! EffectCatalog::canSync (1, 2) && ! EffectCatalog::canSync (3, 0),
               "the delays and the modulation effects offer Sync, reverb and distortion do not");

        // Each delay, at 120 BPM and 1/8: the first echo at 250 ms. The tape delay's wobble and
        // low-pass move the peak by a few samples, so it gets a wider window.
        const struct { const char* name; int c, s, tolerance; } delays[] {
            { "Digital", 2, 0, 2 }, { "Tape", 2, 1, 40 }, { "Ping-Pong", 2, 2, 2 } };

        for (auto& d : delays)
        {
            const auto lag = echoAfter (EffectCatalog::index (d.c, d.s), 5.0f / 13.0f, 0.0f, true, 120.0);
            check (std::abs (lag - (int) (0.25 * rate)) <= d.tolerance, juce::String (d.name) + " delay: 1/8 at 120 BPM lands at 250 ms",
                   juce::String (lag * 1000.0 / rate, 2) + " ms");
        }

        // Tempo changes while playing: the delay glides, it does not step.
        {
            auto effect = EffectCatalog::create (EffectCatalog::index (2, 0));
            effect->prepare (rate, blockSize);
            effect->setParameters (5.0f / 13.0f, 0.4f);
            effect->setTiming (true, 120.0);
            effect->reset();

            const auto input = sine ((int) (3.0 * rate), 440.0, 0.5f);
            auto left = input, right = input;

            for (size_t start = 0, block = 0; start < input.size(); start += blockSize, ++block)
            {
                effect->setTiming (true, block < 150 ? 120.0 : 75.0);
                effect->process (left.data() + start, right.data() + start, (int) juce::jmin ((size_t) blockSize, input.size() - start));
            }

            check (allFinite (left, 8.0f), "a tempo change stays finite");
            check (maxStep (left.data(), (int) left.size()) < 0.35f, "a tempo change from 120 to 75 BPM does not step",
                   juce::String (maxStep (left.data(), (int) left.size()), 3));
        }

        // Slow enough to overrun the buffer: held at four seconds, finite.
        {
            const auto lag = echoAfter (EffectCatalog::index (2, 0), 1.0f, 0.0f, true, 20.0);
            check (lag > 0 && lag <= (int) (4.05 * rate), "a whole note at 20 BPM is held at the buffer's four seconds",
                   juce::String (lag * 1.0 / rate, 2) + " s");
        }
    }

    /** Runs a tremolo on a constant 1.0 and returns its gain over time: the LFO made visible. */
    std::vector<float> tremoloGain (double bpm, float knob, double startPpq, int blocks,
                                    const std::function<double (int block)>& ppqAt = nullptr)
    {
        auto effect = EffectCatalog::create (EffectCatalog::index (0, 3));
        effect->prepare (rate, blockSize);
        effect->setParameters (knob, 1.0f);   // depth 1: gain runs 0..1
        effect->reset();

        std::vector<float> gain;

        for (int b = 0; b < blocks; ++b)
        {
            const auto ppq = ppqAt ? ppqAt (b) : startPpq + b * blockSize * bpm / (60.0 * rate);
            effect->setTiming (true, bpm, true, ppq);

            std::vector<float> left ((size_t) blockSize, 1.0f), right ((size_t) blockSize, 1.0f);
            effect->process (left.data(), right.data(), blockSize);
            gain.insert (gain.end(), left.begin(), left.end());
        }

        return gain;
    }

    void checkLfoSync()
    {
        // 1/4 at 120 BPM: one cycle per 0.5 s. The gain must be the formula for the grid phase,
        // starting from a position that is not on a downbeat.
        {
            const double bpm = 120.0, startPpq = 1.3;
            const auto gain = tremoloGain (bpm, 8.0f / 13.0f, startPpq, 200);
            double worst = 0.0;

            for (size_t i = 0; i < gain.size(); ++i)
            {
                const auto ppq = startPpq + (double) i * bpm / (60.0 * rate);
                const auto expected = 1.0 - 0.5 * (1.0 + std::sin (6.283185307179586 * std::fmod (ppq / 1.0, 1.0)));
                worst = juce::jmax (worst, std::abs ((double) gain[i] - expected));
            }

            check (worst < 0.01, "a synced tremolo (1/4, 120 BPM) sits on the beat grid from a mid-bar start", "worst " + juce::String (worst, 4));
        }

        // The transport jumps: a loop, a locate. The LFO slews to the new phase (50 ms) and is
        // on the grid afterwards, without a step on the way.
        {
            const double bpm = 120.0;
            const auto ppqAt = [=] (int b) { return b < 60 ? b * blockSize * bpm / (60.0 * rate) : 7.7 + (b - 60) * blockSize * bpm / (60.0 * rate); };
            const auto gain = tremoloGain (bpm, 8.0f / 13.0f, 0.0, 200, ppqAt);

            check (maxStep (gain.data(), (int) gain.size()) < 0.05f, "a transport jump does not click the synced tremolo",
                   juce::String (maxStep (gain.data(), (int) gain.size()), 4));

            const size_t probe = 150 * blockSize;
            const auto ppq = ppqAt (150) + 0.0;
            const auto expected = 1.0 - 0.5 * (1.0 + std::sin (6.283185307179586 * std::fmod (ppq, 1.0)));
            check (std::abs ((double) gain[probe] - expected) < 0.01, "and after the slew it is back on the grid",
                   juce::String (gain[probe], 4) + " vs " + juce::String (expected, 4));
        }

        // Faster tempo, faster sweep: the same division at 90 BPM has a period of 0.667 s.
        {
            const auto gain = tremoloGain (90.0, 8.0f / 13.0f, 0.0, 400);
            int rises = 0;
            for (size_t i = 1; i < gain.size(); ++i)
                if (gain[i - 1] < 0.5f && gain[i] >= 0.5f) ++rises;

            const auto seconds = (double) gain.size() / rate;
            check (std::abs ((double) rises / seconds - 90.0 / 60.0) < 0.15, "at 90 BPM a 1/4 tremolo makes 1.5 cycles a second",
                   juce::String (rises / seconds, 2));
        }

        // Every modulation effect, at the extremes of the grid, stays finite and bounded.
        const auto input = noise ((int) (2.0 * rate), 0.3f, 8);

        for (int s = 0; s < 4; ++s)
            for (auto extreme : { std::make_pair (999.0, 0.0f), std::make_pair (20.0, 1.0f) })
            {
                auto effect = EffectCatalog::create (EffectCatalog::index (0, s));
                effect->prepare (rate, blockSize);
                effect->setParameters (extreme.second, 0.8f);
                effect->reset();

                auto left = input, right = input;
                for (size_t start = 0, b = 0; start < input.size(); start += blockSize, ++b)
                {
                    effect->setTiming (true, extreme.first, true, (double) b * 2.7);
                    effect->process (left.data() + start, right.data() + start, (int) juce::jmin ((size_t) blockSize, input.size() - start));
                }

                check (allFinite (left, 8.0f), juce::String (EffectCatalog::info (0, s).name) + " synced at " + juce::String (extreme.first) + " BPM is finite");
            }
    }

    /** The first sample of the effect's output above a threshold, counted from the impulse at 100. */
    int reverbOnset (int effectIndex, int step, double bpm, double beatsPerBar = 4.0)
    {
        auto effect = EffectCatalog::create (effectIndex);
        effect->prepare (rate, blockSize);
        effect->setParameters (0.3f, 0.4f);
        effect->setTiming (false, bpm);
        effect->setPreDelay (step, beatsPerBar);
        effect->reset();

        std::vector<float> left ((size_t) (4.0 * rate), 0.0f);
        auto right = left;
        left[100] = right[100] = 0.8f;

        for (size_t start = 0; start < left.size(); start += blockSize)
            effect->process (left.data() + start, right.data() + start, (int) juce::jmin ((size_t) blockSize, left.size() - start));

        for (size_t i = 0; i < left.size(); ++i)
            if (std::abs (left[i]) > 1.0e-5f || std::abs (right[i]) > 1.0e-5f)
                return (int) i - 100;

        return -1;
    }

    void checkPreDelay()
    {
        // The table: milliseconds at 120 BPM in 4/4, and a bar in 3/4.
        const float expected[] { 0.0f, 15.625f, 31.25f, 62.5f, 125.0f, 250.0f, 500.0f, 1000.0f, 2000.0f };
        bool table = true;
        for (int i = 0; i < Dsp::Sync::numPreDelays; ++i)
            table = table && std::abs (Dsp::Sync::preDelaySeconds (i, 120.0, 4.0) * 1000.0f - expected[i]) < 0.01f;

        check (table, "the pre-delay steps are 0, 15.6, 31.3, 62.5, 125, 250, 500, 1000, 2000 ms at 120 BPM");
        check (std::abs (Dsp::Sync::preDelaySeconds (8, 120.0, 3.0) - 1.5f) < 1.0e-6f, "one bar in 3/4 at 120 BPM is 1.5 s");
        check (std::abs (Dsp::Sync::preDelaySeconds (8, 20.0, 4.0) - 4.0f) < 1.0e-6f, "a bar at 20 BPM is held at the 4 s buffer");
        check (juce::String (Dsp::Sync::preDelayNames[0]) == "OFF" && juce::String (Dsp::Sync::preDelayNames[4]) == "1/16", "the names");

        // Each reverb: nothing comes out before the pre-delay is over, and it comes out soon after.
        for (int s = 0; s < 3; ++s)
        {
            const auto index = EffectCatalog::index (1, s);
            const juce::String name = EffectCatalog::info (1, s).name;

            const auto off = reverbOnset (index, 0, 120.0);
            check (off >= 0 && off < (int) (0.06 * rate), name + " with the pre-delay off starts at once", juce::String (off));

            for (auto [step, bpm, seconds] : { std::make_tuple (4, 120.0, 0.125), std::make_tuple (4, 90.0, 60.0 / 90.0 * 0.25),
                                               std::make_tuple (2, 120.0, 0.03125), std::make_tuple (6, 120.0, 0.5) })
            {
                const auto onset = reverbOnset (index, step, bpm);
                const auto wanted = (int) (seconds * rate);

                // Not before it, and within the reverb's own onset (under 60 ms) after it.
                check (onset >= wanted - 1 && onset <= wanted + (int) (0.06 * rate),
                       name + ", " + juce::String (Dsp::Sync::preDelayNames[step]) + " at " + juce::String (bpm) + " BPM: silent for " + juce::String (seconds * 1000.0, 1) + " ms",
                       "onset " + juce::String (onset * 1000.0 / rate, 1) + " ms");
            }

            const auto bar = reverbOnset (index, 8, 120.0, 3.0);
            check (bar >= (int) (1.5 * rate) - 1 && bar <= (int) (1.5 * rate) + (int) (0.06 * rate), name + ", 1 bar of 3/4 at 120 BPM: 1.5 s",
                   juce::String (bar * 1.0 / rate, 3) + " s");
        }

        // Changing the pre-delay while sound is going through it is a crossfade, not a jump.
        {
            Dsp::PreDelay preDelay;
            preDelay.prepare (rate, 4.0f);

            const auto input = sine ((int) (1.0 * rate), 1000.0, 0.5f);
            std::vector<float> out;
            for (size_t i = 0; i < input.size(); ++i)
                out.push_back (preDelay.process (input[i], i < 20000 ? 0.0f : (i < 36000 ? 24000.0f : 3000.0f)));

            check (maxStep (out.data(), (int) out.size()) < 0.12f, "a pre-delay change (0 -> 500 ms -> 62 ms) does not step",
                   juce::String (maxStep (out.data(), (int) out.size()), 3));

            // A hard jump, for comparison, is the click the crossfade avoids.
            Dsp::DelayLine line;
            line.allocate (48000);
            std::vector<float> hard;
            for (size_t i = 0; i < input.size(); ++i)
            {
                line.push (input[i]);
                hard.push_back (line.read (i < 20000 ? 0.0f : 24000.0f));
            }

            check (maxStep (hard.data(), (int) hard.size()) > 0.3f, "control: a hard jump of the read position does step");
        }

        // The reverbs stay finite with a bar of pre-delay at the tempo extremes.
        const auto input = noise ((int) (2.0 * rate), 0.3f, 9);
        for (int s = 0; s < 3; ++s)
            for (double bpm : { 20.0, 999.0 })
            {
                auto effect = EffectCatalog::create (EffectCatalog::index (1, s));
                effect->prepare (rate, blockSize);
                effect->setParameters (0.5f, 0.5f);
                effect->setTiming (false, bpm);
                effect->setPreDelay (8, 4.0);
                effect->reset();

                auto left = input, right = input;
                for (size_t start = 0; start < input.size(); start += blockSize)
                    effect->process (left.data() + start, right.data() + start, blockSize);

                check (allFinite (left, 8.0f), juce::String (EffectCatalog::info (1, s).name) + " with a bar of pre-delay at " + juce::String (bpm) + " BPM is finite");
            }
    }

    void checkWire()
    {
        const auto input = noise ((int) (2.0 * rate), 0.5f, 4);

        const auto bypassed = runBlock (input, [] (int) { return settingsFor (EffectBlock::drySlot); });
        check (std::memcmp (bypassed.data(), input.data(), input.size() * sizeof (float)) == 0, "a bypassed block is a wire, bit for bit");

        const auto dryMix = runBlock (input, [] (int) { return settingsFor (EffectCatalog::index (2, 1), 0.0f); });
        check (std::memcmp (dryMix.data(), input.data(), input.size() * sizeof (float)) == 0, "Mix at 0 gives the dry signal, bit for bit");

        // Bypass is a real fade, not a cut: after switching an effect out, the output returns to the input exactly.
        const auto returned = runBlock (input, [] (int i) { return settingsFor (i < 20 ? EffectCatalog::index (0, 0) : EffectBlock::drySlot); });
        check (std::memcmp (returned.data() + (int) (1.5 * rate), input.data() + (int) (1.5 * rate), (input.size() - (size_t) (1.5 * rate)) * sizeof (float)) == 0,
               "after the fade to bypass the block is a wire again");
    }

    void checkCrossfade()
    {
        // The measurement first: does the test see a step at all? A sine that flips sign
        // between two samples has a step of about 1.0 against a slope of 0.065.
        {
            auto a = sine (2000, 1000.0, 0.5f);
            auto b = sine (2000, 1000.0, 0.5f, 3.14159265358979);
            std::vector<float> joined (a.begin(), a.begin() + 1000);
            joined.insert (joined.end(), b.begin() + 1000, b.end());
            check (maxStep (joined.data(), (int) joined.size()) > 0.5f, "control: the step measurement sees a hard switch");
            check (maxStep (a.data(), (int) a.size()) < 0.1f, "control: a steady sine's own step is small");
        }

        const int settle = (int) (0.6 * rate), after = (int) (0.4 * rate);
        const auto input = sine (settle + after, 1000.0, 0.5f);
        const int switchBlock = settle / blockSize;

        // The steepest slope each destination has on its own.
        std::vector<float> ownStep ((size_t) EffectBlock::numSlots, 0.0f);
        for (int i = 0; i < EffectBlock::numSlots; ++i)
        {
            const auto steady = runBlock (input, [i] (int) { return settingsFor (i); });
            ownStep[(size_t) i] = maxStep (steady.data() + settle / 2, (int) input.size() - settle / 2);
        }

        int worstFrom = 0, worstTo = 0;
        float worstRatio = 0.0f;
        bool allFine = true;

        for (int from = 0; from < EffectBlock::numSlots; ++from)
            for (int to = 0; to < EffectBlock::numSlots; ++to)
            {
                if (from == to)
                    continue;

                const auto out = runBlock (input, [=] (int piece) { return settingsFor (piece < switchBlock ? from : to); });

                // Only the fade and the moments after it, where a step would be.
                const auto window = maxStep (out.data() + switchBlock * blockSize - 1, (int) (0.1 * rate));
                const auto allowed = 1.3f * juce::jmax (ownStep[(size_t) from], ownStep[(size_t) to]) + 0.02f;

                if (! allFinite (out, 8.0f) || window > allowed)
                {
                    allFine = false;
                    std::printf ("  step %d -> %d: %.3f allowed %.3f\n", from, to, window, allowed);
                }

                if (window / allowed > worstRatio)
                {
                    worstRatio = window / allowed;
                    worstFrom = from;
                    worstTo = to;
                }
            }

        check (allFine, "no effect-to-effect switch (" + juce::String (EffectBlock::numSlots * (EffectBlock::numSlots - 1)) + " pairs incl. bypass) steps", "worst " + juce::String (worstFrom) + " -> " + juce::String (worstTo)
                                                                                     + " at " + juce::String (100.0f * worstRatio, 0) + " % of the allowance");
        std::printf ("  crossfade: worst pair %d -> %d uses %.0f %% of its allowance\n", worstFrom, worstTo, 100.0f * worstRatio);

        // Switching faster than a fade lasts.
        std::mt19937 rng (7);
        std::vector<EffectBlock::Settings> plan;
        for (int i = 0; i < 200; ++i)
            plan.push_back (settingsFor ((int) (rng() % (unsigned) EffectBlock::numSlots), 0.7f));

        const auto stressInput = sine ((int) (2.0 * rate), 440.0, 0.5f);
        const auto stress = runBlock (stressInput, [&] (int piece) { return plan[(size_t) (piece / 1) % plan.size()]; }, 480);

        check (allFinite (stress, 8.0f), "switching every 10 ms stays finite and bounded");
        check (maxStep (stress.data(), (int) stress.size()) < 0.4f, "switching every 10 ms never steps",
               juce::String (maxStep (stress.data(), (int) stress.size()), 3));
    }

    void checkChunking()
    {
        const auto input = noise (1237, 0.4f, 5);
        auto settings = settingsFor (EffectCatalog::index (0, 0), 0.8f);

        // One call longer than the prepared block, against the same audio in pieces of 512.
        EffectBlock whole, pieces;
        whole.prepare (rate, blockSize);
        pieces.prepare (rate, blockSize);

        auto a = input, b = input, ar = input, br = input;
        whole.process (a.data(), ar.data(), (int) a.size(), settings);

        for (int start = 0; start < (int) b.size(); start += blockSize)
            pieces.process (b.data() + start, br.data() + start, juce::jmin (blockSize, (int) b.size() - start), settings);

        check (std::memcmp (a.data(), b.data(), a.size() * sizeof (float)) == 0, "a host block longer than the prepared one gives the same bits");
    }
}

int main()
{
    const juce::ScopedJuceInitialiser_GUI juceInitialiser;

    std::printf ("Catalog...\n");
    checkCatalog();
    std::printf ("Every effect (level of the mid setting on noise)...\n");
    checkEveryEffect();
    std::printf ("Aliasing of the distortions...\n");
    checkAliasing();
    std::printf ("Tails...\n");
    checkTails();
    std::printf ("Reverb loudness across Size...\n");
    checkReverbLevel();
    std::printf ("Frequency shifter...\n");
    checkFreqShifter();
    std::printf ("Modulation knobs...\n");
    checkModulationKnobs();
    std::printf ("Reverb knobs...\n");
    checkReverbKnobs();
    std::printf ("Delay knobs...\n");
    checkDelayKnobs();
    std::printf ("Distortion knobs...\n");
    checkDistortionKnobs();
    std::printf ("Input filters...\n");
    checkFilters();
    std::printf ("Tempo sync...\n");
    checkTempoSync();
    std::printf ("LFO sync...\n");
    checkLfoSync();
    std::printf ("Pre-delay...\n");
    checkPreDelay();
    std::printf ("Wire...\n");
    checkWire();
    std::printf ("Crossfade...\n");
    checkCrossfade();
    std::printf ("Chunking...\n");
    checkChunking();

    std::printf ("\n%d checks, %d failure(s)\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
