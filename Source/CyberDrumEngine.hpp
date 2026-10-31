#pragma once

#include <cmath>
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>

namespace CyberDrum {

// ============================================================================
// Constant-Time Branchless Math & Fluid Approximants
// ============================================================================
namespace FluidMath {
    // 3-cycle branchless Padé approximant for tanh(x) with soft saturation
    inline float fast_tanh(float x) noexcept {
        if (x <= -3.0f) return -1.0f;
        if (x >=  3.0f) return  1.0f;
        const float x2 = x * x;
        return x * (27.0f + x2) / (27.0f + 9.0f * x2);
    }

    // Polynomial fluid boundary transfer curve
    inline float fluidTransfer(float x, float shape = 0.35f) noexcept {
        const float tx = fast_tanh(x);
        return tx * (1.0f - shape) + (tx * tx * tx) * shape;
    }

    // Branchless fast sine approximation for LFO modulation (-pi..pi)
    inline float fast_sin(float x) noexcept {
        const float B = 1.27323954f; // 4/pi
        const float C = -0.405284735f; // -4/(pi^2)
        float y = B * x + C * x * std::abs(x);
        const float P = 0.225f;
        return P * (y * std::abs(y) - y) + y;
    }
}

// ============================================================================
// Stage 0: 10-Band Drum Cyber-Denoiser PRO (0ms Zero-Latency Subtraction)
// Removes mic preamp hiss, pedal squeaks, stage hum, and bleed noise
// ============================================================================
class DrumCyberDenoiser {
public:
    static constexpr int NUM_BANDS = 10;

    void init(float sampleRate) {
        mSampleRate = sampleRate > 8000.0f ? sampleRate : 48000.0f;
        auto getCoeff = [&](float f) {
            float norm = f / mSampleRate;
            norm = std::clamp(norm, 0.0001f, 0.4999f);
            return 1.0f - std::exp(-2.0f * 3.14159265f * norm);
        };

        mHpF = getCoeff(25.0f);     // 25 Hz sub rumble filter
        mLpF_cut = getCoeff(19000.0f); // 19 kHz ultrasonic ceiling

        mLevelAtt = getCoeff(1000.0f); // 1 ms instantaneous peak attack
        mLevelRel = getCoeff(12.0f);   // 85 ms smooth organic release

        const float multi[9] = { 60.0f, 150.0f, 350.0f, 750.0f, 1500.0f, 3000.0f, 5000.0f, 8500.0f, 13000.0f };
        for (int i = 0; i < 9; ++i) {
            mBCoeffs[i] = std::min(0.99f, getCoeff(multi[i]));
        }

        reset();
    }

    void reset() {
        for (int i = 0; i < 9; ++i) lpStates[i] = 0.0f;
        for (int i = 0; i < NUM_BANDS; ++i) {
            levelStates[i] = 0.0f;
            currentGains[i] = 1.0f;
        }
        hpCutStat = 0.0f;
        lpCutStat = 0.0f;
        lastThreshDb = -999.0f;
    }

    inline float process(float inSample, float reductionNorm, float thresholdDb,
                         bool harmonicShield, bool listenNoise) {
        if (reductionNorm <= 0.001f && !listenNoise) {
            return inSample;
        }

        // Sub and ultrasonic conditioning
        hpCutStat += (inSample - hpCutStat) * mHpF;
        float s0 = inSample - hpCutStat;
        lpCutStat += (s0 - lpCutStat) * mLpF_cut;
        s0 = lpCutStat;

        // 10-Band Perfect Reconstruction Filter Bank
        float b[NUM_BANDS];
        float currentIn = s0;

        lpStates[0] += (currentIn - lpStates[0]) * mBCoeffs[0];
        b[0] = lpStates[0];

        for (int i = 1; i < 9; ++i) {
            lpStates[i] += (currentIn - lpStates[i]) * mBCoeffs[i];
            b[i] = lpStates[i] - lpStates[i - 1];
        }
        b[9] = currentIn - lpStates[8];

        // Cache Linear Thresholds
        if (std::abs(thresholdDb - lastThreshDb) > 0.05f) {
            lastThreshDb = thresholdDb;
            static const float bandOffsets[NUM_BANDS] = {
                -2.0f, 0.0f, +1.0f, +1.5f, +2.0f, +2.5f, +3.0f, +3.5f, +4.0f, +3.0f
            };
            for (int i = 0; i < NUM_BANDS; ++i) {
                float db = std::clamp(thresholdDb + bandOffsets[i], -100.0f, -20.0f);
                cachedLinThresh[i] = std::pow(10.0f, db / 20.0f);
            }
        }

        // Harmonic Shield for Drum Fundamentals (45 Hz to 250 Hz)
        float fundamentalActivity = 0.0f;
        if (harmonicShield) {
            for (int fBand = 0; fBand <= 2; ++fBand) {
                float rf = levelStates[fBand];
                float rawG = 1.0f - (cachedLinThresh[fBand] / (rf + 1e-9f));
                fundamentalActivity = std::max(fundamentalActivity, std::clamp(rawG, 0.0f, 1.0f));
            }
        }

        float outSum = 0.0f;
        for (int i = 0; i < NUM_BANDS; ++i) {
            float absVal = std::abs(b[i]);
            float coeff = (absVal > levelStates[i]) ? mLevelAtt : mLevelRel;
            levelStates[i] += (absVal - levelStates[i]) * coeff;
            float r = levelStates[i];

            float effectiveThresh = cachedLinThresh[i];
            if (harmonicShield && i >= 3) {
                effectiveThresh *= (1.0f - 0.65f * fundamentalActivity);
            }

            float targetGain = 1.0f;
            if (r < effectiveThresh) {
                float diffDb = 20.0f * std::log10((r + 1e-9f) / effectiveThresh);
                float attDb = diffDb * reductionNorm * 1.6f;
                attDb = std::clamp(attDb, -45.0f, 0.0f);
                targetGain = std::pow(10.0f, attDb / 20.0f);
            }

            // Non-zero floor (0.08) ensures natural decay into silence without squelch
            targetGain = std::max(targetGain, 0.08f);
            currentGains[i] += (targetGain - currentGains[i]) * 0.15f;

            if (listenNoise) {
                outSum += b[i] * (1.0f - currentGains[i]);
            } else {
                outSum += b[i] * currentGains[i];
            }
        }

        return outSum;
    }

private:
    float mSampleRate = 48000.0f;
    float mHpF = 0.0f;
    float mLpF_cut = 0.0f;
    float mLevelAtt = 0.0f;
    float mLevelRel = 0.0f;
    float mBCoeffs[9] = {0.0f};

