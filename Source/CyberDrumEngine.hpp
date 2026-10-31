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

    // Returns the live internal chamber pressure state for UI visualization
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
        // --------------------------------------------------------------------
        // 1. STICK / BEATER TRANSIENT VELOCITY & FLUID VISCOSITY CUSHION
        // --------------------------------------------------------------------
        // Absorbs piercing digital rimshot spikes and converts them into thick stick "crack"
        const float instantVelocity = inputMicOrTrigger - prevInput;
        prevInput = inputMicOrTrigger;
        stickSnapMomentum = (stickSnapMomentum * 0.58f) + (instantVelocity * 0.42f);

        const float stickCrack = FluidMath::fast_tanh(stickSnapMomentum * 2.8f) * closeMicAir * 0.50f;

        // --------------------------------------------------------------------
        // 2. PNEUMATIC AIR-VENT COMPRESSION (Internal Shell Pressure Bottleneck)
        // --------------------------------------------------------------------
        // When hit hard, air gets trapped inside the wood shell, compressing the peak
        // and increasing the internal air-spring stiffness!
        const float hitEnergy = std::min(std::abs(inputMicOrTrigger) * (1.5f + ventChokeAmount * 2.0f), 2.5f);
        const float ventRecoverySpeed = 0.0008f + (1.0f - shellDepthScalar) * 0.0025f;

        chamberPressure -= hitEnergy * 0.0045f * ventChokeAmount;
        chamberPressure += (1.0f - chamberPressure) * ventRecoverySpeed;
        chamberPressure = std::clamp(chamberPressure, 0.30f, 1.0f);

        // Internal air compression rounds off the raw transient naturally
        const float compressedBatterWave = FluidMath::fluidTransfer(
            inputMicOrTrigger * chamberPressure * (1.2f + ventChokeAmount * 0.8f), 
            0.35f
        );

        // --------------------------------------------------------------------
        // 3. TOP-TO-BOTTOM HEAD AIR COLUMN PROPAGATION DELAY
        // --------------------------------------------------------------------
        // Simulates the physical time (0.3ms to 2.2ms) it takes the shockwave to travel
        // from the top batter head down through the shell to the bottom head
        shellAirColumnBuf[writeIdx] = compressedBatterWave;
        const int mask = kMaxShellDelay - 1;
        const int shellDelaySamples = std::clamp(
            static_cast<int>((0.0003f + shellDepthScalar * 0.0019f) * sampleRate), 
            1, mask - 1
        );
        const int readIdx = (writeIdx - shellDelaySamples + kMaxShellDelay) & mask;
        const float bottomHeadExcitation = shellAirColumnBuf[readIdx] + (sympatheticKitBleed * 0.25f);
        writeIdx = (writeIdx + 1) & mask;

        // --------------------------------------------------------------------
        // 4. NON-LINEAR DRUMHEAD MASS-SPRING WITH TENSION PITCH-GLIDE
        // --------------------------------------------------------------------
        // Hitting the drum harder lowers chamberPressure, which tightens the air-spring
        // and bends the drum's fundamental pitch slightly UP on impact before gliding down!
        const float pitchGlideBoost = 1.0f + ((1.0f - chamberPressure) * ventChokeAmount * 0.32f);
        const float omega = 2.0f * 3.14159265f * drumFundamentalHz * pitchGlideBoost;
        const float stiffness = omega * omega;
        const float damping = 18.0f + (1.0f - shellDepthScalar) * 35.0f;

        const float restoringForce = -stiffness * headPosition;
        const float dampingForce   = -damping * headVelocity;
        const float driveForce     = bottomHeadExcitation * stiffness * 0.45f;

        headVelocity += (driveForce + restoringForce + dampingForce) * timeStep;
        headPosition += headVelocity * timeStep;
        headPosition = FluidMath::fast_tanh(headPosition * 1.4f); // Elastic hoop boundary limit

        // --------------------------------------------------------------------
        // 5. NON-LINEAR SNARE WIRE RATTLE (Excited by Bottom Head Velocity)
        // --------------------------------------------------------------------
        float wireSizzle = 0.0f;
        if (snareWireBuzz > 0.001f) {
            const float headSpeed = std::abs(headVelocity) * 0.0015f + std::abs(bottomHeadExcitation) * 0.8f;
            const float envRate = (headSpeed > snareWireEnvelope) ? 0.08f : 0.0018f;
            snareWireEnvelope += (headSpeed - snareWireEnvelope) * envRate;
            snareWireEnvelope = std::min(snareWireEnvelope, 1.2f);

            // High-passed air-damped metallic wire rattle against the bottom Mylar head
            const float rawNoise = fastWhiteNoise();
            wireSizzle = FluidMath::fast_tanh(rawNoise * snareWireEnvelope * 3.0f) * snareWireBuzz * 0.35f;
        }

        // --------------------------------------------------------------------
        // 6. COMBINE BATTER ATTACK, SHELL BODY, STICK SNAP & SNARE WIRES
        // --------------------------------------------------------------------
        const float combinedDrum = (compressedBatterWave * 0.70f)
                                 + (headPosition * (0.55f + closeMicAir * 0.35f))
                                 + stickCrack
                                 + wireSizzle;

        // DC Blocker & Final Studio Tape/Transformer Headroom Cushion
        const float dcOut = combinedDrum - dcBlockerX1 + 0.995f * dcBlockerY1;
        dcBlockerX1 = combinedDrum;
        dcBlockerY1 = dcOut;

        return FluidMath::fast_tanh(dcOut * 1.20f);
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
    float shellDepthScalar;    // 0.2 (Piccolo) -> 0.95 (26" Bonham Kick)
    float ventChokeAmount;     // Pneumatic air-spring compression
    float woodViscosity;       // 0.35 (Crisp Birch/Brass) -> 0.82 (Warm Vintage Maple)
    float snareWireBuzz;       // 0.0 (Kick/Tom) -> 0.65 (Deep Studio Snare Wires)
    float headDamping;         // 0.2 (Wide open ring) -> 0.9 (70s Tea-Towel / MoonGel dead)
};

