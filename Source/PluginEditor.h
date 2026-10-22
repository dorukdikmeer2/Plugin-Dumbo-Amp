#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "UI/AmpKnob.h"
#include "UI/VUMeter.h"
#include "UI/WelcomePopup.h"

//==============================================================================
// PluginEditor — the current (placeholder) front panel.
//
// The whole UI is a thin layer: every control is a juce attachment to a
// parameter ID from Parameters/ParamIDs.h, nothing here touches the DSP.  A new
// design can replace this file and the UI/ folder without any other change
// (see docs/UI_INTEGRATION.md).
//==============================================================================
class DumbDockAmpAudioProcessorEditor : public juce::AudioProcessorEditor
{
public:
    explicit DumbDockAmpAudioProcessorEditor(DumbDockAmpAudioProcessor&);
    ~DumbDockAmpAudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    using SliderAttach = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttach = juce::AudioProcessorValueTreeState::ButtonAttachment;
    using ComboAttach  = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

    // ---- small reusable pieces -------------------------------------------------------------------
    struct KnobCtl {                                   // a knob + its attachment
        AmpKnob knob;
        std::unique_ptr<SliderAttach> attach;
    };
    struct ChannelStrip {                              // GAIN / BASS / MID / TREBLE of one channel
        juce::Label title;
        KnobCtl gain, bass, mid, treble;
    };
    struct MicStrip {                                  // one microphone of one cab
        juce::ComboBox type;
        std::unique_ptr<ComboAttach> typeAttach;
        KnobCtl pos, dist, level, pan;
        juce::ToggleButton on, phase;                  // on: mic 2 only; phase invert: both mics
        std::unique_ptr<ButtonAttach> onAttach, phaseAttach;
        juce::Label title;
    };
    struct CabStrip {                                  // one cabinet
        juce::Component panel;
        juce::Label title;
        KnobCtl level;
        MicStrip mic[kNumMics];
    };
    struct EqSlider {
        juce::Slider slider;
        juce::Label label;
        std::unique_ptr<SliderAttach> attach;
    };

    void buildAmp001Panel();
    void buildAmp002Panel();
    void buildCabPanels();
    void bindKnob(KnobCtl& k, juce::Component& parent, const char* label, const std::string& id, juce::Colour accent);
    void updatePanelVisibility();
    void refreshPresetList();
    void askPresetName();
    void stepPreset(int dir);
    void showWelcome();
    void hideWelcome();

    DumbDockAmpAudioProcessor& audioProcessor;

    // The whole front panel is laid out in a fixed 1120 x 880 design space inside `content`, which is scaled to the
    // window (fixed aspect ratio), so the plugin window can be resized freely.
    static constexpr int kBaseW = 1120, kBaseH = 880;
    juce::Component content;

    //--- presets ---
    juce::ComboBox presetBox;
    juce::TextButton presetSave, presetDelete, presetPrev, presetNext;
    juce::Label presetLabel;
    bool refreshingPresets = false;

    //--- top bar ---
    juce::ComboBox ampSelector, cabSelector;
    std::unique_ptr<ComboAttach> ampSelectorAttach, cabSelectorAttach;
    juce::Label ampSelectorLabel, cabSelectorLabel;
    juce::ToggleButton cabOffButton;
    std::unique_ptr<ButtonAttach> cabOffAttach;

    //--- AMP-001 "Dumb 1" ---
    juce::Component amp001Panel;
    juce::ComboBox a1ModeBox, a1PowerTubeBox;
    std::unique_ptr<ComboAttach> a1ModeAttach, a1PowerTubeAttach;
    ChannelStrip a1Strip[kNumDumbChannels];
    KnobCtl a1Character, a1Presence, a1Reverb, a1Gate, a1Master;
    EqSlider a1EQ[6];
    juce::ToggleButton a1EQBypassButton;
    std::unique_ptr<ButtonAttach> a1EQBypassAttach;
    juce::Label a1TitleLabel, a1ModeLabel, a1EQLabel, a1PowerTubeLabel;

    //--- AMP-002 "Dock" ---
    juce::Component amp002Panel;
    juce::ComboBox a2ChannelBox;
    std::unique_ptr<ComboAttach> a2ChannelAttach;
    ChannelStrip a2Strip[kNumDockChannels];
    KnobCtl a2Presence, a2Reverb, a2Master;
    juce::ToggleButton a2StandbyButton;
    std::unique_ptr<ButtonAttach> a2StandbyAttach;
    juce::Label a2TitleLabel, a2ChannelLabel, a2MidiIndicator;

    //--- cabinets (both always visible; the active one is highlighted) ---
    CabStrip cabs[kNumCabs];

    //--- I/O + meters ---
    KnobCtl inputTrim, outputTrim;
    juce::Label ioLabel, meterLabelIn, meterLabelOut;
    VUMeter inputMeter, outputMeter;
    juce::TextButton infoButton;
    std::unique_ptr<WelcomePopup> welcome;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DumbDockAmpAudioProcessorEditor)
};
