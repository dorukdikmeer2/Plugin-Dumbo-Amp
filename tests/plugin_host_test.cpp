// Headless host test for the real plugin processor + editor (JUCE).
// Usage:  DumbDockHostTest <output-dir-for-pngs>
#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "UI/WelcomePopup.h"
#include "Parameters/ParamIDs.h"
#include "Presets/PresetManager.h"
#include <cstdio>
#include <random>
#include <functional>

static int failures = 0, checks = 0;
static void check(bool ok, const juce::String& what) { ++checks; if (!ok) { ++failures; std::printf("   FAIL  %s\n", what.toRawUTF8()); } }

static void fillGuitar(juce::AudioBuffer<float>& b, double sr, double t0, float amp)
{
    const double f0[] = { 82.41, 110.0, 146.83, 196.0, 246.94, 329.63 };
    for (int n = 0; n < b.getNumSamples(); ++n) {
        double t = t0 + n / sr, v = 0, env = std::exp(-std::fmod(t, 0.5) * 3.0);
        for (double f : f0) for (int h = 1; h <= 3; ++h) v += std::sin(2 * juce::MathConstants<double>::pi * f * h * t) / (h * 1.5);
        for (int c = 0; c < b.getNumChannels(); ++c) b.setSample(c, n, (float) (v / 6.0 * env * amp * 3.0));
    }
}

static bool finiteAndBounded(const juce::AudioBuffer<float>& b, float& pk)
{
    for (int c = 0; c < b.getNumChannels(); ++c)
        for (int n = 0; n < b.getNumSamples(); ++n) {
            float x = b.getSample(c, n);
            if (!std::isfinite(x)) return false;
            pk = std::max(pk, std::fabs(x));
        }
    return true;
}

