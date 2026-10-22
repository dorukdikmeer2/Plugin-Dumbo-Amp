#pragma once
#include <JuceHeader.h>

//==============================================================================
// PresetManager — user presets for the whole plugin (amp, channels, EQ, cabs, mics, I/O).
//
//   * "Default" is built in: every parameter back to its first-open value.
//   * User presets are small XML files in  <user app data>/DorukAudio/DumbDockAmp/Presets/<name>.ddpreset
//     (a copy of the parameter tree), so they are shared by every project and easy to back up or share.
//   * The name of the last loaded / saved preset travels with the project (property "presetName").
// All calls belong on the message thread.
//==============================================================================
class PresetManager {
public:
    static constexpr const char* kDefaultName = "Default";

    explicit PresetManager(juce::AudioProcessorValueTreeState& s) : apvts(s) {}

    static juce::File presetDirectory();
    static void setDirectoryOverride(const juce::File& dir);          // tests only

    juce::StringArray userPresetNames() const;                         // sorted, without extension
    juce::String currentName() const;                                  // "Default" if none

    bool saveUser(const juce::String& name);                           // creates or overwrites; false if the name is unusable
    bool loadUser(const juce::String& name);                           // false if missing or not a preset of this plugin
    bool removeUser(const juce::String& name);
    void loadDefault();

    static juce::String cleanName(const juce::String& raw);            // trims, removes characters that are illegal in file names

private:
    juce::File fileFor(const juce::String& name) const;
    juce::AudioProcessorValueTreeState& apvts;
};
