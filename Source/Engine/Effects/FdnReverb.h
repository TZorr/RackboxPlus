//
//  FdnReverb.h
//  Rackbox Plus
//
//  What Room and Hall share: a feedback delay network of eight lines.
//
//  Why a network and not Kitbox's plate for these too. A plate has no early
//  reflections and a dense onset, which is right for it and wrong for a room:
//  the sense of a space is mostly the first few hundred milliseconds. A
//  feedback delay network gets there cheaply - eight delay lines of unequal
//  length, their outputs mixed by a Hadamard matrix (orthonormal, so the
//  mixing loses no energy) and fed back into their inputs. Every line ends up
//  in every other, so the echo density grows exponentially with time, which is
//  what a room does.
//
//  Decay. Each line is given the gain that makes its own trip around the loop
//  drop by 60 dB in the chosen RT60: g = 10^(-3 * delay / (RT60 * fs)). A
//  one-pole low-pass in each line makes the highs die faster (Damp), as air and
//  soft surfaces do.
//
//  Level. A network's steady-state loudness grows with its decay time, so the
//  input is scaled down as the loop gets longer: by (1 - g_mean^2)^0.2. The
//  textbook exponent is 0.5 (it holds the energy in the loop constant), but
//  measured on noise it over-corrects here - the damping filters and the
//  decorrelating output taps take level out as well - and a Hall dropped 5 dB
//  across Size. 0.2 is the exponent that leaves the level within about 2 dB
//  across the whole knob (RackboxCheck prints the numbers).
//
//  Modulation. The read positions wander a few samples on slow sine LFOs, one
//  per line. Fixed delays ring at their own resonances on a long tail; a
//  wandering read turns the ringing into shimmer.
//
//  The pre-delay (Dsp::PreDelay, shared with the Plate) comes first, so the whole onset -
//  diffusion and early reflections included - moves later as one.
//
//  Two Schroeder allpasses in front of the network smear the input, so a click
//  does not arrive as eight discrete echoes.
//
//  The knobs, in order:
//    Size       the room: the eight lines at 50 to 200 % of their tuned lengths.
//               Bigger is later first reflections and a sparser, grander tail;
//               the decay time stays what Decay says (the gains follow).
//    Decay      RT60, the time to fall 60 dB, in seconds (the range is the
//               variant's: Room up to 4 s, Hall up to 15 s).
//    Damp       how much faster the highs die, as air and soft surfaces do.
//    Pre-Delay  0 to 250 ms of silence before the reverb, on top of the PRE
//               button's note value.
//    Early      early reflections: eight taps of the dry input between a third
//               and twice the shortest line, alternating left and right, ahead
//               of the network. The sense of a real room's walls; 0 is none.
//    Mod        how far the read positions wander, 0 to 200 %: none rings at the
//               lines' own resonances on a long tail, much is a chorused shimmer.
//    Low Cut    a high-pass on the reverb, 20 Hz to 1 kHz; at the bottom it is off.
//    Width      0 mono, 100 % the network's own image, 150 % wider.
//
//  At its starting values (100 %, the decay in the middle of Rackbox's range,
//  no early reflections, 100 % mod, open, 100 % wide) it is Rackbox's reverb.
//

#pragma once

#include "../Effect.h"
#include "../Dsp.h"

class FdnReverb : public Effect
{
public:
    struct Tuning
    {
        float minDelayMs, maxDelayMs;   // the spread of the eight lines at Size 100 %
        Dsp::Range decaySeconds;        // RT60 across the Decay knob
    };

    static constexpr Dsp::Range size       { 0.5f, 2.0f, Dsp::Range::Curve::exponential };
    static constexpr Dsp::Range preDelayMs { 0.0f, 250.0f };
    static constexpr Dsp::Range mod        { 0.0f, 2.0f };
    static constexpr Dsp::Range lowCutHz   { 20.0f, 1000.0f, Dsp::Range::Curve::exponential };
    static constexpr Dsp::Range width      { 0.0f, 1.5f };

    explicit FdnReverb (Tuning t) : tuning (t) {}

