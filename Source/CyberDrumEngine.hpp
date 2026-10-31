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

    // Polynomial fluid boundary transfer curve (strictly bounded on tanh(x) per Rule 32)
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

    inline float flush_denormal(float v) noexcept {
        return (std::abs(v) < 1e-20f) ? 0.0f : v;
    }
}

// ============================================================================
// Stage 0: 10-Band Drum Cyber-Denoiser PRO (0ms Zero-Latency Subtraction)
// Identical continuous subtractive topology to CyberAcousticEngine
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

        mHpF = getCoeff(22.0f);        // 22 Hz sub-rumble filter
        mLpF_cut = getCoeff(19000.0f); // 19 kHz ultrasonic ceiling

        mLevelAtt = getCoeff(1200.0f); // < 1 ms instantaneous peak attack
        mLevelRel = getCoeff(14.0f);   // Smooth organic release
        mGainAtt  = getCoeff(1800.0f); // Instantaneous gate opening on drum transients

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

        // Harmonic Shield for Drum Fundamentals (40 Hz to 350 Hz)
        float fundamentalActivity = 0.0f;
        if (harmonicShield) {
            for (int fBand = 0; fBand <= 2; ++fBand) {
                float rf = levelStates[fBand];
                float rawG = 1.0f - (cachedLinThresh[fBand] / (rf + 1e-9f));
                fundamentalActivity = std::max(fundamentalActivity, std::clamp(rawG, 0.0f, 1.0f));
            }
        }

        const float floorVal = 0.06f;
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

            // Continuous subtractive gain formula (same as CyberAcousticEngine)
            float targetG = 1.0f - (effectiveThresh / (r + 1e-9f));
            targetG = std::max(floorVal, std::min(1.0f, targetG));

            float scaledTargetG = 1.0f - reductionNorm * (1.0f - targetG);

            float currentCoeff;
            if (scaledTargetG > currentGains[i]) {
                currentCoeff = mGainAtt;
            } else {
                float tailDist = (currentGains[i] - floorVal) / (1.01f - floorVal);
                float tailStab = 1.0f + (1.0f - std::clamp(tailDist, 0.0f, 1.0f)) * 8.0f;
                currentCoeff = mLevelRel / tailStab;
            }
            currentGains[i] += (scaledTargetG - currentGains[i]) * currentCoeff;

            outSum += b[i] * currentGains[i];
        }

        if (listenNoise) {
            return FluidMath::flush_denormal(s0 - outSum);
        }

        return FluidMath::flush_denormal(outSum);
    }

private:
    float mSampleRate = 48000.0f;
    float mHpF = 0.0f;
    float mLpF_cut = 0.0f;
    float mLevelAtt = 0.0f;
    float mLevelRel = 0.0f;
    float mGainAtt  = 0.0f;
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
// Stage 1A: Mechanical Soundboard/Membrane Inertia & Pneumatic Cushion Cell
// Ported from CyberAcousticEngine::AcousticInertialCell + Pneumatic Vent Sag
// ============================================================================
class DrumInertialCell {
private:
    float x_prev = 0.0f;
    float momentum = 0.0f;
    float sag_envelope = 0.0f;
    float sample_rate = 48000.0f;

public:
    void init(float sr) noexcept {
        sample_rate = sr > 8000.0f ? sr : 48000.0f;
        reset();
    }

    void reset() noexcept {
        x_prev = 0.0f;
        momentum = 0.0f;
        sag_envelope = 0.0f;
    }

    float getChamberPressure() const noexcept {
        return std::clamp(1.0f - sag_envelope * 0.45f, 0.35f, 1.0f);
    }

    inline float process(float in_sample, float vent_choke, float wood_density, float stick_snap) noexcept {
        // Step 1: Bounded input to prevent non-linear overflow
        float x = FluidMath::fast_tanh(in_sample);

        // Step 2: Instantaneous acoustic velocity (stick / hand attack derivative)
        float v_str = x - x_prev;
        x_prev = x;

        // Step 3: Wood / Membrane mass viscosity adaptation
        float eta_top = std::clamp(0.72f + 0.20f * wood_density, 0.60f, 0.95f);
        momentum = momentum * eta_top + v_str * (1.0f - eta_top);
        momentum = std::clamp(momentum, -1.0f, 1.0f);

        // Step 4: Pneumatic air-vent compressive compliance (adiabatic air spring sag)
        float hit_energy = std::abs(x) * (1.0f + 2.5f * vent_choke);
        float c_vent = (hit_energy > sag_envelope) ? 0.0045f : 0.00035f;
        sag_envelope += c_vent * (hit_energy - sag_envelope);
        float comp_factor = 1.0f - std::clamp(sag_envelope * 0.65f * vent_choke, 0.0f, 0.35f);

        // Step 5: Asymmetric wood/membrane grain transfer (Rule 32 bounded)
        float u = FluidMath::fast_tanh((x - 0.35f * v_str * (1.0f - eta_top)) * comp_factor);
        float y_wood = 0.0f;
        float bloom = 0.18f * wood_density;

        if (u >= 0.0f) {
            y_wood = u - bloom * u * u * (1.0f - u);
        } else {
            float au = std::abs(u);
            y_wood = u + bloom * (au * au) * (1.0f - au);
        }

        // Step 6: Re-inject mechanical inertia + Stick/Hand Snap articulation
        float snap_wave = FluidMath::fast_tanh(momentum * 2.4f) * (stick_snap - 0.5f) * 0.45f;
        float wet = FluidMath::fast_tanh(y_wood + FluidMath::fast_tanh(momentum) * 0.30f * vent_choke + snap_wave);
        float out = in_sample * (1.0f - vent_choke * 0.85f) + wet * (vent_choke * 0.85f);
        return FluidMath::flush_denormal(out);
    }
};

