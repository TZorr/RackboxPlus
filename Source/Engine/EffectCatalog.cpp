//
//  EffectCatalog.cpp
//  Rackbox Plus
//

#include "EffectCatalog.h"

#include "Dsp.h"
#include "Effects/Bitcrusher.h"
#include "Effects/Chorus.h"
#include "Effects/DigitalDelay.h"
#include "Effects/Flanger.h"
#include "Effects/FreqShifter.h"
#include "Effects/Fuzz.h"
#include "Effects/HallVerb.h"
#include "Effects/Overdrive.h"
#include "Effects/Phaser.h"
#include "Effects/PingPongDelay.h"
#include "Effects/PlateVerb.h"
#include "Effects/RoomVerb.h"
#include "Effects/TapeDelay.h"
#include "Effects/Tremolo.h"

namespace EffectCatalog
{
    namespace
    {
        using U = Unit;

        struct Entry
        {
            Info info;
            std::unique_ptr<Effect> (*make)();
        };

        template <typename T>
        std::unique_ptr<Effect> make() { return std::make_unique<T>(); }

        /** A number with a fixed count of decimals. Not juce::String (v, 0): there 0 means "as many
            as it takes", and 23.4567 Hz is what a knob then prints. */
        juce::String fixed (float v, int decimals)
        {
            return decimals == 0 ? juce::String (juce::roundToInt (v)) : juce::String (v, decimals);
        }

        /** A knob whose start is written as a value in its Range, not as a position. */
        Knob knob (const char* label, Unit unit, Dsp::Range range, float startValue)
        {
            return { label, unit, range.position (startValue), range };
        }

        /** A knob that starts at a position (0..1). */
        Knob at (const char* label, Unit unit, float position)
        {
            return { label, unit, position, {} };
        }

        struct Category
        {
            const char* name;
            int count;
            Entry entries[maxSubtypes];
        };

