#pragma once
#include <cmath>
#include <algorithm>

namespace engine {

//==============================================================================
// NoiseGate — a smooth downward expander that sits at the amp input (guitar
// DI).  It is deliberately *not* a hard gate: the gain falls gradually as the
// note decays below the threshold (ratio 1:3, 6 dB soft knee, -55 dB floor),
// with a hold time and a slow release, so sustained high-gain notes die away
// naturally instead of chopping off.
//
//   THRESHOLD knob 0 .. 10   0 = gate off (bit-exact bypass)
//                            1 = -76 dBFS ... 10 = -34 dBFS (input level)
//==============================================================================
class NoiseGate {
public:
    void prepare(double sampleRate)
    {
        sr = sampleRate;
        aAtt = 1.0 - std::exp(-1.0 / (0.0004 * sr));       // envelope attack  0.4 ms
        aRel = 1.0 - std::exp(-1.0 / (0.030 * sr));        // envelope release 30 ms
        aOpen  = 1.0 - std::exp(-1.0 / (0.0010 * sr));     // gain opening     1 ms
        aClose = 1.0 - std::exp(-1.0 / (0.090 * sr));      // gain closing     90 ms
        holdSamples = (int) (0.045 * sr);
        reset();
    }

    void reset() { env = 0.0; gain = 1.0; target = 1.0; hold = 0; tick = 0; }

    void setThresholdKnob(double knob0to10)
    {
        knob = std::min(std::max(knob0to10, 0.0), 10.0);
        enabled = knob > 0.02;
        thrDb = -76.0 + (knob - 1.0) * (42.0 / 9.0);       // knob 1 -> -76 dBFS, 10 -> -34 dBFS
    }

    bool isEnabled() const { return enabled; }
    double currentGain() const { return gain; }
    double currentGainDb() const { return 20.0 * std::log10(std::max(gain, 1e-6)); }

    // x: the DI sample in full-scale units (before the jack-voltage scaling).  Returns the gain to apply.
    inline double process(double x) noexcept
    {
        if (!enabled) { gain = 1.0; target = 1.0; return 1.0; }
        const double a = std::fabs(x);
        env += (a > env ? aAtt : aRel) * (a - env);

        if ((tick++ & 15) == 0) {                          // expander curve every 16 samples
            const double lvl = 20.0 * std::log10(env + 1e-9);
            const double closeDb = thrDb - 3.0;
            if (lvl > closeDb) hold = holdSamples;
            else if (hold > 0) hold -= 16;
            if (lvl >= thrDb + 3.0 || hold > 0) {
                target = 1.0;
            } else {
                const double below = (thrDb - lvl);        // dB below the threshold
                double grDb;                               // soft knee: quadratic over +-3 dB
                if (below <= -3.0) grDb = 0.0;
                else if (below < 3.0) { const double t = (below + 3.0) / 6.0; grDb = 2.0 * (t * t) * 3.0; }   // 0 .. 6 dB over the knee
                else grDb = 6.0 + 2.0 * (below - 3.0);     // ratio 1:3 -> 2 dB extra per dB below
                grDb = std::min(grDb, 55.0);
                target = std::pow(10.0, -grDb / 20.0);
            }
        }
        gain += (target > gain ? aOpen : aClose) * (target - gain);
        return gain;
    }

private:
    double sr = 48000.0, aAtt = 0, aRel = 0, aOpen = 0, aClose = 0;
    double knob = 0.0, thrDb = -76.0, env = 0.0, gain = 1.0, target = 1.0;
    int holdSamples = 2000, hold = 0, tick = 0;
    bool enabled = false;
};

} // namespace engine
