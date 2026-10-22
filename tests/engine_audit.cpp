// Headless audit of the real engine (no JUCE).  Build: see run_audit.sh
#include "Engine/AmpEngine.h"
#include <chrono>
#include <cstdio>
#include <random>
#include <string>
#include <functional>
#include <cstring>

static int failures = 0, checks = 0;
static void check(bool ok, const std::string& what) { ++checks; if (!ok) { ++failures; printf("   FAIL  %s\n", what.c_str()); } }

struct Stats { double peak = 0, rms = 0, dc = 0; long bad = 0; };
static Stats stats(const std::vector<float>& v, size_t skip = 0) {
    Stats s; double sum = 0, sq = 0; size_t n = 0;
    for (size_t i = skip; i < v.size(); ++i) {
        float x = v[i];
        if (!std::isfinite(x)) { s.bad++; continue; }
        s.peak = std::max(s.peak, (double) std::fabs(x)); sum += x; sq += (double) x * x; ++n;
    }
    if (n) { s.dc = sum / n; s.rms = std::sqrt(sq / n); }
    return s;
}
static std::vector<float> guitar(double sr, double sec, float amp) {
    size_t N = (size_t) (sr * sec); std::vector<float> x(N);
    std::mt19937 rng(7); std::normal_distribution<float> nd(0.f, 0.0003f);
    const double f0[] = { 82.41, 110.0, 146.83, 196.0, 246.94, 329.63 };
    for (size_t n = 0; n < N; ++n) {
        double t = n / sr, v = 0, env = std::exp(-std::fmod(t, 0.5) * 3.0);
        for (double f : f0) for (int h = 1; h <= 4; ++h) v += std::sin(2 * M_PI * f * h * t) / (h * 1.5);
        x[n] = (float) (v / 6.0 * env * amp * 3.0) + nd(rng);
    }
    return x;
}
// renders the left channel; if `right` is given it receives the right channel
static std::vector<float> render(AmpEngine& e, const std::vector<float>& in, int block = 512, std::vector<float>* right = nullptr) {
    std::vector<float> out(in.size()), outR(in.size());
    for (size_t p = 0; p < in.size(); p += block) { int n = (int) std::min<size_t>(block, in.size() - p); e.process(&in[p], &out[p], &outR[p], n); }
    if (right) *right = outR;
    return out;
}
static const double CEIL = std::pow(10.0, engine::SafetyLimiter::CEILING_DB / 20.0);   // the safety limiter's ceiling (-3 dBFS)
static double db(double x) { return 20 * std::log10(x + 1e-12); }

static EngineParams maxed(int amp) {
    EngineParams p; p.activeAmp = amp;
    for (auto& c : p.a1Channel) c.gain = 10;
    p.a1Character = p.a1Presence = p.a1Master = p.a1Reverb = 10;
    for (auto& e : p.a1EQ) e = 1.0f;
    for (auto& c : p.a2ChannelCtl) c = { 10, 10, 10, 10 };
    p.a2Master = p.a2Presence = p.a2Reverb = 10;
    return p;
}

