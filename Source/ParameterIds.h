//
//  ParameterIds.h
//  Rackbox Plus
//
//  Every parameter the host can see, named in one place.
//
//  These ids are the identity a saved session is restored by; once a session
//  exists none of them may change. Rename a display name instead.
//
//  A block's parameters are the same fourteen suffixes four times over, with the
//  block number in front: "b2_type". `type` and `sub` are stepped choices, and
//  `p1` to `p8` are the eight knobs of whatever effect is loaded - named after
//  position because their meaning changes with the effect, and an automation
//  lane should keep pointing at "the third knob" when the effect behind it is
//  swapped. An effect with fewer knobs leaves the rest unused.
//

#pragma once

#include <juce_core/juce_core.h>

namespace RackParams
{
    constexpr int stateVersion = 1;
    constexpr int numBlocks = 4;

    inline const juce::Identifier stateTreeType { "RACKBOXPLUS" };

    inline constexpr const char* lowpass  = "lpf";
    inline constexpr const char* highpass = "hpf";
    inline constexpr const char* dryWet   = "drywet";

    namespace Block
    {
        inline constexpr const char* type   = "type";
        inline constexpr const char* sub    = "sub";
        inline constexpr const char* bypass = "bypass";
        inline constexpr const char* sync   = "sync";
        inline constexpr const char* pre    = "pre";
        inline constexpr const char* mix    = "mix";

        constexpr int numKnobs = 8;
        inline constexpr const char* knobs[numKnobs] { "p1", "p2", "p3", "p4", "p5", "p6", "p7", "p8" };

        // The first two knobs under Rackbox's names for them.
        inline constexpr const char* a = knobs[0];
        inline constexpr const char* b = knobs[1];
    }

    /** "b1_type" for block 0. */
    inline juce::String blockId (int block, const char* suffix)
    {
        return "b" + juce::String (block + 1) + "_" + suffix;
    }
}
