#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Parameters/ParamIDs.h"

namespace {
const juce::Colour kGreenBg  { 0xff0a0f0a }, kGreenPanel { 0xff0e150e }, kGreen { 0xff00e050 }, kMint { 0xff00ff88 };
const juce::Colour kAmberBg  { 0xff120d02 }, kAmberPanel { 0xff1a1200 }, kAmber { 0xffffaa00 };
const juce::Colour kCyan     { 0xff3af0ff };

juce::Font mono(float h, bool bold = true) { return juce::Font(juce::Font::getDefaultMonospacedFontName(), h, bold ? juce::Font::bold : juce::Font::plain); }

void styleLabel(juce::Label& l, const juce::String& text, juce::Colour c, float size = 9.0f, juce::Justification j = juce::Justification::left)
{
    l.setText(text, juce::dontSendNotification);
    l.setFont(mono(size));
    l.setColour(juce::Label::textColourId, c);
    l.setJustificationType(j);
}
void styleCombo(juce::ComboBox& b, juce::Colour c)
{
    b.setColour(juce::ComboBox::textColourId, c);
    b.setColour(juce::ComboBox::outlineColourId, c.withAlpha(0.5f));
    b.setColour(juce::ComboBox::arrowColourId, c);
    b.setColour(juce::ComboBox::backgroundColourId, juce::Colour(0xff0b0b0b));
}
const char* kModeTitles[4] = { "1  CLEAN", "2  CRUNCH", "3  LEAD", "4  ULTRA" };
const char* kChanTitles[2] = { "A  PRISTINE", "B  CHIME" };
const char* kMicNames[5]   = { "SM57 clone", "SM58 clone", "Royer 121 clone", "AKG 414 clone", "MD 421 clone" };
}

//==============================================================================
void DumbDockAmpAudioProcessorEditor::bindKnob(KnobCtl& k, juce::Component& parent, const char* label, const std::string& id, juce::Colour accent)
{
    k.knob.setKnobLabel(label);
    k.knob.setAccentColour(accent);
    parent.addAndMakeVisible(k.knob);
    k.attach = std::make_unique<SliderAttach>(audioProcessor.apvts, juce::String(id), k.knob);
}

