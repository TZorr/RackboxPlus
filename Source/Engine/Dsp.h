//
//  Dsp.h
//  Rackbox Plus
//
//  The small pieces every effect is built from, and the knob-to-value maps.
//
//  The maps live here rather than in the effects because two places must agree
//  on them: the effect turns a knob position into a rate or a time, and the
//  panel prints the same rate or time under the knob. Written twice, they
//  drift; written once, the text under the knob is the number the DSP uses.
//

#pragma once

#include <algorithm>
#include <cmath>
#include <vector>

#include <juce_core/juce_core.h>

namespace Dsp
{
    constexpr float pi    = 3.14159265358979f;
    constexpr float twoPi = 6.28318530717959f;

    /** A Pade tanh, saturating at +-1 (input clamped to +-3). */
    inline float softClip (float x) noexcept
    {
        x = x < -3.0f ? -3.0f : (x > 3.0f ? 3.0f : x);
        const auto x2 = x * x;
        return x * (27.0f + x2) / (27.0f + 9.0f * x2);
    }

    /** softClip's curve in double and without the clamp - the same as softClip inside +-3. */
    constexpr double softClipCurve (double x) noexcept
    {
        return x * (27.0 + x * x) / (27.0 + 9.0 * x * x);
    }

    /** Where softClip reaches y (|y| < 1). Its slope is 9 (x^2 - 9)^2 / (27 + 9x^2)^2, never
        negative, so inside +-3 the curve is monotonic and halving the interval finds it. */
    constexpr double softClipInverse (double y) noexcept
    {
        double low = -3.0, high = 3.0;

        for (int i = 0; i < 64; ++i)
        {
            const auto middle = 0.5 * (low + high);
            (softClipCurve (middle) < y ? low : high) = middle;
        }

        return 0.5 * (low + high);
    }

    /** The antiderivative of softClip, for Adaa below. Inside +-3 the curve splits into
        x/9 + (8/3) x/(x^2 + 3), which integrates to x^2/18 + (4/3) ln(x^2 + 3); beyond it the
        clip is a constant +-1, so the integral carries on as a straight line from F(3). */
    inline double softClipIntegral (double x) noexcept
    {
        const auto a = std::abs (x);

        if (a >= 3.0)
            return 0.5 + (4.0 / 3.0) * std::log (12.0) + (a - 3.0);

        return a * a / 18.0 + (4.0 / 3.0) * std::log (a * a + 3.0);
    }

    //==============================================================================
    /** A waveshaper with first-order antiderivative anti-aliasing (Parker, Zavalishin and
        Le Bivic, DAFx 2016).

        A shaper run sample by sample makes harmonics far above Nyquist, and they fold back as
        tones that belong to no note. Instead of the curve's value at each sample, this returns
        its average over the straight line from the previous input to this one:
        (F(x1) - F(x0)) / (x1 - x0), with F the curve's integral. That average is a gentle
        low-pass on what the curve adds, so the folded-back parts come out far quieter, at the
        price of half a sample of delay and a little top end - and with no oversampling, so no
        latency and no filters to run.

        When two inputs are nearly equal the quotient is 0/0, and the curve at their midpoint is
        the same average to second order. F is evaluated in double: at high drive it is large,
        and the difference of two large floats would be mostly rounding.

        Shape needs `double value (double)` and `double integral (double)`, static or not: a
        shape with settings of its own (a bias) is held in `shape` and set through it.

        reset() takes the input the shaper rests at: 0, or a bias. Starting from anywhere else,
        the first sample would average the curve over the jump to the resting input - a click. */
    template <typename Shape>
    class Adaa
    {
    public:
        Shape shape {};

        float process (float in) noexcept
        {
            const auto x = (double) in;
            const auto integral = shape.integral (x);
            const auto dx = x - previous;

            const auto y = std::abs (dx) > 1.0e-6 ? (integral - previousIntegral) / dx
                                                  : shape.value (0.5 * (x + previous));
            previous = x;
            previousIntegral = integral;
            return (float) y;
        }

        void reset (double restingInput = 0.0) noexcept
        {
            previous = restingInput;
            previousIntegral = shape.integral (restingInput);
        }

        /** After `shape` has changed its settings: the last integral again, under the new curve, so
            the next average is not taken across two different curves. */
        void refresh() noexcept   { previousIntegral = shape.integral (previous); }

    private:
        double previous = 0.0, previousIntegral = 0.0;
    };

