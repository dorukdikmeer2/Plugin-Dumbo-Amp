#include "Parameters/ParameterLayout.h"
#include "Parameters/ParamIDs.h"

namespace params {
namespace {
juce::ParameterID pid(const std::string& id) { return juce::ParameterID { juce::String(id), 1 }; }

using Params = std::vector<std::unique_ptr<juce::RangedAudioParameter>>;

void knob(Params& v, const std::string& id, const juce::String& name, float def, float lo = 0.0f, float hi = 10.0f, float step = 0.01f)
{
    v.push_back(std::make_unique<juce::AudioParameterFloat>(pid(id), name, juce::NormalisableRange<float>(lo, hi, step), def));
}
void choice(Params& v, const std::string& id, const juce::String& name, juce::StringArray items, int def)
{
    v.push_back(std::make_unique<juce::AudioParameterChoice>(pid(id), name, items, def));
}
void toggle(Params& v, const std::string& id, const juce::String& name, bool def)
{
    v.push_back(std::make_unique<juce::AudioParameterBool>(pid(id), name, def));
}
} // namespace

juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    Params p;

    choice(p, ids::activeAmp, "Active Amp", { "AMP-001 Dumb 1", "AMP-002 Dock" }, 0);
    choice(p, ids::activeCab, "Active Cab", { "4x12 Greenback", "1x12 Vintage 30" }, 0);

    // ---- AMP-001 "Dumb 1": four channels, each with its own gain + tone stack
    choice(p, ids::a1Mode, "A1 Mode", { "1: Clean", "2: Crunch", "3: Lead", "4: Ultra" }, 0);
    const char* modeNames[4] = { "Clean", "Crunch", "Lead", "Ultra" };
    const float modeGain[4] = { 3.0f, 3.0f, 3.0f, 3.0f };
    for (int m = 0; m < 4; ++m) {
        const juce::String n = juce::String("A1 ") + modeNames[m] + " ";
        knob(p, ids::a1Ch(m, ids::gain),   n + "Gain",   modeGain[m]);
        knob(p, ids::a1Ch(m, ids::bass),   n + "Bass",   5.0f);
        knob(p, ids::a1Ch(m, ids::mid),    n + "Mid",    5.0f);
        knob(p, ids::a1Ch(m, ids::treble), n + "Treble", 5.0f);
    }
    knob(p, ids::a1Character, "A1 Character", 5.0f);
    knob(p, ids::a1Presence,  "A1 Presence",  5.0f);
    knob(p, ids::a1Master,    "A1 Master",    5.0f);
    knob(p, ids::a1Reverb,    "A1 Reverb",    0.0f);
    knob(p, ids::a1Gate,      "A1 Noise Gate", 0.0f);                 // 0 = off
    const char* bandNames[6] = { "80 Hz", "250 Hz", "800 Hz", "2.5 kHz", "6 kHz", "12 kHz" };
    for (int i = 0; i < 6; ++i) knob(p, ids::a1EQ(i), juce::String("A1 EQ ") + bandNames[i], 0.0f, -1.0f, 1.0f);
    toggle(p, ids::a1EQBypass, "A1 EQ Bypass", false);
    choice(p, ids::a1PowerTube, "A1 Power Tube", { "6L6GC", "EL34", "KT88" }, 0);

    // ---- AMP-002 "Dock": two channels, each with its own gain + tone stack
    choice(p, ids::a2Channel, "A2 Channel", { "A: Pristine", "B: Chime" }, 0);
    const char* chanNames[2] = { "Pristine", "Chime" };
    for (int c = 0; c < 2; ++c) {
        const juce::String n = juce::String("A2 ") + chanNames[c] + " ";
        knob(p, ids::a2Ch(c, ids::gain),   n + "Gain",   5.0f);
        knob(p, ids::a2Ch(c, ids::bass),   n + "Bass",   5.0f);
        knob(p, ids::a2Ch(c, ids::mid),    n + "Mid",    5.0f);
        knob(p, ids::a2Ch(c, ids::treble), n + "Treble", 5.0f);
    }
    knob(p, ids::a2Master,   "A2 Master",   5.0f);
    knob(p, ids::a2Presence, "A2 Presence", 5.0f);
    knob(p, ids::a2Reverb,   "A2 Reverb",   0.0f);
    toggle(p, ids::a2Standby, "A2 Standby", false);

    // ---- cabinets: every cab has its own level and up to two microphones
    const juce::StringArray micNames { "SM57 clone", "SM58 clone", "Royer 121 clone", "AKG 414 clone", "MD 421 clone" };
    for (int c = 0; c < 2; ++c) {
        const juce::String cn = c == 0 ? "Cab A " : "Cab B ";
        knob(p, ids::cab(c, "level"), cn + "Level", 0.0f, -12.0f, 18.0f, 0.1f);
        for (int m = 0; m < 2; ++m) {
            const juce::String mn = cn + "Mic " + juce::String(m + 1) + " ";
            if (m == 1) toggle(p, ids::cabMic(c, m, "on"), mn + "On", false);
            choice(p, ids::cabMic(c, m, "type"), mn + "Type", micNames, m == 0 ? 0 : 2);        // mic 1 SM57, mic 2 Royer 121
            knob(p, ids::cabMic(c, m, "pos"),   mn + "Position", 0.30f, 0.0f, 1.0f, 0.001f);
            knob(p, ids::cabMic(c, m, "dist"),  mn + "Distance", m == 0 ? 0.25f : 0.45f, 0.0f, 1.0f, 0.001f);
            knob(p, ids::cabMic(c, m, "level"), mn + "Level",    m == 0 ? 0.0f : -3.0f, -24.0f, 6.0f, 0.1f);
            knob(p, ids::cabMic(c, m, "pan"),   mn + "Pan",      0.0f, -1.0f, 1.0f, 0.01f);
            toggle(p, ids::cabMic(c, m, "phase"), mn + "Polarity Invert", false);
        }
    }

    // ---- I/O
    toggle(p, ids::cabOff, "Cab Off (use external IR)", false);
    knob(p, ids::inputTrim,  "Input Trim",  0.0f, -24.0f, 24.0f, 0.1f);
    knob(p, ids::outputTrim, "Output Trim", 0.0f, -24.0f, 24.0f, 0.1f);

    return { p.begin(), p.end() };
}

} // namespace params
