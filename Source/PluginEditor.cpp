#include "PluginProcessor.h"
#include "PluginEditor.h"

CyberDrumEditor::CyberDrumEditor(CyberDrumProcessor& p)
    : AudioProcessorEditor(&p), audioProcessor(p)
{
    setLookAndFeel(&customLookAndFeel);

    // 1. Preset Selector
    addAndMakeVisible(presetSelector);
    presetSelector.addItem("Preset: Bonham 26\" Giant Maple Kick", 1);
    presetSelector.addItem("Preset: 70s Dead Tea-Towel Kick", 2);
    presetSelector.addItem("Preset: Modern Punchy Birch 22\" Kick", 3);
    presetSelector.addItem("Preset: Sub-Piston 808 Cannon Kick", 4);
    presetSelector.addItem("Preset: Bell Brass Black Beauty Snare", 5);
    presetSelector.addItem("Preset: 70s Fat Wallet Studio Snare", 6);
    presetSelector.addItem("Preset: Tight Funk 13\" Piccolo Snare", 7);
    presetSelector.addItem("Preset: Studio Maple 5.5x14 Snare", 8);
    presetSelector.addItem("Preset: 12\" & 16\" Singing Studio Toms", 9);
    presetSelector.addItem("Preset: 18\" Bonham Low Thunder Tom", 10);
    presetSelector.addItem("Preset: Flamenco Deep Acoustic Cajon", 11);
    presetSelector.addItem("Preset: Acoustic Gig Live Cajon", 12);
    presetSelector.addItem("Preset: Afro-Cuban Conga / Quinto", 13);
    presetSelector.addItem("Preset: African Djembe Deep Bass", 14);
    presetSelector.addItem("Preset: Stereo Drum Bus / Room Glue", 15);
    presetSelector.setSelectedId(1, juce::dontSendNotification);
    presetSelector.onChange = [this]() {
        loadPreset(presetSelector.getSelectedId());
    };

    // 1b. Input Routing Selector
    addAndMakeVisible(inputSelector);
    inputSelector.addItem("Input: Mono (In 1 / Left)", 1);
    inputSelector.addItem("Input: Mono (In 2 / Right)", 2);
    inputSelector.addItem("Input: Auto-Detect Active", 3);
    inputSelector.addItem("Input: Stereo Sum (-3dB)", 4);
    inputSelector.addItem("Input: Stereo Discrete", 5);
    inputAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        audioProcessor.apvts, "inputRouting", inputSelector);

    // Helper for configuring rotary sliders
    auto setupRotary = [this](juce::Slider& s, juce::Label& l, const juce::String& text, const juce::String& suffix) {
        s.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        s.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 56, 16);
        s.setTextValueSuffix(suffix);
        addAndMakeVisible(s);

        l.setText(text, juce::dontSendNotification);
        l.setFont(juce::FontOptions(10.5f));
        l.setJustificationType(juce::Justification::centred);
        l.setColour(juce::Label::textColourId, juce::Colour(0xffd5c4b1));
        addAndMakeVisible(l);
    };

    // Helper for toggle switches
    auto setupToggle = [this](juce::ToggleButton& b, const juce::String& text) {
        b.setButtonText(text);
        b.setColour(juce::ToggleButton::textColourId, juce::Colour(0xffedd9be));
        b.setColour(juce::ToggleButton::tickColourId, juce::Colour(0xff06b6d4));
        addAndMakeVisible(b);
    };

    // Header Controls
    setupRotary(inputGainSlider, inputGainLabel, "IN GAIN", " dB");
    inputGainAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.apvts, "inputGain", inputGainSlider);

    addAndMakeVisible(inputMeter);

    setupToggle(masterBypassButton, "A/B MATCHED BYPASS");
    masterBypassAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        audioProcessor.apvts, "masterBypass", masterBypassButton);

    // =========================================================================
    // Column 1: D-NOISE & TRANSIENT SNAP
    // =========================================================================
    setupRotary(denoiseAmountSlider, denoiseAmountLabel, "D-NOISE", "%");
    denoiseAmountAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.apvts, "denoiseAmount", denoiseAmountSlider);

    setupRotary(denoiseThreshSlider, denoiseThreshLabel, "THRESHOLD", " dB");
    denoiseThreshAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.apvts, "denoiseThreshold", denoiseThreshSlider);

    setupToggle(denoiseShieldToggle, "HARMONIC SHIELD");
    denoiseShieldAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        audioProcessor.apvts, "denoiseHarmonicShield", denoiseShieldToggle);

    setupToggle(denoiseListenToggle, "LISTEN NOISE");
    denoiseListenAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        audioProcessor.apvts, "denoiseListen", denoiseListenToggle);

    setupRotary(stickSnapSlider, stickSnapLabel, "STICK SNAP", "%");
    stickSnapAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.apvts, "stickSnap", stickSnapSlider);

    setupRotary(deBoxSlider, deBoxLabel, "DE-BOX MUD", "%");
    deBoxAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.apvts, "deBoxAmount", deBoxSlider);

    setupRotary(sympatheticBleedSlider, sympatheticBleedLabel, "KIT BLEED", "%");
    sympatheticBleedAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.apvts, "sympatheticBleed", sympatheticBleedSlider);

    // =========================================================================
    // Column 2: DRUM SHELL & PNEUMATIC VENT
    // =========================================================================
    juce::StringArray profileNames;
    for (int i = 0; i < CyberDrum::DrumKitProfiles::kTotalProfiles; ++i) {
        profileNames.add(CyberDrum::DrumKitProfiles::kAllProfiles[i].name);
    }
    addAndMakeVisible(drumProfileBox);
    drumProfileBox.addItemList(profileNames, 1);
    drumProfileAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        audioProcessor.apvts, "drumProfile", drumProfileBox);

    setupRotary(morphAmountSlider, morphAmountLabel, "SHELL MORPH", "%");
    morphAmountAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.apvts, "morphAmount", morphAmountSlider);

    addAndMakeVisible(visualizer);

    setupRotary(customTuningSlider, customTuningLabel, "TUNING", " Hz");
    customTuningAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.apvts, "customTuning", customTuningSlider);

    setupRotary(shellDepthSlider, shellDepthLabel, "SHELL DEPTH", "x");
    shellDepthAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.apvts, "shellDepth", shellDepthSlider);

    setupRotary(ventChokeSlider, ventChokeLabel, "VENT CHOKE", "%");
    ventChokeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.apvts, "ventChoke", ventChokeSlider);

    setupRotary(snareWireBuzzSlider, snareWireBuzzLabel, "SNARE WIRES", "%");
    snareWireBuzzAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.apvts, "snareWireBuzz", snareWireBuzzSlider);

    // =========================================================================
    // Column 3: STUDIO 4-BAND DRUM EQ
    // =========================================================================
    setupRotary(eqLowGainSlider, eqLowGainLabel, "SUB (80Hz)", " dB");
    eqLowGainAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.apvts, "eqLowGain", eqLowGainSlider);

    setupRotary(eqLowMidFreqSlider, eqLowMidFreqLabel, "BOX FREQ", " Hz");
    eqLowMidFreqAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.apvts, "eqLowMidFreq", eqLowMidFreqSlider);

    setupRotary(eqLowMidGainSlider, eqLowMidGainLabel, "BOX CUT", " dB");
    eqLowMidGainAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.apvts, "eqLowMidGain", eqLowMidGainSlider);

    setupRotary(eqHighMidFreqSlider, eqHighMidFreqLabel, "CRACK FREQ", " Hz");
    eqHighMidFreqAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.apvts, "eqHighMidFreq", eqHighMidFreqSlider);

    setupRotary(eqHighMidGainSlider, eqHighMidGainLabel, "CRACK GAIN", " dB");
    eqHighMidGainAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.apvts, "eqHighMidGain", eqHighMidGainSlider);

    setupRotary(eqHighGainSlider, eqHighGainLabel, "AIR (10.5kHz)", " dB");
    eqHighGainAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.apvts, "eqHighGain", eqHighGainSlider);

    // =========================================================================
    // Column 4: DRUM ROOM REVERB & MASTER
    // =========================================================================
    juce::StringArray reverbTypes = { "Studio Drum Booth", "Arena Live Hall", "Vintage Snare Plate" };
    addAndMakeVisible(reverbTypeBox);
    reverbTypeBox.addItemList(reverbTypes, 1);
    reverbTypeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        audioProcessor.apvts, "reverbType", reverbTypeBox);

    setupRotary(reverbMixSlider, reverbMixLabel, "ROOM MIX", "%");
    reverbMixAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.apvts, "reverbMix", reverbMixSlider);

    setupRotary(reverbDecaySlider, reverbDecayLabel, "DECAY", "%");
    reverbDecayAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.apvts, "reverbDecay", reverbDecaySlider);

    setupRotary(reverbSizeSlider, reverbSizeLabel, "ROOM SIZE", "x");
    reverbSizeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.apvts, "reverbSize", reverbSizeSlider);

    setupRotary(reverbToneSlider, reverbToneLabel, "HIGH CUT", " Hz");
    reverbToneAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.apvts, "reverbTone", reverbToneSlider);

    setupRotary(stereoWidthSlider, stereoWidthLabel, "WIDTH", "%");
    stereoWidthAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.apvts, "stereoWidth", stereoWidthSlider);

    setupRotary(outputGainSlider, outputGainLabel, "OUT GAIN", " dB");
    outputGainAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.apvts, "outputGain", outputGainSlider);

    setSize(1080, 680);
    setResizable(true, true);
    setResizeLimits(920, 580, 1920, 1200);

    startTimerHz(30);
}

