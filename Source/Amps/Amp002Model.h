#pragma once
#include "Amps/TubeStage.h"
#include "Amps/ToneStack.h"
#include "Amps/PowerStage.h"
#include "Amps/Reverb.h"
#include "Common/DspUtil.h"

//==============================================================================
// AMP-002 "Dock"
//
// Signal chain from the schematic (PCB-A / PCB-B / PCB-C):
//   J1 (68k, 1M) -> V1a 12AX7 (100k / 1.5k / 22uF cathode cap)
//        RLY1 channel relay:  A (NC) = cap out,  B (NO) = 22uF in
//   -> GAIN -> passive bass/mid/treble stack (250k / 10k / 250k, 47n / 22n)
//   -> R_vol 500k MASTER -> V1b 12AX7 (100k / 820R, 10k stopper)
//   -> spring reverb tank mixed here (CC11) -> series FX loop (RLY2, CC64)
//   -> 12AU7 long-tail-pair PI (82k loads, 56k tail, HT 350 V)
//   -> EL34 push-pull, fixed bias, ultralinear, GZ34 (50 W) with PRESENCE (NFB) -> OT
//   Heater select (10 V / 20 V): the 10 V setting starves the cathodes.  Standby (CC80) mutes.
//
// 2026-10 rebuild: the two relay channels are voiced as two different amplifiers (voicing values are NOT on
// the schematic, see BUILD_INSTRUCTIONS.md):
//   Channel A "Pristine" : Laney VH100R / Roland JC-120 study - input pad, low-gain unbypassed V1a, high-voltage
//                          power section (huge headroom, almost no sag), tight bass, glassy top.
//   Channel B "Chime"    : AC30 top-boost / edge-of-breakup study - bright, bypassed V1a, brit stack, hot-biased
//                          sagging power amp; GAIN walks from clean through chime to a hairy edge-of-breakup.
//==============================================================================
class Amp002Model {
public:
    void prepare(double internalRate);
    void reset();

    void setChannel(int ch0or1);
    void setGain(double knob0to10);
    void setBass(double knob0to10);
    void setMid(double knob0to10);
    void setTreble(double knob0to10);
    void setMaster(double knob0to10);
    void setPresence(double knob0to10);
    void setReverb(double knob0to10);
    void setFXLoop(bool on) { fxLoop = on; }
    void setHeater(int hv0or1);                // 0 = 10 V (starved), 1 = 20 V

    double process(double vin) noexcept;

    struct ChannelCfg {
        double pad;              // input divider ahead of V1a
        double ckV1a, ckV1b;     // cathode bypass caps (V1a: relay RLY1)
        double floorDb, stepDb;  // GAIN pot law
        double tightHz;          // pre-V1b low-cut
        double bleed;            // bright bleed across the GAIN pot (0..1)
        double fizzHz;           // stray-capacitance low-pass before the PI (two poles)
        double rsV1b;            // grid stopper of V1b
        TMBToneStack::Values tone;
        PushPullPowerStage::Spec power;
        double sagScale;
        double reverbGain;       // spring send level
        double stackMakeup;      // linear make-up after the tone stack (recovers the stack loss; drives V1b)
        double gA, gB;           // GAIN knob law: pot position seen by the circuit = gA + gB * knob (0 / 1 = plain)
    };

private:
    double levelComp(double knob) const noexcept;
    static const ChannelCfg& cfg(int ch) noexcept;

    ChannelCfg cc {};
    double gainComp = 1.0, gainKnob = 5.0;
    bool channelB = false;
    double sr = 96000.0;
    TriodeStage v1a, v1b;
    LTPStage    pi;
    TMBToneStack stack;
    PresenceEQ  presence;
    PushPullPowerStage power;
    AmpReverb      verb;

    double gainTaper = 0.2, masterTaper = 0.2, masterPre = 0.5, masterPost = 0.4, masterOut = 1.0, reverbMix = 0.0;
    bool fxLoop = false;
    double bass = 0.5, mid = 0.5, treble = 0.5;
    double tightA = 0.0, tightState = 0.0, bleedHp = 0.0, aBleed = 0.0, fizz1 = 0.0, fizz2 = 0.0, aFizz = 0.0;
};
