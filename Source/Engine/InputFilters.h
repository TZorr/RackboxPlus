//
//  InputFilters.h
//  Rackbox Plus
//
//  The input section: a 12 dB/octave low-pass and high-pass in series, in front of the
//  effect chain. They filter what the effects are fed, not the dry signal (the processor
//  takes the dry signal before them).
//
//  Both are second-order state-variable filters in the topology-preserving
//  form (Zavalishin), Butterworth-tuned (Q = 1/sqrt 2), with the cutoff swept
//  by a smoothed value. That structure is why these are not biquads: their two
//  states are the integrators of the analogue circuit and stay meaningful
//  while the cutoff moves, so dragging the knob over a sustained signal does
//  not click. The cutoffs glide exponentially over 30 ms - frequency is heard
//  in ratios - and the coefficients follow every eight samples.
//
//  Both run all the time. When the knobs sit at their ends (LPF 20 kHz, HPF
//  20 Hz) the block output is the untouched input instead, so an open input
//  section is bit-exact and a plugin with nothing engaged really is a wire -
//  and because the states are always warm, engaging a filter is not a cold
//  start.
//

#pragma once

#include <cmath>

#include <juce_audio_basics/juce_audio_basics.h>

class InputFilters
{
public:
    static constexpr float lowpassOff  = 20000.0f;
    static constexpr float highpassOff = 20.0f;

    void prepare (double newSampleRate)
    {
        sampleRate = newSampleRate;
        lpf.reset (sampleRate, 0.03);
        hpf.reset (sampleRate, 0.03);
        reset (lowpassOff, highpassOff);
    }

    void reset (float lowpassHz, float highpassHz)
    {
        lpf.setCurrentAndTargetValue (lowpassHz);
        hpf.setCurrentAndTargetValue (highpassHz);

        for (auto& s : lowStage)  s = {};
        for (auto& s : highStage) s = {};

        countdown = 0;
        update();
    }

    void process (float* left, float* right, int n, float lowpassHz, float highpassHz) noexcept
    {
        lpf.setTargetValue (lowpassHz);
        hpf.setTargetValue (highpassHz);

        const auto engaged = lowpassHz < lowpassOff - 0.5f || highpassHz > highpassOff + 0.005f
                             || lpf.isSmoothing() || hpf.isSmoothing();
        float* channels[2] { left, right };

        for (int i = 0; i < n; ++i)
        {
            lpf.getNextValue();
            hpf.getNextValue();

            if (--countdown < 0)
            {
                countdown = 7;
                update();
            }

            for (int c = 0; c < 2; ++c)
            {
                const auto x = channels[c][i];
                const auto filtered = highStage[c].process (lowStage[c].process (x, false), true);

                if (engaged)
                    channels[c][i] = filtered;
            }
        }
    }

private:
    struct Stage
    {
        float a1 = 0, a2 = 0, a3 = 0, k = 1.4142135f, ic1 = 0, ic2 = 0;

        void set (float g) noexcept
        {
            a1 = 1.0f / (1.0f + g * (g + k));
            a2 = g * a1;
            a3 = g * a2;
        }

        float process (float v0, bool highpass) noexcept
        {
            const auto v3 = v0 - ic2;
            const auto v1 = a1 * ic1 + a2 * v3;
            const auto v2 = ic2 + a2 * ic1 + a3 * v3;
            ic1 = 2.0f * v1 - ic1;
            ic2 = 2.0f * v2 - ic2;
            return highpass ? v0 - k * v1 - v2 : v2;
        }
    };

    void update() noexcept
    {
        const auto limit = (float) (0.49 * sampleRate);
        const auto gLow  = std::tan (3.14159265f * juce::jmin (lpf.getCurrentValue(), limit) / (float) sampleRate);
        const auto gHigh = std::tan (3.14159265f * juce::jmin (hpf.getCurrentValue(), limit) / (float) sampleRate);

        for (auto& s : lowStage)  s.set (gLow);
        for (auto& s : highStage) s.set (gHigh);
    }

    double sampleRate = 48000.0;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> lpf { lowpassOff }, hpf { highpassOff };
    Stage lowStage[2], highStage[2];
    int countdown = 0;
};
