// Voicing / gain-structure / clarity checks for every amp mode and channel (headless, real engine).
// Measurement proxies for what cannot be judged without listening:
//   THD windows (character per mode), 10 %-THD input level (mode separation + headroom), loudness range,
//   crest factor and >6 kHz energy ("fizz"), decay smoothness ("gating"), blocking recovery, alias rejection (1x/2x/4x),
//   clean-channel headroom at pedal levels, reverb effectiveness.
#include "metrics.h"
#include <cstdarg>
#include <thread>
#include <mutex>
#include <sstream>
using namespace mx;

static std::mutex gm; static int gFails = 0, gChecks = 0;
struct Log { std::ostringstream o; int fails = 0, checks = 0; };
#define CHECK(L, cond, ...) do { char b_[256]; snprintf(b_, 256, __VA_ARGS__); ++(L).checks; if (!(cond)) { ++(L).fails; (L).o << "   FAIL  " << b_ << "\n"; } } while (0)
static char* fmt(const char* f, ...) { static thread_local char b[256]; va_list a; va_start(a, f); vsnprintf(b, 256, f, a); va_end(a); return b; }

static double satDb(EngineParams p, double pct = 10.0) {       // first input level (dBFS) where 220 Hz THD >= pct
    for (double db = -60; db <= 0.01; db += 3) { double a = std::min(1.0, pow(10, db / 20)); if (thd(p, 220, a) >= pct) return db; }
    return 6.0;
}
struct Voice { const char* name; int amp, mode; };
static EngineParams mk(const Voice& v, double g) { return v.amp == 0 ? dumb(v.mode, (float) g) : dock(v.mode, (float) g); }

