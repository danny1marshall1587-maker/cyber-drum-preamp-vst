# Cyber Drum Preamp v1.0.0
### Fluid Space Drum Shell & Percussion Engine: Pneumatic Shells, Non-Linear Membrane Heads & Live Acoustic Re-Shelling
**Free & Open Source Standalone App (with ASIO), VST3, AU, and LV2 Plugin for Windows, macOS, and Linux.**

---

## Overview

Drums and percussion instruments are physical acoustic systems consisting of an elastic membrane (or wooden tapa plate in the case of a cajon) coupled to a sealed fluid cavity (air column) with a pneumatic pressure-relief venthole.

In both live sound (such as tracking with multi-channel audio interfaces like the Behringer UMC1820) and electronic/studio production, drums suffer from three severe physical limitations that conventional static EQ and dynamic compression cannot solve:
1. **Close-Mic "Cardboard / Paper" Syndrome**: Clipping a dynamic microphone $1\text{ inch}$ from a snare, tom rim, or cajon soundhole captures an aggressive, paper-thin transient spike followed by congested midrange boxiness, while missing the chest-punching air displacement felt in the room.
2. **Digital Voltage Spikes on Drum Transients**: Hard rimshots, cajon slaps, and kick beater impacts create microsecond-fast spikes ($+15\text{ to }+20\text{ dB}$ above RMS body level). Digital limiters or fast VCA compressors squash the life and crack out of the drum, while clipping an ADC or bus produces harsh digital distortion.
3. **"Machine-Gun" Sample Replacement & Trigger Delays**: Pre-recorded sample triggers require $2\text{ to }5\text{ ms}$ of threshold detection latency, phase-smearing with overhead mics and erasing the human player's ghost-note dynamics, brush work, and rim articulation.

**Cyber Drum Preamp v1.0.0** models the non-linear fluid dynamics of drum shells and acoustic air propagation in real time with **$0.0\text{ ms}$ trigger latency**:

---

## Key Physical Modeling Features

### 1. Pneumatic Air-Vent Compression (`chamberPressure`)
- When a drumhead or cajon tapa is struck hard, it displaces a large air volume downward through the shell. Because the shell only has a small venthole ($0.5''$ on drums or $4''\text{--}5''$ on kicks/cajons), internal air cannot escape instantaneously.
- The resulting pressure spike forms an **adiabatic air spring** that naturally resists and cushions the striking force.
- Soft ghost notes breeze through completely uncompressed, while heavy rimshots and beater smashes choke the air vent, naturally absorbing the harsh voltage spike into a thick, punchy analog thud without digital clipping or loss of transient crack.

### 2. Non-Linear Membrane Tension Pitch-Glide
- Striking a membrane or wooden tapa hard stretches it beyond resting equilibrium. Because transverse wave propagation speed is governed by $c_m = \sqrt{T/\sigma}$, the drum's fundamental tuning temporarily bends sharp by $+15\text{ to }+42\text{ cents}$ on initial impact before decaying smoothly back down to resting pitch.
- Dynamically bends and blooms on hard hits, making dead or boxy drums and cajons sing with organic acoustic weight.

### 3. Batter-to-Resonant Head Fluid Propagation Delay & Snare Wire Sizzle
- Models the physical transit time ($0.3\text{ to }2.2\text{ ms}$) of the acoustic shockwave travelling from the batter head through the shell column to excite the resonant head.
- Bottom head velocity drives non-linear high-pass filtered metallic snare wire rattle and cajon guitar-string buzz.
- **Sympathetic Kit Bleed (`sympatheticBleed`)**: Low-frequency air waves from kick and toms cross-excite the virtual snare wires and bottom head, gluing dry close-mics into a coherent, breathing drum kit in a single physical room.

### 4. Live Acoustic Drum "Re-Shelling" (`FluidDrumKitMorpherDSP`)
- **Step 1: Cardboard Mud De-Resonator**: Isolates and phase-cancels the boxy $160\text{--}350\text{ Hz}$ standing-wave mud of the physical room or cheap drum shell prior to fluid injection, leaving 100% of stick transient, ghost-note nuances, and hi-hat/cymbal phase intact.
- **Step 2: Virtual Shell Injection**: Excites the virtual fluid shell with custom tuning ($25\text{--}350\text{ Hz}$), shell depth ($0.20\times\text{--}1.50\times$), and pneumatic vent choke.
- **Step 3: Shell Material Viscosities**:
  - **Vintage Maple / Mahogany**: High fluid viscosity ($\mu \approx 0.78\text{--}0.82$), warm even harmonics, round buttery low-mid bloom.
  - **Studio Birch**: Lower viscosity ($\mu \approx 0.42\text{--}0.45$), fast transient velocity transfer, focused modern attack.
  - **Bell Brass / Steel**: Ultra-stiff boundary reflection, explosive metallic rimshot crack.
  - **Peruvian & Flamenco Cajon**: Beech/birch box cavity with deep $65\text{--}72\text{ Hz}$ soundhole air resonance and internal guitar-string snare sizzle.

