# Cyber Drum Preamp v1.2.0
### Fluid Space Drum Shell, Cymbal & Percussion Engine: 2-DOF Coupled Cytomic SVF Cavity, Pneumatic Shells & Bronze Shimmer
**Free & Open Source Standalone App (with ASIO), VST3, AU, and LV2 Plugin for Windows, macOS, and Linux.**

---

## What's New in v1.2.0 (Acoustic-Sim Coupled Cavity Architecture)

- **2-DOF Coupled Cytomic SVF Acoustic Cavity (`FluidDrumShellDSP`)**:
  - Replaced the Euler mass-spring loop with the unconditionally stable **2-Degree-of-Freedom Coupled Cytomic State-Variable Filter (SVF) Cavity** architecture from `Cyber Acoustic Preamp` (`f_A0` Helmholtz Air Port / Cavity Mode + `f_T1` Batter Head / Wooden Tapa Mode).
  - **Zero Self-Oscillation**: Passive trapezoidal SVF integration (`g / (1 + g*(g + k))`) guarantees 100% silence when no audio is playing—eliminating any continuous sine-wave drone.
- **Acoustic-Sim Style Size & Depth Tuning**:
  - Adjusting **SIZE / TUNE** or **SHELL DEPTH** scales both the Helmholtz air port mode ($f_{A0}$) and the batter/tapa structural mode ($f_{T1}$) together ($f_{A0} \propto \text{tuningRatio} / \sqrt{\text{depthRatio}}$), while **SHELL DEPTH** also adjusts the physical balance between deep Helmholtz air sub-thump and top-head wood bark—just like changing body size and depth in `Cyber Acoustic Preamp`.
  - Selecting any profile in the **30 Drum & Percussion Profiles** dropdown automatically updates the **SIZE / TUNE**, **SHELL DEPTH**, **VENT CHOKE**, and **SNARE WIRES** knobs to that instrument's natural tuning and geometry.
- **Glitch-Free Per-Sample Smoothed Geometry & Hermite Air-Mic Interpolation (`DrumAirMicDSP`)**:
  - All filter cutoffs (`smoothF_A0`, `smoothF_T1`, `smoothBoxHz`, `smoothDepth`), air-column delay pointers (`smoothPlateDelay`, `smoothHoleDelay` with 4-point Hermite cubic interpolation), and reverb tank dimensions (`smoothRevSize`) are ramped per sample (`~20 ms` one-pole) so sweeping **SIZE / TUNE** or **SHELL DEPTH** while playing produces zero thumping, clicks, or "farting" artifacts.
- **Strict Input-Gated & Drum-Modulated Snare Wires**:
  - Snare wire and cajon guitar-string sizzle is strictly gated to real input hits above `-50 dBFS` (`wireEnvelope > 0.003f`), shaped through a $3.4\text{ kHz}$ Cytomic SVF bandpass/highpass network, and modulated by the resonant cavity wave—dropping to complete silence (`0.0f`) between hits.
- **30 World-Class Drum, Cymbal & Percussion Profiles**:
  - **8 Kicks**: Bonham 26", 70s Tea-Towel, Modern Birch 22", Sub-Piston Cannon 24", Metal Click Sub-Drop, Vintage 60s Downbeat 20", Heavy Funk Tight Birch 20", Trap/Electronic 808 Sub Boom.
  - **8 Snares**: 6.5x14 Bell Brass Black Beauty, 70s Fat Dead Wallet, Tight Funk 13" Piccolo, 5.5x14 Studio Maple, Ludwig Supraphonic 400 Aluminum, 80s Power Gated Rock Snare, Nashville Solid Maple Wood-Hoop, Reggae High Soprano 10".
  - **4 Toms**: 10" Fusion High, 12" Classic Studio, 16" Deep Floor, 18" Bonham Low Thunder.
  - **5 Cymbals & Bronze Plates**: Dark Turkish Ride 22" (B20 Bronze), Vintage 60s A-Style 14" Hi-Hats, Fast Studio Bronze Crash 18", Heavy Rock Bell Ride 21", Thin White-Bronze China/Trash 16".
  - **5 Cajons & World Percussion**: Flamenco Deep Acoustic Cajon, Acoustic Gig Live Cajon, Afro-Cuban Conga / Quinto Tumbao, African Djembe Deep Bass, Latin Timbale & Cowbell Rim.
