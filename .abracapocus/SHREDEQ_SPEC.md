# ShredEQ — Product and DSP Architecture Specification

**Product:** Adaptive parametric, dynamic, and multiband equalizer for electric guitar and bass
**Formats:** VST3, Standalone (CLAP and LV2 deferred)
**Stack:** C++20, JUCE, no mandatory third-party DSP framework
**Processing:** Causal, minimum-phase, real-time safe, zero-latency default, fully local

---

## 1. Product Definition

ShredEQ is a guitar-focused equalizer and spectral diagnostic system. It answers a practical question: *what is making this guitar tone muddy, harsh, fizzy, weak, or difficult to mix — and what is the least destructive correction?*

It combines conventional parametric EQ, per-band dynamic EQ, four-zone multiband processing, guitar-specific transient and resonance control, external-sidechain masking analysis, mid/side EQ, and time-aggregated visual diagnostics.

ShredEQ is optimized for distorted guitar spectra, amp-model and cabinet-IR resonances, palm-muted low-frequency bursts, dense upper-mid harmonic structures, double-tracked rhythm guitars, sustained high-gain leads, low-tuned and extended-range instruments, and bass/guitar frequency collisions.

It works as both a precise manual equalizer and a diagnostic instrument that helps users discover what is actually happening in the signal.

### 1.1 Core Philosophy

**Diagnose before correcting.** Instantaneous FFT traces fluctuate too rapidly to distinguish persistent cabinet resonances from note-dependent harmonics, transient pick energy from sustained fizz, or occasional low-frequency overload from normal variation. The visual system aggregates information over time and exposes statistical behavior — what frequencies are consistently present, what appears only during transients, what is unusually narrow and persistent, where the signal spends its dynamic range.

**Use dynamic correction for dynamic problems.** A static cut should not be the default solution when the unwanted energy occurs only on palm mutes, during pick attacks, on certain notes, or when the bass is active. Static EQ remains fully capable, but dynamic correction must be easy.

**Preserve attack and instrument identity.** High-gain guitar depends on pick articulation, palm-mute impact, harmonic density, upper-mid projection, and note sustain. Aggressive broadband compression or linear-phase pre-ringing damages these. ShredEQ defaults to causal minimum-phase filters with zero processing latency.

**Every automatic action remains visible.** When ShredEQ identifies or corrects a problem, the user sees the target frequency, bandwidth, maximum gain change, detector source, actual gain reduction, and confidence. Automatic findings become normal editable controls.

**No cloud dependency.** All analysis and processing runs locally. No cloud accounts, remote inference, subscription services, or externally hosted models. Diagnostic intelligence is deterministic signal analysis.

### 1.2 Suite Overlap Policy

Some capabilities overlap with other Shred Arsenal products where they are intrinsic to equalization:

- ShredBoost already tightens pre-amplifier input, but ShredEQ provides a resonant high-pass filter (Tight Cut)
- ChugGate will handle gating, but ShredEQ performs frequency-selective palm-mute stabilization
- ShredWidth creates and manages width, but ShredEQ provides mid/side equalization
- Aural Maximizer Pro and ShredBoost provide nonlinear coloration — ShredEQ does NOT include saturation

### 1.3 Deliberate Non-Goals

ShredEQ does not include: linear-phase mastering modes, surround/Atmos routing, stereo delay/Haas/decorrelation, noise gate, amp modeling, cabinet convolution, saturation/distortion, mastering limiting, inter-instance management, cloud mixing, or piano-roll features.

---

## 2. Signal Architecture

### 2.1 High-Level Signal Flow

```
Input
  → Input Conditioning
  → Tight Cut (resonant HPF + punch bell)
  → Parametric Stage A
  → Guitar Intelligence Layer
      ├─ Chug Stabilizer
      ├─ Note/Harmonic Follower
      └─ Resonance Suppressor
  → Four-Zone Multiband Engine
  → Parametric Stage B
  → Output Gain
  → Analysis Taps
  → Output
```

Parametric bands are assigned to either Stage A or Stage B, giving the user processing-order control without arbitrary modular routing.

**Stage A** — corrections that influence downstream detection: high-pass filtering, broad tonal shaping, detector cleanup, large low-mid cuts, pre-dynamics shaping.

**Stage B** — final corrections: presence balance, broad shelves, output contour, compensation after dynamic processing, final M/S EQ.

### 2.2 Analyzer Taps

Analysis taps exist at: raw input, post-Stage A, post-adaptive processing, final output, and external sidechain. The user selects which taps appear in each visual view.

### 2.3 Channel Layouts

Required:
- Mono → mono
- Mono → stereo (where host permits)
- Stereo → stereo
- Stereo main with mono or stereo external sidechain

DSP supports both float and double-precision host processing paths.

---

## 3. Parametric EQ Engine

### 3.1 Band Count and Capabilities

**12 fully independent parametric bands.** Each supports:

- Enable/bypass
- Stage A or Stage B assignment
- Frequency, gain, Q/bandwidth
- Filter topology and slope
- Static or dynamic behavior
- Detector source selection
- Stereo placement (linked, unlinked, L, R, mid, side)
- Solo/listen and removed-signal audition
- Optional note or resonance tracking

### 3.2 Filter Types

