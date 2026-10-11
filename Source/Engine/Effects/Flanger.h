//
//  Flanger.h
//  Rackbox Plus
//
//  One short delay, swept, fed back on itself. The comb filter a flanger makes
//  has its notches spaced 1/delay apart, so sweeping the delay sweeps the
//  notches - and feedback turns the notches into a ring. Feedback is taken from
//  the delayed signal, not from the output, so the dry half added at the end
//  never re-enters the loop.
//
//  The knobs, in order:
//    Rate      the sweep speed (or a note value with Sync).
//    Depth     how far the delay travels: at 100 % from a sixth of the centre to
//              nearly twice it.
//    Delay     the centre of the sweep, 0.5 to 10 ms: short is a jet, long a
//              slow, chorus-like comb.
//    Feedback  -95 to +95 %: positive rings at the comb's peaks, negative an
//              octave lower and hollower.
//    Spread    how far apart left and right sweep: 0 mono, 100 % half a cycle.
//    Shape     the sweep's waveform: sine, triangle (at 50 %), square.
//    Tone      a low-pass inside the feedback loop, 1 to 20 kHz: each pass
//              darker, so a long ring softens instead of whistling; at the top
//              it is off.
//
//  At its starting values (3 ms, +55 %, a quarter cycle, sine, Tone open) it
//  is Rackbox's flanger, one sample shorter: there the read came before the
//  write, so every delay was a sample longer than set.
//

#pragma once

#include "../Effect.h"
#include "../Dsp.h"

class Flanger : public Effect
{
public:
    static constexpr Dsp::Range delayMs  { 0.5f, 10.0f, Dsp::Range::Curve::exponential };
    static constexpr Dsp::Range feedback { -0.95f, 0.95f };
    static constexpr Dsp::Range toneHz   { 1000.0f, 20000.0f, Dsp::Range::Curve::exponential };

    void process (float* left, float* right, int n) noexcept override
    {
        float* channels[2] { left, right };

        if (phaseLocked())
            lfo.pull (syncPhase(), lockAmount (n), n);

        for (int i = 0; i < n; ++i)
        {
            lfo.setRate (lfoRateHz (nextA()), sampleRate);
            const auto depth = nextB();
            const auto centre = delayMs.map (next (2)) * 0.001f * (float) sampleRate;
            const auto fb = feedback.map (next (3));
            const auto apart = 0.5f * next (4);
            const auto shape = next (5);
            const auto tone = toneHz.map (next (6));

            for (int c = 0; c < 2; ++c)
            {
                const auto delay = centre * (1.0f + 0.8333333f * depth * lfo.at (apart * (float) c, shape));
                // Read before this sample is pushed, so "0 samples ago" is the previous one: one
                // sample less, and Delay is exactly the delay (Rackbox's flanger is one sample long).
                const auto delayed = line[c].read (juce::jmax (0.0f, delay - 1.0f));
                const auto x = channels[c][i];

                auto back = delayed;
                if (tone < toneHz.high - 1.0f)
                {
                    toneFilter[c].setCutoff (tone, sampleRate);
                    back = toneFilter[c].process (back);
                }

                line[c].push (x + fb * Dsp::softClip (back));
                channels[c][i] = 0.5f * (x + delayed);
            }

            lfo.advance();
        }
    }

protected:
    void onPrepare() override
    {
        for (auto& l : line)
            l.allocate ((int) (0.025 * sampleRate));
    }

    void onReset() override
    {
        for (int c = 0; c < 2; ++c)
        {
            line[c].clear();
            toneFilter[c].reset();
        }

        lfo.setPhase (0.0f);
    }

private:
    Dsp::DelayLine line[2];
    Dsp::OnePole toneFilter[2];
    Dsp::Lfo lfo;   // the right channel reads it Spread later
};