    /** A hard clip at +-1 as an Adaa shape: the line, then flat. Its integral is x^2/2 inside,
        and carries on as a straight line of slope 1 beyond (meeting it at +-1/2). */
    struct HardClipShape
    {
        static double value (double x) noexcept     { return juce::jlimit (-1.0, 1.0, x); }
        static double integral (double x) noexcept  { const auto a = std::abs (x); return a <= 1.0 ? 0.5 * x * x : a - 0.5; }
    };

    /** Gives back the top end Adaa takes. On the straight part of a curve, Adaa's average of two
        samples is the filter (1 + z^-1) / 2, which is cos (w/2) in level: -3 dB at a quarter of
        the sample rate, nothing left at Nyquist. This one-zero shelf, (1 + k) - k z^-1, is flat at
        DC and +3 dB at fs/4 with k = (sqrt 3 - 1) / 2, which puts the pair within half a dB of
        flat up to there. Above fs/4 it lifts less than Adaa cut, so the folded-back harmonics
        that Adaa pushed down near Nyquist mostly stay down. */
    struct AdaaCompensation
    {
        float process (float x) noexcept
        {
            const auto y = (1.0f + k) * x - k * previous;
            previous = x;
            return y;
        }

        /** Starts at the value it will be fed at rest (the shaper's output for its resting input). */
        void reset (float resting = 0.0f) noexcept   { previous = resting; }

        static constexpr float k = 0.3660254f;
        float previous = 0.0f;
    };

    /** softClip as an Adaa shape. */
    struct SoftClipShape
    {
        static double value (double x) noexcept     { return (double) softClip ((float) x); }
        static double integral (double x) noexcept  { return softClipIntegral (x); }
    };

    //==============================================================================
    /** Where a knob's 0..1 lands: from low to high, evenly or in equal ratios (exponential,
        for frequencies and times, which the ear hears as ratios), and optionally in whole
        steps. An effect maps its knob with it, and the panel prints the same number, so the
        text under a knob is always the value the DSP uses. */
    struct Range
    {
        enum class Curve { linear, exponential };

        float low = 0.0f, high = 1.0f;
        Curve curve = Curve::linear;
        float step = 0.0f;   // 0: continuous; otherwise the value is rounded to multiples of it

        float map (float x) const noexcept
        {
            x = juce::jlimit (0.0f, 1.0f, x);
            auto v = curve == Curve::exponential ? low * std::pow (high / low, x) : low + (high - low) * x;
            return step > 0.0f ? step * std::round (v / step) : v;
        }

        /** The knob position for a value: the inverse of map(), for defaults written as values. */
        float position (float v) const noexcept
        {
            const auto x = curve == Curve::exponential ? std::log (v / low) / std::log (high / low)
                                                        : (v - low) / (high - low);
            return juce::jlimit (0.0f, 1.0f, x);
        }
    };

    //==============================================================================
    //  Knob 0..1 -> value
    namespace Map
    {
        inline float exponential (float x, float low, float high) noexcept
        {
            return low * std::pow (high / low, juce::jlimit (0.0f, 1.0f, x));
        }

        inline float rateHz (float x) noexcept   { return exponential (x, 0.05f, 10.0f); }
        inline float timeMs (float x) noexcept   { return exponential (x, 10.0f, 1000.0f); }
        inline float bits (float x) noexcept     { return 16.0f - 14.0f * juce::jlimit (0.0f, 1.0f, x); }
        inline float downsample (float x) noexcept { return exponential (x, 1.0f, 32.0f); }

        /** A frequency shift, -1000 to +1000 Hz, exactly 0 at the knob's centre. Cubic, so the
            slow beating shifts of a few Hz get as much of the knob as the metallic hundreds:
            a quarter of the way from the centre is 15.6 Hz, half way 125 Hz. */
        inline float shiftHz (float x) noexcept
        {
            const auto u = 2.0f * juce::jlimit (0.0f, 1.0f, x) - 1.0f;
            return 1000.0f * u * u * u;
        }
    }

    //==============================================================================
    //  Note values for a tempo-synced delay, shortest to longest. The knob's
    //  0..1 is cut into fourteen equal steps; each step is a length in beats
    //  (a quarter note is one beat): straight, triplet (x 2/3) and dotted (x 3/2).
    namespace Sync
    {
        constexpr int numDivisions = 14;

