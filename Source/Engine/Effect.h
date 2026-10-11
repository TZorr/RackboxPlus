//
//  Effect.h
//  Rackbox Plus
//
//  The base class of every effect.
//
//  An effect is deliberately small: stereo in place, up to eight knobs, and the
//  contract that what comes out is the *whole effected signal* - not a wet
//  part to be added to something else. A chorus therefore mixes its own dry
//  signal in (a chorus without it is a vibrato); a reverb returns only the
//  reverb. Each block then blends that against the untouched input with its
//  Mix knob, and the block never needs to know which kind it is holding.
//
//  The knobs are 0..1 and always the same eight: what they are called and how
//  they are printed comes from EffectCatalog, not from here. That is what lets
//  the host's automation lane keep meaning "the second knob" when the effect
//  behind it is swapped. An effect reads only the knobs it has; Rackbox's
//  first two keep their old names here (nextA, nextB), so an effect written
//  for Rackbox runs unchanged.
//
//  Nothing here allocates once prepare() has run, and reset() must leave the
//  effect exactly as silent and as cold as a fresh one - the block relies on
//  that when it brings a switched-away effect back later.
//

#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <array>

#include "Dsp.h"

class Effect
{
public:
    static constexpr int numKnobs = 8;

    virtual ~Effect() = default;

    void prepare (double newSampleRate, int newMaxBlock)
    {
        sampleRate = newSampleRate;
        maxBlock = newMaxBlock;
        for (auto& k : knobs)
            k.reset (sampleRate, 0.02);

        onPrepare();
        reset();
    }

    /** Jumps both parameters to their targets without touching the audio state. */
    void snapParameters() noexcept
    {
        for (auto& k : knobs)
            k.setCurrentAndTargetValue (k.getTargetValue());
    }

    /** Silences the effect and snaps both parameters to their targets. */
    void reset()
    {
        lfoFresh = true;
        snapParameters();
        onReset();
    }

    /** All eight knobs. */
    void setKnobs (const std::array<float, numKnobs>& values) noexcept
    {
        for (size_t k = 0; k < knobs.size(); ++k)
            knobs[k].setTargetValue (values[k]);
    }

    /** The first two knobs only, as Rackbox sets them. */
    void setParameters (float newA, float newB) noexcept
    {
        knobs[0].setTargetValue (newA);
        knobs[1].setTargetValue (newB);
    }

    /** Tempo for effects with a time knob that can follow it. Call after setParameters. */
    void setTiming (bool sync, double bpm, bool hasPosition = false, double ppqPosition = 0.0) noexcept
    {
        syncOn = sync;
        tempo = bpm;
        playing = hasPosition;
        ppq = ppqPosition;
    }

    /** The reverbs' pre-delay: a step of Dsp::Sync's list, and the host's bar length in beats. */
    void setPreDelay (int step, double beatsPerBarIn) noexcept
    {
        preStep = step;
        beatsPerBar = beatsPerBarIn;
    }

    /** Replaces left and right with the effected signal. n <= the prepared maximum block. */
    virtual void process (float* left, float* right, int n) noexcept = 0;

protected:
    virtual void onPrepare() {}
    virtual void onReset() = 0;

    /** Knob k (0..7), smoothed, one sample on. */
    float next (int k) noexcept     { return knobs[(size_t) k].getNextValue(); }

    /** For effects that update once per block: knob k after n samples. */
    float block (int k, int n) noexcept   { return knobs[(size_t) k].skip (n); }

    float target (int k) const noexcept   { return knobs[(size_t) k].getTargetValue(); }

    /** Knob k where its glide is now, without moving it on. */
    float current (int k) const noexcept  { return knobs[(size_t) k].getCurrentValue(); }

    float nextA() noexcept  { return next (0); }
    float nextB() noexcept  { return next (1); }
    float blockA (int n) noexcept   { return block (0, n); }
    float blockB (int n) noexcept   { return block (1, n); }
    float targetA() const noexcept  { return target (0); }
    float targetB() const noexcept  { return target (1); }

    /** A time knob as a delay in samples: free (10 ms - 1 s) or a note value at the host tempo. In
        sync mode the knob is read from its *target*, not the smoothed value, so a step from 1/8 to
        1/4 is one jump for the delay's own glide to smooth, not a sweep through the divisions
        in between. Never longer than maxSeconds (the buffer). */
    float delaySamples (float smoothedA, float maxSeconds) const noexcept
    {
        const auto seconds = syncOn ? Dsp::Sync::seconds (target (0), tempo)
                                    : Dsp::Map::timeMs (smoothedA) * 0.001f;
        return juce::jmin (seconds, maxSeconds) * (float) sampleRate;
    }

    /** An LFO rate in Hz: free (0.05 - 10 Hz), or - with Sync on - one cycle per note value at the
        host tempo. Read from the knob's target for the same reason as delaySamples(). */
    float lfoRateHz (float smoothedA) const noexcept
    {
        if (! syncOn)
            return Dsp::Map::rateHz (smoothedA);

        const auto beats = Dsp::Sync::beats[Dsp::Sync::index (target (0))];
        return juce::jmin (30.0f, (float) (juce::jlimit (20.0, 999.0, tempo) / 60.0) / beats);
    }

    /** True when the LFOs should sit on the host's beat grid, not merely run at its tempo: Sync is
        on and the host is playing and told us where. */
    bool phaseLocked() const noexcept   { return syncOn && playing; }

    /** Where in its cycle an LFO of the synced length is at the start of this block (0..1). */
    float syncPhase() const noexcept
    {
        const auto beats = (double) Dsp::Sync::beats[Dsp::Sync::index (target (0))];
        return (float) std::fmod (ppq / beats, 1.0);
    }

    /** How far to pull the LFOs towards syncPhase() in a block of n samples. The first block after
        a reset snaps; after that the pull is a 50 ms slew, so when the tempo or the position jumps
        the LFO glides to the new phase instead of clicking there. With nothing to correct it is a
        no-op, which is the normal case: the rate is exact, so the phase stays where the grid says. */
    float lockAmount (int n) noexcept
    {
        const auto amount = lfoFresh ? 1.0f : juce::jmin (1.0f, (float) n / (0.05f * (float) sampleRate));
        lfoFresh = false;
        return amount;
    }

    /** The pre-delay in samples at the current tempo. */
    float preDelaySamples() const noexcept
    {
        return Dsp::Sync::preDelaySeconds (preStep, tempo, beatsPerBar) * (float) sampleRate;
    }

    double sampleRate = 48000.0;
    int maxBlock = 512;
    int preStep = 0;
    double beatsPerBar = 4.0;
    bool syncOn = false, playing = false, lfoFresh = true;
    double tempo = 120.0, ppq = 0.0;

private:
    std::array<juce::SmoothedValue<float>, numKnobs> knobs { [] {
        std::array<juce::SmoothedValue<float>, numKnobs> k;
        for (auto& v : k) v.setCurrentAndTargetValue (0.5f);
        return k; }() };
};
