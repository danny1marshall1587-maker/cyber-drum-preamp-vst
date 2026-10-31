#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <algorithm>
#include <cmath>

namespace CyberDrum {

class DrumShellVisualizer : public juce::Component, private juce::Timer {
public:
    DrumShellVisualizer() {
        startTimerHz(30); // 30 FPS smooth physics animation
    }

    ~DrumShellVisualizer() override {
        stopTimer();
    }

    void setParameters(int profileIndex, float shellDepth, float tuningHz, float ventChoke,
                       float wireBuzz, float chamberPressure, float headDisp, float audioLevel) {
        currentProfile = profileIndex;
        currentDepth = std::clamp(shellDepth, 0.2f, 1.2f);
        currentTuning = tuningHz;
        currentVentChoke = ventChoke;
        currentWireBuzz = wireBuzz;
        currentPressure = chamberPressure;
        currentHeadDisp = headDisp;
        currentAudioLevel = std::max(currentAudioLevel * 0.82f, audioLevel); // Decay envelope
        repaint();
    }

    void paint(juce::Graphics& g) override {
        auto bounds = getLocalBounds().toFloat().reduced(6.0f);
        auto center = bounds.getCentre();

        // 1. Dark chassis frame
        g.setColour(juce::Colour(0xff121110));
        g.fillRoundedRectangle(bounds, 8.0f);
        g.setColour(juce::Colour(0xff2d2926));
        g.drawRoundedRectangle(bounds, 8.0f, 1.5f);

        auto innerArea = bounds.reduced(6.0f);
        auto headerArea = innerArea.removeFromTop(20.0f);
        auto footerArea = innerArea.removeFromBottom(18.0f);

        // 2. Determine Shell Dimensions based on profile and depth
        bool isCajon = (currentProfile == 11 || currentProfile == 12);
        bool isSnare = (currentProfile >= 4 && currentProfile <= 7);
        bool isKick  = (currentProfile >= 0 && currentProfile <= 3);

        float maxW = innerArea.getWidth() * 0.72f;
        float maxH = innerArea.getHeight() * 0.85f;

        float diameterNorm = isKick ? 0.95f : (isSnare ? 0.78f : (isCajon ? 0.65f : 0.82f));
        float shellW = maxW * diameterNorm;
        float shellH = maxH * (0.35f + currentDepth * 0.55f);

        float shellX = center.x - shellW * 0.5f;
        float shellY = center.y - shellH * 0.5f;

        // Visualizer Vibration displacement
        float vibOffset = currentHeadDisp * 4.0f * currentAudioLevel;

        if (isCajon) {
            // ================================================================
            // Render Peruvian / Flamenco Cajon Box Geometry
            // ================================================================
            auto cajonRect = juce::Rectangle<float>(shellX, shellY, shellW, shellH);

            // Wooden Box Body (Beech / Birch / Walnut)
            juce::Colour woodDark(0xff2e1d13);
            juce::Colour woodLight(0xff543623);
            juce::ColourGradient woodGrad(woodLight, cajonRect.getX(), cajonRect.getY(),
                                         woodDark, cajonRect.getRight(), cajonRect.getBottom(), false);
            g.setGradientFill(woodGrad);
            g.fillRoundedRectangle(cajonRect, 4.0f);

            // Front Tapa Playing Plate (Thin resonant ply)
            auto tapaRect = cajonRect.reduced(4.0f);
            juce::Colour tapaCol(0xffb07d56);
            g.setColour(tapaCol.withMultipliedBrightness(1.0f + currentAudioLevel * 0.35f));
            g.fillRoundedRectangle(tapaRect, 2.0f);

            // Tapa screw markers
            g.setColour(juce::Colour(0xff3a271c));
            for (int s = 0; s < 5; ++s) {
                float sx = tapaRect.getX() + (tapaRect.getWidth() / 4.0f) * s;
                g.fillEllipse(sx - 1.5f, tapaRect.getY() + 3.0f, 3.0f, 3.0f);
            }

            // Internal Snare Guitar Strings behind Tapa
            if (currentWireBuzz > 0.05f) {
                g.setColour(juce::Colour(0xffd4af37).withAlpha(0.75f));
                float stringX1 = tapaRect.getX() + tapaRect.getWidth() * 0.35f;
                float stringX2 = tapaRect.getX() + tapaRect.getWidth() * 0.65f;
                g.drawVerticalLine((int)stringX1, tapaRect.getY() + 10.0f, tapaRect.getBottom() - 10.0f);
                g.drawVerticalLine((int)stringX2, tapaRect.getY() + 10.0f, tapaRect.getBottom() - 10.0f);
            }

            // Rear Soundhole Vent (Pneumatic Bass Reflex Port)
            float portR = std::min(shellW, shellH) * 0.22f;
            float portX = cajonRect.getCentreX();
            float portY = cajonRect.getBottom() - portR * 1.5f;
            g.setColour(juce::Colour(0xff0d0a08));
            g.fillEllipse(portX - portR * 0.5f, portY - portR * 0.5f, portR, portR);
            g.setColour(juce::Colour(0xff3f2d21));
            g.drawEllipse(portX - portR * 0.5f, portY - portR * 0.5f, portR, portR, 1.5f);

            // Pneumatic Air Jet from Soundhole on hard hits
            if (currentPressure < 0.85f) {
                float airGlow = (1.0f - currentPressure) * currentVentChoke;
                g.setColour(juce::Colour(0xff38bdf8).withAlpha(airGlow * 0.7f));
                g.drawEllipse(portX - portR * 0.65f, portY - portR * 0.65f, portR * 1.3f, portR * 1.3f, 2.0f);
            }

            g.setColour(juce::Colour(0xffedd9be));
            g.drawRoundedRectangle(cajonRect, 4.0f, 1.2f);

        } else {
            // ================================================================
            // Render Cylindrical Drum Shell (Kick, Snare, Tom)
            // ================================================================
            float rimRadius = shellW * 0.5f;
            float rimHeight = shellW * 0.24f; // Elliptical 3D perspective

            // Colors based on shell material
            juce::Colour shellCol1, shellCol2, rimCol;
            if (currentProfile == 5) { // Bell Brass Black Beauty
                shellCol1 = juce::Colour(0xff222224);
                shellCol2 = juce::Colour(0xff111112);
                rimCol = juce::Colour(0xffeab308); // Gold / Brass hardware
            } else if (isSnare && currentProfile == 6) { // Birch Piccolo
                shellCol1 = juce::Colour(0xff60281e);
                shellCol2 = juce::Colour(0xff290f0a);
                rimCol = juce::Colour(0xffcbd5e1); // Chrome
            } else if (currentProfile == 1) { // 26" Bonham Natural Maple
                shellCol1 = juce::Colour(0xffc59a65);
                shellCol2 = juce::Colour(0xff6e4a23);
                rimCol = juce::Colour(0xffcbd5e1);
            } else { // Standard Warm Maple / Birch Studio
                shellCol1 = juce::Colour(0xff8a4522);
                shellCol2 = juce::Colour(0xff3d1b09);
                rimCol = juce::Colour(0xffcbd5e1);
            }

            // Shell Cylinder Body
            juce::ColourGradient shellGrad(shellCol1, shellX, shellY,
                                          shellCol2, shellX + shellW, shellY + shellH, false);
            g.setGradientFill(shellGrad);
            g.fillRect(shellX, shellY + rimHeight * 0.5f, shellW, shellH);

            // Shell outline
            g.setColour(juce::Colour(0xff181412));
            g.drawRect(shellX, shellY + rimHeight * 0.5f, shellW, shellH, 1.2f);

            // Bottom Resonant Head Rim & Membrane
            auto botRimRect = juce::Rectangle<float>(shellX, shellY + shellH, shellW, rimHeight);
            g.setColour(juce::Colour(0xff1a1816));
            g.fillEllipse(botRimRect);
            g.setColour(rimCol.darker(0.3f));
            g.drawEllipse(botRimRect, 2.0f);

            // Snare Wires on Bottom Head (if snare profile or wire buzz enabled)
            if (currentWireBuzz > 0.02f) {
                g.setColour(juce::Colour(0xffcbd5e1).withAlpha(0.6f + currentAudioLevel * 0.4f));
                for (int w = -3; w <= 3; ++w) {
                    float wy = botRimRect.getCentreY() + w * 2.2f;
                    g.drawLine(botRimRect.getX() + 10.0f, wy, botRimRect.getRight() - 10.0f, wy, 1.0f);
                }
            }

            // Hardware Tension Lugs on Shell Sides
            g.setColour(rimCol);
            int numLugs = 5;
            for (int l = 1; l < numLugs; ++l) {
                float lx = shellX + (shellW / (float)numLugs) * l;
                g.fillRect(lx - 2.0f, shellY + rimHeight * 0.5f + 4.0f, 4.0f, shellH - 8.0f);
                g.fillEllipse(lx - 3.5f, shellY + rimHeight * 0.5f + 1.0f, 7.0f, 5.0f);
                g.fillEllipse(lx - 3.5f, shellY + shellH - 6.0f, 7.0f, 5.0f);
            }

            // Pneumatic Pressure Relief Vent Hole on Shell Body
            float ventX = shellX + shellW * 0.5f;
            float ventY = shellY + shellH * 0.55f;
            g.setColour(juce::Colour(0xff0d0c0b));
            g.fillEllipse(ventX - 5.0f, ventY - 5.0f, 10.0f, 10.0f);
            g.setColour(rimCol.darker(0.2f));
            g.drawEllipse(ventX - 5.0f, ventY - 5.0f, 10.0f, 10.0f, 1.5f);

            // Pneumatic Vent Choke Glow on hard hits (Cyan/Blue air compression)
            if (currentPressure < 0.88f) {
                float ventGlow = (1.0f - currentPressure) * currentVentChoke;
                g.setColour(juce::Colour(0xff06b6d4).withAlpha(std::min(1.0f, ventGlow * 1.5f)));
                g.drawEllipse(ventX - 7.5f, ventY - 7.5f, 15.0f, 15.0f, 2.0f);
            }

            // Top Batter Head Membrane (with Mylar translucency & vibration displacement)
            auto topRimRect = juce::Rectangle<float>(shellX, shellY + vibOffset, shellW, rimHeight);
            juce::Colour headCol(0xffe2d9cd); // Coated Ambassador White
            g.setColour(headCol.withMultipliedBrightness(1.0f + currentAudioLevel * 0.25f));
            g.fillEllipse(topRimRect);

            // 70s Tea-Towel / Wallet Damping Patch (Profiles 0 and 4)
            if (currentProfile == 0 || currentProfile == 4) {
                auto patchRect = juce::Rectangle<float>(topRimRect.getCentreX() - 18.0f,
                                                        topRimRect.getY() + 4.0f,
                                                        36.0f, 14.0f);
                g.setColour(juce::Colour(0xff1e293b));
                g.fillRoundedRectangle(patchRect, 2.0f);
                g.setColour(juce::Colour(0xff475569));
                g.drawRoundedRectangle(patchRect, 2.0f, 1.0f);
            }

            // Top Counterhoop / Chrome Rim
            g.setColour(rimCol);
            g.drawEllipse(topRimRect, 2.5f);
        }

        // 3. Header Text: Profile Name & Fundamental Frequency
        g.setColour(juce::Colour(0xffedd9be));
        g.setFont(juce::FontOptions(12.5f));
        g.drawText(getProfileName(currentProfile), headerArea.removeFromLeft(headerArea.getWidth() - 80),
                   juce::Justification::centredLeft, false);

        g.setColour(juce::Colour(0xff38bdf8));
        g.setFont(juce::FontOptions(11.0f));
        g.drawText(juce::String((int)currentTuning) + " Hz", headerArea,
                   juce::Justification::centredRight, false);

        // 4. Footer Text: Live Pneumatic State & Pitch-Glide Readout
        g.setFont(juce::FontOptions(10.0f));
        if (currentPressure < 0.95f) {
            int pitchCents = (int)((1.0f - currentPressure) * currentVentChoke * 42.0f);
            g.setColour(juce::Colour(0xfff59e0b)); // Amber active choke
            g.drawText("PNEUMATIC CHOKE: " + juce::String((int)((1.0f - currentPressure) * 100.0f)) +
                       "% \u2022 GLIDE: +" + juce::String(pitchCents) + " CENTS \u2022 ADIABATIC AIR SPRING ACTIVE",
                       footerArea, juce::Justification::centred, false);
        } else {
            g.setColour(juce::Colour(0xffa19284));
            g.drawText("PNEUMATIC SHELL ENGINE \u2022 0.0ms TRIGGER-FREE PHYSICAL RE-SHELLING",
                       footerArea, juce::Justification::centred, false);
        }
    }

private:
    int currentProfile = 0;
    float currentDepth = 0.55f;
    float currentTuning = 58.0f;
    float currentVentChoke = 0.70f;
    float currentWireBuzz = 0.0f;
    float currentPressure = 1.0f;
    float currentHeadDisp = 0.0f;
    float currentAudioLevel = 0.0f;

    void timerCallback() override {
        if (currentAudioLevel > 0.005f) repaint();
    }

    juce::String getProfileName(int idx) const {
        if (idx >= 0 && idx < DrumKitProfiles::kTotalProfiles) {
            return DrumKitProfiles::kAllProfiles[idx].name;
        }
        return "Custom Drum Shell";
    }
};

} // namespace CyberDrum
