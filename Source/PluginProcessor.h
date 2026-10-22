#pragma once
#include <JuceHeader.h>
#include "Engine/AmpEngine.h"
#include "Parameters/EngineBinding.h"
#include "Presets/PresetManager.h"

//==============================================================================
// DumbDock Amp — VST3 plugin shell around the JUCE-free AmpEngine.
//   AMP-001 "Dumb 1" and AMP-002 "Dock" (circuit-level models from the
//   schematics) + two cabinets, each miked with one or two microphones.
//   Mono guitar in -> stereo out (a second mic can be panned).
//
//   Layers:  Parameters/  host parameters  <->  EngineParams (EngineBinding)
//            Engine/      the JUCE-free signal chain
//            UI/, PluginEditor   everything the user sees (replaceable)
//==============================================================================
class DumbDockAmpAudioProcessor : public juce::AudioProcessor
{
public:
    DumbDockAmpAudioProcessor();
    ~DumbDockAmpAudioProcessor() override = default;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using juce::AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override  { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 2.5; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    PresetManager presets { apvts };               // user presets + the built-in "Default"

    //--- metering / diagnostics (read by the editor's meters) ---
    std::atomic<float> rawInputPeak { 0.0f };      // before input trim, linear
    std::atomic<float> outputPeak   { 0.0f };      // after the safety limiter, linear
    std::atomic<int>   faultCount   { 0 };         // times the engine had to recover
    std::atomic<float> limiterReductionDb { 0.0f };// safety limiter gain reduction (dB, >= 0), for a meter
    std::atomic<float> gateReductionDb    { 0.0f };// noise gate attenuation (dB, <= 0)
    std::atomic<int>   midiMessagesSeen { 0 };

    //--- welcome popup preference (per user, not per project) ---
    static juce::File getSettingsFile();
    static bool welcomeHidden();
    static void setWelcomeHidden(bool hidden);

private:
    void handleMidi(const juce::MidiBuffer& midi);
    void setNormalised(juce::RangedAudioParameter* p, float value01);
    void migrateOldState(const juce::ValueTree& old);

    EngineBinding binding;
    AmpEngine engine;
    std::vector<float> monoIn, outL, outR;
    int maxBlock = 0;

    juce::RangedAudioParameter *prmA2Channel = nullptr, *prmA2Master = nullptr, *prmA2Reverb = nullptr,
                               *prmA2Standby = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DumbDockAmpAudioProcessor)
};
