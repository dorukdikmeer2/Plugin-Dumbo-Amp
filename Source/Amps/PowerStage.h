#pragma once
#include <cmath>
#include <algorithm>

//==============================================================================
// Push-pull class-AB power stage + output transformer (behavioural model).
//
// Each output tube is a 3/2-power-law device with a soft cut-off, biased
// 'bias' volts from cut-off.  The two halves are subtracted (push-pull), then
// the plate swing is limited by the supply (with rectifier sag) and the OT
// adds band-limiting and core saturation.  This is circuit-informed, NOT a
// full circuit solve — see the build notes.
//
// Input : differential grid drive in volts (Va - Vb from the phase inverter)
// Output: normalised speaker signal, ~ +/-1 at full power
//==============================================================================
class PushPullPowerStage {
public:
    struct Spec {
        double vBias;       // |grid bias| in volts = reference drive
        double cutoff;      // cut-off point in units of vBias (idle current ~ cutoff^expo)
        double expo;        // device exponent
        double ssat;        // supply-limited output swing (normalised units)
        double sag;         // rectifier sag depth 0..1
        double sagRelease;  // seconds
    };

    static Spec sixL6GC() { return { 48.0, 1.40, 1.40, 1.20, 0.18, 0.12 }; }
    static Spec el34()    { return { 50.0, 1.45, 1.50, 1.25, 0.15, 0.10 }; }
    static Spec kt88()    { return { 62.0, 1.50, 1.50, 1.35, 0.10, 0.09 }; }

    void prepare(double sampleRate);
    void reset();
    void setSpec(const Spec& s);
    void setSagScale(double s) { sagScale = s; }
    double process(double vdiff) noexcept;

private:
    static inline double softplus(double z, double k) noexcept {
        const double u = z / k;
        if (u > 30.0) return z;
        if (u < -30.0) return 0.0;
        return k * std::log1p(std::exp(u));
    }

    Spec sp = el34();
    double sr = 48000.0;
    double slope0 = 1.0, env = 0.0, aAtt = 0.0, aRel = 0.0, sagScale = 1.0;
    double otHp = 0.0, otLp = 0.0, aHp = 0.0, aLp = 0.0;
};