        const Category categories[numCategories] =
        {
            { "Modulation", 5, {
                { { "Chorus", { at ("Rate", U::RateHz, 0.5f), at ("Depth", U::Percent, 0.5f),
                                knob ("Delay", U::Ms, Chorus::delayMs, 14.0f), knob ("Voices", U::Count, Chorus::voices, 2.0f),
                                at ("Spread", U::Percent, 0.5f), knob ("Feedback", U::SignedPercent, Chorus::feedback, 0.0f),
                                knob ("Tone", U::Hz, Chorus::toneHz, 20000.0f), knob ("Low Cut", U::Hz, Chorus::lowCutHz, 20.0f) } },
                  make<Chorus> },
                { { "Phaser", { at ("Rate", U::RateHz, 0.5f), at ("Depth", U::Percent, 0.5f),
                                knob ("Center", U::Hz, Phaser::centerHz, 800.0f), knob ("Feedback", U::SignedPercent, Phaser::feedback, 0.5f),
                                knob ("Stages", U::Count, Phaser::stages, 6.0f), at ("Spread", U::Percent, 0.5f),
                                at ("Shape", U::Shape, 0.0f), at ("Blend", U::Percent, 0.5f) } },
                  make<Phaser> },
                { { "Flanger", { at ("Rate", U::RateHz, 0.5f), at ("Depth", U::Percent, 0.5f),
                                 knob ("Delay", U::Ms, Flanger::delayMs, 3.0f), knob ("Feedback", U::SignedPercent, Flanger::feedback, 0.55f),
                                 at ("Spread", U::Percent, 0.5f), at ("Shape", U::Shape, 0.0f),
                                 knob ("Tone", U::Hz, Flanger::toneHz, 20000.0f) } },
                  make<Flanger> },
                { { "Tremolo", { at ("Rate", U::RateHz, 0.5f), at ("Depth", U::Percent, 0.5f),
                                 at ("Shape", U::Shape, 0.0f), at ("Stereo", U::Percent, 0.0f),
                                 knob ("Skew", U::SignedPercent, Tremolo::skew, 0.0f) } },
                  make<Tremolo> },
                { { "Shifter", { at ("Shift", U::ShiftHz, 0.5f), knob ("Fine", U::SignedHz, FreqShifter::fineHz, 0.0f),
                                 at ("Feedback", U::Percent, 0.5f), knob ("Loop", U::Ms, FreqShifter::loopMs, 8.0f),
                                 at ("Spread", U::Percent, 0.0f), knob ("Low Cut", U::Hz, FreqShifter::lowCutHz, 20.0f),
                                 knob ("High Cut", U::Hz, FreqShifter::highCutHz, 20000.0f) } },
                  make<FreqShifter> } } },

            { "Reverb", 3, {
                { { "Plate", { at ("Decay", U::Percent, 0.5f), at ("Damp", U::Percent, 0.5f),
                               knob ("Pre-Delay", U::Ms, PlateVerb::preDelayMs, 0.0f), knob ("Bandwidth", U::Hz, PlateVerb::bandwidthHz, 10000.0f),
                               knob ("Diffusion", U::Amount, PlateVerb::diffusion, 0.75f), knob ("Mod", U::Amount, PlateVerb::mod, 1.0f),
                               knob ("Low Cut", U::Hz, PlateVerb::lowCutHz, 20.0f), knob ("Width", U::Amount, PlateVerb::width, 1.0f) } },
                  make<PlateVerb> },
                { { "Room", { knob ("Size", U::Amount, FdnReverb::size, 1.0f), knob ("Decay", U::Seconds, RoomVerb::decaySeconds, 0.4583f),
                              at ("Damp", U::Percent, 0.5f), knob ("Pre-Delay", U::Ms, FdnReverb::preDelayMs, 0.0f),
                              at ("Early", U::Percent, 0.0f), knob ("Mod", U::Amount, FdnReverb::mod, 1.0f),
                              knob ("Low Cut", U::Hz, FdnReverb::lowCutHz, 20.0f), knob ("Width", U::Amount, FdnReverb::width, 1.0f) } },
                  make<RoomVerb> },
                { { "Hall", { knob ("Size", U::Amount, FdnReverb::size, 1.0f), knob ("Decay", U::Seconds, HallVerb::decaySeconds, 2.366f),
                              at ("Damp", U::Percent, 0.5f), knob ("Pre-Delay", U::Ms, FdnReverb::preDelayMs, 0.0f),
                              at ("Early", U::Percent, 0.0f), knob ("Mod", U::Amount, FdnReverb::mod, 1.0f),
                              knob ("Low Cut", U::Hz, FdnReverb::lowCutHz, 20.0f), knob ("Width", U::Amount, FdnReverb::width, 1.0f) } },
                  make<HallVerb> } } },

            { "Delay", 3, {
                { { "Digital", { at ("Time", U::TimeMs, 0.5f), at ("Feedback", U::Percent, 0.5f),
                                 knob ("Low Cut", U::Hz, DigitalDelay::lowCutHz, 20.0f), knob ("High Cut", U::Hz, DigitalDelay::highCutHz, 20000.0f),
                                 knob ("Offset", U::SignedPercent, DigitalDelay::offset, 0.0f), knob ("Width", U::Amount, DigitalDelay::width, 1.0f),
                                 knob ("Mod Rate", U::Hz, DigitalDelay::modRateHz, 1.0f), knob ("Mod Depth", U::Ms, DigitalDelay::modDepthMs, 0.0f) } },
                  make<DigitalDelay> },
                { { "Tape", { at ("Time", U::TimeMs, 0.5f), at ("Feedback", U::Percent, 0.5f),
                              knob ("Tone", U::Hz, TapeDelay::toneHz, 4500.0f), knob ("Saturation", U::Percent, TapeDelay::drive, 1.4f),
                              knob ("Wow", U::Amount, TapeDelay::wobble, 1.0f), knob ("Flutter", U::Amount, TapeDelay::wobble, 1.0f),
                              knob ("Low Cut", U::Hz, TapeDelay::lowCutHz, 20.0f), knob ("Width", U::Amount, TapeDelay::width, 1.0f) } },
                  make<TapeDelay> },
                { { "Ping-Pong", { at ("Time", U::TimeMs, 0.5f), at ("Feedback", U::Percent, 0.5f),
                                   knob ("Low Cut", U::Hz, PingPongDelay::lowCutHz, 20.0f), knob ("High Cut", U::Hz, PingPongDelay::highCutHz, 20000.0f),
                                   knob ("Width", U::Amount, PingPongDelay::width, 1.0f), knob ("Swing", U::SignedPercent, PingPongDelay::swing, 0.0f),
                                   knob ("Mod Depth", U::Ms, PingPongDelay::modDepthMs, 0.0f) } },
                  make<PingPongDelay> } } },

            { "Distortion", 3, {
                { { "Overdrive", { at ("Drive", U::Percent, 0.5f), at ("Tone", U::Percent, 0.5f),
                                   knob ("Low Cut", U::Hz, Overdrive::lowCutHz, 20.0f), at ("Asymmetry", U::Percent, 0.0f),
                                   at ("Character", U::Percent, 0.0f), knob ("Level", U::Db, Overdrive::levelDb, 0.0f) } },
                  make<Overdrive> },
                { { "Fuzz", { at ("Fuzz", U::Percent, 0.5f), at ("Tone", U::Percent, 0.5f),
                              knob ("Bias", U::Percent, Fuzz::bias, 0.3f), knob ("Gate", U::Db, Fuzz::gateDb, -80.0f),
                              knob ("Low Cut", U::Hz, Fuzz::lowCutHz, 20.0f), knob ("Level", U::Db, Fuzz::levelDb, 0.0f) } },
                  make<Fuzz> },
                { { "Bitcrusher", { at ("Bits", U::Bits, 0.5f), at ("Rate", U::Factor, 0.5f),
                                    at ("Jitter", U::Percent, 0.0f), at ("Pre Filter", U::Percent, 0.0f),
                                    knob ("Tone", U::Hz, Bitcrusher::toneHz, 20000.0f), knob ("Drive", U::Db, Bitcrusher::driveDb, 0.0f),
                                    knob ("Level", U::Db, Bitcrusher::levelDb, 0.0f) } },
                  make<Bitcrusher> } } }
        };