Each band can be: bell, notch, low shelf, high shelf, low-pass, high-pass, band-pass, tilt, resonant high-pass, resonant low-pass, or flat dynamic band (no static gain, applies gain only when detector activates).

### 3.3 Parameter Ranges

| Parameter          | Range                         |
|--------------------|-------------------------------|
| Frequency          | 15 Hz – Nyquist-limited 24 kHz |
| Gain               | −24 dB to +24 dB             |
| Q                  | 0.10 to 36.0                 |
| Filter slope       | 6, 12, 18, 24, 36, 48 dB/oct |
| Dynamic range      | −24 dB to +18 dB             |
| Threshold          | −90 dBFS to 0 dBFS           |
| Ratio              | 1:1 to 10:1                  |
| Attack             | 0.1 ms to 300 ms             |
| Hold               | 0 ms to 500 ms               |
| Release            | 5 ms to 3000 ms              |
| Knee               | 0 dB to 18 dB                |
| Lookahead          | Off, 0.5, 1, 2, or 5 ms      |
| Detector bandwidth | 0.15 to 4 octaves            |

Default processing path uses no lookahead and reports zero samples of latency.

### 3.4 Per-Band Stereo Placement

Each band processes: stereo linked, stereo unlinked, left, right, mid, or side.

M/S encoding uses energy-normalized matrix:

```
M[n] = (L[n] + R[n]) / sqrt(2)
S[n] = (L[n] - R[n]) / sqrt(2)
```

Decoding:

```
L'[n] = (M'[n] + S'[n]) / sqrt(2)
R'[n] = (M'[n] - S'[n]) / sqrt(2)
```

ShredEQ performs EQ in M/S but does not generate synthetic stereo information.

### 3.5 Filter Implementation

Normalized biquad transfer function:

```
H(z) = (b0 + b1*z^-1 + b2*z^-2) / (1 + a1*z^-1 + a2*z^-2)
```

Cascade response in dB:

```
G_total(w) = 20 * log10(|prod_i H_i(e^jw)|)
```

**RBJ-style peaking filter coefficients:**

```
A     = 10^(G/40)
w0    = 2*pi * f0/fs
alpha = sin(w0) / (2*Q)

b0 = 1 + alpha*A
b1 = -2*cos(w0)
b2 = 1 - alpha*A
a0 = 1 + alpha/A
a1 = -2*cos(w0)
a2 = 1 - alpha/A
```

All coefficients divided by a0.

Static bands use optimized biquads. Bands whose frequency or gain is continuously modulated use a topology-preserving transform (TPT) state-variable structure or another modulation-stable design to prevent zipper noise and coefficient instability.

### 3.6 Analog-Matched Minimum-Phase Mode

Default engine is causal minimum-phase IIR with zero added processing latency. Frequency prewarping:

```
W0 = 2*fs * tan(pi * f0/fs)
```

High-frequency shelf and peaking behavior uses compensated coefficient generation so requested gains remain accurate near Nyquist. The implementation does not claim arbitrary analog-phase reconstruction — the phase display shows the actual phase of the implemented causal filter network.

---

## 4. Per-Band Dynamic EQ

Every parametric band can operate as: static EQ, downward dynamic EQ, upward dynamic EQ, compression band, expansion band, transient-only band, sustain-only band, or externally sidechained band.

### 4.1 Detector Sources

Selectable per band: full-band internal, band-limited internal, left, right, mid, side, external sidechain, input-minus-output difference, transient component, sustain component. A detector bandpass isolates the region surrounding the EQ band.

### 4.2 Envelope Follower

For detector magnitude x_d[n]:

```
e[n] = alpha_a * e[n-1] + (1-alpha_a) * |x_d[n]|   when |x_d[n]| > e[n-1]
e[n] = alpha_r * e[n-1] + (1-alpha_r) * |x_d[n]|   when |x_d[n]| <= e[n-1]

alpha_a = exp(-1 / (fs * tau_a))
alpha_r = exp(-1 / (fs * tau_r))

L[n] = 20 * log10(e[n] + epsilon)
```

### 4.3 Soft-Knee Gain Computation

Let u = L - T. For knee width K:

```
F_K(u) = 0                         when u <= -K/2
F_K(u) = (u + K/2)^2 / (2*K)      when -K/2 < u < K/2
F_K(u) = u                         when u >= K/2
```

Downward compression target:

```
g_target = -min(D_max, (1 - 1/R) * F_K(u))
```

For upward action, sign is reversed and capped by configured positive range. Gain modulation is smoothed independently from the level detector to avoid chatter.

### 4.4 Transient and Sustain Separation

Two envelopes: e_f[n] (fast attack/release) and e_s[n] (slower ballistics).

```
transient[n] = max(0, e_f[n] - k * e_s[n])
sustain[n]   = max(0, e_s[n] - lambda * transient[n])
```

Applications: reduce 3 kHz only during hard pick attacks, boost 1.5 kHz only on note attack, reduce 6 kHz during sustain without dulling the pick, increase body only after the transient passes.

### 4.5 Dynamic Visualization

Each dynamic band shows: static response curve, maximum dynamic range as translucent envelope, current moving response, actual gain-reduction history, threshold line, detector level, and whether the band is reacting to transient, sustain, or sidechain content.