namespace DrumKitProfiles {
    // 1. Kick Drum Profiles
    constexpr DrumShellProfile kKick_70sDryDead     = {"70s Tea-Towel Dry Kick",   195.0f,  58.0f, 0.50f, 0.85f, 0.80f, 0.00f, 0.88f};
    constexpr DrumShellProfile kKick_26BonhamMaple  = {"26-Inch Bonham Maple",     165.0f,  48.0f, 0.95f, 0.70f, 0.75f, 0.00f, 0.30f};
    constexpr DrumShellProfile kKick_ModernBirch    = {"Modern Punchy Birch 22",   220.0f,  52.0f, 0.80f, 0.90f, 0.42f, 0.00f, 0.65f};
    constexpr DrumShellProfile kKick_SubCannon808   = {"Sub-Piston Cannon 24",     180.0f,  42.0f, 0.90f, 0.95f, 0.70f, 0.00f, 0.45f};

    // 2. Snare Drum Profiles
    constexpr DrumShellProfile kSnare_Fat70sWallet  = {"70s Fat Dead Wallet Snare", 340.0f, 145.0f, 0.70f, 0.88f, 0.82f, 0.55f, 0.90f};
    constexpr DrumShellProfile kSnare_BlackBeauty   = {"6.5x14 Bell Brass Crack",   290.0f, 185.0f, 0.60f, 0.72f, 0.38f, 0.45f, 0.40f};
    constexpr DrumShellProfile kSnare_TightPiccolo  = {"Tight Funk Birch 13",       260.0f, 225.0f, 0.25f, 0.65f, 0.45f, 0.35f, 0.55f};
    constexpr DrumShellProfile kSnare_VintageMaple  = {"5.5x14 Studio Maple",       310.0f, 195.0f, 0.50f, 0.75f, 0.78f, 0.48f, 0.50f};

    // 3. Tom Profiles
    constexpr DrumShellProfile kTom_Rack12          = {"12-Inch Rack Tom",          240.0f, 130.0f, 0.55f, 0.70f, 0.70f, 0.00f, 0.45f};
    constexpr DrumShellProfile kTom_Floor16         = {"16-Inch Floor Tom",         175.0f,  85.0f, 0.80f, 0.80f, 0.75f, 0.00f, 0.40f};
    constexpr DrumShellProfile kTom_Bonham18        = {"18-Inch Bonham Low Tom",    150.0f,  68.0f, 0.95f, 0.85f, 0.78f, 0.00f, 0.30f};