// ============================================================================
// Stage 1B: Fluid Air & Close-Mic Radiation Engine (From CyberAcousticEngine)
// Uses per-sample smoothed Hermite fractional delay so changing Depth/Size
// NEVER produces thumps, clicks, or "farting" noises!
// ============================================================================
class DrumAirMicDSP {
private:
    static constexpr int kMaxAirDelay = 1024;
    float bodyWaveDelayLine[kMaxAirDelay] = {0.0f};
    float holeWaveDelayLine[kMaxAirDelay] = {0.0f};
    int writeIdx = 0;

    float prevInputPressure = 0.0f;
    float prevSurfaceVelocity = 0.0f;
    float airSheenState = 0.0f;
    float helmholtzLowpass = 0.0f;
    float proximitySubState = 0.0f;

    // Smoothed fractional delay pointers (prevents step-change thumps!)
    float smoothPlateDelay = 16.0f;
    float smoothHoleDelay  = 48.0f;

    // All-Pass Acoustic Diffraction States
    float diffX1 = 0.0f, diffY1 = 0.0f;
    float diffX2 = 0.0f, diffY2 = 0.0f;

    float sampleRate = 48000.0f;

    static inline float readDelayHermite(const float* buffer, int bufSize, int writePos, float delay) noexcept {
        float rPos = static_cast<float>(writePos) - delay;
        while (rPos < 0.0f) rPos += static_cast<float>(bufSize);
        while (rPos >= static_cast<float>(bufSize)) rPos -= static_cast<float>(bufSize);
        int i1 = static_cast<int>(rPos);
        float frac = rPos - static_cast<float>(i1);
        int mask = bufSize - 1;
        int i0 = (i1 - 1 + bufSize) & mask;
        int i2 = (i1 + 1) & mask;
        int i3 = (i1 + 2) & mask;
        i1 &= mask;

        float y0 = buffer[i0], y1 = buffer[i1], y2 = buffer[i2], y3 = buffer[i3];
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
        std::fill(std::begin(bodyWaveDelayLine), std::end(bodyWaveDelayLine), 0.0f);
        std::fill(std::begin(holeWaveDelayLine), std::end(holeWaveDelayLine), 0.0f);
        writeIdx = 0;
        prevInputPressure = 0.0f;
        prevSurfaceVelocity = 0.0f;
        airSheenState = 0.0f;
        helmholtzLowpass = 0.0f;
        proximitySubState = 0.0f;
        smoothPlateDelay = 0.0004f * sampleRate;
        smoothHoleDelay  = 0.0011f * sampleRate;
        diffX1 = diffY1 = diffX2 = diffY2 = 0.0f;
    }