    float lpStates[9] = {0.0f};
    float levelStates[NUM_BANDS] = {0.0f};
    float currentGains[NUM_BANDS] = {1.0f};
    float cachedLinThresh[NUM_BANDS] = {0.0f};
    float lastThreshDb = -999.0f;
    float hpCutStat = 0.0f;
    float lpCutStat = 0.0f;
};

// ============================================================================
// Stage 1: Fluid Space Drum Shell & Pneumatic Vent DSP
// Models pneumatic air compression, adiabatic air springs, membrane pitch-glide,
// batter-to-resonant head column propagation, and sympathetic wire sizzle
// ============================================================================
class FluidDrumShellDSP {
private:
    static constexpr int kMaxShellDelay = 512; // Up to 10.6ms buffer @ 48kHz (supports up to 192kHz)
    float shellAirColumnBuf[kMaxShellDelay] = {0.0f};
    int writeIdx = 0;

    // Pneumatic Shell & Membrane State Variables
    float chamberPressure = 1.0f;    // Pneumatic air-vent compression state
    float headPosition = 0.0f;       // Resonant head displacement
    float headVelocity = 0.0f;       // Resonant head velocity
    float prevInput = 0.0f;
    float stickSnapMomentum = 0.0f;
    float snareWireEnvelope = 0.0f;
    float dcBlockerX1 = 0.0f, dcBlockerY1 = 0.0f;

    // Fast deterministic noise state for sympathetic snare wire sizzle
    uint32_t rngState = 2463534242u;

    float sampleRate = 48000.0f;
    float timeStep = 1.0f / 48000.0f;

    inline float fastWhiteNoise() noexcept {
        // Constant-time 3-cycle Xorshift PRNG (Zero library calls)
        rngState ^= rngState << 13;
        rngState ^= rngState >> 17;
        rngState ^= rngState << 5;
        return static_cast<float>(static_cast<int32_t>(rngState)) * 4.65661287e-10f;
    }

public:
    void prepare(float sr) noexcept {
        sampleRate = sr > 8000.0f ? sr : 48000.0f;
        timeStep = 1.0f / sampleRate;
        reset();
    }

    void reset() noexcept {
        std::fill(std::begin(shellAirColumnBuf), std::end(shellAirColumnBuf), 0.0f);
        writeIdx = 0;
        chamberPressure = 1.0f;
        headPosition = 0.0f;
        headVelocity = 0.0f;
        prevInput = 0.0f;
        stickSnapMomentum = 0.0f;
        snareWireEnvelope = 0.0f;
        dcBlockerX1 = dcBlockerY1 = 0.0f;
    }

    float getChamberPressure() const noexcept { return chamberPressure; }
    float getHeadDisplacement() const noexcept { return headPosition; }
    float getWireActivity() const noexcept { return snareWireEnvelope; }

    inline float processSample(float inputMicOrTrigger,
                               float drumFundamentalHz = 110.0f,
                               float shellDepthScalar = 0.55f,
                               float ventChokeAmount = 0.70f,
                               float snareWireBuzz = 0.0f,
                               float closeMicAir = 0.65f,
                               float sympatheticKitBleed = 0.0f) noexcept
    {
        // 1. STICK / BEATER TRANSIENT VELOCITY & FLUID VISCOSITY CUSHION
        const float instantVelocity = inputMicOrTrigger - prevInput;
        prevInput = inputMicOrTrigger;
        stickSnapMomentum = (stickSnapMomentum * 0.58f) + (instantVelocity * 0.42f);

        const float stickCrack = FluidMath::fast_tanh(stickSnapMomentum * 2.8f) * closeMicAir * 0.50f;

        // 2. PNEUMATIC AIR-VENT COMPRESSION (Internal Shell Pressure Bottleneck)
        const float hitEnergy = std::min(std::abs(inputMicOrTrigger) * (1.5f + ventChokeAmount * 2.0f), 2.5f);
        const float ventRecoverySpeed = 0.0008f + (1.0f - shellDepthScalar) * 0.0025f;

        chamberPressure -= hitEnergy * 0.0045f * ventChokeAmount;
        chamberPressure += (1.0f - chamberPressure) * ventRecoverySpeed;
        chamberPressure = std::clamp(chamberPressure, 0.30f, 1.0f);

        const float compressedBatterWave = FluidMath::fluidTransfer(
            inputMicOrTrigger * chamberPressure * (1.2f + ventChokeAmount * 0.8f), 
            0.35f
        );

        // 3. TOP-TO-BOTTOM HEAD AIR COLUMN PROPAGATION DELAY
        shellAirColumnBuf[writeIdx] = compressedBatterWave;
        const int mask = kMaxShellDelay - 1;
        const int shellDelaySamples = std::clamp(
            static_cast<int>((0.0003f + shellDepthScalar * 0.0019f) * sampleRate), 
            1, mask - 1
        );
        const int readIdx = (writeIdx - shellDelaySamples + kMaxShellDelay) & mask;
        const float bottomHeadExcitation = shellAirColumnBuf[readIdx] + (sympatheticKitBleed * 0.25f);
        writeIdx = (writeIdx + 1) & mask;

        // 4. NON-LINEAR DRUMHEAD MASS-SPRING WITH TENSION PITCH-GLIDE
        const float pitchGlideBoost = 1.0f + ((1.0f - chamberPressure) * ventChokeAmount * 0.32f);
        const float omega = 2.0f * 3.14159265f * drumFundamentalHz * pitchGlideBoost;
        const float stiffness = omega * omega;
        const float damping = 18.0f + (1.0f - shellDepthScalar) * 35.0f;

        const float restoringForce = -stiffness * headPosition;
        const float dampingForce   = -damping * headVelocity;
        const float driveForce     = bottomHeadExcitation * stiffness * 0.45f;

        headVelocity += (driveForce + restoringForce + dampingForce) * timeStep;
        headPosition += headVelocity * timeStep;
        headPosition = FluidMath::fast_tanh(headPosition * 1.4f);

        // 5. NON-LINEAR SNARE WIRE RATTLE
        float wireSizzle = 0.0f;
        if (snareWireBuzz > 0.001f) {
            const float headSpeed = std::abs(headVelocity) * 0.0015f + std::abs(bottomHeadExcitation) * 0.8f;
            const float envRate = (headSpeed > snareWireEnvelope) ? 0.08f : 0.0018f;
            snareWireEnvelope += (headSpeed - snareWireEnvelope) * envRate;
            snareWireEnvelope = std::min(snareWireEnvelope, 1.2f);

            const float rawNoise = fastWhiteNoise();
            wireSizzle = FluidMath::fast_tanh(rawNoise * snareWireEnvelope * 3.0f) * snareWireBuzz * 0.35f;
        }

        // 6. COMBINE BATTER ATTACK, SHELL BODY, STICK SNAP & SNARE WIRES
        const float combinedDrum = (compressedBatterWave * 0.70f)
                                 + (headPosition * (0.55f + closeMicAir * 0.35f))
                                 + stickCrack
                                 + wireSizzle;

        // DC Blocker
        const float dcOut = combinedDrum - dcBlockerX1 + 0.995f * dcBlockerY1;
        dcBlockerX1 = combinedDrum;
        dcBlockerY1 = dcOut;

        return FluidMath::fast_tanh(dcOut * 1.20f);
    }
};

// ============================================================================
// Stage 1B: Fluid Bronze Cymbal & Plate Dispersion DSP
// Models flexural wave dispersion in B20/B8 bronze alloy plates,
// bell vs bow modal resonance, and air viscosity shimmer (zero harsh trash)
// ============================================================================
class FluidCymbalDSP {
private:
    float apX[4] = {0.0f}, apY[4] = {0.0f};

