#pragma once
#include <JuceHeader.h>

namespace params {
// Builds the complete host-visible parameter set (see ParamIDs.h for the naming scheme).
juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
}