    inline float processSample(float rawMic, float bodyResonance,
                               float openness = 0.75f,
                               float micAirSheen = 0.55f,
                               float closeProximity = 0.70f) noexcept
    {
        if (openness <= 0.001f && micAirSheen <= 0.001f && closeProximity <= 0.001f) {
            return bodyResonance;
        }

        // 1. Extract Soundhole / Port Helmholtz Piston (< 200 Hz air pump)
        helmholtzLowpass += 0.025f * (bodyResonance - helmholtzLowpass);

        bodyWaveDelayLine[writeIdx] = bodyResonance;
        holeWaveDelayLine[writeIdx] = helmholtzLowpass;

        // 2. Smoothly ramped Tri-Point Wavefront Geometry (Zero step jumps!)
        const float distanceScale = 1.0f - (closeProximity * 0.45f);
        const float targetPlateDelay = std::clamp((0.00028f + openness * 0.00035f) * distanceScale * sampleRate, 2.0f, static_cast<float>(kMaxAirDelay - 8));
        const float targetHoleDelay  = std::clamp((0.00085f + openness * 0.00075f) * distanceScale * sampleRate, 4.0f, static_cast<float>(kMaxAirDelay - 8));

        // 25ms per-sample smoothing prevents any thumping when Shell Depth / Openness changes
        smoothPlateDelay += 0.0008f * (targetPlateDelay - smoothPlateDelay);
        smoothHoleDelay  += 0.0008f * (targetHoleDelay  - smoothHoleDelay);

        const float delayedPlateWave = readDelayHermite(bodyWaveDelayLine, kMaxAirDelay, writeIdx, smoothPlateDelay);
        const float delayedHoleWave  = readDelayHermite(holeWaveDelayLine, kMaxAirDelay, writeIdx, smoothHoleDelay);

        writeIdx = (writeIdx + 1) & (kMaxAirDelay - 1);

        // 3. Free-Air Particle Acceleration (Condenser Mic Top-End Sheen)
        const float surfaceVelocity = rawMic - prevInputPressure;
        prevInputPressure = rawMic;

        const float airAcceleration = surfaceVelocity - prevSurfaceVelocity;
        prevSurfaceVelocity = surfaceVelocity;

        airSheenState = (airSheenState * 0.45f) + (airAcceleration * 0.55f);
        const float condenserAirWave = FluidMath::fast_tanh(airSheenState * 2.0f) * micAirSheen * 0.45f;

        // 4. Close-Mic Capsule Proximity Effect (Pressure-Gradient Low-End Weight)
        proximitySubState += 0.012f * (delayedPlateWave - proximitySubState);
        const float proximityWarmth = proximitySubState * closeProximity * 0.60f;

        // 5. Exterior Acoustic Interference (Un-boxes the drum/cajon naturally)
        const float openAirInterference = (delayedPlateWave * 0.85f)
                                        - (delayedHoleWave * 0.45f * openness)
                                        + proximityWarmth;

        float exteriorField = (bodyResonance * (1.0f - openness * 0.60f))
                            + (openAirInterference * openness)
                            + condenserAirWave;

        // 6. Dual-Stage All-Pass Boundary Diffraction (3D Shell Wrap)
        const float g1 = 0.35f * openness;
        const float g2 = -0.22f * openness;

        const float ap1 = -g1 * exteriorField + diffX1 + g1 * diffY1;
        diffX1 = exteriorField;
        diffY1 = ap1;

        const float ap2 = -g2 * ap1 + diffX2 + g2 * diffY2;
        diffX2 = ap1;
        diffY2 = ap2;

        return FluidMath::fast_tanh(ap2 * 1.10f);
    }
};

// ============================================================================
// Stage 1C: 2-DOF Coupled Fluid Drum & Cajon Cavity (From FluidAcousticCavity)
// Passive, unconditionally stable Cytomic SVF resonators for:
//   - Helmholtz Air Port / Shell Cavity Mode (f_A0)
//   - Batter Head / Tapa Plate Mode (f_T1)
//   - Input-Gated Snare Wire & Cajon String Exciter (ZERO constant white noise!)
// ============================================================================
class FluidDrumShellDSP {
private:
    DrumInertialCell inertialCell;
    DrumAirMicDSP    airMic;

    // Cytomic SVF states for Boxy Mud De-Resonator
    float s1_box = 0.0f, s2_box = 0.0f;

    // 2-DOF Cytomic SVF states: Batter/Tapa (t) and Helmholtz Air Port (a)
    float s1_t = 0.0f, s2_t = 0.0f;
    float s1_a = 0.0f, s2_a = 0.0f;
    float eddy_state = 0.0f;

    // Per-sample smoothed geometry parameters (100% eliminates thumps/farts on knob moves!)
    float smoothF_A0   = 65.0f;
    float smoothF_T1   = 135.0f;
    float smoothBoxHz  = 220.0f;
    float smoothDepth  = 0.75f;
    bool  initialized  = false;

    // Transient envelope for natural membrane pitch-bloom & gated snare wires
    float hitEnvelope  = 0.0f;
    float wireEnvelope = 0.0f;

    // Cytomic SVF states for metallic snare wire / cajon string shaping (2.8 kHz BP + 1.5 kHz HP)
    float s1_wire = 0.0f, s2_wire = 0.0f;
    uint32_t rngState = 2463534242u;

    float sampleRate = 48000.0f;

    inline float fastWhiteNoise() noexcept {
        rngState ^= rngState << 13;
        rngState ^= rngState >> 17;
        rngState ^= rngState << 5;
        return static_cast<float>(static_cast<int32_t>(rngState)) * 4.65661287e-10f;
    }

public:
    void prepare(float sr) noexcept {
        sampleRate = sr > 8000.0f ? sr : 48000.0f;
        inertialCell.init(sampleRate);
        airMic.init(sampleRate);
        reset();
    }