    float bellBp = 0.0f, bellLp = 0.0f;
    float bowBp = 0.0f,  bowLp = 0.0f;

    float washState = 0.0f;
    float harshDetectEnv = 0.0f;
    float sampleRate = 48000.0f;

public:
    void prepare(float sr) noexcept {
        sampleRate = sr > 8000.0f ? sr : 48000.0f;
        reset();
    }

    void reset() noexcept {
        for (int i = 0; i < 4; ++i) apX[i] = apY[i] = 0.0f;
        bellBp = bellLp = 0.0f;
        bowBp = bowLp = 0.0f;
        washState = 0.0f;
        harshDetectEnv = 0.0f;
    }

    inline float processSample(float in, float bellFreq = 2200.0f, float bowFreq = 650.0f,
                               float shimmer = 0.70f, float bronzeViscosity = 0.45f) noexcept
    {
        // 1. Flexural Wave Dispersion: high frequencies travel faster in thin metal plates
        float s = in;
        const float g = 0.62f;
        for (int i = 0; i < 4; ++i) {
            float d = -g * s + apX[i] + g * apY[i];
            apX[i] = s;
            apY[i] = d;
            s = d;
        }

        // 2. Dual Modal Bell & Bow Resonators
        const float wBell = std::clamp(2.0f * 3.14159265f * bellFreq / sampleRate, 0.01f, 0.45f);
        const float qBell = 0.12f;
        float hpBell = in - bellLp - qBell * bellBp;
        bellBp += wBell * hpBell;
        bellLp += wBell * bellBp;

        const float wBow = std::clamp(2.0f * 3.14159265f * bowFreq / sampleRate, 0.01f, 0.45f);
        const float qBow = 0.35f;
        float hpBow = in - bowLp - qBow * bowBp;
        bowBp += wBow * hpBow;
        bowLp += wBow * bowBp;

        // 3. Dynamic Harsh Sizzle Sieve (Tames 5-8 kHz trashiness, opens 12-18 kHz air)
        const float highEnergy = std::abs(in - bowLp);
        harshDetectEnv += (highEnergy - harshDetectEnv) * 0.005f;
        const float harshDamp = std::clamp(1.0f - harshDetectEnv * 0.85f * bronzeViscosity, 0.25f, 1.0f);

        // 4. Free-Air Viscosity Shimmer Wash
        const float washAlpha = std::clamp(0.40f + shimmer * 0.45f, 0.1f, 0.95f);
        washState += (s - washState) * washAlpha;

        const float combined = (in * 0.35f)
                             + (s * harshDamp * 0.45f)
                             + (bellBp * 0.30f * shimmer)
                             + (bowBp * 0.40f)
                             + (washState * 0.35f * shimmer);

        return FluidMath::fast_tanh(combined * 1.15f);
    }
};

// ============================================================================
// Stage 2: Live Acoustic Drum Kit Morpher (Zero-Sample Physical Re-Shelling)
// De-resonates boxy room/shell mud & injects physical drum shell models in real time
// ============================================================================
struct DrumShellProfile {
    const char* name;
    float originalBoxyCutHz;   // Frequency of the real drum's cardboard mud to strip
    float targetFundamentalHz; // New virtual drum tuning (Hz)
    float shellDepthScalar;    // 0.2 (Piccolo) -> 0.95 (26" Bonham Kick) or Cymbal Diameter
    float ventChokeAmount;     // Pneumatic air-spring compression or Cymbal Shimmer
    float woodViscosity;       // 0.35 (Crisp Birch/Brass) -> 0.82 (Warm Vintage Maple) or Bronze Viscosity
    float snareWireBuzz;       // 0.0 (Kick/Tom) -> 0.65 (Deep Studio Snare Wires) or Bell Ping
    float headDamping;         // 0.2 (Wide open ring) -> 0.9 (70s Tea-Towel / MoonGel dead)
    bool  isCymbal = false;
};

namespace DrumKitProfiles {
    // 1. Kick Drum Profiles (8 Profiles)
    constexpr DrumShellProfile kKick_26BonhamMaple     = {"26-Inch Bonham Maple Kick",       165.0f,  48.0f, 0.95f, 0.70f, 0.75f, 0.00f, 0.30f, false};
    constexpr DrumShellProfile kKick_70sDryDead        = {"70s Tea-Towel Dry Kick",         195.0f,  58.0f, 0.50f, 0.85f, 0.80f, 0.00f, 0.88f, false};
    constexpr DrumShellProfile kKick_ModernBirch       = {"Modern Punchy Birch 22\" Kick",  220.0f,  52.0f, 0.80f, 0.90f, 0.42f, 0.00f, 0.65f, false};
    constexpr DrumShellProfile kKick_SubCannon808      = {"Sub-Piston Cannon 24\" Kick",    180.0f,  42.0f, 0.90f, 0.95f, 0.70f, 0.00f, 0.45f, false};
    constexpr DrumShellProfile kKick_MetalClick        = {"Metal Click Sub-Drop Kick",       240.0f,  45.0f, 0.85f, 0.92f, 0.35f, 0.00f, 0.75f, false};
    constexpr DrumShellProfile kKick_Vintage60sDown    = {"Vintage 60s 20\" Downbeat Kick", 175.0f,  62.0f, 0.60f, 0.65f, 0.78f, 0.00f, 0.40f, false};
    constexpr DrumShellProfile kKick_FunkTightBirch    = {"Heavy Funk Tight 20\" Birch Kick",210.0f, 55.0f, 0.65f, 0.88f, 0.45f, 0.00f, 0.80f, false};
    constexpr DrumShellProfile kKick_TrapAnalog808     = {"Trap / Electronic 808 Sub Boom", 150.0f,  38.0f, 0.95f, 0.98f, 0.80f, 0.00f, 0.25f, false};

