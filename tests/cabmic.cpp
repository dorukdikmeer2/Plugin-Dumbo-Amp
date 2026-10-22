// Cabinet / microphone audit (headless).  The cabinet is "really important", and the previous version's MIC POSITION /
// MIC DISTANCE had almost no audible effect - so every claim is measured here:
//
//   1  impulse-response sanity    finite, decays inside the window, every setting normalised to the same level
//   2  MIC POSITION               centre -> edge must change the tone a lot (every mic, both cabs)
//   3  MIC DISTANCE               proximity effect, room reflections, comb filtering
//   4  the five microphones       distinct, and each one has its documented character
//   5  mic pairing                delay, polarity, pan, level normalisation, per-cab settings
//   6  engine integration         clicks while moving mic knobs / switching cabs, CPU cost of re-rendering IRs
#include "metrics.h"
#include "Cab/CabIrDesigner.h"
#include "Cab/CabinetSim.h"
#include <chrono>
#include <complex>
#include <cstdarg>
#include <functional>
#include <random>
#include <thread>
using namespace mx;

static int gFails = 0, gChecks = 0;
static void CHECKF(bool ok, const char* f, ...) { char b[300]; va_list a; va_start(a, f); vsnprintf(b, 300, f, a); va_end(a); ++gChecks; if (!ok) { ++gFails; printf("   FAIL  %s\n", b); } }

static const int IRLEN = 1024;
static const char* MICN[5] = { "SM57", "SM58", "Royer121", "C414", "MD421" };
static const char* CABN[2] = { "4x12", "1x12" };

static vector<float> design(cab::CabIrDesigner& d, int cabinet, int mic, double pos, double dist, double delay = 0.0) {
    cab::CabIrDesigner::Request r; r.cabinet = cabinet; r.micType = mic; r.position = pos; r.distance = dist; r.delaySamples = delay;
    vector<float> ir(IRLEN); d.design(r, ir.data()); return ir;
}
// power response at one frequency, dB
static double respDb(const vector<float>& ir, double f) {
    std::complex<double> s = 0; for (size_t n = 0; n < ir.size(); ++n) s += (double) ir[n] * std::polar(1.0, -2 * M_PI * f * n / SR);
    return 20 * log10(abs(s) + 1e-12);
}
// mean power (dB) over a band, averaged on a log frequency grid
static double bandDb(const vector<float>& ir, double lo, double hi) {
    double p = 0; int n = 0; for (double f = lo; f <= hi * 1.0001; f *= pow(hi / lo, 1.0 / 24.0)) { p += pow(10.0, respDb(ir, f) / 10.0); ++n; }
    return 10 * log10(p / n);
}
struct Bands { double b[5]; };
static const double BL[5] = { 80, 250, 1000, 3000, 6000 }, BH[5] = { 250, 1000, 3000, 6000, 12000 };
static Bands bands(const vector<float>& ir) { Bands r; for (int i = 0; i < 5; ++i) r.b[i] = bandDb(ir, BL[i], BH[i]); return r; }
static double tilt(const Bands& b) { return b.b[3] - b.b[1]; }                    // 3-6 kHz minus 250-1k
static double weightedLevel(const vector<float>& ir) {                              // same metric the designer normalises with
    double num = 0, den = 0; for (double f = 80; f <= 6000; f *= 1.01) { const double w = 1.0; num += w * pow(10.0, respDb(ir, f) / 10.0); den += w; }
    return 10 * log10(num / den);
}
// energy after `from` samples relative to the total, measured above 1 kHz so the speaker's own low-frequency
// resonance ring does not mask the room reflections (which are what this metric is about)
static double tailDb(vector<float> ir, int from) {
    { const double w = 2 * M_PI * 1000.0 / SR, c = cos(w), al = sin(w) / (2 * 0.707), a0 = 1 + al;
      const double b0 = (1 + c) / 2 / a0, b1 = -(1 + c) / a0, b2 = b0, a1 = -2 * c / a0, a2 = (1 - al) / a0; double x1 = 0, x2 = 0, y1 = 0, y2 = 0;
      for (auto& v : ir) { const double x = v, y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2; x2 = x1; x1 = x; y2 = y1; y1 = y; v = (float) y; } }
    double e = 0, t = 0; for (int i = 0; i < IRLEN; ++i) { t += (double) ir[i] * ir[i]; if (i >= from) e += (double) ir[i] * ir[i]; } return 10 * log10(e / t + 1e-12);
}