---

## Factory Drum & Percussion Profiles

| Profile Name | Category | Boxy Cut | Tuning | Depth | Vent Choke | Viscosity | Snare Wires |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| **Bonham 26" Giant Maple** | Kick | 165 Hz | 48 Hz | 0.95 | 70% | 0.75 | 0% |
| **70s Dead Tea-Towel Kick** | Kick | 195 Hz | 58 Hz | 0.50 | 85% | 0.80 | 0% |
| **Modern Punchy Birch 22"** | Kick | 220 Hz | 52 Hz | 0.80 | 90% | 0.42 | 0% |
| **Sub-Piston 808 Cannon** | Kick | 180 Hz | 42 Hz | 0.90 | 95% | 0.70 | 0% |
| **Bell Brass Black Beauty** | Snare | 290 Hz | 185 Hz | 0.60 | 72% | 0.38 | 50% |
| **70s Fat Wallet Snare** | Snare | 340 Hz | 145 Hz | 0.70 | 88% | 0.82 | 55% |
| **Tight Funk 13" Piccolo** | Snare | 260 Hz | 225 Hz | 0.25 | 65% | 0.45 | 40% |
| **Studio Maple 5.5x14 Snare** | Snare | 310 Hz | 195 Hz | 0.50 | 75% | 0.78 | 48% |
| **12" & 16" Singing Toms** | Tom | 240 Hz | 130 Hz | 0.55 | 70% | 0.70 | 0% |
| **18" Bonham Low Tom** | Tom | 150 Hz | 68 Hz | 0.95 | 85% | 0.78 | 0% |
| **Flamenco Deep Cajon** | Cajon | 190 Hz | 65 Hz | 0.85 | 75% | 0.80 | 40% |
| **Acoustic Gig Live Cajon** | Cajon | 220 Hz | 72 Hz | 0.75 | 70% | 0.75 | 45% |
| **Afro-Cuban Conga / Quinto**| Perc | 280 Hz | 190 Hz | 0.70 | 65% | 0.70 | 0% |
| **African Djembe Deep Bass** | Perc | 160 Hz | 55 Hz | 0.90 | 80% | 0.75 | 0% |
| **Stereo Drum Bus / Glue** | Bus | 180 Hz | 55 Hz | 0.70 | 70% | 0.70 | 25% |

---

## Studio Signal Flow

```
[Raw Mic / Piezo Trigger]
           │
           ▼
[Stage 0: 10-Band Drum Cyber-Denoiser PRO] (0ms Latency, Non-Zero Floor 0.08, Harmonic Shield)
           │
           ▼
[Stage 1: De-Box Cardboard Mud Subtraction] (2nd-Order SVF Notch 160-350 Hz)
           │
           ▼
[Stage 2: Fluid Space Drum Shell Engine]
   ├── Stick/Beater Transient Velocity Cushion (Viscosity filter)
   ├── Pneumatic Air-Vent Compression (Adiabatic air spring)
   ├── Top-to-Bottom Air Column Propagation (0.3 - 2.2ms)
   ├── Non-Linear Membrane Pitch-Glide (+15..+42 cents)
   └── Bottom Head Velocity -> Snare Wire Sizzle / Sympathetic Kit Bleed
           │
           ▼
[Stage 3: Studio 4-Band Sculpting EQ] (Sub 80Hz, Box Cut 180-900Hz, Crack 1.5-6.5kHz, Sizzle 10.5kHz)
           │
           ▼
[Stage 4: World-Class Drum Reverb Space] (Schroeder-Dattorro Dual-Tank: Booth, Hall, Plate)
           │
           ▼
[Stage 5: Stereo Width & Output Gain]
           │
           ▼
[Level-Matched Master Bypass] (0.25x - 4.0x RMS Parity Tracking)
```

---

## Installation & Platforms

- **Windows x64**: Standalone Application (`CyberDrumPreamp.exe` with Steinberg ASIO and WASAPI Exclusive support) + VST3 + LV2.
- **macOS Universal Binary (Apple Silicon M1–M5 & Intel x86_64)**: Standalone Application (`CyberDrumPreamp.app`) + AU (`.component`) + VST3 + LV2.
- **Linux x64**: Standalone Application + VST3 + LV2.
