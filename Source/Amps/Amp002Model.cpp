#include "Amp002Model.h"
#include "Amps/Calibration.h"
using namespace dsp_util;

//------------------------------------------------------------------------------
// Channel voicing table
//------------------------------------------------------------------------------
static TMBToneStack::Values fenderish()  // classic scooped clean stack: deep mid notch, full bass, glassy top
{
    TMBToneStack::Values v; v.R_SLOPE = 100e3; v.C_BASS = 100e-9; v.C_MID = 47e-9; v.R_MID = 10e3; return v;
}

static TMBToneStack::Values brit()      // Vox-style top-boost stack: small treble cap, 1 M pots, forward mids
{
    TMBToneStack::Values v; v.R_TREBLE = 1e6; v.R_BASS = 1e6; v.R_MID = 25e3; v.R_SLOPE = 56e3;
    v.C_TREBLE = 120e-12; v.C_BASS = 22e-9; v.C_MID = 22e-9; return v;
}

const Amp002Model::ChannelCfg& Amp002Model::cfg(int ch) noexcept
{
    static const ChannelCfg T[2] = {
        // A  "Pristine": padded input, unbypassed V1a, JC/Laney-style glassy stack, 100 W-class power section
        { 0.30, 0.0, 0.0, -44.0, 3.4, 45.0, 0.25, 14000.0, 10e3,
          fenderish(),
          { 52.0, 1.40, 1.50, 1.75, 0.04, 0.08 }, 0.4, 1.0, 1.0, 0.0, 1.0 },
        // B  "Chime": bright, bypassed V1a, brit-style stack, sagging hot-biased power amp
        { 1.00, 22e-6, 2.2e-6, -44.0, 4.4, 110.0, 0.0, 9000.0, 10e3,
          brit(),
          { 48.0, 1.90, 1.50, 1.12, 0.30, 0.15 }, 1.0, 1.0, 1.5, 5.6, 0.37 } };   // +3.5 dB make-up: more drive into V1b (edge-of-breakup)
    return T[ch ? 1 : 0];
}

void Amp002Model::prepare(double internalRate)
{
    sr = internalRate;

    TriodeStage::Config c;                      // V1a  (R1 68k, R2 1M, R3 100k, R4 1.5k, C1 22uF, C2 22n)
    c.Ra = 100e3; c.Rk = 1.5e3; c.Ck = 0.0; c.Rs = 68e3; c.Cin = 100e-9; c.Rg = 1e6;
    c.Cout = 100e-9; c.Rload = 1e6; c.Vcc = 260.0;               // +B2 250-280 V preamp rail
    v1a.prepare(sr, c);

    c = TriodeStage::Config();                  // V1b  (R5 10k, R6 100k, R7 820R, C3 22n)
    c.Ra = 100e3; c.Rk = 820.0; c.Ck = 0.0; c.Rs = 10e3; c.Cin = 22e-9; c.Rg = 1e6;
    c.Cout = 22e-9; c.Rload = 470e3; c.Vcc = 260.0;
    v1b.prepare(sr, c);

    LTPStage::Config l;                         // 12AU7 PI  (82k loads, 56k tail, 1.5k stoppers)
    l.Ra = 82e3; l.Rtail = 56e3; l.Vee = -60.0; l.Rs = 1.5e3; l.Cin = 22e-9; l.Rg = 1e6;
    l.Cout = 22e-9; l.Rload = 470e3; l.Vcc = 350.0; l.tube = TUBE_12AU7;
    pi.prepare(sr, l);

    stack.prepare(sr);
    presence.prepare(sr);
    power.prepare(sr);
    verb.prepare(sr, AmpReverb::Type::Spring);
    aBleed = 1.0 - std::exp(-2.0 * kPi * 1800.0 / sr);
    setChannel(0);
    reset();
}

void Amp002Model::reset()
{
    v1a.reset(); v1b.reset(); pi.reset();
    stack.reset(); presence.reset(); power.reset(); verb.reset();
    tightState = bleedHp = fizz1 = fizz2 = 0.0;
}

