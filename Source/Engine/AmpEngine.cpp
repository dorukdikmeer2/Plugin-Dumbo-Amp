#include "Engine/AmpEngine.h"
using namespace dsp_util;

namespace {
// Fixed gain between the (normalised) power-amp output and the cabinet.  Together with the calibration
// tables (Amps/Calibration.h) it sets the output level: see docs/GAIN_STAGING.md.
constexpr float kAmpOutScale = 0.7079458f;      // -3 dB: every voice peaks 3 dB lower (the limiter ceiling is -3 dBFS now); see docs/GAIN_STAGING.md

inline int clampIdx(int v, int hi) { return v < 0 ? 0 : (v > hi ? hi : v); }
}

bool AmpEngine::structuralDiffers(const EngineParams& a, const EngineParams& b)
{
    return a.activeAmp != b.activeAmp || a.a1Mode != b.a1Mode || a.a1PowerTube != b.a1PowerTube
        || a.a2Channel != b.a2Channel || a.a2Heater != b.a2Heater;
}

void AmpEngine::prepare(double sampleRate, int maxBlockSize, int forceOs)
{
    sr = sampleRate;
    osFactor = forceOs ? forceOs : (sr <= 52000.0 ? 4 : (sr <= 100000.0 ? 2 : 1));
    const double internal = sr * osFactor;
    a1.prepare(internal);
    a2.prepare(internal);
    cab.prepare(sr);
    gate.prepare(sr);
    limiter.prepare(sr);
    contour.prepare(sr);
    dc.prepare(sr, 20.0);
    dcOutL.prepare(sr, 12.0);
    dcOutR.prepare(sr, 12.0);
    dcOutPostL.prepare(sr, 8.0);
    dcOutPostR.prepare(sr, 8.0);
    const size_t cap = (size_t) std::max(maxBlockSize, CHUNK) + CHUNK;
    scratch.assign(cap, 0.0f); dryScratch.assign(cap, 0.0f); cabL.assign(cap, 0.0f); cabR.assign(cap, 0.0f);
    cabMix = target.cabOff ? 0.0 : 1.0;
    smK = 1.0 - std::exp(-(double) CHUNK / (0.012 * sr));        // ~12 ms smoothing

    applied = target;
    applyStructural(applied, false);
    smoothTargets(target, true);                                  // snap every smoothed control to its target
    for (Sm* s : { &a1Gain, &a1Char, &a1Pres, &a1Master, &a1Rev, &a1Bass, &a1Mid, &a1Treb, &a2Gain, &a2Bass, &a2Mid,
                   &a2Treb, &a2Master, &a2Pres, &a2Rev })
        s->applied = -1e9;
    for (auto& s : a1EQ) s.applied = -1e9;
    cab.setCabinet(applied.activeCab);
    for (int i = 0; i < kNumMics; ++i) {
        const MicSettings& m = applied.cab[clampIdx(applied.activeCab, 1)].mic[i];
        cab.setMic(i, m.type, (float) micPos[i].cur, (float) micDist[i].cur, i == 0 || m.enabled);
    }
    updateCabMix(target);
    cab.prepare(sr);
    cab.setCabinet(applied.activeCab);
    for (int i = 0; i < kNumMics; ++i) {
        const MicSettings& m = applied.cab[clampIdx(applied.activeCab, 1)].mic[i];
        cab.setMic(i, m.type, (float) micPos[i].cur, (float) micDist[i].cur, i == 0 || m.enabled);
    }
    cab.forceRender();
    updateCabMix(target);
    applyAllContinuous();
    inGainLin  = dbToGain(inTrim.cur);
    outGainLin = dbToGain(outTrim.cur);
    cabGainLin = dbToGain(cabLvl.cur);
    duck = 0.0; duckTarget = 1.0; pendingStructural = false;
    os.reset(); os4.reset(); dc.reset(); dcOutL.reset(); dcOutR.reset(); dcOutPostL.reset(); dcOutPostR.reset(); gate.reset(); limiter.reset(); contour.reset();
}

