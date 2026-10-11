//
//  FreqShifter.h
//  Rackbox Plus
//
//  A frequency shifter (single-sideband, after Harald Bode's). Shift moves
//  every frequency in the signal by the same number of Hz, -1000 to +1000,
//  0 at the knob's centre. Feedback sends the shifted signal round again.
//
//  Shifting is not pitch-shifting. A pitch shift multiplies frequencies and
//  keeps a note's harmonics in tune with each other; a shift adds the same Hz
//  to all of them, so 100, 200 and 300 Hz shifted up by 50 become 150, 250
//  and 350 - no longer a harmonic series. A few Hz is a slow beating, like a
//  phaser that never turns round; tens of Hz is metallic; hundreds is a voice
//  from a different planet.
//
//  How. Dsp::QuadraturePair splits the signal into two parts 90 degrees apart,
//  which together describe each frequency as a turning pointer. Turning every
//  pointer further by a steady rate - multiplying by a cosine and a sine of the
//  shift and taking the difference - moves every frequency by that rate.
//  Multiplying by a cosine alone (a ring modulator) would make two copies, one
//  moved up and one down; the 90 degree part is what cancels the second one.
//  Below zero, frequencies go through 0 Hz and come back mirrored.
//
//  Feedback. The output goes back into the input after 8 ms, so every pass is
//  shifted once more. That is the barber-pole: the 8 ms loop is a comb filter
//  whose teeth move by the shift on every pass, so with a few Hz of shift the
//  comb climbs (or falls) for ever without ever arriving. At larger shifts it
//  smears notes into clangorous clusters. The loop has a DC blocker (a shifted
//  signal near 0 Hz is no longer split cleanly) and a soft clip, so at the top
//  of the Feedback knob it rings loud but never runs away; the output is
//  pulled down as Feedback rises, which keeps it roughly as loud as it went in.
//
//  The quadrature pair is an allpass, so even at a shift of 0 Hz the output
//  is the input with its phases turned, not the input itself. On its own that
//  is inaudible; blended with the dry signal on the block's Mix it colours
//  like a phaser standing still.
//
//  The knobs, in order:
//    Shift     -1000 to +1000 Hz, 0 at the centre (cubic: fine near 0).
//    Fine      -10 to +10 Hz added to it: the slow beatings, set exactly.
//    Feedback  how much goes round the loop again, up to 90 %.
//    Loop      the loop's delay, 1 to 200 ms: short is the barber-pole comb,
//              long a run of echoes each shifted further - a spiral.
//    Spread    the right channel's shift against the left's: 0 the same, 100 %
//              the opposite way (left rises while right falls), so the image
//              turns instead of moving as one.
//    Low Cut   a high-pass on the result (and so on what is fed back), 20 Hz to
//              1 kHz; at the bottom it is off.
//    High Cut  a low-pass on the same, 1 to 20 kHz; at the top it is off. With
//              feedback, each pass loses more top: a spiral that darkens.
//
//  At its starting values (Fine 0, 8 ms loop, no spread, filters open) it is
//  Rackbox's shifter.
//

#pragma once

#include "../Effect.h"
#include "../Dsp.h"

class FreqShifter : public Effect
{
public:
    static constexpr float maxFeedback = 0.9f;

    static constexpr Dsp::Range fineHz    { -10.0f, 10.0f };
    static constexpr Dsp::Range loopMs    { 1.0f, 200.0f, Dsp::Range::Curve::exponential };
    static constexpr Dsp::Range lowCutHz  { 20.0f, 1000.0f, Dsp::Range::Curve::exponential };
    static constexpr Dsp::Range highCutHz { 1000.0f, 20000.0f, Dsp::Range::Curve::exponential };

    void process (float* left, float* right, int n) noexcept override
    {
        float* channels[2] { left, right };

        for (int i = 0; i < n; ++i)
        {
            const auto shift = Dsp::Map::shiftHz (nextA()) + fineHz.map (nextB());
            const auto feedback = maxFeedback * next (2);
            // Read before this sample is pushed, so "0 samples ago" is the previous one.
            const auto loopDelay = juce::jmax (0.0f, loopMs.map (next (3)) * 0.001f * (float) sampleRate - 1.0f);
            const auto spread = next (4);
            const auto lowCut = lowCutHz.map (next (5));
            const auto highCut = highCutHz.map (next (6));
            const auto makeUp = 1.0f - 0.5f * feedback;

            const float shifts[2] { shift, shift * (1.0f - 2.0f * spread) };

            for (int c = 0; c < 2; ++c)
            {
                const auto turn = (float) (2.0 * juce::MathConstants<double>::pi * phase[c]);
                const auto cosine = std::cos (turn), sine = std::sin (turn);
                phase[c] += (double) shifts[c] / sampleRate;
                phase[c] -= std::floor (phase[c]);

                const auto back = Dsp::softClip (dc[c].process (loop[c].read (loopDelay)));

                float inPhase, quadrature;
                split[c].process (channels[c][i] + feedback * back, inPhase, quadrature);

                auto shifted = inPhase * cosine - quadrature * sine;

                if (lowCut > lowCutHz.low + 0.01f)
                {
                    lowCutFilter[c].setCutoff (lowCut, sampleRate);
                    shifted = lowCutFilter[c].process (shifted);
                }

                if (highCut < highCutHz.high - 1.0f)
                {
                    highCutFilter[c].setCutoff (highCut, sampleRate);
                    shifted = highCutFilter[c].process (shifted);
                }

                loop[c].push (shifted);
                channels[c][i] = shifted * makeUp;
            }
        }
    }

protected:
    void onPrepare() override
    {
        for (auto& l : loop)
            l.allocate ((int) (0.21 * sampleRate));
    }

    void onReset() override
    {
        for (int c = 0; c < 2; ++c)
        {
            phase[c] = 0.0;
            split[c].reset();
            loop[c].clear();
            dc[c].reset();
            lowCutFilter[c].reset();
            highCutFilter[c].reset();
        }
    }

private:
    Dsp::QuadraturePair split[2];
    Dsp::DelayLine loop[2];
    Dsp::DcBlocker dc[2];
    Dsp::OnePoleHighPass lowCutFilter[2];
    Dsp::OnePole highCutFilter[2];
    double phase[2] {};
};
