//
//  PingPongDelay.h
//  Rackbox Plus
//
//  The echoes bounce between the channels: the mono sum of the input goes
//  into the left line, the left line feeds the right one, and the right one
//  feeds back into the left. So the first echo is on the left, the second on
//  the right, and each repeat takes two delay times to return to a side.
//
//  Time is the interval between bounces (10 ms to 1 s), Feedback the share that
//  keeps bouncing. Time glides for 60 ms, as in the Digital delay.
//
//  The knobs, in order:
//    Time       the interval between bounces, 10 ms to 1 s, or a note value.
//    Feedback   the share that keeps bouncing, up to 95 %.
//    Low Cut    a high-pass on every bounce, 20 Hz to 1 kHz; off at the bottom.
//    High Cut   a low-pass on every bounce, 1 to 20 kHz; off at the top.
//    Width      how far the bounces travel: 0 mono, 100 % from speaker to
//               speaker, 150 % beyond.
//    Swing      -50 to +50 %: the right bounce comes that much later (or
//               earlier) than the left, so the pair swings instead of ticking.
//    Mod Depth  0 to 5 ms of a slow (0.5 Hz) drift in the read positions.
//
//  At its starting values (filters open, 100 % wide, straight, no drift) it is
//  Rackbox's ping-pong delay.
//

#pragma once

#include "../Effect.h"
#include "../Dsp.h"

class PingPongDelay : public Effect
{
public:
    static constexpr Dsp::Range lowCutHz   { 20.0f, 1000.0f, Dsp::Range::Curve::exponential };
    static constexpr Dsp::Range highCutHz  { 1000.0f, 20000.0f, Dsp::Range::Curve::exponential };
    static constexpr Dsp::Range width      { 0.0f, 1.5f };
    static constexpr Dsp::Range swing      { -0.5f, 0.5f };
    static constexpr Dsp::Range modDepthMs { 0.0f, 5.0f };

    void process (float* left, float* right, int n) noexcept override
    {
        for (int i = 0; i < n; ++i)
        {
            const auto target = delaySamples (nextA(), maxSeconds);
            if (fresh)
            {
                time = target;   // the first sample after a reset starts where the knob is, tempo included
                fresh = false;
            }

            time += glide * (target - time);
            const auto feedback = 0.95f * nextB();
            const auto lowCut = lowCutHz.map (next (2));
            const auto highCut = highCutHz.map (next (3));
            const auto spread = width.map (next (4));
            const auto rightTime = time * (1.0f + swing.map (next (5)));
            const auto depth = modDepthMs.map (next (6)) * 0.001f * (float) sampleRate;

            float l, r;

            if (depth > 0.0f)
            {
                l = lineLeft.read (juce::jmax (1.0f, time - 1.0f + depth * (1.0f + lfo.at (0.0f))));
                r = lineRight.read (juce::jmax (1.0f, rightTime - 1.0f + depth * (1.0f + lfo.at (0.25f))));
            }
            else
            {
                l = lineLeft.read (time - 1.0f);
                r = lineRight.read (rightTime - 1.0f);
            }

            lfo.advance();
            l = band[0].process (l, lowCut, highCut, sampleRate);
            r = band[1].process (r, lowCut, highCut, sampleRate);

            lineLeft.push (0.5f * (left[i] + right[i]) + feedback * r);
            lineRight.push (l);

            Dsp::widen (l, r, spread);
            left[i] = l;
            right[i] = r;
        }
    }

protected:
    void onPrepare() override
    {
        lineLeft.allocate ((int) ((maxSeconds + 0.06) * sampleRate));
        lineRight.allocate ((int) ((maxSeconds * (1.0f + swing.high) + 0.06) * sampleRate));
        glide = 1.0f - std::exp (-1.0f / (0.06f * (float) sampleRate));
        lfo.setRate (0.5f, sampleRate);
    }

    void onReset() override
    {
        lineLeft.clear();
        lineRight.clear();
        band[0].reset();
        band[1].reset();
        lfo.setPhase (0.0f);
        fresh = true;
    }

private:
    Dsp::DelayLine lineLeft, lineRight;
    Dsp::BandLimit band[2];
    Dsp::Lfo lfo;
    static constexpr float maxSeconds = 4.0f;   // a whole note at 60 BPM
    float time = 1000.0f, glide = 0.001f;
    bool fresh = true;
};