void AmpEngine::reset()
{
    a1.reset(); a2.reset(); cab.reset(); os.reset(); os4.reset(); dc.reset(); dcOutL.reset(); dcOutR.reset(); dcOutPostL.reset(); dcOutPostR.reset();
    gate.reset(); limiter.reset(); contour.reset();
    duck = 0.0; duckTarget = 1.0;
}

void AmpEngine::faultReset()
{
    a1.reset(); a2.reset(); os.reset(); os4.reset(); dc.reset(); dcOutL.reset(); dcOutR.reset(); dcOutPostL.reset(); dcOutPostR.reset(); gate.reset();
    duck = 0.0;
    faults.fetch_add(1, std::memory_order_relaxed);
}

void AmpEngine::applyStructural(const EngineParams& p, bool resetIncoming)
{
    a1.setMode(p.a1Mode);
    a1.setPowerTube(p.a1PowerTube);
    a2.setChannel(p.a2Channel);
    a2.setHeater(p.a2Heater);
    if (resetIncoming) { if (p.activeAmp == 0) a1.reset(); else a2.reset(); gate.reset(); }
}

// Moves every smoothed control one step toward its target (or onto it when `snap`).  The per-channel controls
// are taken from the channel that is *currently active* (applied.*), so a channel switch never glides the old
// channel's values into the new one.
void AmpEngine::smoothTargets(const EngineParams& t, bool snap)
{
    auto sm = [&](Sm& s, double v) {
        s.tgt = v;
        if (snap) { s.cur = v; return; }
        s.cur += (s.tgt - s.cur) * smK;
        if (std::abs(s.tgt - s.cur) < 1e-4) s.cur = s.tgt;
    };
    const ChannelControls& c1 = t.a1Channel[clampIdx(applied.a1Mode, kNumDumbChannels - 1)];
    const ChannelControls& c2 = t.a2ChannelCtl[clampIdx(applied.a2Channel, kNumDockChannels - 1)];
    sm(a1Gain, c1.gain); sm(a1Bass, c1.bass); sm(a1Mid, c1.mid); sm(a1Treb, c1.treble);
    sm(a1Char, t.a1Character); sm(a1Pres, t.a1Presence); sm(a1Master, t.a1Master); sm(a1Rev, t.a1Reverb);
    for (int i = 0; i < 6; ++i) sm(a1EQ[i], t.a1EQBypass ? 0.0 : t.a1EQ[i]);     // bypass = all bands smoothly back to flat
    sm(a2Gain, c2.gain); sm(a2Bass, c2.bass); sm(a2Mid, c2.mid); sm(a2Treb, c2.treble);
    sm(a2Master, t.a2Master); sm(a2Pres, t.a2Presence); sm(a2Rev, t.a2Reverb);

    const CabSettings& cs = t.cab[clampIdx(applied.activeCab, kNumCabs - 1)];
    for (int i = 0; i < kNumMics; ++i) {
        sm(micPos[i], cs.mic[i].position); sm(micDist[i], cs.mic[i].distance);
        sm(micLvl[i], cs.mic[i].levelDb);  sm(micPan[i], cs.mic[i].pan);
        sm(micOn[i], (i == 0 || cs.mic[i].enabled) ? 1.0 : 0.0);
    }
    sm(cabLvl, cs.levelDb);
    sm(inTrim, t.inputTrimDb); sm(outTrim, t.outputTrimDb);
}