void Amp002Model::setChannel(int ch)
{
    channelB = ch != 0;
    cc = cfg(ch);
    v1a.setCathodeBypass(cc.ckV1a);
    v1b.setCathodeBypass(cc.ckV1b);
    v1b.setSourceResistance(cc.rsV1b > 0.0 ? cc.rsV1b : 10e3);
    stack.setValues(cc.tone);
    power.setSpec(cc.power);
    power.setSagScale(cc.sagScale);
    aFizz = 1.0 - std::exp(-2.0 * kPi * cc.fizzHz / sr);
    tightA = 1.0 - std::exp(-2.0 * kPi * cc.tightHz / sr);
    setGain(gainKnob);                                      // each channel has its own drive range and make-up table
}
void Amp002Model::setGain(double k)
{
    // GAIN pot: dB-linear sweep with a per-channel range and level make-up (tests/calibrate.cpp): the knob raises
    // level and drive together, the way the pot does on the amp.
    k = std::min(std::max(k, 0.0), 10.0);
    gainKnob = k;
    const double e = std::min(std::max(cc.gA + cc.gB * k, 0.0), 10.0);          // pot position seen by the circuit (see ChannelCfg::gA)
    gainTaper = dbToGain(cc.floorDb + cc.stepDb * e) * std::min(1.0, k / 0.5);
    gainComp = levelComp(k);
}

double Amp002Model::levelComp(double k) const noexcept
{
    const double x = std::min(std::max(k, 0.0), 10.0);
    const int i = std::min((int) x, 9);
    const double f = x - i;
    const double* T = channelB ? calib::a2CompB : calib::a2CompA;
    return dbToGain(T[i] * (1.0 - f) + T[i + 1] * f);
}
void Amp002Model::setBass(double k)    { bass = std::min(std::max(k, 0.0), 10.0) / 10.0;   stack.setControls(bass, mid, treble); }
void Amp002Model::setMid(double k)     { mid = std::min(std::max(k, 0.0), 10.0) / 10.0;    stack.setControls(bass, mid, treble); }
void Amp002Model::setTreble(double k)  { treble = std::min(std::max(k, 0.0), 10.0) / 10.0; stack.setControls(bass, mid, treble); }
void Amp002Model::setMaster(double k)
{
    // MASTER pot (R_vol) sits ahead of V1b on the schematic.  So that GAIN alone decides how hard V1b is
    // driven (and MASTER decides loudness), the same attenuation is split: most of it stays ahead of V1b
    // as drawn, the rest is applied right after it.  The product is the schematic's pot taper.
    masterTaper = audioTaper(k, 2.5);
    masterPre   = std::pow(masterTaper, 0.3);
    masterPost  = (masterPre > 1e-9) ? masterTaper / masterPre : 0.0;
    masterOut   = masterOutComp(k, 2.5);
}
void Amp002Model::setPresence(double k){ presence.setPresence(std::min(std::max(k, 0.0), 10.0) / 10.0); }
void Amp002Model::setReverb(double k)  { const double x = std::min(std::max(k, 0.0), 10.0) / 10.0; reverbMix = 1.0 * std::pow(x, 1.1); }

void Amp002Model::setHeater(int hv)
{
    const double e = hv ? 1.0 : 0.70;           // starved heaters -> lower emission
    v1a.setEmission(e); v1b.setEmission(e); pi.setEmission(e);
}

double Amp002Model::process(double vin) noexcept
{
    double x = v1a.process(vin * cc.pad);                      // V1a (+ relay channel)

    bleedHp += aBleed * (x - bleedHp);                         // bright bleed across the GAIN pot
    const double hp = x - bleedHp;
    x = gainTaper * x + cc.bleed * 0.6 * std::sqrt(gainTaper) * (1.0 - gainTaper) * hp;   // GAIN

    x = stack.process(x) * cc.stackMakeup;                     // passive tone stack (+ make-up)
    tightState += tightA * (x - tightState);                   // coupling-cap low-cut ahead of V1b
    x -= tightState;
    x *= masterPre;                                            // R_vol (MASTER), ahead of V1b
    x = v1b.process(guard(x, 400.0)) * (masterPost * gainComp); // V1b recovery stage, rest of MASTER, level make-up

    x += reverbMix * cc.reverbGain * verb.process(x);          // spring tank (CC11)
    (void) fxLoop;                                             // series loop = unity pass-through
    fizz1 += aFizz * (x - fizz1);                              // stray capacitance ahead of the PI
    fizz2 += aFizz * (fizz1 - fizz2);
    x = presence.process(fizz2);                               // presence (NFB depth)

    double a, b;
    pi.process(guard(x, 400.0), a, b);                         // 12AU7 LTP
    const double y = power.process(a - b) * masterOut;         // EL34 + OT, then the MASTER loudness law
    return guard(y, 4.0);
}