    void reset() noexcept {
        inertialCell.reset();
        airMic.reset();
        s1_box = s2_box = 0.0f;
        s1_t = s2_t = s1_a = s2_a = 0.0f;
        eddy_state = 0.0f;
        hitEnvelope = 0.0f;
        wireEnvelope = 0.0f;
        s1_wire = s2_wire = 0.0f;
        initialized = false;
    }

    float getChamberPressure() const noexcept { return inertialCell.getChamberPressure(); }
    float getHeadDisplacement() const noexcept { return FluidMath::fast_tanh((s2_t + s2_a) * 2.0f); }
    float getWireActivity() const noexcept { return wireEnvelope; }

    inline float processSample(float liveMicInput,
                               float boxyCutHz,
                               float baseA0Hz,
                               float baseDepthScalar,
                               float customTuningHz,
                               float customDepthScalar,
                               float ventChokeAmount,
                               float woodViscosity,
                               float headDamping,
                               float snareWireBuzz,
                               float stickSnap,
                               float deBoxIntensity,
                               float morphAmount,
                               float sympatheticKitBleed = 0.0f) noexcept
    {
        // 1. Acoustic-Sim Style Size & Depth Geometry Calculation
        // Altering either Tuning (Size) or Shell Depth shifts both the Helmholtz Air Mode (A0)
        // and the Batter/Tapa Mode (T1) together, just like FluidAcousticCavity!
        const float refHz = std::max(25.0f, baseA0Hz);
        const float userHz = (customTuningHz > 20.0f) ? customTuningHz : refHz;
        const float tuningRatio = std::clamp(userHz / refHz, 0.45f, 2.20f);

        const float refDepth = std::clamp(baseDepthScalar, 0.20f, 1.20f);
        const float userDepth = (customDepthScalar > 0.05f) ? customDepthScalar : refDepth;
        const float depthRatio = std::clamp(userDepth / refDepth, 0.45f, 2.00f);

        // Deeper shell = larger air cavity volume V = pi*r^2*h -> lowers Helmholtz f_A0 by 1/sqrt(depthRatio)
        const float target_A0 = std::clamp((baseA0Hz * tuningRatio) / std::sqrt(depthRatio), 28.0f, 450.0f);

        // Coupled Top Plate / Batter Head Mode T1 sits ~1.85x above A0 and scales with size & depth
        const float plateRatio = 1.75f + 0.30f * (1.0f - std::clamp(userDepth, 0.2f, 1.0f));
        const float target_T1 = std::clamp((baseA0Hz * plateRatio * std::pow(tuningRatio, 0.88f)) / std::sqrt(depthRatio * 0.85f), 55.0f, 950.0f);
        const float target_Box = std::clamp(boxyCutHz, 80.0f, 800.0f);
        const float target_DepthBlend = std::clamp(userDepth * 0.85f, 0.15f, 1.15f);

        if (!initialized) {
            smoothF_A0  = target_A0;
            smoothF_T1  = target_T1;
            smoothBoxHz = target_Box;
            smoothDepth = target_DepthBlend;
            initialized = true;
        } else {
            // Smooth per-sample one-pole ramping (~20ms) prevents ANY thump or farting on knob changes!
            smoothF_A0  += 0.0010f * (target_A0 - smoothF_A0);
            smoothF_T1  += 0.0010f * (target_T1 - smoothF_T1);
            smoothBoxHz += 0.0010f * (target_Box - smoothBoxHz);
            smoothDepth += 0.0010f * (target_DepthBlend - smoothDepth);
        }

        // 2. Stage 1A: Mechanical Soundboard/Membrane Inertia & Pneumatic Vent Cushion
        const float cushionedInput = inertialCell.process(liveMicInput, ventChokeAmount, woodViscosity, stickSnap);

        // 3. Cardboard Boxiness De-Resonator (Cytomic SVF Band-Reject)
        const float w_box = std::clamp(3.14159265f * smoothBoxHz / sampleRate, 0.005f, 0.40f);
        const float g_box = std::tan(w_box);
        const float k_box = 0.65f; // Musical width for cardboard mud removal
        const float hp_box = (cushionedInput - (k_box + g_box) * s1_box - s2_box) / (1.0f + g_box * (k_box + g_box));
        const float bp_box = g_box * hp_box + s1_box;
        s1_box = g_box * hp_box + bp_box;
        const float lp_box = g_box * bp_box + s2_box;
        s2_box = g_box * bp_box + lp_box;

        // Normalize bandpass gain (k_box * bp_box has unity peak gain)
        const float deBoxed = cushionedInput - (bp_box * k_box * deBoxIntensity * morphAmount * 0.80f);

        // 4. Track Input Hit Envelope (for subtle membrane pitch-bloom & strict snare wire gating)
        const float absIn = std::abs(deBoxed) + std::abs(sympatheticKitBleed) * 0.30f;
        const float envCoeff = (absIn > hitEnvelope) ? 0.08f : 0.0015f;
        hitEnvelope += envCoeff * (absIn - hitEnvelope);
        hitEnvelope = FluidMath::flush_denormal(hitEnvelope);

        // Subtle dynamic membrane pitch-glide on hard hits (+0% at rest, up to +6% on heavy strikes)
        const float pitchBloom = 1.0f + std::min(hitEnvelope * 0.14f * ventChokeAmount, 0.065f);

        // 5. 2-DOF Coupled Fluid Cavity (Exact Cytomic SVF topology from CyberAcousticEngine)
        // Strictly passive & unconditionally stable -> NEVER self-oscillates or generates a sine wave!
        const float f_A0_inst = std::clamp(smoothF_A0 * pitchBloom, 25.0f, sampleRate * 0.42f);
        const float f_T1_inst = std::clamp(smoothF_T1 * pitchBloom, 45.0f, sampleRate * 0.44f);

        // Q is damped (3.5 to 8.5) so drum notes sing with woody punch and decay naturally
        const float Q_a = 4.5f + (1.0f - headDamping) * 4.5f;
        const float Q_t = 3.5f + (1.0f - headDamping) * 3.5f;

        const float g_a = std::tan(3.14159265f * f_A0_inst / sampleRate);
        const float k_a = 1.0f / Q_a;

        const float g_t = std::tan(3.14159265f * f_T1_inst / sampleRate);
        const float k_t = 1.0f / Q_t;

        // Bounded corner eddy turbulence (Rule 32 compliant)
        const float bx = FluidMath::fast_tanh(deBoxed);
        const float tau_vortex = (bx * bx * bx - 0.35f * bx) * woodViscosity;
        eddy_state = 0.80f * eddy_state + 0.20f * tau_vortex;
        const float input_with_eddies = deBoxed + 0.22f * eddy_state;

        // Solve T1 Batter / Tapa Mode
        const float hp_t = (input_with_eddies - (k_t + g_t) * s1_t - s2_t) / (1.0f + g_t * (k_t + g_t));
        const float bp_t = g_t * hp_t + s1_t;
        s1_t = FluidMath::flush_denormal(g_t * hp_t + bp_t);
        const float lp_t = g_t * bp_t + s2_t;
        s2_t = FluidMath::flush_denormal(g_t * bp_t + lp_t);

        // Cross-couple Batter displacement into Helmholtz Air Cavity Mode (A0)
        // Scaling by k_t keeps excitation bounded regardless of Q_t
        const float norm_bp_t = bp_t * k_t;
        const float air_excitation = input_with_eddies * 0.40f + norm_bp_t * 0.75f;
        const float hp_a = (air_excitation - (k_a + g_a) * s1_a - s2_a) / (1.0f + g_a * (k_a + g_a));
        const float bp_a = g_a * hp_a + s1_a;
        s1_a = FluidMath::flush_denormal(g_a * hp_a + bp_a);
        const float lp_a = g_a * bp_a + s2_a;
        s2_a = FluidMath::flush_denormal(g_a * bp_a + lp_a);

        const float norm_bp_a = bp_a * k_a;

        // Acoustically coupled output (Helmholtz Air + Batter Plate - Boxy Dip Anti-Resonance)
        const float airPumpWeight = 0.85f + 0.55f * smoothDepth;
        const float coupled_cavity = norm_bp_t * 0.85f
                                   + norm_bp_a * airPumpWeight
                                   - (lp_t - lp_a) * 0.22f;

        const float bodyResonance = deBoxed * (1.0f - smoothDepth * 0.45f)
                                  + coupled_cavity * smoothDepth * 1.45f;

        // 6. Stage 1B: Fluid Air & Close-Mic Radiation Engine
        const float openness  = std::clamp(0.45f + smoothDepth * 0.40f, 0.20f, 0.95f);
        const float airSheen  = std::clamp((1.0f - headDamping) * 0.65f + (stickSnap - 0.5f) * 0.35f, 0.10f, 0.90f);
        const float proximity = std::clamp(0.40f + smoothDepth * 0.45f, 0.20f, 0.95f);

        const float radiatedDrum = airMic.processSample(deBoxed, bodyResonance, openness, airSheen, proximity);

        // 7. INPUT-GATED SNARE WIRE & CAJON STRING EXCITER
        // Strictly gated to actual drum hits -> 100% SILENT (0.0f) when no hit is occurring!
        float wireSizzle = 0.0f;
        if (snareWireBuzz > 0.001f) {
            // Fast attack (1.5ms), natural wire-settle release (~65ms)
            const float wireDrive = std::abs(norm_bp_t) * 0.9f + std::abs(deBoxed) * 0.7f + std::abs(sympatheticKitBleed) * 0.35f;
            const float wAtt = (wireDrive > wireEnvelope) ? 0.06f : 0.0009f;
            wireEnvelope += wAtt * (wireDrive - wireEnvelope);
            wireEnvelope = FluidMath::flush_denormal(wireEnvelope);

            // Hard noise-floor gate below -50 dBFS (0.003f): guarantees zero background hiss!
            const float gateThresh = 0.003f;
            if (wireEnvelope > gateThresh) {
                float gateOpen = std::clamp((wireEnvelope - gateThresh) * 35.0f, 0.0f, 1.0f);
                gateOpen *= gateOpen; // Smooth quadratic knee

                // Shape white noise through Cytomic SVF Bandpass/Highpass (3.4 kHz metallic wire band)
                const float rawNoise = fastWhiteNoise();
                const float g_w = std::tan(3.14159265f * 3400.0f / sampleRate);
                const float k_w = 0.85f;
                const float hp_w = (rawNoise - (k_w + g_w) * s1_wire - s2_wire) / (1.0f + g_w * (k_w + g_w));
                const float bp_w = g_w * hp_w + s1_wire;
                s1_wire = g_w * hp_w + bp_w;
                const float lp_w = g_w * bp_w + s2_wire;
                s2_wire = g_w * bp_w + lp_w;

                const float metallicNoise = (bp_w * 0.70f + hp_w * 0.30f);
                // Modulate wire rattle with the actual vibrating membrane wave so it breathes with the head
                const float headMod = 0.35f + 0.65f * std::min(1.0f, std::abs(coupled_cavity) * 3.5f);
                const float envGain = std::min(wireEnvelope * 2.5f, 1.0f) * gateOpen * headMod;

                wireSizzle = FluidMath::fast_tanh(metallicNoise * envGain * 2.2f) * snareWireBuzz * 0.32f;
            } else {
                s1_wire = FluidMath::flush_denormal(s1_wire * 0.95f);
                s2_wire = FluidMath::flush_denormal(s2_wire * 0.95f);
            }
        } else {
            wireEnvelope = 0.0f;
        }

        const float fullShellSignal = FluidMath::fast_tanh((radiatedDrum + wireSizzle) * 1.05f);
        return (liveMicInput * (1.0f - morphAmount)) + (fullShellSignal * morphAmount);
    }
};