DumbDockAmpAudioProcessorEditor::DumbDockAmpAudioProcessorEditor(DumbDockAmpAudioProcessor& p)
    : AudioProcessorEditor(&p), audioProcessor(p)
{
    addAndMakeVisible(content);
    setResizable(true, true);
    setResizeLimits(800, 629, 2240, 1760);                                  // 71 % .. 200 %
    getConstrainer()->setFixedAspectRatio((double) kBaseW / (double) kBaseH);
    setSize(kBaseW, kBaseH);

    //--- amp / cab selectors ---
    ampSelector.addItemList({ "AMP-001  \"DUMB 1\"", "AMP-002  \"DOCK\"" }, 1);
    content.addAndMakeVisible(ampSelector);
    ampSelectorAttach = std::make_unique<ComboAttach>(audioProcessor.apvts, ids::activeAmp, ampSelector);
    ampSelector.onChange = [this] { updatePanelVisibility(); };

    cabSelector.addItemList({ "CAB A - 4x12 Greenback", "CAB B - 1x12 Vintage 30" }, 1);
    content.addAndMakeVisible(cabSelector);
    cabSelectorAttach = std::make_unique<ComboAttach>(audioProcessor.apvts, ids::activeCab, cabSelector);
    cabSelector.onChange = [this] { repaint(); };
    styleCombo(ampSelector, kGreen); styleCombo(cabSelector, kAmber);

    styleLabel(ampSelectorLabel, "ACTIVE AMP", kGreen);
    styleLabel(cabSelectorLabel, "ACTIVE CAB", kAmber);
    content.addAndMakeVisible(ampSelectorLabel); content.addAndMakeVisible(cabSelectorLabel);

    cabOffButton.setButtonText("CAB OFF");
    cabOffButton.setTooltip("Bypass the built-in cabinet, e.g. to use your own IR loader after the plugin");
    cabOffButton.setColour(juce::ToggleButton::textColourId, kAmber);
    cabOffButton.setColour(juce::ToggleButton::tickColourId, kAmber);
    content.addAndMakeVisible(cabOffButton);
    cabOffAttach = std::make_unique<ButtonAttach>(audioProcessor.apvts, ids::cabOff, cabOffButton);

    buildAmp001Panel();
    buildAmp002Panel();
    buildCabPanels();
    content.addAndMakeVisible(amp001Panel);
    content.addAndMakeVisible(amp002Panel);
    for (auto& c : cabs) content.addAndMakeVisible(c.panel);

    //--- I/O trim + meters ---
    bindKnob(inputTrim,  content, "IN TRIM",  ids::inputTrim,  kMint);
    bindKnob(outputTrim, content, "OUT TRIM", ids::outputTrim, kMint);
    styleLabel(ioLabel, "I/O", kMint);
    content.addAndMakeVisible(ioLabel);

    content.addAndMakeVisible(inputMeter);
    content.addAndMakeVisible(outputMeter);
    inputMeter.setLevelSource(&audioProcessor.rawInputPeak);
    outputMeter.setLevelSource(&audioProcessor.outputPeak);
    for (auto* l : { &meterLabelIn, &meterLabelOut }) content.addAndMakeVisible(l);
    styleLabel(meterLabelIn, "IN", kGreen, 9.0f, juce::Justification::centred);
    styleLabel(meterLabelOut, "OUT", kGreen, 9.0f, juce::Justification::centred);

    infoButton.setButtonText("?");
    infoButton.setTooltip("Show the welcome overview again");
    infoButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff1a2a1a));
    infoButton.setColour(juce::TextButton::textColourOffId, kGreen);
    infoButton.onClick = [this] { showWelcome(); };
    content.addAndMakeVisible(infoButton);

    //--- presets: built-in Default + the user's own
    styleLabel(presetLabel, "PRESET", kMint);
    content.addAndMakeVisible(presetLabel);
    styleCombo(presetBox, kMint);
    content.addAndMakeVisible(presetBox);
    for (auto* b : { &presetSave, &presetDelete, &presetPrev, &presetNext }) {
        b->setColour(juce::TextButton::buttonColourId, juce::Colour(0xff1a2a1a));
        b->setColour(juce::TextButton::textColourOffId, kMint);
        content.addAndMakeVisible(b);
    }
    presetSave.setButtonText("SAVE");
    presetSave.setTooltip("Save everything (amp, channels, EQ, cabs, mics, I/O) as a preset; an existing name is overwritten");
    presetDelete.setButtonText("DELETE");
    presetDelete.setTooltip("Delete the selected user preset");
    presetPrev.setButtonText("<");  presetNext.setButtonText(">");
    presetPrev.setTooltip("Previous preset");  presetNext.setTooltip("Next preset");
    presetPrev.onClick = [this] { stepPreset(-1); };
    presetNext.onClick = [this] { stepPreset(+1); };
    presetSave.onClick = [this] { askPresetName(); };
    presetDelete.onClick = [this] {
        const auto n = audioProcessor.presets.currentName();
        if (n != PresetManager::kDefaultName) { audioProcessor.presets.removeUser(n); refreshPresetList(); }
    };
    presetBox.onChange = [this] {
        if (refreshingPresets) return;
        const auto name = presetBox.getText();
        if (presetBox.getSelectedId() == 1) audioProcessor.presets.loadDefault();
        else audioProcessor.presets.loadUser(name);
        refreshPresetList();
        updatePanelVisibility();
    };
    refreshPresetList();

    a1EQBypassButton.onStateChange();
    updatePanelVisibility();

    if (!DumbDockAmpAudioProcessor::welcomeHidden())
        juce::MessageManager::callAsync([safe = juce::Component::SafePointer<DumbDockAmpAudioProcessorEditor>(this)] {
            if (safe != nullptr) safe->showWelcome(); });
}

DumbDockAmpAudioProcessorEditor::~DumbDockAmpAudioProcessorEditor() {}

void DumbDockAmpAudioProcessorEditor::showWelcome()
{
    if (welcome != nullptr) return;
    welcome = std::make_unique<WelcomePopup>([this] {
        juce::Component::SafePointer<DumbDockAmpAudioProcessorEditor> safe(this);      // made here, not in the capture list (MSVC rejects `this` there)
        juce::MessageManager::callAsync([safe] {
            if (safe != nullptr) safe->hideWelcome(); });
    });
    addAndMakeVisible(*welcome);
    welcome->setBounds(getLocalBounds());
    welcome->toFront(true);
    welcome->grabKeyboardFocus();
}

