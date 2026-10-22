#pragma once
#include <cmath>
#include <algorithm>
#include <vector>

//==============================================================================
// Small real-time-safe DSP helpers shared by the amp models and the processor
//==============================================================================

namespace dsp_util {

constexpr double kPi = 3.14159265358979323846;

inline double dbToGain(double db) { return std::pow(10.0, db / 20.0); }

// Audio-taper pot: 0..10 -> 0..1 gain (log-ish like a 'A' pot, exact 0 at 0)
inline double audioTaper(double knob0to10, double exponent = 2.0)
{
    const double x = std::min(std::max(knob0to10, 0.0), 10.0) / 10.0;
    return std::pow(x, exponent);
}


// MASTER output make-up.  The schematic's MASTER pot has a steep audio taper (x^2.5 = 40 dB between 2.5 and 5), which
// makes the loudness jump wildly and drives every following stage into the limiter.  The pot still feeds the power
// amp exactly as drawn (so the power-amp saturation character follows it), but the *output level* is brought back
// onto a gentle loudness law (dB re MASTER 5):   0:-50  .5:-40  1:-30  2:-18  3:-11  4:-5  5:0  6:+2.5  7:+4.5  8:+6  9:+7  10:+8
// This make-up is applied after the power stage as a linear gain.
inline double masterLevelLawDb(double knob0to10)
{
    const double k = std::min(std::max(knob0to10, 0.0), 10.0);
    static const double kx[12] = { 0, 0.5, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10 };
    static const double ky[12] = { -50, -40, -30, -18, -11, -5, 0, 2.5, 4.5, 6, 7, 8 };
    int i = 0;
    while (i < 10 && k > kx[i + 1]) ++i;
    const double f = (k - kx[i]) / (kx[i + 1] - kx[i]);
    return ky[i] + f * (ky[i + 1] - ky[i]);
}
inline double masterOutComp(double knob0to10, double taperExponent = 2.5)
{
    const double k = std::min(std::max(knob0to10, 0.0), 10.0);
    const double pot = 20.0 * taperExponent * std::log10(std::max(k, 0.05) / 10.0);      // dB of the pot taper
    const double pot5 = 20.0 * taperExponent * std::log10(0.5);
    const double compDb = masterLevelLawDb(k) - (pot - pot5);
    return std::pow(10.0, compDb / 20.0) * std::min(1.0, k / 0.25);                      // true mute at the bottom of the pot
}

// Transparent below 0.8, asymptotically bounded at +/-1.0 (never exceeds 1.0)
inline float softLimit(float x) noexcept
{
    const float a = std::fabs(x);
    if (a < 0.8f) return x;
    const float t = (a - 0.8f) * 5.0f;                 // 0.2 headroom
    const float y = 0.8f + 0.2f * std::tanh(t);
    return x < 0.0f ? -y : y;
}

// Soft, symmetric guard used between stages (very rarely active in normal use)
inline double guard(double x, double lim) noexcept
{
    if (!(x == x)) return 0.0;                          // NaN -> 0
    if (x > lim) return lim;
    if (x < -lim) return -lim;
    return x;
}

//------------------------------------------------------------------------------
class DcBlocker {
public:
    void prepare(double sr, double fc = 20.0) { R = 1.0 - 2.0 * kPi * fc / sr; reset(); }
    void reset() { x1 = y1 = 0.0; }
    inline double process(double x) noexcept {
        const double y = x - x1 + R * y1;
        x1 = x; y1 = y;
        return y;
    }
private:
    double R = 0.9995, x1 = 0.0, y1 = 0.0;
};

//------------------------------------------------------------------------------
// Linear-phase halfband 2x oversampler, K = centre-tap index (K = 4m+3: 31 or 15).
//   K=31: 63 taps, Kaiser beta 9 ~ -90 dB alias rejection, latency 31 samples (up+down)
//   K=15: 31 taps (used for the second octave of the 4x cascade), latency 15 samples
//------------------------------------------------------------------------------
template <int KK>
class HalfbandOS {
public:
    static constexpr int K = KK;
    static constexpr int NP = KK + 1;            // non-zero even-phase taps
    static constexpr int LATENCY = KK;           // samples at the base rate (up + down)

