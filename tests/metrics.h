// Shared measurement helpers for the voicing / lab / calibration tools (header-only, headless engine).
#pragma once
#include "Engine/AmpEngine.h"
#include <cstdio>
#include <complex>
#include <cmath>
#include <vector>
#include <algorithm>
namespace mx {
using std::vector; using std::min; using std::max;
static const double SR = 48000;

inline vector<float> chord(float peak, double f0 = 82.41, double dur = 2.0, double tau = 1.2) {
    int N = (int)(SR * dur); vector<float> x(N, 0.f);
    const double fr[3] = { f0, f0 * 1.4983, f0 * 2.0 };
    double m = 0;
    for (int n = 0; n < N; ++n) {
        double t = n / SR, env = (1 - exp(-t / 0.003)) * exp(-t / tau), s = 0;
        for (double f : fr) for (int h = 1; h <= 12; ++h) s += sin(2 * M_PI * f * h * t + h) / pow(h, 1.1);
        x[n] = (float)(s * env); m = max(m, fabs((double)x[n]));
    }
    for (auto& v : x) v = (float)(v / m * peak);
    return x;
}
struct Stereo { vector<float> l, r; };
inline Stereo processStereo(EngineParams p, const vector<float>& in, int forceOs = 0) {
    AmpEngine e; e.setParams(p); e.prepare(SR, 512, forceOs);
    Stereo o; o.l.resize(in.size()); o.r.resize(in.size());
    for (size_t i = 0; i < in.size(); i += 512) { int n = (int)min<size_t>(512, in.size() - i); e.process(&in[i], &o.l[i], &o.r[i], n); }
    return o;
}
// left channel of the stereo result (identical to the right one while the cabinet has a single centred mic)
inline vector<float> process(EngineParams p, const vector<float>& in, int forceOs = 0) { return processStereo(p, in, forceOs).l; }
// convenience: select a Dumb mode / Dock channel and set its own GAIN (and optional tone) in one go
inline EngineParams dumb(int mode, float gain, float bass = 5, float mid = 5, float treble = 5) {
    EngineParams p; p.activeAmp = 0; p.a1Mode = mode; p.a1Channel[mode] = { gain, bass, mid, treble }; return p; }
inline EngineParams dock(int ch, float gain, float bass = 5, float mid = 5, float treble = 5) {
    EngineParams p; p.activeAmp = 1; p.a2Channel = ch; p.a2ChannelCtl[ch] = { gain, bass, mid, treble }; return p; }
struct R { double rmsDb, pkDb, crest, fizz, flat; };
inline R level(const vector<float>& out) {
    size_t a = (size_t)(0.15 * SR), b = (size_t)(0.9 * SR);
    double rms = 0, pk = 0; for (size_t i = a; i < b; ++i) { rms += (double)out[i] * out[i]; pk = max(pk, (double)fabs(out[i])); }
    rms = sqrt(rms / (b - a));
    // fizz = share of the energy above 6 kHz (two cascaded one-pole high-passes = 12 dB/oct)
    double hf = 0, tot = 0, y1 = 0, x1 = 0, z1 = 0, w1 = 0; double k = exp(-2 * M_PI * 6000 / SR);
    for (size_t i = a; i < b; ++i) { double y = k * (y1 + out[i] - x1); x1 = out[i]; y1 = y;
        double z = k * (z1 + y - w1); w1 = y; z1 = z; hf += z * z; tot += (double)out[i] * out[i]; }
    size_t nr = 0; for (size_t i = a; i < b; ++i) if (fabs(out[i]) > 0.9 * pk) ++nr;
    return { 20 * log10(rms + 1e-12), 20 * log10(pk + 1e-12), pk / (rms + 1e-12), 10 * log10(hf / (tot + 1e-12) + 1e-12), 100.0 * nr / (b - a) };
}
inline double thd(EngineParams p, double f, float amp, int forceOs = 0) {
    int N = 48000; vector<float> in(N);
    for (int n = 0; n < N; ++n) in[n] = amp * sin(2 * M_PI * f * n / SR);
    auto out = process(p, in, forceOs);
    int s = 24000, L = 24000; double fund = 0, har = 0;
    for (int k = 1; k <= 12; ++k) { std::complex<double> a = 0; for (int n = 0; n < L; ++n) a += double(out[s + n]) * std::polar(1.0, -2 * M_PI * f * k * (s + n) / SR); double m = abs(a) / L * 2; if (k == 1) fund = m; else har += m * m; }
    return 100 * sqrt(har) / (fund + 1e-12);
}
// sine level at the output (dBFS peak of fundamental) – used for headroom
inline double fundDb(EngineParams p, double f, float amp) {
    int N = 48000; vector<float> in(N);
    for (int n = 0; n < N; ++n) in[n] = amp * sin(2 * M_PI * f * n / SR);
    auto out = process(p, in);
    std::complex<double> a = 0; int s = 24000, L = 24000;
    for (int n = 0; n < L; ++n) a += double(out[s + n]) * std::polar(1.0, -2 * M_PI * f * (s + n) / SR);
    return 20 * log10(abs(a) / L * 2 + 1e-12);
}
// Decay smoothness ("gating"): a plucked note decaying 40 dB; returns the worst *excess* level drop of the
// output over any 40 ms window, beyond what the input itself dropped (positive = the amp gates / chokes).
inline double gateExcessDb(EngineParams p, double peak = 0.3, double f = 110.0, int forceOs = 0, double* minOutTailDb = nullptr) {
    const double dur = 3.2, tau = 0.8; int N = (int)(SR * dur);
    vector<float> in(N);
    for (int n = 0; n < N; ++n) { double t = n / SR; double env = (1 - exp(-t / 0.002)) * exp(-t / tau);
        double s = 0; for (int h = 1; h <= 8; ++h) s += sin(2 * M_PI * f * h * t + 0.7 * h) / pow(h, 1.0);
        in[n] = (float)(peak * env * s / 1.9); }
    auto out = process(p, in, forceOs);
    const int W = (int)(0.02 * SR), H = (int)(0.01 * SR);
    vector<double> di, dout;
    for (int s = (int)(0.1 * SR); s + W < N; s += H) {
        double a = 0, b = 0; for (int i = 0; i < W; ++i) { a += (double)in[s + i] * in[s + i]; b += (double)out[s + i] * out[s + i]; }
        di.push_back(10 * log10(a / W + 1e-14)); dout.push_back(10 * log10(b / W + 1e-14));
    }
    double worst = -1e9; int D = 4;
    for (size_t i = 0; i + D < di.size(); ++i) {
        if (dout[i] < -75.0) break;
        double ex = (dout[i] - dout[i + D]) - (di[i] - di[i + D]);
        worst = max(worst, ex);
    }
    if (minOutTailDb) *minOutTailDb = dout.empty() ? 0 : dout.back();
    return worst;
}
// Blocking recovery: 0.12 s burst at 0 dBFS then a -30 dB tail; output level of the tail relative to the
// level it has when played alone (a stage that is blocked/choked comes back late or too low).
inline double blockingDb(EngineParams p, double f = 196.0) {
    int N = (int)(SR * 1.0); vector<float> a(N), b(N);
    for (int n = 0; n < N; ++n) { double t = n / SR; double s = sin(2 * M_PI * f * t) + 0.5 * sin(2 * M_PI * 2 * f * t + 1);
        b[n] = (float)(0.03 * s);
        a[n] = (float)((t < 0.12 ? 0.9 : 0.03) * s); }
    auto oa = process(p, a), ob = process(p, b);
    auto rms = [&](const vector<float>& o, double t0, double t1) { double s = 0; int c = 0; for (int i = (int)(t0 * SR); i < (int)(t1 * SR); ++i) { s += (double)o[i] * o[i]; ++c; } return 10 * log10(s / c + 1e-14); };
    return rms(oa, 0.20, 0.30) - rms(ob, 0.20, 0.30);
}
// Alias: share of the output energy that is NOT on a harmonic of the (bin aligned) test tone
inline double aliasDb(EngineParams p, double f, float amp, int forceOs) {
    int N = 48000; vector<float> in(N);
    for (int n = 0; n < N; ++n) in[n] = amp * sin(2 * M_PI * f * n / SR);
    auto out = process(p, in, forceOs);
    int L = 32768, s = 12000;     // exact FFT length via DFT on a coarse set is too slow: use Goertzel over all bins 100..20k in 20 Hz steps
    (void)L; (void)s;
    // direct DFT at 10 Hz resolution over a 4800-sample window (integer cycles of 10 Hz)
    const int W = 4800; double tot = 0, har = 0;
    for (int fr = 100; fr <= 20000; fr += 10) {
        std::complex<double> a = 0;
        for (int n = 0; n < W; ++n) a += double(out[24000 + n]) * std::polar(1.0, -2 * M_PI * fr * n / SR);
        double e = std::norm(a);
        tot += e;
        double q = fr / f; if (std::fabs(q - std::round(q)) * f < 25.0) har += e;
    }
    return 10 * log10((tot - har) / (tot + 1e-30) + 1e-12);
}
}