    // 4. Cajon & World Percussion Profiles
    constexpr DrumShellProfile kCajon_DeepFlamenco  = {"Flamenco Deep Cajon",       190.0f,  65.0f, 0.85f, 0.75f, 0.80f, 0.35f, 0.50f};
    constexpr DrumShellProfile kCajon_CrispAcoustic = {"Acoustic Gig Cajon",        220.0f,  72.0f, 0.75f, 0.70f, 0.75f, 0.45f, 0.45f};
    constexpr DrumShellProfile kPerc_CongaTumbao    = {"Deep Quinto / Conga",       280.0f, 190.0f, 0.70f, 0.65f, 0.70f, 0.00f, 0.60f};
    constexpr DrumShellProfile kPerc_DjembeBass     = {"African Djembe Bass",       160.0f,  55.0f, 0.90f, 0.80f, 0.75f, 0.00f, 0.35f};

    constexpr int kTotalProfiles = 15;
    constexpr DrumShellProfile kAllProfiles[kTotalProfiles] = {
        kKick_70sDryDead,
        kKick_26BonhamMaple,
        kKick_ModernBirch,
        kKick_SubCannon808,
        kSnare_Fat70sWallet,
        kSnare_BlackBeauty,
        kSnare_TightPiccolo,
        kSnare_VintageMaple,
        kTom_Rack12,
        kTom_Floor16,
        kTom_Bonham18,
        kCajon_DeepFlamenco,
        kCajon_CrispAcoustic,
        kPerc_CongaTumbao,
        kPerc_DjembeBass
    };
}

class FluidDrumKitMorpherDSP {
private:
    FluidDrumShellDSP virtualShell;

    // State variables for the Boxy Shell De-Resonator (2nd-order state-variable notch/scoop)
    float bpState = 0.0f;
    float lpState = 0.0f;
    float woodViscosityState = 0.0f;
    float sampleRate = 48000.0f;

public:
    void prepare(float sr) noexcept {
        sampleRate = sr > 8000.0f ? sr : 48000.0f;
        virtualShell.prepare(sampleRate);
        bpState = lpState = woodViscosityState = 0.0f;
    }

    void reset() noexcept {
        virtualShell.reset();
        bpState = lpState = woodViscosityState = 0.0f;
    }

    float getChamberPressure() const noexcept { return virtualShell.getChamberPressure(); }
    float getHeadDisplacement() const noexcept { return virtualShell.getHeadDisplacement(); }
    float getWireActivity() const noexcept { return virtualShell.getWireActivity(); }

    // morphAmount: 0.0f = 100% Original Raw Mic, 1.0f = 100% Re-Shelled Studio Kit
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
        // --------------------------------------------------------------------
        // 1. SUBTRACT ORIGINAL DRUM'S "CARDBOARD BOX" MUD (Zero-Phase Bleed Safe)
        // --------------------------------------------------------------------
        // Isolates the boxy 150Hz-350Hz resonance of the physical room/cheap shell
        // and phase-cancels it BEFORE building the new drum body!
        const float boxyCut = std::clamp(profile.originalBoxyCutHz, 50.0f, 1000.0f);
        const float f = std::clamp(2.0f * 3.14159265f * boxyCut / sampleRate, 0.005f, 0.45f);
        const float qDamp = 0.75f; // Wide enough to scoop cardboard mud, narrow enough to save transient
        
        const float hp = liveMicInput - lpState - qDamp * bpState;
        bpState += f * hp;
        lpState += f * bpState;

        // Subtract the isolated boxy ring proportionally to morphAmount & deBoxIntensity
        const float deBoxedExcitation = liveMicInput - (bpState * deBoxIntensity * morphAmount);

        // --------------------------------------------------------------------
        // 2. WOOD SHELL VISCOSITY & DAMPING SHAPING (Maple vs. Birch vs. Brass vs. Cajon)
        // --------------------------------------------------------------------
        woodViscosityState = (woodViscosityState * profile.woodViscosity) 
                           + (deBoxedExcitation * (1.0f - profile.woodViscosity));
        const float shapedExcitation = (deBoxedExcitation * (1.0f - profile.headDamping * 0.45f)) 
                                     + (woodViscosityState * profile.headDamping * 0.45f);

        // Determine effective parameters (allowing custom GUI overrides or profile defaults)
        const float tuningHz = customTuningHz > 20.0f ? customTuningHz : profile.targetFundamentalHz;
        const float depthScalar = customDepth > 0.05f ? customDepth : profile.shellDepthScalar;
        const float ventChoke = customVentChoke >= 0.0f ? customVentChoke : profile.ventChokeAmount;
        const float wireBuzz = customWireBuzz >= 0.0f ? customWireBuzz : profile.snareWireBuzz;

