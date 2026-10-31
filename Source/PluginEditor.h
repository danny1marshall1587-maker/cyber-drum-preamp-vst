#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "PluginProcessor.h"
#include "CustomDrumLookAndFeel.h"
#include "DrumShellVisualizer.h"

namespace CyberDrum {

class DrumInputMeter : public juce::Component {
public:
    void setLevels(float rmsDb, float peakDb) {
        currentRmsDb = rmsDb;
        currentPeakDb = peakDb;
        repaint();
    }

    void paint(juce::Graphics& g) override {
        auto bounds = getLocalBounds().toFloat();
        // Background track
        g.setColour(juce::Colour(0xff121110));
        g.fillRoundedRectangle(bounds, 4.0f);
        g.setColour(juce::Colour(0xff2e2926));
        g.drawRoundedRectangle(bounds, 4.0f, 1.0f);

        auto meterBarArea = bounds.reduced(3.0f);
        float w = meterBarArea.getWidth();
        float h = meterBarArea.getHeight();

        auto dbToX = [w](float db) {
            float norm = std::clamp((db + 48.0f) / 48.0f, 0.0f, 1.0f);
            return norm * w;
        };

        // Zone background shading:
        // Good: -24 to -12 dB (cyan/emerald target sweet spot)
        // Hot: -12 to -3 dB (amber warning)
        // Clip: -3 to 0 dB (red warning)
        float xLow = dbToX(-24.0f);
        float xGood = dbToX(-12.0f);
        float xHot = dbToX(-3.0f);

        g.setColour(juce::Colour(0x2206b6d4)); // Cyan/Emerald zone
        g.fillRect(meterBarArea.getX() + xLow, meterBarArea.getY(), xGood - xLow, h);
        g.setColour(juce::Colour(0x22f59e0b)); // Amber zone
        g.fillRect(meterBarArea.getX() + xGood, meterBarArea.getY(), xHot - xGood, h);
        g.setColour(juce::Colour(0x33ef4444)); // Red zone
        g.fillRect(meterBarArea.getX() + xHot, meterBarArea.getY(), w - xHot, h);

        // Active RMS Bar fill
        float rmsX = dbToX(currentRmsDb);
        if (rmsX > 1.0f) {
            juce::Colour fillColour;
            if (currentRmsDb < -24.0f) fillColour = juce::Colour(0xff3b82f6); // Cyan/Blue
            else if (currentRmsDb <= -12.0f) fillColour = juce::Colour(0xff06b6d4); // Cyan OPTIMAL
            else if (currentRmsDb <= -3.0f) fillColour = juce::Colour(0xfff59e0b);  // Amber HOT
            else fillColour = juce::Colour(0xffef4444); // Red CLIP

            g.setColour(fillColour.withAlpha(0.85f));
            g.fillRoundedRectangle(meterBarArea.getX(), meterBarArea.getY(), rmsX, h, 2.0f);
        }

        // Peak needle line
        float peakX = dbToX(currentPeakDb);
        if (peakX > 2.0f) {
            g.setColour(juce::Colour(0xffffffff));
            g.fillRect(meterBarArea.getX() + peakX - 1.0f, meterBarArea.getY(), 2.0f, h);
        }

        // Zone Tick Lines
        g.setColour(juce::Colour(0xff554d46));
        g.drawVerticalLine((int)(meterBarArea.getX() + xLow), meterBarArea.getY(), meterBarArea.getBottom());
        g.drawVerticalLine((int)(meterBarArea.getX() + xGood), meterBarArea.getY(), meterBarArea.getBottom());
        g.drawVerticalLine((int)(meterBarArea.getX() + xHot), meterBarArea.getY(), meterBarArea.getBottom());

        // Status Label Text in Center
        g.setFont(juce::FontOptions(9.5f));
        if (currentRmsDb >= -24.0f && currentRmsDb <= -12.0f) {
            g.setColour(juce::Colour(0xff38bdf8)); // Glowing bright cyan
            g.drawText("● OPTIMAL SIGNAL (-24..-12dB)", bounds, juce::Justification::centred, false);
        } else if (currentRmsDb > -12.0f) {
            g.setColour(juce::Colour(0xfffca5a5));
            g.drawText(currentRmsDb > -3.0f ? "▲ SIGNAL CLIPPING" : "▲ SIGNAL HOT", bounds, juce::Justification::centred, false);
        } else if (currentRmsDb > -45.0f) {
            g.setColour(juce::Colour(0xff94a3b8));
            g.drawText("▼ SIGNAL LOW (BOOST GAIN)", bounds, juce::Justification::centred, false);
        } else {
            g.setColour(juce::Colour(0xff5c534b));
            g.drawText("INPUT RMS METER", bounds, juce::Justification::centred, false);
        }
    }

private:
    float currentRmsDb = -100.0f;
    float currentPeakDb = -100.0f;
};

} // namespace CyberDrum

