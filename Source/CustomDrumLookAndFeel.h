#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace CyberDrum {

class CustomDrumLookAndFeel : public juce::LookAndFeel_V4 {
public:
    CustomDrumLookAndFeel() {
        setColour(juce::ResizableWindow::backgroundColourId, juce::Colour(0xff141312));
        setColour(juce::Slider::rotarySliderFillColourId, juce::Colour(0xff06b6d4));      // Cyan pneumatic glow
        setColour(juce::Slider::rotarySliderOutlineColourId, juce::Colour(0xff2b2724));   // Dark chassis
        setColour(juce::Slider::thumbColourId, juce::Colour(0xff38bdf8));                 // Electric sky indicator
        setColour(juce::ComboBox::backgroundColourId, juce::Colour(0xff22201e));
        setColour(juce::ComboBox::outlineColourId, juce::Colour(0xff443f3b));
        setColour(juce::ComboBox::textColourId, juce::Colour(0xffedd9be));
        setColour(juce::PopupMenu::backgroundColourId, juce::Colour(0xff1c1a19));
        setColour(juce::PopupMenu::textColourId, juce::Colour(0xffedd9be));
        setColour(juce::PopupMenu::highlightedBackgroundColourId, juce::Colour(0xff0891b2));
        setColour(juce::PopupMenu::highlightedTextColourId, juce::Colour(0xffffffff));
    }

    void drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
                          float sliderPos, const float rotaryStartAngle,
                          const float rotaryEndAngle, juce::Slider& slider) override {
        auto bounds = juce::Rectangle<int>(x, y, width, height).toFloat().reduced(4.0f);
        auto radius = juce::jmin(bounds.getWidth(), bounds.getHeight()) / 2.0f;
        auto toAngle = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);
        auto center = bounds.getCentre();

        // 1. Outer Dark Rim Recess
        g.setColour(juce::Colour(0xff0a0908));
        g.fillEllipse(center.x - radius, center.y - radius, radius * 2.0f, radius * 2.0f);

        // 2. Arc Track Background
        auto arcRadius = radius - 4.0f;
        juce::Path backgroundArc;
        backgroundArc.addCentredArc(center.x, center.y, arcRadius, arcRadius, 0.0f,
                                   rotaryStartAngle, rotaryEndAngle, true);
        g.setColour(juce::Colour(0xff2b2724));
        g.strokePath(backgroundArc, juce::PathStrokeType(4.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        // 3. Active Colored Arc
        juce::Path valueArc;
        valueArc.addCentredArc(center.x, center.y, arcRadius, arcRadius, 0.0f,
                               rotaryStartAngle, toAngle, true);
        g.setColour(slider.findColour(juce::Slider::rotarySliderFillColourId));
        g.strokePath(valueArc, juce::PathStrokeType(4.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        // 4. Machined Drum Lug / Knob Rotor Body
        auto knobRadius = radius - 10.0f;
        if (knobRadius > 4.0f) {
            juce::ColourGradient knobGrad(juce::Colour(0xff36322e), center.x - knobRadius, center.y - knobRadius,
                                         juce::Colour(0xff1c1a18), center.x + knobRadius, center.y + knobRadius, false);
            g.setGradientFill(knobGrad);
            g.fillEllipse(center.x - knobRadius, center.y - knobRadius, knobRadius * 2.0f, knobRadius * 2.0f);

            // Subtle bezel border (brushed chrome feel)
            g.setColour(juce::Colour(0xff554d46));
            g.drawEllipse(center.x - knobRadius, center.y - knobRadius, knobRadius * 2.0f, knobRadius * 2.0f, 1.2f);

            // 5. Dial Indicator Needle
            juce::Path p;
            auto pointerLength = knobRadius * 0.72f;
            auto pointerThickness = 2.5f;
            p.addRoundedRectangle(-pointerThickness * 0.5f, -knobRadius, pointerThickness, pointerLength, 1.2f);
            p.applyTransform(juce::AffineTransform::rotation(toAngle).translated(center.x, center.y));

            g.setColour(slider.findColour(juce::Slider::thumbColourId));
            g.fillPath(p);

            // Center Drum-Tension Cap Accent
            auto capRadius = knobRadius * 0.28f;
            g.setColour(juce::Colour(0xff272421));
            g.fillEllipse(center.x - capRadius, center.y - capRadius, capRadius * 2.0f, capRadius * 2.0f);
            g.setColour(juce::Colour(0xff78716c));
            g.drawEllipse(center.x - capRadius, center.y - capRadius, capRadius * 2.0f, capRadius * 2.0f, 1.0f);
        }
    }
};

} // namespace CyberDrum
