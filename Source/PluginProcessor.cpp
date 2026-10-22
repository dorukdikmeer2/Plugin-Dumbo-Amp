#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Parameters/ParamIDs.h"
#include "Parameters/ParameterLayout.h"

namespace {
// Version 3: per-channel gain/tone controls, noise gate, per-cab microphones.
// Version 2 projects are migrated (see migrateOldState).
constexpr int kStateVersion = 3;
}

//==============================================================================
DumbDockAmpAudioProcessor::DumbDockAmpAudioProcessor()
    : AudioProcessor(BusesProperties()
                      .withInput("Input", juce::AudioChannelSet::stereo(), true)
                      .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "PARAMETERS", createParameterLayout())
{
    binding.attach(apvts);
    prmA2Channel = apvts.getParameter(ids::a2Channel);  prmA2Master  = apvts.getParameter(ids::a2Master);
    prmA2Reverb  = apvts.getParameter(ids::a2Reverb);
    prmA2Standby = apvts.getParameter(ids::a2Standby);
}

juce::AudioProcessorValueTreeState::ParameterLayout DumbDockAmpAudioProcessor::createParameterLayout()
{
    return params::createLayout();
}

//==============================================================================
void DumbDockAmpAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    maxBlock = std::max(samplesPerBlock, 8192);              // hosts may exceed their promise
    monoIn.assign((size_t) maxBlock, 0.0f);
    outL.assign((size_t) maxBlock, 0.0f);
    outR.assign((size_t) maxBlock, 0.0f);

    EngineParams p; binding.read(p);
    engine.setParams(p);
    engine.prepare(sampleRate, maxBlock);
    setLatencySamples(engine.getLatencySamples());
}

bool DumbDockAmpAudioProcessor::isBusesLayoutSupported(const BusesLayout& l) const
{
    const auto in = l.getMainInputChannelSet(), out = l.getMainOutputChannelSet();
    const bool outOk = out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo();
    const bool inOk  = in  == juce::AudioChannelSet::mono() || in  == juce::AudioChannelSet::stereo();
    return outOk && inOk;                                    // mono->mono, mono->stereo, stereo->stereo
}

//==============================================================================
// MIDI CC map from the AMP-002 schematic (PCB-D):
//   CC1 = channel, CC7 = master, CC11 = reverb, CC80 = standby
//==============================================================================
void DumbDockAmpAudioProcessor::setNormalised(juce::RangedAudioParameter* p, float v)
{
    if (p != nullptr && std::abs(p->getValue() - v) > 1e-4f) p->setValueNotifyingHost(v);
}

void DumbDockAmpAudioProcessor::handleMidi(const juce::MidiBuffer& midi)
{
    for (const auto meta : midi) {
        const auto msg = meta.getMessage();
        if (!msg.isController()) continue;
        midiMessagesSeen.fetch_add(1, std::memory_order_relaxed);
        const float v = (float) msg.getControllerValue() / 127.0f;
        const float sw = msg.getControllerValue() >= 64 ? 1.0f : 0.0f;
        switch (msg.getControllerNumber()) {
            case 1:  setNormalised(prmA2Channel, sw); break;
            case 7:  setNormalised(prmA2Master, v);   break;
            case 11: setNormalised(prmA2Reverb, v);   break;
            case 80: setNormalised(prmA2Standby, sw); break;
            default: break;
        }
    }
}

//==============================================================================
void DumbDockAmpAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;

    const int numIn  = getTotalNumInputChannels();
    const int numOut = getTotalNumOutputChannels();
    const int total  = buffer.getNumSamples();
    for (int c = numIn; c < numOut; ++c) buffer.clear(c, 0, total);
    if (maxBlock == 0 || numOut == 0) return;                // not prepared

    handleMidi(midi);

    EngineParams p; binding.read(p);
    engine.setParams(p);

    float rawPk = 0.0f, outPk = 0.0f;
    for (int start = 0; start < total; start += maxBlock) {
        const int n = std::min(maxBlock, total - start);

        // mono sum of the available input channels (an amp is a mono device)
        if (numIn >= 2) {
            const float* l = buffer.getReadPointer(0, start);
            const float* r = buffer.getReadPointer(1, start);
            for (int i = 0; i < n; ++i) monoIn[(size_t) i] = 0.5f * (l[i] + r[i]);
        } else if (numIn == 1) {
            std::copy_n(buffer.getReadPointer(0, start), n, monoIn.begin());
        } else {
            std::fill_n(monoIn.begin(), n, 0.0f);
        }

        engine.process(monoIn.data(), outL.data(), outR.data(), n);
        rawPk = std::max(rawPk, engine.rawInputPeak());
        outPk = std::max(outPk, engine.outputPeak());

        if (numOut >= 2) {
            std::copy_n(outL.begin(), n, buffer.getWritePointer(0, start));
            std::copy_n(outR.begin(), n, buffer.getWritePointer(1, start));
            for (int c = 2; c < numOut; ++c) buffer.clear(c, start, n);
        } else {
            float* o = buffer.getWritePointer(0, start);                 // mono host track: fold the stereo mic mix
            for (int i = 0; i < n; ++i) o[i] = 0.5f * (outL[(size_t) i] + outR[(size_t) i]);
        }
    }

    rawInputPeak.store(rawPk, std::memory_order_relaxed);
    outputPeak.store(outPk, std::memory_order_relaxed);
    faultCount.store(engine.faultCount(), std::memory_order_relaxed);
    limiterReductionDb.store(engine.limiterGainReductionDb(), std::memory_order_relaxed);
    gateReductionDb.store(engine.gateGainDb(), std::memory_order_relaxed);
}

