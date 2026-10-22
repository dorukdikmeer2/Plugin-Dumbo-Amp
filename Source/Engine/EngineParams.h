#pragma once

//==============================================================================
// EngineParams — the complete, plain-data description of what the user has
// dialled in.  The plugin shell (Parameters/EngineBinding) fills one of these
// from the host parameters every block; the tests fill them directly.  No JUCE.
//==============================================================================

constexpr int kNumDumbChannels = 4;    // AMP-001 modes   : Clean / Crunch / Lead / Ultra
constexpr int kNumDockChannels = 2;    // AMP-002 channels: A Pristine / B Chime
constexpr int kNumCabs         = 2;
constexpr int kNumMics         = 2;    // a cab can be miked with two mics at once

// Each amp channel owns its own gain and tone-stack settings.
struct ChannelControls {
    float gain = 5.0f, bass = 5.0f, mid = 5.0f, treble = 5.0f;
};

// Microphone models (see Cab/MicModels.h for what each one represents).
enum MicType { kMicSM57 = 0, kMicSM58, kMicRoyer121, kMicC414, kMicMD421, kNumMicTypes };

struct MicSettings {
    int   type = kMicSM57;
    float position = 0.30f;     // 0 = dust-cap centre ... 1 = cone edge
    float distance = 0.25f;     // 0 = touching the grille ... 1 = ~1.2 m (room mic)
    float levelDb = 0.0f;
    float pan = 0.0f;           // -1 .. +1
    bool  enabled = true;       // mic 1 is always on; mic 2 is the optional second mic
    bool  polarityInvert = false;
};

// Every cabinet remembers its own microphone setup and level.
struct CabSettings {
    MicSettings mic[kNumMics];
    float levelDb = 0.0f;
    CabSettings() { mic[1].enabled = false; mic[1].type = kMicRoyer121; mic[1].distance = 0.45f; mic[1].levelDb = -3.0f; }
};

struct EngineParams {
    int   activeAmp = 0, activeCab = 0;

    // ---- AMP-001 "Dumb 1"
    int   a1Mode = 0, a1PowerTube = 0;
    ChannelControls a1Channel[kNumDumbChannels];
    float a1Character = 5, a1Presence = 5, a1Master = 5, a1Reverb = 0, a1Gate = 0;
    float a1EQ[6] = { 0, 0, 0, 0, 0, 0 };           // six-band EQ, -1 .. +1 (= +-15 dB)
    bool  a1FXLoop = false;
    bool  a1EQBypass = false;                        // true = the six-band EQ is switched out (all bands flat)

    // ---- AMP-002 "Dock"
    int   a2Channel = 0, a2Heater = 1;
    ChannelControls a2ChannelCtl[kNumDockChannels];
    float a2Master = 5, a2Presence = 5, a2Reverb = 0;
    bool  a2FXLoop = false, a2Standby = false;

    // ---- cabinets + I/O
    CabSettings cab[kNumCabs];
    bool  cabOff = false;                            // bypass the built-in cabinet (for an external IR loader)
    float inputTrimDb = 0.0f, outputTrimDb = 0.0f;

    // convenience accessors for the *selected* channel (used by the tests)
    ChannelControls& a1Sel() { return a1Channel[a1Mode < 0 ? 0 : (a1Mode > 3 ? 3 : a1Mode)]; }
    ChannelControls& a2Sel() { return a2ChannelCtl[a2Channel < 0 ? 0 : (a2Channel > 1 ? 1 : a2Channel)]; }
    CabSettings&     cabSel() { return cab[activeCab < 0 ? 0 : (activeCab > 1 ? 1 : activeCab)]; }
};
