#pragma once
#include "Amps/TubeStage.h"
#include "Amps/ToneStack.h"
#include "Amps/PowerStage.h"
#include "Amps/Reverb.h"
#include "Common/DspUtil.h"

//==============================================================================
// AMP-001 "Dumb 1"
//
// Signal chain from the schematic (PCB-A / PCB-B / PCB-C):
//   J1 (68k stopper, 1M leak) -> V1a 12AX7 (100k / 1.5k, cathode cap by SW1: open / 1uF / 4.7uF / 22uF)
//   -> GAIN pot (+ bright bleed = CHARACTER) -> V1b 12AX7 (100k / 1.5k, 10k stopper)
//   -> series FX loop (SW2) -> V2a 12AX7 (100k / 820R, 68k stopper)
//   -> PCB-B six-band EQ (+/-15 dB) -> MASTER -> presence
//   -> V2b 12AX7 long-tail-pair PI -> push-pull 6L6GC / EL34 / KT88 -> OT -> reverb
//
// Voicing additions for the 2026-10 rebuild (NOT on the schematic, disclosed in the docs):
//   * a passive Bass / Mid / Treble stack between V2a and the six-band EQ
//   * mode 3 ("Lead") adds one cascaded gain stage X1 after V2a, mode 4 ("Ultra") adds two (X1, X2),
//     each behind a 2-resistor attenuator with a treble-peaker cap and its own coupling cap / grid leak,
//     in the manner of the SLO-100 / 5150 / Mark-series preamps (see BUILD_INSTRUCTIONS.md)
//   * V1b gets a small cathode bypass in the high-gain modes (mid-focus), pot-dependent tightening
//
// All voltages are real volts; the model runs at the (oversampled) internal rate.
//==============================================================================
class Amp001Model {
public:
    void prepare(double internalRate);
    void reset();

    void setMode(int mode0to3);                // SW1: 0..3
    void setGain(double knob0to10);
    void setCharacter(double knob0to10);
    void setBass(double knob0to10);
    void setMid(double knob0to10);
    void setTreble(double knob0to10);
    void setEQBand(int band, double norm);     // -1..+1
    void setPresence(double knob0to10);
    void setMaster(double knob0to10);
    void setReverb(double knob0to10);
    void setFXLoop(bool on) { fxLoop = on; }
    void setPowerTube(int t0to2);              // 6L6GC / EL34 / KT88

    // vin: volts at the guitar jack.  Returns the normalised power-amp output.
    double process(double vin) noexcept;

    struct ModeCfg {
        double pad;            // input divider ahead of V1a
        double ckV1b;          // V1b cathode bypass (0 = none as on the schematic)
        int    nExtra;         // extra cascaded gain stages after V2a (0..2)
        double floorDb;        // GAIN pot attenuation at knob 0 / dB per knob step
        double stepDb;
        double tightLo, tightHi;   // pre-V1b low-cut at GAIN 0 / GAIN 10 (Hz)
        double tightDepth;         // how much of the low end the low-cut removes (0..1)
        double att[3];         // attenuator ahead of V2a, X1, X2
        double peak[3];        // treble-peaker amount (0..1)
        double peakFc[3];
        double lp[3];          // interstage low-pass (Miller + stray), Hz
        double cin[2];         // X1 / X2 coupling caps
        double rg[2];          // X1 / X2 grid leaks
        double rk[2];          // X1 / X2 cathode resistors
        double ck[2];          // X1 / X2 cathode bypass
        double rsV1b;          // extra grid stopper in series with V1b (adds to R5 + the pot)
        double rsV2a;          // total grid-stopper resistance of V2a
        double rsX[2];         // grid stoppers of X1 / X2 (the SLO-style 'big stopper' = smooth, blocking-free clipping)
        double fizzLp;         // low-pass ahead of the tone stack
        double tmbLoss;        // linear make-up for the tone stack / board insertion loss
        int    stack;          // 0 = warm, 1 = marshall-style tone stack values
        double gA, gB;         // GAIN knob law: the pot position the circuit sees is  gA + gB * knob  (0..10); 0 / 1 = plain
    };

private:
    double levelComp(double knob) const noexcept;
    static constexpr double CK_MODE[4] = { 0.0, 1e-6, 4.7e-6, 22e-6 };
    static constexpr double EQ_BOARD_LOSS = 0.1;   // 10k Rin / six-band summing network, ~ -20 dB

    static const ModeCfg& cfg(int mode) noexcept;

    double sr = 96000.0;
    TriodeStage v1a, v1b, v2a, x1, x2;
    TriodeStage::Config xBase {};
    LTPStage    pi;
    TMBToneStack tmb;
    SixBandEQ   eq;
    PresenceEQ  presence;
    PushPullPowerStage power;
    AmpReverb      verb;

    static constexpr double R_STOP_V1B = 10e3;       // R5 on the schematic
    static constexpr double R_OUT_PLATE = 40e3;      // output resistance of the previous 12AX7 plate
    int modeIdx = 0;
    ModeCfg mc {};
    double gainKnob = 5.0;
    double gainTaper = 0.2, gainMech = 0.3, gainComp = 1.0, tightA = 0.0, tightState = 0.0;
    double bleed = 0.0, masterTaper = 0.2, masterOut = 1.0, reverbMix = 0.0;
    bool fxLoop = false;
    double bleedHp = 0.0, aBleed = 0.0;

    // interstage helper: attenuator + treble peaker + low-pass
    struct Inter {
        double g = 1.0, pk = 0.0, aPk = 0.0, aLp = 0.0, lpPk = 0.0, lpS = 0.0;
        void set(double sr, double gain, double peak, double peakFc, double lpFc) {
            g = gain; pk = peak;
            aPk = 1.0 - std::exp(-2.0 * dsp_util::kPi * peakFc / sr);
            aLp = 1.0 - std::exp(-2.0 * dsp_util::kPi * lpFc / sr);
        }
        void reset() { lpPk = lpS = 0.0; }
        inline double process(double x) noexcept {
            lpPk += aPk * (x - lpPk);
            const double y = g * x + pk * (1.0 - g) * (x - lpPk);     // cap across the upper resistor of the divider
            lpS += aLp * (y - lpS);
            return lpS;
        }
    };
    Inter inter[3];
    double bass = 0.5, mid = 0.5, treble = 0.5;
    double fizzState = 0.0, fizzState2 = 0.0, aFizz = 0.0;
};
