//
//  RoomVerb.h
//  Rackbox Plus
//
//  The Room: the eight-line network tuned short. Lines between 7 and 31 ms at
//  Size 100 % are the delays of a room a few metres across; Decay runs from
//  0.1 to 4 s (Rackbox's Room: 0.15 to 1.4 s, 0.46 s in the middle).
//

#pragma once

#include "FdnReverb.h"

class RoomVerb : public FdnReverb
{
public:
    static constexpr Dsp::Range decaySeconds { 0.1f, 4.0f, Dsp::Range::Curve::exponential };

    RoomVerb() : FdnReverb ({ 7.0f, 31.0f, decaySeconds }) {}
};
