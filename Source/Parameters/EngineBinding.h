#pragma once
#include <JuceHeader.h>
#include "Engine/EngineParams.h"

//==============================================================================
// EngineBinding — reads the host parameters (lock-free) into an EngineParams.
// This is the ONLY place where parameter IDs meet engine fields, so a new UI
// never has to touch the DSP side: it just attaches controls to the same IDs.
//==============================================================================
class EngineBinding {
public:
    void attach(juce::AudioProcessorValueTreeState& apvts);
    void read(EngineParams& out) const;               // real-time safe

private:
    using Raw = std::atomic<float>*;
    static float get(Raw p) { return p->load(std::memory_order_relaxed); }

    struct Channel { Raw gain = nullptr, bass = nullptr, mid = nullptr, treble = nullptr; };
    struct Mic { Raw type = nullptr, pos = nullptr, dist = nullptr, level = nullptr, pan = nullptr, on = nullptr, phase = nullptr; };
    struct Cab { Raw level = nullptr; Mic mic[kNumMics]; };

    Raw activeAmp = nullptr, activeCab = nullptr, inTrim = nullptr, outTrim = nullptr, cabOff = nullptr;
    Raw a1Mode = nullptr, a1Tube = nullptr, a1Char = nullptr, a1Pres = nullptr, a1Master = nullptr, a1Rev = nullptr, a1Gate = nullptr,
        a1EQByp = nullptr, a1EQ[6] = {};
    Raw a2Chan = nullptr, a2Master = nullptr, a2Pres = nullptr, a2Rev = nullptr, a2Stby = nullptr;
    Channel a1Ch[kNumDumbChannels], a2Ch[kNumDockChannels];
    Cab cab[kNumCabs];
};