        int firstIndex (int category)
        {
            int first = 0;
            for (int c = 0; c < category; ++c)
                first += categories[c].count;
            return first;
        }
    }

    const char* categoryName (int category)
    {
        return categories[juce::jlimit (0, numCategories - 1, category)].name;
    }

    int numSubtypes (int category)
    {
        return categories[juce::jlimit (0, numCategories - 1, category)].count;
    }

    int clampSubtype (int category, int subtype)
    {
        return juce::jlimit (0, numSubtypes (category) - 1, subtype);
    }

    int index (int category, int subtype)
    {
        category = juce::jlimit (0, numCategories - 1, category);
        return firstIndex (category) + clampSubtype (category, subtype);
    }

    const Info& info (int category, int subtype)
    {
        category = juce::jlimit (0, numCategories - 1, category);
        return categories[category].entries[clampSubtype (category, subtype)].info;
    }

    std::unique_ptr<Effect> create (int effectIndex)
    {
        for (int c = 0; c < numCategories; ++c)
        {
            if (effectIndex < categories[c].count)
            {
                // A new effect starts where its knobs start on the panel, not at 0.5 each.
                const auto& entry = categories[c].entries[effectIndex];
                auto effect = entry.make();
                std::array<float, Effect::numKnobs> start {};

                for (size_t k = 0; k < start.size(); ++k)
                    start[k] = entry.info.knobs[k].defaultValue;

                effect->setKnobs (start);
                effect->snapParameters();
                return effect;
            }

            effectIndex -= categories[c].count;
        }

        jassertfalse;
        return {};
    }

