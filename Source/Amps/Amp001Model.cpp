#include "Amp001Model.h"
#include "Amps/Calibration.h"

constexpr double Amp001Model::CK_MODE[4];
using namespace dsp_util;
#ifdef AMP_DEBUG_TAPS
double g_tap[8];
#define TAP(i, v) (g_tap[i] = (v))
#else
#define TAP(i, v) ((void) 0)
#endif

//------------------------------------------------------------------------------
// Per-mode voicing table (the numbers that were tuned against the measurement suite in tests/)
//------------------------------------------------------------------------------
const Amp001Model::ModeCfg& Amp001Model::cfg(int m) noexcept
{
    static const ModeCfg T[4] = {
        // 1: CLEAN  - SW1 open cathode.  Maximum headroom (input pad), pristine, wide.
        { 0.25, 0.0, 0, -66.0, 4.6,  60.0, 170.0, 0.5,
          { 1.0, 1.0, 1.0 }, { 0, 0, 0 }, { 3000, 3000, 3000 }, { 20000, 20000, 20000 },
          { 10e-9, 10e-9 }, { 1e6, 1e6 }, { 1.5e3, 1.5e3 }, { 0, 0 }, 0.0, 108e3, { 88e3, 88e3 }, 14000.0, 1.0, 0, 0.0, 1.0 },
        // 2: CRUNCH - SW1 1uF.  Blues-rock crunch: warm, mid-focused, edge-of-breakup at the low end of GAIN and a smooth
        //    crunch (no fuzz, no fizz) at the top.  Less drive into V1a / V2a, a darker interstage and fizz filter.
        { 0.5, 0.0, 0, -68.0, 6.0,  70.0, 150.0, 0.5,
          { 0.6, 1.0, 1.0 }, { 0, 0, 0 }, { 3000, 3000, 3000 }, { 4200, 12000, 12000 },
          { 10e-9, 10e-9 }, { 1e6, 1e6 }, { 1.5e3, 1.5e3 }, { 0, 0 }, 0.0, 150e3, { 88e3, 88e3 }, 3400.0, 1.0, 1, 4.4, 0.36 },
        // 3: LEAD   - SW1 4.7uF + one cascaded stage (JCM800 / Mark-style lead)
        { 1.0, 0.68e-6, 1, -70.0, 6.2, 60.0, 130.0, 0.5,
          { 0.32, 0.26, 1.0 }, { 0.2, 0.1, 0 }, { 2500, 2500, 2500 }, { 7500, 6500, 6000 },
          { 6.8e-9, 6.8e-9 }, { 470e3, 470e3 }, { 2.2e3, 1.5e3 }, { 0.68e-6, 0 }, 0.0, 150e3, { 270e3, 270e3 }, 5800.0, 1.0, 1, -0.3, 0.95 },
        // 4: ULTRA  - SW1 22uF + two cascaded stages (SLO-100 / 5150 / Kraken-style)
        { 1.0, 0.68e-6, 2, -72.0, 6.4, 55.0, 130.0, 0.5,
          { 0.26, 0.19, 0.19 }, { 0.25, 0.2, 0.1 }, { 2200, 2200, 2200 }, { 7500, 6000, 5500 },
          { 6.8e-9, 6.8e-9 }, { 470e3, 470e3 }, { 2.7e3, 3.9e3 }, { 0.68e-6, 0.22e-6 }, 40e3, 200e3, { 330e3, 470e3 }, 5200.0, 1.0, 1, 0.1, 0.95 } };
    return T[std::min(std::max(m, 0), 3)];
}

