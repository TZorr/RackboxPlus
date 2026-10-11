//
//  PlateVerb.h
//  Rackbox Plus
//
//  The Plate: Dattorro's plate (PlateReverb) behind the Effect interface.
//  PlateReverb takes one mono input and adds a stereo return into the buffers
//  it is given, so the wrapper sums the input to mono first, clears the
//  buffers, and lets the reverb fill them.
//
//  The Dattorro tank gets much louder as its decay lengthens - measured on noise,
//  7.5 dB from Decay 0 to Decay 1 - so the return is scaled by a curve fitted to
//  those measurements, which leaves the level within about 2 dB of -4 dB re input
//  over the whole knob. Without it, turning Decay up is also turning the reverb
//  up, and the two want to be separate knobs.
//
//  The knobs, in order:
//    Decay      the tank's gain per round trip, short to very long.
//    Damp       the high-frequency loss in the tank.
//    Pre-Delay  0 to 250 ms before the plate, on top of the PRE button's note value.
//    Bandwidth  the input's band limit, 1 to 20 kHz: lower is a darker plate that
//               never hisses on hi-hats.
//    Diffusion  how much the input is smeared before the tank, 0 to 90 %: low is
//               grainy, separate echoes at the start; high a smooth wash at once.
//    Mod        how far the tank's two allpasses wander, 0 to 200 %: none rings
//               metallically on long tails, much is a chorused, lush plate.
//    Low Cut    a high-pass on the return, 20 Hz to 1 kHz; at the bottom it is off.
//    Width      0 mono, 100 % the plate's own image, 150 % wider.
//
//  At its starting values (10 kHz, 75 %, 100 %, open, 100 % wide, no pre-delay)
//  it is Rackbox's plate.
//

#pragma once

#include "../Effect.h"
#include "../PlateReverb.h"

class PlateVerb : public Effect
{
public:
    static constexpr Dsp::Range preDelayMs  { 0.0f, 250.0f };
    static constexpr Dsp::Range bandwidthHz { 1000.0f, 20000.0f, Dsp::Range::Curve::exponential };
    static constexpr Dsp::Range diffusion   { 0.0f, 0.9f };
    static constexpr Dsp::Range mod         { 0.0f, 2.0f };
    static constexpr Dsp::Range lowCutHz    { 20.0f, 1000.0f, Dsp::Range::Curve::exponential };
    static constexpr Dsp::Range width       { 0.0f, 1.5f };

    void process (float* left, float* right, int n) noexcept override
    {
        const auto preTarget = preDelaySamples();

        for (int i = 0; i < n; ++i)
        {
            const auto pre = preDelayMs.map (next (2)) * 0.001f * (float) sampleRate;
            mono[(size_t) i] = preDelay.process (0.5f * (left[i] + right[i]), preTarget + pre);
            left[i] = right[i] = 0.0f;
        }

        const auto decay = blockA (n);
        reverb.setParameters (decay, blockB (n), 0.0f);
        reverb.setBandwidth (bandwidthHz.map (block (3, n)));
        reverb.setDiffusion (diffusion.map (block (4, n)));
        reverb.setModulation (mod.map (block (5, n)));
        reverb.process (mono.data(), left, right, n, makeUp (decay));

        const auto lowCut = lowCutHz.map (block (6, n));
        const auto spread = width.map (block (7, n));

        for (int i = 0; i < n; ++i)
        {
            if (lowCut > lowCutHz.low + 0.01f)
            {
                lowCutFilter[0].setCutoff (lowCut, sampleRate);
                lowCutFilter[1].setCutoff (lowCut, sampleRate);
                left[i] = lowCutFilter[0].process (left[i]);
                right[i] = lowCutFilter[1].process (right[i]);
            }

            Dsp::widen (left[i], right[i], spread);
        }
    }

protected:
    void onPrepare() override
    {
        mono.assign ((size_t) maxBlock, 0.0f);
        reverb.prepare (sampleRate);
        preDelay.prepare (sampleRate, Dsp::Sync::maxPreDelaySeconds + preDelayMs.high * 0.001f);
    }

    void onReset() override
    {
        reverb.reset();
        reverb.setParameters (target (0), target (1), 0.0f);
        reverb.setBandwidth (bandwidthHz.map (target (3)));
        reverb.setDiffusion (diffusion.map (target (4)));
        reverb.setModulation (mod.map (target (5)));
        preDelay.reset();
        lowCutFilter[0].reset();
        lowCutFilter[1].reset();
    }

private:
    /** Gain that flattens the measured level (-2.7, -1.3, +4.8 dB at Decay 0, 0.5, 1) to about -4 dB. */
    static float makeUp (float decay) noexcept
    {
        const auto measured = -2.7f - 1.9f * decay + 9.4f * decay * decay;
        return std::pow (10.0f, (-4.0f - measured) / 20.0f);
    }

    PlateReverb reverb;
    Dsp::PreDelay preDelay;
    Dsp::OnePoleHighPass lowCutFilter[2];
    std::vector<float> mono;
};
