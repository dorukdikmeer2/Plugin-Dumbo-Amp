#pragma once
#include <cmath>
#include <algorithm>
#include <vector>

//==============================================================================
// Circuit-level tube models.
//
//  * Koren triode equations (plate current + analytic partial derivatives)
//  * TriodeStage : one common-cathode gain stage, solved per sample
//        - plate load Ra from HT, cathode resistor Rk with switchable bypass Ck
//          (trapezoidal companion model of the capacitor)
//        - input coupling cap Cin + grid leak Rg with grid-conduction clamp
//          (blocking distortion / bias shift emerges from the cap charging)
//        - grid-stopper Rs + Miller capacitance low-pass
//        - output coupling cap Cout into Rload (high-pass)
//  * LTPStage    : long-tail-pair phase inverter, 2x2 Newton solve with the
//                  shared tail resistor
//
//  All voltages are real volts.  All values come from the AMP-001/AMP-002
//  schematics (see Amp001Model.cpp / Amp002Model.cpp).
//==============================================================================

struct TubeParams {
    double mu, ex, Kg1, Kp, Kvb;
};

// Norman Koren's published parameter sets
static constexpr TubeParams TUBE_12AX7 = { 100.0, 1.4, 1060.0, 600.0, 300.0 };
static constexpr TubeParams TUBE_12AU7 = {  21.5, 1.3, 1180.0,  84.0, 300.0 };

struct TubeEval { double ip, gm, gp; };   // A, A/V (d/dVgk), A/V (d/dVpk)

inline void korenEval(const TubeParams& t, double emission, double vgk, double vpk, TubeEval& r) noexcept
{
    r.ip = r.gm = r.gp = 0.0;
    if (vpk <= 1e-6) return;                       // plate below cathode: no conduction

    const double s2 = t.Kvb + vpk * vpk;
    const double s  = std::sqrt(s2);
    const double u  = t.Kp * (1.0 / t.mu + vgk / s);

    double L, sig;                                  // softplus(u) and its slope (logistic)
    if (u > 30.0)       { L = u;  sig = 1.0; }
    else if (u < -30.0) { const double e = std::exp(u); L = e; sig = e; }
    else                { const double e = std::exp(u); L = std::log1p(e); sig = e / (1.0 + e); }

    const double E1 = vpk / t.Kp * L;
    if (E1 <= 1e-12) return;

    const double ip = emission * 2.0 / t.Kg1 * std::pow(E1, t.ex);
    const double k  = t.ex * ip / E1;
    const double dE_dvg = vpk / s * sig;
    const double dE_dvp = L / t.Kp - vpk * vpk * vgk * sig / (s2 * s);

    r.ip = ip;
    r.gm = k * dE_dvg;
    r.gp = std::max(0.0, k * dE_dvp);
}

//------------------------------------------------------------------------------
// Pre-tabulated Koren surface (bilinear; gm/gp are the exact derivatives of the
// interpolant, so Newton converges cleanly).  ~4x faster than evaluating the
// transcendental form in the inner loop.
//------------------------------------------------------------------------------
class TubeTable {
public:
    static constexpr double gMax = 0.8;
    static constexpr double pMax = 500.0, dP = 1.0;
    static constexpr int nP = (int) (pMax / dP + 0.5) + 1;
    static constexpr int nG = 541;
    double gMin = -10.0, dG = 0.02, invDg = 50.0;

    explicit TubeTable(const TubeParams& t);

    inline void eval(double emission, double vgk, double vpk, TubeEval& r) const noexcept
    {
        r.ip = r.gm = r.gp = 0.0;
        if (vpk <= 0.0) return;
        double x = (vgk - gMin) * invDg;
        if (x <= 0.0) return;                               // deep cutoff
        if (x > nG - 1.0001) x = nG - 1.0001;
        double y = vpk * (1.0 / dP);
        if (y > nP - 1.0001) y = nP - 1.0001;
        const int ix = (int) x, iy = (int) y;
        const double fx = x - ix, fy = y - iy;
        const float* p = &tab[(size_t) iy * nG + ix];
        const double i00 = p[0], i10 = p[1], i01 = p[nG], i11 = p[nG + 1];
        r.ip = emission * ((i00 * (1 - fx) + i10 * fx) * (1 - fy) + (i01 * (1 - fx) + i11 * fx) * fy);
        r.gm = emission * ((i10 - i00) * (1 - fy) + (i11 - i01) * fy) * invDg;
        r.gp = emission * ((i01 - i00) * (1 - fx) + (i11 - i10) * fx) * (1.0 / dP);
    }

