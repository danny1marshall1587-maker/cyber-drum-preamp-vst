#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <algorithm>
#include <cmath>
#include "CyberDrumEngine.hpp"

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
        currentProfile = std::clamp(profileIndex, 0, DrumKitProfiles::kTotalProfiles - 1);
        currentDepth = std::clamp(shellDepth, 0.2f, 1.5f);
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

        // 2. Identify instrument category
        bool isCymbal     = (currentProfile >= 20 && currentProfile <= 24);
        bool isCajon      = (currentProfile == 25 || currentProfile == 26);
        bool isPercussion = (currentProfile >= 27 && currentProfile <= 29);
        bool isSnare      = (currentProfile >= 8  && currentProfile <= 15);
        bool isTom        = (currentProfile >= 16 && currentProfile <= 19);
        bool isKick       = (currentProfile >= 0  && currentProfile <= 7);

        float maxW = innerArea.getWidth() * 0.74f;
        float maxH = innerArea.getHeight() * 0.86f;

        // Visualizer Vibration displacement
        float vibOffset = currentHeadDisp * 4.5f * currentAudioLevel;

        if (isCymbal) {
            // ================================================================
            // 3D Lathed Bronze Cymbal & Plate Dispersion Rendering
            // ================================================================
            float diameterNorm = (currentProfile == 20 || currentProfile == 23) ? 0.95f   // 22", 21" Rides
                               : (currentProfile == 22) ? 0.88f                          // 18" Crash
                               : (currentProfile == 24) ? 0.82f                          // 16" China
                               : 0.75f;                                                  // 14" Hats
            float cymbalW = maxW * diameterNorm;
            float cymbalH = cymbalW * 0.38f; // 3D oblique viewing perspective
            float cymbalX = center.x - cymbalW * 0.5f;
            float cymbalY = center.y - cymbalH * 0.5f;

            auto cymbalRect = juce::Rectangle<float>(cymbalX, cymbalY, cymbalW, cymbalH);

            // Audio-reactive flexural ripple waves along outer rim
            if (currentAudioLevel > 0.04f) {
                float rippleAlpha = std::min(0.65f, currentAudioLevel * 0.85f);
                float rippleExpand = vibOffset * 2.8f;
                g.setColour(juce::Colour(0xfff59e0b).withAlpha(rippleAlpha));
                g.drawEllipse(cymbalRect.expanded(rippleExpand * 1.5f), 1.5f);
                g.setColour(juce::Colour(0xff38bdf8).withAlpha(rippleAlpha * 0.5f));
                g.drawEllipse(cymbalRect.expanded(rippleExpand * 2.5f), 1.0f);
            }

            // Cymbal Alloy Palette
            juce::Colour bronzeDark, bronzeMid, bronzeBright;
            if (currentProfile == 20) { // Dark Turkish B20 Ride (Aged, hand-hammered)
                bronzeDark   = juce::Colour(0xff573a18);
                bronzeMid    = juce::Colour(0xff8c622b);
                bronzeBright = juce::Colour(0xffd4af37);
            } else if (currentProfile == 21) { // 60s A-Style Hats
                bronzeDark   = juce::Colour(0xff6e4a1f);
                bronzeMid    = juce::Colour(0xffa17538);
                bronzeBright = juce::Colour(0xffeab308);
            } else if (currentProfile == 22) { // Fast Studio Crash (Brilliant finish)
                bronzeDark   = juce::Colour(0xff784e1b);
                bronzeMid    = juce::Colour(0xffb8862d);
                bronzeBright = juce::Colour(0xfffcd34d);
            } else if (currentProfile == 23) { // Heavy Rock Bell Ride
                bronzeDark   = juce::Colour(0xff4a3215);
                bronzeMid    = juce::Colour(0xff996515);
                bronzeBright = juce::Colour(0xfff59e0b);
            } else { // 24: Thin White-Bronze China/Trash
                bronzeDark   = juce::Colour(0xff4b4737);
                bronzeMid    = juce::Colour(0xff878465);
                bronzeBright = juce::Colour(0xffd1d5db);
            }

            // Bow Body Radial Fill
            juce::ColourGradient cymbalGrad(bronzeBright, center.x, center.y,
                                           bronzeDark, cymbalRect.getRight(), cymbalRect.getBottom(), true);
            g.setGradientFill(cymbalGrad);
            g.fillEllipse(cymbalRect);

            // Lathed tonal grooves (Concentric elliptical micro-rings)
            int numLatheRings = 7;
            for (int r = 1; r < numLatheRings; ++r) {
                float frac = (float)r / (float)numLatheRings;
                auto ringRect = cymbalRect.reduced(cymbalW * 0.5f * (1.0f - frac),
                                                   cymbalH * 0.5f * (1.0f - frac));
                float ringAlpha = (r % 2 == 0) ? 0.35f : 0.18f;
                g.setColour(bronzeDark.withAlpha(ringAlpha));
                g.drawEllipse(ringRect, 1.0f);
            }

            // China Cymbal inverted flared lip
            if (currentProfile == 24) {
                auto chinaLipRect = cymbalRect.reduced(8.0f, 3.0f);
                g.setColour(juce::Colour(0xff18181b).withAlpha(0.6f));
                g.drawEllipse(chinaLipRect, 2.0f);
            }

            // Center Bell / Cup (Raised dome)
            float bellW = cymbalW * 0.24f;
            float bellH = cymbalH * 0.32f;
            auto bellRect = juce::Rectangle<float>(center.x - bellW * 0.5f,
                                                   center.y - bellH * 0.5f,
                                                   bellW, bellH);
            juce::ColourGradient bellGrad(bronzeBright.brighter(0.2f), center.x, center.y - 2.0f,
                                         bronzeDark, bellRect.getX(), bellRect.getBottom(), true);
            g.setGradientFill(bellGrad);
            g.fillEllipse(bellRect);
            g.setColour(bronzeDark.darker(0.3f));
            g.drawEllipse(bellRect, 1.5f);

            // Center mounting hole & felt washer
            float feltR = 7.0f;
            g.setColour(juce::Colour(0xff111111)); // Black mounting felt
            g.fillEllipse(center.x - feltR * 0.5f, center.y - feltR * 0.3f, feltR, feltR * 0.6f);
            g.setColour(juce::Colour(0xffcbd5e1)); // Chrome cymbal post nut
            g.fillEllipse(center.x - 2.0f, center.y - 1.5f, 4.0f, 3.0f);

            // Outer rim bronze edge outline
            g.setColour(bronzeBright.brighter(0.15f));
            g.drawEllipse(cymbalRect, 1.5f);

        } else if (isCajon) {
            // ================================================================
            // Render Peruvian / Flamenco Cajon Box Geometry
            // ================================================================
            float diameterNorm = 0.68f;
            float shellW = maxW * diameterNorm;
            float shellH = maxH * (0.50f + currentDepth * 0.45f);
            float shellX = center.x - shellW * 0.5f;
            float shellY = center.y - shellH * 0.5f;

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
            if (currentPressure < 0.88f) {
                float airGlow = (1.0f - currentPressure) * currentVentChoke;
                g.setColour(juce::Colour(0xff38bdf8).withAlpha(airGlow * 0.75f));
                g.drawEllipse(portX - portR * 0.65f, portY - portR * 0.65f, portR * 1.3f, portR * 1.3f, 2.0f);
            }

            g.setColour(juce::Colour(0xffedd9be));
            g.drawRoundedRectangle(cajonRect, 4.0f, 1.2f);

        } else if (isPercussion) {
            // ================================================================
            // Render Conga / Djembe / Timbale Percussion Shapes
            // ================================================================
            float shellW = maxW * 0.62f;
            float shellH = maxH * 0.82f;
            float shellX = center.x - shellW * 0.5f;
            float shellY = center.y - shellH * 0.5f;
            float rimHeight = shellW * 0.22f;

            if (currentProfile == 27) { // Conga / Quinto (Tapered wood barrel)
                juce::Path congaPath;
                congaPath.startNewSubPath(shellX + 6.0f, shellY + rimHeight * 0.5f);
                congaPath.quadraticTo(shellX - 8.0f, shellY + shellH * 0.45f, shellX + 16.0f, shellY + shellH);
                congaPath.lineTo(shellX + shellW - 16.0f, shellY + shellH);
                congaPath.quadraticTo(shellX + shellW + 8.0f, shellY + shellH * 0.45f, shellX + shellW - 6.0f, shellY + rimHeight * 0.5f);
                congaPath.closeSubPath();

                juce::ColourGradient congaGrad(juce::Colour(0xff9a3412), shellX, shellY,
                                              juce::Colour(0xff431407), shellX + shellW, shellY + shellH, false);
                g.setGradientFill(congaGrad);
                g.fillPath(congaPath);
                g.setColour(juce::Colour(0xff290f0a));
                g.drawPath(congaPath, 1.5f);

                // Rawhide Head & Chrome Crown Hoop
                auto crownRect = juce::Rectangle<float>(shellX + 4.0f, shellY + vibOffset, shellW - 8.0f, rimHeight);
                g.setColour(juce::Colour(0xffe2d9cd)); // Natural buffalo hide
                g.fillEllipse(crownRect);
                g.setColour(juce::Colour(0xff94a3b8)); // Heavy curved crown hoop
                g.drawEllipse(crownRect, 3.0f);

            } else if (currentProfile == 28) { // African Djembe (Goblet shape)
                juce::Path djembePath;
                djembePath.startNewSubPath(shellX, shellY + rimHeight * 0.5f);
                djembePath.lineTo(shellX + 10.0f, shellY + shellH * 0.52f);
                djembePath.lineTo(shellX + shellW * 0.25f, shellY + shellH * 0.60f);
                djembePath.lineTo(shellX + 18.0f, shellY + shellH);
                djembePath.lineTo(shellX + shellW - 18.0f, shellY + shellH);
                djembePath.lineTo(shellX + shellW * 0.75f, shellY + shellH * 0.60f);
                djembePath.lineTo(shellX + shellW - 10.0f, shellY + shellH * 0.52f);
                djembePath.lineTo(shellX + shellW, shellY + rimHeight * 0.5f);
                djembePath.closeSubPath();

                juce::ColourGradient djembeGrad(juce::Colour(0xff3f2d21), shellX, shellY,
                                               juce::Colour(0xff1f150e), shellX + shellW, shellY + shellH, false);
                g.setGradientFill(djembeGrad);
                g.fillPath(djembePath);
                g.setColour(juce::Colour(0xff120c08));
                g.drawPath(djembePath, 1.5f);

                // Traditional rope tension lacing
                g.setColour(juce::Colour(0xffd97706).withAlpha(0.65f));
                for (int r = 0; r < 6; ++r) {
                    float rx1 = shellX + 8.0f + (shellW - 16.0f) * (r / 5.0f);
                    float rx2 = shellX + shellW * 0.32f + (shellW * 0.36f) * (r / 5.0f);
                    g.drawLine(rx1, shellY + rimHeight * 0.5f + 4.0f, rx2, shellY + shellH * 0.58f, 1.0f);
                }

                auto djembeHead = juce::Rectangle<float>(shellX, shellY + vibOffset, shellW, rimHeight);
                g.setColour(juce::Colour(0xfffef3c7)); // Goatskin head
                g.fillEllipse(djembeHead);
                g.setColour(juce::Colour(0xff78350f));
                g.drawEllipse(djembeHead, 2.0f);

            } else { // 29: Latin Timbale & Cowbell Rim
                auto timbaleRect = juce::Rectangle<float>(shellX, shellY + shellH * 0.3f, shellW, shellH * 0.65f);
                juce::ColourGradient brassGrad(juce::Colour(0xffe2e8f0), timbaleRect.getX(), timbaleRect.getY(),
                                              juce::Colour(0xff64748b), timbaleRect.getRight(), timbaleRect.getBottom(), false);
                g.setGradientFill(brassGrad);
                g.fillRect(timbaleRect);
                g.setColour(juce::Colour(0xff334155));
                g.drawRect(timbaleRect, 1.5f);

                // Thin plastic clear batter head
                auto timbHead = juce::Rectangle<float>(shellX, shellY + shellH * 0.3f - rimHeight * 0.5f + vibOffset, shellW, rimHeight);
                g.setColour(juce::Colour(0x55cbd5e1));
                g.fillEllipse(timbHead);
                g.setColour(juce::Colour(0xfff8fafc));
                g.drawEllipse(timbHead, 2.0f);

                // Mounted black cowbell on rim
                auto cowbell = juce::Rectangle<float>(shellX - 16.0f, shellY + shellH * 0.2f, 26.0f, 38.0f);
                g.setColour(juce::Colour(0xff1e1e24));
                g.fillRoundedRectangle(cowbell, 2.0f);
                g.setColour(juce::Colour(0xff475569));
                g.drawRoundedRectangle(cowbell, 2.0f, 1.0f);
            }

        } else {
            // ================================================================
            // Render Cylindrical Drum Shell (Kick, Snare, Tom)
            // ================================================================
            float diameterNorm = isKick  ? 0.96f
                               : isSnare ? (currentProfile == 10 ? 0.72f : (currentProfile == 15 ? 0.68f : 0.80f))
                               : (currentProfile == 16 ? 0.65f : (currentProfile == 17 ? 0.72f : (currentProfile == 18 ? 0.86f : 0.94f)));
            float shellW = maxW * diameterNorm;
            float shellH = maxH * (0.32f + currentDepth * 0.58f);

            float shellX = center.x - shellW * 0.5f;
            float shellY = center.y - shellH * 0.5f;

            float rimHeight = shellW * 0.23f; // Elliptical 3D perspective

            // Colors based on shell material
            juce::Colour shellCol1, shellCol2, rimCol;
            bool isWoodHoop = (currentProfile == 14); // Nashville Solid Maple Wood-Hoop
            bool isSupraphonic = (currentProfile == 12); // Ludwig Supraphonic 400 Aluminum

            if (currentProfile == 8) { // Bell Brass Black Beauty
                shellCol1 = juce::Colour(0xff232326);
                shellCol2 = juce::Colour(0xff111113);
                rimCol    = juce::Colour(0xffeab308); // 24K Gold / Brass hardware
            } else if (isSupraphonic) { // Ludwig 400 Seamless Aluminum
                shellCol1 = juce::Colour(0xffcbd5e1);
                shellCol2 = juce::Colour(0xff94a3b8);
                rimCol    = juce::Colour(0xfff8fafc); // Chrome Imperial lugs
            } else if (currentProfile == 13) { // 80s Power Gated Snare
                shellCol1 = juce::Colour(0xff1e1b4b);
                shellCol2 = juce::Colour(0xff0f172a);
                rimCol    = juce::Colour(0xffec4899); // 80s Neon Magenta accents
            } else if (isWoodHoop) { // Nashville Solid Maple
                shellCol1 = juce::Colour(0xffd97706);
                shellCol2 = juce::Colour(0xff78350f);
                rimCol    = juce::Colour(0xffb45309); // Solid Maple Wood Hoops
            } else if (currentProfile == 0) { // 26" Bonham Natural Maple
                shellCol1 = juce::Colour(0xffc59a65);
                shellCol2 = juce::Colour(0xff6e4a23);
                rimCol    = juce::Colour(0xff18181b); // Black wood kick hoops
            } else if (currentProfile == 2 || currentProfile == 6) { // Punchy Birch
                shellCol1 = juce::Colour(0xff1e3a8a);
                shellCol2 = juce::Colour(0xff0f172a);
                rimCol    = juce::Colour(0xffcbd5e1);
            } else if (currentProfile == 4) { // Metal Click
                shellCol1 = juce::Colour(0xff18181b);
                shellCol2 = juce::Colour(0xff09090b);
                rimCol    = juce::Colour(0xff94a3b8);
            } else { // Standard Warm Maple / Studio Birch
                shellCol1 = juce::Colour(0xff8a4522);
                shellCol2 = juce::Colour(0xff3d1b09);
                rimCol    = juce::Colour(0xffcbd5e1);
            }

            // Shell Cylinder Body
            juce::ColourGradient shellGrad(shellCol1, shellX, shellY,
                                          shellCol2, shellX + shellW, shellY + shellH, false);
            g.setGradientFill(shellGrad);
            g.fillRect(shellX, shellY + rimHeight * 0.5f, shellW, shellH);

            // Supraphonic Acoustic Center Bead Line
            if (isSupraphonic) {
                float beadY = shellY + rimHeight * 0.5f + shellH * 0.5f;
                g.setColour(juce::Colour(0xfff1f5f9));
                g.drawLine(shellX, beadY - 1.0f, shellX + shellW, beadY - 1.0f, 1.5f);
                g.setColour(juce::Colour(0xff64748b));
                g.drawLine(shellX, beadY + 1.0f, shellX + shellW, beadY + 1.0f, 1.5f);
            }

            // Shell outline
            g.setColour(juce::Colour(0xff181412));
            g.drawRect(shellX, shellY + rimHeight * 0.5f, shellW, shellH, 1.2f);

            // Bottom Resonant Head Rim & Membrane
            auto botRimRect = juce::Rectangle<float>(shellX, shellY + shellH, shellW, rimHeight);
            g.setColour(juce::Colour(0xff1a1816));
            g.fillEllipse(botRimRect);
            g.setColour(rimCol.darker(0.3f));
            g.drawEllipse(botRimRect, isWoodHoop ? 3.5f : 2.0f);

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
            int numLugs = isKick ? 6 : 5;
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

            // 70s Tea-Towel / Wallet Damping Patch (Profiles 1 and 9)
            if (currentProfile == 1 || currentProfile == 9) {
                auto patchRect = juce::Rectangle<float>(topRimRect.getCentreX() - 20.0f,
                                                        topRimRect.getY() + 4.0f,
                                                        40.0f, 14.0f);
                g.setColour(juce::Colour(0xff1e293b));
                g.fillRoundedRectangle(patchRect, 2.0f);
                g.setColour(juce::Colour(0xff475569));
                g.drawRoundedRectangle(patchRect, 2.0f, 1.0f);
            }

            // Metal Click Strike Pad (Profile 4)
            if (currentProfile == 4) {
                auto padRect = juce::Rectangle<float>(topRimRect.getCentreX() - 8.0f,
                                                      topRimRect.getCentreY() - 4.0f,
                                                      16.0f, 8.0f);
                g.setColour(juce::Colour(0xff475569));
                g.fillEllipse(padRect);
            }

            // Top Counterhoop / Rim
            g.setColour(rimCol);
            g.drawEllipse(topRimRect, isWoodHoop ? 3.5f : 2.5f);
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
        if (isCymbal) {
            g.setColour(juce::Colour(0xfff59e0b));
            g.drawText("BRONZE FLEXURAL DISPERSION \u2022 MODAL BELL & BOW \u2022 AIR SHIMMER WASH",
                       footerArea, juce::Justification::centred, false);
        } else if (currentPressure < 0.95f) {
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
