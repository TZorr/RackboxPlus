//
//  DigitalDelay.h
//  Rackbox Plus
//
//  A clean stereo delay: one line per channel, feedback straight back in.
//
//  Time is 10 ms to 1 s on an exponential knob (the ear hears time in ratios), or - with
//  Sync on - one of fourteen note values at the host tempo, up to four seconds (a whole
//  note at 60 BPM; slower tempi are held at four).
//  Feedback is the fraction returned, up to 95 %. What comes out is the echoes
//  only - the block's Mix knob puts the dry signal back.
//
//  Turning Time glides the read position (about 60 ms) instead of jumping it.
//  A jump lands on a different part of every repeat still in the line and is a
//  click on each; a glide bends their pitch briefly, the lesser fault.
//
//  The knobs, in order:
//    Time       10 ms to 1 s, or a note value with Sync.
//    Feedback   the share returned, up to 95 %.
//    Low Cut    a high-pass on the echoes, 20 Hz to 1 kHz, inside the loop:
//               every repeat thinner than the last. Off at the bottom.
//    High Cut   a low-pass on the echoes, 1 to 20 kHz, inside the loop: every
//               repeat darker. Off at the top.
//    Offset     the right channel's time against the left's, -50 to +50 %: the
//               echoes split into two rhythms, the classic wide stereo delay.
//    Width      0 mono, 100 % as the lines give it, 150 % wider.
//    Mod Rate   0.1 to 10 Hz, the speed of a sine moving the read positions.
//    Mod Depth  0 to 5 ms of that movement: a chorused, drifting echo. 0 is off.
//
//  At its starting values (filters open, no offset, 100 % wide, no mod) it is
//  Rackbox's digital delay.
//

#pragma once

#include "../Effect.h"
#include "../Dsp.h"

class DigitalDelay : public Effect
{
public:
    static constexpr Dsp::Range lowCutHz   { 20.0f, 1000.0f, Dsp::Range::Curve::exponential };
    static constexpr Dsp::Range highCutHz  { 1000.0f, 20000.0f, Dsp::Range::Curve::exponential };
    static constexpr Dsp::Range offset     { -0.5f, 0.5f };
    static constexpr Dsp::Range width      { 0.0f, 1.5f };
    static constexpr Dsp::Range modRateHz  { 0.1f, 10.0f, Dsp::Range::Curve::exponential };
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
            const auto rightTime = time * (1.0f + offset.map (next (4)));
            const auto spread = width.map (next (5));
            lfo.setRate (modRateHz.map (next (6)), sampleRate);
            const auto depth = modDepthMs.map (next (7)) * 0.001f * (float) sampleRate;

            float l, r;

            if (depth > 0.0f)
            {
                l = line[0].read (juce::jmax (1.0f, time - 1.0f + depth * (1.0f + lfo.at (0.0f))));
                r = line[1].read (juce::jmax (1.0f, rightTime - 1.0f + depth * (1.0f + lfo.at (0.25f))));
            }
            else
            {
                l = line[0].read (time - 1.0f);
                r = line[1].read (rightTime - 1.0f);
            }

            lfo.advance();
            l = band[0].process (l, lowCut, highCut, sampleRate);
            r = band[1].process (r, lowCut, highCut, sampleRate);

            line[0].push (left[i]  + feedback * l);
            line[1].push (right[i] + feedback * r);

            Dsp::widen (l, r, spread);
            left[i] = l;
            right[i] = r;
        }
    }

protected:
    void onPrepare() override
    {
        for (auto& l : line)
            l.allocate ((int) ((maxSeconds * (1.0f + offset.high) + 0.06) * sampleRate));

        glide = 1.0f - std::exp (-1.0f / (0.06f * (float) sampleRate));
    }

    void onReset() override
    {
        for (int c = 0; c < 2; ++c)
        {
            line[c].clear();
            band[c].reset();
        }

        lfo.setPhase (0.0f);
        fresh = true;
    }

private:
    Dsp::DelayLine line[2];
    Dsp::BandLimit band[2];
    Dsp::Lfo lfo;
    static constexpr float maxSeconds = 4.0f;   // a whole note at 60 BPM
    float time = 1000.0f, glide = 0.001f;
    bool fresh = true;
};
