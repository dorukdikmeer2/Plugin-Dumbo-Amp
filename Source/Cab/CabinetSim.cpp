#include "Cab/CabinetSim.h"

void CabinetSim::prepare(double sampleRate)
{
    sr = sampleRate;
    irLen = sr <= 52000.0 ? 1024 : MAX_IR;
    designer.prepare(sr, irLen);
    hist.assign((size_t) irLen * 2, 0.0f);
    hpos = 0;
    renderedCab = -1;
    minRenderInterval = (int) (0.030 * sr);                        // at most one render per 30 ms while a knob moves
    renderNow();
    for (auto& p : path) { p.cur = p.nxt; p.gL = p.tL; p.gR = p.tR; }
    fadePos = fadeLen = 0;
    cooldown = 0;
}

void CabinetSim::reset()
{
    std::fill(hist.begin(), hist.end(), 0.0f);
    hpos = 0;
    for (auto& p : path) p.cur = p.nxt;
    fadePos = fadeLen = 0;
}

void CabinetSim::setCabinet(int i) { cabinet = std::min(std::max(i, 0), 1); }

void CabinetSim::setMic(int slot, int type, float p, float d, bool enabled)
{
    MicState& m = mic[slot & 1];
    m.type = std::min(std::max(type, 0), cab::CabIrDesigner::kMicTypeCount - 1);
    m.pos  = std::min(std::max(p, 0.0f), 1.0f);
    m.dist = std::min(std::max(d, 0.0f), 1.0f);
    m.enabled = enabled;
}

void CabinetSim::setMix(int slot, float gl, float gr) { path[slot & 1].tL = gl; path[slot & 1].tR = gr; }

//------------------------------------------------------------------------------
bool CabinetSim::needsRender() const
{
    if (cabinet != renderedCab) return true;
    for (int i = 0; i < 2; ++i) {
        if (mic[i].enabled != rendered[i].enabled) return true;
        if (!mic[i].enabled) continue;
        if (mic[i].type != rendered[i].type || std::fabs(mic[i].pos - rendered[i].pos) > 0.002f
            || std::fabs(mic[i].dist - rendered[i].dist) > 0.002f) return true;
        // the other mic moving changes this mic's relative arrival time
        if (mic[0].enabled && mic[1].enabled && i == 1) { /* handled by the checks of both mics */ }
    }
    return false;
}

void CabinetSim::renderNow()
{
    // start the fade from the CURRENT blend so back-to-back changes stay smooth
    if (fadeLen > 0 && renderedCab >= 0) {
        const float t = std::min(1.0f, (float) fadePos / (float) fadeLen);
        for (auto& p : path)
            for (int i = 0; i < irLen; ++i) p.cur[(size_t) i] = p.cur[(size_t) i] * (1.0f - t) + p.nxt[(size_t) i] * t;
    } else if (renderedCab >= 0) {
        for (auto& p : path) p.cur = p.nxt;
    }

    // arrival time of each enabled mic relative to the nearest one
    double axial[2] = { 0, 0 }, minAxial = 1e9;
    for (int i = 0; i < 2; ++i) {
        if (!mic[i].enabled) continue;
        axial[i] = cab::CabIrDesigner::geometry(mic[i].type, mic[i].pos, mic[i].dist).axial;
        minAxial = std::min(minAxial, axial[i]);
    }
    for (int i = 0; i < 2; ++i) {
        std::array<float, MAX_IR>& dst = path[i].nxt;
        if (!mic[i].enabled) { std::fill(dst.begin(), dst.end(), 0.0f); dst[0] = 1.0f; rendered[i] = mic[i]; continue; }
        cab::CabIrDesigner::Request rq;
        rq.cabinet = cabinet; rq.micType = mic[i].type; rq.position = mic[i].pos; rq.distance = mic[i].dist;
        rq.delaySamples = (axial[i] - minAxial) / cab::CabIrDesigner::SPEED_OF_SOUND * sr;
        designer.design(rq, dst.data());
        rendered[i] = mic[i];
    }
    renderedCab = cabinet;
    computeStats();
    fadePos = 0; fadeLen = hist.empty() ? 0 : 512;
    cooldown = minRenderInterval;
}