        inline constexpr float beats[numDivisions] {
            0.125f, 1.0f / 6.0f, 0.25f, 1.0f / 3.0f, 0.375f, 0.5f, 2.0f / 3.0f,
            0.75f, 1.0f, 4.0f / 3.0f, 1.5f, 2.0f, 3.0f, 4.0f };

        inline constexpr const char* names[numDivisions] {
            "1/32", "1/16 T", "1/16", "1/8 T", "1/16 D", "1/8", "1/4 T",
            "1/8 D", "1/4", "1/2 T", "1/4 D", "1/2", "1/2 D", "1/1" };

        inline int index (float x) noexcept
        {
            return juce::jlimit (0, numDivisions - 1, juce::roundToInt (x * (float) (numDivisions - 1)));
        }

        //  Pre-delay steps for the reverbs: off, then note values from 1/128 up to one bar. These are
        //  note lengths in beats (a quarter note is one beat), so 1/128 is 1/32 of a beat; the last
        //  step is a whole bar, whose length in beats comes from the host's time signature.
        constexpr int numPreDelays = 9;
        constexpr int preDelayBar = numPreDelays - 1;
        constexpr float maxPreDelaySeconds = 4.0f;   // one bar of 4/4 at 60 BPM

        inline constexpr float preDelayBeats[numPreDelays] {
            0.0f, 1.0f / 32.0f, 1.0f / 16.0f, 1.0f / 8.0f, 0.25f, 0.5f, 1.0f, 2.0f, 0.0f };

        inline constexpr const char* preDelayNames[numPreDelays] {
            "OFF", "1/128", "1/64", "1/32", "1/16", "1/8", "1/4", "1/2", "1 BAR" };

        /** A pre-delay step as seconds at a tempo (and bar length in beats), never beyond the buffer. */
        inline float preDelaySeconds (int step, double bpm, double beatsPerBar) noexcept
        {
            step = juce::jlimit (0, numPreDelays - 1, step);
            const auto length = step == preDelayBar ? juce::jlimit (1.0, 16.0, beatsPerBar) : (double) preDelayBeats[step];
            return juce::jmin (maxPreDelaySeconds, (float) (length * 60.0 / juce::jlimit (20.0, 999.0, bpm)));
        }

        /** The delay in seconds for a knob position at a tempo. */
        inline float seconds (float x, double bpm) noexcept
        {
            return beats[index (x)] * 60.0f / (float) juce::jlimit (20.0, 999.0, bpm);
        }
    }

    //==============================================================================
    /** A circular buffer read with linear interpolation. */
    class DelayLine
    {
    public:
        void allocate (int maxSamples)
        {
            int size = 4;
            while (size < maxSamples + 4)
                size <<= 1;

            buffer.assign ((size_t) size, 0.0f);
            mask = size - 1;
            pos = 0;
        }

        void clear() noexcept   { std::fill (buffer.begin(), buffer.end(), 0.0f); }

        void push (float x) noexcept    { pos = (pos + 1) & mask; buffer[(size_t) pos] = x; }

        /** The sample pushed `delay` samples ago; 0 is the latest. */
        float read (float delay) const noexcept
        {
            const auto whole = (int) delay;
            const auto fraction = delay - (float) whole;
            const auto a = buffer[(size_t) ((pos - whole) & mask)];
            const auto b = buffer[(size_t) ((pos - whole - 1) & mask)];
            return a + fraction * (b - a);
        }

    private:
        std::vector<float> buffer;
        int mask = 0, pos = 0;
    };

    /** A delay whose time can be changed at any moment without a click.
        A delay line read at a moving position is a pitch shift, and a jump from 0 to half a second
        would be a zip lasting as long as the move; so a change is a crossfade between two fixed
        reads (30 ms, linear, which is right for two copies of the same signal), not a glide.
        A change that arrives during a fade waits for it to finish. */
    class PreDelay
    {
    public:
        void prepare (double sampleRate, float maxSeconds)
        {
            line.allocate ((int) (maxSeconds * sampleRate) + 4);
            maxTime = maxSeconds * (float) sampleRate;
            fadeStep = 1.0f / (0.03f * (float) sampleRate);
            reset();
        }

        /** Empties the line; the next sample starts at whatever time it is given. */
        void reset() noexcept
        {
            line.clear();
            fresh = true;
            fading = false;
        }