int main() {
    cab::CabIrDesigner d; d.prepare(SR, IRLEN);

    printf("== 1. impulse responses: finite, decayed, level-matched ==\n");
    {
        double minL = 1e9, maxL = -1e9, worstTail = -1e9; int bad = 0, count = 0;
        for (int c = 0; c < 2; ++c) for (int m = 0; m < 5; ++m) for (double p : { 0.0, 0.3, 0.6, 1.0 }) for (double dist : { 0.0, 0.25, 0.5, 0.75, 1.0 }) {
            auto ir = design(d, c, m, p, dist); ++count;
            for (float v : ir) if (!std::isfinite(v)) ++bad;
            const double L = weightedLevel(ir); minL = min(minL, L); maxL = max(maxL, L);
            worstTail = max(worstTail, tailDb(ir, IRLEN - IRLEN / 8));
        }
        printf("  %d impulse responses: non-finite samples %d, weighted level %.2f .. %.2f dB, worst energy in the fade-out zone %.1f dB\n", count, bad, minL, maxL, worstTail);
        CHECKF(bad == 0, "all IRs finite");
        CHECKF(minL > -1.0 && maxL < 1.0, "every cab/mic/position/distance has the same guitar-weighted level (+-1 dB): %.2f..%.2f", minL, maxL);
        CHECKF(worstTail < -30.0, "IRs have decayed before the fade-out zone (worst %.1f dB < -30)", worstTail);
    }

    printf("\n== 2. MIC POSITION (dist 0.25): band levels dB re the 250-1k band; bands 80-250 / 250-1k / 1-3k / 3-6k / 6-12k ==\n");
    for (int c = 0; c < 2; ++c) for (int m = 0; m < 5; ++m) {
        double tl[5]; Bands bb[5]; const double ps[5] = { 0.0, 0.25, 0.5, 0.75, 1.0 };
        for (int i = 0; i < 5; ++i) { bb[i] = bands(design(d, c, m, ps[i], 0.25)); tl[i] = tilt(bb[i]); }
        printf("  %-4s %-9s", CABN[c], MICN[m]);
        for (int i = 0; i < 5; ++i) printf(" | pos %.2f: %+5.1f %+5.1f %+5.1f %+5.1f %+5.1f", ps[i], bb[i].b[0] - bb[i].b[1], 0.0, bb[i].b[2] - bb[i].b[1], bb[i].b[3] - bb[i].b[1], bb[i].b[4] - bb[i].b[1]);
        printf("\n");
        CHECKF(tl[0] - tl[4] >= 8.0, "%s/%s: centre-to-edge changes the 3-6k vs 250-1k tilt by >= 8 dB (%.1f)", CABN[c], MICN[m], tl[0] - tl[4]);
        for (int i = 1; i < 5; ++i) CHECKF(tl[i] <= tl[i - 1] + 1.0, "%s/%s: moving outward never gets brighter (%.2f -> %.2f)", CABN[c], MICN[m], ps[i - 1], ps[i]);
        CHECKF(bb[0].b[4] - bb[4].b[4] >= 8.0, "%s/%s: 6-12k falls by >= 8 dB from centre to edge (%.1f)", CABN[c], MICN[m], bb[0].b[4] - bb[4].b[4]);
    }

    printf("\n== 3. MIC DISTANCE (pos 0.3): low-end proximity effect and room reflections ==\n");
    for (int c = 0; c < 2; ++c) for (int m = 0; m < 5; ++m) {
        double lowRel[5], tail[5]; double ripple[5]; const double ds[5] = { 0.0, 0.25, 0.5, 0.75, 1.0 };
        for (int i = 0; i < 5; ++i) {
            auto ir = design(d, c, m, 0.3, ds[i]); Bands b = bands(ir);
            lowRel[i] = b.b[0] - b.b[1]; tail[i] = tailDb(ir, (int) (0.003 * SR));
            // comb ripple: RMS deviation (dB) of the 300-5k response from its own 1/3-octave smoothing
            vector<double> r, sm; for (double f = 300; f <= 5000; f *= 1.03) r.push_back(respDb(ir, f));
            double acc = 0; int n = 0; for (size_t k = 0; k < r.size(); ++k) { double s = 0; int q = 0; for (int j = -11; j <= 11; ++j) if (k + j < r.size() && (int) k + j >= 0) { s += r[k + j]; ++q; } acc += pow(r[k] - s / q, 2); ++n; }
            ripple[i] = sqrt(acc / n);
        }
        printf("  %-4s %-9s low end re mids (dB): %+5.1f %+5.1f %+5.1f %+5.1f %+5.1f | energy after 3 ms (dB): %6.1f %6.1f %6.1f %6.1f %6.1f | ripple %.1f %.1f dB\n", CABN[c], MICN[m],
               lowRel[0], lowRel[1], lowRel[2], lowRel[3], lowRel[4], tail[0], tail[1], tail[2], tail[3], tail[4], ripple[0], ripple[4]);
        const double need = (m == 2) ? 3.0 : 2.5;       // ribbon: ~3 dB band-averaged (several dB more at the very bottom)
        CHECKF(lowRel[0] - lowRel[3] >= need, "%s/%s: close mic has >= %.1f dB more low end than a far one (proximity effect, %.1f)", CABN[c], MICN[m], need, lowRel[0] - lowRel[3]);
        CHECKF(tail[4] - tail[0] >= 8.0, "%s/%s: far mic hears >= 8 dB more late (room) energy than a close mic (%.1f)", CABN[c], MICN[m], tail[4] - tail[0]);
        CHECKF(ripple[4] >= ripple[0] + 0.8, "%s/%s: far mic shows comb-filter ripple (%.1f vs %.1f dB)", CABN[c], MICN[m], ripple[4], ripple[0]);
    }

    printf("\n== 4. the five microphones (cab 4x12, pos 0.3, dist 0.25): bands re 250-1k, and pairwise RMS difference ==\n");
    {
        Bands B[5]; vector<float> irs[5]; for (int m = 0; m < 5; ++m) { irs[m] = design(d, 0, m, 0.3, 0.25); B[m] = bands(irs[m]); printf("  %-9s %+5.1f %+5.1f %+5.1f %+5.1f %+5.1f   (%s)\n", MICN[m], B[m].b[0] - B[m].b[1], 0.0, B[m].b[2] - B[m].b[1], B[m].b[3] - B[m].b[1], B[m].b[4] - B[m].b[1], cab::micSpec(m).character); }
        double diff[5][5];
        for (int a = 0; a < 5; ++a) for (int b = 0; b < 5; ++b) { double s = 0; int n = 0; for (double f = 100; f <= 10000; f *= 1.05) { const double x = respDb(irs[a], f) - respDb(irs[b], f); s += x * x; ++n; } diff[a][b] = sqrt(s / n); }
        printf("  pairwise RMS difference (dB, 100 Hz - 10 kHz):\n            ");
        for (int b = 0; b < 5; ++b) printf(" %9s", MICN[b]); printf("\n");
        for (int a = 0; a < 5; ++a) { printf("  %-9s ", MICN[a]); for (int b = 0; b < 5; ++b) printf(" %9.1f", diff[a][b]); printf("\n"); }
        for (int a = 0; a < 5; ++a) for (int b = a + 1; b < 5; ++b) CHECKF(diff[a][b] >= (a == 0 && b == 1 ? 1.0 : 1.6), "%s vs %s are audibly different (%.1f dB RMS)", MICN[a], MICN[b], diff[a][b]);
        // documented character
        CHECKF(B[0].b[3] - B[0].b[1] > B[1].b[3] - B[1].b[1] + 1.0, "SM57 has more 3-6k presence than the SM58 (%.1f vs %.1f)", B[0].b[3] - B[0].b[1], B[1].b[3] - B[1].b[1]);
        CHECKF(B[1].b[0] - B[1].b[1] > B[0].b[0] - B[0].b[1] + 0.8, "SM58 is fuller in the low end than the SM57 (%.1f vs %.1f)", B[1].b[0] - B[1].b[1], B[0].b[0] - B[0].b[1]);
        CHECKF(B[0].b[3] - B[0].b[1] > B[2].b[3] - B[2].b[1] + 4.0, "SM57 is >= 4 dB brighter than the Royer in the presence band (%.1f vs %.1f)", B[0].b[3] - B[0].b[1], B[2].b[3] - B[2].b[1]);
        CHECKF(B[2].b[0] - B[2].b[1] > B[0].b[0] - B[0].b[1] + 2.5, "Royer ribbon has >= 2.5 dB more low end than the SM57 (proximity + extension, %.1f vs %.1f)", B[2].b[0] - B[2].b[1], B[0].b[0] - B[0].b[1]);
        { // the cab itself rolls off above ~6 kHz, so the mics' "air" is judged on 8-12 kHz, where the mic - not the speaker - decides
          const double a3 = bandDb(irs[3], 8000, 12000) - bandDb(irs[3], 250, 1000), a0 = bandDb(irs[0], 8000, 12000) - bandDb(irs[0], 250, 1000);
          CHECKF(a3 > a0 + 3.0, "C414 has >= 3 dB more air (8-12k) than the SM57 (%.1f vs %.1f)", a3, a0); }
        CHECKF(B[2].b[4] - B[2].b[1] < B[3].b[4] - B[3].b[1] - 3.0, "Royer is >= 3 dB darker than the C414 on top (%.1f vs %.1f)", B[2].b[4] - B[2].b[1], B[3].b[4] - B[3].b[1]);
        CHECKF(B[4].b[0] - B[4].b[1] > B[0].b[0] - B[0].b[1] + 1.5, "MD421 has >= 1.5 dB more low end than the SM57 (%.1f vs %.1f)", B[4].b[0] - B[4].b[1], B[0].b[0] - B[0].b[1]);
        { const double m4 = bandDb(irs[4], 350, 700) - bandDb(irs[4], 1000, 2000), m0 = bandDb(irs[0], 350, 700) - bandDb(irs[0], 1000, 2000);
          CHECKF(m4 < m0 - 0.8, "MD421 scoops the low mids more than the SM57 (%.1f vs %.1f dB, 350-700 Hz re 1-2 kHz)", m4, m0); }
    }

    printf("\n== 5. mic pairing ==\n");
    {
        // (a) arrival delay is physically correct and fractional
        auto a = design(d, 0, 0, 0.3, 0.25, 0.0), b = design(d, 0, 0, 0.3, 0.25, 10.5);
        double best = -1e9; int bl = 0; vector<double> xc(41);
        for (int lag = 0; lag <= 20; ++lag) { double s = 0; for (int i = 0; i + lag < IRLEN; ++i) s += (double) a[i] * b[i + lag]; xc[lag] = s; if (s > best) { best = s; bl = lag; } }
        const double y0 = xc[bl - 1], y1 = xc[bl], y2 = xc[bl + 1], frac = 0.5 * (y0 - y2) / (y0 - 2 * y1 + y2);
        printf("  (a) IR delayed by 10.5 samples: cross-correlation lag %.2f samples\n", bl + frac);
        CHECKF(fabs(bl + frac - 10.5) < 0.2, "fractional mic delay accurate (%.2f vs 10.5)", bl + frac);
        const cab::MicGeometry g1 = cab::CabIrDesigner::geometry(0, 0.3, 0.25), g2 = cab::CabIrDesigner::geometry(0, 0.3, 0.75);
        printf("      mic at 0.25 -> %.1f cm, mic at 0.75 -> %.1f cm : arrival difference %.2f ms\n", g1.axial * 100, g2.axial * 100, (g2.axial - g1.axial) / cab::CabIrDesigner::SPEED_OF_SOUND * 1000);

        // (b) engine-level pairing through CabinetSim: polarity, pan
        auto cabRun = [&](bool two, bool invert, double pan1, double pan2, double lvl2Db, int type2, double dist2) {
            CabinetSim cs; cs.prepare(SR); cs.setCabinet(0);
            cs.setMic(0, 0, 0.3f, 0.25f, true); cs.setMic(1, type2, 0.3f, (float) dist2, two);
            const double pm = invert ? -1.0 : 1.0;
            auto gains = [&](double pan, double g) { const double phi = (pan + 1) * M_PI / 4; return std::pair<float, float>((float) (g * sqrt(2.0) * cos(phi)), (float) (g * sqrt(2.0) * sin(phi))); };
            auto g1 = gains(pan1, 1.0), g2 = gains(pan2, pm * pow(10.0, lvl2Db / 20.0) * (two ? 1 : 0));
            cs.setMix(0, g1.first, g1.second); cs.setMix(1, g2.first, g2.second); cs.forceRender();
            // let the mix settle, then run a noise burst
            vector<float> in((size_t) SR), l(in.size()), r(in.size()); std::mt19937 rng(5); std::normal_distribution<float> nd(0, 0.1f); for (auto& v : in) v = nd(rng);
            cs.prepare(SR); cs.setCabinet(0); cs.setMic(0, 0, 0.3f, 0.25f, true); cs.setMic(1, type2, 0.3f, (float) dist2, two); cs.setMix(0, g1.first, g1.second); cs.setMix(1, g2.first, g2.second); cs.forceRender();
            for (size_t i = 0; i < in.size(); i += 256) cs.process(&in[i], &l[i], &r[i], (int) std::min<size_t>(256, in.size() - i));
            double el = 0, er = 0; for (size_t i = 24000; i < in.size(); ++i) { el += (double) l[i] * l[i]; er += (double) r[i] * r[i]; }
            return std::pair<double, double>(10 * log10(el / 24000 + 1e-20), 10 * log10(er / 24000 + 1e-20)); };
        auto one = cabRun(false, false, 0, 0, 0, 0, 0.25), sameSum = cabRun(true, false, 0, 0, 0, 0, 0.25), cancel = cabRun(true, true, 0, 0, 0, 0, 0.25);
        printf("  (b) identical mics in phase: %+.1f dB vs one mic;  polarity inverted: %+.1f dB (cancellation)\n", sameSum.first - one.first, cancel.first - one.first);
        CHECKF(fabs((sameSum.first - one.first) - 6.0) < 0.3, "two identical mics at 0 dB in phase sum to +6 dB (%.2f)", sameSum.first - one.first);
        CHECKF(cancel.first - one.first < -30.0, "polarity invert cancels identical mics by > 30 dB (%.1f)", cancel.first - one.first);
        auto lr = cabRun(true, false, -1.0, 1.0, 0, 2, 0.5);
        auto onlyL = cabRun(false, false, -1.0, 0, 0, 0, 0.25);
        printf("  (c) mic 1 hard left, mic 2 hard right: L %.1f dB, R %.1f dB;  single mic hard left: L %.1f dB, R %.1f dB\n", lr.first, lr.second, onlyL.first, onlyL.second);
        CHECKF(onlyL.second < onlyL.first - 80.0, "hard-left mic produces nothing on the right (%.1f dB)", onlyL.second - onlyL.first);
        CHECKF(fabs(lr.first - lr.second) < 6.0 && lr.first > onlyL.first - 6.0, "hard-panned pair is balanced and not quieter than a single mic (L %.1f R %.1f)", lr.first, lr.second);
        auto ctr = cabRun(false, false, 0, 0, 0, 0, 0.25); CHECKF(fabs(ctr.first - ctr.second) < 1e-6, "a centred single mic is identical in L and R");
    }
    {   // (d) engine-level: enabling MIC 2 does not jump the level; each cab keeps its own settings
        auto sig = chord(0.25f);
        auto lvl = [&](EngineParams p) { return level(process(p, sig)).rmsDb; };
        EngineParams p = dumb(1, 5); const double l1 = lvl(p); p.cab[0].mic[1].enabled = true; const double l2 = lvl(p);
        p.cab[0].mic[1].type = kMicC414; p.cab[0].mic[1].distance = 0.7f; p.cab[0].mic[1].levelDb = 0; const double l3 = lvl(p);
        printf("  (d) level with one mic %.1f dBFS, with the pair (default) %+.1f dB, with a far 414 at equal level %+.1f dB\n", l1, l2 - l1, l3 - l1);
        CHECKF(fabs(l2 - l1) < 3.0 && fabs(l3 - l1) < 3.0, "switching MIC 2 on does not jump the level by more than 3 dB (%.1f / %.1f)", l2 - l1, l3 - l1);

        EngineParams q = dumb(1, 5); q.activeCab = 0; q.cab[0].mic[0].position = 0.0f; q.cab[1].mic[0].position = 1.0f; q.cab[1].mic[0].type = kMicRoyer121; q.cab[1].levelDb = -6;
        EngineParams q2 = q; q2.cab[1] = CabSettings(); q2.cab[1].mic[0].position = 0.5f;
        auto o1 = process(q, sig), o2 = process(q2, sig); double dmax = 0; for (size_t i = 0; i < o1.size(); ++i) dmax = max(dmax, (double) fabs(o1[i] - o2[i]));
        EngineParams r = q; r.activeCab = 1; EngineParams r2 = r; r2.cab[0] = CabSettings(); r2.cab[0].mic[0].position = 0.9f; r2.cab[0].levelDb = 6;
        auto o3 = process(r, sig), o4 = process(r2, sig); double dmax2 = 0; for (size_t i = 0; i < o3.size(); ++i) dmax2 = max(dmax2, (double) fabs(o3[i] - o4[i]));
        printf("  (e) per-cab settings: editing the inactive cab changes the output by %.1e (cab A active) / %.1e (cab B active)\n", dmax, dmax2);
        CHECKF(dmax == 0.0 && dmax2 == 0.0, "settings of the inactive cab never leak into the active one");
        // and the two cabs really use their own values
        EngineParams s0 = dumb(1, 5); s0.cab[0].levelDb = -6; s0.cab[1].levelDb = +3;
        s0.activeCab = 0; const double ra = lvl(s0); s0.activeCab = 1; const double rb = lvl(s0);
        s0.cab[0].levelDb = 0; s0.cab[1].levelDb = 0; s0.activeCab = 0; const double ra0 = lvl(s0); s0.activeCab = 1; const double rb0 = lvl(s0);
        printf("  (f) per-cab CAB LEVEL: cab A -6 dB -> %+.2f dB change, cab B +3 dB -> %+.2f dB change\n", ra - ra0, rb - rb0);
        CHECKF(fabs((ra - ra0) + 6.0) < 0.3 && fabs((rb - rb0) - 3.0) < 0.3, "each cab applies its own CAB LEVEL");
    }

    printf("\n== 6. engine integration: knob moves, cab / mic switching, CPU ==\n");
    {
        // click test: sustained note while switching cab, mic type, enabling mic 2
        const int N = (int) (4 * SR); vector<float> in(N), l(N), r(N);
        for (int n = 0; n < N; ++n) in[n] = (float) (0.2 * (sin(2 * M_PI * 196 * n / SR) + 0.5 * sin(2 * M_PI * 392 * n / SR)));
        EngineParams p = dumb(0, 4); AmpEngine e; e.setParams(p); e.prepare(SR, 512);
        double worstStep = 0, steady = 0; float prev = 0;
        for (int pos = 0; pos < N; pos += 512) {
            const double t = pos / SR;
            EngineParams q = p; q.activeCab = ((int) (t / 0.8)) % 2; q.cab[0].mic[0].type = ((int) (t / 0.5)) % 5; q.cab[1].mic[0].type = ((int) (t / 0.7)) % 5;
            q.cab[0].mic[1].enabled = ((int) (t / 0.6)) % 2; q.cab[1].mic[1].enabled = ((int) (t / 0.9)) % 2; e.setParams(q);
            e.process(&in[pos], &l[pos], &r[pos], 512);
        }
        for (int n = 1; n < N; ++n) { const double st = fabs((double) l[n] - l[n - 1]); if (n > (int) (0.5 * SR)) worstStep = max(worstStep, st); }
        double rms = 0; for (int n = (int) (0.5 * SR); n < N; ++n) rms += (double) l[n] * l[n]; rms = sqrt(rms / (N - 0.5 * SR));
        printf("  cab / mic switching during a note: largest sample step %.3f (signal rms %.3f)\n", worstStep, rms);
        CHECKF(worstStep < 2.0 * rms * 2.0 * M_PI * 1000.0 / SR * 4.0 + 0.1, "no click when switching cabs / mics (step %.3f)", worstStep);
        (void) steady; (void) prev;

        // CPU: static vs continuously re-rendered IRs (a knob being dragged) vs two mics
        auto cpu = [&](std::function<void(EngineParams&, double)> f) {
            EngineParams q = dumb(1, 5); AmpEngine eng; eng.setParams(q); eng.prepare(SR, 512);
            const int M = (int) (8 * SR); vector<float> x(M, 0.f), a(M), b(M); for (int n = 0; n < M; ++n) x[n] = (float) (0.2 * sin(2 * M_PI * 196 * n / SR));
            auto t0 = std::chrono::steady_clock::now();
            for (int pos = 0; pos < M; pos += 512) { EngineParams z = q; f(z, pos / SR); eng.setParams(z); eng.process(&x[pos], &a[pos], &b[pos], 512); }
            return 100.0 * std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() / 8.0; };
        const double cStatic = cpu([](EngineParams&, double) {});
        const double cDrag = cpu([](EngineParams& z, double t) { z.cab[0].mic[0].position = (float) (0.5 + 0.5 * sin(t * 3.0)); z.cab[0].mic[0].distance = (float) (0.5 + 0.5 * sin(t * 2.3)); });
        const double cTwo = cpu([](EngineParams& z, double) { z.cab[0].mic[1].enabled = true; });
        const double cTwoDrag = cpu([](EngineParams& z, double t) { z.cab[0].mic[1].enabled = true; z.cab[0].mic[0].position = (float) (0.5 + 0.5 * sin(t * 3.0)); z.cab[0].mic[1].distance = (float) (0.5 + 0.5 * sin(t * 2.3)); });
        printf("  CPU (48 kHz, one core, this sandbox): amp+1 mic %.1f%%, mic knobs dragged %.1f%%, 2 mics %.1f%%, 2 mics + dragging %.1f%%\n", cStatic, cDrag, cTwo, cTwoDrag);
        CHECKF(cDrag < cStatic + 12.0, "dragging mic knobs costs < 12 percentage points (%.1f vs %.1f)", cDrag, cStatic);
        CHECKF(cTwo < cStatic + 12.0, "a second mic costs < 12 percentage points (%.1f vs %.1f)", cTwo, cStatic);
    }

    printf("== 7. every cab x mic combination: level, phase, clipping ==\n");
    {
        // Level: the clean amp (linear) plays a chord; every combination is compared with MIC 1 alone, centred, same cab.
        //   - turning MIC 2 on / changing its polarity / panning must not change the loudness of a side beyond +-1.5 dB
        //     (a hard-panned side with ONE mic may be quieter by that mic's own LVL setting, never louder)
        // Phase: the band-weighted correlation of the two IRs (pairStats) is reported, and the polarity switches are checked
        // Clipping: a hot setting (lead, MASTER 10, input 0.9 FS) through every combination never exceeds the limiter ceiling
        const double ceil = pow(10.0, engine::SafetyLimiter::CEILING_DB / 20.0);
        auto chordSig = chord(0.1f, 110.0, 1.0, 0.6);
        auto render = [&](EngineParams p, const vector<float>& in, long long* clamp = nullptr) {
            AmpEngine e; e.setParams(p); e.prepare(SR, 512);
            Stereo o; o.l.resize(in.size()); o.r.resize(in.size());
            for (size_t i = 0; i + 512 <= in.size(); i += 512) e.process(&in[i], &o.l[i], &o.r[i], 512);
            if (clamp) *clamp = e.clampHits();
            return o; };
        struct Row { int cab, m1, m2; double dCentre, dInv, dSplit, dHard, dOff, rho, lowDelta, pk2, pk1; bool ok; };
        vector<Row> rows(2 * 5 * 5);
        std::vector<std::thread> th;
        for (int cabi = 0; cabi < 2; ++cabi) for (int m1 = 0; m1 < 5; ++m1) th.emplace_back([&, cabi, m1] {
            EngineParams base = dumb(0, 3.0f); base.activeCab = cabi; base.cabOff = false;
            auto setCab = [&](EngineParams& q, int t1, int t2, bool on2, bool inv1, bool inv2, float pan1, float pan2) {
                CabSettings& c = q.cab[cabi];
                c.mic[0].type = t1; c.mic[1].type = t2; c.mic[1].enabled = on2;
                c.mic[0].polarityInvert = inv1; c.mic[1].polarityInvert = inv2; c.mic[0].pan = pan1; c.mic[1].pan = pan2; };
            EngineParams ref = base; setCab(ref, m1, 2, false, false, false, 0, 0);
            auto yr = render(ref, chordSig); const R rr = level(yr.l);
            for (int m2 = 0; m2 < 5; ++m2) {
                Row& w = rows[(size_t) ((cabi * 5 + m1) * 5 + m2)]; w = Row{ cabi, m1, m2, 0, 0, 0, 0, 0, 0, 0, 0, 0, true };
                // MIC 2 on, centred, normal polarity
                EngineParams a = base; setCab(a, m1, m2, true, false, false, 0, 0);
                auto ya = render(a, chordSig); w.dCentre = std::max(level(ya.l).rmsDb, level(ya.r).rmsDb) - rr.rmsDb; w.pk2 = level(ya.l).pkDb - rr.pkDb;
                // MIC 2 phase inverted
                EngineParams b = a; setCab(b, m1, m2, true, false, true, 0, 0);
                auto yb = render(b, chordSig); w.dInv = std::max(level(yb.l).rmsDb, level(yb.r).rmsDb) - rr.rmsDb;
                w.lowDelta = fabs(level(yb.l).rmsDb - level(ya.l).rmsDb);
                // split pan: mic 1 hard left, mic 2 hard right
                EngineParams c = a; setCab(c, m1, m2, true, false, false, -1, 1);
                auto yc = render(c, chordSig); w.dSplit = std::max(level(yc.l).rmsDb, level(yc.r).rmsDb) - rr.rmsDb;
                // both mics hard left: the left side keeps the loudness, the right side is silent
                EngineParams d = a; setCab(d, m1, m2, true, false, false, -1, -1);
                auto yd = render(d, chordSig); w.dHard = level(yd.l).rmsDb - rr.rmsDb; w.dOff = level(yd.r).rmsDb - rr.rmsDb;
                // phase statistics of the pair
                CabinetSim cs; cs.prepare(SR); cs.setCabinet(cabi); cs.setMic(0, m1, 0.3f, 0.25f, true); cs.setMic(1, m2, 0.3f, 0.45f, true); cs.forceRender();
                double e0, e1, cc; cs.pairStats(e0, e1, cc); w.rho = cc / sqrt(e0 * e1);
                // clipping: hot setting, both mics on, normal and inverted
                EngineParams h = dumb(2, 10.0f); h.activeCab = cabi; h.a1Master = 10; h.cabOff = false;
                setCab(h, m1, m2, true, false, false, 0, 0);
                auto hot = chordSig; for (auto& v : hot) v *= 9.0f;
                long long cl1 = 0, cl2 = 0;
                auto h1 = render(h, hot, &cl1);
                setCab(h, m1, m2, true, false, true, 0, 0);
                auto h2 = render(h, hot, &cl2);
                const double pk = std::max({ level(h1.l).pkDb, level(h1.r).pkDb, level(h2.l).pkDb, level(h2.r).pkDb });
                w.pk1 = pk;
                w.ok = pk <= engine::SafetyLimiter::CEILING_DB + 0.01 && cl1 == 0 && cl2 == 0;
            }
        });
        for (auto& t : th) t.join();
        double mxC = -99, mnC = 99, mxI = -99, mnI = 99, mxS = -99, mxH = -99, mxOff = -99, mnRho = 9, mxPk = -99; int bad = 0;
        for (auto& w : rows) {
            mxC = max(mxC, w.dCentre); mnC = min(mnC, w.dCentre); mxI = max(mxI, w.dInv); mnI = min(mnI, w.dInv); mxS = max(mxS, w.dSplit);
            mxH = max(mxH, fabs(w.dHard)); mxOff = max(mxOff, w.dOff); mnRho = min(mnRho, w.rho); mxPk = max(mxPk, w.pk1); bad += !w.ok;
        }
        printf("  %zu combinations (2 cabs x 5 mic 1 x 5 mic 2)\n", rows.size());
        printf("  MIC 2 on, centred            : loudest side vs MIC 1 alone  %+.2f .. %+.2f dB\n", mnC, mxC);
        printf("  MIC 2 on, polarity inverted  : %+.2f .. %+.2f dB\n", mnI, mxI);
        printf("  mic 1 hard L + mic 2 hard R  : loudest side up to %+.2f dB vs MIC 1 alone\n", mxS);
        printf("  both mics hard L             : left side within %.2f dB of MIC 1 alone, right side at most %+.1f dB\n", mxH, mxOff);
        printf("  mic-pair correlation (default positions, 80 Hz - 2.5 kHz, pink-weighted): worst %.2f\n", mnRho);
        printf("  hot setting (lead g10, MASTER 10, input +9 dB over the chord): worst peak %.2f dBFS, %d combos over the ceiling or clamped\n", mxPk, bad);
        CHECKF(mxC < 1.0 && mnC > -1.0, "MIC 2 on does not change the loudness (%+.2f .. %+.2f dB, limit +-1)", mnC, mxC);
        CHECKF(mxI < 1.0 && mnI > -1.5, "MIC 2 phase invert does not change the loudness (%+.2f .. %+.2f dB)", mnI, mxI);
        CHECKF(mxS < 1.5, "split panning never gets louder than MIC 1 alone (%+.2f dB)", mxS);
        CHECKF(mxH < 1.0, "panning both mics to one side keeps that side's loudness (%.2f dB)", mxH);
        CHECKF(mxOff < -40.0, "the opposite side is silent when everything is panned away (%.1f dB)", mxOff);
        CHECKF(bad == 0, "no combination clips or exceeds the ceiling at a hot setting (%d bad)", bad);
        CHECKF(mnRho > -0.5, "no mic pair at its default positions is badly out of phase (worst correlation %.2f)", mnRho);

        // MIC 1 PHASE INVERT on a single mic: same loudness, exactly inverted waveform
        {
            EngineParams a = dumb(0, 3.0f), b = a; b.cab[0].mic[0].polarityInvert = true;
            auto ya = render(a, chordSig), yb = render(b, chordSig);
            double num = 0, den = 0, ea = 0, eb = 0; for (size_t i = 0; i < ya.l.size(); ++i) { num += (double) ya.l[i] * yb.l[i]; ea += (double) ya.l[i] * ya.l[i]; eb += (double) yb.l[i] * yb.l[i]; (void) den; }
            const double corr = num / sqrt(ea * eb + 1e-30);
            printf("  MIC 1 phase invert alone: correlation with the normal sound %.4f, level change %.3f dB\n", corr, 10 * log10(eb / ea));
            CHECKF(corr < -0.999, "MIC 1 phase invert flips the waveform (correlation %.4f)", corr);
            CHECKF(fabs(10 * log10(eb / ea)) < 0.05, "MIC 1 phase invert does not change the level");
            // with two mics it must change the sound (comb filter) but not the loudness
            EngineParams c = dumb(0, 3.0f); c.cab[0].mic[1].enabled = true; EngineParams d = c; d.cab[0].mic[0].polarityInvert = true;
            auto yc = render(c, chordSig), yd = render(d, chordSig);
            double diff = 0, e = 0; for (size_t i = 0; i < yc.l.size(); ++i) { diff += fabs((double) yc.l[i] - yd.l[i]); e += fabs((double) yc.l[i]); }
            CHECKF(diff / e > 0.05, "with two mics, MIC 1 phase invert changes the sound (relative difference %.3f)", diff / e);
            CHECKF(fabs(level(yc.l).rmsDb - level(yd.l).rmsDb) < 1.5, "... but not the loudness (%.2f dB)", level(yc.l).rmsDb - level(yd.l).rmsDb);
        }
    }

    printf("\n%s  (%d of %d checks failed)\n", gFails ? "CAB/MIC AUDIT FAILED" : "CAB/MIC AUDIT PASSED", gFails, gChecks);
    return gFails ? 1 : 0;
}