    // 2. Snare Drum Profiles (8 Profiles)
    constexpr DrumShellProfile kSnare_BlackBeauty      = {"6.5x14 Bell Brass Black Beauty",  290.0f, 185.0f, 0.60f, 0.72f, 0.38f, 0.50f, 0.40f, false};
    constexpr DrumShellProfile kSnare_Fat70sWallet     = {"70s Fat Dead Wallet Snare",       340.0f, 145.0f, 0.70f, 0.88f, 0.82f, 0.55f, 0.90f, false};
    constexpr DrumShellProfile kSnare_TightPiccolo     = {"Tight Funk Birch 13\" Piccolo",   260.0f, 225.0f, 0.25f, 0.65f, 0.45f, 0.40f, 0.55f, false};
    constexpr DrumShellProfile kSnare_VintageMaple     = {"5.5x14 Studio Maple All-Round",  310.0f, 195.0f, 0.50f, 0.75f, 0.78f, 0.48f, 0.50f, false};
    constexpr DrumShellProfile kSnare_Supraphonic400   = {"Ludwig Supraphonic 400 Aluminum", 320.0f, 210.0f, 0.50f, 0.70f, 0.32f, 0.52f, 0.38f, false};
    constexpr DrumShellProfile kSnare_80sGatedPower    = {"80s Power Gated Rock Snare",      280.0f, 170.0f, 0.75f, 0.85f, 0.50f, 0.65f, 0.45f, false};
    constexpr DrumShellProfile kSnare_NashvilleWood    = {"Nashville Solid Maple Wood-Hoop", 330.0f, 165.0f, 0.65f, 0.80f, 0.85f, 0.45f, 0.60f, false};
    constexpr DrumShellProfile kSnare_ReggaeSoprano    = {"Reggae High Soprano 10\" Snare",  250.0f, 260.0f, 0.20f, 0.60f, 0.40f, 0.35f, 0.35f, false};

    // 3. Tom Profiles (4 Profiles)
    constexpr DrumShellProfile kTom_Rack10Fusion       = {"10-Inch Fusion High Tom",         260.0f, 160.0f, 0.45f, 0.65f, 0.68f, 0.00f, 0.40f, false};
    constexpr DrumShellProfile kTom_Rack12             = {"12-Inch Classic Studio Rack Tom", 240.0f, 130.0f, 0.55f, 0.70f, 0.70f, 0.00f, 0.45f, false};
    constexpr DrumShellProfile kTom_Floor16            = {"16-Inch Deep Studio Floor Tom",   175.0f,  85.0f, 0.80f, 0.80f, 0.75f, 0.00f, 0.40f, false};
    constexpr DrumShellProfile kTom_Bonham18           = {"18-Inch Bonham Low Thunder Tom",  150.0f,  68.0f, 0.95f, 0.85f, 0.78f, 0.00f, 0.30f, false};

    // 4. Cymbals & Bronze (5 Profiles)
    constexpr DrumShellProfile kCymbal_TurkishRide22   = {"Dark Turkish Ride 22\" (B20 Bronze)", 450.0f, 2200.0f, 0.85f, 0.75f, 0.55f, 0.40f, 0.30f, true};
    constexpr DrumShellProfile kCymbal_Vintage60sHats  = {"Vintage 60s A-Style 14\" Hi-Hats",    350.0f, 2600.0f, 0.55f, 0.80f, 0.40f, 0.50f, 0.50f, true};
    constexpr DrumShellProfile kCymbal_FastCrash18     = {"Fast Studio Bronze Crash 18\"",       500.0f, 1800.0f, 0.70f, 0.85f, 0.45f, 0.35f, 0.25f, true};
    constexpr DrumShellProfile kCymbal_HeavyRockRide21 = {"Heavy Rock Bell Ride 21\"",          400.0f, 2400.0f, 0.80f, 0.70f, 0.35f, 0.65f, 0.35f, true};
    constexpr DrumShellProfile kCymbal_TrashChina16    = {"Thin White-Bronze China / Trash 16\"", 600.0f, 2000.0f, 0.60f, 0.90f, 0.30f, 0.25f, 0.20f, true};