static void sweep(const Voice& v, Log& L) {
    char b[128]; snprintf(b, 128, "\n== %s ==\n gain | rms dBFS | peak | crest | >6k dB | THD%% @.05/.15/.4/.9 FS | gateΔ dB | block dB\n", v.name); L.o << b;
    auto c12 = chord(0.25f);
    double lo = 1e9, hi = -1e9, thd15[11] = {};
    for (int g = 1; g <= 10; ++g) {
        EngineParams p = mk(v, g);
        R r = level(process(p, c12));
        double t1 = thd(p, 220, .05f), t2 = thd(p, 220, .15f), t3 = thd(p, 220, .4f), t4 = thd(p, 220, .9f);
        double gt = std::max(gateExcessDb(p, 0.3), gateExcessDb(p, 0.05)), bl = blockingDb(p);
        snprintf(b, 128, "  %2d  | %7.1f  | %5.1f | %4.1f | %6.1f | %5.1f %5.1f %5.1f %5.1f | %7.1f | %5.1f\n", g, r.rmsDb, r.pkDb, r.crest, r.fizz, t1, t2, t3, t4, gt, bl); L.o << b;
        lo = std::min(lo, r.rmsDb); hi = std::max(hi, r.rmsDb); thd15[g] = t2;
        CHECK(L, r.pkDb < -8.0, "g%d peak below -8 dBFS for a -12 dBFS chord (%.1f)", g, r.pkDb);
        CHECK(L, r.fizz < -24.0, "g%d fizz (>6 kHz, 12 dB/oct) under -24 dB (%.1f)", g, r.fizz);
        CHECK(L, r.crest > 2.0, "g%d not flat-topped (crest %.1f)", g, r.crest);
        CHECK(L, gt < 3.0, "g%d decay does not gate (worst excess drop %.1f dB < 3)", g, gt);
        CHECK(L, std::fabs(bl) < 3.0, "g%d blocking recovery within 3 dB (%.1f)", g, bl);
    }
    snprintf(b, 128, "  loudness range over GAIN 1..10: %.1f dB\n", hi - lo); L.o << b;
    for (int g = 1; g < 10; ++g) CHECK(L, thd15[g + 1] >= thd15[g] - 4.0 || thd15[g] > 25, "distortion does not fall as GAIN rises (g%d->%d)", g, g + 1);

    // the GAIN knob must be usable over its whole sweep (no dead first half, no cliff): checked on the gain-voiced channels
    if ((v.amp == 0 && (v.mode == 1)) || (v.amp == 1 && v.mode == 1)) {
        double maxStep = 0; for (int g = 1; g < 10; ++g) maxStep = std::max(maxStep, thd15[g + 1] - thd15[g]);
        CHECK(L, maxStep <= 9.0, "GAIN has no cliff: biggest THD step between neighbouring knob positions <= 9 points (%.1f)", maxStep);
        CHECK(L, thd15[5] >= 0.15 * thd15[10], "the first half of the GAIN knob already does 15%% of the work (g5 %.1f vs g10 %.1f)", thd15[5], thd15[10]);
    }
    if (v.amp == 0) {
        const double rangeMin[4] = { 4.0, 4.0, 3.0, 3.0 };
        CHECK(L, hi - lo >= rangeMin[v.mode], "GAIN changes level by at least %.0f dB (%.1f)", rangeMin[v.mode], hi - lo);
        CHECK(L, hi - lo <= 14.0, "GAIN does not behave like a volume knob (<= 14 dB, %.1f)", hi - lo);
        switch (v.mode) {
            case 0: CHECK(L, thd15[6] < 2.0, "Clean: THD<2%% up to GAIN 6 (%.1f)", thd15[6]); CHECK(L, thd15[10] > 5.0, "Clean: breaks up a little at GAIN 10 (%.1f)", thd15[10]); break;
            case 1: CHECK(L, thd15[1] < 3.0, "Crunch: GAIN 1 is edge-of-breakup or cleaner (%.1f)", thd15[1]);
                    CHECK(L, thd15[4] < 6.0, "Crunch: still only an edge of breakup at GAIN 4 (%.1f)", thd15[4]);
                    CHECK(L, thd15[10] > 25.0 && thd15[10] < 40.0, "Crunch: properly dirty but not fuzz at GAIN 10 (%.1f)", thd15[10]); break;
            case 2: CHECK(L, thd15[1] < 15.0, "Lead: GAIN 1 is crunch not fuzz (%.1f)", thd15[1]); CHECK(L, thd15[6] > 18.0, "Lead: saturated by GAIN 6 (%.1f)", thd15[6]); break;
            case 3: CHECK(L, thd15[3] > 15.0, "Ultra: saturated by GAIN 3 (%.1f)", thd15[3]); break;
        }
    } else {
        CHECK(L, hi - lo >= (v.mode == 0 ? 10.0 : 7.5), "Dock GAIN clearly raises level+drive (>= %.0f dB range, %.1f)", v.mode == 0 ? 10.0 : 8.0, hi - lo);
        if (v.mode == 0) { CHECK(L, thd15[10] < 8.0, "Pristine stays clean even at GAIN 10 (%.1f)", thd15[10]); }
        else { CHECK(L, thd15[2] < 3.0, "Chime: clean at GAIN 2 (%.1f)", thd15[2]); CHECK(L, thd15[5] > 5.0 && thd15[5] < 15.0, "Chime: edge of breakup at GAIN 5 (%.1f)", thd15[5]); CHECK(L, thd15[10] > 10.0 && thd15[10] < 60.0, "Chime: edge-of-breakup at GAIN 10, not fuzz (%.1f)", thd15[10]); }
    }
}

