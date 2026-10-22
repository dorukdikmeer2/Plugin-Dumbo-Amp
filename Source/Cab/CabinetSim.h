#pragma once
#include "Cab/CabIrDesigner.h"
#include <array>
#include <vector>

//==============================================================================
// CabinetSim — a guitar cabinet miked with one or two microphones.
//
//   Cab 0 : 4x12 closed-back, 25 W "Greenback"-style drivers (warm, 5 kHz roll-off)
//   Cab 1 : 1x12 open-back, "Vintage 30"-style driver (forward upper-mids)
//
// Each microphone is a FIR whose impulse response is synthesised on the fly by
// CabIrDesigner from the mic model, its position and its distance (see that
// file for the physics).  The two mic paths share one input history, so the
// second mic costs one more convolution only while it is switched on.
//
// Any change of cabinet / mic type / position / distance renders a new IR and
// morphs from the old one to the new one over 512 samples (the effective IR is
// re-blended every 32 samples, so only ONE convolution per mic runs even while
// a knob is being dragged), so moving a knob or switching cabs never clicks.
// Renders are rate-limited to one per 30 ms while a knob is being dragged.
//
// Level / pan / polarity of the mics are applied by the engine through
// setMix(); they are ramped sample-by-sample here.
//==============================================================================
class CabinetSim {
public:
    static constexpr int MAX_IR = 2048;

    void prepare(double sampleRate);
    void reset();

    void setCabinet(int index);                                    // 0 = 4x12, 1 = 1x12
    void setMic(int slot, int type, float position01, float distance01, bool enabled);
    void setMix(int slot, float gainL, float gainR);               // linear gains per output channel

    // mono in -> stereo out (not in place).  Real-time safe.
    void process(const float* in, float* outL, float* outR, int numSamples) noexcept;

    int irLength() const { return irLen; }
    // test hook: the most recent IR of a mic path (after cross-fades have finished)
    const float* currentIr(int slot) const { return path[slot].nxt.data(); }
    void forceRender() { renderNow(); }
    // power statistics of the two mic IRs over the guitar band (80 Hz - 2.5 kHz, equal weight per octave = pink):
    //   e0, e1 = mean |H|^2 of mic 1 / mic 2,   c = mean Re(H0 * conj(H1))  (the cross term; negative = the mics partly cancel)
    // so a gain pair (a, b) gives the power  a^2 e0 + b^2 e1 + 2 a b c.  The engine uses it to keep loudness constant.
    void pairStats(double& e0, double& e1, double& c) const { e0 = statE0; e1 = statE1; c = statC; }

private:
    struct MicState { int type = 0; float pos = 0.3f, dist = 0.25f; bool enabled = true; };
    struct Path {
        std::array<float, MAX_IR> cur {}, nxt {}, blend {};
        float gL = 0.f, gR = 0.f, tL = 0.f, tR = 0.f;              // current / target mix gains
    };

    void renderNow();
    void computeStats();
    bool needsRender() const;
    float convolve(const float* ir) const noexcept;

    double sr = 48000.0;
    int irLen = 1024;
    int cabinet = 0;
    MicState mic[2], rendered[2];
    int renderedCab = -1;
    cab::CabIrDesigner designer;
    Path path[2];
    double statE0 = 1.0, statE1 = 1.0, statC = 0.0;
    int fadePos = 0, fadeLen = 0;
    int cooldown = 0, minRenderInterval = 480;

    std::vector<float> hist;                                       // doubled circular input history
    int hpos = 0;
};