    // 5. Cajons & World Percussion (5 Profiles)
    constexpr DrumShellProfile kCajon_DeepFlamenco     = {"Flamenco Deep Acoustic Cajon",     190.0f,  65.0f, 0.85f, 0.75f, 0.80f, 0.40f, 0.50f, false};
    constexpr DrumShellProfile kCajon_CrispAcoustic    = {"Acoustic Gig Live Cajon",          220.0f,  72.0f, 0.75f, 0.70f, 0.75f, 0.45f, 0.45f, false};
    constexpr DrumShellProfile kPerc_CongaTumbao       = {"Afro-Cuban Conga / Quinto Tumbao", 280.0f, 190.0f, 0.70f, 0.65f, 0.70f, 0.00f, 0.60f, false};
    constexpr DrumShellProfile kPerc_DjembeBass        = {"African Djembe Deep Bass",         160.0f,  55.0f, 0.90f, 0.80f, 0.75f, 0.00f, 0.35f, false};
    constexpr DrumShellProfile kPerc_TimbaleCowbell    = {"Latin Timbale & Cowbell Rim",      350.0f, 240.0f, 0.30f, 0.60f, 0.35f, 0.00f, 0.25f, false};

    constexpr int kTotalProfiles = 30;
    constexpr DrumShellProfile kAllProfiles[kTotalProfiles] = {
        // Kicks 0..7
        kKick_26BonhamMaple,
        kKick_70sDryDead,
        kKick_ModernBirch,
        kKick_SubCannon808,
        kKick_MetalClick,
        kKick_Vintage60sDown,
        kKick_FunkTightBirch,
        kKick_TrapAnalog808,
        // Snares 8..15
        kSnare_BlackBeauty,
        kSnare_Fat70sWallet,
        kSnare_TightPiccolo,
        kSnare_VintageMaple,
        kSnare_Supraphonic400,
        kSnare_80sGatedPower,
        kSnare_NashvilleWood,
        kSnare_ReggaeSoprano,
        // Toms 16..19
        kTom_Rack10Fusion,
        kTom_Rack12,
        kTom_Floor16,
        kTom_Bonham18,
        // Cymbals 20..24
        kCymbal_TurkishRide22,
        kCymbal_Vintage60sHats,
        kCymbal_FastCrash18,
        kCymbal_HeavyRockRide21,
        kCymbal_TrashChina16,
        // Cajons & Percussion 25..29
        kCajon_DeepFlamenco,
        kCajon_CrispAcoustic,
        kPerc_CongaTumbao,
        kPerc_DjembeBass,
        kPerc_TimbaleCowbell
    };
}

class FluidDrumKitMorpherDSP {
private:
    FluidDrumShellDSP virtualShell;
    FluidCymbalDSP    virtualCymbal;

    // State variables for the Boxy Shell De-Resonator (2nd-order state-variable notch/scoop)
    float bpState = 0.0f;
    float lpState = 0.0f;
    float woodViscosityState = 0.0f;
    float sampleRate = 48000.0f;

public:
    void prepare(float sr) noexcept {
        sampleRate = sr > 8000.0f ? sr : 48000.0f;
        virtualShell.prepare(sampleRate);
        virtualCymbal.prepare(sampleRate);
        bpState = lpState = woodViscosityState = 0.0f;
    }

    void reset() noexcept {
        virtualShell.reset();
        virtualCymbal.reset();
        bpState = lpState = woodViscosityState = 0.0f;
    }

    float getChamberPressure() const noexcept { return virtualShell.getChamberPressure(); }
    float getHeadDisplacement() const noexcept { return virtualShell.getHeadDisplacement(); }
    float getWireActivity() const noexcept { return virtualShell.getWireActivity(); }

    inline float processSample(float liveMicInput, 
                               const DrumShellProfile& profile, 
                               float morphAmount = 0.85f,
                               float deBoxIntensity = 0.75f,
                               float customTuningHz = 0.0f,
                               float customDepth = 0.0f,
                               float customVentChoke = 0.0f,
                               float customWireBuzz = -1.0f,
                               float sympatheticBleed = 0.0f) noexcept 
    {
        if (profile.isCymbal) {
            // Cymbal branch: Bronze plate flexural dispersion & shimmer
            const float bellF = customTuningHz > 500.0f ? customTuningHz : profile.targetFundamentalHz;
            const float bowF = 550.0f;
            const float shimmer = customVentChoke >= 0.0f ? customVentChoke : profile.ventChokeAmount;
            const float visc = profile.woodViscosity;

            float cymbalOut = virtualCymbal.processSample(liveMicInput, bellF, bowF, shimmer, visc);
            return (liveMicInput * (1.0f - morphAmount)) + (cymbalOut * morphAmount);
        }

        // Drum Shell branch: Boxy mud cancellation + pneumatic air-spring chamber
        const float boxyCut = std::clamp(profile.originalBoxyCutHz, 50.0f, 1000.0f);
        const float f = std::clamp(2.0f * 3.14159265f * boxyCut / sampleRate, 0.005f, 0.45f);
        const float qDamp = 0.75f;
        
        const float hp = liveMicInput - lpState - qDamp * bpState;
        bpState += f * hp;
        lpState += f * bpState;

        const float deBoxedExcitation = liveMicInput - (bpState * deBoxIntensity * morphAmount);

        woodViscosityState = (woodViscosityState * profile.woodViscosity) 
                           + (deBoxedExcitation * (1.0f - profile.woodViscosity));
        const float shapedExcitation = (deBoxedExcitation * (1.0f - profile.headDamping * 0.45f)) 
                                     + (woodViscosityState * profile.headDamping * 0.45f);

        const float tuningHz = customTuningHz > 20.0f ? customTuningHz : profile.targetFundamentalHz;
        const float depthScalar = customDepth > 0.05f ? customDepth : profile.shellDepthScalar;
        const float ventChoke = customVentChoke >= 0.0f ? customVentChoke : profile.ventChokeAmount;
        const float wireBuzz = customWireBuzz >= 0.0f ? customWireBuzz : profile.snareWireBuzz;

        const float newShellOutput = virtualShell.processSample(
            shapedExcitation,
            tuningHz,
            depthScalar,
            ventChoke,
            wireBuzz,
            1.0f - (profile.headDamping * 0.5f),
            sympatheticBleed
        );

        return (liveMicInput * (1.0f - morphAmount)) + (newShellOutput * morphAmount);
    }
};

// ============================================================================
// Stage 3: Studio 4-Band Drum Parametric EQ (Active 3D Tone Matrix)
// ============================================================================
class Drum4BandEQ {
public:
    void init(float sampleRate) {
        mSampleRate = sampleRate > 8000.0f ? sampleRate : 48000.0f;
        reset();
    }

