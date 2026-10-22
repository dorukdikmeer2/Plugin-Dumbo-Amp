#pragma once
#include <JuceHeader.h>
#include "../PluginProcessor.h"

//==============================================================================
// WelcomePopup - a short, read-only overview shown when the plugin window opens:
// the two amps, their options, and the two cabinets with their microphones.  It does not change any
// setting.  "Don't show this again" is remembered per user (not per project);
// the small "?" button in the title bar brings it back.
//==============================================================================
class WelcomePopup : public juce::Component
{
public:
    explicit WelcomePopup(std::function<void()> onClose) : closeCallback(std::move(onClose))
    {
        setInterceptsMouseClicks(true, true);
        setWantsKeyboardFocus(true);

        startBtn.setButtonText("START PLAYING");
        startBtn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff17381a));
        startBtn.setColour(juce::TextButton::textColourOffId, green());
        startBtn.onClick = [this] { close(); };
        addAndMakeVisible(startBtn);

        dontShow.setButtonText("Don't show this again");
        dontShow.setToggleState(DumbDockAmpAudioProcessor::welcomeHidden(), juce::dontSendNotification);
        dontShow.setColour(juce::ToggleButton::textColourId, juce::Colour(0xffc8d8c8));
        dontShow.setColour(juce::ToggleButton::tickColourId, green());
        addAndMakeVisible(dontShow);
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colours::black.withAlpha(0.74f));
        const auto panel = panelBounds();
        g.setColour(juce::Colour(0xff0b110b)); g.fillRoundedRectangle(panel.toFloat(), 8.0f);
        g.setColour(green());                  g.drawRoundedRectangle(panel.toFloat(), 8.0f, 1.5f);

        auto area = panel.reduced(26, 18);
        g.setColour(juce::Colours::white); g.setFont(mono(22.0f, true));
        g.drawText("WELCOME TO DUMBDOCK AMP", area.removeFromTop(30), juce::Justification::left);
        g.setColour(juce::Colour(0xff9ab09a)); g.setFont(mono(11.0f, false));
        g.drawText("Two tube amps and two cabinets in one plugin. Pick any amp with any cab.",
                   area.removeFromTop(18), juce::Justification::left);
        area.removeFromTop(10);

        auto cards = area.removeFromTop(250);
        auto left  = cards.removeFromLeft((cards.getWidth() - 14) / 2);
        cards.removeFromLeft(14);
        card(g, left,  green(), "AMP-001  \"DUMB 1\"", "Clean to Opeth-style high gain",
             { "4 channels: Clean / Crunch / Lead / Ultra - each with its OWN GAIN, BASS, MID, TREBLE",
               "Pick the active channel with ACTIVE CHANNEL (the lit frame shows which one plays)",
               "Shared: CHAR (bright bleed), PRES, MASTER, VERB, GATE (noise gate, left = off)",
               "6-band EQ faders (80 Hz - 12 kHz, +/-15 dB) with an EQ BYPASS switch",
               "POWER TUBE: 6L6GC / EL34 / KT88" });
        card(g, cards, amber(), "AMP-002  \"DOCK\"", "50 W EL34 combo, two different channels",
             { "A: PRISTINE - Laney / JC-style clean with big headroom (MIDI CC1)",
               "B: CHIME - bright, pushed GAIN walks into edge-of-breakup",
               "Each channel has its OWN GAIN, BASS, MID, TREBLE",
               "PRESENCE, MASTER, spring REVERB (CC11)",
               "STANDBY (CC80)" });

        area.removeFromTop(10);
        auto cab = area.removeFromTop(70);
        g.setColour(juce::Colour(0xff111a11)); g.fillRoundedRectangle(cab.toFloat(), 6.0f);
        g.setColour(juce::Colour(0xff4a5d4a)); g.drawRoundedRectangle(cab.toFloat(), 6.0f, 1.0f);
        auto c = cab.reduced(12, 6);
        g.setColour(juce::Colour(0xff8ab0ff)); g.setFont(mono(12.0f, true));
        g.drawText("CABINETS  (work with either amp, each remembers its own mics)", c.removeFromTop(18), juce::Justification::left);
        g.setColour(juce::Colour(0xffc8d8c8)); g.setFont(mono(11.0f, false));
        g.drawFittedText("CAB A 4x12 Greenback  |  CAB B 1x12 Vintage 30  |  MIC 1 + optional MIC 2: SM57 / SM58 / Royer 121 / AKG 414 / MD 421 "
                         "clones, POS, DIST, LVL, PAN (balance only), PHASE INVERT on each mic  |  CAB LVL (overall cab level)  |  CAB OFF (use your own IR)",
                         c, juce::Justification::left, 2);

        area.removeFromTop(8);
        g.setColour(juce::Colour(0xff9ab09a)); g.setFont(mono(11.0f, false));
        g.drawFittedText("PRESET (use < > to step through them): save your own amp / mic / cab / EQ settings, pick DEFAULT to return to the opening sound. Drag the window corner to resize. IN / OUT TRIM and the level meters are on the right - "
                         "aim for input peaks around -12 dBFS. A safety limiter at -3 dBFS (inter-sample aware) keeps every setting from clipping.", area.removeFromTop(46), juce::Justification::topLeft, 3);
    }

    void resized() override
    {
        auto area = panelBounds().reduced(26, 18);
        auto nav = area.removeFromBottom(34);
        startBtn.setBounds(nav.removeFromRight(180));
        dontShow.setBounds(nav.removeFromLeft(260));
    }

    bool keyPressed(const juce::KeyPress& k) override
    {
        if (k == juce::KeyPress::returnKey || k == juce::KeyPress::escapeKey) { close(); return true; }
        return false;
    }

