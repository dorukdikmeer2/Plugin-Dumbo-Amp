// Gain-staging / clipping audit (headless, real engine).
//
// The question this answers: "when I turn MASTER, CAB LEVEL, OUT TRIM or IN TRIM up, does the signal clip?"
//
//   A  default headroom      every voicing x GAIN {1,5,10} x DI {-12, -3 dBFS}: peaks, limiter activity
//   B  MASTER sweep          level law, monotonic, no cliff, headroom at every position
//   C  CAB LEVEL sweep       pure gain (+-0.3 dB accuracy), headroom, limiter only far above normal
//   D  OUT TRIM sweep        same
//   E  IN TRIM sweep         the amp compresses instead of overloading
//   F  everything up         MASTER 10 + CAB +12 + OUT +12: output stays under the ceiling, no flat tops
//   G  limiter quality       steady sine 3..12 dB over the ceiling: THD, and a comparison with the old tanh clipper
//   H  zipper / click test   each volume control swept while a sustained note plays
//   I  noise / DC            nothing audible when idle, no DC offset after limiting
#include "metrics.h"
#include <atomic>
#include <cstdarg>
#include <complex>
#include <functional>
#include <mutex>
#include <sstream>
#include <thread>
using namespace mx;

static std::mutex gm; static int gFails = 0, gChecks = 0;
struct Log { std::ostringstream o; int fails = 0, checks = 0; };
#define CHECK(L, cond, ...) do { char b_[300]; snprintf(b_, 300, __VA_ARGS__); ++(L).checks; if (!(cond)) { ++(L).fails; (L).o << "   FAIL  " << b_ << "\n"; } } while (0)
static char* fmt(const char* f, ...) { static thread_local char b[300]; va_list a; va_start(a, f); vsnprintf(b, 300, f, a); va_end(a); return b; }

struct Res { double pkDb, rmsDb, grDb; int flatRuns; double dc; };

// render through the real engine; measure after the first 0.15 s
static Res run(const EngineParams& p, const vector<float>& in) {
    AmpEngine e; e.setParams(p); e.prepare(SR, 512);
    vector<float> l(in.size()), r(in.size());
    double gr = 0;
    for (size_t i = 0; i < in.size(); i += 512) { int n = (int) min<size_t>(512, in.size() - i); e.process(&in[i], &l[i], &r[i], n); gr = max(gr, (double) e.limiterGainReductionDb()); }
    const int clampHits = (int) e.clampHits();
    size_t a = (size_t) (0.15 * SR);
    double pk = 0, sq = 0, sum = 0; int runs = clampHits, cur = 0;
    for (size_t i = a; i < l.size(); ++i) {
        const double v = fabs(l[i]); pk = max(pk, v); sq += (double) l[i] * l[i]; sum += l[i];
        (void) cur;
    }
    const size_t n = l.size() - a;
    return { 20 * log10(pk + 1e-12), 10 * log10(sq / n + 1e-14), gr, runs, sum / n };
}

static const double GS[3] = { 1, 5, 10 };
struct Voice { const char* name; int amp, mode; };
static const Voice VS[] = { { "Dumb 1 Clean", 0, 0 }, { "Dumb 2 Crunch", 0, 1 }, { "Dumb 3 Lead", 0, 2 }, { "Dumb 4 Ultra", 0, 3 }, { "Dock A Pristine", 1, 0 }, { "Dock B Chime", 1, 1 } };
static EngineParams mk(const Voice& v, double g) { return v.amp == 0 ? dumb(v.mode, (float) g) : dock(v.mode, (float) g); }
static void setMaster(EngineParams& p, float m) { p.a1Master = p.a2Master = m; }

template <class F> static void parallel(int n, F f) {
    std::atomic<int> next { 0 }; vector<std::thread> th;
    for (unsigned t = 0; t < std::max(2u, std::min(8u, std::thread::hardware_concurrency())); ++t)
        th.emplace_back([&] { for (int i; (i = next++) < n;) f(i); });
    for (auto& t : th) t.join();
}

