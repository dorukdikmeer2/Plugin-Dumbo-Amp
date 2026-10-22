#include "TubeStage.h"
#ifndef NEWTON_TOL
#define NEWTON_TOL 3e-9      // A; Newton converges quadratically, so a step this small leaves a residual far below it
#endif

namespace {
constexpr double kPi = 3.14159265358979323846;
inline double onePole(double fc, double T) { return 1.0 - std::exp(-2.0 * kPi * fc * T); }
}

//==============================================================================
// TubeTable
//==============================================================================
TubeTable::TubeTable(const TubeParams& t)
{
    tab.assign((size_t) nG * nP, 0.0f);
    gMin  = -(pMax / t.mu + 2.0);                 // below cutoff at the highest plate voltage
    dG    = (gMax - gMin) / (nG - 1);
    invDg = 1.0 / dG;
    TubeEval e;
    for (int iy = 0; iy < nP; ++iy)
        for (int ix = 0; ix < nG; ++ix) {
            korenEval(t, 1.0, gMin + ix * dG, iy * dP, e);
            tab[(size_t) iy * nG + ix] = (float) e.ip;
        }
}

const TubeTable& TubeTable::get(const TubeParams& t)
{
    static const TubeTable ax7(TUBE_12AX7);
    static const TubeTable au7(TUBE_12AU7);
    return (t.mu > 50.0) ? ax7 : au7;
}

//==============================================================================
// TriodeStage
//==============================================================================
void TriodeStage::prepare(double sampleRate, const Config& cfg)
{
    c = cfg;
    tbl = &TubeTable::get(c.tube);
    T = 1.0 / sampleRate;
    Gc = (c.Ck > 0.0) ? 2.0 * c.Ck / T : 0.0;
    aOut = onePole(1.0 / (2.0 * kPi * c.Cout * c.Rload), T);
    solveQuiescent();
    reset();
}

void TriodeStage::setCathodeBypass(double Ck)
{
    if (Ck == c.Ck) return;
    c.Ck = Ck;
    Gc = (Ck > 0.0) ? 2.0 * Ck / T : 0.0;
    icPrev = 0.0;                        // fresh capacitor history, voltage is continuous
}

void TriodeStage::setEmission(double e)
{
    c.emission = e;
    solveQuiescent();
    reset();
}

void TriodeStage::reset()
{
    vCin = 0.0; lpG = 0.0;
    vkPrev = Vk_q; icPrev = 0.0;
    hpOut = Vp_q;
    Iprev = Vk_q / c.Rk;
}

// Bracketed Newton: robust for every input including 1e6 V spikes.
static inline double solveCurrent(const TriodeStage::Config& c, const TubeTable& tb, double vG, double Gt, double Ihist,
                                  double guess, double& vkOut) noexcept
{
    const double Imax = c.Vcc / c.Ra;
    double lo = 0.0, hi = Imax;
    double I = std::min(std::max(guess, 0.0), Imax);
    TubeEval e;
    double vk = 0.0;
    for (int it = 0; it < 40; ++it) {
        vk = (I - Ihist) / Gt;
        double cs;
        const double vgk = gridClamp(vG - vk, cs);
        const double vpk = c.Vcc - I * c.Ra - vk;
        tb.eval(c.emission, vgk, vpk, e);
        const double F  = I - e.ip;
        if (F > 0.0) hi = I; else lo = I;
        const double Fp = 1.0 + cs * e.gm / Gt + e.gp * (c.Ra + 1.0 / Gt);
        double In = I - F / Fp;
        if (!(In > lo && In < hi)) In = 0.5 * (lo + hi);
        const double d = std::abs(In - I);
        I = In;
        if (d < NEWTON_TOL) break;
    }
    vkOut = (I - Ihist) / Gt;
    return I;
}

void TriodeStage::solveQuiescent()
{
    double vk;
    const double I = solveCurrent(c, *tbl, 0.0, 1.0 / c.Rk, 0.0, 0.5e-3, vk);
    Vk_q = vk;
    Vp_q = c.Vcc - I * c.Ra;

    TubeEval e;
    korenEval(c.tube, c.emission, -vk, Vp_q - vk, e);
    const double rp = (e.gp > 1e-12) ? 1.0 / e.gp : 1e9;
    const double Rpar = c.Ra * rp / (c.Ra + rp);
    const double muEff = e.gm * rp;
    const double Rkd = (c.Ck > 0.0) ? 0.0 : c.Rk;           // unbypassed cathode degenerates
    gain0 = muEff * c.Ra / (c.Ra + rp + (muEff + 1.0) * Rkd);
    (void) Rpar;

    Cmiller = 1.6e-12 + 1.7e-12 * (1.0 + gain0) + 5e-12;
    setSourceResistance(c.Rs);
}

void TriodeStage::setSourceResistance(double rs) noexcept
{
    c.Rs = std::max(rs, 100.0);
    const double fcM = std::min(1.0 / (2.0 * kPi * c.Rs * Cmiller), 0.45 / T);
    aMiller = onePole(fcM, T);
}

double TriodeStage::process(double vin) noexcept
{
    const double vA = vin - vCin;
    lpG += aMiller * (vA - lpG);

    const double Ihist = (Gc > 0.0) ? -(Gc * vkPrev + icPrev) : 0.0;
    const double Gt = 1.0 / c.Rk + Gc;

    double vk;
    const double I = solveCurrent(c, *tbl, lpG, Gt, Ihist, Iprev, vk);
    Iprev = I;

    if (Gc > 0.0) { icPrev = Gc * vk + Ihist; }
    vkPrev = vk;

    // grid-conduction current charges the coupling cap (blocking distortion)
    double cs;
    const double vgkC = gridClamp(lpG - vk, cs);
    const double ig = ((lpG - vk) - vgkC) / c.Rs;
    vCin += T * (vA / c.Rg + ig) / c.Cin;
    vCin = std::max(-200.0, std::min(200.0, vCin));

    const double Vp = c.Vcc - I * c.Ra;
    hpOut += aOut * (Vp - hpOut);
    return Vp - hpOut;
}