CyberDrumEditor::~CyberDrumEditor()
{
    stopTimer();
    setLookAndFeel(nullptr);
}

void CyberDrumEditor::timerCallback()
{
    int profileIndex = drumProfileBox.getSelectedItemIndex();
    float shellDepth = (float)shellDepthSlider.getValue();
    float tuningHz = (float)customTuningSlider.getValue();
    float ventChoke = (float)ventChokeSlider.getValue() / 100.0f;
    float wireBuzz = (float)snareWireBuzzSlider.getValue() / 100.0f;

    float chamberPressure = audioProcessor.getChamberPressure();
    float headDisp = audioProcessor.getHeadDisplacement();
    float audioLevel = audioProcessor.getCurrentAudioLevel();

    visualizer.setParameters(profileIndex, shellDepth, tuningHz, ventChoke,
                             wireBuzz, chamberPressure, headDisp, audioLevel);

    inputMeter.setLevels(audioProcessor.getInputRmsDb(), audioProcessor.getInputPeakDb());

    // Update bypass button with live matched dB gain compensation
    float matchGain = audioProcessor.getMatchedBypassGain();
    float matchDb = 20.0f * std::log10(std::max(1e-4f, matchGain));
    juce::String gainStr = (matchDb >= 0.0f ? "+" : "") + juce::String(matchDb, 1) + " dB";
    if (masterBypassButton.getToggleState()) {
        masterBypassButton.setButtonText("BYPASS ACTIVE (" + gainStr + ")");
    } else {
        masterBypassButton.setButtonText("A/B MATCHED (" + gainStr + ")");
    }
}

