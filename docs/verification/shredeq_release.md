# ShredEQ v1.0 Release Verification Checklist

This document maps the release criteria and testing requirements defined in `SHREDEQ_SPEC.md` to specific verification methods. It distinguishes between automated gates (CI/scripts) and manual validation required on host systems.

## 1. Automated Verification Gates

The following steps must be executed to ensure the build is stable and meets the basic RT-safety and functional requirements.

### How to Run
1. **Full Validation Suite**:
   ```bash
   ./scripts/validate.sh
   ```
   This script performs:
   - Release-compatible configuration and build.
   - Execution of all `ctest` unit tests.
   - Static analysis of the audio thread hot path for forbidden patterns (allocations, locks, I/O).
   - Static verification of zero-latency reporting and lookahead gating.
   - Intent-based symbol checks for click-free bypass and state round-tripping.

2. **Manual Build & Test (Alternate)**:
   - Configure: `cmake --preset linux-relwithdebinfo`
   - Build: `cmake --build --preset linux-relwithdebinfo`
   - Test: `ctest --preset linux-relwithdebinfo --output-on-failure`

**Note:** Current CI expectations are Linux-first. Windows builds must be verified manually on a Windows host.

---

## 2. Release Criteria Mapping (§22)

| Criterion | Status | Verification Method |
| :--- | :--- | :--- |
| Standalone/VST3 build (Linux) | `VERIFIED_IN_CI` | `scripts/validate.sh` (Build/Configure) |
| Standalone/VST3 build (Windows) | `MANUAL_REQUIRED` | Manual build on Windows host |
| No audio-thread allocations | `PARTIAL` | `scripts/validate.sh` (Static scan) $\rightarrow$ `MANUAL_REQUIRED` (`pluginval`) |
| No parameter zippering/clicks | `PARTIAL` | Symbol check (SmoothedValue) $\rightarrow$ `MANUAL_REQUIRED` (Ear check) |
| Bypass is click-free | `PARTIAL` | Symbol check (crossfade/gainMatch) $\rightarrow$ `MANUAL_REQUIRED` (Ear check) |
| Reported latency is correct | `VERIFIED_IN_CI` | `scripts/validate.sh` (Zero-latency static check) |
| External sidechain works | `MANUAL_REQUIRED` | Host routing (Bitwig/REAPER) |
| Mono/Stereo layouts correct | `MANUAL_REQUIRED` | Host routing (Bitwig/REAPER) |
| A/B comparison is reliable | `PARTIAL` | State/A/B unit tests $\rightarrow$ `MANUAL_REQUIRED` (Ear check) |
| Gain matching stable | `PARTIAL` | Symbol check $\rightarrow$ `MANUAL_REQUIRED` (Ear check) |
| Analyzer CPU/Interrupt safe | `MANUAL_REQUIRED` | Performance profiling (CPU meter/Host stress) |
| All state restores after project reopen | `PARTIAL` | `StateTests.cpp` $\rightarrow$ `MANUAL_REQUIRED` (Host project save/load) |
| Factory presets categorized/gain-conscious | `PARTIAL` | Filesystem check in `Resources/Presets/` $\rightarrow$ `MANUAL_REQUIRED` (Listening) |
| Resonance recommendations editable | `PARTIAL` | `ResonanceTests.cpp` $\rightarrow$ `MANUAL_REQUIRED` (UI usage) |
| Removed-signal audition works | `PARTIAL` | Symbol check $\rightarrow$ `MANUAL_REQUIRED` (UI usage) |
| GUI usable at min size/high DPI | `MANUAL_REQUIRED` | Manual resize and OS scaling check |
| No architecture depends on online services | `VERIFIED_IN_CI` | Source code scan (no networking libs/URLs) |
| Validation scripts/docs complete | `VERIFIED_IN_CI` | This document and `scripts/validate.sh` |

---

## 3. Testing Expansion (§21.1–21.10)

| Area | Status | Verification Method |
| :--- | :--- | :--- |
| 21.1 Filter Response | `VERIFIED_IN_CI` | `BiquadTests.cpp`, `ParametricBandTests.cpp` |
| 21.2 Stability | `PARTIAL` | Unit tests (basic) $\rightarrow$ `MANUAL_REQUIRED` (Automation stress) |
| 21.3 Dynamic EQ | `VERIFIED_IN_CI` | `DynamicEQTests.cpp` |
| 21.4 Multiband Reconstruction | `VERIFIED_IN_CI` | `MultibandTests.cpp` |
| 21.5 Chug Stabilizer | `VERIFIED_IN_CI` | `ChugStabilizerTests.cpp` |
| 21.6 Resonance Detector | `VERIFIED_IN_CI` | `ResonanceTests.cpp` |
| 21.7 Pitch Tracking | `VERIFIED_IN_CI` | `PitchTrackingTests.cpp` |
| 21.8 State | `VERIFIED_IN_CI` | `StateTests.cpp` |
| 21.9 Host Validation | `MANUAL_REQUIRED` | Bitwig Linux, REAPER Linux/Windows, `pluginval` |
| 21.10 Audio Regression | `MANUAL_REQUIRED` | Comparison of rendered fixtures against gold standard |

---

## 4. Manual Validation Procedures

Since the following cannot be verified by `scripts/validate.sh` or `ctest`, the following stubs must be performed by the operator:

### Host & Routing
- [ ] **External Sidechain**: Create a track with ShredEQ, route a second track to the sidechain input. Verify gain reduction occurs only when sidechain is active.
- [ ] **Layouts**: Test Mono $\rightarrow$ Mono, Mono $\rightarrow$ Stereo, Stereo $\rightarrow$ Stereo. Verify no channel swapping or unexpected mono-summing.
- [ ] **Windows Host**: Build and load in REAPER/Bitwig on Windows. Verify basic stability.

### Project & State
- [ ] **Reopen**: Save a project with complex EQ settings, close the DAW, and reopen. Verify all parameters and A/B states are restored.
- [ ] **Offline Render**: Perform a bounce/export. Verify that the processed audio matches the real-time experience.

### GUI & Ergonomics
- [ ] **DPI/Resize**: Change OS scaling from 100% to 200%. Resize window to minimum. Verify all controls are visible and clickable.
- [ ] **A/B Gain Match**: Toggle A/B. Verify that the perceived loudness remains constant despite different EQ curves.

---

## 5. Latency & Performance

- **Default Mode**: Reports `0` samples.
- **Lookahead Modes**: Reports non-zero samples (0.5ms, 1ms, 2ms, 5ms). Verify that the host's Plugin Delay Compensation (PDC) updates correctly.
- **Bypass**: Toggle global bypass. Verify that the transition is click-free (crossfaded).

## 6. Factory Presets

- **Path**: `Resources/Presets/`
- **Categories**: Pre-Amp, Rhythm Post-Cab, Lead, Stereo Bus, Bass, Diagnostic.
- **Criteria**: All presets must be gain-conscious (no output clipping at nominal input) and categorized according to the spec.

---

## 7. Sign-Off

| Role | Initials | Date | Git SHA |
| :--- | :--- | :--- | :--- |
| QA Operator | | | |
| Technical Lead | | | |
