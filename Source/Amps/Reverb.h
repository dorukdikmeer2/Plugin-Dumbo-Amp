#pragma once
#include <vector>
#include <cmath>
#include <algorithm>

//==============================================================================
// Reverbs used by the amps (all buffers allocated in prepare(), nothing allocates in process()).
//
// Both run at a reduced internal rate (~24 kHz) behind a triangular decimator / interpolating up-sampler:
// reverb tails are dark anyway, and it lets the spring use the long all-pass dispersion chains it needs to
// "drip" without costing CPU at the oversampled amp rate.
//
//   Room   (AMP-001 "V300"): 8-line feedback delay network, Householder mixing, per-line damping, slow
//          delay modulation (no metallic ringing), 20 ms pre-delay, ~1.9 s RT60 and a gentle low-cut.
//   Spring (AMP-002)       : three parallel spring "transmission lines" (long all-pass dispersion chains inside a
//          feedback loop, pick-up high-pass / transducer low-pass), ~2.4 s RT60, so the tail chirps.
//
// Both return the WET signal only, normalised so a unit-RMS input gives roughly unit-RMS output.
//==============================================================================
class AmpReverb {
public:
    enum class Type { Room, Spring };

    void prepare(double sampleRate, Type t);
    void reset();
    double process(double x) noexcept;

private:
    double tick(double x) noexcept;            // one step at the reverb's internal rate
    double sr = 48000.0, rate = 24000.0;
    int dec = 8, phase = 0;
    Type type = Type::Room;

    // resampling
    double d1 = 0.0, d2 = 0.0, acc1 = 0.0, acc2 = 0.0, prevOut = 0.0, curOut = 0.0, upLp = 0.0, aUp = 0.0, preLp = 0.0, aPre = 0.0;

    struct Line {
        std::vector<double> buf; int pos = 0, len = 1; double lp = 0.0, hp = 0.0;
        void init(int n) { len = std::max(2, n); buf.assign((size_t) len, 0.0); pos = 0; lp = hp = 0.0; }
        void clear() { std::fill(buf.begin(), buf.end(), 0.0); pos = 0; lp = hp = 0.0; }
    };
    struct AP { double a = 0.6, z = 0.0; double process(double x) noexcept { const double y = a * x + z; z = x - a * y; return y; } };
    struct Diff { std::vector<double> buf; int pos = 0, len = 1; };

    // ---- room (FDN)
    static constexpr int NF = 8;
    Line fdn[NF];
    double fdnG[NF] = {}, damp = 0.4, lowcut = 0.0, outHp = 0.0, modPhase[2] = {};
    double modRate = 0.0, modDepth = 0.0;
    Diff pre, ap1, ap2;
    double fdnState[NF] = {};

    // ---- spring
    static constexpr int NS = 3, NDISP = 64;
    Line spring[NS];
    AP disp[NS][NDISP];
    double springFb[NS] = {}, lpA = 0.0, hpA = 0.0;
    double outNorm = 1.0;
};