int main(int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    const juce::File outDir = argc > 1 ? juce::File(argv[1]) : juce::File::getCurrentWorkingDirectory();
    outDir.createDirectory();

    std::printf("== 1. parameters ==\n");
    {
        DumbDockAmpAudioProcessor p;
        auto& params = p.getParameters();
        juce::StringArray ids;
        for (auto* q : params) if (auto* r = dynamic_cast<juce::RangedAudioParameter*>(q)) {
            check(!ids.contains(r->paramID), "duplicate id " + r->paramID); ids.add(r->paramID);
            const float d = r->convertFrom0to1(r->getDefaultValue());
            check(d >= r->getNormalisableRange().start && d <= r->getNormalisableRange().end, "default in range " + r->paramID);
        }
        std::printf("  %d parameters, unique ids OK\n", params.size());
        std::vector<std::string> expected = { ids::activeAmp, ids::activeCab, ids::a1Mode, ids::a1Character, ids::a1Presence, ids::a1Master, ids::a1Reverb,
            ids::a1Gate, ids::a1EQBypass, ids::a1PowerTube, ids::a2Channel, ids::a2Master, ids::a2Presence, ids::a2Reverb,
            ids::a2Standby, ids::cabOff, ids::inputTrim, ids::outputTrim };
        for (int i = 0; i < 6; ++i) expected.push_back(ids::a1EQ(i));
        for (int m = 0; m < 4; ++m) for (auto* w : { ids::gain, ids::bass, ids::mid, ids::treble }) expected.push_back(ids::a1Ch(m, w));
        for (int c = 0; c < 2; ++c) for (auto* w : { ids::gain, ids::bass, ids::mid, ids::treble }) expected.push_back(ids::a2Ch(c, w));
        for (int c = 0; c < 2; ++c) {
            expected.push_back(ids::cab(c, "level"));
            for (int m = 0; m < 2; ++m) for (auto* w : { "type", "pos", "dist", "level", "pan" }) expected.push_back(ids::cabMic(c, m, w));
            expected.push_back(ids::cabMic(c, 1, "on")); expected.push_back(ids::cabMic(c, 0, "phase")); expected.push_back(ids::cabMic(c, 1, "phase"));
        }
        check((int) expected.size() == params.size(), juce::String::formatted("parameter count %d == expected %d (no stray / missing ids)", params.size(), (int) expected.size()));
        for (auto& e : expected) check(p.apvts.getParameter(juce::String(e)) != nullptr, juce::String("param exists: ") + e);
        for (auto* gone : { "a1_fxloop", "a2_fxloop", "a2_heater" }) check(p.apvts.getParameter(gone) == nullptr, juce::String("removed parameter is gone: ") + gone);
    }

    std::printf("== 2. bus layouts ==\n");
    {
        DumbDockAmpAudioProcessor p;
        auto layout = [&](juce::AudioChannelSet in, juce::AudioChannelSet out) {
            juce::AudioProcessor::BusesLayout l; l.inputBuses.add(in); l.outputBuses.add(out); return p.checkBusesLayoutSupported(l); };
        check(layout(juce::AudioChannelSet::stereo(), juce::AudioChannelSet::stereo()), "stereo->stereo");
        check(layout(juce::AudioChannelSet::mono(),   juce::AudioChannelSet::mono()),   "mono->mono");
        check(layout(juce::AudioChannelSet::mono(),   juce::AudioChannelSet::stereo()), "mono->stereo");
        check(!layout(juce::AudioChannelSet::create5point1(), juce::AudioChannelSet::create5point1()), "5.1 rejected");
    }

    std::printf("== 3. audio at several rates / block sizes / layouts ==\n");
    for (double sr : { 44100.0, 48000.0, 96000.0, 192000.0 })
        for (int bs : { 32, 64, 480, 2048 })
            for (int layoutId = 0; layoutId < 3; ++layoutId) {
                DumbDockAmpAudioProcessor p;
                const int nin = layoutId == 0 ? 2 : 1, nout = layoutId == 1 ? 1 : 2;
                p.setPlayConfigDetails(nin, nout, sr, bs);
                p.prepareToPlay(sr, bs);
                for (int amp = 0; amp < 2; ++amp) {
                    p.apvts.getParameter("activeAmp")->setValueNotifyingHost((float) amp);
                    for (int m = 0; m < 4; ++m) p.apvts.getParameter(juce::String(ids::a1Ch(m, ids::gain)))->setValueNotifyingHost(0.9f);
                    for (int c = 0; c < 2; ++c) p.apvts.getParameter(juce::String(ids::a2Ch(c, ids::gain)))->setValueNotifyingHost(0.9f);
                    juce::AudioBuffer<float> buf(std::max(nin, nout), bs); juce::MidiBuffer midi; float pk = 0; bool fin = true;
                    for (int blk = 0; blk < (int) (sr * 0.5 / bs) + 1; ++blk) {
                        buf.setSize(std::max(nin, nout), bs, false, false, true);
                        fillGuitar(buf, sr, blk * (double) bs / sr, 0.5f);
                        p.processBlock(buf, midi);
                        fin &= finiteAndBounded(buf, pk);
                    }
                    check(fin && pk <= 1.0001f && pk > 1e-4f, juce::String::formatted("audio ok sr=%.0f bs=%d layout=%d amp=%d (peak %.3f)", sr, bs, layoutId, amp, pk));
                }
                if (bs == 64 && layoutId == 0) check(p.getLatencySamples() == (sr <= 52000.0 ? 39 : (sr <= 100000.0 ? 31 : 0)) + std::max(8, (int) std::ceil(0.001 * sr)), "latency reporting (oversampler + 1 ms limiter look-ahead)");
            }
    std::printf("  done\n");

    std::printf("== 4. host sends a bigger block than it promised; odd sizes; silence ==\n");
    {
        DumbDockAmpAudioProcessor p; p.setPlayConfigDetails(2, 2, 48000, 512); p.prepareToPlay(48000, 512);
        juce::MidiBuffer midi; float pk = 0; bool fin = true;
        for (int n : { 8209, 1, 3, 17, 0, 100000 }) {
            juce::AudioBuffer<float> b(2, std::max(n, 1)); b.setSize(2, n, false, false, true);
            fillGuitar(b, 48000, 0.0, 0.4f); p.processBlock(b, midi); fin &= finiteAndBounded(b, pk);
        }
        check(fin && pk <= 1.0001f, "oversized / odd blocks");
        float zpk = 0;
        for (int i = 0; i < 40; ++i) {                          // 40 x 4800 samples = 4 s of silence: tails must die out
            juce::AudioBuffer<float> z(2, 4800); z.clear(); p.processBlock(z, midi);
            zpk = 0; finiteAndBounded(z, zpk);
        }
        check(zpk < 1e-4f, "silence in -> silence out (tails decay)");
        // unprepared processor must not crash
        DumbDockAmpAudioProcessor q; juce::AudioBuffer<float> b(2, 64); b.clear(); q.processBlock(b, midi);
        check(true, "processBlock before prepare");
    }

    std::printf("== 5. MIDI CC map (AMP-002 schematic) ==\n");
    {
        DumbDockAmpAudioProcessor p; p.setPlayConfigDetails(2, 2, 48000, 128); p.prepareToPlay(48000, 128);
        juce::AudioBuffer<float> b(2, 128); b.clear(); juce::MidiBuffer m;
        m.addEvent(juce::MidiMessage::controllerEvent(1, 1, 100), 0);
        m.addEvent(juce::MidiMessage::controllerEvent(1, 7, 127), 1);
        m.addEvent(juce::MidiMessage::controllerEvent(1, 11, 64), 2);
        m.addEvent(juce::MidiMessage::controllerEvent(1, 64, 127), 3);
        m.addEvent(juce::MidiMessage::controllerEvent(1, 80, 127), 4);
        p.processBlock(b, m);
        auto v = [&](const char* id) { return p.apvts.getParameter(id)->getValue(); };
        check(v("a2_channel") > 0.99f, "CC1 -> channel B");
        check(v("a2_master") > 0.99f, "CC7 -> master");
        check(std::abs(v("a2_reverb") - 64.0f / 127.0f) < 0.01f, "CC11 -> reverb");
        check(p.apvts.getParameter("a2_fxloop") == nullptr, "CC64 has no target any more (FX loop removed)");
        check(v("a2_standby") > 0.99f, "CC80 -> standby");
        // standby really mutes
        p.apvts.getParameter("activeAmp")->setValueNotifyingHost(1.0f);
        float pk = 0; for (int i = 0; i < 60; ++i) { fillGuitar(b, 48000, i * 128 / 48000.0, 0.5f); juce::MidiBuffer e; p.processBlock(b, e); pk = 0; finiteAndBounded(b, pk); }
        check(pk < 1e-4f, "standby mutes the output");
        p.apvts.getParameter("a2_standby")->setValueNotifyingHost(0.0f);
        for (int i = 0; i < 100; ++i) { fillGuitar(b, 48000, i * 128 / 48000.0, 0.5f); juce::MidiBuffer e; p.processBlock(b, e); pk = 0; finiteAndBounded(b, pk); }
        check(pk > 1e-3f, "audio returns after standby");
    }

    std::printf("== 6. state round-trip, corrupt state ==\n");
    {
        DumbDockAmpAudioProcessor a, b;
        std::mt19937 rng(5); std::uniform_real_distribution<float> u(0, 1);
        for (auto* q : a.getParameters()) q->setValueNotifyingHost(u(rng));
        juce::MemoryBlock mb; a.getStateInformation(mb);
        b.setStateInformation(mb.getData(), (int) mb.getSize());
        bool same = true;
        for (int i = 0; i < a.getParameters().size(); ++i) {
            const float va = a.getParameters()[i]->getValue(), vb = b.getParameters()[i]->getValue();
            const bool discrete = dynamic_cast<juce::AudioParameterBool*>(a.getParameters()[i]) != nullptr || dynamic_cast<juce::AudioParameterChoice*>(a.getParameters()[i]) != nullptr;
            if (discrete ? (std::round(va * (a.getParameters()[i]->getNumSteps() - 1)) != std::round(vb * (a.getParameters()[i]->getNumSteps() - 1))) : std::abs(va - vb) >= 1e-4f) { same = false; std::printf("  mismatch %s: %.5f vs %.5f\n", a.getParameters()[i]->getName(40).toRawUTF8(), va, vb); }
        }
        check(same, "all parameters survive save/load");
        float before = b.apvts.getParameter("a1_c3_gain")->getValue();
        b.setStateInformation(nullptr, 0);
        const char junk[] = "this is not xml at all \x01\x02\x03";
        b.setStateInformation(junk, (int) sizeof(junk));
        juce::XmlElement foreign("SOMETHING_ELSE"); juce::MemoryBlock fb; juce::AudioProcessor::copyXmlToBinary(foreign, fb);
        b.setStateInformation(fb.getData(), (int) fb.getSize());
        check(std::abs(b.apvts.getParameter("a1_c3_gain")->getValue() - before) < 1e-6f, "garbage / foreign state ignored");
        std::printf("  state size %d bytes\n", (int) mb.getSize());
    }

    std::printf("== 6b. per-channel controls, stereo mics, old-project migration ==\n");
    {
        // helper: render a steady guitar through a fresh plugin and return the RMS of the left / right output
        auto render = [&](std::function<void(DumbDockAmpAudioProcessor&)> setup, double& rmsL, double& rmsR) {
            DumbDockAmpAudioProcessor p; p.setPlayConfigDetails(1, 2, 48000, 256); p.prepareToPlay(48000, 256);
            setup(p);
            juce::AudioBuffer<float> b(2, 256); juce::MidiBuffer m; double sl = 0, sr = 0; int n = 0;
            for (int i = 0; i < 120; ++i) {
                b.setSize(2, 256, false, false, true); b.clear();
                fillGuitar(b, 48000, i * 256 / 48000.0, 0.03f); b.copyFrom(1, 0, b, 0, 0, 256);     // a quiet DI, so the limiter stays out of the comparison
                p.processBlock(b, m);
                if (i >= 40) for (int k = 0; k < 256; ++k) { sl += b.getSample(0, k) * b.getSample(0, k); sr += b.getSample(1, k) * b.getSample(1, k); n += 1; }
            }
            rmsL = std::sqrt(sl / n); rmsR = std::sqrt(sr / n); };
        auto set = [](DumbDockAmpAudioProcessor& p, const std::string& id, float plain) {
            auto* q = p.apvts.getParameter(juce::String(id)); q->setValueNotifyingHost(q->convertTo0to1(plain)); };

        // each Dumb channel owns its gain: raising Crunch's gain must change Crunch but not Clean
        double ref, dummy, a, b2;
        render([&](auto& p) { set(p, ids::a1Mode, 0); }, ref, dummy);
        render([&](auto& p) { set(p, ids::a1Mode, 0); set(p, ids::a1Ch(1, ids::gain), 9.0f); }, a, dummy);
        check(std::abs(20 * std::log10(a / ref)) < 0.01, "Clean does not move when CRUNCH's gain is changed");
        render([&](auto& p) { set(p, ids::a1Mode, 1); }, ref, dummy);
        render([&](auto& p) { set(p, ids::a1Mode, 1); set(p, ids::a1Ch(1, ids::gain), 9.0f); }, a, dummy);
        check(std::abs(20 * std::log10(a / ref)) > 0.5, "CRUNCH's own gain changes CRUNCH");
        render([&](auto& p) { set(p, ids::a1Mode, 2); set(p, ids::a1Ch(2, ids::bass), 0.0f); }, a, dummy);
        render([&](auto& p) { set(p, ids::a1Mode, 2); set(p, ids::a1Ch(2, ids::bass), 10.0f); }, b2, dummy);
        check(std::abs(20 * std::log10(a / b2)) > 0.3, "LEAD's own bass control works");
        // Dock: same idea
        render([&](auto& p) { set(p, ids::activeAmp, 1); set(p, ids::a2Channel, 0); }, ref, dummy);
        render([&](auto& p) { set(p, ids::activeAmp, 1); set(p, ids::a2Channel, 0); set(p, ids::a2Ch(1, ids::gain), 10.0f); }, a, dummy);
        check(std::abs(20 * std::log10(a / ref)) < 0.01, "Dock A does not move when B's gain is changed");

        // stereo: a hard-panned mic 1 goes to one side only; centred it is identical on both
        double l, r;
        render([&](auto& p) { set(p, ids::cabMic(0, 0, "pan"), 0.0f); }, l, r);
        check(std::abs(20 * std::log10(l / r)) < 0.05, "centred mic: left == right");
        double cl, cr;
        render([&](auto& p) { set(p, ids::cabMic(0, 0, "pan"), 0.0f); }, cl, cr);
        render([&](auto& p) { set(p, ids::cabMic(0, 0, "pan"), 1.0f); }, l, r);
        check(r > 20 * l, "mic panned hard right: the left channel is (almost) silent");
        check(std::abs(20 * std::log10(r / cr)) < 0.05, "pan only pans: the side it is panned to keeps exactly the centre level");
        render([&](auto& p) { set(p, ids::cabMic(0, 0, "pan"), -0.5f); }, l, r);
        check(std::abs(20 * std::log10(l / cl)) < 0.05 && r < cr, "pan never raises a channel above the centre level (half pan: left unchanged, right turned down)");
        // second mic: on changes the sound, off does not
        render([&](auto& p) { }, ref, dummy);
        render([&](auto& p) { set(p, ids::cabMic(0, 1, "on"), 1.0f); set(p, ids::cabMic(0, 1, "pan"), 1.0f); set(p, ids::cabMic(0, 0, "pan"), -1.0f); }, l, r);
        check(l > 0.1 * ref && r > 0.1 * ref, "two mics panned apart fill both sides");
        check(std::abs(20 * std::log10(std::sqrt(l * l + r * r) / std::sqrt(2.0) / ref)) < 6.0, "switching mic 2 on does not jump the level by more than 6 dB");
        // loudness is the same with one mic or two (centred, and panned apart): +-1.5 dB
        {
            double l2, r2;
            render([&](auto& p) { set(p, ids::cabMic(0, 1, "on"), 1.0f); }, l2, r2);
            check(std::abs(20 * std::log10(std::max(l2, r2) / ref)) < 1.5, "one mic -> two mics (centred): same loudness within 1.5 dB");
            check(std::abs(20 * std::log10(std::max(l, r) / ref)) < 1.5, "one mic -> two mics (panned apart): same loudness within 1.5 dB");
            double pl, pr;
            render([&](auto& p) { set(p, ids::cabMic(0, 0, "phase"), 1.0f); }, pl, pr);
            check(std::abs(20 * std::log10(pl / ref)) < 0.05, "MIC 1 PHASE INVERT (one mic) does not change the level");
        }
        // gate: with the gate fully up a quiet signal is attenuated, with the gate off it is not
        {
            double off, on;
            auto quiet = [&](float gate, double& rms) {
                DumbDockAmpAudioProcessor p; p.setPlayConfigDetails(1, 2, 48000, 256); p.prepareToPlay(48000, 256);
                set(p, ids::a1Gate, gate);
                juce::AudioBuffer<float> bb(2, 256); juce::MidiBuffer m; double s2 = 0; int n = 0;
                for (int i = 0; i < 150; ++i) { bb.setSize(2, 256, false, false, true); bb.clear(); fillGuitar(bb, 48000, i * 256 / 48000.0, 0.002f); p.processBlock(bb, m);
                    if (i > 80) for (int k = 0; k < 256; ++k) { s2 += bb.getSample(0, k) * bb.getSample(0, k); ++n; } }
                rms = std::sqrt(s2 / n); };
            quiet(0.0f, off); quiet(10.0f, on);
            check(on < off * 0.5, "noise gate knob at max attenuates a very quiet signal by > 6 dB; at 0 it is off");
        }

        // migration of a version-2 project: shared gain/tone values are copied to every channel, the cab mic to both cabs
        {
            DumbDockAmpAudioProcessor p;
            juce::ValueTree old(p.apvts.state.getType());
            auto add = [&](const char* id, float v) { juce::ValueTree c("PARAM"); c.setProperty("id", id, nullptr); c.setProperty("value", v, nullptr); old.appendChild(c, nullptr); };
            add("a1_gain", 7.0f); add("a1_bass", 2.0f); add("a2_treble", 8.0f); add("cab_micpos", 0.8f); add("cab_level", 3.0f);
            juce::MemoryBlock mb; if (auto xml = old.createXml()) juce::AudioProcessor::copyXmlToBinary(*xml, mb);
            p.setStateInformation(mb.getData(), (int) mb.getSize());
            auto plain = [&](const std::string& id) { auto* q = p.apvts.getParameter(juce::String(id)); return q->convertFrom0to1(q->getValue()); };
            bool ok = true;
            for (int m = 0; m < 4; ++m) ok &= std::abs(plain(ids::a1Ch(m, ids::gain)) - 7.0f) < 0.02f && std::abs(plain(ids::a1Ch(m, ids::bass)) - 2.0f) < 0.02f;
            for (int c = 0; c < 2; ++c) ok &= std::abs(plain(ids::a2Ch(c, ids::treble)) - 8.0f) < 0.02f;
            for (int c = 0; c < 2; ++c) ok &= std::abs(plain(ids::cabMic(c, 0, "pos")) - 0.8f) < 0.002f && std::abs(plain(ids::cab(c, "level")) - 3.0f) < 0.1f;
            check(ok, "a version-2 project is migrated into the per-channel / per-cab parameters");
        }
    }

    std::printf("== 6c. presets ==\n");
    {
        auto tmp = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("ddamp_presets_test");
        tmp.deleteRecursively(); PresetManager::setDirectoryOverride(tmp);
        DumbDockAmpAudioProcessor p;
        auto set = [&](const std::string& id, float plain) { auto* q = p.apvts.getParameter(juce::String(id)); q->setValueNotifyingHost(q->convertTo0to1(plain)); };
        auto get = [&](const std::string& id) { auto* q = p.apvts.getParameter(juce::String(id)); return q->convertFrom0to1(q->getValue()); };
        check(p.presets.currentName() == "Default" && p.presets.userPresetNames().isEmpty(), "fresh install: preset is Default, no user presets");
        // build a "my sound": Dock, chime channel, mic 2 on, EQ bypassed, odd values everywhere
        set(ids::activeAmp, 1); set(ids::a2Channel, 1); set(ids::a2Ch(1, ids::treble), 8.5f); set(ids::cabMic(1, 1, "on"), 1.0f);
        set(ids::cabMic(1, 1, "type"), 4); set(ids::activeCab, 1); set(ids::a1EQBypass, 1); set(ids::a1EQ(3), 0.6f); set(ids::cab(1, "level"), 7.5f);
        std::mt19937 rng(21); std::uniform_real_distribution<float> u(0, 1);
        juce::Array<float> snap; for (auto* q : p.getParameters()) snap.add(q->getValue());
        check(p.presets.saveUser("  My: Blues/Rock? "), "save a user preset (illegal characters are cleaned)");
        const auto names = p.presets.userPresetNames();
        check(names.size() == 1 && names[0].isNotEmpty() && p.presets.currentName() == names[0], "the preset appears in the list and becomes the current one");
        check(!p.presets.saveUser("default") && !p.presets.saveUser("   "), "reserved / empty names are refused");
        for (auto* q : p.getParameters()) q->setValueNotifyingHost(u(rng));                // wreck every setting
        check(p.presets.loadUser(names[0]), "load the preset");
        bool same = true; int bad = 0;
        for (int i = 0; i < p.getParameters().size(); ++i) { auto* q = p.getParameters()[i]; const float a = q->getValue(), b = snap[i];
            const bool discrete = q->getNumSteps() < 100 && q->getNumSteps() > 1;
            if (discrete ? std::round(a * (q->getNumSteps() - 1)) != std::round(b * (q->getNumSteps() - 1)) : std::abs(a - b) > 1e-4f) { same = false; ++bad; } }
        check(same, juce::String("loading restores every parameter (amp, channels, EQ, cabs, mics, I/O); mismatches ") + juce::String(bad));
        // Default = exactly the first-open state
        p.presets.loadDefault();
        DumbDockAmpAudioProcessor fresh; bool dflt = true;
        for (int i = 0; i < p.getParameters().size(); ++i) dflt &= std::abs(p.getParameters()[i]->getValue() - fresh.getParameters()[i]->getValue()) < 1e-6f;
        check(dflt && p.presets.currentName() == "Default", "Default puts every parameter back to its first-open value");
        // the user preset survives in a project: state round trip keeps the name
        p.presets.loadUser(names[0]); juce::MemoryBlock mb; p.getStateInformation(mb);
        DumbDockAmpAudioProcessor q2; q2.setStateInformation(mb.getData(), (int) mb.getSize());
        check(q2.presets.currentName() == names[0], "the current preset name is stored with the project");
        // corrupt / foreign files are ignored
        tmp.getChildFile("broken.ddpreset").replaceWithText("<nonsense");
        tmp.getChildFile("foreign.ddpreset").replaceWithText("<OTHER/>");
        check(!p.presets.loadUser("broken") && !p.presets.loadUser("foreign") && !p.presets.loadUser("missing"), "corrupt, foreign and missing presets are refused without touching the settings");
        check(p.presets.removeUser(names[0]) && !p.presets.userPresetNames().contains(names[0]), "delete a preset");
        tmp.deleteRecursively(); PresetManager::setDirectoryOverride(juce::File());
    }

    std::printf("== 7. automation thrash while processing ==\n");
    {
        DumbDockAmpAudioProcessor p; p.setPlayConfigDetails(2, 2, 48000, 256); p.prepareToPlay(48000, 256);
        std::mt19937 rng(11); std::uniform_real_distribution<float> u(0, 1);
        juce::AudioBuffer<float> b(2, 256); juce::MidiBuffer m; float pk = 0; bool fin = true;
        for (int i = 0; i < 1500; ++i) {
            for (auto* q : p.getParameters()) if (u(rng) < 0.3f) q->setValueNotifyingHost(u(rng));
            fillGuitar(b, 48000, i * 256 / 48000.0, 0.5f); p.processBlock(b, m); fin &= finiteAndBounded(b, pk);
            if (i % 400 == 399) { p.releaseResources(); p.prepareToPlay(i % 800 ? 44100 : 96000, 256); }
        }
        check(fin && pk <= 1.0001f && p.faultCount.load() == 0, juce::String::formatted("thrash safe (peak %.3f, faults %d)", pk, p.faultCount.load()));
    }

    std::printf("== 8. editor + welcome popup ==\n");
    {
        auto pump = [](int ms) { juce::MessageManager::getInstance()->runDispatchLoopUntil(ms); };
        auto findPopup = [](juce::Component& ed) -> WelcomePopup* {
            for (auto* c : ed.getChildren()) if (auto* w = dynamic_cast<WelcomePopup*>(c)) return w;
            return nullptr; };
        auto findText = [](juce::Component& c, const juce::String& text) -> juce::Button* {
            std::function<juce::Button*(juce::Component&)> rec = [&](juce::Component& cc) -> juce::Button* {
                for (auto* ch : cc.getChildren()) { if (auto* b = dynamic_cast<juce::Button*>(ch)) if (b->getButtonText() == text) return b;
                                                    if (auto* r = rec(*ch)) return r; }
                return nullptr; };
            return rec(c); };

        DumbDockAmpAudioProcessor::setWelcomeHidden(false);
        check(!DumbDockAmpAudioProcessor::welcomeHidden(), "welcome popup enabled on a fresh profile");
        DumbDockAmpAudioProcessor p; p.setPlayConfigDetails(2, 2, 48000, 256); p.prepareToPlay(48000, 256);
        std::unique_ptr<juce::AudioProcessorEditor> ed(p.createEditor());
        check(ed != nullptr, "editor created");
        if (ed) {
            auto save = [&](const juce::String& name) {
                auto img = ed->createComponentSnapshot(ed->getLocalBounds(), true, 1.0f);
                juce::PNGImageFormat png; juce::FileOutputStream fo(outDir.getChildFile(name)); fo.setPosition(0); fo.truncate(); png.writeImageToStream(img, fo); };

            pump(300);
            WelcomePopup* w = findPopup(*ed);
            check(w != nullptr, "welcome popup opens when the window opens");
            check(findText(*ed, "SETUP") == nullptr, "there is no SETUP button any more");
            if (w) {
                save("welcome_popup.png");
                auto* start = findText(*w, "START PLAYING");
                auto* dont  = findText(*w, "Don't show this again");
                check(start != nullptr && dont != nullptr, "popup has START PLAYING and the don't-show option");
                const auto before = p.apvts.copyState().toXmlString();
                if (start) { start->triggerClick(); pump(150); }
                check(findPopup(*ed) == nullptr, "START PLAYING closes the popup");
                check(p.apvts.copyState().toXmlString() == before, "the popup changes no parameter");
                check(!DumbDockAmpAudioProcessor::welcomeHidden(), "popup comes back next time unless told not to");
            }
            // '?' button re-opens it; ticking 'don't show again' is remembered
            if (auto* q = findText(*ed, "?")) {
                q->triggerClick(); pump(150);
                w = findPopup(*ed);
                check(w != nullptr, "the ? button re-opens the popup");
                if (w) {
                    if (auto* dont = dynamic_cast<juce::ToggleButton*>(findText(*w, "Don't show this again"))) dont->setToggleState(true, juce::sendNotification);
                    if (auto* s = findText(*w, "START PLAYING")) { s->triggerClick(); pump(150); }
                }
                check(DumbDockAmpAudioProcessor::welcomeHidden(), "'don't show again' is remembered");
            } else check(false, "? button exists");
            ed.reset();

            std::unique_ptr<juce::AudioProcessorEditor> ed2(p.createEditor());
            pump(300);
            check(findPopup(*ed2) == nullptr, "popup stays away once 'don't show again' is set");
            for (int amp = 0; amp < 2; ++amp) {
                p.apvts.getParameter("activeAmp")->setValueNotifyingHost((float) amp);
                pump(60);
                auto img = ed2->createComponentSnapshot(ed2->getLocalBounds(), true, 1.0f);
                juce::PNGImageFormat png; juce::FileOutputStream fo(outDir.getChildFile("editor_amp00" + juce::String(amp + 1) + ".png")); fo.setPosition(0); fo.truncate(); png.writeImageToStream(img, fo);
            }
            // preset arrows: < and > step through Default and the user presets and wrap around
            {
                const auto tmp = outDir.getChildFile("arrow_presets"); tmp.deleteRecursively(); PresetManager::setDirectoryOverride(tmp);
                p.presets.saveUser("Alpha"); p.presets.saveUser("Beta");
                p.presets.loadDefault();
                ed2.reset(); ed2.reset(p.createEditor()); pump(200);
                auto* nxt = findText(*ed2, ">"); auto* prv = findText(*ed2, "<");
                check(nxt != nullptr && prv != nullptr, "preset < and > buttons exist");
                if (nxt && prv) {
                    std::vector<juce::String> seq;
                    for (int i = 0; i < 4; ++i) { nxt->triggerClick(); pump(120); seq.push_back(p.presets.currentName()); }
                    check(seq[0] == "Alpha" && seq[1] == "Beta" && seq[2] == "Default" && seq[3] == "Alpha", "> steps Default -> Alpha -> Beta -> Default (wraps)");
                    prv->triggerClick(); pump(120); const auto a = p.presets.currentName();
                    prv->triggerClick(); pump(120); const auto b = p.presets.currentName();
                    check(a == "Default" && b == "Beta", "< steps backwards and wraps");
                }
                tmp.deleteRecursively(); PresetManager::setDirectoryOverride(juce::File());
            }
            // resizing: the layout scales with the window (aspect ratio kept), nothing falls outside, extreme sizes render
            for (int w : { 800, 1120, 1680 }) {
                ed2->setSize(w, (int) std::lround(w * 880.0 / 1120.0)); pump(60);
                auto img = ed2->createComponentSnapshot(ed2->getLocalBounds(), true, 1.0f);
                check(img.getWidth() == w, juce::String::formatted("editor renders at width %d", w));
                bool inside = true; int seen = 0;
                std::function<void(juce::Component&, juce::Component&)> walk = [&](juce::Component& root, juce::Component& c) {
                    for (auto* ch : c.getChildren()) {
                        if (!ch->isVisible()) continue;
                        if (dynamic_cast<juce::Slider*>(ch) || dynamic_cast<juce::Button*>(ch) || dynamic_cast<juce::ComboBox*>(ch)) {
                            ++seen; auto b = root.getLocalArea(ch->getParentComponent(), ch->getBounds());
                            const bool ok = b.getX() >= -1 && b.getY() >= -1 && b.getRight() <= 1121 && b.getBottom() <= 881;
                            if (!ok) std::printf("    outside: %s (%d,%d,%d,%d)\n", ch->getName().toRawUTF8(), b.getX(), b.getY(), b.getWidth(), b.getHeight());
                            inside &= ok;
                        }
                        walk(root, *ch);
                    } };
                for (auto* host : ed2->getChildren()) if (dynamic_cast<WelcomePopup*>(host) == nullptr) walk(*host, *host);
                std::printf("    controls checked: %d\n", seen); inside &= seen > 40;
                check(inside, "all controls sit inside the design space");
                if (w != 1120) { juce::PNGImageFormat png; juce::FileOutputStream fo(outDir.getChildFile("editor_size_" + juce::String(w) + ".png")); fo.setPosition(0); fo.truncate(); png.writeImageToStream(img, fo); }
            }
            ed2->setBoundsConstrained(juce::Rectangle<int>(0, 0, 1000, 100)); pump(60);
            check(ed2->getWidth() >= 800 && ed2->getHeight() >= 629, "the window cannot be made smaller than the minimum size");
            ed2.reset();
        }
        DumbDockAmpAudioProcessor::setWelcomeHidden(false);
    }

    std::printf("\n%s (%d of %d checks failed)\n", failures ? "HOST TEST FAILED" : "HOST TEST PASSED", failures, checks);
    return failures ? 1 : 0;
}
