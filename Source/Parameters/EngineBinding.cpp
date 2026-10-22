#include "Parameters/EngineBinding.h"
#include "Parameters/ParamIDs.h"

void EngineBinding::attach(juce::AudioProcessorValueTreeState& apvts)
{
    auto raw = [&](const std::string& id) { auto* p = apvts.getRawParameterValue(juce::String(id)); jassert(p != nullptr); return p; };

    activeAmp = raw(ids::activeAmp); activeCab = raw(ids::activeCab);
    inTrim = raw(ids::inputTrim); outTrim = raw(ids::outputTrim); cabOff = raw(ids::cabOff);

    a1Mode = raw(ids::a1Mode); a1Tube = raw(ids::a1PowerTube); a1Char = raw(ids::a1Character); a1Pres = raw(ids::a1Presence);
    a1Master = raw(ids::a1Master); a1Rev = raw(ids::a1Reverb); a1Gate = raw(ids::a1Gate); a1EQByp = raw(ids::a1EQBypass);
    for (int i = 0; i < 6; ++i) a1EQ[i] = raw(ids::a1EQ(i));
    for (int m = 0; m < kNumDumbChannels; ++m)
        a1Ch[m] = { raw(ids::a1Ch(m, ids::gain)), raw(ids::a1Ch(m, ids::bass)), raw(ids::a1Ch(m, ids::mid)), raw(ids::a1Ch(m, ids::treble)) };

    a2Chan = raw(ids::a2Channel); a2Master = raw(ids::a2Master); a2Pres = raw(ids::a2Presence); a2Rev = raw(ids::a2Reverb);
    a2Stby = raw(ids::a2Standby);
    for (int c = 0; c < kNumDockChannels; ++c)
        a2Ch[c] = { raw(ids::a2Ch(c, ids::gain)), raw(ids::a2Ch(c, ids::bass)), raw(ids::a2Ch(c, ids::mid)), raw(ids::a2Ch(c, ids::treble)) };

    for (int c = 0; c < kNumCabs; ++c) {
        cab[c].level = raw(ids::cab(c, "level"));
        for (int m = 0; m < kNumMics; ++m) {
            Mic& mc = cab[c].mic[m];
            mc.type = raw(ids::cabMic(c, m, "type")); mc.pos = raw(ids::cabMic(c, m, "pos")); mc.dist = raw(ids::cabMic(c, m, "dist"));
            mc.level = raw(ids::cabMic(c, m, "level")); mc.pan = raw(ids::cabMic(c, m, "pan"));
            mc.phase = raw(ids::cabMic(c, m, "phase"));
            if (m == 1) mc.on = raw(ids::cabMic(c, m, "on"));
        }
    }
}

void EngineBinding::read(EngineParams& p) const
{
    auto idx = [](float v, int hi) { return juce::jlimit(0, hi, (int) (v + 0.5f)); };

    p.activeAmp = idx(get(activeAmp), 1);  p.activeCab = idx(get(activeCab), kNumCabs - 1);
    p.inputTrimDb = get(inTrim);  p.outputTrimDb = get(outTrim);  p.cabOff = get(cabOff) > 0.5f;

    p.a1Mode = idx(get(a1Mode), kNumDumbChannels - 1);  p.a1PowerTube = idx(get(a1Tube), 2);
    for (int m = 0; m < kNumDumbChannels; ++m)
        p.a1Channel[m] = { get(a1Ch[m].gain), get(a1Ch[m].bass), get(a1Ch[m].mid), get(a1Ch[m].treble) };
    p.a1Character = get(a1Char);  p.a1Presence = get(a1Pres);  p.a1Master = get(a1Master);
    p.a1Reverb = get(a1Rev);  p.a1Gate = get(a1Gate);  p.a1EQBypass = get(a1EQByp) > 0.5f;
    for (int i = 0; i < 6; ++i) p.a1EQ[i] = get(a1EQ[i]);

    p.a2Channel = idx(get(a2Chan), kNumDockChannels - 1);
    for (int c = 0; c < kNumDockChannels; ++c)
        p.a2ChannelCtl[c] = { get(a2Ch[c].gain), get(a2Ch[c].bass), get(a2Ch[c].mid), get(a2Ch[c].treble) };
    p.a2Master = get(a2Master);  p.a2Presence = get(a2Pres);  p.a2Reverb = get(a2Rev);
    p.a2Standby = get(a2Stby) > 0.5f;

    for (int c = 0; c < kNumCabs; ++c) {
        p.cab[c].levelDb = get(cab[c].level);
        for (int m = 0; m < kNumMics; ++m) {
            const Mic& s = cab[c].mic[m];
            MicSettings& d = p.cab[c].mic[m];
            d.type = idx(get(s.type), kNumMicTypes - 1);
            d.position = get(s.pos);  d.distance = get(s.dist);  d.levelDb = get(s.level);  d.pan = get(s.pan);
            d.enabled = m == 0 ? true : get(s.on) > 0.5f;
            d.polarityInvert = get(s.phase) > 0.5f;
        }
    }
}