void DumbDockAmpAudioProcessorEditor::hideWelcome() { welcome.reset(); repaint(); }

void DumbDockAmpAudioProcessorEditor::refreshPresetList()
{
    refreshingPresets = true;
    presetBox.clear(juce::dontSendNotification);
    presetBox.addItem(PresetManager::kDefaultName, 1);
    const auto names = audioProcessor.presets.userPresetNames();
    presetBox.addSeparator();
    for (int i = 0; i < names.size(); ++i) presetBox.addItem(names[i], 2 + i);
    const auto cur = audioProcessor.presets.currentName();
    const int idx = names.indexOf(cur);
    presetBox.setSelectedId(idx >= 0 ? 2 + idx : 1, juce::dontSendNotification);
    presetDelete.setEnabled(idx >= 0);
    refreshingPresets = false;
}

void DumbDockAmpAudioProcessorEditor::stepPreset(int dir)
{
    // order: Default, then the user presets (the same order as the drop-down); wraps around at both ends
    const int n = 1 + audioProcessor.presets.userPresetNames().size();
    const auto cur = audioProcessor.presets.currentName();
    int idx = 0;
    if (cur != PresetManager::kDefaultName) { const int u = audioProcessor.presets.userPresetNames().indexOf(cur); idx = u >= 0 ? 1 + u : 0; }
    idx = ((idx + dir) % n + n) % n;
    presetBox.setSelectedId(idx == 0 ? 1 : 2 + (idx - 1), juce::sendNotification);
}

void DumbDockAmpAudioProcessorEditor::askPresetName()
{
    auto* w = new juce::AlertWindow("Save preset", "Name for this preset (an existing name is overwritten):", juce::MessageBoxIconType::NoIcon, this);
    const auto cur = audioProcessor.presets.currentName();
    w->addTextEditor("name", cur == PresetManager::kDefaultName ? juce::String() : cur);
    w->addButton("Save", 1, juce::KeyPress(juce::KeyPress::returnKey));
    w->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
    w->enterModalState(true, juce::ModalCallbackFunction::create([safe = juce::Component::SafePointer<DumbDockAmpAudioProcessorEditor>(this), w](int result) {
        if (result == 1 && safe != nullptr) {
            safe->audioProcessor.presets.saveUser(w->getTextEditorContents("name"));
            safe->refreshPresetList();
        }
    }), true);
}