        float process (float x, float targetSamples) noexcept
        {
            targetSamples = juce::jlimit (0.0f, maxTime, targetSamples);

            if (fresh)
            {
                time = targetSamples;
                fresh = false;
            }
            else if (! fading && std::abs (targetSamples - time) > 0.5f)
            {
                from = time;
                time = targetSamples;
                amount = 0.0f;
                fading = true;
            }

            line.push (x);

            if (! fading)
                return line.read (time);

            amount = juce::jmin (1.0f, amount + fadeStep);
            const auto y = line.read (from) + amount * (line.read (time) - line.read (from));

            if (amount >= 1.0f)
                fading = false;

            return y;
        }

    private:
        DelayLine line;
        float time = 0.0f, from = 0.0f, amount = 0.0f, fadeStep = 0.001f, maxTime = 0.0f;
        bool fresh = true, fading = false;
    };

    /** An LFO waveform at a phase (0..1): shape 0 is a sine, 0.5 a triangle, 1 a square with
        its edges rounded off (a trapezoid steep enough to read as square, gentle enough not to
        click); in between, the two neighbours are blended. All three cross zero rising at
        phase 0 and peak at 0.25, so changing the shape never shifts the timing. */
    inline float waveform (float phase, float shape) noexcept
    {
        const auto sine = std::sin (twoPi * phase);
        auto t = phase + 0.25f;
        t -= std::floor (t);
        const auto triangle = 1.0f - 4.0f * std::abs (t - 0.5f);

        if (shape <= 0.5f)
            return sine + 2.0f * shape * (triangle - sine);

        const auto square = juce::jlimit (-1.0f, 1.0f, 4.0f * triangle);
        return triangle + 2.0f * (shape - 0.5f) * (square - triangle);
    }

    /** Bends a phase so the first half of the cycle takes a fraction `skew` of it (0.5: no
        bend). A tremolo with skew below 0.5 rises fast and falls slowly, above it the opposite. */
    inline float skewPhase (float phase, float skew) noexcept
    {
        const auto b = juce::jlimit (0.05f, 0.95f, skew);
        return phase < b ? 0.5f * phase / b : 0.5f + 0.5f * (phase - b) / (1.0f - b);
    }

    /** A sine LFO that can be started at any phase. */
    struct Lfo
    {
        void setRate (float hz, double sampleRate) noexcept  { increment = hz / (float) sampleRate; }
        void setPhase (float p) noexcept                     { phase = p - std::floor (p); }

        /** Moves the phase a fraction of the way (the short way round) towards a target. 1 snaps; a
            smaller amount is spread evenly over the next n samples, so a synced LFO that has to catch
            up with a jumping transport slides there instead of stepping at a block boundary. */
        void pull (float target, float amount, int n) noexcept
        {
            auto error = target - phase;
            error -= std::floor (error + 0.5f);

            if (amount >= 1.0f)
            {
                setPhase (phase + error);
                slewLeft = 0;
            }
            else
            {
                slew = amount * error / (float) juce::jmax (1, n);
                slewLeft = n;
            }
        }

        /** -1..1 */
        float next() noexcept
        {
            const auto v = std::sin (twoPi * phase);
            advance();
            return v;
        }

        /** -1..1, in a shape (see waveform) and with a skew (see skewPhase). */
        float next (float shape, float skew = 0.5f) noexcept
        {
            const auto v = waveform (skewPhase (phase, skew), shape);
            advance();
            return v;
        }

        /** The value at the current phase plus an offset (0..1), without moving on: for a second
            channel or voice that follows the same LFO a fixed part of a cycle apart. */
        float at (float offset, float shape = 0.0f, float skew = 0.5f) const noexcept
        {
            auto p = phase + offset;
            p -= std::floor (p);
            return waveform (skewPhase (p, skew), shape);
        }

        void advance() noexcept
        {
            phase += increment;

            if (slewLeft > 0)
            {
                phase += slew;
                --slewLeft;
            }

            phase -= std::floor (phase);
        }

        float phase = 0.0f, increment = 0.0f, slew = 0.0f;
        int slewLeft = 0;
    };

    /** One-pole low-pass. */
    struct OnePole
    {
        void setCutoff (float hz, double sampleRate) noexcept
        {
            coefficient = 1.0f - std::exp (-twoPi * juce::jmin (hz, (float) (0.45 * sampleRate)) / (float) sampleRate);
        }

        float process (float x) noexcept    { state += coefficient * (x - state); return state; }
        void reset() noexcept               { state = 0.0f; }

        float coefficient = 0.5f, state = 0.0f;
    };