//==============================================================================
// LTPStage
//==============================================================================
void LTPStage::prepare(double sampleRate, const Config& cfg)
{
    c = cfg;
    tbl = &TubeTable::get(c.tube);
    T = 1.0 / sampleRate;
    aOut = onePole(1.0 / (2.0 * kPi * c.Cout * c.Rload), T);
    solveQuiescent();
    reset();
}

void LTPStage::reset()
{
    vCin = lpG = 0.0;
    hpA = VaQ; hpB = VbQ;
    // Ia/Ib already hold the quiescent point
}

namespace {
struct LtpSolution { double Ia, Ib; };

struct LtpPoint { double Fa, Fb, j11, j12, j21, j22; };

inline void ltpEval(const LTPStage::Config& c, const TubeTable& tb, double vgA, double Ia, double Ib, LtpPoint& p) noexcept
{
    const double Rt = c.Rtail, Ra = c.Ra;
    const double Vt = (Ia + Ib) * Rt + c.Vee;
    double csA;
    const double vgkA = gridClamp(vgA - Vt, csA);
    const double vgkB = -Vt;                                   // grid B is AC-grounded
    TubeEval ea, eb;
    tb.eval(c.emission, vgkA, c.Vcc - Ia * Ra - Vt, ea);
    tb.eval(c.emission, vgkB, c.Vcc - Ib * Ra - Vt, eb);
    p.Fa = Ia - ea.ip;
    p.Fb = Ib - eb.ip;
    p.j11 = 1.0 + csA * ea.gm * Rt + ea.gp * (Ra + Rt);
    p.j12 = csA * ea.gm * Rt + ea.gp * Rt;
    p.j21 = eb.gm * Rt + eb.gp * Rt;
    p.j22 = 1.0 + eb.gm * Rt + eb.gp * (Ra + Rt);
}

// Damped Newton (backtracking on |F|): robust from any starting point.
inline LtpSolution solveLtp(const LTPStage::Config& c, const TubeTable& tb, double vgA, double Ia, double Ib) noexcept
{
    const double Imax = c.Vcc / c.Ra;
    Ia = std::min(std::max(Ia, 0.0), Imax);
    Ib = std::min(std::max(Ib, 0.0), Imax);
    LtpPoint p;
    ltpEval(c, tb, vgA, Ia, Ib, p);
    double nrm = p.Fa * p.Fa + p.Fb * p.Fb;
    for (int it = 0; it < 60; ++it) {
        const double det = p.j11 * p.j22 - p.j12 * p.j21;
        if (std::abs(det) < 1e-12) break;
        const double dA = (p.Fa * p.j22 - p.j12 * p.Fb) / det;
        const double dB = (p.j11 * p.Fb - p.j21 * p.Fa) / det;
        if (std::abs(dA) < NEWTON_TOL && std::abs(dB) < NEWTON_TOL) break;

        double alpha = 1.0;
        LtpPoint q; double nA = Ia, nB = Ib, nn = nrm;
        for (int bt = 0; bt < 8; ++bt) {
            nA = std::min(std::max(Ia - alpha * dA, 0.0), Imax);
            nB = std::min(std::max(Ib - alpha * dB, 0.0), Imax);
            ltpEval(c, tb, vgA, nA, nB, q);
            nn = q.Fa * q.Fa + q.Fb * q.Fb;
            if (nn < nrm || nn < 1e-24) break;
            alpha *= 0.5;
        }
        Ia = nA; Ib = nB; p = q; nrm = nn;
    }
    return { Ia, Ib };
}
}

void LTPStage::solveQuiescent()
{
    auto s = solveLtp(c, *tbl, 0.0, 0.5e-3, 0.5e-3);
    Ia = s.Ia; Ib = s.Ib;
    VaQ = c.Vcc - Ia * c.Ra;
    VbQ = c.Vcc - Ib * c.Ra;

    TubeEval e;
    const double Vt = (Ia + Ib) * c.Rtail + c.Vee;
    korenEval(c.tube, c.emission, -Vt, VaQ - Vt, e);
    const double rp = (e.gp > 1e-12) ? 1.0 / e.gp : 1e9;
    const double A = e.gm * c.Ra * rp / (c.Ra + rp);
    const double Cm = 1.6e-12 + 1.7e-12 * (1.0 + 0.5 * A) + 5e-12;
    aMiller = onePole(std::min(1.0 / (2.0 * kPi * c.Rs * Cm), 0.45 / T), T);
}

void LTPStage::process(double vin, double& outA, double& outB) noexcept
{
    const double vA = vin - vCin;
    lpG += aMiller * (vA - lpG);

    auto s = solveLtp(c, *tbl, lpG, Ia, Ib);
    Ia = s.Ia; Ib = s.Ib;

    const double Vt = (Ia + Ib) * c.Rtail + c.Vee;
    double cs;
    const double vgkC = gridClamp(lpG - Vt, cs);
    const double ig = ((lpG - Vt) - vgkC) / c.Rs;
    vCin += T * (vA / c.Rg + ig) / c.Cin;
    vCin = std::max(-300.0, std::min(300.0, vCin));

    const double Va = c.Vcc - Ia * c.Ra;
    const double Vb = c.Vcc - Ib * c.Ra;
    hpA += aOut * (Va - hpA);
    hpB += aOut * (Vb - hpB);
    outA = Va - hpA;
    outB = Vb - hpB;
}