//==============================================================================
void DumbDockAmpAudioProcessorEditor::buildAmp001Panel()
{
    styleLabel(a1TitleLabel, "AMP-001  |  \"DUMB 1\"  |  FOUR-CHANNEL BOUTIQUE HEAD", kGreen, 13.0f);
    amp001Panel.addAndMakeVisible(a1TitleLabel);

    a1ModeBox.addItemList({ "1: CLEAN", "2: CRUNCH", "3: LEAD", "4: ULTRA" }, 1);
    amp001Panel.addAndMakeVisible(a1ModeBox);
    a1ModeAttach = std::make_unique<ComboAttach>(audioProcessor.apvts, ids::a1Mode, a1ModeBox);
    a1ModeBox.onChange = [this] { repaint(); };
    a1PowerTubeBox.addItemList({ "6L6GC", "EL34", "KT88" }, 1);
    amp001Panel.addAndMakeVisible(a1PowerTubeBox);
    a1PowerTubeAttach = std::make_unique<ComboAttach>(audioProcessor.apvts, ids::a1PowerTube, a1PowerTubeBox);
    styleCombo(a1ModeBox, kGreen); styleCombo(a1PowerTubeBox, kGreen);

    styleLabel(a1ModeLabel, "ACTIVE CHANNEL (SW1)", kGreen);
    styleLabel(a1PowerTubeLabel, "POWER TUBE", kGreen);
    styleLabel(a1EQLabel, "6-BAND EQ  (+/-15 dB)", kGreen);
    for (auto* l : { &a1ModeLabel, &a1PowerTubeLabel, &a1EQLabel }) amp001Panel.addAndMakeVisible(l);

    // one strip per channel, each with its own GAIN / BASS / MID / TREBLE
    for (int m = 0; m < kNumDumbChannels; ++m) {
        styleLabel(a1Strip[m].title, kModeTitles[m], kGreen, 10.0f, juce::Justification::centred);
        amp001Panel.addAndMakeVisible(a1Strip[m].title);
        bindKnob(a1Strip[m].gain,   amp001Panel, "GAIN",   ids::a1Ch(m, ids::gain),   kGreen);
        bindKnob(a1Strip[m].bass,   amp001Panel, "BASS",   ids::a1Ch(m, ids::bass),   kGreen);
        bindKnob(a1Strip[m].mid,    amp001Panel, "MID",    ids::a1Ch(m, ids::mid),    kGreen);
        bindKnob(a1Strip[m].treble, amp001Panel, "TREBLE", ids::a1Ch(m, ids::treble), kGreen);
    }
    // controls shared by all channels
    bindKnob(a1Character, amp001Panel, "CHAR",   ids::a1Character, kGreen);
    bindKnob(a1Presence,  amp001Panel, "PRES",   ids::a1Presence,  kGreen);
    bindKnob(a1Reverb,    amp001Panel, "VERB",   ids::a1Reverb,    kGreen);
    bindKnob(a1Gate,      amp001Panel, "GATE",   ids::a1Gate,      kMint);
    bindKnob(a1Master,    amp001Panel, "MASTER", ids::a1Master,    kGreen);
    a1Gate.knob.setTooltip("Noise gate threshold. Fully left = gate off; turn up until the hiss between notes disappears");

    // 6 EQ faders
    const char* eqLabels[6] = { "80", "250", "800", "2.5k", "6k", "12k" };
    for (int i = 0; i < 6; ++i) {
        auto& e = a1EQ[i];
        e.slider.setSliderStyle(juce::Slider::LinearVertical);
        e.slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 54, 16);
        e.slider.setColour(juce::Slider::thumbColourId, kCyan);
        e.slider.setColour(juce::Slider::trackColourId, kCyan.withAlpha(0.55f));
        e.slider.setColour(juce::Slider::backgroundColourId, juce::Colour(0xff1a2a2a));
        e.slider.setColour(juce::Slider::textBoxTextColourId, kCyan);
        e.slider.setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        e.slider.setDoubleClickReturnValue(true, 0.0);
        e.slider.textFromValueFunction = [](double v) { return juce::String(v * 15.0, 1) + " dB"; };
        e.slider.valueFromTextFunction = [](const juce::String& t) { return t.getDoubleValue() / 15.0; };
        amp001Panel.addAndMakeVisible(e.slider);
        e.attach = std::make_unique<SliderAttach>(audioProcessor.apvts, juce::String(ids::a1EQ(i)), e.slider);
        styleLabel(e.label, eqLabels[i], kCyan, 10.0f, juce::Justification::centred);
        amp001Panel.addAndMakeVisible(e.label);
    }

    a1EQBypassButton.setButtonText("EQ BYPASS");
    a1EQBypassButton.setTooltip("Switch the six-band EQ out of the signal path (your slider settings are kept)");
    a1EQBypassButton.setColour(juce::ToggleButton::textColourId, kCyan);
    a1EQBypassButton.setColour(juce::ToggleButton::tickColourId, kCyan);
    amp001Panel.addAndMakeVisible(a1EQBypassButton);
    a1EQBypassAttach = std::make_unique<ButtonAttach>(audioProcessor.apvts, ids::a1EQBypass, a1EQBypassButton);
    a1EQBypassButton.onStateChange = [this] { for (auto& e : a1EQ) e.slider.setAlpha(a1EQBypassButton.getToggleState() ? 0.35f : 1.0f); };
}