    void process (float* left, float* right, int n) noexcept override
    {
        // Gains and damping once per block, from where the knobs are now.
        updateFeedback (size.map (current (0)), block (1, n), block (2, n));
        const auto preTarget = preDelaySamples();

        for (int i = 0; i < n; ++i)
        {
            const auto sizeNow = size.map (next (0));
            const auto pre = preDelayMs.map (next (3)) * 0.001f * (float) sampleRate;
            const auto early = next (4);
            const auto wobbleDepth = 6.0f * (float) scale * mod.map (next (5));
            const auto lowCut = lowCutHz.map (next (6));
            const auto spread = width.map (next (7));

            const auto dry = preDelay.process (0.5f * (left[i] + right[i]), preTarget + pre);
            auto x = diffuse (allpass[0], dry, 0.6f);
            x = diffuse (allpass[1], x, 0.6f);

            float out[numLines];
            for (int l = 0; l < numLines; ++l)
                out[l] = lines[l].read (baseLength[l] * sizeNow + wobbleDepth * modulation[l].next());

            // Hadamard, in place: three butterfly stages, then 1/sqrt(8).
            float mixed[numLines];
            for (int l = 0; l < numLines; ++l)
                mixed[l] = out[l];

            for (int span = 1; span < numLines; span <<= 1)
                for (int base = 0; base < numLines; base += span * 2)
                    for (int k = base; k < base + span; ++k)
                    {
                        const auto p = mixed[k], q = mixed[k + span];
                        mixed[k] = p + q;
                        mixed[k + span] = p - q;
                    }

            for (int l = 0; l < numLines; ++l)
            {
                const auto fed = damping[l].process (mixed[l] * 0.35355339f) * gains[l];
                lines[l].push (fed + inputSigns[l] * inputGain * x);
            }

            // Even lines to the left, odd to the right, alternating signs so
            // the two outputs are decorrelated.
            auto l = 0.5f * (out[0] - out[2] + out[4] - out[6]);
            auto r = 0.5f * (out[1] - out[3] + out[5] - out[7]);

            earlyLine.push (dry);

            if (early > 0.0f)
            {
                const auto first = tuning.minDelayMs * 0.001f * (float) sampleRate * sizeNow;

                for (int t = 0; t < numTaps; ++t)
                {
                    const auto tap = early * earlyGains[t] * earlyLine.read (first * earlyTimes[t]);
                    (t % 2 == 0 ? l : r) += tap;
                }
            }

            if (lowCut > lowCutHz.low + 0.01f)
            {
                lowCutFilter[0].setCutoff (lowCut, sampleRate);
                lowCutFilter[1].setCutoff (lowCut, sampleRate);
                l = lowCutFilter[0].process (l);
                r = lowCutFilter[1].process (r);
            }

            Dsp::widen (l, r, spread);
            left[i] = l;
            right[i] = r;
        }
    }

protected:
    void onPrepare() override
    {
        scale = sampleRate / 48000.0;
        preDelay.prepare (sampleRate, Dsp::Sync::maxPreDelaySeconds + preDelayMs.high * 0.001f);

        for (int l = 0; l < numLines; ++l)
        {
            // Geometric spread between the two limits, then nudged to a nearby prime
            // so no two lines share a factor.
            const auto fraction = (float) l / (float) (numLines - 1);
            const auto ms = tuning.minDelayMs * std::pow (tuning.maxDelayMs / tuning.minDelayMs, fraction);
            baseLength[l] = (float) nearestPrime (juce::jmax (8, (int) (ms * 0.001 * sampleRate)));
            lines[l].allocate ((int) (baseLength[l] * size.high) + (int) (16.0 * scale * mod.high) + 8);
            modulation[l].setRate (0.11f + 0.037f * (float) l, sampleRate);
        }

        earlyLine.allocate ((int) (tuning.minDelayMs * 0.001 * sampleRate * size.high * earlyTimes[numTaps - 1]) + 8);

        allpass[0].line.allocate ((int) (0.0053 * sampleRate) + 4);
        allpass[0].delay = (int) (0.0053 * sampleRate);
        allpass[1].line.allocate ((int) (0.0077 * sampleRate) + 4);
        allpass[1].delay = (int) (0.0077 * sampleRate);
    }

    void onReset() override
    {
        for (int l = 0; l < numLines; ++l)
        {
            lines[l].clear();
            damping[l].reset();
            modulation[l].setPhase ((float) l / (float) numLines);
        }

        for (auto& a : allpass)
            a.line.clear();

        earlyLine.clear();
        preDelay.reset();
        lowCutFilter[0].reset();
        lowCutFilter[1].reset();

        updateFeedback (size.map (target (0)), target (1), target (2));
    }

private:
    static constexpr int numLines = 8, numTaps = 8;

    // The early taps, as multiples of the shortest line, and their levels.
    static constexpr float earlyTimes[numTaps] { 0.33f, 0.52f, 0.71f, 0.87f, 1.08f, 1.31f, 1.62f, 2.0f };
    static constexpr float earlyGains[numTaps] { 0.62f, -0.55f, 0.48f, -0.43f, 0.37f, -0.33f, 0.28f, -0.24f };

    struct Allpass
    {
        Dsp::DelayLine line;
        int delay = 1;
    };

    static float diffuse (Allpass& a, float x, float g) noexcept
    {
        const auto delayed = a.line.read ((float) a.delay);
        const auto v = x - g * delayed;
        a.line.push (v);
        return g * v + delayed;
    }

    static int nearestPrime (int n) noexcept
    {
        const auto isPrime = [] (int v)
        {
            if (v < 2) return false;
            for (int d = 2; d * d <= v; ++d)
                if (v % d == 0) return false;
            return true;
        };

        for (int offset = 0;; ++offset)
        {
            if (isPrime (n + offset)) return n + offset;
            if (n - offset > 1 && isPrime (n - offset)) return n - offset;
        }
    }

    void updateFeedback (float sizeFactor, float decayKnob, float damp) noexcept
    {
        const auto rt60 = tuning.decaySeconds.map (decayKnob);
        const auto cutoff = Dsp::Map::exponential (1.0f - juce::jlimit (0.0f, 1.0f, damp), 1500.0f, 14000.0f);

        float sum = 0.0f;
        for (int l = 0; l < numLines; ++l)
        {
            gains[l] = std::pow (10.0f, -3.0f * baseLength[l] * sizeFactor / (rt60 * (float) sampleRate));
            damping[l].setCutoff (cutoff, sampleRate);
            sum += gains[l];
        }

        const auto mean = sum / (float) numLines;
        inputGain = 0.7f * std::pow (juce::jmax (1.0e-4f, 1.0f - mean * mean), 0.2f);
    }

    Tuning tuning;
    double scale = 1.0;

    Dsp::DelayLine lines[numLines], earlyLine;
    Dsp::OnePole damping[numLines];
    Dsp::OnePoleHighPass lowCutFilter[2];
    Dsp::Lfo modulation[numLines];
    Allpass allpass[2];
    Dsp::PreDelay preDelay;

    float baseLength[numLines] {};
    float gains[numLines] {};
    float inputGain = 0.3f;

    static constexpr float inputSigns[numLines] { 1, -1, 1, 1, -1, 1, -1, -1 };
};