class CyberDrumEditor : public juce::AudioProcessorEditor, private juce::Timer {
public:
    explicit CyberDrumEditor(CyberDrumProcessor&);
    ~CyberDrumEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void loadPreset(int presetId);
    bool isLoadingPreset = false;
    int lastSyncedProfile = 0;

    CyberDrumProcessor& audioProcessor;
    CyberDrum::CustomDrumLookAndFeel customLookAndFeel;

    // Header Components
    juce::ComboBox presetSelector;
    juce::ComboBox inputSelector;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> inputAttachment;

    juce::Slider inputGainSlider;
    juce::Label inputGainLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> inputGainAttachment;

    CyberDrum::DrumInputMeter inputMeter;

    juce::ToggleButton masterBypassButton;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> masterBypassAttachment;

    // Column 1: D-NOISE & TRANSIENT SNAP
    juce::Slider denoiseAmountSlider;
    juce::Label denoiseAmountLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> denoiseAmountAttachment;

    juce::Slider denoiseThreshSlider;
    juce::Label denoiseThreshLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> denoiseThreshAttachment;

    juce::ToggleButton denoiseShieldToggle;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> denoiseShieldAttachment;

    juce::ToggleButton denoiseListenToggle;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> denoiseListenAttachment;

    juce::Slider stickSnapSlider;
    juce::Label stickSnapLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> stickSnapAttachment;

    juce::Slider deBoxSlider;
    juce::Label deBoxLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> deBoxAttachment;

    juce::Slider sympatheticBleedSlider;
    juce::Label sympatheticBleedLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> sympatheticBleedAttachment;

    // Column 2: DRUM SHELL & PNEUMATIC VENT
    juce::ComboBox drumProfileBox;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> drumProfileAttachment;

    juce::Slider morphAmountSlider;
    juce::Label morphAmountLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> morphAmountAttachment;

    CyberDrum::DrumShellVisualizer visualizer;

    juce::Slider customTuningSlider;
    juce::Label customTuningLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> customTuningAttachment;

    juce::Slider shellDepthSlider;
    juce::Label shellDepthLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> shellDepthAttachment;

    juce::Slider ventChokeSlider;
    juce::Label ventChokeLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> ventChokeAttachment;

    juce::Slider snareWireBuzzSlider;
    juce::Label snareWireBuzzLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> snareWireBuzzAttachment;

    // Column 3: STUDIO 4-BAND DRUM EQ
    juce::Slider eqLowGainSlider;
    juce::Label eqLowGainLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> eqLowGainAttachment;

    juce::Slider eqLowMidFreqSlider;
    juce::Label eqLowMidFreqLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> eqLowMidFreqAttachment;

    juce::Slider eqLowMidGainSlider;
    juce::Label eqLowMidGainLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> eqLowMidGainAttachment;

    juce::Slider eqHighMidFreqSlider;
    juce::Label eqHighMidFreqLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> eqHighMidFreqAttachment;

    juce::Slider eqHighMidGainSlider;
    juce::Label eqHighMidGainLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> eqHighMidGainAttachment;

    juce::Slider eqHighGainSlider;
    juce::Label eqHighGainLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> eqHighGainAttachment;

    // Column 4: WORLD-CLASS DRUM REVERB & MASTER
    juce::ComboBox reverbTypeBox;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> reverbTypeAttachment;

    juce::Slider reverbMixSlider;
    juce::Label reverbMixLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> reverbMixAttachment;

    juce::Slider reverbDecaySlider;
    juce::Label reverbDecayLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> reverbDecayAttachment;

    juce::Slider reverbSizeSlider;
    juce::Label reverbSizeLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> reverbSizeAttachment;

    juce::Slider reverbToneSlider;
    juce::Label reverbToneLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> reverbToneAttachment;

    juce::Slider stereoWidthSlider;
    juce::Label stereoWidthLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> stereoWidthAttachment;

    juce::Slider outputGainSlider;
    juce::Label outputGainLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> outputGainAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CyberDrumEditor)
};
