//
//  Phaser.h
//  Rackbox Plus
//
//  First-order allpass stages swept by an LFO, with feedback. The notches come
//  from adding the allpassed signal to the dry one, and the output does that
//  itself, so the effect does not depend on the block's Mix. Coefficients are
//  recomputed every eight samples - an LFO of a few hertz moves nowhere near far
//  enough in that time to be heard stepping.
//
//  The knobs, in order:
//    Rate      the sweep speed (or a note value with Sync).
//    Depth     how many octaves the sweep covers, up to five.
//    Center    the middle of the sweep, 200 Hz to 3.2 kHz.
//    Feedback  -90 to +90 %: positive deepens the notches into peaks, negative
//              moves the peaks between them (a hollower sound).
//    Stages    2 to 12 allpasses, in pairs: each pair is one more notch.
//    Spread    how far apart left and right sweep: 0 mono, 100 % half a cycle.
//    Shape     the sweep's waveform: sine, triangle (at 50 %), square.
//    Blend     how much of the allpassed signal is mixed with the dry: 50 % gives
//              the deepest notches, more or less makes them shallower.
//
//  At its starting values (800 Hz, +50 %, six stages, a quarter cycle apart,
//  sine, 50 %) it is Rackbox's phaser. Changing Stages while it plays switches
//  the chain at once and can click; it is a set-and-leave knob.
//

#pragma once

#include "../Effect.h"
#include "../Dsp.h"

class Phaser : public Effect
{
public:
    static constexpr int maxStages = 12;

    static constexpr Dsp::Range centerHz { 200.0f, 3200.0f, Dsp::Range::Curve::exponential };
    static constexpr Dsp::Range feedback { -0.9f, 0.9f };
    static constexpr Dsp::Range stages   { 2.0f, 12.0f, Dsp::Range::Curve::linear, 2.0f };

    void process (float* left, float* right, int n) noexcept override
    {
        float* channels[2] { left, right };

        if (phaseLocked())
            lfo.pull (syncPhase(), lockAmount (n), n);

        for (int i = 0; i < n; ++i)
        {
            lfo.setRate (lfoRateHz (nextA()), sampleRate);
            const auto depth = nextB();
            const auto center = centerHz.map (next (2));
            const auto fb = feedback.map (next (3));
            const auto count = (int) stages.map (next (4));
            const auto apart = 0.5f * next (5);
            const auto shape = next (6);
            const auto blend = next (7);

            for (int c = 0; c < 2; ++c)
            {
                auto& s = side[c];

                if (--s.countdown < 0)
                {
                    s.countdown = 7;
                    const auto sweep = lfo.at (apart * (float) c, shape);
                    const auto corner = center * std::exp2 (sweep * depth * 2.5f);
                    const auto t = std::tan (Dsp::pi * std::fmin (corner, (float) (0.45 * sampleRate)) / (float) sampleRate);
                    s.coefficient = (t - 1.0f) / (t + 1.0f);
                }

                const auto in = channels[c][i];
                auto x = in + Dsp::softClip (fb * s.last);

                for (int k = 0; k < count; ++k)
                {
                    auto& z = s.state[k];
                    const auto y = s.coefficient * x + z;   // transposed direct form II allpass
                    z = x - s.coefficient * y;
                    x = y;
                }

                s.last = x;
                channels[c][i] = blend == 0.5f ? 0.5f * (in + x) : (1.0f - blend) * in + blend * x;
            }

            lfo.advance();
        }
    }

protected:
    void onReset() override
    {
        for (auto& s : side)
            s = {};

        lfo.setPhase (0.0f);
    }

private:
    struct Side
    {
        float state[maxStages] {};
        float coefficient = 0.0f, last = 0.0f;
        int countdown = 0;
    };

    Dsp::Lfo lfo;   // the right channel reads it Spread later
    Side side[2];
};
