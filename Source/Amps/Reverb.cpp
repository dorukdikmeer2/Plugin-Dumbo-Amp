#include "Reverb.h"

namespace { constexpr double kPi = 3.14159265358979323846; }

void AmpReverb::prepare(double sampleRate, Type t)
{
    sr = sampleRate; type = t;
    dec = std::max(1, (int) std::lround(sr / 24000.0));
    rate = sr / dec;
    aUp = 1.0 - std::exp(-2.0 * kPi * 9000.0 / sr);
    aPre = 1.0 - std::exp(-2.0 * kPi * 7000.0 / sr);

    if (type == Type::Room) {
        // mutually-prime-ish line lengths (seconds), ~30..90 ms
        const double len[NF] = { 0.0297, 0.0371, 0.0411, 0.0437, 0.0541, 0.0613, 0.0709, 0.0823 };
        const double rt60 = 2.3;
        for (int i = 0; i < NF; ++i) {
            fdn[i].init((int) std::lround(len[i] * rate) + 8);                  // +8: head-room for modulation
            fdnG[i] = std::pow(10.0, -3.0 * len[i] / rt60);                     // per-pass gain for the target RT60
        }
        damp   = 1.0 - std::exp(-2.0 * kPi * 4200.0 / rate);
        lowcut = 1.0 - std::exp(-2.0 * kPi * 160.0 / rate);
        modRate = 0.6 / rate; modDepth = 0.0006 * rate;                         // +-0.6 ms
        pre.len = std::max(2, (int) (0.020 * rate)); pre.buf.assign((size_t) pre.len, 0.0);
        ap1.len = std::max(2, (int) (0.0050 * rate)); ap1.buf.assign((size_t) ap1.len, 0.0);
        ap2.len = std::max(2, (int) (0.0017 * rate)); ap2.buf.assign((size_t) ap2.len, 0.0);
        outNorm = 2.6;
    } else {
        const double len[NS] = { 0.0243, 0.0311, 0.0383 };
        const double rt60 = 3.4;
        for (int i = 0; i < NS; ++i) {
            spring[i].init((int) std::lround(len[i] * rate));
            springFb[i] = std::pow(10.0, -3.0 * len[i] / rt60);
            for (int k = 0; k < NDISP; ++k) disp[i][k].a = 0.66 + 0.03 * i - 0.02 * std::sin(k * 1.7);
        }
        lpA = 1.0 - std::exp(-2.0 * kPi * 3600.0 / rate);       // transducer / spring HF loss, applied every pass
        hpA = 1.0 - std::exp(-2.0 * kPi * 160.0 / rate);        // pick-up LF loss
        outNorm = 1.6;
    }
    reset();
}

void AmpReverb::reset()
{
    d1 = d2 = acc1 = acc2 = prevOut = curOut = upLp = preLp = 0.0; phase = 0;
    for (auto& l : fdn) l.clear();
    for (auto& l : spring) l.clear();
    for (auto& s : disp) for (auto& a : s) a.z = 0.0;
    for (auto* d : { &pre, &ap1, &ap2 }) { std::fill(d->buf.begin(), d->buf.end(), 0.0); d->pos = 0; }
    for (auto& s : fdnState) s = 0.0;
    outHp = 0.0; modPhase[0] = 0.0; modPhase[1] = 0.37;
}

// one step at the internal (reduced) rate
double AmpReverb::tick(double x) noexcept
{
    if (type == Type::Room) {
        // pre-delay
        const double xd = pre.buf[(size_t) pre.pos]; pre.buf[(size_t) pre.pos] = x; if (++pre.pos >= pre.len) pre.pos = 0;
        // input diffusion (two Schroeder all-passes)
        double s = xd;
        for (Diff* d : { &ap1, &ap2 }) {
            const double dl = d->buf[(size_t) d->pos];
            const double w = s + 0.6 * dl;
            d->buf[(size_t) d->pos] = w;
            if (++d->pos >= d->len) d->pos = 0;
            s = dl - 0.6 * w;
        }
        // read the 8 lines (two of them with slow modulation -> no metallic ringing)
        double v[NF], sum = 0.0;
        for (int i = 0; i < NF; ++i) {
            Line& L = fdn[i];
            double d = (double) (L.len - 8);
            if (i < 2) {
                modPhase[i] += modRate * (1.0 + 0.31 * i); if (modPhase[i] >= 1.0) modPhase[i] -= 1.0;
                d += modDepth * std::sin(2.0 * kPi * modPhase[i]) + 4.0;
            } else d += 4.0;
            double rp = (double) L.pos - d; while (rp < 0.0) rp += L.len;
            const int i0 = (int) rp; const double fr = rp - i0; const int i1 = (i0 + 1 >= L.len) ? 0 : i0 + 1;
            v[i] = L.buf[(size_t) i0] * (1.0 - fr) + L.buf[(size_t) i1] * fr;
            L.lp += damp * (v[i] - L.lp);                                         // damping
            v[i] = L.lp * fdnG[i];
            sum += v[i];
        }
        const double h = sum * (2.0 / NF);                                         // Householder reflection
        double out = 0.0;
        for (int i = 0; i < NF; ++i) {
            Line& L = fdn[i];
            L.buf[(size_t) L.pos] = (v[i] - h) + s * 0.35;
            if (++L.pos >= L.len) L.pos = 0;
            out += (i & 1) ? -v[i] : v[i];
        }
        outHp += lowcut * (out - outHp);
        return (out - outHp) * outNorm * 0.5;
    }

    // spring: three transmission lines, each = delay -> dispersion chain -> pick-up/transducer filtering -> feedback
    double out = 0.0;
    for (int s = 0; s < NS; ++s) {
        Line& L = spring[s];
        double y = L.buf[(size_t) L.pos];
        L.lp += lpA * (y - L.lp);
        double r = L.lp;
        L.hp += hpA * (r - L.hp);
        r -= L.hp;
        for (auto& a : disp[s]) r = a.process(r);
        L.buf[(size_t) L.pos] = x * 0.6 + r * springFb[s];
        if (++L.pos >= L.len) L.pos = 0;
        out += r;
    }
    return out * outNorm * 0.58;
}

double AmpReverb::process(double x) noexcept
{
    // decimate (two cascaded boxcar averages = triangular), run, then linearly interpolate back up
    preLp += aPre * (x - preLp);
    acc1 += preLp;
    if (++phase >= dec) {
        phase = 0;
        const double a = acc1 / dec; acc1 = 0.0;
        d1 = 0.5 * (a + d2); d2 = a;
        prevOut = curOut;
        curOut = tick(d1);
    }
    const double f = (double) phase / dec;
    const double y = prevOut + (curOut - prevOut) * f;
    upLp += aUp * (y - upLp);
    return upLp;
}