void DumbDockAmpAudioProcessorEditor::buildAmp002Panel()
{
    styleLabel(a2TitleLabel, "AMP-002  |  \"DOCK\"  |  50W MIDI-CONTROLLED COMBO", kAmber, 13.0f);
    amp002Panel.addAndMakeVisible(a2TitleLabel);

    a2ChannelBox.addItemList({ "A: PRISTINE", "B: CHIME" }, 1);
    amp002Panel.addAndMakeVisible(a2ChannelBox);
    a2ChannelAttach = std::make_unique<ComboAttach>(audioProcessor.apvts, ids::a2Channel, a2ChannelBox);
    a2ChannelBox.onChange = [this] { repaint(); };
    styleCombo(a2ChannelBox, kAmber);
    styleLabel(a2ChannelLabel, "ACTIVE CHANNEL (MIDI CC1)", kAmber);
    amp002Panel.addAndMakeVisible(a2ChannelLabel);

    for (int c = 0; c < kNumDockChannels; ++c) {
        styleLabel(a2Strip[c].title, kChanTitles[c], kAmber, 10.0f, juce::Justification::centred);
        amp002Panel.addAndMakeVisible(a2Strip[c].title);
        bindKnob(a2Strip[c].gain,   amp002Panel, "GAIN",   ids::a2Ch(c, ids::gain),   kAmber);
        bindKnob(a2Strip[c].bass,   amp002Panel, "BASS",   ids::a2Ch(c, ids::bass),   kAmber);
        bindKnob(a2Strip[c].mid,    amp002Panel, "MID",    ids::a2Ch(c, ids::mid),    kAmber);
        bindKnob(a2Strip[c].treble, amp002Panel, "TREBLE", ids::a2Ch(c, ids::treble), kAmber);
    }
    bindKnob(a2Presence, amp002Panel, "PRES",   ids::a2Presence, kAmber);
    bindKnob(a2Reverb,   amp002Panel, "VERB",   ids::a2Reverb,   kAmber);
    bindKnob(a2Master,   amp002Panel, "MASTER", ids::a2Master,   kAmber);

    a2StandbyButton.setButtonText("STANDBY (CC80)");
    a2StandbyButton.setColour(juce::ToggleButton::textColourId, kAmber); amp002Panel.addAndMakeVisible(a2StandbyButton);
    a2StandbyAttach = std::make_unique<ButtonAttach>(audioProcessor.apvts, ids::a2Standby, a2StandbyButton);

    styleLabel(a2MidiIndicator, "MIDI CC MAP:  CC1=CH  CC7=VOL  CC11=VERB  CC80=SBY", kMint, 8.5f);
    amp002Panel.addAndMakeVisible(a2MidiIndicator);
}

void DumbDockAmpAudioProcessorEditor::buildCabPanels()
{
    for (int c = 0; c < kNumCabs; ++c) {
        CabStrip& cs = cabs[c];
        styleLabel(cs.title, c == 0 ? "CAB A  -  4x12 GREENBACK" : "CAB B  -  1x12 VINTAGE 30", kAmber, 11.0f);
        cs.panel.addAndMakeVisible(cs.title);
        bindKnob(cs.level, cs.panel, "CAB LVL", ids::cab(c, "level"), kAmber);

        for (int m = 0; m < kNumMics; ++m) {
            MicStrip& ms = cs.mic[m];
            styleLabel(ms.title, m == 0 ? "MIC 1" : "MIC 2", kAmber, 9.0f);
            cs.panel.addAndMakeVisible(ms.title);
            ms.type.addItemList(juce::StringArray(kMicNames, 5), 1);
            styleCombo(ms.type, kAmber);
            cs.panel.addAndMakeVisible(ms.type);
            ms.typeAttach = std::make_unique<ComboAttach>(audioProcessor.apvts, juce::String(ids::cabMic(c, m, "type")), ms.type);
            bindKnob(ms.pos,   cs.panel, "POS",  ids::cabMic(c, m, "pos"),   kAmber);
            bindKnob(ms.dist,  cs.panel, "DIST", ids::cabMic(c, m, "dist"),  kAmber);
            bindKnob(ms.level, cs.panel, "LVL",  ids::cabMic(c, m, "level"), kAmber);
            bindKnob(ms.pan,   cs.panel, "PAN",  ids::cabMic(c, m, "pan"),   kAmber);
            ms.phase.setButtonText("PHASE INVERT");
            ms.phase.setTooltip("Flip this microphone's polarity (changes the sound when two mics are on, and is also useful against the amp's phase)");
            for (auto* b : { &ms.on, &ms.phase }) { b->setColour(juce::ToggleButton::textColourId, kAmber); b->setColour(juce::ToggleButton::tickColourId, kAmber); }
            cs.panel.addAndMakeVisible(ms.phase);
            ms.phaseAttach = std::make_unique<ButtonAttach>(audioProcessor.apvts, juce::String(ids::cabMic(c, m, "phase")), ms.phase);
            if (m == 1) {
                ms.on.setButtonText("MIC 2 ON");
                cs.panel.addAndMakeVisible(ms.on);
                ms.onAttach = std::make_unique<ButtonAttach>(audioProcessor.apvts, juce::String(ids::cabMic(c, m, "on")), ms.on);
            }
        }
    }
}

