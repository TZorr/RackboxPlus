//
//  TapeDelay.h
//  Rackbox Plus
//
//  The Digital delay with a tape machine's faults added on purpose.
//
//  - Every repeat is darker: a low-pass (4.5 kHz) and a soft saturator sit in
//    the feedback path, so a long trail recedes instead of piling up bright
//    copies. The saturator is on the recirculated part only, so the first
//    echo is clean and the tenth is warm.
//  - The transport wobbles: a slow "wow" (0.6 Hz, +-0.35 ms) and a fast
//    "flutter" (6.3 Hz, +-0.05 ms) modulate the read position, and the two
//    channels use different phases so the echoes drift apart a little.
//  - The head is slow: Time glides for 150 ms, which on a tape delay is the
//    pitch-dive everybody knows.
//
//  Time and Feedback mean what they mean on the Digital one.
//
//  The knobs, in order:
//    Time        10 ms to 1 s, or a note value with Sync.
//    Feedback    the share returned, up to 95 %.
//    Tone        the low-pass on every repeat, 1 to 20 kHz (4.5 kHz at start).
//    Saturation  how hard the recirculated echoes hit the saturator: little is
//                Rackbox's warmth, much is a trail that compresses and grits up.
//    Wow         the slow wobble's depth, 0 to 200 % of Rackbox's 0.35 ms.
//    Flutter     the fast wobble's depth, 0 to 200 % of Rackbox's 0.05 ms.
//    Low Cut     a high-pass on every repeat, 20 Hz to 1 kHz; off at the bottom.
//    Width       0 mono, 100 % as the two drifting channels give it, 150 % wider.
//
//  At its starting values (4.5 kHz, Rackbox's saturation, 100 % wow and
//  flutter, Low Cut off, 100 % wide) it is Rackbox's tape delay.
//

#pragma once

#include "../Effect.h"
#include "../Dsp.h"

class TapeDelay : public Effect
{
public:
    static constexpr Dsp::Range toneHz   { 1000.0f, 20000.0f, Dsp::Range::Curve::exponential };
    static constexpr Dsp::Range drive    { 1.0f, 10.0f, Dsp::Range::Curve::exponential };
    static constexpr Dsp::Range wobble   { 0.0f, 2.0f };
    static constexpr Dsp::Range lowCutHz { 20.0f, 1000.0f, Dsp::Range::Curve::exponential };
    static constexpr Dsp::Range width    { 0.0f, 1.5f };

    void process (float* left, float* right, int n) noexcept override
    {
        float* channels[2] { left, right };

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
            const auto toneCut = toneHz.map (next (2));
            const auto push = drive.map (next (3));
            const auto wowDepth = 0.00035f * (float) sampleRate * wobble.map (next (4));
            const auto flutterDepth = 0.00005f * (float) sampleRate * wobble.map (next (5));
            const auto lowCut = lowCutHz.map (next (6));
            const auto spread = width.map (next (7));

            float out[2];

            for (int c = 0; c < 2; ++c)
            {
                const auto move = wowDepth * wow[c].next() + flutterDepth * flutter[c].next();

                tone[c].setCutoff (toneCut, sampleRate);
                auto delayed = tone[c].process (line[c].read (juce::jmax (1.0f, time - 1.0f + move)));
                delayed = band[c].process (delayed, lowCut, 20000.0f, sampleRate);

                line[c].push (channels[c][i] + feedback * Dsp::softClip (push * delayed) / push);
                out[c] = delayed;
            }

            Dsp::widen (out[0], out[1], spread);
            left[i] = out[0];
            right[i] = out[1];
        }
    }

protected:
    void onPrepare() override
    {
        for (auto& l : line)
            l.allocate ((int) ((maxSeconds + 0.05) * sampleRate));

        for (int c = 0; c < 2; ++c)
        {
            wow[c].setRate (0.6f, sampleRate);
            flutter[c].setRate (6.3f, sampleRate);
        }

        glide = 1.0f - std::exp (-1.0f / (0.15f * (float) sampleRate));
    }

    void onReset() override
    {
        for (int c = 0; c < 2; ++c)
        {
            line[c].clear();
            tone[c].reset();
            band[c].reset();
            wow[c].setPhase (0.5f * (float) c);
            flutter[c].setPhase (0.33f * (float) c);
        }

        fresh = true;
    }

private:
    Dsp::DelayLine line[2];
    Dsp::OnePole tone[2];
    Dsp::BandLimit band[2];
    Dsp::Lfo wow[2], flutter[2];
    static constexpr float maxSeconds = 4.0f;   // a whole note at 60 BPM
    float time = 1000.0f, glide = 0.001f;
    bool fresh = true;
};