    void reset() {
        for (int i = 0; i < 4; ++i) {
            s1[i] = s2[i] = 0.0f;
            lastF[i] = lastG[i] = -999.0f;
        }
    }

    inline float process(float in, float lowGainDb,
                         float lowMidFreq, float lowMidGainDb,
                         float highMidFreq, float highMidGainDb,
                         float highGainDb) {
        float sample = in;
        sample = processFilter(0, sample, 80.0f, lowGainDb, 0.707f, FilterType::LowShelf);
        sample = processFilter(1, sample, lowMidFreq, lowMidGainDb, 1.4f, FilterType::Peak);
        sample = processFilter(2, sample, highMidFreq, highMidGainDb, 1.2f, FilterType::Peak);
        sample = processFilter(3, sample, 10500.0f, highGainDb, 0.707f, FilterType::HighShelf);
        return sample;
    }

private:
    enum class FilterType { LowShelf, Peak, HighShelf };

    float mSampleRate = 48000.0f;
    float s1[4] = {0.0f};
    float s2[4] = {0.0f};
    float b0[4] = {1.0f}, b1[4] = {0.0f}, b2[4] = {0.0f};
    float a1[4] = {0.0f}, a2[4] = {0.0f};
    float lastF[4] = {-999.0f}, lastG[4] = {-999.0f};

    inline float processFilter(int idx, float in, float freq, float gainDb, float Q, FilterType type) {
        if (std::abs(freq - lastF[idx]) > 0.5f || std::abs(gainDb - lastG[idx]) > 0.05f) {
            lastF[idx] = freq;
            lastG[idx] = gainDb;
            computeCoefficients(idx, freq, gainDb, Q, type);
        }

        float out = b0[idx] * in + s1[idx];
        s1[idx] = b1[idx] * in - a1[idx] * out + s2[idx];
        s2[idx] = b2[idx] * in - a2[idx] * out;
        return out;
    }

    void computeCoefficients(int idx, float freq, float gainDb, float Q, FilterType type) {
        float A = std::pow(10.0f, gainDb / 40.0f);
        float w0 = 2.0f * 3.14159265f * std::clamp(freq, 20.0f, mSampleRate * 0.48f) / mSampleRate;
        float cosw0 = std::cos(w0);
        float sinw0 = std::sin(w0);
        float alpha = sinw0 / (2.0f * Q);

        float a0_inv = 1.0f;

        if (type == FilterType::Peak) {
            float b0_raw = 1.0f + alpha * A;
            float b1_raw = -2.0f * cosw0;
            float b2_raw = 1.0f - alpha * A;
            float a0_raw = 1.0f + alpha / A;
            float a1_raw = -2.0f * cosw0;
            float a2_raw = 1.0f - alpha / A;

            a0_inv = 1.0f / a0_raw;
            b0[idx] = b0_raw * a0_inv;
            b1[idx] = b1_raw * a0_inv;
            b2[idx] = b2_raw * a0_inv;
            a1[idx] = a1_raw * a0_inv;
            a2[idx] = a2_raw * a0_inv;
        } else if (type == FilterType::LowShelf) {
            float two_sqrt_A_alpha = 2.0f * std::sqrt(A) * alpha;
            float b0_raw = A * ((A + 1.0f) - (A - 1.0f) * cosw0 + two_sqrt_A_alpha);
            float b1_raw = 2.0f * A * ((A - 1.0f) - (A + 1.0f) * cosw0);
            float b2_raw = A * ((A + 1.0f) - (A - 1.0f) * cosw0 - two_sqrt_A_alpha);
            float a0_raw = (A + 1.0f) + (A - 1.0f) * cosw0 + two_sqrt_A_alpha;
            float a1_raw = -2.0f * ((A - 1.0f) + (A + 1.0f) * cosw0);
            float a2_raw = (A + 1.0f) + (A - 1.0f) * cosw0 - two_sqrt_A_alpha;

            a0_inv = 1.0f / a0_raw;
            b0[idx] = b0_raw * a0_inv;
            b1[idx] = b1_raw * a0_inv;
            b2[idx] = b2_raw * a0_inv;
            a1[idx] = a1_raw * a0_inv;
            a2[idx] = a2_raw * a0_inv;
        } else if (type == FilterType::HighShelf) {
            float two_sqrt_A_alpha = 2.0f * std::sqrt(A) * alpha;
            float b0_raw = A * ((A + 1.0f) + (A - 1.0f) * cosw0 + two_sqrt_A_alpha);
            float b1_raw = -2.0f * A * ((A - 1.0f) + (A + 1.0f) * cosw0);
            float b2_raw = A * ((A + 1.0f) - (A - 1.0f) * cosw0 - two_sqrt_A_alpha);
            float a0_raw = (A + 1.0f) - (A - 1.0f) * cosw0 + two_sqrt_A_alpha;
            float a1_raw = 2.0f * ((A - 1.0f) - (A + 1.0f) * cosw0);
            float a2_raw = (A + 1.0f) - (A - 1.0f) * cosw0 - two_sqrt_A_alpha;

            a0_inv = 1.0f / a0_raw;
            b0[idx] = b0_raw * a0_inv;
            b1[idx] = b1_raw * a0_inv;
            b2[idx] = b2_raw * a0_inv;
            a1[idx] = a1_raw * a0_inv;
            a2[idx] = a2_raw * a0_inv;
        }
    }
};

// ============================================================================
// Stage 4: World-Class Modulated Dual-Tank Drum Reverb Space
// ============================================================================
class DrumFluidReverb {
private:
    static constexpr int kMaxPreDelay = 4096;
    float preDelayBuf[kMaxPreDelay] = {0.0f};
    int preDelayWrite = 0;

