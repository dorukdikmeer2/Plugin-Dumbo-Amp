#pragma once
#include "Cab/Fft.h"
#include "Cab/MicModels.h"
#include <vector>

namespace cab {

//==============================================================================
// CabIrDesigner — turns "this speaker cabinet, this microphone, this position,
// this distance" into a minimum-phase FIR impulse response, entirely from a
// small physical model (no stored IR data):
//
//   H(f) = Speaker(f) x Directivity(f, theta) x Mic(f, theta, r) + room reflections, then delayed
//
//   Speaker      the cabinet voicing (4x12 closed back / 1x12 open back) plus a
//                dust-cap resonance that only a mic near the centre hears
//   Directivity  a piston radiator whose radiating radius shrinks with
//                frequency (the whole cone at low f, the dust cap at high f).
//                A mic off the centre therefore loses highs first
//   Mic          the model's far-field curve x its polar pattern (cardioid /
//                figure-8) x its off-axis high-frequency loss x the near-field
//                proximity boost  |a + (1-a) cos(t) (1 - j/(k r))|
//                (an extended source, so the effective r never gets below ~6 cm)
//   Room         four early reflections (floor, ceiling, side wall, front
//                wall); their level relative to the direct sound grows with the
//                mic distance, so close mics are dry and far mics are roomy
//   Delay        the arrival time of the mic relative to the nearest mic
//                (so two mics sum with a physically correct phase relationship)
//
// design() is not real-time free (it runs a few FFTs) but never allocates; the
// caller rate-limits it while knobs move.
//==============================================================================
struct MicGeometry {
    double axial;      // distance from the baffle to the diaphragm along the axis (m)
    double offset;     // lateral offset of the mic axis from the cone centre (m)
};

class CabIrDesigner {
public:
    static constexpr int kMicTypeCount = 5;
    void prepare(double sampleRate, int irLength);

    struct Request {
        int    cabinet = 0;            // 0 = 4x12 closed back, 1 = 1x12 open back
        int    micType = 0;
        double position = 0.3, distance = 0.25;
        double delaySamples = 0.0;     // arrival delay relative to the earliest mic
    };

    // writes irLength() taps
    void design(const Request& r, float* ir);

    int irLength() const { return len; }

    static MicGeometry geometry(int micType, double position01, double distance01);
    static constexpr double SPEED_OF_SOUND = 343.0;

private:
    double speakerDb(int cabinet, double f) const;
    void buildTables();

    double sr = 48000.0;
    int len = 1024, N = 4096, bins = 2049;
    Fft fft;
    std::vector<double> reA, imA, reB, imB;
    std::vector<double> freq, radius, kWave, hfExp;                 // per bin
    std::vector<double> spk[2];                                      // speaker response (dB) per cabinet
    std::vector<double> mic[kMicTypeCount];                          // far-field mic curve (dB) per type
};

} // namespace cab