void CyberDrumEditor::loadPreset(int id)
{
    auto setVal = [this](const juce::String& paramId, float value) {
        if (auto* param = audioProcessor.apvts.getParameter(paramId))
            param->setValueNotifyingHost(param->convertTo0to1(value));
    };

    switch (id) {
        case 1: // Bonham 26" Giant Maple Kick
            setVal("drumProfile", 1);
            setVal("morphAmount", 90.0f); setVal("deBoxAmount", 80.0f);
            setVal("customTuning", 48.0f); setVal("shellDepth", 0.95f);
            setVal("ventChoke", 75.0f); setVal("snareWireBuzz", 0.0f); setVal("stickSnap", 60.0f);
            setVal("eqLowGain", 2.5f); setVal("eqLowMidFreq", 300.0f); setVal("eqLowMidGain", -3.5f);
            setVal("eqHighMidFreq", 3200.0f); setVal("eqHighMidGain", 1.5f); setVal("eqHighGain", 0.5f);
            setVal("reverbType", 1); // Arena Live Hall
            setVal("reverbMix", 24.0f); setVal("reverbDecay", 45.0f); setVal("reverbSize", 1.4f); setVal("reverbTone", 6000.0f);
            break;

        case 2: // 70s Dead Tea-Towel Kick
            setVal("drumProfile", 0);
            setVal("morphAmount", 85.0f); setVal("deBoxAmount", 85.0f);
            setVal("customTuning", 58.0f); setVal("shellDepth", 0.50f);
            setVal("ventChoke", 85.0f); setVal("snareWireBuzz", 0.0f); setVal("stickSnap", 50.0f);
            setVal("eqLowGain", 1.5f); setVal("eqLowMidFreq", 380.0f); setVal("eqLowMidGain", -2.0f);
            setVal("eqHighMidFreq", 2800.0f); setVal("eqHighMidGain", 0.5f); setVal("eqHighGain", 0.0f);
            setVal("reverbType", 0); // Studio Drum Booth
            setVal("reverbMix", 10.0f); setVal("reverbDecay", 20.0f); setVal("reverbSize", 0.8f); setVal("reverbTone", 5000.0f);
            break;

        case 3: // Modern Punchy Birch 22" Kick
            setVal("drumProfile", 2);
            setVal("morphAmount", 88.0f); setVal("deBoxAmount", 80.0f);
            setVal("customTuning", 52.0f); setVal("shellDepth", 0.80f);
            setVal("ventChoke", 90.0f); setVal("snareWireBuzz", 0.0f); setVal("stickSnap", 80.0f);
            setVal("eqLowGain", 2.0f); setVal("eqLowMidFreq", 350.0f); setVal("eqLowMidGain", -4.0f);
            setVal("eqHighMidFreq", 3800.0f); setVal("eqHighMidGain", 3.0f); setVal("eqHighGain", 1.5f);
            setVal("reverbType", 0);
            setVal("reverbMix", 14.0f); setVal("reverbDecay", 25.0f); setVal("reverbSize", 1.0f); setVal("reverbTone", 7500.0f);
            break;

        case 4: // Sub-Piston 808 Cannon Kick
            setVal("drumProfile", 3);
            setVal("morphAmount", 95.0f); setVal("deBoxAmount", 75.0f);
            setVal("customTuning", 42.0f); setVal("shellDepth", 0.90f);
            setVal("ventChoke", 95.0f); setVal("snareWireBuzz", 0.0f); setVal("stickSnap", 70.0f);
            setVal("eqLowGain", 3.5f); setVal("eqLowMidFreq", 320.0f); setVal("eqLowMidGain", -3.0f);
            setVal("eqHighMidFreq", 3500.0f); setVal("eqHighMidGain", 1.0f); setVal("eqHighGain", 0.0f);
            setVal("reverbType", 0);
            setVal("reverbMix", 12.0f); setVal("reverbDecay", 22.0f); setVal("reverbSize", 1.1f); setVal("reverbTone", 6000.0f);
            break;

        case 5: // Bell Brass Black Beauty Snare
            setVal("drumProfile", 5);
            setVal("morphAmount", 88.0f); setVal("deBoxAmount", 75.0f);
            setVal("customTuning", 185.0f); setVal("shellDepth", 0.60f);
            setVal("ventChoke", 72.0f); setVal("snareWireBuzz", 50.0f); setVal("stickSnap", 80.0f);
            setVal("eqLowGain", 1.0f); setVal("eqLowMidFreq", 450.0f); setVal("eqLowMidGain", -2.5f);
            setVal("eqHighMidFreq", 3400.0f); setVal("eqHighMidGain", 2.5f); setVal("eqHighGain", 2.0f);
            setVal("reverbType", 2); // Vintage Snare Plate
            setVal("reverbMix", 26.0f); setVal("reverbDecay", 40.0f); setVal("reverbSize", 1.2f); setVal("reverbTone", 9500.0f);
            break;

        case 6: // 70s Fat Wallet Studio Snare
            setVal("drumProfile", 4);
            setVal("morphAmount", 85.0f); setVal("deBoxAmount", 85.0f);
            setVal("customTuning", 145.0f); setVal("shellDepth", 0.70f);
            setVal("ventChoke", 88.0f); setVal("snareWireBuzz", 55.0f); setVal("stickSnap", 50.0f);
            setVal("eqLowGain", 2.0f); setVal("eqLowMidFreq", 500.0f); setVal("eqLowMidGain", -3.0f);
            setVal("eqHighMidFreq", 2800.0f); setVal("eqHighMidGain", 1.0f); setVal("eqHighGain", 0.5f);
            setVal("reverbType", 0);
            setVal("reverbMix", 15.0f); setVal("reverbDecay", 22.0f); setVal("reverbSize", 0.9f); setVal("reverbTone", 6000.0f);
            break;

        case 7: // Tight Funk 13" Piccolo Snare
            setVal("drumProfile", 6);
            setVal("morphAmount", 80.0f); setVal("deBoxAmount", 70.0f);
            setVal("customTuning", 225.0f); setVal("shellDepth", 0.25f);
            setVal("ventChoke", 65.0f); setVal("snareWireBuzz", 40.0f); setVal("stickSnap", 85.0f);
            setVal("eqLowGain", 0.0f); setVal("eqLowMidFreq", 480.0f); setVal("eqLowMidGain", -2.0f);
            setVal("eqHighMidFreq", 4000.0f); setVal("eqHighMidGain", 3.0f); setVal("eqHighGain", 2.5f);
            setVal("reverbType", 2);
            setVal("reverbMix", 20.0f); setVal("reverbDecay", 30.0f); setVal("reverbSize", 1.0f); setVal("reverbTone", 10000.0f);
            break;

        case 8: // Studio Maple 5.5x14 Snare
            setVal("drumProfile", 7);
            setVal("morphAmount", 85.0f); setVal("deBoxAmount", 75.0f);
            setVal("customTuning", 195.0f); setVal("shellDepth", 0.50f);
            setVal("ventChoke", 75.0f); setVal("snareWireBuzz", 48.0f); setVal("stickSnap", 70.0f);
            setVal("eqLowGain", 1.0f); setVal("eqLowMidFreq", 420.0f); setVal("eqLowMidGain", -2.5f);
            setVal("eqHighMidFreq", 3200.0f); setVal("eqHighMidGain", 2.0f); setVal("eqHighGain", 1.5f);
            setVal("reverbType", 0);
            setVal("reverbMix", 20.0f); setVal("reverbDecay", 32.0f); setVal("reverbSize", 1.1f); setVal("reverbTone", 7500.0f);
            break;

        case 9: // 12" & 16" Singing Studio Toms
            setVal("drumProfile", 8);
            setVal("morphAmount", 85.0f); setVal("deBoxAmount", 78.0f);
            setVal("customTuning", 130.0f); setVal("shellDepth", 0.55f);
            setVal("ventChoke", 70.0f); setVal("snareWireBuzz", 0.0f); setVal("stickSnap", 65.0f);
            setVal("eqLowGain", 1.5f); setVal("eqLowMidFreq", 380.0f); setVal("eqLowMidGain", -3.0f);
            setVal("eqHighMidFreq", 3400.0f); setVal("eqHighMidGain", 1.5f); setVal("eqHighGain", 1.0f);
            setVal("reverbType", 0);
            setVal("reverbMix", 22.0f); setVal("reverbDecay", 38.0f); setVal("reverbSize", 1.2f); setVal("reverbTone", 7000.0f);
            break;

        case 10: // 18" Bonham Low Thunder Tom
            setVal("drumProfile", 10);
            setVal("morphAmount", 90.0f); setVal("deBoxAmount", 80.0f);
            setVal("customTuning", 68.0f); setVal("shellDepth", 0.95f);
            setVal("ventChoke", 85.0f); setVal("snareWireBuzz", 0.0f); setVal("stickSnap", 60.0f);
            setVal("eqLowGain", 2.5f); setVal("eqLowMidFreq", 280.0f); setVal("eqLowMidGain", -3.5f);
            setVal("eqHighMidFreq", 3000.0f); setVal("eqHighMidGain", 1.5f); setVal("eqHighGain", 0.5f);
            setVal("reverbType", 1);
            setVal("reverbMix", 25.0f); setVal("reverbDecay", 48.0f); setVal("reverbSize", 1.4f); setVal("reverbTone", 6000.0f);
            break;

        case 11: // Flamenco Deep Acoustic Cajon (Warm Peruvian beech box, low bass slap, guitar-string sizzle)
            setVal("drumProfile", 11);
            setVal("morphAmount", 88.0f); setVal("deBoxAmount", 82.0f);
            setVal("customTuning", 65.0f); setVal("shellDepth", 0.85f);
            setVal("ventChoke", 75.0f); setVal("snareWireBuzz", 40.0f); setVal("stickSnap", 70.0f);
            setVal("eqLowGain", 2.5f); setVal("eqLowMidFreq", 320.0f); setVal("eqLowMidGain", -3.0f);
            setVal("eqHighMidFreq", 3600.0f); setVal("eqHighMidGain", 2.0f); setVal("eqHighGain", 2.0f);
            setVal("reverbType", 0);
            setVal("reverbMix", 18.0f); setVal("reverbDecay", 30.0f); setVal("reverbSize", 1.1f); setVal("reverbTone", 7000.0f);
            break;

        case 12: // Acoustic Gig Live Cajon (Punchy duo cajon with 68Hz soundhole air boom)
            setVal("drumProfile", 12);
            setVal("morphAmount", 85.0f); setVal("deBoxAmount", 80.0f);
            setVal("customTuning", 72.0f); setVal("shellDepth", 0.75f);
            setVal("ventChoke", 70.0f); setVal("snareWireBuzz", 45.0f); setVal("stickSnap", 75.0f);
            setVal("eqLowGain", 2.0f); setVal("eqLowMidFreq", 350.0f); setVal("eqLowMidGain", -2.5f);
            setVal("eqHighMidFreq", 3800.0f); setVal("eqHighMidGain", 2.5f); setVal("eqHighGain", 2.0f);
            setVal("reverbType", 0);
            setVal("reverbMix", 16.0f); setVal("reverbDecay", 28.0f); setVal("reverbSize", 1.0f); setVal("reverbTone", 7500.0f);
            break;

        case 13: // Afro-Cuban Conga / Quinto
            setVal("drumProfile", 13);
            setVal("morphAmount", 82.0f); setVal("deBoxAmount", 75.0f);
            setVal("customTuning", 190.0f); setVal("shellDepth", 0.70f);
            setVal("ventChoke", 65.0f); setVal("snareWireBuzz", 0.0f); setVal("stickSnap", 75.0f);
            setVal("eqLowGain", 1.5f); setVal("eqLowMidFreq", 420.0f); setVal("eqLowMidGain", -2.0f);
            setVal("eqHighMidFreq", 3200.0f); setVal("eqHighMidGain", 1.5f); setVal("eqHighGain", 1.0f);
            setVal("reverbType", 0);
            setVal("reverbMix", 20.0f); setVal("reverbDecay", 32.0f); setVal("reverbSize", 1.1f); setVal("reverbTone", 7000.0f);
            break;

        case 14: // African Djembe Deep Bass
            setVal("drumProfile", 14);
            setVal("morphAmount", 88.0f); setVal("deBoxAmount", 80.0f);
            setVal("customTuning", 55.0f); setVal("shellDepth", 0.90f);
            setVal("ventChoke", 80.0f); setVal("snareWireBuzz", 0.0f); setVal("stickSnap", 80.0f);
            setVal("eqLowGain", 3.0f); setVal("eqLowMidFreq", 280.0f); setVal("eqLowMidGain", -3.5f);
            setVal("eqHighMidFreq", 3500.0f); setVal("eqHighMidGain", 2.0f); setVal("eqHighGain", 1.0f);
            setVal("reverbType", 1);
            setVal("reverbMix", 22.0f); setVal("reverbDecay", 40.0f); setVal("reverbSize", 1.3f); setVal("reverbTone", 6500.0f);
            break;

        case 15: // Stereo Drum Bus / Room Glue
            setVal("drumProfile", 1);
            setVal("morphAmount", 65.0f); setVal("deBoxAmount", 60.0f);
            setVal("customTuning", 55.0f); setVal("shellDepth", 0.70f);
            setVal("ventChoke", 70.0f); setVal("snareWireBuzz", 25.0f); setVal("stickSnap", 60.0f);
            setVal("sympatheticBleed", 20.0f);
            setVal("eqLowGain", 1.5f); setVal("eqLowMidFreq", 380.0f); setVal("eqLowMidGain", -2.0f);
            setVal("eqHighMidFreq", 3200.0f); setVal("eqHighMidGain", 1.5f); setVal("eqHighGain", 1.5f);
            setVal("stereoWidth", 125.0f);
            setVal("reverbType", 0);
            setVal("reverbMix", 24.0f); setVal("reverbDecay", 35.0f); setVal("reverbSize", 1.25f); setVal("reverbTone", 7500.0f);
            break;
    }
}