    static constexpr int kDiff1Len = 228;
    static constexpr int kDiff2Len = 172;
    static constexpr int kDiff3Len = 610;
    static constexpr int kDiff4Len = 445;
    float diff1Buf[kDiff1Len] = {0.0f}, diff2Buf[kDiff2Len] = {0.0f};
    float diff3Buf[kDiff3Len] = {0.0f}, diff4Buf[kDiff4Len] = {0.0f};
    int diff1Idx = 0, diff2Idx = 0, diff3Idx = 0, diff4Idx = 0;

    static constexpr int kTankA1Len = 7120;
    static constexpr int kTankA2Len = 5900;
    static constexpr int kTankB1Len = 7500;
    static constexpr int kTankB2Len = 5100;
    float tankA1Buf[kTankA1Len] = {0.0f}, tankA2Buf[kTankA2Len] = {0.0f};
    float tankB1Buf[kTankB1Len] = {0.0f}, tankB2Buf[kTankB2Len] = {0.0f};
    int tankA1Idx = 0, tankA2Idx = 0, tankB1Idx = 0, tankB2Idx = 0;

    float dampStateA = 0.0f, dampStateB = 0.0f;
    float sendHpStateL = 0.0f, sendHpStateR = 0.0f;

    float lfoPhase1 = 0.17f;
    float lfoPhase2 = 0.63f;

    float sampleRate = 48000.0f;

    inline float readHermite(const float* buf, int len, float pos) const noexcept {
        int i1 = static_cast<int>(pos);
        float frac = pos - static_cast<float>(i1);
        int i0 = (i1 - 1 + len) % len;
        int i2 = (i1 + 1) % len;
        int i3 = (i1 + 2) % len;
        i1 = i1 % len;

        float y0 = buf[i0], y1 = buf[i1], y2 = buf[i2], y3 = buf[i3];
        float c0 = y1;
        float c1 = 0.5f * (y2 - y0);
        float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
        float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
        return ((c3 * frac + c2) * frac + c1) * frac + c0;
    }

public:
    void init(float sr) noexcept {
        sampleRate = sr > 8000.0f ? sr : 48000.0f;
        reset();
    }

    void reset() noexcept {
        std::fill(std::begin(preDelayBuf), std::end(preDelayBuf), 0.0f);
        std::fill(std::begin(diff1Buf), std::end(diff1Buf), 0.0f);
        std::fill(std::begin(diff2Buf), std::end(diff2Buf), 0.0f);
        std::fill(std::begin(diff3Buf), std::end(diff3Buf), 0.0f);
        std::fill(std::begin(diff4Buf), std::end(diff4Buf), 0.0f);
        std::fill(std::begin(tankA1Buf), std::end(tankA1Buf), 0.0f);
        std::fill(std::begin(tankA2Buf), std::end(tankA2Buf), 0.0f);
        std::fill(std::begin(tankB1Buf), std::end(tankB1Buf), 0.0f);
        std::fill(std::begin(tankB2Buf), std::end(tankB2Buf), 0.0f);
        preDelayWrite = 0;
        diff1Idx = diff2Idx = diff3Idx = diff4Idx = 0;
        tankA1Idx = tankA2Idx = tankB1Idx = tankB2Idx = 0;
        dampStateA = dampStateB = 0.0f;
        sendHpStateL = sendHpStateR = 0.0f;
    }