---

## 5. Four-Zone Multiband Engine

### 5.1 Purpose

Handles problems too broad for a narrow dynamic EQ node but too frequency-specific for a broadband compressor: stabilizing low-tuned rhythm weight, controlling low-mid buildup, preserving or reducing pick articulation, reducing sustained fizz, expanding air only during open chords.

### 5.2 Zones

1. **Weight** (lows)
2. **Body** (low-mids)
3. **Bite** (upper-mids)
4. **Fizz/Air** (highs)

Zone names change appropriately when Bass profile is active.

### 5.3 Crossovers

Cascaded fourth-order Linkwitz-Riley crossovers. Each LR4 filter is two cascaded second-order Butterworth filters (Q = 1/sqrt(2)):

```
H_LR4_LP(z) = H_BW2_LP(z)^2
H_LR4_HP(z) = H_BW2_HP(z)^2
```

Split architecture uses a complementary serial tree so bypassed bands reconstruct the input with flat magnitude response. Crossover frequencies are smoothly interpolated and cannot cross.

| Crossover    | Default  | Range         |
|-------------|----------|---------------|
| Weight/Body | 140 Hz   | 40–500 Hz     |
| Body/Bite   | 900 Hz   | 150 Hz–3 kHz  |
| Bite/Fizz   | 4.5 kHz  | 1–12 kHz      |

### 5.4 Zone Controls

Each zone: gain, solo, mute, bypass, dynamic range, threshold, ratio, attack, hold, release, transient emphasis, sustain emphasis, stereo placement, detector source, auto threshold, upward or downward operation.

### 5.5 Multiband Display

Shows zone boundaries, zone energy distribution, current gain change as broad moving contour, pre/post energy percentile bands, soloed frequency region. Must not resemble four conventional compressor meters.

---

## 6. Tight Cut

### 6.1 Purpose

Controlled low-frequency shaping with separated concerns: low-frequency rejection, cutoff resonance, punch-band gain, and punch-band bandwidth. More controllable than an extremely high-Q high-pass filter.

### 6.2 Architecture

```
Input → High-Pass Filter → Linked Punch Bell → Output
```

Punch bell frequency: `f_p = r * f_c` where r ∈ [0.75, 1.5]. Linked mode moves the punch bell with cutoff. Unlinked allows independent positioning.

### 6.3 Controls

- Cutoff: 25–220 Hz
- Slope: 12, 18, 24, or 36 dB/oct
- HPF resonance: Q 0.5 to 2.5
- Punch frequency ratio
- Punch gain: 0 to +8 dB
- Punch Q: 0.35 to 3.0
- Pre/post placement
- Stereo, mid, or side routing
- Listen-to-removed-low-end mode

---

## 7. Chug Stabilizer

### 7.1 Purpose

