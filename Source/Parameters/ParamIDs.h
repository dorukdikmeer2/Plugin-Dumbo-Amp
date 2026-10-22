#pragma once
#include <string>

//==============================================================================
// ParamIDs — every host-visible parameter ID in one place.
//
// IDs are part of the saved-project format: never rename one without adding a
// migration to PluginProcessor::setStateInformation().  This header is JUCE-free
// so tools and tests can use it as well.
//
//   global      activeAmp activeCab input_trim output_trim cab_off
//   AMP-001     a1_mode a1_powertube a1_character a1_presence a1_master a1_reverb a1_gate a1_eq0..5
//               a1_c1..c4_{gain,bass,mid,treble}          one set per mode (Clean/Crunch/Lead/Ultra)
//   AMP-002     a2_channel a2_master a2_presence a2_reverb a2_standby
//               a2_a_{gain,bass,mid,treble}  a2_b_{...}   one set per channel (A Pristine / B Chime)
//   cabinets    cabA_level cabA_m1_{type,pos,dist,level,pan}  cabA_m2_{on,type,pos,dist,level,pan,phase}   (and cabB_...)
//==============================================================================
namespace ids {

constexpr const char* activeAmp   = "activeAmp";
constexpr const char* activeCab   = "activeCab";
constexpr const char* inputTrim   = "input_trim";
constexpr const char* outputTrim  = "output_trim";
constexpr const char* cabOff      = "cab_off";

constexpr const char* a1Mode      = "a1_mode";
constexpr const char* a1PowerTube = "a1_powertube";
constexpr const char* a1Character = "a1_character";
constexpr const char* a1Presence  = "a1_presence";
constexpr const char* a1Master    = "a1_master";
constexpr const char* a1Reverb    = "a1_reverb";
constexpr const char* a1Gate      = "a1_gate";
constexpr const char* a1EQBypass  = "a1_eq_bypass";

constexpr const char* a2Channel   = "a2_channel";
constexpr const char* a2Master    = "a2_master";
constexpr const char* a2Presence  = "a2_presence";
constexpr const char* a2Reverb    = "a2_reverb";
constexpr const char* a2Standby   = "a2_standby";

// per-channel control names
constexpr const char* gain = "gain";
constexpr const char* bass = "bass";
constexpr const char* mid  = "mid";
constexpr const char* treble = "treble";

inline std::string a1EQ(int band)                     { return "a1_eq" + std::to_string(band); }
inline std::string a1Ch(int mode, const char* what)   { return "a1_c" + std::to_string(mode + 1) + "_" + what; }
inline std::string a2Ch(int channel, const char* what){ return std::string("a2_") + (channel == 0 ? "a" : "b") + "_" + what; }

inline std::string cab(int cabIndex, const char* what)                 { return std::string("cab") + (cabIndex == 0 ? "A" : "B") + "_" + what; }
inline std::string cabMic(int cabIndex, int mic, const char* what)     { return cab(cabIndex, ("m" + std::to_string(mic + 1) + "_" + what).c_str()); }

} // namespace ids
