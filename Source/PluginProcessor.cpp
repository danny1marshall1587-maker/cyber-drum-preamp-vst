#include "PluginProcessor.h"
#include "PluginEditor.h"

CyberDrumProcessor::CyberDrumProcessor()
    : AudioProcessor(BusesProperties()
                     .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                     .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "PARAMETERS", createParameterLayout())
{
}

CyberDrumProcessor::~CyberDrumProcessor()
{
}

juce::AudioProcessorValueTreeState::ParameterLayout CyberDrumProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    // Input Staging & Routing
    juce::StringArray inputModes = {
        "Mono (In 1 / Left)",
        "Mono (In 2 / Right)",
        "Auto-Detect Active",
        "Stereo Sum (-3dB)",
        "Stereo Discrete"
    };
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID("inputRouting", 1), "Input Routing", inputModes, 0));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("inputGain", 1), "Input Gain", -24.0f, 24.0f, 0.0f));

    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID("masterBypass", 1), "Level-Matched Bypass", false));

    // Stage 0: 10-Band Cyber-Denoiser PRO
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("denoiseAmount", 1), "D-Noise Reduction", 0.0f, 100.0f, 40.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("denoiseThreshold", 1), "D-Noise Threshold", -100.0f, -40.0f, -80.0f));
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID("denoiseHarmonicShield", 1), "Harmonic Shield", true));
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID("denoiseListen", 1), "Listen Noise", false));

    // Stage 1 & 2: Drum Kit Morpher & Fluid Shell
    juce::StringArray profileNames;
    for (int i = 0; i < CyberDrum::DrumKitProfiles::kTotalProfiles; ++i) {
        profileNames.add(CyberDrum::DrumKitProfiles::kAllProfiles[i].name);
    }
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID("drumProfile", 1), "Drum Shell Profile", profileNames, 1)); // Default Bonham Maple Kick

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("morphAmount", 1), "Shell Morph Amount", 0.0f, 100.0f, 85.0f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("deBoxAmount", 1), "De-Box Mud Cut", 0.0f, 100.0f, 75.0f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("customTuning", 1), "Shell Fundamental Tuning", 25.0f, 350.0f, 48.0f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("shellDepth", 1), "Shell Depth Scalar", 0.20f, 1.50f, 0.95f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("ventChoke", 1), "Pneumatic Vent Choke", 0.0f, 100.0f, 70.0f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("snareWireBuzz", 1), "Snare Wire Sizzle", 0.0f, 100.0f, 0.0f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("stickSnap", 1), "Stick / Beater Snap", 0.0f, 100.0f, 65.0f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("sympatheticBleed", 1), "Sympathetic Kit Bleed", 0.0f, 50.0f, 15.0f));

    // Stage 3: Studio 4-Band Drum Parametric EQ
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("eqLowGain", 1), "Sub / Thump (80 Hz)", -12.0f, 12.0f, 1.5f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("eqLowMidFreq", 1), "Boxiness Cut Freq", 180.0f, 900.0f, 350.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("eqLowMidGain", 1), "Boxiness Cut Gain", -12.0f, 12.0f, -2.5f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("eqHighMidFreq", 1), "Crack / Attack Freq", 1500.0f, 6500.0f, 3400.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("eqHighMidGain", 1), "Crack / Attack Gain", -12.0f, 12.0f, 1.5f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("eqHighGain", 1), "Air / Sizzle (10.5 kHz)", -12.0f, 12.0f, 1.0f));

    // Stage 4: World-Class Drum Reverb Space
    juce::StringArray reverbTypes = { "Studio Drum Booth", "Arena Live Hall", "Vintage Snare Plate" };
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID("reverbType", 1), "Reverb Type", reverbTypes, 0));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("reverbMix", 1), "Reverb Mix", 0.0f, 100.0f, 18.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("reverbDecay", 1), "Reverb Decay", 0.0f, 100.0f, 35.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("reverbSize", 1), "Reverb Size", 0.5f, 2.5f, 1.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("reverbTone", 1), "Reverb High Cut", 1500.0f, 16000.0f, 7500.0f));

    // Stage 5: Output & Width
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("stereoWidth", 1), "Stereo Width", 0.0f, 200.0f, 100.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("outputGain", 1), "Output Gain", -24.0f, 12.0f, 0.0f));

    return { params.begin(), params.end() };
}

const juce::String CyberDrumProcessor::getName() const
{
    return "CyberDrumPreamp";
}