//------------------------------------------------------------------------------
void CabinetSim::computeStats()
{
    constexpr int NB = 48;                                          // log-spaced frequencies, 80 Hz .. 2.5 kHz (where a miked guitar cab carries its energy)
    double e0 = 0, e1 = 0, c = 0;
    const double fmax = std::min(2500.0, 0.45 * sr);
    for (int k = 0; k < NB; ++k) {
        const double f = 80.0 * std::pow(fmax / 80.0, (k + 0.5) / NB);
        const double w = 2.0 * 3.14159265358979323846 * f / sr;
        const double cs = std::cos(w), sn = std::sin(w);
        double re0 = 0, im0 = 0, re1 = 0, im1 = 0;
        double pr = 1.0, pi_ = 0.0;                                  // e^{-j w n}
        const float* a = path[0].nxt.data();
        const float* b = path[1].nxt.data();
        for (int n = 0; n < irLen; ++n) {
            re0 += a[n] * pr; im0 += a[n] * pi_;
            re1 += b[n] * pr; im1 += b[n] * pi_;
            const double nr = pr * cs + pi_ * sn, ni = pi_ * cs - pr * sn;       // multiply by e^{-j w}
            pr = nr; pi_ = ni;
        }
        e0 += re0 * re0 + im0 * im0;  e1 += re1 * re1 + im1 * im1;  c += re0 * re1 + im0 * im1;
    }
    statE0 = std::max(e0 / NB, 1e-12);  statE1 = std::max(e1 / NB, 1e-12);  statC = c / NB;
}

float CabinetSim::convolve(const float* ir) const noexcept
{
    const float* h = &hist[(size_t) hpos];                         // h[i] = x[n - i], contiguous thanks to the doubled buffer
    float a0 = 0, a1 = 0, a2 = 0, a3 = 0, a4 = 0, a5 = 0, a6 = 0, a7 = 0;
    for (int i = 0; i < irLen; i += 8) {
        a0 += ir[i + 0] * h[i + 0]; a1 += ir[i + 1] * h[i + 1];
        a2 += ir[i + 2] * h[i + 2]; a3 += ir[i + 3] * h[i + 3];
        a4 += ir[i + 4] * h[i + 4]; a5 += ir[i + 5] * h[i + 5];
        a6 += ir[i + 6] * h[i + 6]; a7 += ir[i + 7] * h[i + 7];
    }
    return ((a0 + a1) + (a2 + a3)) + ((a4 + a5) + (a6 + a7));
}

void CabinetSim::process(const float* in, float* outL, float* outR, int numSamples) noexcept
{
    if (hist.empty()) { std::fill(outL, outL + numSamples, 0.0f); std::fill(outR, outR + numSamples, 0.0f); return; }

    int done = 0;
    while (done < numSamples) {
        const int n = std::min(32, numSamples - done);              // the effective IR is refreshed once per 32 samples

        if (cooldown > 0) cooldown -= n;
        if (cooldown <= 0 && needsRender()) renderNow();

        // IR morph: blend = (1 - t) * old + t * new, t at the middle of this slice
        const float* irUse[2];
        for (int m = 0; m < 2; ++m) {
            Path& p = path[m];
            if (fadeLen > 0) {
                const float t = std::min(1.0f, ((float) fadePos + 0.5f * (float) n) / (float) fadeLen), u = 1.0f - t;
                for (int i = 0; i < irLen; ++i) p.blend[(size_t) i] = p.cur[(size_t) i] * u + p.nxt[(size_t) i] * t;
                irUse[m] = p.blend.data();
            } else {
                irUse[m] = p.nxt.data();
            }
        }

        float stepL[2], stepR[2];
        for (int m = 0; m < 2; ++m) {
            stepL[m] = (path[m].tL - path[m].gL) / (float) std::max(1, numSamples - done);
            stepR[m] = (path[m].tR - path[m].gR) / (float) std::max(1, numSamples - done);
        }
        for (int k = 0; k < n; ++k) {
            const int i = done + k;
            hpos = (hpos == 0) ? irLen - 1 : hpos - 1;
            hist[(size_t) hpos] = in[i];
            hist[(size_t) hpos + (size_t) irLen] = in[i];
            float l = 0.0f, r = 0.0f;
            for (int m = 0; m < 2; ++m) {
                Path& p = path[m];
                p.gL += stepL[m]; p.gR += stepR[m];
                if (p.gL == 0.0f && p.gR == 0.0f && p.tL == 0.0f && p.tR == 0.0f) continue;   // mic fully off: no convolution
                const float y = convolve(irUse[m]);
                l += y * p.gL; r += y * p.gR;
            }
            outL[i] = l; outR[i] = r;
        }
        if (fadeLen > 0) {
            fadePos += n;
            if (fadePos >= fadeLen) { for (auto& p : path) p.cur = p.nxt; fadePos = fadeLen = 0; }
        }
        done += n;
    }
    for (auto& p : path) { p.gL = p.tL; p.gR = p.tR; }
}