void Amp001Model::prepare(double internalRate)
{
    sr = internalRate;

    TriodeStage::Config c;                      // V1a  (R1 68k, R2 1M, R3 100k, R4 1.5k, C1 by SW1, C2 22n)
    c.Ra = 100e3; c.Rk = 1.5e3; c.Ck = CK_MODE[0]; c.Rs = 68e3; c.Cin = 100e-9; c.Rg = 1e6;
    c.Cout = 100e-9; c.Rload = 1e6; c.Vcc = 300.0;
    v1a.prepare(sr, c);

    c = TriodeStage::Config();                  // V1b  (R5 10k, R6 100k, R7 1.5k, C3 22n)
    c.Ra = 100e3; c.Rk = 1.5e3; c.Ck = 0.0; c.Rs = R_STOP_V1B + R_OUT_PLATE; c.Cin = 22e-9; c.Rg = 1e6;
    c.Cout = 100e-9; c.Rload = 1e6; c.Vcc = 300.0;
    v1b.prepare(sr, c);

    c = TriodeStage::Config();                  // V2a  (R8 68k, R9 100k, R10 820R, C4 47n -> EQ)
    c.Ra = 100e3; c.Rk = 820.0; c.Ck = 0.0; c.Rs = 68e3 + R_OUT_PLATE; c.Cin = 22e-9; c.Rg = 1e6;
    c.Cout = 47e-9; c.Rload = 1e6; c.Vcc = 300.0;
    v2a.prepare(sr, c);

    c = TriodeStage::Config();                  // X1 / X2 (voicing stages: 100k plate load, 68k stopper)
    c.Ra = 100e3; c.Rk = 1.5e3; c.Ck = 0.0; c.Rs = 68e3 + 20e3; c.Cin = 4.7e-9; c.Rg = 470e3;
    c.Cout = 100e-9; c.Rload = 1e6; c.Vcc = 300.0;
    xBase = c;                                  // per-mode values are applied in setMode()

    LTPStage::Config l;                         // V2b PI  (R11 100k, R12 56k tail, 100k loads)
    l.Ra = 100e3; l.Rtail = 56e3; l.Vee = -60.0; l.Rs = 1.5e3; l.Cin = 22e-9; l.Rg = 100e3;
    l.Cout = 22e-9; l.Rload = 470e3; l.Vcc = 300.0; l.tube = TUBE_12AX7;
    pi.prepare(sr, l);

    tmb.prepare(sr);
    tmb.setControls(0.5, 0.5, 0.5);
    eq.prepare(sr);
    presence.prepare(sr);
    power.prepare(sr);
    power.setSagScale(1.0);                     // GZ34 tube rectifier
    verb.prepare(sr, AmpReverb::Type::Room);
    aBleed = 1.0 - std::exp(-2.0 * kPi * 1800.0 / sr);
    setPowerTube(0);
    setMode(0);
    reset();
}

void Amp001Model::reset()
{
    v1a.reset(); v1b.reset(); v2a.reset(); x1.reset(); x2.reset(); pi.reset();
    tmb.reset(); eq.reset(); presence.reset(); power.reset(); verb.reset();
    for (auto& i : inter) i.reset();
    bleedHp = 0.0; tightState = 0.0; fizzState = fizzState2 = 0.0;
}

void Amp001Model::setMode(int m)
{
    modeIdx = std::min(std::max(m, 0), 3);
    mc = cfg(modeIdx);
    {   TMBToneStack::Values tv = TMBToneStack::marshall();
        if (mc.stack == 0) { tv.R_MID = 50e3; tv.R_SLOPE = 47e3; tv.C_BASS = 47e-9; tv.R_BASS = 500e3; }   // warmer, fuller mids for the clean mode
        tmb.setValues(tv); }
    v1a.setCathodeBypass(CK_MODE[modeIdx]);
    v1b.setCathodeBypass(mc.ckV1b);
    v2a.setSourceResistance(mc.rsV2a);
    {   // X1 / X2 are re-biased for the selected mode (cathode resistor, coupling cap, grid leak, bypass)
        TriodeStage::Config xc = xBase;
        xc.Rs = mc.rsX[0]; xc.Rk = mc.rk[0]; xc.Cin = mc.cin[0]; xc.Rg = mc.rg[0]; xc.Ck = mc.ck[0]; x1.prepare(sr, xc);
        xc.Rs = mc.rsX[1]; xc.Rk = mc.rk[1]; xc.Cin = mc.cin[1]; xc.Rg = mc.rg[1]; xc.Ck = mc.ck[1]; x2.prepare(sr, xc);
    }
    for (int i = 0; i < 3; ++i) inter[i].set(sr, mc.att[i], mc.peak[i], mc.peakFc[i], mc.lp[i]);
    aFizz = 1.0 - std::exp(-2.0 * kPi * mc.fizzLp / sr);
    setGain(gainKnob);                                  // each SW1 position has its own taper and make-up table
}