// ============================================================================
// Stage 1D: Fluid Bronze Cymbal & Plate Dispersion DSP
// Models flexural wave dispersion in B20/B8 bronze alloy plates,
// passive Cytomic SVF bell vs bow modal resonance, and air viscosity shimmer
// ============================================================================
class FluidCymbalDSP {
private:
    float apX[4] = {0.0f}, apY[4] = {0.0f};

    float s1_bell = 0.0f, s2_bell = 0.0f;
    float s1_bow  = 0.0f, s2_bow  = 0.0f;

    float smoothBellF = 2200.0f;
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
        s1_bell = s2_bell = 0.0f;
        s1_bow  = s2_bow  = 0.0f;
        washState = 0.0f;
        harshDetectEnv = 0.0f;
    }

    inline float processSample(float in, float bellFreq = 2200.0f, float bowFreq = 650.0f,
                               float shimmer = 0.70f, float bronzeViscosity = 0.45f) noexcept
    {
        smoothBellF += 0.001f * (std::clamp(bellFreq, 600.0f, 5000.0f) - smoothBellF);

        // 1. Flexural Wave Dispersion: high frequencies travel faster in thin metal plates
        float s = in;
        const float g = 0.62f;
        for (int i = 0; i < 4; ++i) {
            float d = -g * s + apX[i] + g * apY[i];
            apX[i] = s;
            apY[i] = d;
            s = d;
        }

        // 2. Unconditionally Stable Cytomic SVF Bell & Bow Resonators
        const float g_bell = std::tan(std::clamp(3.14159265f * smoothBellF / sampleRate, 0.01f, 0.42f));
        const float k_bell = 0.20f;
        const float hp_bell = (in - (k_bell + g_bell) * s1_bell - s2_bell) / (1.0f + g_bell * (k_bell + g_bell));
        const float bp_bell = g_bell * hp_bell + s1_bell;
        s1_bell = FluidMath::flush_denormal(g_bell * hp_bell + bp_bell);
        const float lp_bell = g_bell * bp_bell + s2_bell;
        s2_bell = FluidMath::flush_denormal(g_bell * bp_bell + lp_bell);

        const float g_bow = std::tan(std::clamp(3.14159265f * bowFreq / sampleRate, 0.01f, 0.40f));
        const float k_bow = 0.35f;
        const float hp_bow = (in - (k_bow + g_bow) * s1_bow - s2_bow) / (1.0f + g_bow * (k_bow + g_bow));
        const float bp_bow = g_bow * hp_bow + s1_bow;
        s1_bow = FluidMath::flush_denormal(g_bow * hp_bow + bp_bow);
        const float lp_bow = g_bow * bp_bow + s2_bow;
        s2_bow = FluidMath::flush_denormal(g_bow * bp_bow + lp_bow);

        // 3. Dynamic Harsh Sizzle Sieve (Tames 5-8 kHz trashiness, opens 12-18 kHz air)
        const float highEnergy = std::abs(in - lp_bow);
        harshDetectEnv += (highEnergy - harshDetectEnv) * 0.005f;
        const float harshDamp = std::clamp(1.0f - harshDetectEnv * 0.85f * bronzeViscosity, 0.25f, 1.0f);

        // 4. Free-Air Viscosity Shimmer Wash
        const float washAlpha = std::clamp(0.40f + shimmer * 0.45f, 0.1f, 0.95f);
        washState += (s - washState) * washAlpha;

        const float combined = (in * 0.35f)
                             + (s * harshDamp * 0.45f)
                             + (bp_bell * k_bell * 0.35f * shimmer)
                             + (bp_bow * k_bow * 0.40f)
                             + (washState * 0.35f * shimmer);

        return FluidMath::fast_tanh(combined * 1.10f);
    }
};