void DumbDockAmpAudioProcessorEditor::updatePanelVisibility()
{
    const bool isDock = ampSelector.getSelectedId() == 2;
    amp001Panel.setVisible(!isDock);
    amp002Panel.setVisible(isDock);
    repaint();
}

//==============================================================================
void DumbDockAmpAudioProcessorEditor::paint(juce::Graphics& g)
{
    const bool isDock = ampSelector.getSelectedId() == 2;
    const auto bgCol  = isDock ? kAmberBg : kGreenBg;
    const auto accent = isDock ? kAmber : kGreen;

    g.fillAll(bgCol);
    g.addTransform(juce::AffineTransform::scale((float) getWidth() / (float) kBaseW));          // everything below is in design-space units
    g.setColour(accent.withAlpha(0.04f));
    for (int x = 0; x < kBaseW; x += 24)  g.drawVerticalLine(x, 0, (float) kBaseH);
    for (int y = 0; y < kBaseH; y += 24) g.drawHorizontalLine(y, 0, (float) kBaseW);

    g.setColour(bgCol.darker(0.3f));
    g.fillRect(0, 0, kBaseW, 56);
    g.setColour(accent);
    g.drawLine(0, 56, (float) kBaseW, 56, 2.0f);
    g.setFont(mono(22.0f));
    g.drawText("DUMB-DOCK AMP", 16, 6, 400, 30, juce::Justification::left);
    g.setFont(mono(9.0f, false));
    g.setColour(accent.withAlpha(0.6f));
    g.drawText("SCHEMATIC-ACCURATE DUAL AMP MODELER  |  REV 02", 16, 34, 400, 16, juce::Justification::left);

    // amp panel
    auto& ampPanel = isDock ? amp002Panel : amp001Panel;
    g.setColour((isDock ? kAmberPanel : kGreenPanel).withAlpha(0.5f)); g.fillRect(ampPanel.getBounds());
    g.setColour(accent.withAlpha(0.3f));                                g.drawRect(ampPanel.getBounds(), 1);

    // frame around the active channel strip (knob positions are those of resized())
    {
        const auto origin = ampPanel.getPosition();
        const int active = isDock ? a2ChannelBox.getSelectedItemIndex() : a1ModeBox.getSelectedItemIndex();
        const auto& tl = isDock ? a2Strip[juce::jlimit(0, kNumDockChannels - 1, active)].title : a1Strip[juce::jlimit(0, kNumDumbChannels - 1, active)].title;
        const auto& bt = isDock ? a2Strip[juce::jlimit(0, kNumDockChannels - 1, active)].treble.knob : a1Strip[juce::jlimit(0, kNumDumbChannels - 1, active)].treble.knob;
        const juce::Rectangle<int> r(tl.getX(), tl.getY(), tl.getWidth(), bt.getBottom() - tl.getY());
        g.setColour(accent.withAlpha(0.9f));
        g.drawRoundedRectangle(r.translated(origin.x, origin.y).expanded(2).toFloat(), 5.0f, 1.8f);
    }

    // cab panels (active one brighter)
    const int activeCab = cabSelector.getSelectedItemIndex();
    for (int c = 0; c < kNumCabs; ++c) {
        g.setColour(juce::Colour(0xff0e0a02).withAlpha(0.6f)); g.fillRect(cabs[c].panel.getBounds());
        g.setColour(kAmber.withAlpha(c == activeCab ? 0.9f : 0.28f)); g.drawRect(cabs[c].panel.getBounds(), c == activeCab ? 2 : 1);
    }
}

