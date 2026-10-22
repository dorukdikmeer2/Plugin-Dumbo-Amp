#pragma once
#include <cmath>
#include <algorithm>

namespace engine {

//==============================================================================
// ToneContour — extra BASS / MID / TREBLE range for both amps.
//
// The passive tone stacks of the schematics only move the sound by a few dB, which was too little to shape a tone
// with.  This stage sits right after the amp (before the cabinet) and follows the same three knobs: knob 5 = flat
// (exactly unity), knob 0 / 10 = -/+ kRangeDb at the shelf / bell centre.  It is NOT on the schematics.
//   bass   : low shelf  120 Hz
//   mid    : bell       700 Hz, Q 0.6
//   treble : high shelf 3.2 kHz
// Coefficients are refreshed once per chunk from the smoothed knob values.
//==============================================================================
class ToneContour {
public:
    static constexpr double kRangeDb = 9.0;

    void prepare(double sampleRate) { sr = sampleRate; reset(); setKnobs(5.0, 5.0, 5.0); }
    void reset() { for (auto& b : bq) b.z1 = b.z2 = 0.0; }

    void setKnobs(double bass, double mid, double treble)
    {
        design(bq[0], Kind::LowShelf,  120.0,  dbOf(bass),   0.7);
        design(bq[1], Kind::Peak,      700.0,  dbOf(mid),    0.6);
        design(bq[2], Kind::HighShelf, 3200.0, dbOf(treble), 0.7);
    }

    inline double process(double x) noexcept
    {
        for (auto& b : bq) {
            const double y = b.b0 * x + b.z1;
            b.z1 = b.b1 * x - b.a1 * y + b.z2;
            b.z2 = b.b2 * x - b.a2 * y;
            x = y;
        }
        return x;
    }

private:
    enum class Kind { LowShelf, Peak, HighShelf };
    struct Bq { double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0; };

    static double dbOf(double knob) { return (std::min(std::max(knob, 0.0), 10.0) - 5.0) * (kRangeDb / 5.0); }

    void design(Bq& q, Kind k, double f, double gainDb, double Q) const
    {
        if (std::fabs(gainDb) < 1e-4) { q.b0 = 1; q.b1 = q.b2 = q.a1 = q.a2 = 0; return; }     // exactly flat
        const double A = std::pow(10.0, gainDb / 40.0), w = 2.0 * 3.14159265358979323846 * std::min(f, 0.45 * sr) / sr;
        const double c = std::cos(w), s = std::sin(w), al = s / (2.0 * Q), sq = 2.0 * std::sqrt(A) * al;
        double b0, b1, b2, a0, a1, a2;
        switch (k) {
            case Kind::LowShelf:
                b0 = A * ((A + 1) - (A - 1) * c + sq); b1 = 2 * A * ((A - 1) - (A + 1) * c); b2 = A * ((A + 1) - (A - 1) * c - sq);
                a0 = (A + 1) + (A - 1) * c + sq;       a1 = -2 * ((A - 1) + (A + 1) * c);    a2 = (A + 1) + (A - 1) * c - sq; break;
            case Kind::HighShelf:
                b0 = A * ((A + 1) + (A - 1) * c + sq); b1 = -2 * A * ((A - 1) + (A + 1) * c); b2 = A * ((A + 1) + (A - 1) * c - sq);
                a0 = (A + 1) - (A - 1) * c + sq;       a1 = 2 * ((A - 1) - (A + 1) * c);     a2 = (A + 1) - (A - 1) * c - sq; break;
            default:
                b0 = 1 + al * A; b1 = -2 * c; b2 = 1 - al * A; a0 = 1 + al / A; a1 = -2 * c; a2 = 1 - al / A; break;
        }
        q.b0 = b0 / a0; q.b1 = b1 / a0; q.b2 = b2 / a0; q.a1 = a1 / a0; q.a2 = a2 / a0;
    }

    double sr = 48000.0;
    Bq bq[3];
};

} // namespace engine