- **Fluid Cymbal & Bronze Shimmer Engine (`FluidCymbalDSP`)**:
  - Models flexural wave propagation velocity in thin B20/B8 bronze alloy plates ($v \propto \sqrt{\omega}$) using a 4-stage allpass dispersion network.
  - Dual modal Bell & Bow resonators for crystalline stick articulation without harsh trashiness.
  - Dynamic harsh sizzle filtering and free-air viscosity shimmer wash.
- **3D Lathed Bronze Cymbal & Shell Visualizer**:
  - Automatically switches between 3D cylindrical drum shells, Peruvian cajon boxes, African djembe goblet forms, Cuban conga barrels, and lathed bronze cymbals with audio-reactive edge flexure ripples.
- **20 Signature Factory Presets**:
  - Instant one-click studio setups for kicks, snares, toms, cymbals, cajons, and full stereo drum bus glue.

---

## Physical Modeling Features

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

### 4. Bronze Cymbal Plate Dispersion (`FluidCymbalDSP`)
- Cymbals do not have a closed air cavity or resonant head; they are thin elastic circular plates where flexural wave propagation velocity increases with frequency ($v \propto \sqrt{\omega}$). High frequencies disperse faster than low frequencies.
- The 4-stage allpass network ($g = 0.62$) reproduces this natural phase dispersion, transforming harsh stick hits into silky bronze wash.

---

## 30 Factory Drum, Cymbal & Percussion Profiles

| Profile Name | Category | Boxy Cut | Tuning | Depth | Vent Choke | Viscosity | Snare / Bell | Cymbal |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| **26" Bonham Maple Kick** | Kick | 165 Hz | 48 Hz | 0.95 | 70% | 0.75 | 0% | No |
| **70s Tea-Towel Dry Kick** | Kick | 195 Hz | 58 Hz | 0.50 | 85% | 0.80 | 0% | No |
| **Modern Punchy Birch 22"** | Kick | 220 Hz | 52 Hz | 0.80 | 90% | 0.42 | 0% | No |
| **Sub-Piston Cannon 24"** | Kick | 180 Hz | 42 Hz | 0.90 | 95% | 0.70 | 0% | No |
| **Metal Click Sub-Drop** | Kick | 240 Hz | 45 Hz | 0.85 | 92% | 0.35 | 0% | No |
| **Vintage 60s 20" Downbeat** | Kick | 175 Hz | 62 Hz | 0.60 | 65% | 0.78 | 0% | No |
| **Heavy Funk Tight 20" Birch** | Kick | 210 Hz | 55 Hz | 0.65 | 88% | 0.45 | 0% | No |
| **Trap / Electronic 808 Sub** | Kick | 150 Hz | 38 Hz | 0.95 | 98% | 0.80 | 0% | No |
| **6.5x14 Bell Brass Black Beauty** | Snare | 290 Hz | 185 Hz | 0.60 | 72% | 0.38 | 50% | No |
| **70s Fat Dead Wallet Snare** | Snare | 340 Hz | 145 Hz | 0.70 | 88% | 0.82 | 55% | No |
| **Tight Funk Birch 13" Piccolo** | Snare | 260 Hz | 225 Hz | 0.25 | 65% | 0.45 | 40% | No |
| **5.5x14 Studio Maple All-Round** | Snare | 310 Hz | 195 Hz | 0.50 | 75% | 0.78 | 48% | No |
| **Ludwig Supraphonic 400 Aluminum** | Snare | 320 Hz | 210 Hz | 0.50 | 70% | 0.32 | 52% | No |
| **80s Power Gated Rock Snare** | Snare | 280 Hz | 170 Hz | 0.75 | 85% | 0.50 | 65% | No |
| **Nashville Solid Maple Wood-Hoop** | Snare | 330 Hz | 165 Hz | 0.65 | 80% | 0.85 | 45% | No |
| **Reggae High Soprano 10" Snare** | Snare | 250 Hz | 260 Hz | 0.20 | 60% | 0.40 | 35% | No |
| **10" Fusion High Tom** | Tom | 260 Hz | 160 Hz | 0.45 | 65% | 0.68 | 0% | No |
| **12" Classic Studio Tom** | Tom | 240 Hz | 130 Hz | 0.55 | 70% | 0.70 | 0% | No |
| **16" Deep Floor Tom** | Tom | 175 Hz | 85 Hz | 0.80 | 80% | 0.75 | 0% | No |
| **18" Bonham Low Thunder Tom** | Tom | 150 Hz | 68 Hz | 0.95 | 85% | 0.78 | 0% | No |
| **Dark Turkish Ride 22" (B20 Bronze)** | Cymbal | 450 Hz | 2200 Hz | 0.85 | 75% | 0.55 | 40% | Yes |
| **Vintage 60s A-Style 14" Hi-Hats** | Cymbal | 350 Hz | 2600 Hz | 0.55 | 80% | 0.40 | 50% | Yes |
| **Fast Studio Bronze Crash 18"** | Cymbal | 500 Hz | 1800 Hz | 0.70 | 85% | 0.45 | 35% | Yes |
| **Heavy Rock Bell Ride 21"** | Cymbal | 400 Hz | 2400 Hz | 0.80 | 70% | 0.35 | 65% | Yes |
| **Thin White-Bronze China 16"** | Cymbal | 600 Hz | 2000 Hz | 0.60 | 90% | 0.30 | 25% | Yes |
| **Flamenco Deep Acoustic Cajon** | Cajon | 190 Hz | 65 Hz | 0.85 | 75% | 0.80 | 40% | No |
| **Acoustic Gig Live Cajon** | Cajon | 220 Hz | 72 Hz | 0.75 | 70% | 0.75 | 45% | No |
| **Afro-Cuban Conga / Quinto** | Perc | 280 Hz | 190 Hz | 0.70 | 65% | 0.70 | 0% | No |
| **African Djembe Deep Bass** | Perc | 160 Hz | 55 Hz | 0.90 | 80% | 0.75 | 0% | No |
| **Latin Timbale & Cowbell Rim** | Perc | 350 Hz | 240 Hz | 0.30 | 60% | 0.35 | 0% | No |