    static const TubeTable& get(const TubeParams& t);

private:
    std::vector<float> tab;
};

//------------------------------------------------------------------------------
// Grid conduction: smooth clamp of Vgk to ~ +0.6 V.  Returns clamped value,
// writes d(clamped)/d(raw) into 'slope'.
//------------------------------------------------------------------------------
inline double gridClamp(double vgk, double& slope) noexcept
{
    if (vgk <= 0.0) { slope = 1.0; return vgk; }
    const double k = 0.6;
    const double th = std::tanh(vgk / k);
    slope = 1.0 - th * th;
    return k * th;
}

//==============================================================================
class TriodeStage {
public:
    struct Config {
        double Ra    = 100e3;     // plate load
        double Rk    = 1.5e3;     // cathode resistor
        double Ck    = 0.0;       // cathode bypass (0 = none)
        double Rs    = 68e3;      // grid stopper
        double Cin   = 22e-9;     // input coupling cap
        double Rg    = 1e6;       // grid leak
        double Cout  = 22e-9;     // output coupling cap
        double Rload = 1e6;       // what the output cap drives
        double Vcc   = 300.0;     // HT at the top of Ra
        TubeParams tube = TUBE_12AX7;
        double emission = 1.0;    // 1 = nominal heater
    };

    void prepare(double sampleRate, const Config& cfg);
    void reset();

    void setCathodeBypass(double Ck);       // runtime switch (mode selector)
    void setEmission(double e);             // heater voltage
    // Source resistance seen by the grid (grid stopper + whatever drives it, e.g. a pot wiper).
    // Limits grid current (less blocking distortion) and sets the Miller low-pass.
    void setSourceResistance(double rs) noexcept;

    // vin: volts at the stage input (before Cin).  Returns AC plate volts after Cout.
    double process(double vin) noexcept;

    double quiescentPlate() const { return Vp_q; }
    double smallSignalGain() const { return gain0; }

private:
    void solveQuiescent();

    Config c {};
    const TubeTable* tbl = nullptr;
    double T = 1.0 / 48000.0;
    double Gc = 0.0, vkPrev = 0.0, icPrev = 0.0;      // cathode cap companion state
    double vCin = 0.0;                                 // charge on input coupling cap
    double lpG = 0.0, aMiller = 0.0;                   // Miller low-pass
    double hpOut = 0.0, aOut = 0.0;                    // output HPF state
    double Iprev = 0.0, Vp_q = 0.0, Vk_q = 0.0, gain0 = 0.0, Cmiller = 5e-12;
};

//==============================================================================
class LTPStage {
public:
    struct Config {
        double Ra    = 100e3;
        double Rtail = 56e3;
        double Vee   = -60.0;     // tail returned to the bias rail (schematic: 'to -V rail')
        double Rs    = 1.5e3;
        double Cin   = 22e-9;
        double Rg    = 100e3;     // input resistor (also sets the HPF)
        double Cout  = 22e-9;
        double Rload = 470e3;
        double Vcc   = 300.0;
        TubeParams tube = TUBE_12AX7;
        double emission = 1.0;
    };

    void prepare(double sampleRate, const Config& cfg);
    void reset();
    void setEmission(double e) { c.emission = e; solveQuiescent(); }

    // vin: volts at the input.  outA/outB: AC plate volts, anti-phase.
    void process(double vin, double& outA, double& outB) noexcept;

private:
    void solveQuiescent();

    Config c {};
    const TubeTable* tbl = nullptr;
    double T = 1.0 / 48000.0;
    double vCin = 0.0, lpG = 0.0, aMiller = 0.0;
    double hpA = 0.0, hpB = 0.0, aOut = 0.0;
    double Ia = 0.0, Ib = 0.0, VaQ = 0.0, VbQ = 0.0;
};