void AmpEngine::applyAllContinuous()
{
    auto up = [](Sm& s, auto fn) { if (std::abs(s.cur - s.applied) > 1e-4) { fn(s.cur); s.applied = s.cur; } };
    if (applied.activeAmp == 0) {
        up(a1Gain,   [&](double v) { a1.setGain(v); });
        up(a1Char,   [&](double v) { a1.setCharacter(v); });
        up(a1Pres,   [&](double v) { a1.setPresence(v); });
        up(a1Master, [&](double v) { a1.setMaster(v); });
        up(a1Rev,    [&](double v) { a1.setReverb(v); });
        up(a1Bass,   [&](double v) { a1.setBass(v); });
        up(a1Mid,    [&](double v) { a1.setMid(v); });
        up(a1Treb,   [&](double v) { a1.setTreble(v); });
        for (int i = 0; i < 6; ++i) up(a1EQ[i], [&](double v) { a1.setEQBand(i, v); });
        a1.setFXLoop(applied.a1FXLoop);
    } else {
        up(a2Gain,   [&](double v) { a2.setGain(v); });
        up(a2Bass,   [&](double v) { a2.setBass(v); });
        up(a2Mid,    [&](double v) { a2.setMid(v); });
        up(a2Treb,   [&](double v) { a2.setTreble(v); });
        up(a2Master, [&](double v) { a2.setMaster(v); });
        up(a2Pres,   [&](double v) { a2.setPresence(v); });
        up(a2Rev,    [&](double v) { a2.setReverb(v); });
        a2.setFXLoop(applied.a2FXLoop);
    }
    const CabSettings& cs = target.cab[clampIdx(applied.activeCab, kNumCabs - 1)];
    for (int i = 0; i < kNumMics; ++i)
        cab.setMic(i, cs.mic[i].type, (float) micPos[i].cur, (float) micDist[i].cur, i == 0 || cs.mic[i].enabled);
}

// Level / pan / polarity of the two mics -> per-channel gains for the cabinet.
//   * pan is a BALANCE: centre = both sides at the mic level, panning only turns the opposite side down (linear taper).
//   * every output side is kept at the loudness of its LOUDEST mic.  The cab measures how the two mic IRs add in power
//     (pairStats: e0, e1 and the cross term c, which includes the comb filtering of the mic distance and the polarity), and
//     the side is scaled so that its power equals that of the louder mic alone.  Consequences:
//       - switching MIC 2 on/off, changing its level, its phase or its position never changes the loudness (only the tone)
//       - panning the mics never changes the loudness of a side (only which mic is heard where)
//       - the MIC LVL knobs are faders (they set the loudest mic and the balance), CAB LVL is the master level
void AmpEngine::updateCabMix(const EngineParams& t)
{
    const CabSettings& cs = t.cab[clampIdx(applied.activeCab, kNumCabs - 1)];
    double e0, e1, c;  cab.pairStats(e0, e1, c);
    double lvl[2], sgn[2], pg[2][2];
    for (int i = 0; i < kNumMics; ++i) {
        lvl[i] = dbToGain(micLvl[i].cur) * micOn[i].cur;
        sgn[i] = cs.mic[i].polarityInvert ? -1.0 : 1.0;
        const double pan = std::min(std::max(micPan[i].cur, -1.0), 1.0);
        pg[i][0] = std::min(1.0, 1.0 - pan);            // left
        pg[i][1] = std::min(1.0, 1.0 + pan);            // right
    }
    double side[2][2];                                  // [mic][L/R] final gains
    for (int s = 0; s < 2; ++s) {
        const double a = lvl[0] * pg[0][s] * sgn[0], b = lvl[1] * pg[1][s] * sgn[1];
        const double pa = a * a * e0, pb = b * b * e1;
        const double P = pa + pb + 2.0 * a * b * c;
        const double ref = std::max(pa, pb);
        const double k = (P > 1e-12 && ref > 1e-12) ? std::min(3.0, std::sqrt(ref / P)) : 1.0;
        side[0][s] = a * k; side[1][s] = b * k;
    }
    for (int i = 0; i < kNumMics; ++i) cab.setMix(i, (float) side[i][0], (float) side[i][1]);
}

