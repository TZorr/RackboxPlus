//
//  EffectCatalog.h
//  Rackbox Plus
//
//  The list of what a block can hold: four categories, fourteen effects, and
//  for each the knobs it has (up to eight).
//
//  This is the one place that knows the names. The editor prints them, the
//  display prints them, the block asks it for an instance - and the effects
//  themselves know nothing about their own name or what their two knobs are
//  called. That is on purpose: a block's two effect knobs are fixed parameters
//  whose meaning depends on what is loaded, and the table is where "the first
//  knob" turns into Rate, Size or Time.
//
//  Categories are chosen by one parameter and the subtype by another, and the
//  subtype range is the longest list (five, since Modulation gained the
//  frequency Shifter in 0.4.0) for every category. A shorter category clamps: a stored
//  subtype of 3 or 4 in Reverb means Hall, so a saved session never asks for an
//  effect that does not exist.
//

#pragma once

#include <memory>

#include <juce_core/juce_core.h>

#include "Effect.h"

namespace EffectCatalog
{
    constexpr int numCategories = 4;
    constexpr int maxSubtypes = 5;
    constexpr int numEffects = 14;

    /** How a knob's value is printed. The first seven have their own maps (Dsp::Map); the rest
        print the knob's Range: Hz and kHz, ms, a whole count, a signed percentage, signed Hz, and
        an LFO shape (Sine, Triangle, Square, or the percentage in between), seconds, and an
        amount as a percentage of the Range's value (100 % where it is 1), and a level in dB. */
    enum class Unit { Percent, RateHz, TimeMs, Bits, Factor, Division, ShiftHz,
                      Hz, Ms, Count, SignedPercent, SignedHz, Shape, Seconds, Amount, Db };

    /** One knob of an effect: its name, how its value is printed, and where it sits when the
        effect is chosen on the panel. */
    struct Knob
    {
        const char* label = nullptr;   // "Rate"; none for a knob the effect does not have
        Unit unit = Unit::Percent;
        float defaultValue = 0.5f;     // knob position, 0..1
        Dsp::Range range {};           // for the units that print a Range
    };

    struct Info
    {
        const char* name;       // "Chorus"
        Knob knobs[Effect::numKnobs];

        /** The knobs the effect has: the first ones, up to the first without a label. */
        int numKnobs() const noexcept
        {
            int n = 0;
            while (n < Effect::numKnobs && knobs[n].label != nullptr)
                ++n;
            return n;
        }
    };

    const char* categoryName (int category);
    int numSubtypes (int category);

    /** The subtype limited to what the category has. */
    int clampSubtype (int category, int subtype);

    /** Category and subtype to a running index 0..numEffects-1. */
    int index (int category, int subtype);

    const Info& info (int category, int subtype);

    /** A fresh instance. Allocates - never call from the audio thread. */
    std::unique_ptr<Effect> create (int effectIndex);

    /** Reverbs have a pre-delay button where the others have Sync. */
    bool hasPreDelay (int category);

    /** Whether the effect's first knob is a time or a rate that can follow the host tempo. */
    bool canSync (int category, int subtype);

    /** A knob position 0..1 printed in the unit the effect uses for it. */
    juce::String format (Unit unit, float normalised);

    /** The same for a knob of the catalog, Range and all. */
    juce::String format (const Knob& knob, float normalised);
}
