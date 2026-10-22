#pragma once
#include <JuceHeader.h>

//==============================================================================
// VUMeter — vertical LED-style level meter for input/output metering
//==============================================================================
class VUMeter : public juce::Component, private juce::Timer
{
public:
    VUMeter() { startTimerHz(30); }

    void setLevelSource(std::atomic<float>* src) { levelSource = src; }

    void paint(juce::Graphics& g) override {
        auto bounds = getLocalBounds().toFloat();
        g.setColour(juce::Colour(0xff0a0f0a));
        g.fillRoundedRectangle(bounds, 2.0f);
        g.setColour(juce::Colour(0xff00e050).withAlpha(0.3f));
        g.drawRoundedRectangle(bounds.reduced(0.5f), 2.0f, 1.0f);

        const int numSegs = 24;                                   // -48 .. 0 dBFS, 2 dB per segment
        float segH = (bounds.getHeight() - 4.0f) / (float)numSegs;
        float db = juce::Decibels::gainToDecibels(smoothedLevel, -48.0f);
        float normLevel = juce::jlimit(0.0f, 1.0f, (db + 48.0f) / 48.0f);
        int litSegs = (int)(normLevel * numSegs);

        for (int i = 0; i < numSegs; ++i) {
            float y = bounds.getBottom() - 2.0f - (i + 1) * segH;
            juce::Rectangle<float> seg(bounds.getX() + 2.0f, y, bounds.getWidth() - 4.0f, segH - 1.5f);
            juce::Colour c;
            // colours by level: green up to -8 dBFS, amber -8 .. -4 dBFS, red above -4 dBFS (= the limiter ceiling region)
            const float segBottomDb = -48.0f + (float) i * 2.0f;
            if (segBottomDb < -8.0f)      c = juce::Colour(0xff00e050);
            else if (segBottomDb < -4.0f) c = juce::Colour(0xffffaa00);
            else                           c = juce::Colour(0xffff3b1e);

            if (i < litSegs)
                g.setColour(c);
            else
                g.setColour(c.withAlpha(0.08f));
            g.fillRect(seg);
        }
    }

private:
    void timerCallback() override {
        if (levelSource != nullptr) {
            float target = levelSource->load();
            smoothedLevel = smoothedLevel * 0.7f + target * 0.3f;
            repaint();
        }
    }

    std::atomic<float>* levelSource = nullptr;
    float smoothedLevel = 0.0f;
};