    inline void processStereo(float inL, float inR,
                              float size, float highCutHz, float decayNorm, float mixNorm,
                              int algorithm, float& outL, float& outR) noexcept
    {
        if (mixNorm <= 0.001f) {
            outL = inL;
            outR = inR;
            return;
        }

        const float hpCut = (algorithm == 2) ? 180.0f : 80.0f;
        const float hpAlpha = 1.0f - std::exp(-2.0f * 3.14159265f * hpCut / sampleRate);
        sendHpStateL += (inL - sendHpStateL) * hpAlpha;
        sendHpStateR += (inR - sendHpStateR) * hpAlpha;
        const float monoSend = 0.5f * ((inL - sendHpStateL) + (inR - sendHpStateR));

        const float preDelayMs = (algorithm == 1) ? 22.0f : (algorithm == 2 ? 4.0f : 0.0f);
        const int preDelaySamples = std::clamp(static_cast<int>(preDelayMs * 0.001f * sampleRate), 0, kMaxPreDelay - 1);
        preDelayBuf[preDelayWrite] = monoSend;
        const int preReadIdx = (preDelayWrite - preDelaySamples + kMaxPreDelay) % kMaxPreDelay;
        const float delayedSend = preDelayBuf[preReadIdx];
        preDelayWrite = (preDelayWrite + 1) % kMaxPreDelay;

        const float diffCoeff1 = (algorithm == 2) ? 0.82f : 0.75f;
        const float diffCoeff2 = (algorithm == 2) ? 0.72f : 0.65f;

        auto processAP = [](float in, float* buf, int len, int& idx, float g) noexcept {
            float bufOut = buf[idx];
            float out = -g * in + bufOut;
            buf[idx] = in + g * out;
            idx = (idx + 1) % len;
            return out;
        };

        float diffSig = processAP(delayedSend, diff1Buf, kDiff1Len, diff1Idx, diffCoeff1);
        diffSig = processAP(diffSig, diff2Buf, kDiff2Len, diff2Idx, diffCoeff1);
        diffSig = processAP(diffSig, diff3Buf, kDiff3Len, diff3Idx, diffCoeff2);
        diffSig = processAP(diffSig, diff4Buf, kDiff4Len, diff4Idx, diffCoeff2);

        const float lfoRate1 = (algorithm == 2) ? 1.45f : 0.75f;
        const float lfoRate2 = (algorithm == 2) ? 1.15f : 0.55f;
        lfoPhase1 += lfoRate1 / sampleRate;
        if (lfoPhase1 > 1.0f) lfoPhase1 -= 1.0f;
        lfoPhase2 += lfoRate2 / sampleRate;
        if (lfoPhase2 > 1.0f) lfoPhase2 -= 1.0f;

        const float lfoDepth = (algorithm == 2) ? 12.0f : 5.0f;
        const float modOffset1 = FluidMath::fast_sin(lfoPhase1 * 6.2831853f) * lfoDepth;
        const float modOffset2 = FluidMath::fast_sin(lfoPhase2 * 6.2831853f) * lfoDepth;

        const float maxDecay = (algorithm == 1) ? 0.94f : (algorithm == 2 ? 0.92f : 0.78f);
        const float loopFeedback = std::clamp(decayNorm * maxDecay, 0.10f, 0.95f);

        const float dampCut = std::clamp(highCutHz, 1200.0f, 16000.0f);
        const float dampCoeff = 1.0f - std::exp(-2.0f * 3.14159265f * dampCut / sampleRate);

        float tankAIn = diffSig + tankB2Buf[tankB2Idx] * loopFeedback;
        tankAIn = FluidMath::fast_tanh(tankAIn * 0.95f);

        float readPosA1 = static_cast<float>(tankA1Idx) - (kTankA1Len * 0.5f * size + modOffset1);
        while (readPosA1 < 0.0f) readPosA1 += kTankA1Len;
        float tankA1Out = readHermite(tankA1Buf, kTankA1Len, readPosA1);
        tankA1Buf[tankA1Idx] = tankAIn;
        tankA1Idx = (tankA1Idx + 1) % kTankA1Len;

        dampStateA += (tankA1Out - dampStateA) * dampCoeff;
        float tankA2In = dampStateA;

        float tankA2Out = tankA2Buf[tankA2Idx];
        tankA2Buf[tankA2Idx] = tankA2In;
        tankA2Idx = (tankA2Idx + 1) % kTankA2Len;

        float tankBIn = diffSig + tankA2Out * loopFeedback;
        tankBIn = FluidMath::fast_tanh(tankBIn * 0.95f);

        float readPosB1 = static_cast<float>(tankB1Idx) - (kTankB1Len * 0.5f * size + modOffset2);
        while (readPosB1 < 0.0f) readPosB1 += kTankB1Len;
        float tankB1Out = readHermite(tankB1Buf, kTankB1Len, readPosB1);
        tankB1Buf[tankB1Idx] = tankBIn;
        tankB1Idx = (tankB1Idx + 1) % kTankB1Len;

        dampStateB += (tankB1Out - dampStateB) * dampCoeff;
        float tankB2In = dampStateB;

        tankB2Buf[tankB2Idx] = tankB2In;
        tankB2Idx = (tankB2Idx + 1) % kTankB2Len;

        float wetL = (tankA1Buf[(tankA1Idx + 189) % kTankA1Len] * 0.35f)
                   + (tankA1Buf[(tankA1Idx + 2980) % kTankA1Len] * 0.40f)
                   - (tankA2Buf[(tankA2Idx + 1420) % kTankA2Len] * 0.35f)
                   + (tankB1Buf[(tankB1Idx + 380) % kTankB1Len] * 0.30f)
                   - (tankB2Buf[(tankB2Idx + 2410) % kTankB2Len] * 0.28f);

        float wetR = (tankB1Buf[(tankB1Idx + 230) % kTankB1Len] * 0.35f)
                   + (tankB1Buf[(tankB1Idx + 3120) % kTankB1Len] * 0.40f)
                   - (tankB2Buf[(tankB2Idx + 1650) % kTankB2Len] * 0.35f)
                   + (tankA1Buf[(tankA1Idx + 420) % kTankA1Len] * 0.30f)
                   - (tankA2Buf[(tankA2Idx + 2190) % kTankA2Len] * 0.28f);

        outL = inL * (1.0f - mixNorm) + wetL * mixNorm * 1.35f;
        outR = inR * (1.0f - mixNorm) + wetR * mixNorm * 1.35f;
    }
};

// ============================================================================
// Master Drum Preamp Engine (Single Channel / Monolithic Unit)
// ============================================================================
class CyberDrumEngine {
public:
    DrumCyberDenoiser      denoiser;
    FluidDrumKitMorpherDSP morpher;
    Drum4BandEQ            eq;
    DrumFluidReverb        reverb;

    void init(float sampleRate) {
        denoiser.init(sampleRate);
        morpher.prepare(sampleRate);
        eq.init(sampleRate);
        reverb.init(sampleRate);
    }

    void reset() {
        denoiser.reset();
        morpher.reset();
        eq.reset();
        reverb.reset();
    }

    inline float processMonoDrum(float inSample,
                                 float denoiseAmt, float denoiseThresh, bool denoiseShield, bool denoiseListen,
                                 int profileIndex, float morphAmount, float deBoxIntensity,
                                 float customTuningHz, float customDepth, float customVentChoke, float customWireBuzz,
                                 float eqLow, float eqLowMidF, float eqLowMidG, float eqHighMidF, float eqHighMidG, float eqHigh,
                                 float sympatheticBleed = 0.0f)
    {
        float cleaned = denoiseListen ? denoiser.process(inSample, denoiseAmt, denoiseThresh, denoiseShield, true)
                                      : denoiser.process(inSample, denoiseAmt, denoiseThresh, denoiseShield, false);
        if (denoiseListen) return cleaned;

        const auto& profile = DrumKitProfiles::kAllProfiles[std::clamp(profileIndex, 0, DrumKitProfiles::kTotalProfiles - 1)];
        float reShelled = morpher.processSample(cleaned, profile, morphAmount, deBoxIntensity,
                                                customTuningHz, customDepth, customVentChoke, customWireBuzz, sympatheticBleed);

        float equalized = eq.process(reShelled, eqLow, eqLowMidF, eqLowMidG, eqHighMidF, eqHighMidG, eqHigh);

        return equalized;
    }
};

} // namespace CyberDrum
