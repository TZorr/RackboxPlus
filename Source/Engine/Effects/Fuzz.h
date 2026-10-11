//
//  Fuzz.h
//  Rackbox Plus
//
//  A hard, lopsided clipper. Fuzz is the gain into it (20 to 50 dB), Tone a
//  low-pass after it.
//
//  Two things separate it from the Overdrive. The signal is biased before the
//  shaper, so the positive and negative halves clip at different points: that
//  asymmetry adds even harmonics, the octave-up growl. And the shaper is a
//  soft clip followed by a hard one, so the top of every wave is flat.
//
//  The bias puts DC on the output, which a DC blocker removes; without it a
//  loud note would push the following blocks off centre. The output is held
//  down (x0.35) because at these gains every signal is at full scale.
//
//  At 20 to 50 dB of gain nearly every wave is clipped flat, and a flat top
//  has harmonics far past Nyquist. The whole curve - bias, soft clip, hard
//  clip - is therefore one shape for Dsp::Adaa, which needs its integral:
//  between the two corners of the hard clip that is softClip's integral less
//  the bias offset times the input, outside them a straight line at the clip
//  level. The bias lives inside the shape, so silence is the shape's zero and
//  a reset shaper starts from rest instead of from a step to the bias. The top
//  end the averaging softens is lifted back (Dsp::AdaaCompensation).
//
//  The knobs, in order:
//    Fuzz       the gain into it, 20 to 50 dB.
//    Tone       a low-pass after it, 800 Hz to 14 kHz.
//    Bias       how far off centre the signal sits in the shaper, 0 to 0.6
//               (Rackbox: 0.3, 50 % on the knob): 0 is a symmetric, buzzy square;
//               more is lopsided, octave-up growl; at the top the upper half
//               stops reaching the hard clip at all - a gated, sputtering fuzz.
//    Gate       a noise gate before the fuzz, -80 to -20 dB: below it the input
//               is shut off (1 ms to open, 40 ms to close), so the hiss and hum
//               a 50 dB gain would raise between notes stays out. Off at -80.
//    Low Cut    a high-pass before the fuzz, 20 Hz to 1 kHz; off at the bottom.
//    Level      -12 to +12 dB after everything.
//
//  At its starting values (bias 0.3, no gate, Low Cut off, 0 dB) it is
//  Rackbox's fuzz.
//

#pragma once

#include "../Effect.h"
#include "../Dsp.h"

/** The Fuzz curve at Rackbox's fixed bias, as an Adaa shape:
    in -> clip (1.5 (softClip (in + bias) - softClip (bias)), +-0.8). */
struct FuzzShape
{
    static constexpr double bias = 0.3, gain = 1.5, limit = 0.8;
    static constexpr double offset = Dsp::softClipCurve (bias);

    // The hard clip's corners, as inputs to softClip (bias included).
    static constexpr double cornerLow  = Dsp::softClipInverse (offset - limit / gain);
    static constexpr double cornerHigh = Dsp::softClipInverse (offset + limit / gain);

    static double value (double in) noexcept
    {
        const auto u = juce::jlimit (-3.0, 3.0, in + bias);
        return juce::jlimit (-limit, limit, gain * (Dsp::softClipCurve (u) - offset));
    }

    static double integral (double in) noexcept
    {
        const auto u = in + bias;

        if (u > cornerHigh) return between (cornerHigh) + limit * (u - cornerHigh);
        if (u < cornerLow)  return between (cornerLow) - limit * (u - cornerLow);
        return between (u);
    }

private:
    static double between (double u) noexcept   { return gain * (Dsp::softClipIntegral (u) - offset * u); }
};

/** The same curve with the bias as a setting. Past about 0.5 the soft curve's top (1) no longer
    reaches the hard clip, so the top flattens at the soft curve's own ceiling instead; the
    corners and the levels beyond them are worked out for whichever happens. */
struct VariableFuzzShape
{
    static constexpr double gain = 1.5, limit = 0.8;

    void setBias (double newBias) noexcept
    {
        bias = newBias;
        offset = Dsp::softClipCurve (bias);
        top = juce::jmin (limit, gain * (1.0 - offset));
        bottom = juce::jmax (-limit, gain * (-1.0 - offset));
        cornerHigh = top < limit ? 3.0 : Dsp::softClipInverse (offset + limit / gain);
        cornerLow = bottom > -limit ? -3.0 : Dsp::softClipInverse (offset - limit / gain);
    }

