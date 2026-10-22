// Feature audit of the engine: per-channel controls and the noise gate (headless, no JUCE).
//   1  per-channel independence   a channel's output depends ONLY on its own gain / bass / mid / treble
//   2  channel switching          switching to a channel gives exactly the sound of that channel played on its own
//   3  noise gate                 hiss between notes is removed, notes pass, knob is monotonic, no clicks, off at 0
//   4  scope                      the gate acts on Dumb only; it adds no latency
#include "metrics.h"
#include <random>
using namespace mx;

static int failures = 0, checks = 0;
#define CHECKF(cond, ...) do { ++checks; if (!(cond)) { ++failures; std::printf("   FAIL  "); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)

static double rmsDb(const vector<float>& x, size_t a, size_t b) {
    double s = 0; for (size_t i = a; i < b && i < x.size(); ++i) s += (double) x[i] * x[i];
    return 10 * log10(s / std::max<size_t>(1, b - a) + 1e-20);
}
static double maxStep(const vector<float>& x, size_t a) { double m = 0; for (size_t i = a + 1; i < x.size(); ++i) m = max(m, (double) fabs(x[i] - x[i - 1])); return m; }

// guitar DI: a decaying chord, then silence, with a constant hiss floor under everything
static vector<float> hissAndNotes(double hissDb, float notePeak, double dur, std::mt19937& rng, size_t* noteEnd = nullptr) {
    const size_t N = (size_t) (dur * SR);
    vector<float> x(N, 0.f);
    auto note = chord(notePeak, 110.0, 1.0, 0.25);
    std::copy(note.begin(), note.end(), x.begin() + (long) (0.2 * SR));            // note at 0.2 s .. 1.2 s
    if (noteEnd) *noteEnd = (size_t) (1.2 * SR);
    std::normal_distribution<float> n(0.f, 1.f);
    const float a = (float) pow(10.0, hissDb / 20.0);
    for (auto& v : x) v += a * n(rng) * 0.577f;                                    // rms ~ a * 0.577... scaled to ~hissDb (re full scale peak ~ 1)
    return x;
}