//==============================================================================
juce::AudioProcessorEditor* DumbDockAmpAudioProcessor::createEditor()
{
    return new DumbDockAmpAudioProcessorEditor(*this);
}

void DumbDockAmpAudioProcessor::getStateInformation(juce::MemoryBlock& dest)
{
    auto state = apvts.copyState();
    state.setProperty("stateVersion", kStateVersion, nullptr);
    if (auto xml = state.createXml()) copyXmlToBinary(*xml, dest);
}

void DumbDockAmpAudioProcessor::setStateInformation(const void* data, int size)
{
    if (data == nullptr || size <= 0) return;
    std::unique_ptr<juce::XmlElement> xml(getXmlFromBinary(data, size));
    if (xml == nullptr || !xml->hasTagName(apvts.state.getType())) return;     // ignore foreign / corrupt state
    auto vt = juce::ValueTree::fromXml(*xml);
    if (!vt.isValid()) return;
    const int version = (int) vt.getProperty("stateVersion", 1);
    apvts.replaceState(vt);                                                    // unknown ids are ignored, out-of-range values are clamped
    if (version < kStateVersion) migrateOldState(vt);
}

// Projects saved before version 3 had ONE gain/bass/mid/treble set shared by all channels and ONE cabinet mic.
// Copy those values into every channel / both cabinets so an old project sounds the way it did.
void DumbDockAmpAudioProcessor::migrateOldState(const juce::ValueTree& old)
{
    auto oldValue = [&](const char* id, float& out) {
        for (const auto& child : old)
            if (child.hasProperty("id") && child["id"].toString() == id && child.hasProperty("value")) { out = (float) child["value"]; return true; }
        return false;
    };
    auto setPlain = [&](const std::string& id, float v) {
        if (auto* p = apvts.getParameter(juce::String(id))) p->setValueNotifyingHost(p->convertTo0to1(v));
    };
    float v = 0.0f;
    const char* knobs[4] = { ids::gain, ids::bass, ids::mid, ids::treble };
    const char* a1Old[4] = { "a1_gain", "a1_bass", "a1_mid", "a1_treble" };
    const char* a2Old[4] = { "a2_gain", "a2_bass", "a2_mid", "a2_treble" };
    for (int k = 0; k < 4; ++k) {
        if (oldValue(a1Old[k], v)) for (int m = 0; m < kNumDumbChannels; ++m) setPlain(ids::a1Ch(m, knobs[k]), v);
        if (oldValue(a2Old[k], v)) for (int c = 0; c < kNumDockChannels; ++c) setPlain(ids::a2Ch(c, knobs[k]), v);
    }
    for (int c = 0; c < kNumCabs; ++c) {
        if (oldValue("cab_micpos", v))  setPlain(ids::cabMic(c, 0, "pos"), v);
        if (oldValue("cab_micdist", v)) setPlain(ids::cabMic(c, 0, "dist"), v);
        if (oldValue("cab_level", v))   setPlain(ids::cab(c, "level"), v);
    }
}

//==============================================================================
juce::File DumbDockAmpAudioProcessor::getSettingsFile()
{
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
               .getChildFile("DorukAudio").getChildFile("DumbDockAmp.settings");
}

bool DumbDockAmpAudioProcessor::welcomeHidden()
{
    juce::PropertiesFile::Options o; o.filenameSuffix = ".settings"; o.osxLibrarySubFolder = "Application Support";
    juce::PropertiesFile pf(getSettingsFile(), o);
    return pf.getBoolValue("welcomeHidden", false);
}

void DumbDockAmpAudioProcessor::setWelcomeHidden(bool done)
{
    getSettingsFile().getParentDirectory().createDirectory();
    juce::PropertiesFile::Options o; o.filenameSuffix = ".settings"; o.osxLibrarySubFolder = "Application Support";
    juce::PropertiesFile pf(getSettingsFile(), o);
    pf.setValue("welcomeHidden", done);
    pf.saveIfNeeded();
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new DumbDockAmpAudioProcessor(); }