Palm-muted notes create large temporary bursts between ~70–300 Hz. A static cut weakens open chords and sustained notes. Chug Stabilizer reduces only the excessive burst. It is not a gate — it does not silence signal between notes (that is ChugGate's role).

### 7.2 Detection Model

Evaluates: low-band energy, rate of energy increase, low-band dominance relative to midrange, transient strength. Let L_low be detector level in the target low band and L_mid be level in a broad midrange reference:

```
C[n] = w1 * max(0, L_low - T_low)
     + w2 * max(0, dL_low/dt - T_d)
     + w3 * max(0, L_low - L_mid - Delta)
```

The score drives a wide dynamic bell or low shelf.

### 7.3 Controls

- Target frequency: 60–320 Hz
- Bandwidth: 0.3–2.5 octaves
- Maximum reduction: 0–18 dB
- Threshold: automatic or manual
- Attack: 0.2–20 ms
- Hold: 0–80 ms
- Release: 15–350 ms
- Preserve Pick (detail preservation)
- Transient sensitivity
- Low/mid dominance sensitivity
- Static support gain
- Output listen modes

### 7.4 Detail Preservation

A parallel high-frequency transient detector prevents treating every pick attack as low-frequency overload. **Preserve Pick** reduces gain reduction when the broadband transient is strong but low-band dominance is normal.

### 7.5 Visual Feedback

Shows detected chug events, low-band energy, temporary cut curve, event duration, reduction history, and trigger source (raw energy, low-band dominance, or transient acceleration).

---

## 8. Resonance and Anti-Fizz System

### 8.1 Purpose

Identifies fixed or semi-fixed narrow spectral peaks from: cabinet IRs, microphones, speaker breakup, room coloration, resonant pickups, harsh post-distortion filters, accumulated upper-mid energy. Distinguishes these from normal moving musical harmonics.

### 8.2 Spectral Analysis

STFT frame with Hann window:

```
X_t[k] = sum_{n=0}^{N-1} x[n + t*H] * w[n] * exp(-j*2*pi*k*n/N)
S_t[k] = 20 * log10(|X_t[k]| + epsilon)
```

Local spectral baseline via octave- or ERB-domain smoothing:

```
B_t[k] = smooth_broad(S_t[k])
E_t[k] = S_t[k] - B_t[k]    // excess above local baseline
```

### 8.3 Persistence Analysis

```
P_t[k] = lambda * P_{t-1}[k] + (1-lambda) * indicator(E_t[k] > theta)
```

Candidate score combines: excess level, persistence, narrowness, perceptual weighting, frequency stability, independence from estimated note movement:

```
R_t[k] = E_t[k] * P_t[k] * N_t[k] * W[k] * F_t[k]
```

### 8.4 Musical-Harmonic Rejection

A strong spectral peak that moves correlated with the detected note or its harmonics should not automatically be classified as cabinet fizz. A resonance candidate receives lower confidence when f_candidate ≈ m * f0 for integer harmonic m, and when the candidate moves with f0. A fixed 4.2 kHz peak present while different notes play receives higher cab-resonance confidence than a peak following the third harmonic.

### 8.5 Suppressor Slots

**Six suppressor nodes** (expandable to eight in v1.1). Each:

- Center frequency: 150 Hz–16 kHz
- Q: 3–40
- Maximum depth: 0–18 dB
- Attack: 0.5–100 ms
- Release: 20–2000 ms
- Persistence requirement: 0–100%
- Static/dynamic mode
- Stereo placement
- Confidence score
- Freeze, solo, removed-signal audition

### 8.6 Scan Workflow

User selects **Scan**, plays audio for a configurable period. ShredEQ produces candidate cards:

- Persistent cabinet resonance at 4.35 kHz
- Intermittent fizz cluster at 6.1–7.0 kHz
- Box resonance at 480 Hz
- Note-linked peak at 1.2 kHz (lower confidence)

For each candidate the user can: audition, reject, create dynamic notch, create static notch, reduce depth, classify as intentional, add to exclusion list.

### 8.7 Removed-Signal Audition

```
y_removed[n] = x[n] - y_processed[n]
```

Exposes overprocessing immediately. Available for every adaptive module.

---

## 9. Note and Harmonic Follower

### 9.1 Purpose

An EQ node tracks the played fundamental or a selected harmonic, enabling frequency-relative processing: reduce excessive fundamental bloom, boost second harmonic for lead focus, follow a painful resonance associated with a specific harmonic, support low notes without a broad static boost.

### 9.2 Tracking Sources (v1.0)

**Audio Monophonic** — YIN or McLeod Pitch Method on a conditioned, downsampled signal:

```
d_t(tau) = sum_j (x_j - x_{j+tau})^2

d'_t(tau) = 1                                   when tau = 0
d'_t(tau) = d_t(tau) / ((1/tau) * sum d_t(j))   when tau > 0

f0 = fs / tau_0
```

Intended for single-note leads, bass lines, held low notes, monophonic riffs. Does not claim chord tracking.

**MIDI Note** — deterministic tracking from host MIDI:

```
f0 = 440 * 2^((m - 69) / 12)
```

User selects: lowest held note, highest held note, last note, or note-priority mode.

### 9.3 Target Frequency

```
f_target = f0 * h * 2^(c/1200)
```

Where h is the harmonic multiplier (1–8) and c is cents offset.

### 9.4 Log-Frequency Smoothing

```
ell[n] = beta * ell[n-1] + (1-beta) * ln(f_target)
f_smooth[n] = exp(ell[n])
```

Prevents unnatural movement at low frequencies and zipper noise.

### 9.5 Confidence Handling

When confidence falls below threshold, the band can: hold last valid frequency, return to a configured home frequency, fade gain toward zero, or temporarily bypass. Default: hold briefly then fade toward neutral.

### 9.6 Controls

Tracking source, harmonic 1–8, cents offset, Q, static gain, dynamic range, confidence threshold, smoothing, hold time, return time, min/max frequency, note priority, listen mode.

---

## 10. External-Sidechain Mix Lock-In

### 10.1 Purpose

Identifies and reduces masking between main input and external sidechain. Typical: ShredEQ on bass with rhythm guitar sidechain, ShredEQ on rhythm bus with bass sidechain, ShredEQ on lead with vocal sidechain.

### 10.2 Masking Metric

Signals accumulated into ERB- or octave-spaced bands. Let P_M(b,t) be main power and P_S(b,t) be sidechain power in band b:

```
Overlap(b,t)   = min(P_M, P_S) / (max(P_M, P_S) + eps)
Dominance(b,t) = P_S / (P_M + P_S + eps)
Masking(b,t)   = Overlap * Dominance * A_S(b,t)
```

Where A_S is a sidechain activity gate. Requested gain change:

```
G_carve(b,t) = -min(G_max, K * max(0, Masking(b,t) - T))
```

### 10.3 Processing Modes

Manual sidechain dynamic band, broad bass/guitar lock, automatic masking suggestions, transient-priority carve, sustained-density carve, mid-only carve, side-only carve.

### 10.4 Collision Map

Horizontal: frequency. Vertical: recent time. Intensity: masking score. Optional color: which source dominates. Shows where both sources repeatedly occupy the same region.

### 10.5 Product Boundary

v1.0 uses the host's external sidechain input. No private inter-plugin communication system.

---

## 11. Mid/Side Guitar-Bus Processing

### 11.1 Applications

- Side high-pass below 120–300 Hz
- Side bite shaping around 2–4 kHz
- Mid congestion reduction
- Center room for bass, kick, vocal, snare
- Stereo fizz correction
- Independent treatment of common vs. differential cabinet resonances

### 11.2 Mono Guard

Frequency-dependent L/R coherence analysis:

```
S_LR(f)  = E[X_L(f) * conj(X_R(f))]
rho(f)   = Re(S_LR(f)) / (sqrt(S_LL(f) * S_RR(f)) + eps)
```

Values near +1: mono-compatible. Near 0: weakly correlated. Near −1: high cancellation risk.

Mono Guard warns or limits excessive side boosts in unstable frequency regions. Does not secretly alter processing unless the user enables protection.

### 11.3 ShredWidth Boundary

ShredEQ supports M/S EQ, M/S dynamic EQ, M/S filtering, and mono compatibility analysis. It does NOT provide delay-based width, modulation, stereo generation, artificial doubling, or decorrelation. Those belong to ShredWidth and ShredDouble.

---

## 12. Visual Diagnostic System

The analyzer is a primary product subsystem, not decoration.

### 12.1 Instantaneous Spectrum

Standard FFT with: pre-EQ trace, post-EQ trace, sidechain trace, peak hold, smoothing, decay, adjustable FFT size, adjustable averaging. Can be disabled entirely.

### 12.2 Spectral Percentile Cloud (Core Diagnostic View)

For each log-frequency bin, ShredEQ stores recent spectral values and calculates 10th, 50th (median), and 90th percentiles. Optionally 99th.

Interpretation:
- Narrow percentile band → stable energy
- Wide band → dynamic or transient energy
- High median → persistent tonal emphasis
- High 90th but low median → occasional bursts
- Narrow persistent spike → resonance
- Broad temporary increase → musical event

This is far more useful for tone diagnosis than a rapidly moving FFT trace.

### 12.3 Spectral Dynamic Range Map

```
D(f) = Q_90(f) - Q_10(f)
```

Reveals: stable compressed fizz, highly variable palm-mute lows, transient pick regions, overly static/flattened areas, frequencies that disappear between notes.

### 12.4 Resonance Persistence Map

Scrolling frequency-vs-time view where intensity represents resonance excess and persistence rather than raw level. Cabinet fizz appears as stable vertical structures, moving notes as diagonal structures, palm mutes as short low-frequency events, hum as fixed narrow lines.

### 12.5 Headroom Histogram

Output amplitude distribution as histogram across dBFS bins. Includes: L/R distributions, mono-sum distribution, sample peak, estimated true peak, RMS, crest factor, clipped-sample count, time above configurable thresholds, headroom before and after EQ.

An EQ boost causing the distribution to pile up near 0 dBFS becomes obvious even when peak meters move too quickly.

### 12.6 Stereo Coherence View

Frequency-dependent: mid energy, side energy, correlation, negative-correlation risk, mono-sum loss, side low-frequency accumulation.

### 12.7 Note and Tuning Ruler

Optional overlay: note names, string fundamentals, tuning fundamentals, octave harmonics, fret-range guides, current detected note, current MIDI note. Does not force frequency parameters to musical notes.

### 12.8 Analyzer History Windows

Selectable: 2s, 5s, 10s, 30s, frozen capture, entire scan pass. History stops consuming CPU when disabled.

### 12.9 Diagnostic Labels

Contextual labels (not universal judgments):

| Region         | Typical interpretation                              |
|----------------|-----------------------------------------------------|
| 20–60 Hz       | Rumble, handling noise, unnecessary preamp energy    |
| 60–140 Hz      | Weight, low-string fundamental, palm-mute impact    |
| 120–300 Hz     | Boom, chug accumulation, low-mid body               |
| 250–650 Hz     | Boxiness, cabinet enclosure character                |
| 600 Hz–1.2 kHz | Honk, bark, note focus                              |
| 1–2.5 kHz      | Nasal, projection, articulation                     |
| 2–4.5 kHz      | Pick bite, aggression, vocal competition             |
| 3.5–7 kHz      | Harshness, speaker resonance, fizz                   |
| 6–12 kHz       | Fizz, noise, cabinet air                             |
| 12+ kHz        | Air, hiss, aliasing, mostly nonessential guitar      |

Regions shift with tuning, amp, cabinet, microphone, playing style. The product says "persistent narrow excess near 4.8 kHz" rather than "4.8 kHz is bad."

---

## 13. GUI

### 13.1 Window Behavior

- Default: ~1360×820
- Minimum usable: ~980×620
- Freely resizable, high-DPI aware
- Layout scaling 100%–200%
- Keyboard navigation and numerical entry
- Analyzer frame rate independent from audio processing

### 13.2 Global Header

Preset browser, profile selector, tuning selector, A/B switch, copy A→B, undo/redo, global bypass, gain-matched audition, analyzer enable, processing latency display, CPU indicator, help, plugin menu.

### 13.3 Main EQ Workspace

Primary graph: log frequency axis, gain axis (±24 dB), composite response, Stage A response, adaptive-layer response, Stage B response, pre/post spectra, percentile cloud, dynamic ranges, resonance candidates, note guides, sidechain collision regions, phase response (when enabled).

### 13.4 Band Nodes

Visual state includes: band number, filter type, stage, current gain, dynamic activity ring, M/S or channel routing glyph, tracking indicator, sidechain indicator, bypass state, selection state.

For dynamic nodes: center handle = static gain, outer range handle = max dynamic movement, current position = actual gain.

### 13.5 Bottom Inspector

Selecting a node opens a full-width inspector: frequency, gain, Q, type, slope, stage, routing, dynamic mode, threshold, range, attack, hold, release, knee, detector source, detector filter, tracking, solo, bypass. No essential parameter requires a hidden context menu.

### 13.6 Guitar Intelligence Drawer

Collapsible side panel: Tight Cut, Chug Stabilizer, Resonance Scan, Note Follower, Mix Lock-In. Each module has: enable, status, primary controls, deeper editor, audition, reset.

### 13.7 View Modes

**Sculpt** — main EQ editing workspace.
**Diagnose** — percentile cloud, resonance persistence, spectral dynamic range, diagnostic candidates.
**Dynamics** — four-zone engine, per-band detector behavior, sidechain collision map.

EQ curve remains visible in all views.

### 13.8 Interaction Design

- Double-click empty graph: create bell band
- Modifier + double-click: select filter type
- Horizontal drag: frequency
- Vertical drag: gain
- Mouse wheel/gesture: Q
- Modifier drag: fine adjustment
- Double-click parameter: numerical entry
- Drag range handle: dynamic range
- Headphone icon: solo band
- Delta icon: hear removed signal
- Delete key: remove selected band
- Undo/redo for all editing actions

### 13.9 Visual Appearance

Suite-consistent dark neutral background, restrained luminous analysis layers, high-contrast response curves, distinct colors for Stage A / adaptive / Stage B, color-blind-safe routing indicators, subtle depth, animation only where it communicates signal behavior, all meters and analyzer overlays individually disableable.

More analytical appearance than ShredBoost or ShredSpace — this is a measurement and correction tool.

---

## 14. Gain Matching and Comparison

### 14.1 A/B States

Two complete internal states. Instant switch with click-safe crossfade, copy A→B, reset either.

### 14.2 Gain-Matched Bypass

Rolling window measurement of input/output level difference:

```
Delta_L = L_out - L_in
G_match = -Delta_L
```

Uses filtered RMS or short-term perceptual level (not raw sample peak). Trim is slow-moving, bounded, frozen during comparison switching, not written to permanent output gain unless requested.

### 14.3 Delta Audition

Audition modes: full removed signal, selected band only, all dynamic reductions, resonance suppressor output, sidechain carve only, Tight Cut removal, Chug Stabilizer removal.

---

## 15. Profiles

Profiles change defaults, analyzer weighting, labels, and recommended ranges. They do not impose hidden processing.

### 15.1 Placement Profiles

- **Guitar DI / Pre-Amp** — Tight Cut, input control, midrange drive shaping
- **Post Amp / Cab** — resonance scanning, fizz persistence, boxiness
- **Rhythm Guitar** — palm-mute dynamics, low-mid stability, double-track analysis
- **Lead Guitar** — sustain fizz, harsh-note control, note follower, harmonicity
- **Stereo Guitar Bus** — M/S EQ, mono compatibility, center masking
- **Active Bass** — fundamental stability, transient clang, guitar sidechain masking
- **Distorted Bass** — clean low protection, upper distortion control, fizz reduction

### 15.2 Tuning Profiles

Factory tunings: E Standard, Eb, D, C#, C, B Standard; Drop D, C#, C, B, A; 7-string standard and Drop A; 8-string standard and Drop E; 4-string bass, 5-string bass; custom tuning.

User can specify: string count, open-note pitches, fret range, concert pitch reference, capo/transposition.

---

## 16. Factory Presets

Factory presets are useful starting points, not marketing demonstrations. Categories:

- **Pre-Amp** — tight input variants for 6/7/8 string, pick shaping, mid push, bass prep
- **Rhythm Post-Cab** — cleanup, palm-mute control, cab box removal, fizz suppression, low-tuned stability, double-track cleanup
- **Lead** — singing focus, smooth sustain, pick clarity, dynamic upper-mid control, harmonic follower lead
- **Stereo Bus** — mono-safe low end, side bite, mid congestion, double-track balance
- **Bass** — clang control, fundamental stability, distorted split, guitar/bass integration
- **Diagnostic** — find resonances, analyze palm mutes, analyze fizz, headroom audit, mono compatibility audit

Specific preset names and parameter values designed after DSP is testable with real guitar signals.

---

## 17. Parameter and State Model

### 17.1 Stable Parameter IDs

Stable IDs from first public release, allocated for all 12 bands regardless of active state:

```
band01.enabled
band01.stage
band01.type
band01.frequency
band01.gain
band01.q
band01.slope
band01.route
band01.dynamicMode
band01.dynamicRange
band01.threshold
band01.ratio
band01.attack
band01.hold
band01.release
band01.knee
band01.detectorSource
band01.detectorFrequency
band01.detectorQ
band01.trackingSource
band01.trackingHarmonic
```

IDs never depend on visible node order.

### 17.2 State Versioning

Saved state includes: schema version, all parameters, band ordering, A/B states, profiles, tuning, analysis settings, resonance exclusions, custom labels, UI layout, window size. State migration supports loading earlier presets after schema changes.

### 17.3 Host Automation

Automatable: all audio-processing controls, module enables, band parameters, output gain, dynamic thresholds/ranges, M/S assignments. Not automatable: analyzer history length, graph zoom, visual layer visibility, scan commands, window arrangement.

### 17.4 Undo/Redo

All user edits enter a unified undo history: node creation/deletion, movement, parameter edits, scan application, profile loading, A/B copying.

---

## 18. Real-Time Implementation Requirements

### 18.1 Audio-Thread Rules

The audio thread contains: **no heap allocation, no file I/O, no mutex waits, no GUI calls, no FFT planning, no optimizer execution, no state serialization, no dynamic container resizing, no logging, no network calls.**

Use `juce::ScopedNoDenormals` at the top of processBlock.

### 18.2 Parameter Smoothing

Use `juce::SmoothedValue` for all parameter reads in the audio thread:

- Frequency: smoothed in logarithmic space
- Gain: smoothed in decibels
- Q: bounded interpolation
- Crossover frequencies: smoothed
- Output gain: smoothed
- Dynamic range: smoothed

Topology changes use dual-filter crossfade or state-safe reset (~5–30 ms click-free transition).

### 18.3 Analyzer Pipeline

Audio copied into preallocated lock-free FIFOs (`juce::AbstractFifo`). Analyzer runs on background/message-adjacent worker thread.

Requirements:
- Skip frames when overloaded rather than block audio
- Stop expensive processing when hidden
- Reduce frame rate before reducing audio quality
- Fixed memory bounds
- No unbounded history

### 18.4 Pitch-Analysis Thread

Runs at lower control rate. Audio thread supplies conditioned frames through lock-free FIFO. Control thread returns: estimated frequency, confidence, timestamp, tracking state. Audio thread interpolates toward latest valid target.

### 18.5 Resonance Scans

Scan analysis runs outside the audio thread. Results applied through atomic state transfer or message-thread parameter transaction.

### 18.6 Numerical Safety

Every processing block guards against: NaN, infinity, unstable coefficients, invalid frequency above Nyquist, invalid Q, sample-rate changes, zero-length buffers, extreme automation, sidechain channel mismatches.

---

## 19. Latency

### 19.1 Default

```
0 samples
```

Covers: static EQ, dynamic EQ without lookahead, multiband, Chug Stabilizer, resonance suppression, M/S processing, note-followed EQ. Analyzer looks into past buffered data but does not delay the audible signal.

### 19.2 Optional Lookahead

Per-band or global: 0.5, 1, 2, or 5 ms. When enabled: plugin reports exact latency, UI displays value, host PDC updated. Live profiles default to lookahead off. No hidden latency.

---

## 20. CPU and Scalability

Must remain usable with multiple instances in a full guitar session.

- Disabled modules: near-zero processing
- Disabled analyzers: stop heavy work
- Static bands: optimized fixed-coefficient paths
- Dynamic/tracking bands: additional cost only when enabled
- FFT sizes and frame rates: visual-quality settings only
- Multiband: independently bypassable
- No oversampling required for linear EQ path

Analyzer quality levels:
- **Live** — smaller FFT, lower frame rate, shorter history
- **Studio** — balanced defaults
- **Analysis** — larger FFT, longer history, higher precision

These do not alter the underlying EQ response.

---

## 21. Testing

### 21.1 Filter Response

For each filter type: verify center frequency, gain, Q, slope, response near Nyquist, response at multiple sample rates, compare with analytical response. Target static magnitude accuracy: ±0.1 dB through operating range (documented exceptions near Nyquist).

### 21.2 Stability

Test: min/max frequency, min/max Q, max gain, rapid automation, sample-rate changes, block-size changes, topology switches, M/S switching, parameter randomization. Must produce no NaNs, infinities, unstable oscillation, unbounded output, or denormal stalls.

### 21.3 Dynamic EQ

Verify: attack/release timing, threshold, knee, range cap, upward/downward action, transient/sustain modes, sidechain mode, channel linking.

### 21.4 Multiband Reconstruction

With all zone gains at unity and dynamics inactive: flat magnitude response, no level jump enabling engine, click-free crossover motion.

### 21.5 Chug Stabilizer

Fixtures: isolated palm mutes, open chords, alternate-picked passages, bass transients, sustained low notes. Verify: palm-mute bursts trigger reduction, open chords not permanently thinned, recovery before next expected note, pick preservation reduces false triggering.

### 21.6 Resonance Detector

Fixtures: fixed sine resonances, moving harmonics, pink-noise peaks, cabinet IR resonances, held notes, fast scales, chords. Scanner identifies fixed narrow excess, avoids following musical harmonics, produces bounded recommendations, never modifies sound until explicitly applied.

### 21.7 Pitch Tracking

Test: guitar fundamentals, low bass notes, bends, vibrato, legato, rapid changes, silence, chords, low-confidence noise, MIDI note priority.

### 21.8 State

Verify: save/restore, A/B restoration, automation restoration, old-schema migration, profile persistence, UI-state persistence, host project reopen.

### 21.9 Host Validation

Required: Bitwig Studio (Linux), REAPER (Linux), REAPER (Windows), pluginval, standalone. Test: mono/stereo tracks, external sidechain routing, automation, preset loading, project save/reopen, offline render, sample-rate changes.

### 21.10 Audio Regression

Deterministic rendered fixtures for: static EQ, dynamic EQ, Chug Stabilizer, resonance suppression, M/S EQ, multiband, sidechain carve, bypass, state recall. Compared with floating-point tolerances.

---

## 22. Release Criteria

ShredEQ ships when:

- Standalone and VST3 build in Release mode (Linux and Windows)
- No known audio-thread allocations remain
- No parameter change produces zippering or clicks
- Bypass is click-free
- Reported latency is correct (zero-latency mode truly reports zero)
- External sidechain works in supported hosts
- Mono and stereo layouts behave correctly
- A/B comparison is reliable
- Gain matching does not pump or drift
- Analyzer disablement materially reduces CPU
- Analyzer load cannot interrupt audio
- All state restores after host project reopen
- Factory presets are gain-conscious and categorized
- Resonance recommendations are editable and bounded
- Removed-signal audition works for every adaptive module
- GUI usable at minimum size and high DPI
- No control inaccessible after resizing
- All meters and visualizations can be disabled
- No architecture depends on online services
- Validation scripts and release documentation complete

---

## 23. Implementation Phasing (abracapocus)

Suggested phase decomposition for plan materialization:

| Phase | Focus                                    | Key Deliverables                                          |
|-------|------------------------------------------|-----------------------------------------------------------|
| A     | Project scaffold + APVTS                 | CMakeLists, build scripts, parameter IDs, state engine    |
| B     | Parametric EQ core                       | Biquad implementations, all filter types, cascade display |
| C     | Dynamic EQ                               | Envelope followers, gain computation, detector filtering  |
| D     | Tight Cut + Chug Stabilizer              | Resonant HPF, punch bell, palm-mute detection/reduction   |
| E     | Multiband engine                         | LR4 crossovers, zone processing, zone dynamics            |
| F     | Resonance / Anti-Fizz                    | Spectral analysis, persistence, suppressor slots, scan    |
| G     | Sidechain + M/S                          | External sidechain, masking metric, M/S matrix, Mono Guard|
| H     | Note Follower                            | YIN pitch detection, MIDI, harmonic targeting, confidence |
| I     | Analyzer system                          | FFT, Percentile Cloud, Headroom Histogram, Resonance Map |
| J     | GUI                                      | LookAndFeel, workspace, nodes, inspector, drawers, views  |
| K     | Profiles, presets, A/B, gain matching    | All profiles, factory presets, comparison system          |
| L     | Validation + release                     | Test fixtures, validation scripts, release documentation  |

Each phase follows the established pattern: codex_cli for DSP/math, antigravity_cli for GUI/headers, pi_cli for remainder. C++ language adapter provides cmake/ctest verification inference.

---

## Appendix A: v1.1 Features (Deferred)

The following features are fully specified but deferred from v1.0 to reduce initial implementation scope while delivering a complete professional product:

### A.1 EQ Sketch

User draws a desired tonal contour over the graph. ShredEQ fits a constrained set of filters to the target curve using least-squares optimization with regularization penalizing band count, extreme Q, and extreme gain. Max 12 fitted bands. Fitting runs on background thread. Accepted bands become ordinary editable nodes.

### A.2 Reference Capture and Spectral Matching

Capture current input/output, sidechain, imported WAV/AIFF, or factory reference. Level-normalized comparison produces a bounded, smoothed target difference fitted with the EQ Sketch optimizer. Defaults to broad matching — exact spectral cloning is musically destructive.

### A.3 Spectral Peak Tracking Mode (Note Follower)

Follow a strong local peak within a constrained frequency range without labeling it as a true fundamental. Useful for distorted chords and moving resonances.

### A.4 Tuning-Aware Candidate Mode (Note Follower)

Snap candidate fundamentals to notes available in the selected tuning and fret range.

### A.5 Advanced Analyzer Views

- **Crest and Transient Map** — spectral crest comparing fast peak with typical energy
- **Harmonicity View** — marks expected harmonic locations when pitch confidence sufficient
- **Gain-Reduction History Surface** — frequency-vs-time map of applied gain per dynamic band

### A.6 Expanded Suppressor Slots

Increase from 6 to 8 suppressor nodes.

---

## 24. Product Identity

ShredEQ is a zero-latency guitar and bass equalizer combining professional parametric control, selective dynamics, broad multiband processing, guitar-specific resonance and palm-mute management, and statistical visual analysis that explains what the signal is doing over time.

Defining advantages:

1. **Spectral Percentile Cloud** — persistent, typical, and occasional frequency energy rather than only instantaneous trace
2. **Headroom Histogram** — level distribution, clipping risk, and crest behavior visible at a glance
3. **Resonance Persistence Analysis** — separates fixed cab fizz from musical harmonics
4. **Chug Stabilizer** — controls palm-mute bursts without thinning the performance
5. **Transient and Sustain EQ** — treats attack and sustained distortion differently
6. **Note and Harmonic Following** — frequency-relative processing for leads, low tunings, bass
7. **Four-Zone Multiband Engine** — broad dynamic control without replacing precise parametric EQ
8. **Mix Lock-In** — shows and reduces guitar/bass masking through host sidechain
9. **Editable Analysis** — every recommendation becomes a visible, user-controlled filter
10. **Real-Time Guitar Priority** — no linear-phase pre-ringing, no unnecessary latency, no studio bloat