void DumbDockAmpAudioProcessorEditor::resized()
{
    if (welcome != nullptr) welcome->setBounds(getLocalBounds());
    content.setBounds(0, 0, kBaseW, kBaseH);
    content.setTransform(juce::AffineTransform::scale((float) getWidth() / (float) kBaseW));
    infoButton.setBounds(kBaseW - 46, 14, 30, 28);
    auto area = juce::Rectangle<int>(0, 0, kBaseW, kBaseH);
    area.removeFromTop(56);

    // selector row: amp + presets (the cab selector lives with the cabinets, further down)
    {
        auto row = area.removeFromTop(44).reduced(16, 6);
        auto a = row.removeFromLeft(260); ampSelectorLabel.setBounds(a.removeFromTop(14)); ampSelector.setBounds(a);
        row.removeFromLeft(30);
        auto pr = row.removeFromLeft(380);
        presetLabel.setBounds(pr.removeFromTop(14));
        presetDelete.setBounds(pr.removeFromRight(70)); pr.removeFromRight(6);
        presetSave.setBounds(pr.removeFromRight(70));   pr.removeFromRight(6);
        presetPrev.setBounds(pr.removeFromLeft(28));    pr.removeFromLeft(4);
        presetNext.setBounds(pr.removeFromRight(28));   pr.removeFromRight(4);
        presetBox.setBounds(pr);
    }

    // right column: meters + trims
    {
        auto col = area.removeFromRight(100).reduced(8);
        meterLabelIn.setBounds(col.removeFromTop(16));
        auto im = col.removeFromTop(150);  inputMeter.setBounds(im.withSizeKeepingCentre(34, im.getHeight()).reduced(2));
        col.removeFromTop(8);
        meterLabelOut.setBounds(col.removeFromTop(16));
        auto om = col.removeFromTop(150);  outputMeter.setBounds(om.withSizeKeepingCentre(34, om.getHeight()).reduced(2));
        col.removeFromTop(14);
        ioLabel.setBounds(col.removeFromTop(14));
        inputTrim.knob.setBounds(col.removeFromTop(96));
        outputTrim.knob.setBounds(col.removeFromTop(96));
    }

    auto main = area.reduced(16, 6);
    auto ampArea = main.removeFromTop(420);
    amp001Panel.setBounds(ampArea);
    amp002Panel.setBounds(ampArea);
    main.removeFromTop(4);
    {   // cab selector right above the cabinets it controls
        auto row = main.removeFromTop(44).reduced(0, 3);
        auto c = row.removeFromLeft(260); cabSelectorLabel.setBounds(c.removeFromTop(14)); cabSelector.setBounds(c);
        row.removeFromLeft(20);
        cabOffButton.setBounds(row.removeFromLeft(110).withTrimmedTop(14));
    }
    main.removeFromTop(2);
    const int cabW = (main.getWidth() - 12) / 2;
    cabs[0].panel.setBounds(main.removeFromLeft(cabW));
    main.removeFromLeft(12);
    cabs[1].panel.setBounds(main);

    const int kw = 78, kh = 94;                                    // knob cell

    // ===== AMP-001 =====
    {
        auto a = amp001Panel.getLocalBounds().reduced(14, 8);
        a1TitleLabel.setBounds(a.removeFromTop(22));
        a.removeFromTop(4);
        auto top = a.removeFromTop(44);
        auto m = top.removeFromLeft(170); a1ModeLabel.setBounds(m.removeFromTop(14)); a1ModeBox.setBounds(m.removeFromTop(26));
        top.removeFromLeft(14);
        auto t = top.removeFromLeft(120); a1PowerTubeLabel.setBounds(t.removeFromTop(14)); a1PowerTubeBox.setBounds(t.removeFromTop(26));
        a.removeFromTop(4);

        // four channel strips (2 x 2 knobs each) on the left, shared controls on the right
        auto strips = a.removeFromTop(2 * kh + 22);
        for (int i = 0; i < kNumDumbChannels; ++i) {
            auto s = strips.removeFromLeft(2 * kw + 6);
            strips.removeFromLeft(6);
            a1Strip[i].title.setBounds(s.removeFromTop(20));
            auto r1 = s.removeFromTop(kh), r2 = s.removeFromTop(kh);
            a1Strip[i].gain.knob.setBounds(r1.removeFromLeft(kw));   a1Strip[i].bass.knob.setBounds(r1.removeFromLeft(kw));
            a1Strip[i].mid.knob.setBounds(r2.removeFromLeft(kw));    a1Strip[i].treble.knob.setBounds(r2.removeFromLeft(kw));
        }
        strips.removeFromLeft(10);
        auto g1 = strips.removeFromTop(kh + 20).withTrimmedTop(20), g2 = strips.removeFromTop(kh);
        a1Character.knob.setBounds(g1.removeFromLeft(kw)); a1Presence.knob.setBounds(g1.removeFromLeft(kw)); a1Reverb.knob.setBounds(g1.removeFromLeft(kw));
        a1Gate.knob.setBounds(g2.removeFromLeft(kw)); a1Master.knob.setBounds(g2.removeFromLeft(kw + 8));

        // EQ faders along the bottom of the panel
        a.removeFromTop(4);
        auto eqLeft = a.removeFromLeft(150);
        a1EQLabel.setBounds(eqLeft.removeFromTop(16));
        a1EQBypassButton.setBounds(eqLeft.removeFromTop(28));
        auto eq = a;
        const int ew = 74;
        for (int i = 0; i < 6; ++i) {
            auto cell = eq.removeFromLeft(ew);
            a1EQ[i].label.setBounds(cell.removeFromTop(14));
            a1EQ[i].slider.setBounds(cell);
        }
    }

    // ===== AMP-002 =====
    {
        auto a = amp002Panel.getLocalBounds().reduced(14, 8);
        a2TitleLabel.setBounds(a.removeFromTop(22));
        a.removeFromTop(4);
        auto top = a.removeFromTop(44);
        auto c = top.removeFromLeft(190); a2ChannelLabel.setBounds(c.removeFromTop(14)); a2ChannelBox.setBounds(c.removeFromTop(26));
        top.removeFromLeft(14);
        a2StandbyButton.setBounds(top.removeFromLeft(140).withTrimmedTop(16));
        a.removeFromTop(8);

        auto strips = a.removeFromTop(2 * kh + 22);
        for (int i = 0; i < kNumDockChannels; ++i) {
            auto s = strips.removeFromLeft(2 * kw + 6);
            strips.removeFromLeft(10);
            a2Strip[i].title.setBounds(s.removeFromTop(20));
            auto r1 = s.removeFromTop(kh), r2 = s.removeFromTop(kh);
            a2Strip[i].gain.knob.setBounds(r1.removeFromLeft(kw)); a2Strip[i].bass.knob.setBounds(r1.removeFromLeft(kw));
            a2Strip[i].mid.knob.setBounds(r2.removeFromLeft(kw));  a2Strip[i].treble.knob.setBounds(r2.removeFromLeft(kw));
        }
        strips.removeFromLeft(14);
        auto g1 = strips.removeFromTop(kh + 20).withTrimmedTop(20);
        a2Presence.knob.setBounds(g1.removeFromLeft(kw)); a2Reverb.knob.setBounds(g1.removeFromLeft(kw)); a2Master.knob.setBounds(g1.removeFromLeft(kw + 8));
        a.removeFromTop(10);
        a2MidiIndicator.setBounds(a.removeFromTop(18));
    }

    // ===== cabinets =====
    for (int c = 0; c < kNumCabs; ++c) {
        CabStrip& cs = cabs[c];
        auto a = cs.panel.getLocalBounds().reduced(10, 6);
        cs.title.setBounds(a.removeFromTop(18));
        const int w = 66, h = 92;
        for (int m = 0; m < kNumMics; ++m) {
            MicStrip& ms = cs.mic[m];
            auto row = a.removeFromTop(h + 14);
            auto left = row.removeFromLeft(124);
            ms.title.setBounds(left.removeFromTop(14));
            ms.type.setBounds(left.removeFromTop(24));
            if (m == 1) ms.on.setBounds(left.removeFromTop(22));
            ms.phase.setBounds(left.removeFromTop(22));
            row.removeFromTop(0);
            auto knobs = row.withTrimmedTop(0);
            ms.pos.knob.setBounds(knobs.removeFromLeft(w));  ms.dist.knob.setBounds(knobs.removeFromLeft(w));
            ms.level.knob.setBounds(knobs.removeFromLeft(w)); ms.pan.knob.setBounds(knobs.removeFromLeft(w));
            if (m == 0) { knobs.removeFromLeft(4); cs.level.knob.setBounds(knobs.removeFromLeft(w + 2)); }
        }
    }
}
