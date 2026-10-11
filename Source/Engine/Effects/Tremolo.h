//
//  Tremolo.h
//  Rackbox Plus
//
//  Amplitude modulation: the gain runs between 1 and 1 - Depth.
//
//  The knobs, in order:
//    Rate      the speed (or a note value with Sync).
//    Depth     how far down the gain goes.
//    Shape     the waveform: sine, triangle (at 50 %), square - a chop.
//    Stereo    how far the right channel's gain is shifted against the left:
//              0 is a tremolo, 100 % half a cycle - an auto-pan, the sound
//              moving between the speakers instead of pulsing in place.
//    Skew      -100 to +100 %: negative makes the gain fall fast and rise
//              slowly (a pulse that swells back), positive the opposite.
//
//  With Sync on and the host playing, the sweep sits on the beat grid: the gain
//  is at its top on the downbeat of the note value chosen, so a 1/8 tremolo
//  chops in time. At its starting values (sine, mono, no skew) it is Rackbox's
//  tremolo.
//

#pragma once

#include "../Effect.h"
#include "../Dsp.h"

class Tremolo : public Effect
{
public:
    static constexpr Dsp::Range skew { -1.0f, 1.0f };

    void process (float* left, float* right, int n) noexcept override
    {
        if (phaseLocked())
            lfo.pull (syncPhase(), lockAmount (n), n);

        for (int i = 0; i < n; ++i)
        {
            lfo.setRate (lfoRateHz (nextA()), sampleRate);
            const auto depth = nextB();
            const auto shape = next (2);
            const auto apart = 0.5f * next (3);
            const auto bend = 0.5f + 0.45f * skew.map (next (4));

            left[i]  *= 1.0f - depth * 0.5f * (1.0f + lfo.at (0.0f, shape, bend));
            right[i] *= 1.0f - depth * 0.5f * (1.0f + lfo.at (apart, shape, bend));
            lfo.advance();
        }
    }

protected:
    void onReset() override     { lfo.setPhase (0.0f); }

private:
    Dsp::Lfo lfo;
};
