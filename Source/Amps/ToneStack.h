#pragma once
#include <array>
#include <cmath>
#include <algorithm>

//==============================================================================
// Tone circuits
//
// AMP-001 : six-band gyrator EQ (80 / 250 / 800 / 2.5k / 6k / 12k Hz,
//           +/-15 dB, Q ~ 0.7 — values from PCB-B of the AMP-001 schematic)
// AMP-002 : passive bass/mid/treble stack, solved from its component values
//           with the exact third-order transfer function (Yeh/Smith) and
//           discretised with the bilinear transform.
//==============================================================================

struct Biquad {
    // transposed direct form II
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    double z1 = 0, z2 = 0;

    void reset() { z1 = z2 = 0.0; }

    inline double process(double x) noexcept {
        const double y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }

    void setPeaking(double fc, double gainDB, double Q, double sr);
    void setHighShelf(double fc, double gainDB, double sr);
};

//------------------------------------------------------------------------------
class SixBandEQ {
public:
    static constexpr int NUM_BANDS = 6;
    static constexpr double BAND_FREQS[NUM_BANDS] = { 80, 250, 800, 2500, 6000, 12000 };
    static constexpr double BAND_Q = 0.7;
    static constexpr double MAX_GAIN_DB = 15.0;

    void prepare(double sampleRate);
    void reset();
    void setBandGain(int band, double normalised);        // -1 .. +1
    inline double process(double x) noexcept {
        for (int i = 0; i < NUM_BANDS; ++i) x = filters[i].process(x);
        return x;
    }
private:
    double sr = 48000.0;
    double gains[NUM_BANDS] = {};
    Biquad filters[NUM_BANDS];
};

//------------------------------------------------------------------------------
class TMBToneStack {
public:
    // Component values.  Defaults = AMP-002 schematic (PCB-A "PASSIVE TONE STACK").
    struct Values {
        double R_TREBLE = 250e3;   // VR_treb
        double R_BASS   = 250e3;   // VR_bass
        double R_MID    = 10e3;    // VR_mid
        double R_SLOPE  = 56e3;    // not on the sheet: standard slope resistor
        double C_TREBLE = 250e-12; // sheet says 4.7 nF -> would make the treble pot inert; 250 pF is the TMB value
        double C_BASS   = 47e-9;
        double C_MID    = 22e-9;
        double bassTaper = 2.5, trebleTaper = 2.5;   // pot law exponents (1 = linear)
    };
    // Marshall-style stack as used after the distortion stages of the high-gain amps studied
    // for the AMP-001 voicing (JCM800 / 5150 family: 470 pF, 22 n, 22 n, 1 M bass, 25 k mid, 33 k slope).
    static Values marshall() { Values v; v.R_TREBLE = 220e3; v.R_BASS = 1e6; v.R_MID = 25e3; v.R_SLOPE = 33e3;
                               v.C_TREBLE = 470e-12; v.C_BASS = 22e-9; v.C_MID = 22e-9; v.bassTaper = 2.0; v.trebleTaper = 2.0; return v; }
    // Brit-vox style stack for the clean/edge-of-breakup channel B voicing (much lighter bass cut)
    void setValues(const Values& v) { val = v; update(); }

    void prepare(double sampleRate);
    void reset();
    void setControls(double bass01, double mid01, double treble01);
    inline double process(double x) noexcept {
        const double y = nb[0] * x + s1;
        s1 = nb[1] * x - na[1] * y + s2;
        s2 = nb[2] * x - na[2] * y + s3;
        s3 = nb[3] * x - na[3] * y;
        return y;
    }
private:
    void update();
    Values val;
    double sr = 48000.0, bass = 0.5, mid = 0.5, treble = 0.5;
    double nb[4] = { 1, 0, 0, 0 }, na[4] = { 1, 0, 0, 0 };
    double s1 = 0, s2 = 0, s3 = 0;
};

//------------------------------------------------------------------------------
// Presence: HF lift in the power-amp feedback loop (modelled as a high shelf)
//------------------------------------------------------------------------------
class PresenceEQ {
public:
    void prepare(double sampleRate) { sr = sampleRate; setPresence(0.5); reset(); }
    void reset() { f.reset(); }
    void setPresence(double v01);
    inline double process(double x) noexcept { return f.process(x); }
private:
    double sr = 48000.0;
    Biquad f;
};
