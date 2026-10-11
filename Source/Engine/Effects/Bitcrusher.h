//
//  Bitcrusher.h
//  Rackbox Plus
//
//  Bit depth and sample rate, both lowered on purpose.
//
//  Bits runs from 16 down to 2: the signal is rounded to that many levels,
//  which is quantisation noise that follows the signal. Rate holds each sample
//  for 1 to 32 samples, so everything above the reduced Nyquist folds back as
//  aliasing. Neither is filtered - the alias products are the effect.
//
//  Bits is continuous, not stepped, so the knob sweeps smoothly through the
//  in-between depths; the level count is 2^(bits-1) per polarity.
//
//  The knobs, in order:
//    Bits        16 down to 2.
//    Rate        hold each sample for 1 to 32 samples.
//    Jitter      how much each hold's length varies at random, 0 to 100 %: the
//                steady whine of a fixed reduced rate turns into a noisy grind.
//    Pre Filter  a two-pole low-pass before the rate is reduced, tied to the reduced
//                Nyquist: 0 is off (all the aliasing), 100 % sits right on it
//                (little aliasing, a dull, muffled lo-fi instead).
//    Tone        a low-pass after everything, 1 to 20 kHz; at the top it is off.
//    Drive       0 to +24 dB into the crusher, clipping hard at full scale like
//                an overloaded converter; the quantising follows the louder signal.
//    Level       -12 to +12 dB after everything.
//
//  At its starting values (no jitter, no filters, 0 dB in and out) it is
//  Rackbox's bitcrusher.
//

#pragma once

#include "../Effect.h"
#include "../Dsp.h"

class Bitcrusher : public Effect
{
public:
    static constexpr Dsp::Range toneHz  { 1000.0f, 20000.0f, Dsp::Range::Curve::exponential };
    static constexpr Dsp::Range driveDb { 0.0f, 24.0f };
    static constexpr Dsp::Range levelDb { -12.0f, 12.0f };

    void process (float* left, float* right, int n) noexcept override
    {
        for (int i = 0; i < n; ++i)
        {
            const auto levels = std::exp2 (Dsp::Map::bits (nextA()) - 1.0f);
            const auto period = Dsp::Map::downsample (nextB());
            const auto jitter = next (2);
            const auto preFilter = next (3);
            const auto tone = toneHz.map (next (4));
            const auto drive = driveDb.map (next (5));
            const auto level = std::pow (10.0f, levelDb.map (next (6)) / 20.0f);

            auto l = left[i], r = right[i];

            if (drive > 0.0f)
            {
                const auto g = std::pow (10.0f, drive / 20.0f);
                l = juce::jlimit (-1.0f, 1.0f, g * l);
                r = juce::jlimit (-1.0f, 1.0f, g * r);
            }

            if (preFilter > 0.0f)
            {
                // From 16 times the reduced Nyquist (barely there) down to the reduced Nyquist itself,
                // two poles (12 dB an octave): one alone is too gentle to keep much from folding.
                const auto corner = (float) sampleRate / (2.0f * period) * std::exp2 (4.0f * (1.0f - preFilter));
                for (auto& stage : pre)
                    stage.setCutoff (corner, sampleRate);
                l = pre[1].process (pre[0].process (l));
                r = pre[3].process (pre[2].process (r));
            }

            counter += 1.0f;
            if (counter >= hold)
            {
                counter -= hold;
                hold = jitter > 0.0f ? juce::jmax (1.0f, period * (1.0f + jitter * (random.nextFloat() - 0.5f) * 1.6f)) : period;
                heldLeft  = std::round (l * levels) / levels;
                heldRight = std::round (r * levels) / levels;
            }

            l = heldLeft;
            r = heldRight;

            if (tone < toneHz.high - 1.0f)
            {
                post[0].setCutoff (tone, sampleRate);
                post[1].setCutoff (tone, sampleRate);
                l = post[0].process (l);
                r = post[1].process (r);
            }

            left[i] = l * level;
            right[i] = r * level;
        }
    }

protected:
    void onReset() override
    {
        counter = 0.0f;
        hold = Dsp::Map::downsample (target (1));
        heldLeft = heldRight = 0.0f;
        random.setSeed (0x5eed);

        for (auto& stage : pre)
            stage.reset();

        for (int c = 0; c < 2; ++c)
        {
            post[c].reset();
        }
    }

private:
    Dsp::OnePole pre[4], post[2];   // pre: two stages per channel
    juce::Random random { 0x5eed };
    float counter = 0.0f, hold = 1.0f, heldLeft = 0.0f, heldRight = 0.0f;
};