int main() {
    std::mt19937 rng(7);

    std::printf("== 1. per-channel independence ==\n");
    {
        auto in = chord(0.3f, 110.0, 1.5);
        for (int m = 0; m < 4; ++m) {
            EngineParams ref = dumb(m, 5.0f, 4.0f, 6.0f, 3.0f);
            auto y0 = process(ref, in);
            EngineParams scrambled = ref;
            for (int o = 0; o < 4; ++o) if (o != m) scrambled.a1Channel[o] = { 10.0f, 0.0f, 10.0f, 0.0f };
            auto y1 = process(scrambled, in);
            double d = 0; for (size_t i = 0; i < y0.size(); ++i) d = max(d, (double) fabs(y0[i] - y1[i]));
            CHECKF(d == 0.0, "Dumb mode %d: changing the other three channels' controls changes nothing (max diff %.2e)", m + 1, d);
            // and each of its own four controls really does something
            const char* nm[4] = { "GAIN", "BASS", "MID", "TREBLE" };
            for (int k = 0; k < 4; ++k) {
                EngineParams q = ref; float* f[4] = { &q.a1Channel[m].gain, &q.a1Channel[m].bass, &q.a1Channel[m].mid, &q.a1Channel[m].treble };
                *f[k] = 9.0f; auto y2 = process(q, in);
                const double dd = fabs(rmsDb(y2, 24000, y2.size()) - rmsDb(y0, 24000, y0.size()));
                double diff = 0; for (size_t i = 0; i < y0.size(); ++i) diff = max(diff, (double) fabs(y2[i] - y0[i]));
                CHECKF(diff > 0.01, "Dumb mode %d: own %s changes the sound (peak diff %.3f, level diff %.2f dB)", m + 1, nm[k], diff, dd);
            }
        }
        for (int c = 0; c < 2; ++c) {
            EngineParams ref = dock(c, 5.0f, 4.0f, 6.0f, 3.0f);
            auto y0 = process(ref, in);
            EngineParams s = ref; s.a2ChannelCtl[1 - c] = { 10.0f, 0.0f, 10.0f, 0.0f };
            auto y1 = process(s, in);
            double d = 0; for (size_t i = 0; i < y0.size(); ++i) d = max(d, (double) fabs(y0[i] - y1[i]));
            CHECKF(d == 0.0, "Dock %c: changing the other channel's controls changes nothing (max diff %.2e)", 'A' + c, d);
            for (int k = 0; k < 4; ++k) {
                EngineParams q = ref; float* f[4] = { &q.a2ChannelCtl[c].gain, &q.a2ChannelCtl[c].bass, &q.a2ChannelCtl[c].mid, &q.a2ChannelCtl[c].treble };
                *f[k] = 9.0f; auto y2 = process(q, in);
                double diff = 0; for (size_t i = 0; i < y0.size(); ++i) diff = max(diff, (double) fabs(y2[i] - y0[i]));
                CHECKF(diff > 0.01, "Dock %c: control %d changes the sound (peak diff %.3f)", 'A' + c, k, diff);
            }
        }
        std::printf("  Dumb 4 channels x (isolation + 4 controls), Dock 2 channels x (isolation + 4 controls) checked\n");
    }

    std::printf("\n== 2. switching channels gives that channel's own sound ==\n");
    {
        // all four Dumb channels carry different settings; start on channel 1, switch to 3 and 4 mid-stream
        auto in = chord(0.3f, 110.0, 4.0, 3.0);
        EngineParams base; base.activeAmp = 0;
        base.a1Channel[0] = { 2, 5, 5, 5 }; base.a1Channel[1] = { 5, 7, 3, 6 }; base.a1Channel[2] = { 7, 4, 6, 5 }; base.a1Channel[3] = { 9, 3, 8, 4 };
        for (int target = 1; target < 4; ++target) {
            EngineParams start = base; start.a1Mode = 0;
            AmpEngine e; e.setParams(start); e.prepare(SR, 512);
            vector<float> l(in.size()), r(in.size());
            for (size_t i = 0; i < in.size(); i += 512) {
                if (i == (size_t) (1.0 * SR) / 512 * 512) { EngineParams sw = base; sw.a1Mode = target; e.setParams(sw); }
                int n = (int) min<size_t>(512, in.size() - i); e.process(&in[i], &l[i], &r[i], n);
            }
            EngineParams alone = base; alone.a1Mode = target;
            auto ref = process(alone, in);
            // compare the late part of the same note (after the switch settled), 3.0 .. 3.5 s
            const double a = rmsDb(l, (size_t) (3.0 * SR), (size_t) (3.5 * SR)), b = rmsDb(ref, (size_t) (3.0 * SR), (size_t) (3.5 * SR));
            CHECKF(fabs(a - b) < 0.5, "Dumb 1 -> %d: level after switching matches that channel played alone (%.2f vs %.2f dB)", target + 1, a, b);
        }
        EngineParams base2; base2.activeAmp = 1; base2.a2ChannelCtl[0] = { 3, 5, 5, 5 }; base2.a2ChannelCtl[1] = { 8, 4, 6, 7 };
        EngineParams s0 = base2; s0.a2Channel = 0;
        AmpEngine e; e.setParams(s0); e.prepare(SR, 512);
        vector<float> l(in.size()), r(in.size());
        for (size_t i = 0; i < in.size(); i += 512) {
            if (i == (size_t) (1.0 * SR) / 512 * 512) { EngineParams sw = base2; sw.a2Channel = 1; e.setParams(sw); }
            int n = (int) min<size_t>(512, in.size() - i); e.process(&in[i], &l[i], &r[i], n);
        }
        EngineParams alone = base2; alone.a2Channel = 1; auto ref = process(alone, in);
        const double a = rmsDb(l, (size_t) (3.0 * SR), (size_t) (3.5 * SR)), b = rmsDb(ref, (size_t) (3.0 * SR), (size_t) (3.5 * SR));
        CHECKF(fabs(a - b) < 0.5, "Dock A -> B: level after switching matches B played alone (%.2f vs %.2f dB)", a, b);
    }

    std::printf("\n== 3. noise gate (Dumb, Ultra channel, gain 8) ==\n");
    {
        size_t noteEnd = 0;
        auto in = hissAndNotes(-62.0, 0.25f, 3.0, rng, &noteEnd);
        const size_t quietA = (size_t) (2.2 * SR), quietB = (size_t) (3.0 * SR);        // long after the note: only hiss is left
        auto run = [&](float gate) { EngineParams p = dumb(3, 8.0f); p.a1Gate = gate; return process(p, in); };
        auto off = run(0.0f);
        const double hissOff = rmsDb(off, quietA, quietB), noteOff = rmsDb(off, (size_t) (0.25 * SR), (size_t) (0.5 * SR));
        std::printf("  gate 0 : hiss floor %.1f dBFS, note %.1f dBFS\n", hissOff, noteOff);
        double prev = hissOff;
        for (float g : { 1.0f, 2.0f, 3.0f, 4.0f, 6.0f, 8.0f, 10.0f }) {
            auto y = run(g);
            const double hiss = rmsDb(y, quietA, quietB), note = rmsDb(y, (size_t) (0.25 * SR), (size_t) (0.5 * SR));
            std::printf("  gate %-2.0f: hiss floor %6.1f dBFS (%+.1f dB)   note %.1f dBFS (%+.2f dB)\n", g, hiss, hiss - hissOff, note, note - noteOff);
            CHECKF(hiss <= prev + 0.3, "gate %.0f: more gate never lets MORE hiss through (%.1f <= %.1f)", g, hiss, prev);
            prev = std::min(prev, hiss);
            // the test hiss peaks around -55 dBFS, so the threshold (knob 4 = -62 dBFS, knob 6 = -48 dBFS, knob 8 = -43... see NoiseGate.h)
            // has to be above that before the gate closes: knob 6 must already cut clearly, knob 8 must remove it
            if (g == 6.0f) CHECKF(hiss - hissOff <= -6.0, "gate 6: hiss between notes is reduced by >= 6 dB (%.1f)", hiss - hissOff);
            if (g == 8.0f) CHECKF(hiss - hissOff <= -20.0, "gate 8: hiss between notes is reduced by >= 20 dB (%.1f)", hiss - hissOff);
            if (g <= 8.0f) CHECKF(fabs(note - noteOff) < 0.6, "gate %.0f: the played note keeps its level (%.2f dB change)", g, note - noteOff);
        }
        // attack: the first 10 ms of the note must come through (gate opens in ~1 ms)
        { auto y = run(4.0f); double pkOn = 0, pkOff = 0;
          for (size_t i = (size_t) (0.2 * SR); i < (size_t) (0.21 * SR); ++i) { pkOn = max(pkOn, (double) fabs(y[i])); pkOff = max(pkOff, (double) fabs(off[i])); }
          CHECKF(pkOn >= 0.7 * pkOff, "gate 4: note onset passes (%.3f vs %.3f without the gate)", pkOn, pkOff); }
        // release: the tail of the note fades out instead of being chopped (no jump > 6 dB between 20 ms windows while the gate closes)
        { auto y = run(6.0f); double worst = 0; double prevDb = 0; bool have = false;
          for (size_t t = (size_t) (0.9 * SR); t + 960 < (size_t) (2.0 * SR); t += 960) { const double d = rmsDb(y, t, t + 960); if (have) worst = max(worst, fabs(d - prevDb)); prevDb = d; have = true; }
          CHECKF(worst < 9.0, "gate 6: the note's tail fades out smoothly (largest 20 ms level step %.1f dB)", worst); }
        // clicks: input sweeping slowly through the threshold; the gate must not add discontinuities
        { const size_t N = (size_t) (4.0 * SR); vector<float> sw(N); std::normal_distribution<float> nn(0.f, 1.f);
          for (size_t i = 0; i < N; ++i) { const double lvl = pow(10.0, (-75.0 + 40.0 * (double) i / N) / 20.0); sw[i] = (float) (lvl * (0.6 * sin(2 * M_PI * 196.0 * i / SR) + 0.2 * nn(rng))); }
          EngineParams a = dumb(2, 6.0f), b = a; a.a1Gate = 0.0f; b.a1Gate = 5.0f;
          auto ya = process(a, sw), yb = process(b, sw);
          double ra = maxStep(ya, 4800), rb = maxStep(yb, 4800);
          CHECKF(rb <= 1.35 * ra, "gate 5 on a slow level sweep through the threshold adds no clicks (max step %.4f vs %.4f ungated)", rb, ra); }
        // knob 0 = bypass: reports no attenuation, and equals the ungated signal path exactly
        { EngineParams p = dumb(3, 8.0f); p.a1Gate = 0.0f; AmpEngine e; e.setParams(p); e.prepare(SR, 512); vector<float> l(in.size()), r(in.size()); float gmin = 0;
          for (size_t i = 0; i < in.size(); i += 512) { int n = (int) min<size_t>(512, in.size() - i); e.process(&in[i], &l[i], &r[i], n); gmin = min(gmin, e.gateGainDb()); }
          CHECKF(gmin > -0.01f, "gate 0: bypassed (attenuation %.3f dB)", gmin); }
    }

    std::printf("\n== 4. gate scope ==\n");
    {
        auto in = hissAndNotes(-62.0, 0.25f, 2.0, rng);
        EngineParams a = dock(0, 8.0f), b = a; a.a1Gate = 0.0f; b.a1Gate = 10.0f;
        auto ya = process(a, in), yb = process(b, in);
        double d = 0; for (size_t i = 0; i < ya.size(); ++i) d = max(d, (double) fabs(ya[i] - yb[i]));
        CHECKF(d == 0.0, "the GATE knob does nothing on the Dock amp (max diff %.2e)", d);
        EngineParams p = dumb(0, 5.0f); AmpEngine e0, e1; e0.setParams(p); p.a1Gate = 8.0f; e1.setParams(p); e0.prepare(SR, 512); e1.prepare(SR, 512);
        CHECKF(e0.getLatencySamples() == e1.getLatencySamples(), "the gate adds no latency (%d vs %d samples)", e0.getLatencySamples(), e1.getLatencySamples());
    }

    std::printf("== 5. tone range: BASS / MID / TREBLE are wide and monotonic on every channel ==\n");
    {
        // quiet sine (clean region) so the measurement is the linear tone response; level in dB over the steady part
        auto sine = [](double f, float pk) { vector<float> x((size_t) (0.8 * SR)); for (size_t n = 0; n < x.size(); ++n) x[n] = (float) (pk * sin(2 * M_PI * f * n / SR)); return x; };
        auto lvl = [&](EngineParams p, double f) { auto y = process(p, sine(f, 0.01f)); return rmsDb(y, (size_t) (0.4 * SR), y.size()); };
        struct V { const char* name; bool dockAmp; int ch; };
        const V vs[] = { {"Dumb Clean", false, 0}, {"Dumb Crunch", false, 1}, {"Dumb Lead", false, 2}, {"Dumb Ultra", false, 3}, {"Dock Pristine", true, 0}, {"Dock Chime", true, 1} };
        const char* knob[3] = { "BASS", "MID", "TREBLE" }; const double fr[3] = { 100.0, 700.0, 3500.0 }; const double need[3] = { 15.0, 14.0, 15.0 };
        for (const V& v : vs) for (int k = 0; k < 3; ++k) {
            double l[3];
            for (int j = 0; j < 3; ++j) {
                float kn[3] = { 5, 5, 5 }; kn[k] = j == 0 ? 0.f : (j == 1 ? 5.f : 10.f);
                EngineParams p = v.dockAmp ? dock(v.ch, 2.0f, kn[0], kn[1], kn[2]) : dumb(v.ch, 2.0f, kn[0], kn[1], kn[2]);
                l[j] = lvl(p, fr[k]);
            }
            CHECKF(l[0] < l[1] && l[1] < l[2], "%s %s is monotonic at %.0f Hz (%.1f / %.1f / %.1f dB)", v.name, knob[k], fr[k], l[0], l[1], l[2]);
            CHECKF(l[2] - l[0] >= need[k], "%s %s 0 -> 10 moves its band by >= %.0f dB (%.1f dB)", v.name, knob[k], need[k], l[2] - l[0]);
            std::printf("  %-14s %-6s @%5.0f Hz: %+6.1f dB at 0, %+6.1f dB at 10 (range %.1f dB)\n", v.name, knob[k], fr[k], l[0] - l[1], l[2] - l[1], l[2] - l[0]);
        }
    }

    std::printf("== 6. EQ bypass (Dumb six-band EQ) ==\n");
    {
        auto in = chord(0.3f, 110.0, 1.5);
        for (int m = 0; m < 4; ++m) {
            EngineParams flat = dumb(m, 5.0f), eq = flat, byp = flat;
            for (int b = 0; b < 6; ++b) { eq.a1EQ[b] = (b % 2 ? -1.0f : 1.0f); byp.a1EQ[b] = eq.a1EQ[b]; }
            byp.a1EQBypass = true;
            auto yf = process(flat, in), ye = process(eq, in), yb = process(byp, in);
            double dEq = 0, dBy = 0; for (size_t i = 0; i < yf.size(); ++i) { dEq = max(dEq, (double) fabs(yf[i] - ye[i])); dBy = max(dBy, (double) fabs(yf[i] - yb[i])); }
            CHECKF(dEq > 0.01, "Dumb mode %d: the EQ does something when engaged (max diff %.3f)", m + 1, dEq);
            CHECKF(dBy == 0.0, "Dumb mode %d: bypass with extreme EQ settings is bit-identical to a flat EQ (max diff %.2e)", m + 1, dBy);
        }
        // toggling the bypass while playing must not click
        EngineParams p = dumb(0, 5.0f); for (int b = 0; b < 6; ++b) p.a1EQ[b] = (b % 2 ? -1.0f : 1.0f);
        AmpEngine e; e.setParams(p); e.prepare(SR, 512);
        vector<float> out(in.size()), outR(in.size()); double steady = 0, during = 0;
        for (size_t i = 0, blk = 0; i + 512 <= in.size(); i += 512, ++blk) {
            p.a1EQBypass = (blk / 20) % 2 == 1; e.setParams(p);
            e.process(&in[i], &out[i], &outR[i], 512);
        }
        for (size_t i = 1; i < out.size(); ++i) { const double d = fabs(out[i] - out[i - 1]); (i > (size_t) (0.1 * SR) ? during : steady) = max(i > (size_t) (0.1 * SR) ? during : steady, d); }
        CHECKF(during < 0.3, "toggling EQ BYPASS during playback is click-free (max sample step %.3f)", during);
        CHECKF(e.clampHits() == 0, "no clamp hits while toggling EQ BYPASS");
    }

    std::printf("\n%s (%d of %d checks failed)\n", failures ? "FEATURE AUDIT FAILED" : "FEATURE AUDIT PASSED", failures, checks);
    return failures ? 1 : 0;
}