        // --------------------------------------------------------------------
        // 3. DRIVE THE NEW VIRTUAL FLUID SHELL
        // --------------------------------------------------------------------
        const float newShellOutput = virtualShell.processSample(
            shapedExcitation,
            tuningHz,
            depthScalar,
            ventChoke,
            wireBuzz,
            1.0f - (profile.headDamping * 0.5f), // Less high-air ring when heavily damped
            sympatheticBleed
        );

        // Seamless wet/dry blend between raw studio mic and re-shelled drum
        return (liveMicInput * (1.0f - morphAmount)) + (newShellOutput * morphAmount);
    }
};

// ============================================================================
// Stage 3: Studio 4-Band Drum Parametric EQ (Active 3D Tone Matrix)
// Low Shelf (Thump), Lo-Mid Boxiness Cut, Hi-Mid Attack/Crack, High Sizzle Shelf
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
        // Band 0: Low Shelf (80 Hz)
        sample = processFilter(0, sample, 80.0f, lowGainDb, 0.707f, FilterType::LowShelf);
        // Band 1: Low-Mid Parametric Boxiness Cut (200-800 Hz)
        sample = processFilter(1, sample, lowMidFreq, lowMidGainDb, 1.4f, FilterType::Peak);
        // Band 2: High-Mid Parametric Attack/Crack (1.5-6.5 kHz)
        sample = processFilter(2, sample, highMidFreq, highMidGainDb, 1.2f, FilterType::Peak);
        // Band 3: High Air Sizzle Shelf (10.5 kHz)
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

        // Transposed Direct Form II
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
            float b2_raw = A * ((A + 1.0f) + (A - 1.0f) * cosw0 - two_sqrt_A_alpha);
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
// Studio Drum Booth, Gated Live Arena Room, and Vintage Snare Plate
// ============================================================================
class DrumFluidReverb {
private:
    static constexpr int kMaxPreDelay = 4096;
    float preDelayBuf[kMaxPreDelay] = {0.0f};
    int preDelayWrite = 0;

    // 4 Schroeder-Dattorro Input Allpass Diffusers
    static constexpr int kDiff1Len = 228;
    static constexpr int kDiff2Len = 172;
    static constexpr int kDiff3Len = 610;
    static constexpr int kDiff4Len = 445;
    float diff1Buf[kDiff1Len] = {0.0f}, diff2Buf[kDiff2Len] = {0.0f};
    float diff3Buf[kDiff3Len] = {0.0f}, diff4Buf[kDiff4Len] = {0.0f};
    int diff1Idx = 0, diff2Idx = 0, diff3Idx = 0, diff4Idx = 0;

    // Dual Cross-Coupled Modulated Tanks (Tank A & Tank B)
    static constexpr int kTankA1Len = 7120;
    static constexpr int kTankA2Len = 5900;
    static constexpr int kTankB1Len = 7500;
    static constexpr int kTankB2Len = 5100;
    float tankA1Buf[kTankA1Len] = {0.0f}, tankA2Buf[kTankA2Len] = {0.0f};
    float tankB1Buf[kTankB1Len] = {0.0f}, tankB2Buf[kTankB2Len] = {0.0f};
    int tankA1Idx = 0, tankA2Idx = 0, tankB1Idx = 0, tankB2Idx = 0;

    // Low-Pass Damp Filters in each tank
    float dampStateA = 0.0f, dampStateB = 0.0f;
    float sendHpStateL = 0.0f, sendHpStateR = 0.0f;

    // LFO Modulation Phase
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