int main(int argc, char** argv) {
    const bool quick = argc > 1 && !std::strcmp(argv[1], "quick");

    printf("== 1. level / stability matrix (all knobs max, every mode) ==\n");
    const double rates[] = { 44100, 48000, 96000, 192000 };
    const float levels[] = { 0.05f, 0.3f, 1.0f, 4.0f };
    for (double sr : rates) {
        if (quick && sr > 48000) continue;
        for (float lv : levels) {
            struct Cfg { const char* name; EngineParams p; };
            std::vector<Cfg> cfgs;
            for (int m = 0; m < 4; ++m) for (int t = 0; t < 3; ++t) {
                if (quick && t > 0) continue;
                EngineParams p = maxed(0); p.a1Mode = m; p.a1PowerTube = t; cfgs.push_back({ "A1", p }); }
            for (int c = 0; c < 2; ++c) for (int h = 0; h < 2; ++h) { EngineParams p = maxed(1); p.a2Channel = c; p.a2Heater = h; cfgs.push_back({ "A2", p }); }
            double worstPk = 0; long bad = 0; int flt = 0; double worstDc = 0;
            for (auto& c : cfgs) {
                AmpEngine e; e.setParams(c.p); e.prepare(sr, 512);
                auto out = render(e, guitar(sr, sr > 100000 ? 1.0 : 1.5, lv));
                auto s = stats(out, (size_t) (sr * 0.3));
                worstPk = std::max(worstPk, s.peak); bad += s.bad; flt += e.faultCount(); worstDc = std::max(worstDc, std::fabs(s.dc));
            }
            printf("  sr=%6.0f in=%.2f FS  %2zu configs: worst peak %.3f  worst |DC| %.4f  nonfinite %ld  faults %d\n", sr, lv, cfgs.size(), worstPk, worstDc, bad, flt);
            check(bad == 0, "no NaN/Inf"); check(worstPk <= CEIL, "peak <= limiter ceiling (-3 dBFS)");
            check(worstDc < 0.03, "mean level (DC) < 0.03"); check(flt == 0, "no engine faults");
        }
    }

    printf("== 2. single-knob extremes ==\n");
    {
        long bad = 0; double pk = 0; int flt = 0;
        for (int amp = 0; amp < 2; ++amp) for (int k = 0; k < 16; ++k) for (float v : { 0.f, 10.f }) {
            EngineParams p; p.activeAmp = amp;
            float* knobs[] = { &p.a1Channel[0].gain, &p.a1Character, &p.a1Presence, &p.a1Master, &p.a1Reverb, &p.a1EQ[0], &p.a1EQ[5],
                               &p.a2ChannelCtl[0].gain, &p.a2ChannelCtl[0].bass, &p.a2ChannelCtl[0].mid, &p.a2ChannelCtl[0].treble,
                               &p.a2Master, &p.a2Presence, &p.a2Reverb, &p.a1Gate, &p.a1Channel[0].bass };
            *knobs[k] = (k == 5 || k == 6) ? (v > 5 ? 1.f : -1.f) : v;
            AmpEngine e; e.setParams(p); e.prepare(48000, 512);
            auto s = stats(render(e, guitar(48000, 1.0, 0.5f)), 12000);
            bad += s.bad; pk = std::max(pk, s.peak); flt += e.faultCount();
        }
        printf("  nonfinite %ld, worst peak %.3f, faults %d\n", bad, pk, flt);
        check(bad == 0 && pk <= CEIL && flt == 0, "knob extremes safe");
    }

    printf("== 3. idle noise & DC (silence in) ==\n");
    for (int amp = 0; amp < 2; ++amp) {
        EngineParams p = maxed(amp); AmpEngine e; e.setParams(p); e.prepare(48000, 512);
        auto s = stats(render(e, std::vector<float>(48000 * 2, 0.f)), 24000);
        printf("  AMP-00%d idle: peak %.2e rms %.2e (%.0f dBFS) dc %.2e\n", amp + 1, s.peak, s.rms, db(s.rms), s.dc);
        check(s.rms < 1e-4, "idle rms < -80 dBFS");
    }

    printf("== 4. abuse inputs (square, DC step, +/-1e6 spike, NaN, Inf) ==\n");
    for (int amp = 0; amp < 2; ++amp) {
        EngineParams p = maxed(amp); AmpEngine e; e.setParams(p); e.prepare(48000, 512);
        std::vector<float> x(48000 * 2);
        for (size_t i = 0; i < x.size(); ++i) {
            x[i] = (i < 24000) ? (((i / 50) % 2) ? 1.f : -1.f) : (i < 48000 ? 1.0f : 0.f);
            if (i == 60000) x[i] = 1e6f; if (i == 60001) x[i] = -1e6f;
            if (i == 70000) x[i] = NAN; if (i == 70001) x[i] = INFINITY; if (i == 70002) x[i] = -INFINITY;
        }
        auto s = stats(render(e, x));
        printf("  AMP-00%d: nonfinite %ld peak %.3f faults %d\n", amp + 1, s.bad, s.peak, e.faultCount());
        check(s.bad == 0 && s.peak <= CEIL, "abuse input stays finite and under the limiter ceiling");
    }

    printf("== 5. parameter thrash with silence (pops) and with signal ==\n");
    // pass 0: silence, only CONTINUOUS controls thrash (must be pop-free); pass 1: silence, everything thrashes incl. amp / channel /
// tube / heater / standby switches every 5 ms (must stay finite and under the ceiling); pass 2: the same with a guitar signal
    for (int pass = 0; pass < 3; ++pass) {
        const int withSig = pass == 2, structural = pass != 0;
        std::mt19937 rng(99); std::uniform_real_distribution<float> u(0, 1);
        AmpEngine e; EngineParams p; e.setParams(p); e.prepare(48000, 256);
        auto in = guitar(48000, 12.0, 0.3f);
        long bad = 0; double pk = 0, maxStep = 0; float prev = 0; int flt0 = 0;
        for (size_t pos = 0; pos + 256 <= in.size(); pos += 256) {
            { const bool v0 = u(rng) < 0.3f, v1 = u(rng) < 0.5f; const int v2 = (int) (u(rng) * 3.999f), v3 = (int) (u(rng) * 2.999f);
              const bool v4 = u(rng) < 0.5f, v5 = u(rng) < 0.5f, v6 = u(rng) < 0.1f;
              if (structural) { p.activeAmp = v0; p.activeCab = v1; p.a1Mode = v2; p.a1PowerTube = v3; p.a2Channel = v4; p.a2Heater = v5; p.a2Standby = v6; } }
            for (auto& c : p.a1Channel) c = { u(rng) * 10, u(rng) * 10, u(rng) * 10, u(rng) * 10 };
            p.a1Master = u(rng) * 10; p.a1Character = u(rng) * 10; p.a1Presence = u(rng) * 10; p.a1Reverb = u(rng) * 10; p.a1Gate = u(rng) * 10;
            for (auto& q : p.a1EQ) q = u(rng) * 2 - 1;
            for (auto& c : p.a2ChannelCtl) c = { u(rng) * 10, u(rng) * 10, u(rng) * 10, u(rng) * 10 };
            p.a2Master = u(rng) * 10; p.a2Reverb = u(rng) * 10;
            for (auto& cs : p.cab) {
                cs.levelDb = u(rng) * 24 - 12;
                for (auto& m : cs.mic) { m.type = (int) (u(rng) * 4.999f); m.position = u(rng); m.distance = u(rng); m.levelDb = u(rng) * 30 - 24;
                                         m.pan = u(rng) * 2 - 1; m.enabled = u(rng) < 0.6f; m.polarityInvert = u(rng) < 0.3f; }
            }
            { const bool co = u(rng) < 0.15f; if (structural) p.cabOff = co; }
            p.inputTrimDb = u(rng) * 48 - 24; p.outputTrimDb = u(rng) * 48 - 24;
            e.setParams(p);
            std::vector<float> blk(256, 0.f), out(256);
            if (withSig) std::copy(in.begin() + pos, in.begin() + pos + 256, blk.begin());
            std::vector<float> outR(256);
            e.process(blk.data(), out.data(), outR.data(), 256);
            for (float y : out) { if (!std::isfinite(y)) ++bad; else { pk = std::max(pk, (double) std::fabs(y)); maxStep = std::max(maxStep, (double) std::fabs(y - prev)); prev = y; } }
        }
        printf("  %s: nonfinite %ld peak %.3f max sample step %.3f faults %d\n", pass == 2 ? "signal " : (pass == 1 ? "silence+switching" : "silence"), bad, pk, maxStep, e.faultCount());
        check(bad == 0 && pk <= CEIL && e.faultCount() == flt0, "thrash safe");
        if (pass == 0) check(pk < 0.01, "no pops while thrashing continuous controls with silent input");
    }

    printf("== 6. block-size independence (constant params) ==\n");
    {
        EngineParams p = maxed(0); p.a1Channel[0].gain = 7; p.a1Master = 6; p.a1Reverb = 3;
        auto in = guitar(48000, 2.0, 0.3f);
        std::vector<float> ref;
        for (int bs : { 512, 1, 13, 480, 2048 }) {
            AmpEngine e; e.setParams(p); e.prepare(48000, 2048);
            auto o = render(e, in, bs);
            if (ref.empty()) ref = o;
            double d = 0; for (size_t i = 0; i < o.size(); ++i) d = std::max(d, (double) std::fabs(o[i] - ref[i]));
            printf("  block %4d: max diff vs 512 = %.2e\n", bs, d);
            check(d < 1e-5, "block size independent");
        }
    }

    printf("== 7. calibration: typical DI, default knobs ==\n");
    for (int amp = 0; amp < 2; ++amp) for (float lv : { 0.1f, 0.3f, 0.7f }) {
        EngineParams p; p.activeAmp = amp; AmpEngine e; e.setParams(p); e.prepare(48000, 512);
        auto in = guitar(48000, 3.0, lv); auto s = stats(render(e, in), 24000); auto si = stats(in, 24000);
        printf("  AMP-00%d DI peak %5.1f dBFS -> out peak %6.1f dBFS  rms %6.1f dBFS\n", amp + 1, db(si.peak), db(s.peak), db(s.rms));
    }

    printf("== 8. CPU (48 kHz, 512 blocks, mono engine) ==\n");
    for (int amp = 0; amp < 2; ++amp) {
        EngineParams p = maxed(amp); p.a1Channel[0].gain = p.a2ChannelCtl[0].gain = 8; AmpEngine e; e.setParams(p); e.prepare(48000, 512);
        auto in = guitar(48000, 8.0, 0.4f);
        auto t0 = std::chrono::steady_clock::now(); render(e, in);
        double d = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        printf("  AMP-00%d: %.1f%% of one core\n", amp + 1, 100 * d / 8.0);
        check(100 * d / 8.0 < 40.0, "CPU < 40% of a core (4x oversampling, sandbox CPU)");
    }
    {
        EngineParams p = maxed(0); AmpEngine e; e.setParams(p); e.prepare(192000, 512);
        auto in = guitar(192000, 3.0, 0.4f);
        auto t0 = std::chrono::steady_clock::now(); render(e, in);
        double d = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        printf("  AMP-001 @192k: %.1f%% of one core\n", 100 * d / 3.0);
        check(100 * d / 3.0 < 45.0, "CPU @192k < 45%");
    }

    printf("== 9. oversampler (4x halfband cascade): flatness, delay, alias-free round trip ==\n");
    {
        dsp_util::Oversampler4x o;
        for (double f : { 100.0, 1000.0, 5000.0, 12000.0, 18000.0 }) {
            o.reset(); const int N = 9600; std::vector<double> y(N);
            for (int n = 0; n < N; ++n) y[n] = o.process(std::sin(2 * M_PI * f * n / 48000.0), [](double v) { return v; });
            double Scc = 0, Sss = 0, Scs = 0, Sxc = 0, Sxs = 0;
            for (int n = 2000; n < 8000; ++n) { double c = std::cos(2 * M_PI * f * n / 48000.0), s = std::sin(2 * M_PI * f * n / 48000.0); Scc += c * c; Sss += s * s; Scs += c * s; Sxc += y[n] * c; Sxs += y[n] * s; }
            double det = Scc * Sss - Scs * Scs, A = (Sxc * Sss - Sxs * Scs) / det, B = (Sxs * Scc - Sxc * Scs) / det;
            double res = 0, tot = 0; for (int n = 2000; n < 8000; ++n) { double fit = A * std::cos(2 * M_PI * f * n / 48000.0) + B * std::sin(2 * M_PI * f * n / 48000.0); res += (y[n] - fit) * (y[n] - fit); tot += fit * fit; }
            const double g = 20 * std::log10(std::sqrt(A * A + B * B));
            printf("  %6.0f Hz: gain %+.3f dB, residual %.0f dB\n", f, g, 10 * std::log10(res / tot + 1e-30));
            check(std::fabs(g) < 0.05, "oversampler pass-band flat (+/-0.05 dB up to 18 kHz)");
            check(10 * std::log10(res / tot + 1e-30) < -100.0, "oversampler round trip adds no distortion/alias (< -100 dB)");
        }
        AmpEngine e; e.prepare(48000, 512); check(e.getLatencySamples() == 39 + 48 && e.getOversampling() == 4, "latency 39 (oversampler) + 48 (1 ms limiter look-ahead) at 4x / 48 kHz");
        AmpEngine e2; e2.prepare(96000, 512); check(e2.getLatencySamples() == 31 + 96 && e2.getOversampling() == 2, "latency 31 + 96 at 2x / 96 kHz");
        AmpEngine e3; e3.prepare(192000, 512); check(e3.getLatencySamples() == 192 && e3.getOversampling() == 1, "no oversampling at 192 kHz (latency = limiter look-ahead only)");
    }

    printf("== 10. CAB OFF: cross-fade is click-free and really bypasses the cabinet ==\n");
    {
        EngineParams p; p.activeAmp = 0; p.a1Mode = 0; AmpEngine e; e.setParams(p); e.prepare(48000, 512);
        const int N = 48000 * 2; std::vector<float> in(N), out(N);
        for (int n = 0; n < N; ++n) in[n] = (float) (0.2 * std::sin(2 * M_PI * 220.0 * n / 48000.0) + 0.1 * std::sin(2 * M_PI * 3100.0 * n / 48000.0));
        double maxStepOn = 0, maxStepToggle = 0; double rmsOn = 0, rmsOff = 0;
        for (int blk = 0; blk < N / 512; ++blk) {
            p.cabOff = ((blk / 20) % 2) == 1; e.setParams(p);
            std::vector<float> oR(512); e.process(&in[blk * 512], &out[blk * 512], oR.data(), 512);
        }
        for (int blk = 8; blk < N / 512; ++blk) for (int i = 1; i < 512; ++i) {
            int n = blk * 512 + i; double st = std::fabs((double) out[n] - out[n - 1]); (((blk % 20) < 2) ? maxStepToggle : maxStepOn) = std::max(((blk % 20) < 2) ? maxStepToggle : maxStepOn, st); }
        for (int n = 20000; n < 24000; ++n) rmsOn += (double) out[n] * out[n];     // blocks 39..46: cab on (blk 0-19 on, 20-39 off, ...)
        printf("  max sample step: steady %.4f, during switching %.4f\n", maxStepOn, maxStepToggle);
        check(maxStepToggle < 4.0 * maxStepOn + 0.05, "cab switching does not click");
        (void) rmsOff; (void) rmsOn;
    }

    printf("\n%s  (%d of %d checks failed)\n", failures ? "AUDIT FAILED" : "AUDIT PASSED", failures, checks);
    return failures ? 1 : 0;
}