---

## Signal Flow Architecture

```
[Raw Mic / Piezo Trigger]
           │
           ▼
[Stage 0: 10-Band Drum Cyber-Denoiser PRO] (0ms Latency, Non-Zero Floor 0.08, Harmonic Shield)
           │
           ▼
[Branching: Drum Shell vs Bronze Cymbal]
   ├── [Drum Shell Branch]:
   │      ├── De-Box Mud Cancellation (2nd-order SVF Notch 160-350 Hz)
   │      ├── Stick/Beater Transient Velocity Cushion (Viscosity filter)
   │      ├── Pneumatic Air-Vent Compression (Adiabatic air spring)
   │      ├── Top-to-Bottom Air Column Propagation (0.3 - 2.2ms)
   │      ├── Non-Linear Membrane Pitch-Glide (+15..+42 cents)
   │      └── Bottom Head Velocity -> Snare Wire Sizzle & Sympathetic Bleed
   └── [Bronze Cymbal Branch]:
          ├── 4-Stage Flexural Dispersion Allpass Cascade
          ├── Dual Modal Bell & Bow Resonators
          ├── Dynamic Harsh Sizzle Sieve
          └── Free-Air Viscosity Shimmer Wash
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

## Pre-Built Binaries

Pre-compiled binary releases for **Windows (x64)**, **macOS (Universal Apple Silicon & Intel)**, and **Linux (x64)** are available on the GitHub Releases page:
👉 **[Download Latest Release v1.2.0](https://github.com/danny1marshall1587-maker/cyber-drum-preamp-vst/releases/tag/v1.2.0)**

---

## Building from Source

```bash
git clone https://github.com/danny1marshall1587-maker/cyber-drum-preamp-vst.git
cd cyber-drum-preamp-vst
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```