// HF-burst detector for the zipper test: RMS of an 8 kHz high-passed copy in 1 ms windows, returns the largest window
static double hfBurst(const vector<float>& y, size_t from, size_t to) {
    double x1 = 0, y1 = 0, x2 = 0, y2 = 0; const double k = exp(-2 * M_PI * 8000 / SR);
    vector<double> hp(y.size());
    for (size_t i = 0; i < y.size(); ++i) { double a = k * (y1 + y[i] - x1); x1 = y[i]; y1 = a; double b = k * (y2 + a - x2); x2 = a; y2 = b; hp[i] = b; }
    const int W = (int) (0.001 * SR); double worst = 0;
    for (size_t s = from; s + W < to; s += W / 2) { double e = 0; for (int i = 0; i < W; ++i) e += hp[s + i] * hp[s + i]; worst = max(worst, sqrt(e / W)); }
    return worst;
}

int main() {
    const auto c12 = chord(0.25f), c3 = chord(0.708f), cDrop = chord(0.5f, 73.42);
    Log A, B, C, D, E, F, G, H, I;

    //------------------------------------------------------------------ A: defaults
    A.o << "\n== A. default headroom (MASTER 5, CAB 0 dB, OUT 0 dB): peak dBFS / limiter gain reduction ==\n";
    A.o << " voicing          gain |  DI -12 dBFS: peak   rms   GR | DI -3 dBFS: peak   rms   GR | drop-D chord -6: peak  GR\n";
    {
        struct Row { Res a, b, c; }; vector<Row> rows(6 * 3);
        parallel(18, [&](int i) { const Voice& v = VS[i / 3]; const double g = GS[i % 3];
            EngineParams p = mk(v, g); rows[i] = { run(p, c12), run(p, c3), run(p, cDrop) }; });
        for (int i = 0; i < 18; ++i) {
            const Voice& v = VS[i / 3]; const double g = GS[i % 3]; const Row& r = rows[i];
            A.o << fmt(" %-16s %3.0f  |        %6.1f %6.1f %5.2f |       %6.1f %6.1f %5.2f |            %6.1f %5.2f\n", v.name, g, r.a.pkDb, r.a.rmsDb, r.a.grDb, r.b.pkDb, r.b.rmsDb, r.b.grDb, r.c.pkDb, r.c.grDb);
            CHECK(A, r.a.pkDb <= -7.5, "%s g%.0f: -12 dBFS DI peaks at <= -7.5 dBFS with default knobs (%.1f)", v.name, g, r.a.pkDb);
            CHECK(A, r.a.grDb < 0.01, "%s g%.0f: limiter idle for a -12 dBFS DI (GR %.2f dB)", v.name, g, r.a.grDb);
            // (limits were 0.5 / 1.0 dB before the Dock got its +6 dB output level; the limiter now catches a bit more of a hot DI at max GAIN)
            CHECK(A, r.b.grDb < 2.0, "%s g%.0f: limiter essentially idle for a hot -3 dBFS DI (GR %.2f dB)", v.name, g, r.b.grDb);
            CHECK(A, r.c.grDb < 0.5, "%s g%.0f: limiter idle for a hot drop-D chord (GR %.2f dB)", v.name, g, r.c.grDb);
        }
    }

    //------------------------------------------------------------------ B: MASTER
    B.o << "\n== B. MASTER sweep at GAIN 5 / 10 (DI -12 dBFS): rms dBFS, peak dBFS, GR dB ==\n master |";
    const float MS[] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10 };
    {
        for (auto& v : VS) B.o << fmt(" %-15s |", v.name);
        B.o << "\n";
        vector<Res> res(6 * 2 * 11);
        parallel(6 * 2 * 11, [&](int i) { const int vi = i / 22, gi = (i / 11) % 2, mi = i % 11; EngineParams p = mk(VS[vi], gi ? 10 : 5); setMaster(p, MS[mi]); res[i] = run(p, c12); });
        for (int gi = 0; gi < 2; ++gi) {
            B.o << fmt(" -- GAIN %d --\n", gi ? 10 : 5);
            for (int mi = 0; mi < 11; ++mi) {
                B.o << fmt("  %4.0f  |", MS[mi]);
                for (int vi = 0; vi < 6; ++vi) { const Res& r = res[vi * 22 + gi * 11 + mi]; B.o << fmt(" %6.1f %5.1f %4.1f |", r.rmsDb, r.pkDb, r.grDb); }
                B.o << "\n";
            }
        }
        for (int vi = 0; vi < 6; ++vi) for (int gi = 0; gi < 2; ++gi) {
            const double g = gi ? 10 : 5; auto R = [&](int mi) -> const Res& { return res[vi * 22 + gi * 11 + mi]; };
            CHECK(B, R(0).rmsDb < -80.0, "%s g%.0f: MASTER 0 is silent (%.1f dBFS)", VS[vi].name, g, R(0).rmsDb);
            for (int mi = 1; mi <= 10; ++mi) CHECK(B, R(mi).rmsDb >= R(mi - 1).rmsDb - 0.8, "%s g%.0f: level never falls as MASTER rises (%d->%d)", VS[vi].name, g, mi - 1, mi);
            for (int mi = 2; mi <= 10; ++mi) CHECK(B, R(mi).rmsDb - R(mi - 1).rmsDb < (mi == 2 ? 12.5 : 9.0), "%s g%.0f: no level cliff between MASTER %d and %d (%.1f dB)", VS[vi].name, g, mi - 1, mi, R(mi).rmsDb - R(mi - 1).rmsDb);
            const double rise = R(10).rmsDb - R(5).rmsDb;
            CHECK(B, rise >= 1.0 && rise <= 10.0, "%s g%.0f: MASTER 5 -> 10 raises level by 1..10 dB (%.1f)", VS[vi].name, g, rise);
            for (int mi = 0; mi <= 7; ++mi) CHECK(B, R(mi).grDb < 0.05, "%s g%.0f: limiter idle up to MASTER 7 (m%d GR %.2f)", VS[vi].name, g, mi, R(mi).grDb);
            CHECK(B, R(10).grDb < 3.0, "%s g%.0f: MASTER 10 needs < 3 dB of limiting (%.2f)", VS[vi].name, g, R(10).grDb);
            CHECK(B, R(10).pkDb <= engine::SafetyLimiter::CEILING_DB + 0.01, "%s g%.0f: MASTER 10 stays under the ceiling (%.2f)", VS[vi].name, g, R(10).pkDb);
        }
    }

    //------------------------------------------------------------------ C / D: CAB LEVEL, OUT TRIM
    for (int which = 0; which < 2; ++which) {
        Log& L = which ? D : C;
        L.o << fmt("\n== %c. %s sweep (GAIN 5, MASTER 5, DI -12 dBFS): measured change vs set change, peak, GR ==\n", which ? 'D' : 'C', which ? "OUT TRIM" : "CAB LEVEL");
        const float DB[] = { -12, -6, 0, 3, 6, 9, 12 };
        vector<Res> res(6 * 7);
        parallel(42, [&](int i) { const int vi = i / 7, di = i % 7; EngineParams p = mk(VS[vi], 5); if (which) p.outputTrimDb = DB[di]; else p.cab[0].levelDb = DB[di]; res[i] = run(p, c12); });
        for (int vi = 0; vi < 6; ++vi) {
            L.o << fmt(" %-16s", VS[vi].name);
            const Res& r0 = res[vi * 7 + 2];
            for (int di = 0; di < 7; ++di) { const Res& r = res[vi * 7 + di]; L.o << fmt(" %+3.0f:%+5.1f/%5.1f/%.1f", DB[di], r.rmsDb - r0.rmsDb, r.pkDb, r.grDb); }
            L.o << "\n";
            for (int di = 0; di < 7; ++di) {
                const Res& r = res[vi * 7 + di];
                if (r.grDb < 0.01) CHECK(L, fabs((r.rmsDb - r0.rmsDb) - DB[di]) < 0.3, "%s: %s %+.0f dB changes the level by %+.2f dB (pure gain)", VS[vi].name, which ? "OUT TRIM" : "CAB LEVEL", DB[di], r.rmsDb - r0.rmsDb);
                if (DB[di] <= 6) CHECK(L, r.grDb < 0.05, "%s: %s %+.0f dB does not touch the limiter (GR %.2f)", VS[vi].name, which ? "OUT TRIM" : "CAB LEVEL", DB[di], r.grDb);
                CHECK(L, r.pkDb <= engine::SafetyLimiter::CEILING_DB + 0.01, "%s: %s %+.0f dB stays under the ceiling (%.2f)", VS[vi].name, which ? "OUT TRIM" : "CAB LEVEL", DB[di], r.pkDb);
            }
        }
    }

    //------------------------------------------------------------------ E: IN TRIM
    E.o << "\n== E. IN TRIM sweep (GAIN 5, DI -12 dBFS): rms change / peak / GR ==\n";
    {
        const float DB[] = { -12, -6, 0, 6, 12, 18, 24 };
        vector<Res> res(6 * 7);
        parallel(42, [&](int i) { const int vi = i / 7, di = i % 7; EngineParams p = mk(VS[vi], 5); p.inputTrimDb = DB[di]; res[i] = run(p, c12); });
        for (int vi = 0; vi < 6; ++vi) {
            E.o << fmt(" %-16s", VS[vi].name);
            for (int di = 0; di < 7; ++di) { const Res& r = res[vi * 7 + di]; E.o << fmt(" %+3.0f:%+5.1f/%5.1f/%.1f", DB[di], r.rmsDb - res[vi * 7 + 2].rmsDb, r.pkDb, r.grDb); }
            E.o << "\n";
            for (int di = 0; di < 5; ++di) CHECK(E, res[vi * 7 + di].grDb < 1.5, "%s: IN TRIM %+.0f dB does not drive the limiter (GR %.2f)", VS[vi].name, DB[di], res[vi * 7 + di].grDb);
            for (int di = 0; di < 7; ++di) CHECK(E, res[vi * 7 + di].pkDb <= engine::SafetyLimiter::CEILING_DB + 0.01, "%s: IN TRIM %+.0f dB stays under the ceiling", VS[vi].name, DB[di]);
        }
    }

    //------------------------------------------------------------------ F: everything up
    F.o << "\n== F. everything up: MASTER 10, CAB +12, OUT +12, GAIN 10, hot DI (-3 dBFS) ==\n";
    {
        vector<Res> res(6);
        parallel(6, [&](int i) { EngineParams p = mk(VS[i], 10); setMaster(p, 10); p.cab[0].levelDb = 12; p.outputTrimDb = 12; res[i] = run(p, c3); });
        for (int i = 0; i < 6; ++i) {
            F.o << fmt(" %-16s peak %.2f dBFS  GR max %.1f dB  clamp hits %d  DC %.1e\n", VS[i].name, res[i].pkDb, res[i].grDb, res[i].flatRuns, res[i].dc);
            CHECK(F, res[i].pkDb <= engine::SafetyLimiter::CEILING_DB + 0.01, "%s: worst case stays under the limiter ceiling (%.2f)", VS[i].name, res[i].pkDb);
            CHECK(F, res[i].flatRuns < 8, "%s: worst case is limited, not hard-clipped (clamp hits %d)", VS[i].name, res[i].flatRuns);
            CHECK(F, fabs(res[i].dc) < 0.002, "%s: no DC offset (%.4f)", VS[i].name, res[i].dc);
        }
    }

    //------------------------------------------------------------------ G: limiter quality
    G.o << "\n== G. limiter quality: 220 Hz sine, THD of the limiter alone (Limiter.h, steady state) vs the old tanh clipper ==\n";
    {
        for (double overDb : { 1.0, 3.0, 6.0, 12.0 }) {
            engine::SafetyLimiter lim; lim.prepare(SR);
            const double amp = pow(10.0, (engine::SafetyLimiter::CEILING_DB + overDb) / 20.0);
            const int N = 48000; vector<float> y(N), z(N);
            for (int n = 0; n < N; ++n) { float x = (float) (amp * sin(2 * M_PI * 220.0 * n / SR)), xr = x; lim.process(x, xr); y[n] = x; z[n] = dsp_util::softLimit((float) (amp * sin(2 * M_PI * 220.0 * n / SR))); }
            auto thdOf = [&](const vector<float>& v, int lag) { double fund = 0, har = 0; for (int k = 1; k <= 10; ++k) { std::complex<double> s = 0; for (int n = 24000; n < 48000; ++n) s += (double) v[n] * std::polar(1.0, -2 * M_PI * 220.0 * k * (n - lag) / SR); double m = abs(s) / 24000 * 2; if (k == 1) fund = m; else har += m * m; } return 100 * sqrt(har) / fund; };
            double pk = 0; for (int n = 0; n < N; ++n) pk = max(pk, (double) fabs(y[n]));
            const double tl = thdOf(y, lim.lookahead()), ts = thdOf(z, 0);
            G.o << fmt("  %4.0f dB over the ceiling: limiter THD %.3f %%, old soft clipper THD %.2f %%, peak %.4f\n", overDb, tl, ts, pk);
            CHECK(G, tl < (overDb <= 6.0 ? 1.0 : 2.5), "limiter THD < %.1f%% at %.0f dB over (%.3f)", overDb <= 6.0 ? 1.0 : 2.5, overDb, tl);
            CHECK(G, ts < 0.05 || tl < ts * 0.25, "limiter is >= 4x cleaner than the old tanh clipper at %.0f dB over (%.3f vs %.2f)", overDb, tl, ts);
            CHECK(G, pk <= pow(10.0, engine::SafetyLimiter::CEILING_DB / 20.0) + 1e-6, "limiter never exceeds the ceiling at %.0f dB over (%.5f)", overDb, pk);
        }
        // transparency below the ceiling: bit-exact after the look-ahead delay
        engine::SafetyLimiter lim; lim.prepare(SR); double maxErr = 0; vector<float> ring(4096);
        for (int n = 0; n < 20000; ++n) { float x = (float) (0.6 * sin(2 * M_PI * 440.0 * n / SR) * sin(2 * M_PI * 3.0 * n / SR)), xr = x; ring[n & 4095] = x; lim.process(x, xr); if (n > lim.lookahead()) maxErr = max(maxErr, (double) fabs(x - ring[(n - lim.lookahead()) & 4095])); }
        G.o << fmt("  below the ceiling: max deviation from the delayed input %.2e\n", maxErr);
        CHECK(G, maxErr < 1e-6, "limiter is transparent below the ceiling (%.2e)", maxErr);

        // TRUE peak: an inter-sample peak is what a DAW meter / converter sees after oversampling.  Worst case for a sample-peak
        // limiter is a tone near fs/4 whose samples land at 45 degrees (sample peak 0.71 of the real peak).
        for (double f : { 11900.0, 9000.0, 15000.0, 3000.0 }) for (double overDb : { 3.0, 9.0 }) {
            engine::SafetyLimiter lm; lm.prepare(SR);
            const double amp = pow(10.0, (engine::SafetyLimiter::CEILING_DB + overDb) / 20.0);
            const int N = 24000; vector<float> y(N);
            for (int n = 0; n < N; ++n) { float x = (float) (amp * sin(2 * M_PI * f * n / SR + 0.785398)), xr = x; lm.process(x, xr); y[n] = x; }
            double tp = 0;
            for (int n = 12000; n < N - 40; ++n) for (int fr = 0; fr < 8; ++fr) {       // 8x windowed-sinc (Hann, 32 taps)
                const double ph = fr / 8.0; double acc = 0;
                for (int k = -16; k < 16; ++k) { const double t = k - ph, w = 0.5 + 0.5 * cos(M_PI * (k - ph) / 17.0); acc += y[n + k] * (fabs(t) < 1e-9 ? 1.0 : sin(M_PI * t) / (M_PI * t)) * w; }
                tp = max(tp, fabs(acc)); }
            const double tpDb = 20 * log10(tp);
            G.o << fmt("  %6.0f Hz, %.0f dB over: true peak %.2f dBFS (ceiling %.1f)\n", f, overDb, tpDb, engine::SafetyLimiter::CEILING_DB);
            CHECK(G, tpDb <= engine::SafetyLimiter::CEILING_DB + 0.4, "limiter TRUE peak at %.0f Hz, %.0f dB over: %.2f dBFS stays within 0.4 dB of the ceiling", f, overDb, tpDb);
        }
    }

    //------------------------------------------------------------------ H: zipper / click test while sweeping
    H.o << "\n== H. zipper/click test: control swept during a sustained note; worst 1 ms burst above 8 kHz vs the steady reference ==\n";
    {
        struct Sweep { const char* name; std::function<void(EngineParams&, double)> set; double lo, hi; };
        const Sweep sw[] = {
            { "MASTER 2 -> 9",       [](EngineParams& p, double t) { setMaster(p, (float) (2 + 7 * t)); }, 0, 1 },
            { "CAB LEVEL -12 -> +6", [](EngineParams& p, double t) { p.cab[0].levelDb = (float) (-12 + 18 * t); }, 0, 1 },
            { "OUT TRIM -12 -> +3",  [](EngineParams& p, double t) { p.outputTrimDb = (float) (-12 + 15 * t); }, 0, 1 },
            { "IN TRIM -12 -> +12",  [](EngineParams& p, double t) { p.inputTrimDb = (float) (-12 + 24 * t); }, 0, 1 },
            { "GAIN 1 -> 10",        [](EngineParams& p, double t) { p.a1Channel[1].gain = p.a2ChannelCtl[1].gain = (float) (1 + 9 * t); }, 0, 1 },
            { "MIC LEVEL -12 -> 0",  [](EngineParams& p, double t) { p.cab[0].mic[0].levelDb = (float) (-12 + 12 * t); }, 0, 1 },
            { "MIC POSITION 0 -> 1", [](EngineParams& p, double t) { p.cab[0].mic[0].position = (float) t; }, 0, 1 },
            { "MIC DISTANCE 0 -> 1", [](EngineParams& p, double t) { p.cab[0].mic[0].distance = (float) t; }, 0, 1 },
            { "MIC PAN -1 -> +1",    [](EngineParams& p, double t) { p.cab[0].mic[0].pan = (float) (-1 + 2 * t); }, 0, 1 } };
        const int NS = (int) (sizeof(sw) / sizeof(sw[0]));
        struct Out { double sweepBurst, refBurst; };
        vector<Out> res((size_t) NS * 2);
        parallel(NS * 2, [&](int idx) {
            const int si = idx / 2, amp = idx % 2; const Sweep& s = sw[si];
            EngineParams p = amp == 0 ? dumb(1, 4) : dock(1, 4);
            const int N = (int) (3.0 * SR); vector<float> in(N), refIn;
            for (int n = 0; n < N; ++n) in[n] = (float) (0.25 * (sin(2 * M_PI * 220.0 * n / SR) + 0.4 * sin(2 * M_PI * 660.0 * n / SR)));
            auto runSweep = [&](bool sweepOn, double fixedT) {
                AmpEngine e; e.setParams(p); e.prepare(SR, 512); vector<float> l(N), r(N);
                for (int pos = 0; pos < N; pos += 512) {
                    EngineParams q = p; const double t = sweepOn ? std::min(1.0, std::max(0.0, (pos / SR - 0.8) / 1.6)) : fixedT; s.set(q, t); e.setParams(q);
                    e.process(&in[pos], &l[pos], &r[pos], std::min(512, N - pos)); }
                return l; };
            auto ys = runSweep(true, 0), y0 = runSweep(false, 0), y1 = runSweep(false, 1);
            res[idx] = { hfBurst(ys, (size_t) (0.8 * SR), (size_t) (2.5 * SR)), std::max(hfBurst(y0, (size_t) (0.8 * SR), (size_t) (2.5 * SR)), hfBurst(y1, (size_t) (0.8 * SR), (size_t) (2.5 * SR))) };
        });
        for (int si = 0; si < NS; ++si) for (int amp = 0; amp < 2; ++amp) {
            const Out& o = res[(size_t) si * 2 + amp];
            H.o << fmt("  %-22s %s  sweep burst %.2e  steady max %.2e  ratio %.2f\n", sw[si].name, amp ? "Dock " : "Dumb ", o.sweepBurst, o.refBurst, o.sweepBurst / (o.refBurst + 1e-12));
            CHECK(H, o.sweepBurst <= 2.0 * o.refBurst + 2e-5, "%s (%s): no zipper/click above the steadier settings (ratio %.2f)", sw[si].name, amp ? "Dock" : "Dumb", o.sweepBurst / (o.refBurst + 1e-12));
        }
    }

    //------------------------------------------------------------------ I: idle
    I.o << "\n== I. idle noise / DC with everything up ==\n";
    for (int vi = 0; vi < 6; ++vi) {
        EngineParams p = mk(VS[vi], 10); setMaster(p, 10); p.cab[0].levelDb = 12; p.outputTrimDb = 12; p.a1Reverb = p.a2Reverb = 10;
        vector<float> z((size_t) (2 * SR), 0.f); Res r = run(p, z);
        I.o << fmt("  %-16s idle rms %.0f dBFS  peak %.0f dBFS\n", VS[vi].name, r.rmsDb, r.pkDb);
        CHECK(I, r.rmsDb < -85.0, "%s: idle rms below -85 dBFS even with everything up (%.1f)", VS[vi].name, r.rmsDb);
    }

    for (Log* l : { &A, &B, &C, &D, &E, &F, &G, &H, &I }) { printf("%s", l->o.str().c_str()); gFails += l->fails; gChecks += l->checks; }
    printf("\n%s  (%d of %d checks failed)\n", gFails ? "GAIN-STAGING AUDIT FAILED" : "GAIN-STAGING AUDIT PASSED", gFails, gChecks);
    return gFails ? 1 : 0;
}
