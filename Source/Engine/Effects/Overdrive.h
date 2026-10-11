//
//  Overdrive.h
//  Rackbox Plus
//
//  A soft clipper. Drive is the gain into it (0 to 36 dB), Tone a low-pass
//  after it (800 Hz to 14 kHz) that takes the fizz off.
//
//  The shaper is symmetric, so it adds odd harmonics only and no DC. The
//  output is pulled down by drive^0.4: a saturator gets louder as it is
//  pushed, but by much less than its gain, and this keeps the block roughly
//  level-matched across the Drive knob without making the low settings quiet.
//
//  The shaper is not oversampled but anti-aliased (Dsp::Adaa): it returns the
//  curve's average between two samples rather than its value at each, which
//  keeps most of the harmonics above Nyquist from folding back as grit - with
//  no latency, for half a sample of delay. The top end that averaging softens
//  is lifted back (Dsp::AdaaCompensation), so the Tone knob sounds as before.
//
//  The knobs, in order:
//    Drive      the gain into the shaper, 0 to 36 dB.
//    Tone       a low-pass after it, 800 Hz to 14 kHz.
//    Low Cut    a high-pass before it, 20 Hz to 1 kHz: less bass into the
//               shaper is a tighter drive that keeps its low end from mushing.
//               Off at the bottom.
//    Asymmetry  a bias into the shaper, so the two halves clip at different
//               points: even harmonics, a warmer, tube-like edge. The DC it
//               makes is taken out after.
//    Character  from the soft curve (0) to a hard clip (100 %), blended: the
//               flat top of a transistor stage. Both halves are anti-aliased.
//    Level      -12 to +12 dB after everything.
//
//  At its starting values (Low Cut off, no asymmetry, soft, 0 dB) it is
//  Rackbox's overdrive with a DC blocker added, which the bias needs and which
//  takes nothing from a symmetric signal above 20 Hz.
//

#pragma once

#include "../Effect.h"
#include "../Dsp.h"

class Overdrive : public Effect
{
public:
    static constexpr Dsp::Range lowCutHz  { 20.0f, 1000.0f, Dsp::Range::Curve::exponential };
    static constexpr Dsp::Range asymmetry { 0.0f, 0.5f };
    static constexpr Dsp::Range levelDb   { -12.0f, 12.0f };

    void process (float* left, float* right, int n) noexcept override
    {
        float* channels[2] { left, right };

        for (int i = 0; i < n; ++i)
        {
            const auto gain = std::pow (10.0f, 36.0f * nextA() / 20.0f);
            const auto cutoff = Dsp::Map::exponential (nextB(), 800.0f, 14000.0f);
            const auto makeUp = std::pow (gain, -0.4f);
            const auto lowCut = lowCutHz.map (next (2));
            const auto bias = asymmetry.map (next (3));
            const auto hard = next (4);
            const auto level = std::pow (10.0f, levelDb.map (next (5)) / 20.0f);

            // What the curve gives at rest, so silence comes out as silence.
            const auto offset = (1.0f - hard) * Dsp::softClip (bias) + hard * juce::jlimit (-1.0f, 1.0f, bias);

            for (int c = 0; c < 2; ++c)
            {
                tone[c].setCutoff (cutoff, sampleRate);

                const auto x = band[c].process (channels[c][i], lowCut, 20000.0f, sampleRate);
                const auto u = gain * x + bias;
                // Both curves run all the time, so either is ready the moment Character moves.
                const auto softPart = lift[c].process (soft[c].process (u));
                const auto hardPart = liftHard[c].process (clip[c].process (u));
                const auto shaped = hard > 0.0f ? (1.0f - hard) * softPart + hard * hardPart : softPart;

                channels[c][i] = tone[c].process (dc[c].process (shaped - offset)) * makeUp * level;
            }
        }
    }

protected:
    void onReset() override
    {
        const auto bias = (double) asymmetry.map (target (3));

        for (int c = 0; c < 2; ++c)
        {
            tone[c].reset();
            band[c].reset();
            dc[c].reset();
            soft[c].reset (bias);
            clip[c].reset (bias);
            lift[c].reset (Dsp::softClip ((float) bias));
            liftHard[c].reset (juce::jlimit (-1.0f, 1.0f, (float) bias));
        }
    }

private:
    Dsp::OnePole tone[2];
    Dsp::BandLimit band[2];
    Dsp::DcBlocker dc[2];
    Dsp::Adaa<Dsp::SoftClipShape> soft[2];
    Dsp::Adaa<Dsp::HardClipShape> clip[2];
    Dsp::AdaaCompensation lift[2], liftHard[2];
};