void Amp001Model::setGain(double k)
{
    // GAIN pot: dB-linear taper, floor and slope per mode; the knob law (gA, gB) spreads the useful drive range over the whole sweep.  Below ~0.5 the pot fades to zero (true mute at 0).
    k = std::min(std::max(k, 0.0), 10.0);
    gainKnob = k;
    const double e = std::min(std::max(mc.gA + mc.gB * k, 0.0), 10.0);          // the pot position the circuit sees (see ModeCfg::gA)
    gainTaper = dbToGain(mc.floorDb + mc.stepDb * e) * std::min(1.0, k / 0.5);
    gainMech  = e / 10.0;
    // wiper source impedance (500k pot: Rout = R * t * (1-t)) feeds V1b's grid: limits grid current
    v1b.setSourceResistance(R_STOP_V1B + R_OUT_PLATE + mc.rsV1b + 500e3 * gainMech * (1.0 - gainMech));
    gainComp = levelComp(k);
    const double fcTight = mc.tightLo + (mc.tightHi - mc.tightLo) * gainMech;   // bass tightening ahead of the clipping stages
    tightA = 1.0 - std::exp(-2.0 * kPi * fcTight / sr);
}

double Amp001Model::levelComp(double k) const noexcept
{
    const double x = std::min(std::max(k, 0.0), 10.0);
    const int i = std::min((int) x, 9);
    const double f = x - i;
    const double* T = calib::a1Comp[modeIdx];
    return dbToGain(T[i] * (1.0 - f) + T[i + 1] * f);
}
void Amp001Model::setCharacter(double k)    { bleed = std::min(std::max(k, 0.0), 10.0) / 10.0; }
void Amp001Model::setBass(double k)         { bass = std::min(std::max(k, 0.0), 10.0) / 10.0;   tmb.setControls(bass, mid, treble); }
void Amp001Model::setMid(double k)          { mid = std::min(std::max(k, 0.0), 10.0) / 10.0;    tmb.setControls(bass, mid, treble); }
void Amp001Model::setTreble(double k)       { treble = std::min(std::max(k, 0.0), 10.0) / 10.0; tmb.setControls(bass, mid, treble); }
void Amp001Model::setEQBand(int b, double n){ eq.setBandGain(b, n); }
void Amp001Model::setPresence(double k)     { presence.setPresence(std::min(std::max(k, 0.0), 10.0) / 10.0); }
void Amp001Model::setMaster(double k)       { masterTaper = audioTaper(k, 2.5); masterOut = masterOutComp(k, 2.5); }
void Amp001Model::setReverb(double k)       { const double x = std::min(std::max(k, 0.0), 10.0) / 10.0; reverbMix = 1.0 * std::pow(x, 1.1); }

void Amp001Model::setPowerTube(int t)
{
    switch (std::min(std::max(t, 0), 2)) {
        case 0: power.setSpec(PushPullPowerStage::sixL6GC()); break;
        case 1: power.setSpec(PushPullPowerStage::el34());    break;
        default: power.setSpec(PushPullPowerStage::kt88());   break;
    }
}

double Amp001Model::process(double vin) noexcept
{
    double x = v1a.process(vin * mc.pad);                      // V1a
    TAP(0, x);

    bleedHp += aBleed * (x - bleedHp);                         // gain pot with bright bleed (CHARACTER)
    const double hp = x - bleedHp;
    tightState += tightA * (x - tightState);                    // tighten: remove low end before V1b
    x -= tightState * mc.tightDepth;
    x = gainTaper * x + bleed * 0.5 * std::sqrt(gainTaper) * (1.0 - gainTaper) * hp;   // bright cap across the pot

    x = v1b.process(guard(x, 400.0));                          // V1b
    TAP(1, x);
    (void) fxLoop;                                             // series loop = unity pass-through
    x = v2a.process(guard(inter[0].process(x), 400.0));        // V2a
    TAP(2, x);
    if (mc.nExtra >= 1) x = x1.process(guard(inter[1].process(x), 400.0));   // X1
    if (mc.nExtra >= 2) x = x2.process(guard(inter[2].process(x), 400.0));   // X2
    TAP(3, x);

    fizzState += aFizz * (x - fizzState);                      // stray capacitance ahead of the tone stack (two poles)
    fizzState2 += aFizz * (fizzState - fizzState2);
    x = tmb.process(fizzState2);                                // Bass / Mid / Treble
    x = eq.process(x);                                         // PCB-B EQ
    TAP(4, x);
    x = presence.process(x * EQ_BOARD_LOSS * masterTaper * gainComp * mc.tmbLoss);   // board loss, MASTER, presence

    double a, b;
    pi.process(guard(x, 400.0), a, b);                         // V2b long-tail pair
    double y = power.process(a - b) * masterOut;                // power stage + OT, then the MASTER loudness law
    TAP(5, y);

    y += reverbMix * verb.process(y);                          // V300 reverb on the output
    return guard(y, 4.0);
}