    // algorithm: 0 = Studio Drum Booth, 1 = Arena Live Hall, 2 = Vintage Snare Plate
    inline void processStereo(float inL, float inR,
                              float size, float highCutHz, float decayNorm, float mixNorm,
                              int algorithm, float& outL, float& outR) noexcept
    {
        if (mixNorm <= 0.001f) {
            outL = inL;
            outR = inR;
            return;
        }

        // Send High-Pass Filter (eliminates mud below 80 Hz on Room/Hall, or 180 Hz on Plate)
        const float hpCut = (algorithm == 2) ? 180.0f : 80.0f;
        const float hpAlpha = 1.0f - std::exp(-2.0f * 3.14159265f * hpCut / sampleRate);
        sendHpStateL += (inL - sendHpStateL) * hpAlpha;
        sendHpStateR += (inR - sendHpStateR) * hpAlpha;
        const float monoSend = 0.5f * ((inL - sendHpStateL) + (inR - sendHpStateR));

        // Pre-Delay: 0ms on Booth, 22ms on Arena Hall, 4ms on Plate
        const float preDelayMs = (algorithm == 1) ? 22.0f : (algorithm == 2 ? 4.0f : 0.0f);
        const int preDelaySamples = std::clamp(static_cast<int>(preDelayMs * 0.001f * sampleRate), 0, kMaxPreDelay - 1);
        preDelayBuf[preDelayWrite] = monoSend;
        const int preReadIdx = (preDelayWrite - preDelaySamples + kMaxPreDelay) % kMaxPreDelay;
        const float delayedSend = preDelayBuf[preReadIdx];
        preDelayWrite = (preDelayWrite + 1) % kMaxPreDelay;

        // 4 Input Allpass Diffusers
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

        // LFO Phase Modulation
        const float lfoRate1 = (algorithm == 2) ? 1.45f : 0.75f;
        const float lfoRate2 = (algorithm == 2) ? 1.15f : 0.55f;
        lfoPhase1 += lfoRate1 / sampleRate;
        if (lfoPhase1 > 1.0f) lfoPhase1 -= 1.0f;
        lfoPhase2 += lfoRate2 / sampleRate;
        if (lfoPhase2 > 1.0f) lfoPhase2 -= 1.0f;

        const float lfoDepth = (algorithm == 2) ? 12.0f : 5.0f;
        const float modOffset1 = FluidMath::fast_sin(lfoPhase1 * 6.2831853f) * lfoDepth;
        const float modOffset2 = FluidMath::fast_sin(lfoPhase2 * 6.2831853f) * lfoDepth;

        // Feedback Decay Loop
        const float maxDecay = (algorithm == 1) ? 0.94f : (algorithm == 2 ? 0.92f : 0.78f);
        const float loopFeedback = std::clamp(decayNorm * maxDecay, 0.10f, 0.95f);

        // High-Cut Damping
        const float dampCut = std::clamp(highCutHz, 1200.0f, 16000.0f);
        const float dampCoeff = 1.0f - std::exp(-2.0f * 3.14159265f * dampCut / sampleRate);

        // Tank A Processing
        float tankAIn = diffSig + tankB2Buf[tankB2Idx] * loopFeedback;
        tankAIn = FluidMath::fast_tanh(tankAIn * 0.95f); // Soft fluid non-linearity in tank

        // Tank A1 with Hermite cubic modulation
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

        // Tank B Processing
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

        // 14-Tap Spatial Stereo Extraction Matrix
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

        // Mix Wet & Dry
        outL = inL * (1.0f - mixNorm) + wetL * mixNorm * 1.35f;
        outR = inR * (1.0f - mixNorm) + wetR * mixNorm * 1.35f;
    }
};

// ============================================================================
// Master Drum Preamp Engine (Single Channel / Monolithic Unit)
// Combines Denoiser, Kit Morpher, 4-Band Sculpting EQ, and Reverb
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

    // Process mono drum channel through the complete fluid chain
    inline float processMonoDrum(float inSample,
                                 float denoiseAmt, float denoiseThresh, bool denoiseShield, bool denoiseListen,
                                 int profileIndex, float morphAmount, float deBoxIntensity,
                                 float customTuningHz, float customDepth, float customVentChoke, float customWireBuzz,
                                 float eqLow, float eqLowMidF, float eqLowMidG, float eqHighMidF, float eqHighMidG, float eqHigh,
                                 float sympatheticBleed = 0.0f)
    {
        // 1. Denoise stage
        float cleaned = denoiseListen ? denoiser.process(inSample, denoiseAmt, denoiseThresh, denoiseShield, true)
                                      : denoiser.process(inSample, denoiseAmt, denoiseThresh, denoiseShield, false);
        if (denoiseListen) return cleaned;

        // 2. Drum Kit Morpher & Fluid Shell
        const auto& profile = DrumKitProfiles::kAllProfiles[std::clamp(profileIndex, 0, DrumKitProfiles::kTotalProfiles - 1)];
        float reShelled = morpher.processSample(cleaned, profile, morphAmount, deBoxIntensity,
                                                customTuningHz, customDepth, customVentChoke, customWireBuzz, sympatheticBleed);

        // 3. Studio 4-Band EQ
        float equalized = eq.process(reShelled, eqLow, eqLowMidF, eqLowMidG, eqHighMidF, eqHighMidG, eqHigh);

        return equalized;
    }
};

} // namespace CyberDrum
