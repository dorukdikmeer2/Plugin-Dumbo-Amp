#pragma once
#include <JuceHeader.h>

//==============================================================================
// AmpKnob — custom-painted rotary control styled after the vintage chicken-head
// / skirted knobs on the AMP-001 and AMP-002 front panels (per schematic art)
//==============================================================================
class AmpKnob : public juce::Slider
{
public:
    AmpKnob() {
        setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        setTextBoxStyle(juce::Slider::TextBoxBelow, false, 56, 16);
        setRotaryParameters(juce::MathConstants<float>::pi * 1.2f,
                             juce::MathConstants<float>::pi * 2.8f, true);
    }

    void setAccentColour(juce::Colour c) { accent = c; repaint(); }
    void setKnobLabel(const juce::String& l) { label = l; repaint(); }

    void paint(juce::Graphics& g) override {
        auto bounds = getLocalBounds().toFloat();
        auto textBoxH = 16.0f;
        auto knobArea = bounds.withTrimmedBottom(textBoxH + 14.0f);
        auto diameter = juce::jmin(knobArea.getWidth(), knobArea.getHeight()) - 6.0f;
        auto cx = knobArea.getCentreX();
        auto cy = knobArea.getCentreY();
        auto radius = diameter * 0.5f;

        // Outer bezel
        g.setColour(juce::Colour(0xff1a1209));
        g.fillEllipse(cx - radius - 3, cy - radius - 3, (radius + 3) * 2, (radius + 3) * 2);
        g.setColour(accent.withAlpha(0.5f));
        g.drawEllipse(cx - radius - 3, cy - radius - 3, (radius + 3) * 2, (radius + 3) * 2, 1.2f);

        // Knob body — radial gradient
        juce::ColourGradient grad(juce::Colour(0xff3a2f1f), cx, cy - radius,
                                   juce::Colour(0xff15100a), cx, cy + radius, false);
        g.setGradientFill(grad);
        g.fillEllipse(cx - radius, cy - radius, radius * 2, radius * 2);
        g.setColour(accent);
        g.drawEllipse(cx - radius, cy - radius, radius * 2, radius * 2, 1.5f);

        // Rotation angle
        auto rotaryStart = getRotaryParameters().startAngleRadians;
        auto rotaryEnd   = getRotaryParameters().endAngleRadians;
        auto normValue   = (float)valueToProportionOfLength(getValue());
        auto angle = rotaryStart + normValue * (rotaryEnd - rotaryStart);

        // Pointer line (chicken-head style)
        juce::Path pointer;
        float pointerLen = radius * 0.78f;
        float pointerW = 3.0f;
        pointer.addRoundedRectangle(-pointerW * 0.5f, -radius + 4.0f, pointerW, pointerLen, 1.5f);
        g.setColour(accent);
        g.fillPath(pointer, juce::AffineTransform::rotation(angle).translated(cx, cy));

        // Centre cap
        g.setColour(juce::Colour(0xff0d0a06));
        g.fillEllipse(cx - radius * 0.22f, cy - radius * 0.22f, radius * 0.44f, radius * 0.44f);

        // Tick marks around the perimeter
        g.setColour(accent.withAlpha(0.35f));
        for (int i = 0; i <= 10; ++i) {
            float t = (float)i / 10.0f;
            float a = rotaryStart + t * (rotaryEnd - rotaryStart);
            float r1 = radius + 5.0f, r2 = radius + 9.0f;
            juce::Point<float> p1(cx + r1 * std::sin(a), cy - r1 * std::cos(a));
            juce::Point<float> p2(cx + r2 * std::sin(a), cy - r2 * std::cos(a));
            g.drawLine({ p1, p2 }, 1.0f);
        }

        // Label
        g.setColour(accent);
        g.setFont(juce::Font(juce::Font::getDefaultMonospacedFontName(), 9.5f, juce::Font::bold));
        g.drawText(label, bounds.withY(knobArea.getBottom() - 2).withHeight(14.0f),
                   juce::Justification::centred);
    }

private:
    juce::Colour accent { 0xff00e050 };
    juce::String label;
};
