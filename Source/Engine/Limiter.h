#pragma once
#include <vector>
#include <cmath>
#include <algorithm>

namespace engine {

//==============================================================================
// SafetyLimiter — a stereo-linked look-ahead peak limiter that is the very last
// stage before the host.  It replaces the old waveshaping soft clipper:
//   * below the ceiling it is bit-transparent (gain = 1, no harmonic distortion)
//   * above it, the gain is pulled down *smoothly* ahead of the peak (1 ms look
//     ahead), so a hot MASTER / CAB LEVEL / OUT TRIM setting makes the signal
//     compress instead of clip
//   * the detector looks at the INTER-SAMPLE peak too (4-point interpolation half-way between two samples), so a DAW
//     that upsamples for its meters / a converter does not see an over
//   * release is 80 ms; the output never exceeds the ceiling (a final hard
//     clamp backs the guarantee up against rounding)
//
// Latency = lookahead() samples (reported to the host).
//
// How the look-ahead works (L = look-ahead length in samples):
//   req[n]   gain this sample needs so that it fits under the ceiling
//   mn[n]    min(req[n-L .. n])                      (window of L+1 samples)
//   target   mean(mn[n-L+1 .. n])                    (box of L samples)
//   audio is delayed by L samples, so when the sample from time t leaves the
//   delay line the box only contains mn[] values whose window includes t,
//   hence target <= req[t] and the peak is already attenuated, with a smooth
//   (linear-ish) ramp over the L samples before it.
//==============================================================================
class SafetyLimiter {
public:
    static constexpr double CEILING_DB = -3.0;              // dBFS (leaves headroom for inter-sample peaks and for what you add after the plugin)

    void prepare(double sampleRate)
    {
        sr = sampleRate;
        L = std::max(8, (int) std::ceil(0.001 * sr));       // 1 ms look-ahead
        ceiling = (float) std::pow(10.0, CEILING_DB / 20.0);
        aim = ceiling * 0.90f;                               // aim 0.92 dB under the ceiling: leaves room for the DC blocker that follows
        relCoef = (float) (1.0 - std::exp(-1.0 / (0.080 * sr)));
        dlL.assign((size_t) L, 0.0f);  dlR.assign((size_t) L, 0.0f);
        reqRing.assign((size_t) L + 1, 1.0f);
        boxRing.assign((size_t) L, 1.0f);
        reset();
    }

    int lookahead() const { return L; }
    float ceilingLin() const { return ceiling; }

    void reset()
    {
        std::fill(dlL.begin(), dlL.end(), 0.0f);  std::fill(dlR.begin(), dlR.end(), 0.0f);
        std::fill(reqRing.begin(), reqRing.end(), 1.0f);
        std::fill(boxRing.begin(), boxRing.end(), 1.0f);
        hL[0] = hL[1] = hL[2] = hR[0] = hR[1] = hR[2] = 0.0f;
        pDel = pReq = pBox = 0; boxSum = (double) L; follow = 1.0f; grDb = 0.0f; maxGrDb = 0.0f;
    }

    // one stereo sample in, one (delayed by lookahead()) sample out
    inline void process(float& l, float& r) noexcept
    {
        // inter-sample peak estimate half-way between the previous two samples (cubic through 4 points)
        const float mL = (-hL[2] + 9.0f * hL[1] + 9.0f * hL[0] - l) * 0.0625f;
        const float mR = (-hR[2] + 9.0f * hR[1] + 9.0f * hR[0] - r) * 0.0625f;
        hL[2] = hL[1]; hL[1] = hL[0]; hL[0] = l;  hR[2] = hR[1]; hR[1] = hR[0]; hR[0] = r;
        const float pk = std::max(std::max(std::fabs(l), std::fabs(r)), std::max(std::fabs(mL), std::fabs(mR)));
        reqRing[pReq] = pk > aim ? aim / pk : 1.0f;
        if (++pReq == reqRing.size()) pReq = 0;
        float mn = 1.0f;
        for (float v : reqRing) mn = std::min(mn, v);                    // L ~ 48..192: small next to the amp model

        boxSum += (double) mn - (double) boxRing[pBox];
        boxRing[pBox] = mn;
        if (++pBox == boxRing.size()) {                                   // refresh the running sum once per lap (no drift)
            pBox = 0;
            boxSum = 0.0;
            for (float v : boxRing) boxSum += v;
        }
        const float target = (float) (boxSum / (double) L);

        follow = std::min(target, follow + (1.0f - follow) * relCoef);    // instant attack, 80 ms release

        const float oL = dlL[pDel], oR = dlR[pDel];
        dlL[pDel] = l; dlR[pDel] = r;
        if (++pDel == dlL.size()) pDel = 0;

        l = std::max(-ceiling, std::min(ceiling, oL * follow));
        r = std::max(-ceiling, std::min(ceiling, oR * follow));
        grDb = -20.0f * std::log10(std::max(follow, 1e-4f));
        maxGrDb = std::max(maxGrDb, grDb);
    }

    float gainReductionDb() const { return grDb; }
    float takeMaxGainReductionDb() { const float v = maxGrDb; maxGrDb = 0.0f; return v; }

private:
    double sr = 48000.0, boxSum = 0.0;
    int L = 48;
    float ceiling = 0.89f, aim = 0.86f, relCoef = 0.0f, follow = 1.0f, grDb = 0.0f, maxGrDb = 0.0f;
    float hL[3] = { 0, 0, 0 }, hR[3] = { 0, 0, 0 };
    size_t pDel = 0, pReq = 0, pBox = 0;
    std::vector<float> dlL, dlR, reqRing, boxRing;
};

} // namespace engine