    bool hasPreDelay (int category)
    {
        return category == 1;   // Reverb: the second entry of the table above
    }

    bool canSync (int category, int subtype)
    {
        const auto unit = info (category, subtype).knobs[0].unit;
        return unit == Unit::TimeMs || unit == Unit::RateHz;
    }

    juce::String format (Unit unit, float x)
    {
        switch (unit)
        {
            case Unit::Percent: return juce::String (juce::roundToInt (100.0f * x)) + " %";
            case Unit::RateHz:  return juce::String (Dsp::Map::rateHz (x), 2) + " Hz";
            case Unit::Bits:    return juce::String (Dsp::Map::bits (x), 1) + " bit";
            case Unit::Division: return Dsp::Sync::names[Dsp::Sync::index (x)];
            case Unit::Factor:  return "x" + juce::String (Dsp::Map::downsample (x), 1);
            case Unit::Hz: case Unit::Ms: case Unit::Count: case Unit::SignedPercent:
            case Unit::SignedHz: case Unit::Shape: case Unit::Seconds: case Unit::Amount: case Unit::Db:
                return format (Knob { "", unit, x, {} }, x);   // these need a Range; the plain 0..1 one
            case Unit::ShiftHz:
            {
                const auto hz = Dsp::Map::shiftHz (x);
                const auto size = std::abs (hz);
                const auto digits = size < 9.995f ? 2 : (size < 99.95f ? 1 : 0);
                const auto text = fixed (size, digits) + " Hz";
                return hz > 0.0f ? "+" + text : (hz < 0.0f ? "-" + text : text);
            }
            case Unit::TimeMs:
            {
                const auto ms = Dsp::Map::timeMs (x);
                return ms < 100.0f ? juce::String (ms, 1) + " ms"
                                   : (ms < 1000.0f ? juce::String (juce::roundToInt (ms)) + " ms"
                                                   : juce::String (ms / 1000.0f, 2) + " s");
            }
        }

        return {};
    }

    juce::String format (const Knob& knob, float x)
    {
        const auto v = knob.range.map (x);
        const auto unit = knob.unit;

        if (unit == Unit::Hz)
            return v < 9.995f ? juce::String (v, 2) + " Hz"
                 : v < 99.95f ? fixed (v, std::abs (v - std::round (v)) < 0.05f ? 0 : 1) + " Hz"
                 : v < 1000.0f ? juce::String (juce::roundToInt (v)) + " Hz"
                               : fixed (v / 1000.0f, v < 9995.0f ? 2 : 1) + " kHz";

        if (unit == Unit::Ms)
            return v < 0.005f ? juce::String ("0 ms") : fixed (v, v < 10.0f ? 2 : (v < 100.0f ? 1 : 0)) + " ms";

        if (unit == Unit::Count)
            return juce::String (juce::roundToInt (v));

        if (unit == Unit::SignedPercent)
        {
            const auto p = juce::roundToInt (100.0f * v);
            return (p > 0 ? "+" : "") + juce::String (p) + " %";
        }

        if (unit == Unit::SignedHz)
            return (v > 0.004f ? "+" : "") + juce::String (std::abs (v) < 0.005f ? 0.0f : v, 2) + " Hz";

        if (unit == Unit::Seconds)
            return fixed (v, v < 1.0f ? 2 : 1) + " s";

        if (unit == Unit::Db)
        {
            const auto rounded = std::round (v * 10.0f) / 10.0f;
            return (rounded > 0.0f ? "+" : "") + juce::String (rounded == 0.0f ? 0.0f : rounded, 1) + " dB";
        }

        if (unit == Unit::Amount)
            return juce::String (juce::roundToInt (100.0f * v)) + " %";

        if (unit == Unit::Shape)
        {
            if (x < 0.01f)                   return "Sine";
            if (std::abs (x - 0.5f) < 0.01f) return "Triangle";
            if (x > 0.99f)                   return "Square";
            return juce::String (juce::roundToInt (100.0f * x)) + " %";
        }

        return format (unit, x);
    }
}