    /** Two copies of a signal, 90 degrees apart at every frequency: the in-phase and the
        quadrature part. With both, a signal's frequencies can be moved by a fixed number of Hz
        (see FreqShifter), which no single filter can do.

        Each part is a chain of seven allpass sections (c + z^-2) / (1 + c z^-2); the quadrature
        chain has one more sample of delay. Allpasses change only phase, never level, so both
        parts keep the input's spectrum, and the coefficients are chosen so that the difference
        between the two chains' phases stays at 90 degrees. They were designed for Rackbox by
        Scripts/HilbertDesign.cpp (a minimax fit of that phase difference): from 10 Hz to 10 Hz
        below Nyquist at 48 kHz it is never off by more than 0.053 degrees, which leaves the
        sideband a shifter must cancel at least 66 dB down. The band scales with the sample
        rate (at 96 kHz it starts at 20 Hz). No latency: an FIR Hilbert filter would be exact,
        but only by delaying the signal. */
    class QuadraturePair
    {
    public:
        static constexpr int sections = 7;

        void reset() noexcept
        {
            for (auto* chain : { inPhaseState, quadratureState })
                for (int i = 0; i < sections; ++i)
                    chain[i] = {};

            delayed = 0.0f;
        }

        /** One sample in, both parts out; the quadrature part trails the in-phase part by 90 degrees. */
        void process (float x, float& inPhase, float& quadrature) noexcept
        {
            inPhase = run (x, inPhaseCoefficients, inPhaseState);
            quadrature = run (delayed, quadratureCoefficients, quadratureState);
            delayed = x;
        }

    private:
        struct Section { float x1 = 0.0f, x2 = 0.0f, y1 = 0.0f, y2 = 0.0f; };

        static float run (float x, const float* c, Section* s) noexcept
        {
            for (int i = 0; i < sections; ++i)
            {
                const auto y = c[i] * (x - s[i].y2) + s[i].x2;
                s[i].x2 = s[i].x1;
                s[i].x1 = x;
                s[i].y2 = s[i].y1;
                s[i].y1 = y;
                x = y;
            }

            return x;
        }

        static constexpr float inPhaseCoefficients[sections] {
            -0.997866960154f, -0.992532717939f, -0.977619922875f, -0.933068746788f,
            -0.804761980877f, -0.496881669092f, -0.080824523823f };

        static constexpr float quadratureCoefficients[sections] {
            -0.999347000428f, -0.995800665842f, -0.987048582700f, -0.961288544713f,
            -0.884882991514f, -0.678316536059f, -0.276882879487f };

        Section inPhaseState[sections], quadratureState[sections];
        float delayed = 0.0f;
    };

    /** One-pole high-pass: what the low-pass takes away. */
    struct OnePoleHighPass
    {
        void setCutoff (float hz, double sampleRate) noexcept   { low.setCutoff (hz, sampleRate); }
        float process (float x) noexcept                         { return x - low.process (x); }
        void reset() noexcept                                    { low.reset(); }

        OnePole low;
    };

    /** Stereo width by mid and side: 0 folds to mono, 1 leaves the pair alone, above 1 widens. */
    inline void widen (float& left, float& right, float width) noexcept
    {
        if (std::abs (width - 1.0f) < 1.0e-4f)
            return;

        const auto mid = 0.5f * (left + right), side = 0.5f * width * (left - right);
        left = mid + side;
        right = mid - side;
    }

    /** A low cut and a high cut in one, each off at its end stop (20 Hz, 20 kHz) so a knob turned
        all the way out leaves the signal untouched. */
    struct BandLimit
    {
        float process (float x, float lowCutHz, float highCutHz, double sampleRate) noexcept
        {
            if (lowCutHz > 20.01f)
            {
                low.setCutoff (lowCutHz, sampleRate);
                x = low.process (x);
            }

            if (highCutHz < 19999.0f)
            {
                high.setCutoff (highCutHz, sampleRate);
                x = high.process (x);
            }

            return x;
        }

        void reset() noexcept   { low.reset(); high.reset(); }

        OnePoleHighPass low;
        OnePole high;
    };

    /** Removes DC that an asymmetric shaper adds. */
    struct DcBlocker
    {
        float process (float x) noexcept
        {
            const auto y = x - x1 + 0.9975f * y1;
            x1 = x;
            y1 = y;
            return y;
        }

        void reset() noexcept   { x1 = y1 = 0.0f; }

        float x1 = 0.0f, y1 = 0.0f;
    };
}
