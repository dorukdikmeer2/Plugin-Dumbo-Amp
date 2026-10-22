#include "PowerStage.h"

namespace { constexpr double kPi = 3.14159265358979323846; }

void PushPullPowerStage::prepare(double sampleRate)
{
    sr = sampleRate;
    aAtt = 1.0 - std::exp(-1.0 / (0.005 * sr));
    aHp  = 1.0 - std::exp(-2.0 * kPi * 28.0 / sr);        // OT primary inductance
    aLp  = 1.0 - std::exp(-2.0 * kPi * std::min(15000.0, 0.45 * sr) / sr);   // leakage inductance
    setSpec(sp);
    reset();
}

void PushPullPowerStage::setSpec(const Spec& s)
{
    sp = s;
    aRel = 1.0 - std::exp(-1.0 / (sp.sagRelease * sr));
    slope0 = 2.0 * sp.expo * std::pow(sp.cutoff, sp.expo - 1.0);
}

void PushPullPowerStage::reset() { env = otHp = otLp = 0.0; }

double PushPullPowerStage::process(double vdiff) noexcept
{
    // grid drive per tube, relative to bias; grid conduction softens large swings
    double x = 0.5 * vdiff / sp.vBias;
    constexpr double gl = 2.4;
    x = gl * std::tanh(x / gl);

    const double iA = std::pow(softplus(sp.cutoff + x, 0.06), sp.expo);
    const double iB = std::pow(softplus(sp.cutoff - x, 0.06), sp.expo);
    double y = (iA - iB) / slope0;                         // unity small-signal gain

    // supply-limited swing with sag
    const double S = sp.ssat * (1.0 - sp.sag * sagScale * std::min(env, 1.0));
    const double u = std::abs(y) / S;
    y = y / std::cbrt(1.0 + u * u * u);                    // sharp-knee limiter

    const double a = std::abs(y);
    env += ((a > env) ? aAtt : aRel) * (a - env);

    // output transformer: band-limit + core saturation
    otLp += aLp * (y - otLp);
    otHp += aHp * (otLp - otHp);
    double z = otLp - otHp;
    z = z / (1.0 + 0.08 * z * z);
    return z;
}