    double value (double in) const noexcept
    {
        const auto u = juce::jlimit (-3.0, 3.0, in + bias);
        return juce::jlimit (bottom, top, gain * (Dsp::softClipCurve (u) - offset));
    }

    double integral (double in) const noexcept
    {
        const auto u = in + bias;

        if (u > cornerHigh) return between (cornerHigh) + top * (u - cornerHigh);
        if (u < cornerLow)  return between (cornerLow) + bottom * (u - cornerLow);
        return between (u);
    }

    double bias = 0.3, offset = Dsp::softClipCurve (0.3), top = 0.8, bottom = -0.8;
    double cornerLow = FuzzShape::cornerLow, cornerHigh = FuzzShape::cornerHigh;

private:
    double between (double u) const noexcept   { return gain * (Dsp::softClipIntegral (u) - offset * u); }
};

class Fuzz : public Effect
{
public:
    static constexpr Dsp::Range bias     { 0.0f, 0.6f };
    static constexpr Dsp::Range gateDb   { -80.0f, -20.0f };
    static constexpr Dsp::Range lowCutHz { 20.0f, 1000.0f, Dsp::Range::Curve::exponential };
    static constexpr Dsp::Range levelDb  { -12.0f, 12.0f };

    void process (float* left, float* right, int n) noexcept override
    {
        float* channels[2] { left, right };

        for (int i = 0; i < n; ++i)
        {
            const auto gain = std::pow (10.0f, (20.0f + 30.0f * nextA()) / 20.0f);
            const auto cutoff = Dsp::Map::exponential (nextB(), 800.0f, 14000.0f);
            const auto offCentre = (double) bias.map (next (2));
            const auto gate = gateDb.map (next (3));
            const auto lowCut = lowCutHz.map (next (4));
            const auto level = std::pow (10.0f, levelDb.map (next (5)) / 20.0f);

            if (std::abs (offCentre - shaper[0].shape.bias) > 1.0e-5)
                for (auto& s : shaper)
                {
                    s.shape.setBias (offCentre);
                    s.refresh();
                }

            // The gate listens to both channels, so it opens and closes them together.
            auto open = 1.0f;
            if (gate > gateDb.low + 0.01f)
            {
                const auto loudest = juce::jmax (std::abs (channels[0][i]), std::abs (channels[1][i]));
                envelope = juce::jmax (loudest, envelope * envelopeFall);
                const auto wanted = envelope > std::pow (10.0f, gate / 20.0f) ? 1.0f : 0.0f;
                gateGain += (wanted > gateGain ? gateOpen : gateClose) * (wanted - gateGain);
                open = gateGain;
            }

            for (int c = 0; c < 2; ++c)
            {
                tone[c].setCutoff (cutoff, sampleRate);

                const auto x = band[c].process (channels[c][i], lowCut, 20000.0f, sampleRate) * open;
                const auto y = lift[c].process (shaper[c].process (gain * x));
                channels[c][i] = tone[c].process (dc[c].process (y)) * 0.35f * level;
            }
        }
    }

protected:
    void onPrepare() override
    {
        envelopeFall = std::exp (-1.0f / (0.03f * (float) sampleRate));
        gateOpen = 1.0f - std::exp (-1.0f / (0.001f * (float) sampleRate));
        gateClose = 1.0f - std::exp (-1.0f / (0.04f * (float) sampleRate));
    }

    void onReset() override
    {
        for (int c = 0; c < 2; ++c)
        {
            tone[c].reset();
            band[c].reset();
            dc[c].reset();
            shaper[c].shape.setBias (bias.map (target (2)));
            shaper[c].reset();
            lift[c].reset();
        }

        envelope = 0.0f;
        gateGain = 0.0f;
    }

private:
    Dsp::OnePole tone[2];
    Dsp::BandLimit band[2];
    Dsp::DcBlocker dc[2];
    Dsp::Adaa<VariableFuzzShape> shaper[2];
    Dsp::AdaaCompensation lift[2];
    float envelope = 0.0f, envelopeFall = 0.999f, gateGain = 0.0f, gateOpen = 0.02f, gateClose = 0.0005f;
};