void CyberDrumEditor::paint(juce::Graphics& g)
{
    // Background gradient: dark aged mahogany & brushed titanium trim
    juce::ColourGradient bg(juce::Colour(0xff181615), 0, 0,
                            juce::Colour(0xff100e0d), 0, (float)getHeight(), false);
    g.setGradientFill(bg);
    g.fillAll();

    // Top Header Banner
    auto headerArea = getLocalBounds().removeFromTop(58);
    g.setColour(juce::Colour(0xff1f1c1a));
    g.fillRect(headerArea);
    g.setColour(juce::Colour(0xff38322e));
    g.drawHorizontalLine(57, 0, (float)getWidth());

    // Title & Subtitle
    g.setColour(juce::Colour(0xffedd9be));
    g.setFont(juce::Font(juce::FontOptions(17.0f)).withExtraKerningFactor(0.06f));
    g.drawText("CYBER DRUM PREAMP", 18, 10, 240, 20, juce::Justification::left, false);

    g.setColour(juce::Colour(0xff06b6d4));
    g.setFont(juce::FontOptions(10.0f));
    g.drawText("Fluid Space Drum Shell & Percussion Physics", 20, 30, 260, 16, juce::Justification::left, false);

    // Section Dividing Lines (4 Columns)
    auto mainArea = getLocalBounds().reduced(14).withTrimmedTop(56);
    float colW = mainArea.getWidth() / 4.0f;

    g.setColour(juce::Colour(0xff2d2825));
    for (int c = 1; c < 4; ++c) {
        g.drawVerticalLine((int)(mainArea.getX() + colW * (float)c), (float)mainArea.getY() + 10, (float)mainArea.getBottom() - 10);
    }

    // Column Titles
    g.setFont(juce::FontOptions(11.0f));
    g.setColour(juce::Colour(0xffd4af37)); // Gold accents
    g.drawText("D-NOISE & SNAP", (int)mainArea.getX() + 6, (int)mainArea.getY() + 4, (int)colW - 12, 18, juce::Justification::centred, false);
    g.drawText("SHELL & PNEUMATIC VENT", (int)(mainArea.getX() + colW) + 6, (int)mainArea.getY() + 4, (int)colW - 12, 18, juce::Justification::centred, false);
    g.drawText("4-BAND SCULPTING EQ", (int)(mainArea.getX() + colW * 2) + 6, (int)mainArea.getY() + 4, (int)colW - 12, 18, juce::Justification::centred, false);
    g.drawText("DRUM ROOM & MASTER", (int)(mainArea.getX() + colW * 3) + 6, (int)mainArea.getY() + 4, (int)colW - 12, 18, juce::Justification::centred, false);
}

