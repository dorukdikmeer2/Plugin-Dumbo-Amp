#pragma once
#include "Amps/Amp001Model.h"
#include "Amps/Amp002Model.h"
#include "Cab/CabinetSim.h"
#include "Common/DspUtil.h"
#include "Engine/EngineParams.h"
#include "Engine/Limiter.h"
#include "Engine/NoiseGate.h"
#include "Engine/ToneContour.h"
#include <vector>
#include <atomic>

//==============================================================================
// AmpEngine — everything between the host's audio buffer and the host's output,
// with no JUCE dependency (so it can be unit-tested headless).
//
//   mono in -> input trim -> [AMP-001 only: noise gate] -> oversampled amp model
//           -> DC block -> tone contour (extra bass/mid/treble range) -> duck/mute envelope -> cabinet (1 or 2 mics, stereo)
//           -> cab level -> output trim -> DC block -> look-ahead safety limiter -> DC block
//
// Gain staging (see docs/GAIN_STAGING.md): the amp output is scaled so that, with
// every control at its default, a normal guitar DI peaks around -9 dBFS or lower; MASTER,
// CAB LEVEL and OUT TRIM then have several dB of headroom before the limiter,
// which only ever *turns the signal down* - it never clips it.
//
// Safety features
//   * all params smoothed, structural switches (mode, channel, heater, power
//     tube, active amp, standby) are applied while the output is ducked
//   * NaN / Inf / runaway detection: the amp is reset and the output muted
//   * 50 ms fade-in after prepare / reset / fault / amp switch
//   * no allocation, locks or string lookups in process() (cabinet IR renders
//     use buffers that were allocated in prepare())
//==============================================================================
class AmpEngine {
public:
    static constexpr double JACK_VOLTS_PER_FS = 2.0;     // 0 dBFS DI = 2 V peak at the jack
    static constexpr int    CHUNK = 32;

    // forceOs (tests only): 0 = automatic (4x/2x/1x by sample rate), otherwise 1, 2 or 4
    void prepare(double sampleRate, int maxBlockSize, int forceOs = 0);
    void reset();
    int  getLatencySamples() const { return ampLatency() + limiter.lookahead(); }
    int  getOversampling() const { return osFactor; }

    void setParams(const EngineParams& p) { target = p; }
    // mono in, stereo out
    void process(const float* in, float* outL, float* outR, int numSamples) noexcept;

    // diagnostics (read from the message thread)
    float rawInputPeak() const  { return rawPeak.load(std::memory_order_relaxed); }
    float outputPeak() const    { return outPeak.load(std::memory_order_relaxed); }
    int   faultCount() const    { return faults.load(std::memory_order_relaxed); }
    // number of samples the final safety clamp had to cut by more than 0.1 dB since prepare() (0 = the limiter did all the work; test diagnostic)
    long long clampHits() const { return clampHitCount.load(std::memory_order_relaxed); }
    float limiterGainReductionDb() const { return limiterGr.load(std::memory_order_relaxed); }   // max over the last block
    float gateGainDb() const    { return gateDb.load(std::memory_order_relaxed); }

private:
    struct Sm { double cur = 0, tgt = 0, applied = -1e9; };
    int  ampLatency() const { return osFactor == 4 ? dsp_util::Oversampler4x::LATENCY : osFactor == 2 ? dsp_util::Oversampler2x::LATENCY : 0; }
    void applyStructural(const EngineParams& p, bool resetIncoming);
    void applyAllContinuous();
    void smoothTargets(const EngineParams& t, bool snap);
    void updateCabMix(const EngineParams& t);
    static bool structuralDiffers(const EngineParams& a, const EngineParams& b);
    void faultReset();

    double sr = 48000.0;
    int osFactor = 4;                       // 4x at 44.1/48 kHz, 2x at 88.2/96 kHz, 1x above (see prepare())
    dsp_util::Oversampler2x os;
    dsp_util::Oversampler4x os4;
    dsp_util::DcBlocker dc, dcOutL, dcOutR, dcOutPostL, dcOutPostR;
    Amp001Model a1;
    Amp002Model a2;
    CabinetSim cab;
    engine::NoiseGate gate;
    engine::ToneContour contour;
    engine::SafetyLimiter limiter;

    EngineParams target, applied;
    // smoothed continuous controls (the per-channel ones follow whichever channel is currently active)
    Sm a1Gain, a1Char, a1Pres, a1Master, a1Rev, a1Bass, a1Mid, a1Treb, a1EQ[6];
    Sm a2Gain, a2Bass, a2Mid, a2Treb, a2Master, a2Pres, a2Rev;
    Sm cabLvl, inTrim, outTrim;
    Sm micPos[2], micDist[2], micLvl[2], micPan[2], micOn[2];
    double smK = 0.1;

    double duck = 0.0, duckTarget = 1.0;
    bool pendingStructural = false;
    double inGainLin = 1.0, outGainLin = 1.0, cabGainLin = 1.0;
    std::vector<float> scratch, dryScratch, cabL, cabR;
    double cabMix = 1.0;

    std::atomic<long long> clampHitCount { 0 };
    std::atomic<float> rawPeak { 0.f }, outPeak { 0.f }, limiterGr { 0.f }, gateDb { 0.f };
    std::atomic<int> faults { 0 };
};
