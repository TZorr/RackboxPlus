//
//  Chorus.h
//  Rackbox Plus
//
//  Detuned copies of the signal per channel, each read from a delay line whose
//  length is swept by its own sine LFO. The copies are what make it a chorus:
//  the sweep changes the delay, a changing delay is a pitch shift, and the ear
//  hears slightly different pitches beating against the dry signal. The output
//  already contains half of the dry signal - a chorus of nothing but the
//  shifted copies is a vibrato.
//
//  The knobs, in order:
//    Rate      the sweep speed (or a note value with Sync).
//    Depth     how far each delay travels, up to 6 ms either way.
//    Delay     the centre of the first copy, 5 to 30 ms; each further copy sits
//              40 % further out (short: a doubled, flanger-ish shimmer; long: a
//              wide ensemble that starts to read as separate voices).
//    Voices    one to four copies. Free-running, each sweeps 13 % faster than
//              the one before, so they never lock into a pattern; synced, all
//              keep the grid's rate. They start a half and then a quarter cycle
//              apart. A change of count fades the copies in or out over 20 ms.
//    Spread    how far apart left and right sweep: 0 is mono, 100 % half a cycle
//              (the widest image), 50 % a quarter. Both channels follow one LFO
//              per voice, the right one reading it later, so Spread moves the
//              image at once, not at the next reset.
//    Feedback  the copies fed back into the line, -70 to +70 %: a metallic edge,
//              towards a flanger at short delays.
//    Tone      a low-pass on the copies, 1 to 20 kHz; at the top it is off.
//    Low Cut   a high-pass on the copies, 20 Hz to 1 kHz; at the bottom it is off.
//              Keeps a chorused bass from wobbling in pitch.
//
//  At its starting values (two voices, 14 ms, a quarter cycle, no feedback,
//  filters off) it is Rackbox's chorus.
//

#pragma once

#include "../Effect.h"
#include "../Dsp.h"

class Chorus : public Effect
{
public:
    static constexpr int maxVoices = 4;

    static constexpr Dsp::Range delayMs   { 5.0f, 30.0f, Dsp::Range::Curve::exponential };
    static constexpr Dsp::Range voices    { 1.0f, 4.0f, Dsp::Range::Curve::linear, 1.0f };
    static constexpr Dsp::Range feedback  { -0.7f, 0.7f };
    static constexpr Dsp::Range toneHz    { 1000.0f, 20000.0f, Dsp::Range::Curve::exponential };
    static constexpr Dsp::Range lowCutHz  { 20.0f, 1000.0f, Dsp::Range::Curve::exponential };

    void process (float* left, float* right, int n) noexcept override
    {
        float* channels[2] { left, right };

        if (phaseLocked())
        {
            const auto base = syncPhase(), amount = lockAmount (n);

            for (int v = 0; v < maxVoices; ++v)
                lfo[v].pull (base + voiceOffset[v], amount, n);
        }

        for (int i = 0; i < n; ++i)
        {
            const auto rate = lfoRateHz (nextA());
            const auto excursion = nextB() * 0.006f * (float) sampleRate;
            const auto centre = delayMs.map (next (2)) * 0.001f * (float) sampleRate;
            const auto count = (int) voices.map (next (3));
            const auto apart = 0.5f * next (4);   // right reads each sweep this part of a cycle later
            const auto fb = feedback.map (next (5));
            const auto tone = toneHz.map (next (6));
            const auto lowCut = lowCutHz.map (next (7));

            for (int v = 0; v < maxVoices; ++v)
            {
                const auto goal = v < count ? 1.0f : 0.0f;
                weight[v] = goal > weight[v] ? juce::jmin (goal, weight[v] + fadeStep)
                                             : juce::jmax (goal, weight[v] - fadeStep);
            }

            for (int v = 0; v < maxVoices; ++v)   // synced, all sweeps must keep the grid's rate
                lfo[v].setRate (rate * (syncOn ? 1.0f : 1.0f + 0.13f * (float) v), sampleRate);

            for (int c = 0; c < 2; ++c)
            {
                const auto x = channels[c][i];
                line[c].push (fb == 0.0f ? x : x + fb * Dsp::softClip (wetBefore[c]));

                float sum = 0.0f, total = 0.0f;

                for (int v = 0; v < maxVoices; ++v)
                    if (weight[v] > 0.0f)
                    {
                        const auto sweep = lfo[v].at (apart * (float) c);
                        sum += weight[v] * line[c].read (centre * (1.0f + 0.4f * (float) v) + excursion * sweep);
                        total += weight[v];
                    }

                auto wet = total > 0.0f ? sum / total : 0.0f;
                wetBefore[c] = wet;

                if (tone < toneHz.high - 1.0f)
                {
                    toneFilter[c].setCutoff (tone, sampleRate);
                    wet = toneFilter[c].process (wet);
                }

                if (lowCut > lowCutHz.low + 0.01f)
                {
                    lowCutFilter[c].setCutoff (lowCut, sampleRate);
                    wet = lowCutFilter[c].process (wet);
                }

                channels[c][i] = 0.5f * x + 0.5f * wet;
            }

            for (auto& l : lfo)
                l.advance();
        }
    }

protected:
    void onPrepare() override
    {
        for (auto& l : line)
            l.allocate ((int) (0.1 * sampleRate));

        fadeStep = 1.0f / (0.02f * (float) sampleRate);
    }

    void onReset() override
    {
        const auto count = (int) voices.map (target (3));

        for (int c = 0; c < 2; ++c)
        {
            line[c].clear();
            toneFilter[c].reset();
            lowCutFilter[c].reset();
            wetBefore[c] = 0.0f;
        }

        for (int v = 0; v < maxVoices; ++v)
        {
            lfo[v].setPhase (voiceOffset[v]);
            weight[v] = v < count ? 1.0f : 0.0f;
        }
    }

private:
    static constexpr float voiceOffset[maxVoices] { 0.0f, 0.5f, 0.25f, 0.75f };

    Dsp::DelayLine line[2];
    Dsp::Lfo lfo[maxVoices];   // one per voice; the right channel reads it Spread later
    Dsp::OnePole toneFilter[2];
    Dsp::OnePoleHighPass lowCutFilter[2];
    float weight[maxVoices] {}, wetBefore[2] {};
    float fadeStep = 0.001f;
};