void AmpEngine::process(const float* in, float* outL, float* outR, int numSamples) noexcept
{
    float rawPk = 0.f, outPk = 0.f, grMax = 0.f;
    int pos = 0;
    while (pos < numSamples) {
        const int n = std::min(CHUNK, numSamples - pos);
        const EngineParams t = target;                 // snapshot

        // ---- structural changes happen while the output is ducked
        if (structuralDiffers(t, applied)) { pendingStructural = true; duckTarget = 0.0; }
        bool snapAll = false;
        if (pendingStructural && duck <= 1e-3) {
            const bool ampChanged = t.activeAmp != applied.activeAmp;
            applied.activeAmp = t.activeAmp; applied.a1Mode = t.a1Mode; applied.a1PowerTube = t.a1PowerTube;
            applied.a2Channel = t.a2Channel; applied.a2Heater = t.a2Heater;
            applyStructural(applied, ampChanged);
            snapAll = true;                                       // the new channel's own knob values take effect immediately
            for (auto* s : { &a1Gain, &a1Char, &a1Pres, &a1Master, &a1Rev, &a1Bass, &a1Mid, &a1Treb, &a2Gain, &a2Bass, &a2Mid,
                             &a2Treb, &a2Master, &a2Pres, &a2Rev })
                s->applied = -1e9;
            for (auto& s : a1EQ) s.applied = -1e9;
            os.reset(); os4.reset(); dc.reset();
            pendingStructural = false;
        }
        applied.a1FXLoop = t.a1FXLoop; applied.a2FXLoop = t.a2FXLoop; applied.a2Standby = t.a2Standby;
        if (!pendingStructural)
            duckTarget = (applied.activeAmp == 1 && applied.a2Standby) ? 0.0 : 1.0;

        if (t.activeCab != applied.activeCab) {                   // each cabinet has its own mic setup: jump to it, the IR cross-fades
            applied.activeCab = t.activeCab; cab.setCabinet(t.activeCab); snapAll = true;
        }

        smoothTargets(t, snapAll);
        applyAllContinuous();
        updateCabMix(t);
        gate.setThresholdKnob(t.a1Gate);
        if (applied.activeAmp == 0) contour.setKnobs(a1Bass.cur, a1Mid.cur, a1Treb.cur); else contour.setKnobs(a2Bass.cur, a2Mid.cur, a2Treb.cur);

        // ---- gains for this chunk (linear ramps inside the chunk)
        const double ig1 = dbToGain(inTrim.cur), og1 = dbToGain(outTrim.cur), cg1 = dbToGain(cabLvl.cur);
        const double igStep = (ig1 - inGainLin) / n, ogStep = (og1 - outGainLin) / n;
        const double duckStep = (duckTarget > duck ? 1.0 / (0.040 * sr) : -1.0 / (0.008 * sr));

        float* buf = scratch.data();
        const bool useA2 = applied.activeAmp == 1;
        for (int i = 0; i < n; ++i) {
            float x = in[pos + i];
            if (!(x == x) || x > 1e3f || x < -1e3f) x = 0.0f;           // NaN / Inf / absurd
            rawPk = std::max(rawPk, std::fabs(x));
            x = std::max(-16.0f, std::min(16.0f, x));
            inGainLin += igStep;
            const double xin = (double) x * inGainLin;
            double vin = xin * JACK_VOLTS_PER_FS;
            if (!useA2) vin *= gate.process(xin);                       // AMP-001 noise gate (bit-exact bypass when the knob is at 0)

            double y;
            if (useA2) {
                if (osFactor == 4)      y = os4.process(vin, [&](double v) { return a2.process(v); });
                else if (osFactor == 2) { double u0, u1; os.up(vin, u0, u1); const double y0 = a2.process(u0); const double y1 = a2.process(u1); y = os.down(y0, y1); }
                else                    y = a2.process(vin);
            } else {
                if (osFactor == 4)      y = os4.process(vin, [&](double v) { return a1.process(v); });
                else if (osFactor == 2) { double u0, u1; os.up(vin, u0, u1); const double y0 = a1.process(u0); const double y1 = a1.process(u1); y = os.down(y0, y1); }
                else                    y = a1.process(vin);
            }
            y = dc.process(y);
            if (!(std::fabs(y) < 8.0)) { faultReset(); y = 0.0; }        // NaN or runaway (checked on the amp itself)
            y = contour.process(y);                                      // extra bass / mid / treble range (not on the schematics)
            if (!std::isfinite(y)) { faultReset(); y = 0.0; }

            duck += duckStep;
            if (duckStep > 0 && duck > duckTarget) duck = duckTarget;
            if (duckStep < 0 && duck < duckTarget) duck = duckTarget;
            const double d = duck * duck * (3.0 - 2.0 * duck);           // smoothstep
            buf[i] = (float) (y * d * kAmpOutScale);
        }
        inGainLin = ig1;

        // ---- cabinet (keeps running while bypassed so its history is valid)
        float* dry = dryScratch.data();
        std::copy(buf, buf + n, dry);
        cab.process(buf, cabL.data(), cabR.data(), n);
        const double mTarget = t.cabOff ? 0.0 : 1.0;
        const double m0 = cabMix, m1 = cabMix + (mTarget - cabMix) * 0.5;      // ~ 2-chunk cross-fade, click free
        const double cgStep = (cg1 - cabGainLin) / n;
        for (int i = 0; i < n; ++i) {
            const double m = m0 + (m1 - m0) * (i + 1) / n;
            cabGainLin += cgStep;
            const float cg = (float) cabGainLin;
            cabL[(size_t) i] = (float) (cabL[(size_t) i] * cg * m + dry[i] * (1.0 - m));
            cabR[(size_t) i] = (float) (cabR[(size_t) i] * cg * m + dry[i] * (1.0 - m));
        }
        cabGainLin = cg1;
        cabMix = std::fabs(mTarget - m1) < 1e-4 ? mTarget : m1;

        // ---- output trim, DC block, safety limiter
        for (int i = 0; i < n; ++i) {
            outGainLin += ogStep;
            float l = cabL[(size_t) i] * (float) outGainLin, r = cabR[(size_t) i] * (float) outGainLin;
            if (!(l == l)) l = 0.0f;
            if (!(r == r)) r = 0.0f;
            l = (float) dcOutL.process((double) l);                     // DC is removed BEFORE the limiter so that the limiter's
            r = (float) dcOutR.process((double) r);                     // peak detector sees the signal that will actually leave
            limiter.process(l, r);
            l = (float) dcOutPostL.process((double) l);                 // second blocker: gain modulation by the limiter on an asymmetric waveform
            r = (float) dcOutPostR.process((double) r);                 // can leave a little DC; the limiter aims below the ceiling to leave room for it
            const float cl = limiter.ceilingLin();
            if (std::fabs(l) > cl * 1.01f || std::fabs(r) > cl * 1.01f) clampHitCount.fetch_add(1, std::memory_order_relaxed);   // diagnostic: >0.1 dB over the ceiling; the limiter should make this 0
            l = std::max(-cl, std::min(cl, l));
            r = std::max(-cl, std::min(cl, r));
            outL[pos + i] = l; outR[pos + i] = r;
            outPk = std::max(outPk, std::max(std::fabs(l), std::fabs(r)));
            grMax = std::max(grMax, limiter.gainReductionDb());
        }
        outGainLin = og1;
        pos += n;
    }
    rawPeak.store(rawPk, std::memory_order_relaxed);
    outPeak.store(outPk, std::memory_order_relaxed);
    limiterGr.store(grMax, std::memory_order_relaxed);
    gateDb.store((float) gate.currentGainDb(), std::memory_order_relaxed);
}