// ============================================================================
// Stage 2: Live Acoustic Drum Kit Morpher (Zero-Sample Physical Re-Shelling)
// De-resonates boxy room/shell mud & injects coupled 2-DOF acoustic drum cavities
// ============================================================================
struct DrumShellProfile {
    const char* name;
    float originalBoxyCutHz;   // Frequency of the real drum's cardboard mud to strip
    float targetFundamentalHz; // Helmholtz air / shell fundamental tuning (Hz)
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
    float sampleRate = 48000.0f;

public:
    void prepare(float sr) noexcept {
        sampleRate = sr > 8000.0f ? sr : 48000.0f;
        virtualShell.prepare(sampleRate);
        virtualCymbal.prepare(sampleRate);
    }

    void reset() noexcept {
        virtualShell.reset();
        virtualCymbal.reset();
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
                               float stickSnap = 0.65f,
                               float sympatheticBleed = 0.0f) noexcept 
    {
        if (profile.isCymbal) {
            const float bellF = customTuningHz > 500.0f ? customTuningHz : profile.targetFundamentalHz;
            const float bowF = 550.0f;
            const float shimmer = customVentChoke >= 0.0f ? customVentChoke : profile.ventChokeAmount;
            const float visc = profile.woodViscosity;

            float cymbalOut = virtualCymbal.processSample(liveMicInput, bellF, bowF, shimmer, visc);
            return (liveMicInput * (1.0f - morphAmount)) + (cymbalOut * morphAmount);
        }

        const float ventChoke = customVentChoke >= 0.0f ? customVentChoke : profile.ventChokeAmount;
        const float wireBuzz  = customWireBuzz >= 0.0f  ? customWireBuzz  : profile.snareWireBuzz;

        return virtualShell.processSample(
            liveMicInput,
            profile.originalBoxyCutHz,
            profile.targetFundamentalHz,
            profile.shellDepthScalar,
            customTuningHz,
            customDepth,
            ventChoke,
            profile.woodViscosity,
            profile.headDamping,
            wireBuzz,
            stickSnap,
            deBoxIntensity,
            morphAmount,
            sympatheticBleed
        );
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
        smoothF[0] = 80.0f;    smoothG[0] = 0.0f;
        smoothF[1] = 320.0f;   smoothG[1] = 0.0f;
        smoothF[2] = 3500.0f;  smoothG[2] = 0.0f;
        smoothF[3] = 10500.0f; smoothG[3] = 0.0f;
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
    float smoothF[4] = {80.0f, 320.0f, 3500.0f, 10500.0f};
    float smoothG[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    float lastF[4] = {-999.0f}, lastG[4] = {-999.0f};

    inline float processFilter(int idx, float in, float freq, float gainDb, float Q, FilterType type) {
        smoothF[idx] += 0.002f * (freq - smoothF[idx]);
        smoothG[idx] += 0.002f * (gainDb - smoothG[idx]);
        if (std::abs(smoothF[idx] - lastF[idx]) > 0.15f || std::abs(smoothG[idx] - lastG[idx]) > 0.02f) {
            lastF[idx] = smoothF[idx];
            lastG[idx] = smoothG[idx];
            computeCoefficients(idx, smoothF[idx], smoothG[idx], Q, type);
        }

        float out = b0[idx] * in + s1[idx];
        s1[idx] = FluidMath::flush_denormal(b1[idx] * in - a1[idx] * out + s2[idx]);
        s2[idx] = FluidMath::flush_denormal(b2[idx] * in - a2[idx] * out);
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
// With per-sample smoothed Size & Decay to prevent thumping on knob changes
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

    float smoothRevSize = 1.0f;
    float smoothRevDecay = 0.35f;

    float lfoPhase1 = 0.17f;
    float lfoPhase2 = 0.63f;

    float sampleRate = 48000.0f;

    inline float readHermite(const float* buf, int len, float pos) const noexcept {
        while (pos < 0.0f) pos += static_cast<float>(len);
        while (pos >= static_cast<float>(len)) pos -= static_cast<float>(len);
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
        smoothRevSize = 1.0f;
        smoothRevDecay = 0.35f;
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

        // Smooth reverb size & decay per sample to prevent any thumping on knob changes
        smoothRevSize  += 0.0005f * (std::clamp(size, 0.4f, 1.8f) - smoothRevSize);
        smoothRevDecay += 0.0010f * (std::clamp(decayNorm, 0.05f, 0.98f) - smoothRevDecay);

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
            buf[idx] = FluidMath::flush_denormal(in + g * out);
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
        const float modOffset1 = FluidMath::fast_sin((lfoPhase1 - 0.5f) * 6.2831853f) * lfoDepth;
        const float modOffset2 = FluidMath::fast_sin((lfoPhase2 - 0.5f) * 6.2831853f) * lfoDepth;

        const float maxDecay = (algorithm == 1) ? 0.92f : (algorithm == 2 ? 0.90f : 0.78f);
        const float loopFeedback = std::clamp(smoothRevDecay * maxDecay, 0.10f, 0.93f);

        const float dampCut = std::clamp(highCutHz, 1200.0f, 16000.0f);
        const float dampCoeff = 1.0f - std::exp(-2.0f * 3.14159265f * dampCut / sampleRate);

        float tankAIn = diffSig + tankB2Buf[tankB2Idx] * loopFeedback;
        tankAIn = FluidMath::fast_tanh(tankAIn * 0.95f);

        float readPosA1 = static_cast<float>(tankA1Idx) - (kTankA1Len * 0.5f * smoothRevSize + modOffset1);
        float tankA1Out = readHermite(tankA1Buf, kTankA1Len, readPosA1);
        tankA1Buf[tankA1Idx] = tankAIn;
        tankA1Idx = (tankA1Idx + 1) % kTankA1Len;

        dampStateA = FluidMath::flush_denormal(dampStateA + (tankA1Out - dampStateA) * dampCoeff);
        float tankA2In = dampStateA;

        float tankA2Out = tankA2Buf[tankA2Idx];
        tankA2Buf[tankA2Idx] = tankA2In;
        tankA2Idx = (tankA2Idx + 1) % kTankA2Len;

        float tankBIn = diffSig + tankA2Out * loopFeedback;
        tankBIn = FluidMath::fast_tanh(tankBIn * 0.95f);

        float readPosB1 = static_cast<float>(tankB1Idx) - (kTankB1Len * 0.5f * smoothRevSize + modOffset2);
        float tankB1Out = readHermite(tankB1Buf, kTankB1Len, readPosB1);
        tankB1Buf[tankB1Idx] = tankBIn;
        tankB1Idx = (tankB1Idx + 1) % kTankB1Len;

        dampStateB = FluidMath::flush_denormal(dampStateB + (tankB1Out - dampStateB) * dampCoeff);
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
                                 float sympatheticBleed = 0.0f,
                                 float stickSnap = 0.65f)
    {
        float cleaned = denoiseListen ? denoiser.process(inSample, denoiseAmt, denoiseThresh, denoiseShield, true)
                                      : denoiser.process(inSample, denoiseAmt, denoiseThresh, denoiseShield, false);
        if (denoiseListen) return cleaned;

        const auto& profile = DrumKitProfiles::kAllProfiles[std::clamp(profileIndex, 0, DrumKitProfiles::kTotalProfiles - 1)];
        float reShelled = morpher.processSample(cleaned, profile, morphAmount, deBoxIntensity,
                                                customTuningHz, customDepth, customVentChoke, customWireBuzz,
                                                stickSnap, sympatheticBleed);

        float equalized = eq.process(reShelled, eqLow, eqLowMidF, eqLowMidG, eqHighMidF, eqHighMidG, eqHigh);

        return equalized;
    }
};

} // namespace CyberDrum