bool CyberDrumProcessor::acceptsMidi() const { return false; }
bool CyberDrumProcessor::producesMidi() const { return false; }
bool CyberDrumProcessor::isMidiEffect() const { return false; }
double CyberDrumProcessor::getTailLengthSeconds() const { return 0.0; }

int CyberDrumProcessor::getNumPrograms() { return 1; }
int CyberDrumProcessor::getCurrentProgram() { return 0; }
void CyberDrumProcessor::setCurrentProgram(int) {}
const juce::String CyberDrumProcessor::getProgramName(int) { return {}; }
void CyberDrumProcessor::changeProgramName(int, const juce::String&) {}

void CyberDrumProcessor::prepareToPlay(double sampleRate, int /*samplesPerBlock*/)
{
    dspEngineLeft.init((float)sampleRate);
    dspEngineRight.init((float)sampleRate);
    dryRmsFiltered = 0.0f;
    wetRmsFiltered = 0.0f;
    targetMatchedGain = 1.0f;
}

void CyberDrumProcessor::releaseResources()
{
    dspEngineLeft.reset();
    dspEngineRight.reset();
}

bool CyberDrumProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::mono()
     && layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    if (layouts.getMainInputChannelSet() != juce::AudioChannelSet::mono()
     && layouts.getMainInputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    return true;
}

void CyberDrumProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    auto totalNumInputChannels  = getTotalNumInputChannels();
    auto totalNumOutputChannels = getTotalNumOutputChannels();

    for (auto i = totalNumInputChannels; i < totalNumOutputChannels; ++i)
        buffer.clear(i, 0, buffer.getNumSamples());

    // Fetch normalized parameters
    int inputMode = (int)apvts.getRawParameterValue("inputRouting")->load();
    float inGainDb = apvts.getRawParameterValue("inputGain")->load();
    float inGainLin = std::pow(10.0f, inGainDb / 20.0f);
    bool  masterBypass = apvts.getRawParameterValue("masterBypass")->load() > 0.5f;

    // Denoiser
    float denoiseAmt = apvts.getRawParameterValue("denoiseAmount")->load() / 100.0f;
    float denoiseThresh = apvts.getRawParameterValue("denoiseThreshold")->load();
    bool  denoiseHarmonicShield = apvts.getRawParameterValue("denoiseHarmonicShield")->load() > 0.5f;
    bool  denoiseListen = apvts.getRawParameterValue("denoiseListen")->load() > 0.5f;

    // Drum Shell Morpher
    int   profileIndex = (int)apvts.getRawParameterValue("drumProfile")->load();
    float morphAmount = apvts.getRawParameterValue("morphAmount")->load() / 100.0f;
    float deBoxAmount = apvts.getRawParameterValue("deBoxAmount")->load() / 100.0f;
    float customTuningHz = apvts.getRawParameterValue("customTuning")->load();
    float customDepth = apvts.getRawParameterValue("shellDepth")->load();
    float ventChoke = apvts.getRawParameterValue("ventChoke")->load() / 100.0f;
    float snareWireBuzz = apvts.getRawParameterValue("snareWireBuzz")->load() / 100.0f;
    float stickSnap = apvts.getRawParameterValue("stickSnap")->load() / 100.0f;
    float sympatheticBleed = apvts.getRawParameterValue("sympatheticBleed")->load() / 100.0f;

    // 4-Band EQ
    float eqLow = apvts.getRawParameterValue("eqLowGain")->load();
    float eqLowMidF = apvts.getRawParameterValue("eqLowMidFreq")->load();
    float eqLowMidG = apvts.getRawParameterValue("eqLowMidGain")->load();
    float eqHighMidF = apvts.getRawParameterValue("eqHighMidFreq")->load();
    float eqHighMidG = apvts.getRawParameterValue("eqHighMidGain")->load();
    float eqHigh = apvts.getRawParameterValue("eqHighGain")->load();

    // Reverb
    int   revType = (int)apvts.getRawParameterValue("reverbType")->load();
    float revMix = apvts.getRawParameterValue("reverbMix")->load() / 100.0f;
    float revDecay = apvts.getRawParameterValue("reverbDecay")->load() / 100.0f;
    float revSize = apvts.getRawParameterValue("reverbSize")->load();
    float revTone = apvts.getRawParameterValue("reverbTone")->load();

    // Output & Width
    float width = apvts.getRawParameterValue("stereoWidth")->load() / 100.0f;
    float gainDb = apvts.getRawParameterValue("outputGain")->load();
    float gainLin = std::pow(10.0f, gainDb / 20.0f);

    auto numSamples = buffer.getNumSamples();
    float maxAbs = 0.0f;
    float inPeak = 0.0f;
    float inSumSq = 0.0f;
    float wetSumSq = 0.0f;

    const float* inL = (totalNumInputChannels > 0) ? buffer.getReadPointer(0) : nullptr;
    const float* inR = (totalNumInputChannels > 1) ? buffer.getReadPointer(1) : nullptr;

    float* outL = (totalNumOutputChannels > 0) ? buffer.getWritePointer(0) : nullptr;
    float* outR = (totalNumOutputChannels > 1) ? buffer.getWritePointer(1) : nullptr;

    if (inputMode == 4 && totalNumInputChannels >= 2) {
        // Mode 4: Stereo Discrete Processing (Overheads / E-Drum Stereo / Stereo Cajon pair)
        for (int n = 0; n < numSamples; ++n) {
            float rawL = (inL ? inL[n] : 0.0f) * inGainLin;
            float rawR = (inR ? inR[n] : 0.0f) * inGainLin;

            inPeak = std::max(inPeak, std::max(std::abs(rawL), std::abs(rawR)));
            inSumSq += 0.5f * (rawL * rawL + rawR * rawR);

            float processedL = dspEngineLeft.processMonoDrum(rawL,
                                                            denoiseAmt, denoiseThresh, denoiseHarmonicShield, denoiseListen,
                                                            profileIndex, morphAmount, deBoxAmount,
                                                            customTuningHz, customDepth, ventChoke, snareWireBuzz,
                                                            eqLow, eqLowMidF, eqLowMidG, eqHighMidF, eqHighMidG, eqHigh,
                                                            rawR * sympatheticBleed);

            float processedR = dspEngineRight.processMonoDrum(rawR,
                                                             denoiseAmt, denoiseThresh, denoiseHarmonicShield, denoiseListen,
                                                             profileIndex, morphAmount, deBoxAmount,
                                                             customTuningHz, customDepth, ventChoke, snareWireBuzz,
                                                             eqLow, eqLowMidF, eqLowMidG, eqHighMidF, eqHighMidG, eqHigh,
                                                             rawL * sympatheticBleed);

            // Stereo Width (M/S Adjust)
            float mid = 0.5f * (processedL + processedR);
            float side = 0.5f * (processedL - processedR) * width;
            float dryL = mid + side;
            float dryR = mid - side;

            // True Stereo Drum Reverb Space
            float outWetL = 0.0f, outWetR = 0.0f;
            dspEngineLeft.reverb.processStereo(dryL, dryR, revSize, revTone, revDecay, revMix, revType, outWetL, outWetR);

            outWetL *= gainLin;
            outWetR *= gainLin;
            wetSumSq += 0.5f * (outWetL * outWetL + outWetR * outWetR);

            if (masterBypass) {
                float bypassGain = targetMatchedGain;
                if (outL) outL[n] = rawL * bypassGain;
                if (outR) outR[n] = rawR * bypassGain;
                maxAbs = std::max(maxAbs, std::max(std::abs(rawL * bypassGain), std::abs(rawR * bypassGain)));
            } else {
                if (outL) outL[n] = outWetL;
                if (outR) outR[n] = outWetR;
                maxAbs = std::max(maxAbs, std::max(std::abs(outWetL), std::abs(outWetR)));
            }
        }
    } else {
        // Mono Drum Processing (Mode 0: In 1, Mode 1: In 2, Mode 2: Auto, Mode 3: Sum)
        for (int n = 0; n < numSamples; ++n) {
            float drumMono = 0.0f;

            if (totalNumInputChannels == 1) {
                drumMono = inL ? inL[n] : 0.0f;
            } else if (totalNumInputChannels >= 2) {
                float sL = inL ? inL[n] : 0.0f;
                float sR = inR ? inR[n] : 0.0f;

                switch (inputMode) {
                    case 0: drumMono = sL; break;
                    case 1: drumMono = sR; break;
                    case 2: // Auto-Detect Active Mono
                        if (std::abs(sL) > 1e-4f && std::abs(sR) < 1e-5f) {
                            drumMono = sL;
                        } else if (std::abs(sR) > 1e-4f && std::abs(sL) < 1e-5f) {
                            drumMono = sR;
                        } else {
                            drumMono = (sL + sR) * 0.7071f;
                        }
                        break;
                    case 3: // Stereo Sum (-3dB)
                    default:
                        drumMono = (sL + sR) * 0.7071f;
                        break;
                }
            }

            drumMono *= inGainLin;
            inPeak = std::max(inPeak, std::abs(drumMono));
            inSumSq += drumMono * drumMono;

            float processedMono = dspEngineLeft.processMonoDrum(drumMono,
                                                               denoiseAmt, denoiseThresh, denoiseHarmonicShield, denoiseListen,
                                                               profileIndex, morphAmount, deBoxAmount,
                                                               customTuningHz, customDepth, ventChoke, snareWireBuzz,
                                                               eqLow, eqLowMidF, eqLowMidG, eqHighMidF, eqHighMidG, eqHigh,
                                                               0.0f);

            float outWetL = 0.0f, outWetR = 0.0f;
            dspEngineLeft.reverb.processStereo(processedMono, processedMono, revSize, revTone, revDecay, revMix, revType, outWetL, outWetR);

            // Apply stereo width to the reverb tail
            float mid = 0.5f * (outWetL + outWetR);
            float side = 0.5f * (outWetL - outWetR) * width;
            outWetL = (mid + side) * gainLin;
            outWetR = (mid - side) * gainLin;

            wetSumSq += 0.5f * (outWetL * outWetL + outWetR * outWetR);

            if (masterBypass) {
                float bypassGain = targetMatchedGain;
                float dryBypass = drumMono * bypassGain;
                if (totalNumOutputChannels >= 2) {
                    if (outL) outL[n] = dryBypass;
                    if (outR) outR[n] = dryBypass;
                } else if (totalNumOutputChannels == 1) {
                    if (outL) outL[n] = dryBypass;
                }
                maxAbs = std::max(maxAbs, std::abs(dryBypass));
            } else {
                if (totalNumOutputChannels >= 2) {
                    if (outL) outL[n] = outWetL;
                    if (outR) outR[n] = outWetR;
                } else if (totalNumOutputChannels == 1) {
                    if (outL) outL[n] = 0.5f * (outWetL + outWetR);
                }
                maxAbs = std::max(maxAbs, std::max(std::abs(outWetL), std::abs(outWetR)));
            }
        }
    }

    // Telemetry updates
    float inRms = std::sqrt(inSumSq / static_cast<float>(std::max(1, numSamples)));
    float wetRms = std::sqrt(wetSumSq / static_cast<float>(std::max(1, numSamples)));

    dryRmsFiltered = dryRmsFiltered * 0.94f + inRms * 0.06f;
    wetRmsFiltered = wetRmsFiltered * 0.94f + wetRms * 0.06f;

    if (dryRmsFiltered > 1e-4f && wetRmsFiltered > 1e-4f) {
        float matchRatio = wetRmsFiltered / dryRmsFiltered;
        matchRatio = std::clamp(matchRatio, 0.25f, 4.0f);
        targetMatchedGain = targetMatchedGain * 0.95f + matchRatio * 0.05f;
    }
    currentMatchedGain.store(targetMatchedGain);

    float inRmsDb = (inRms > 1e-5f) ? 20.0f * std::log10(inRms) : -100.0f;
    float inPeakDb = (inPeak > 1e-5f) ? 20.0f * std::log10(inPeak) : -100.0f;

    currentInputRms.store(inRmsDb);
    currentInputPeak.store(inPeakDb);
    currentAudioLevel.store(maxAbs);

    // Live physical state readouts from the virtual drum shell
    currentChamberPressure.store(dspEngineLeft.morpher.getChamberPressure());
    currentHeadDisp.store(dspEngineLeft.morpher.getHeadDisplacement());
    currentWireActivity.store(dspEngineLeft.morpher.getWireActivity());
}

juce::AudioProcessorEditor* CyberDrumProcessor::createEditor()
{
    return new CyberDrumEditor(*this);
}

bool CyberDrumProcessor::hasEditor() const
{
    return true;
}

void CyberDrumProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml(state.createXml());
    copyXmlToBinary(*xml, destData);
}

void CyberDrumProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xmlState(getXmlFromBinary(data, sizeInBytes));
    if (xmlState.get() != nullptr && xmlState->hasTagName(apvts.state.getType())) {
        apvts.replaceState(juce::ValueTree::fromXml(*xmlState));
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new CyberDrumProcessor();
}
