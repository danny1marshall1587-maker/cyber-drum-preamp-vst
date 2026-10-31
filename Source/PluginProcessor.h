#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "CyberDrumEngine.hpp"

class CyberDrumProcessor : public juce::AudioProcessor {
public:
    CyberDrumProcessor();
    ~CyberDrumProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;

    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    const juce::String getName() const override;

    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram(int index) override;
    const juce::String getProgramName(int index) override;
    void changeProgramName(int index, const juce::String& newName) override;

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;
    float getCurrentAudioLevel() const { return currentAudioLevel.load(); }
    float getInputRmsDb() const { return currentInputRms.load(); }
    float getInputPeakDb() const { return currentInputPeak.load(); }
    float getMatchedBypassGain() const { return currentMatchedGain.load(); }
    float getChamberPressure() const { return currentChamberPressure.load(); }
    float getHeadDisplacement() const { return currentHeadDisp.load(); }
    float getWireActivity() const { return currentWireActivity.load(); }

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

private:
    CyberDrum::CyberDrumEngine dspEngineLeft;
    CyberDrum::CyberDrumEngine dspEngineRight;

    std::atomic<float> currentAudioLevel { 0.0f };
    std::atomic<float> currentInputRms { -100.0f };
    std::atomic<float> currentInputPeak { -100.0f };
    std::atomic<float> currentMatchedGain { 1.0f };
    std::atomic<float> currentChamberPressure { 1.0f };
    std::atomic<float> currentHeadDisp { 0.0f };
    std::atomic<float> currentWireActivity { 0.0f };

    float dryRmsFiltered = 0.0f;
    float wetRmsFiltered = 0.0f;
    float targetMatchedGain = 1.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CyberDrumProcessor)
};