int main() {
    const Voice vs[] = { { "AMP-001 mode 1 Clean", 0, 0 }, { "AMP-001 mode 2 Crunch", 0, 1 }, { "AMP-001 mode 3 Lead", 0, 2 }, { "AMP-001 mode 4 Ultra", 0, 3 },
                         { "AMP-002 channel A Pristine", 1, 0 }, { "AMP-002 channel B Chime", 1, 1 } };
    Log logs[6]; std::vector<std::thread> th;
    for (int i = 0; i < 6; ++i) th.emplace_back([&, i] { sweep(vs[i], logs[i]); });
    for (auto& t : th) t.join();
    for (auto& l : logs) { printf("%s", l.o.str().c_str()); gFails += l.fails; gChecks += l.checks; }

    Log X; auto& o = X.o;
    // ---------------- mode separation (the "3 and 4 sound the same" complaint) -------------------------------------
    o << "\n== mode / channel separation: input level (dBFS) at which 220 Hz reaches 10% THD ==\n";
    double sat[6][3]; const int gs[3] = { 3, 5, 8 };
    { std::vector<std::thread> t2; for (int i = 0; i < 6; ++i) t2.emplace_back([&, i] { for (int k = 0; k < 3; ++k) sat[i][k] = satDb(mk(vs[i], gs[k])); }); for (auto& t : t2) t.join(); }
    for (int i = 0; i < 6; ++i) o << fmt("  %-28s g3 %+5.0f  g5 %+5.0f  g8 %+5.0f\n", vs[i].name, sat[i][0], sat[i][1], sat[i][2]);
    for (int i = 0; i < 3; ++i) CHECK(X, sat[i][1] - sat[i + 1][1] >= (i == 0 ? 5.0 : 9.0), "AMP-001 mode %d is >= 5 dB (9 dB for the others) cleaner than mode %d at GAIN 5 (%.0f vs %.0f)", i + 1, i + 2, sat[i][1], sat[i + 1][1]);
    CHECK(X, sat[3][1] <= -36.0, "Ultra saturates from a quiet DI at GAIN 5 (%.0f dBFS)", sat[3][1]);
    CHECK(X, sat[0][1] >= -3.0, "Clean mode keeps headroom up to near 0 dBFS at GAIN 5 (%.0f)", sat[0][1]);
    CHECK(X, sat[4][1] >= 0.0 && sat[4][2] >= -3.0, "Pristine: clean to full-scale at GAIN 5, -3 dB at GAIN 8 (%.0f / %.0f)", sat[4][1], sat[4][2]);
    CHECK(X, sat[4][2] - sat[5][2] >= 9.0, "Chime breaks up >= 9 dB earlier than Pristine at GAIN 8 (%.0f vs %.0f)", sat[5][2], sat[4][2]);

    // ---------------- tonal difference between Dock channels --------------------------------------------------------
    {
        const double fq[6] = { 100, 200, 400, 800, 1600, 3200 };
        auto resp = [&](int ch, double f) { EngineParams p = dock(ch, 5); std::vector<float> in(24000); for (int n = 0; n < 24000; ++n) in[n] = (float) (0.01 * sin(2 * M_PI * f * n / SR));
            auto out = process(p, in); double a = 0, b = 0; for (int n = 12000; n < 21600; ++n) { a += out[n] * sin(2 * M_PI * f * n / SR); b += out[n] * cos(2 * M_PI * f * n / SR); } return 20 * log10(sqrt(a * a + b * b) + 1e-12); };
        double ra[6], rb[6], ma = 0, mb = 0; for (int i = 0; i < 6; ++i) { ra[i] = resp(0, fq[i]); rb[i] = resp(1, fq[i]); ma += ra[i] / 6; mb += rb[i] / 6; }
        double d = 0; o << "\n== Dock channel tone (dB re own mean) 100/200/400/800/1.6k/3.2k:\n   A:"; for (int i = 0; i < 6; ++i) { o << fmt(" %+5.1f", ra[i] - ma); } o << "\n   B:";
        for (int i = 0; i < 6; ++i) { o << fmt(" %+5.1f", rb[i] - mb); d += std::pow((ra[i] - ma) - (rb[i] - mb), 2) / 6; }
        d = sqrt(d); o << fmt("\n   RMS difference %.1f dB\n", d);
        CHECK(X, d >= 2.5, "Dock channels have a clearly different tonal balance (RMS difference >= 2.5 dB, %.1f)", d);
    }

    // ---------------- alias rejection: 4x vs 2x vs 1x ----------------------------------------------------------------
    {
        EngineParams p = mk(vs[3], 8); double a1 = aliasDb(p, 3300, .15f, 1), a2 = aliasDb(p, 3300, .15f, 2), a4 = aliasDb(p, 3300, .15f, 4);
        o << fmt("\n== aliasing (Ultra g8, 3.3 kHz sine): non-harmonic energy 1x %.1f dB, 2x %.1f dB, 4x %.1f dB\n", a1, a2, a4);
        CHECK(X, a4 <= a2 - 6.0 && a2 <= a1 - 4.0, "4x oversampling folds back >= 6 dB less than 2x, which beats 1x");
        CHECK(X, a4 <= -35.0, "alias products under -35 dB at 4x (%.1f)", a4);
    }

    // ---------------- headroom at pedal levels (clean channels) ------------------------------------------------------
    o << "\n== headroom: 220 Hz sine, THD% / output fundamental dBFS ==\n";
    {
        struct H { const char* n; Voice v; double g; double a; double maxThd; };
        // (the Pristine limits include the output limiter, which now engages near 0 dBFS after the +6 dB Dock level change)
        const H hs[] = { { "Clean g4  @ -6 dBFS", vs[0], 4, 0.5, 3.0 }, { "Clean g6  @ -6 dBFS", vs[0], 6, 0.5, 8.0 },
                         { "Pristine g5 @ 0 dBFS", vs[4], 5, 1.0, 7.0 }, { "Pristine g8 @ -6 dBFS", vs[4], 8, 0.5, 5.0 } };
        for (auto& h : hs) { EngineParams p = mk(h.v, h.g); double t = thd(p, 220, (float) h.a), lv = fundDb(p, 220, (float) h.a);
            o << fmt("  %-26s THD %5.1f %%   out %+6.1f dBFS\n", h.n, t, lv);
            CHECK(X, t <= h.maxThd, "%s: THD %.1f%% <= %.0f%%", h.n, t, h.maxThd);
            CHECK(X, lv < -0.5, "%s: output stays below full scale (%.1f)", h.n, lv); }
    }

    // ---------------- reverb effectiveness ----------------------------------------------------------------------------
    o << "\n== reverb: tail energy 0.3-1.3 s after a 0.1 s burst, relative to the burst (dB) ==\n";
    for (int a = 0; a < 2; ++a) {
        double tail[3]; const float rv[3] = { 0, 3, 6 };
        for (int k = 0; k < 3; ++k) {
            EngineParams p; p.activeAmp = a; p.a1Channel[0].gain = 4; p.a2ChannelCtl[0].gain = 6; p.a1Reverb = p.a2Reverb = rv[k];
            std::vector<float> in((int) (SR * 1.5), 0.f); for (int n = 0; n < (int) (SR * 0.1); ++n) in[n] = (float) (0.15 * (sin(2 * M_PI * 196 * n / SR) + 0.5 * sin(2 * M_PI * 392 * n / SR)));
            auto out = process(p, in); double e0 = 0, e1 = 0; for (int n = 0; n < (int) (SR * 0.1); ++n) e0 += (double) out[n] * out[n];
            for (int n = (int) (SR * 0.3); n < (int) (SR * 1.3); ++n) e1 += (double) out[n] * out[n];
            tail[k] = 10 * log10(e1 / (SR * 1.0) / (e0 / (SR * 0.1)) + 1e-14);
        }
        o << fmt("  %s: knob 0 %.1f   knob 3 %.1f   knob 6 %.1f\n", a ? "AMP-002 spring" : "AMP-001 room  ", tail[0], tail[1], tail[2]);
        CHECK(X, tail[1] > tail[0] + 8.0 && tail[1] > -45.0, "reverb knob 3 is clearly audible (tail %.1f dB, +%.1f over dry)", tail[1], tail[1] - tail[0]);
        CHECK(X, tail[2] > -35.0, "reverb knob 6 is a big tail (%.1f dB)", tail[2]);
    }
    printf("%s", X.o.str().c_str()); gFails += X.fails; gChecks += X.checks;
    printf("\n%s  (%d of %d checks failed)\n", gFails ? "VOICING CHECK FAILED" : "VOICING CHECK PASSED", gFails, gChecks);
    return gFails ? 1 : 0;
}