void CyberDrumEditor::resized()
{
    auto bounds = getLocalBounds();

    // Top Header Row
    auto header = bounds.removeFromTop(58);
    int rightX = header.getWidth() - 14;

    int presetW = std::clamp((int)(header.getWidth() * 0.20f), 170, 240);
    presetSelector.setBounds(rightX - presetW, 14, presetW, 28);
    rightX -= (presetW + 8);

    int inputW = std::clamp((int)(header.getWidth() * 0.14f), 130, 170);
    inputSelector.setBounds(rightX - inputW, 14, inputW, 28);
    rightX -= (inputW + 8);

    int bypassW = std::clamp((int)(header.getWidth() * 0.14f), 130, 160);
    masterBypassButton.setBounds(rightX - bypassW, 14, bypassW, 28);
    rightX -= (bypassW + 8);

    int meterW = std::clamp((int)(header.getWidth() * 0.14f), 130, 170);
    inputMeter.setBounds(rightX - meterW, 14, meterW, 28);
    rightX -= (meterW + 8);

    int inGainW = 55;
    inputGainLabel.setBounds(rightX - inGainW, 6, inGainW, 14);
    inputGainSlider.setBounds(rightX - inGainW, 20, inGainW, 36);

    auto mainArea = bounds.reduced(14).withTrimmedTop(4);
    int colW = mainArea.getWidth() / 4;

    // =========================================================================
    // Column 1: D-NOISE & TRANSIENT SNAP
    // =========================================================================
    auto col1 = mainArea.removeFromLeft(colW).reduced(6);
    col1.removeFromTop(26);

    auto dnRow1 = col1.removeFromTop(95);
    int dnHalfW = dnRow1.getWidth() / 2;
    auto dnR1L = dnRow1.removeFromLeft(dnHalfW);
    denoiseAmountLabel.setBounds(dnR1L.removeFromBottom(16));
    denoiseAmountSlider.setBounds(dnR1L);

    auto dnR1R = dnRow1;
    denoiseThreshLabel.setBounds(dnR1R.removeFromBottom(16));
    denoiseThreshSlider.setBounds(dnR1R);

    auto dnToggleRow = col1.removeFromTop(32);
    int tglHalfW = dnToggleRow.getWidth() / 2;
    denoiseShieldToggle.setBounds(dnToggleRow.removeFromLeft(tglHalfW).reduced(2));
    denoiseListenToggle.setBounds(dnToggleRow.reduced(2));

    col1.removeFromTop(8);

    int transKnobH = (col1.getHeight() - 10) / 3;

    auto ssArea = col1.removeFromTop(transKnobH);
    stickSnapLabel.setBounds(ssArea.removeFromBottom(16));
    stickSnapSlider.setBounds(ssArea.reduced(4));

    auto dbArea = col1.removeFromTop(transKnobH);
    deBoxLabel.setBounds(dbArea.removeFromBottom(16));
    deBoxSlider.setBounds(dbArea.reduced(4));

    auto sbArea = col1;
    sympatheticBleedLabel.setBounds(sbArea.removeFromBottom(16));
    sympatheticBleedSlider.setBounds(sbArea.reduced(4));

    // =========================================================================
    // Column 2: DRUM SHELL & PNEUMATIC VENT
    // =========================================================================
    auto col2 = mainArea.removeFromLeft(colW).reduced(6);
    col2.removeFromTop(26);

    // Profile Dropdown
    auto profRow = col2.removeFromTop(26);
    drumProfileBox.setBounds(profRow);
    col2.removeFromTop(6);

    // Morph Slider
    auto morphRow = col2.removeFromTop(56);
    morphAmountLabel.setBounds(morphRow.removeFromBottom(16));
    morphAmountSlider.setBounds(morphRow);
    col2.removeFromTop(6);

    // Visualizer (Dynamic 3D Drum Shell / Cajon Box)
    int visH = (int)(col2.getHeight() * 0.45f);
    visualizer.setBounds(col2.removeFromTop(visH));
    col2.removeFromTop(8);

    // Physical Shell Controls (2x2 Grid: Tuning, Depth, Vent Choke, Snare Wires)
    int subKnobH = col2.getHeight() / 2;
    auto rowShell1 = col2.removeFromTop(subKnobH);
    auto rowShell2 = col2;

    int subHalfW = rowShell1.getWidth() / 2;

    auto tnArea = rowShell1.removeFromLeft(subHalfW);
    customTuningLabel.setBounds(tnArea.removeFromBottom(16));
    customTuningSlider.setBounds(tnArea);

    auto dpArea = rowShell1;
    shellDepthLabel.setBounds(dpArea.removeFromBottom(16));
    shellDepthSlider.setBounds(dpArea);

    auto vcArea = rowShell2.removeFromLeft(subHalfW);
    ventChokeLabel.setBounds(vcArea.removeFromBottom(16));
    ventChokeSlider.setBounds(vcArea);

    auto swArea = rowShell2;
    snareWireBuzzLabel.setBounds(swArea.removeFromBottom(16));
    snareWireBuzzSlider.setBounds(swArea);

    // =========================================================================
    // Column 3: STUDIO 4-BAND DRUM EQ
    // =========================================================================
    auto col3 = mainArea.removeFromLeft(colW).reduced(6);
    col3.removeFromTop(26);

    // Band 1: Sub Thump Low Shelf
    auto eqRow1 = col3.removeFromTop(98);
    eqLowGainLabel.setBounds(eqRow1.removeFromBottom(16));
    eqLowGainSlider.setBounds(eqRow1.reduced(4));
    col3.removeFromTop(6);

    // Band 2: Boxiness Freq & Cut (2 Knobs)
    auto eqRow2 = col3.removeFromTop(105);
    int eqHalfW = eqRow2.getWidth() / 2;
    auto eqR2L = eqRow2.removeFromLeft(eqHalfW);
    eqLowMidFreqLabel.setBounds(eqR2L.removeFromBottom(16));
    eqLowMidFreqSlider.setBounds(eqR2L);

    auto eqR2R = eqRow2;
    eqLowMidGainLabel.setBounds(eqR2R.removeFromBottom(16));
    eqLowMidGainSlider.setBounds(eqR2R);
    col3.removeFromTop(6);

    // Band 3: Crack Freq & Gain (2 Knobs)
    auto eqRow3 = col3.removeFromTop(105);
    auto eqR3L = eqRow3.removeFromLeft(eqHalfW);
    eqHighMidFreqLabel.setBounds(eqR3L.removeFromBottom(16));
    eqHighMidFreqSlider.setBounds(eqR3L);

    auto eqR3R = eqRow3;
    eqHighMidGainLabel.setBounds(eqR3R.removeFromBottom(16));
    eqHighMidGainSlider.setBounds(eqR3R);
    col3.removeFromTop(6);

    // Band 4: Air Sizzle High Shelf
    auto eqRow4 = col3;
    eqHighGainLabel.setBounds(eqRow4.removeFromBottom(16));
    eqHighGainSlider.setBounds(eqRow4.reduced(4));

    // =========================================================================
    // Column 4: DRUM ROOM REVERB & MASTER
    // =========================================================================
    auto col4 = mainArea.reduced(6);
    col4.removeFromTop(26);

    auto revTypeRow = col4.removeFromTop(26);
    reverbTypeBox.setBounds(revTypeRow);
    col4.removeFromTop(6);

    auto revArea = col4.removeFromTop(160);
    int revRowH = revArea.getHeight() / 2;
    auto revRow1 = revArea.removeFromTop(revRowH);
    auto revRow2 = revArea;

    int revHalfW = revRow1.getWidth() / 2;
    auto r1L = revRow1.removeFromLeft(revHalfW);
    reverbMixLabel.setBounds(r1L.removeFromBottom(16));
    reverbMixSlider.setBounds(r1L);

    auto r1R = revRow1;
    reverbDecayLabel.setBounds(r1R.removeFromBottom(16));
    reverbDecaySlider.setBounds(r1R);

    auto r2L = revRow2.removeFromLeft(revHalfW);
    reverbSizeLabel.setBounds(r2L.removeFromBottom(16));
    reverbSizeSlider.setBounds(r2L);

    auto r2R = revRow2;
    reverbToneLabel.setBounds(r2R.removeFromBottom(16));
    reverbToneSlider.setBounds(r2R);

    col4.removeFromTop(18);

    auto masterRow = col4;
    int mKnobW = masterRow.getWidth() / 2;

    auto stArea = masterRow.removeFromLeft(mKnobW);
    stereoWidthLabel.setBounds(stArea.removeFromBottom(16));
    stereoWidthSlider.setBounds(stArea.reduced(4));

    auto outArea = masterRow;
    outputGainLabel.setBounds(outArea.removeFromBottom(16));
    outputGainSlider.setBounds(outArea.reduced(4));
}