private:
    static juce::Colour green() { return juce::Colour(0xff00e050); }
    static juce::Colour amber() { return juce::Colour(0xffffa000); }
    static juce::Font mono(float h, bool bold) { return juce::Font(juce::Font::getDefaultMonospacedFontName(), h, bold ? juce::Font::bold : juce::Font::plain); }
    juce::Rectangle<int> panelBounds() const { return getLocalBounds().withSizeKeepingCentre(780, 500); }

    void close()
    {
        DumbDockAmpAudioProcessor::setWelcomeHidden(dontShow.getToggleState());
        if (closeCallback) closeCallback();
    }

    static void card(juce::Graphics& g, juce::Rectangle<int> r, juce::Colour col, const juce::String& title,
                     const juce::String& sub, std::initializer_list<const char*> lines)
    {
        g.setColour(juce::Colour(0xff111a11)); g.fillRoundedRectangle(r.toFloat(), 6.0f);
        g.setColour(col.withAlpha(0.7f));      g.drawRoundedRectangle(r.toFloat(), 6.0f, 1.2f);
        auto a = r.reduced(14, 10);
        g.setColour(col); g.setFont(mono(15.0f, true));
        g.drawText(title, a.removeFromTop(22), juce::Justification::left);
        g.setColour(juce::Colour(0xff9ab09a)); g.setFont(mono(11.0f, false));
        g.drawText(sub, a.removeFromTop(18), juce::Justification::left);
        a.removeFromTop(6);
        g.setColour(juce::Colour(0xffd8e4d8)); g.setFont(mono(11.0f, false));
        for (auto* l : lines) {
            auto row = a.removeFromTop(26);
            g.setColour(col); g.fillEllipse((float) row.getX(), (float) row.getY() + 6.0f, 5.0f, 5.0f);
            g.setColour(juce::Colour(0xffd8e4d8));
            g.drawFittedText(l, row.withTrimmedLeft(12), juce::Justification::topLeft, 2);
        }
    }

    std::function<void()> closeCallback;
    juce::TextButton startBtn;
    juce::ToggleButton dontShow;
};
