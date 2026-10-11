//
//  HallVerb.h
//  Rackbox Plus
//
//  The Hall: the same network tuned long. Lines between 29 and 97 ms at Size
//  100 % put the first reflections far apart; Decay runs from 0.5 to 15 s
//  (Rackbox's Hall: 0.8 to 7 s, 2.4 s in the middle).
//

#pragma once

#include "FdnReverb.h"

class HallVerb : public FdnReverb
{
public:
    static constexpr Dsp::Range decaySeconds { 0.5f, 15.0f, Dsp::Range::Curve::exponential };

    HallVerb() : FdnReverb ({ 29.0f, 97.0f, decaySeconds }) {}
};