    HalfbandOS() { design(); reset(); }

    void reset() {
        std::fill(std::begin(upHist), std::end(upHist), 0.0);
        std::fill(std::begin(dnA), std::end(dnA), 0.0);
        std::fill(std::begin(dnB), std::end(dnB), 0.0);
        upPos = dnPos = 0;
    }

    inline void up(double x, double& y0, double& y1) noexcept {
        upPos = (upPos + 1) & MASK;
        upHist[upPos] = x;
        upHist[upPos + 64] = x;                  // mirrored for contiguous reads
        const double* h = &upHist[upPos + 64];   // h[-i] = x[n-i]
        double acc = 0.0;
        for (int i = 0; i < NP; ++i) acc += g[i] * h[-i];
        y0 = 2.0 * acc;
        y1 = 2.0 * hc * h[-(K - 1) / 2];
    }

    inline double down(double a, double b) noexcept {
        dnPos = (dnPos + 1) & MASK;
        dnA[dnPos] = a;  dnA[dnPos + 64] = a;
        dnB[dnPos] = b;  dnB[dnPos + 64] = b;
        const double* ha = &dnA[dnPos + 64];
        const double* hb = &dnB[dnPos + 64];
        double acc = 0.0;
        for (int i = 0; i < NP; ++i) acc += g[i] * ha[-i];
        return acc + hc * hb[-(K + 1) / 2];
    }

private:
    static constexpr int MASK = 63;
    double g[NP];                                // even causal taps g[2i]
    double hc = 0.5;                             // centre tap
    double upHist[128], dnA[128], dnB[128];
    int upPos = 0, dnPos = 0;

    static double bessel0(double x) {
        double s = 1.0, t = 1.0;
        for (int k = 1; k < 50; ++k) { t *= (x / (2.0 * k)) * (x / (2.0 * k)); s += t; }
        return s;
    }

    void design() {
        double h[2 * K + 1];
        const double beta = 9.0;
        double sum = 0.0;
        for (int j = 0; j <= 2 * K; ++j) {
            const int m = j - K;
            double v;
            if (m == 0) v = 0.5;
            else if ((m & 1) == 0) v = 0.0;
            else {
                const double s = std::sin(kPi * m / 2.0) / (kPi * m);
                const double r = (double) m / (K + 1);
                v = s * bessel0(beta * std::sqrt(std::max(0.0, 1.0 - r * r))) / bessel0(beta);
            }
            h[j] = v; sum += v;
        }
        for (int j = 0; j <= 2 * K; ++j) h[j] /= sum;       // exact unity DC gain
        for (int i = 0; i < NP; ++i) g[i] = h[2 * i];
        hc = h[K];
    }
};

using Oversampler2x = HalfbandOS<31>;

// 4x = cascade of two halfband stages (63-tap at the base rate, 31-tap at 2x).
// process(): feeds one base-rate sample and runs `fn` four times at the 4x rate.
class Oversampler4x {
public:
    static constexpr int LATENCY = 39;           // 31 + ceil(15 / 2) base-rate samples
    void reset() { a.reset(); b.reset(); }
    template <typename Fn>
    inline double process(double x, Fn&& fn) noexcept {
        double u0, u1; a.up(x, u0, u1);
        double v[4];
        b.up(u0, v[0], v[1]);
        b.up(u1, v[2], v[3]);
        for (int i = 0; i < 4; ++i) v[i] = fn(v[i]);
        const double d0 = b.down(v[0], v[1]);
        const double d1 = b.down(v[2], v[3]);
        return a.down(d0, d1);
    }
private:
    HalfbandOS<31> a;
    HalfbandOS<15> b;      // the 2x-rate halfband (separate up / down histories)
};

} // namespace dsp_util
