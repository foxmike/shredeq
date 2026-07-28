# ShredEQ — Operations Guide

## Purpose
ShredEQ is an adaptive parametric, dynamic, and multiband equalizer designed specifically for electric guitar and bass. It target-corrects common high-gain issues such as palm-muted low-frequency mud, cabinet/room resonances, and excessive high-frequency fizz. The plugin integrates conventional parametric EQ, dynamic processing, and automatic resonance suppression with an analysis layer to provide non-destructive subtractive control that maintains pick transient clarity and instrument identity.

## Signal Flow
The internal audio signal path runs from input to output through the following sequential stages:
1. **Input Conditioning (`inputGain`)**: Applies pre-processing gain (-24 to +24 dB).
2. **Tight Cut (`TightCut::enabled`)**: A high-pass filter with an optional punch bell. If configured as `preStageA`, it processes the signal first.
3. **Chug Stabilizer (`Chug::enabled`)**: A dynamic bandpass attenuation processor configured to clamp low-frequency energy spikes (palm-mutes) in the 60–320 Hz target band.
4. **Parametric Stage A (Unimplemented)**: A placeholder stage for early-routing parametric EQ bands (1 to 12).
5. **Guitar Intelligence & Analysis**: Runs a pitch detector and spectral analysis engine. At control rate (every 256 samples), the pitch detector output feeds the resonance detector.
6. **Resonance Suppressor (`resonanceSlots`)**: Features 6 slots that can detect and dynamically suppress narrow-Q cabinet/room resonances between 150 Hz and 16 kHz.
7. **Four-Zone Multiband Engine (`multibandEnabled`)**: A multiband compressor/expander dividing the spectrum using three crossover frequencies into four independent bands, with transient and sustain adjustments.
8. **Parametric Stage B (Unimplemented)**: A placeholder stage for late-routing parametric EQ bands.
9. **Tight Cut (Alternate Placement)**: Processes the signal if configured as `postStageA`.
10. **Output Gain / Trim (`outputGain`, `outputTrim`)**: Applies final master gain.

---

## Controls Reference

Exposed parameters registered with the APVTS:

### Global Controls
- **Input Gain (`inputGain`)**: float, -24.0 to +24.0 dB, default 0.0 dB. Sets pre-processing level.
- **Output Gain (`outputGain`)**: float, -24.0 to +24.0 dB, default 0.0 dB. Sets master output level.
- **Output Trim (`outputTrim`)**: float, -12.0 to +12.0 dB, default 0.0 dB. Fine output trim level.
- **Global Bypass (`globalBypass`)**: bool, default Off. Bypasses all processing.
- **Analyzer Enable (`analyzerEnable`)**: bool, default On. Toggles the spectrum analyzer.
- **A/B State (`abState`)**: choice, A or B, default A. Switches active EQ preset buffers.
- **Profile (`profileIndex`)**: choice, 7 options (Guitar DI / Pre-Amp, Post Amp / Cab, Rhythm Guitar, Lead Guitar, Stereo Guitar Bus, Active Bass, Distorted Bass), default Guitar DI.
- **Tuning (`tuningIndex`)**: choice, 18 guitar/bass tunings, default E Standard.
- **Global Lookahead (`lookaheadMode`)**: choice, Off, 0.5 ms, 1 ms, 2 ms, 5 ms, default Off. (Unused in DSP thread).

### Tight Cut
- **Tight Cut Enabled (`TightCut::enabled`)**: bool, default Off. Enables the resonant HPF stage.
- **Tight Cut Frequency (`TightCut::frequency`)**: float, 25.0 to 220.0 Hz, default 80.0 Hz.
- **Tight Cut Slope (`TightCut::slope`)**: choice, 12, 18, 24, 36 dB/oct, default 24 dB/oct (index 2).
- **Tight Cut Resonance (`TightCut::resonance`)**: float, 0.5 to 2.5, default 0.707. Sets Q factor.
- **Tight Cut Punch Ratio (`TightCut::punchRatio`)**: float, 0.75 to 1.5, default 1.0. Scales punch bell Q relative to HPF Q.
- **Tight Cut Punch Gain (`TightCut::punchGain`)**: float, 0.0 to 8.0 dB, default 0.0 dB. Adds pre-HPF boost.
- **Tight Cut Punch Q (`TightCut::punchQ`)**: float, 0.35 to 3.0, default 0.707. Sets punch bell Q.
- **Tight Cut Placement (`TightCut::placement`)**: choice, Pre Stage A (0) or Post Stage A (1), default Pre Stage A.
- **Tight Cut Routing (`TightCut::routing`)**: choice, Stereo (0), Mid (1), Side (2), default Stereo.
- **Tight Cut Listen Removed (`TightCut::listenRemoved`)**: bool, default Off. Auditions only the attenuated signal.

### Chug Stabilizer
- **Chug Stabilizer Enabled (`Chug::enabled`)**: bool, default Off. Enables dynamic palm-mute clamp.
- **Chug Target Frequency (`Chug::frequency`)**: float, 60.0 to 320.0 Hz, default 140.0 Hz.
- **Chug Bandwidth (`Chug::bandwidth`)**: float, 0.3 to 2.5 oct, default 1.0 oct.
- **Chug Maximum Reduction (`Chug::maxReduction`)**: float, 0.0 to 18.0 dB, default 6.0 dB. Max dynamic cut.
- **Chug Threshold (`Chug::threshold`)**: float, -90.0 to 0.0 dBFS, default -24.0 dBFS.
- **Chug Attack (`Chug::attack`)**: float, 0.2 to 20.0 ms, default 3.0 ms.
- **Chug Hold (`Chug::hold`)**: float, 0.0 to 80.0 ms, default 20.0 ms.
- **Chug Release (`Chug::release`)**: float, 15.0 to 350.0 ms, default 100.0 ms.
- **Chug Preserve Pick (`Chug::preservePick`)**: float, 0.0% to 100.0%, default 50.0%. Prevents transient attenuation.
- **Chug Transient Sensitivity (`Chug::transientSens`)**: float, 0.0% to 100.0%, default 50.0%.
- **Chug Dominance Sensitivity (`Chug::dominanceSens`)**: float, 0.0% to 100.0%, default 50.0%.

### Multiband Compressor
- **Multiband Enabled (`multibandEnabled`)**: bool, default Off. Enables 4-zone multiband dynamics.
- **Multiband Crossovers (`multibandCrossovers[0-2]`)**: float:
  - Crossover 1: 40.0 to 500.0 Hz, default 140.0 Hz.
  - Crossover 2: 150.0 to 3000.0 Hz, default 900.0 Hz.
  - Crossover 3: 1000.0 to 12000.0 Hz, default 4500.0 Hz.
- **Multiband Zone [1-4] Controls**:
  - *Gain*: -24.0 to +24.0 dB, default 0.0 dB.
  - *Threshold*: -90.0 to 0.0 dBFS, default -24.0 dBFS.
  - *Ratio*: 1.0:1 to 10.0:1, default 2.0:1.
  - *Attack*: 0.1 to 300.0 ms, default 10.0 ms.
  - *Release*: 5.0 to 3000.0 ms, default 150.0 ms.
  - *Transient Emphasis*: -100.0% to 100.0%, default 0.0%.
  - *Sustain Emphasis*: -100.0% to 100.0%, default 0.0%.
  - *Placement*: choice (Stereo Linked, Stereo Unlinked, Left, Right, Mid, Side), default Stereo Linked.
  - *Detector Source*: choice (10 options, e.g. Full-band, Band-limited), default Band-limited.
  - *Auto Threshold*: bool, default True.
  - *Mode*: choice (Downward, Upward), default Downward.

### Resonance Suppressor
- **Resonance [1-6] Enabled**: bool, default Off.
- **Resonance [1-6] Frequency**: float, 150.0 Hz to 16.0 kHz, default 3.0 kHz.
- **Resonance [1-6] Q**: float, 3.0 to 40.0, default 10.0.
- **Resonance [1-6] Maximum Depth**: float, 0.0 to 18.0 dB, default 6.0 dB.
- **Resonance [1-6] Attack**: float, 0.5 to 100.0 ms, default 10.0 ms.
- **Resonance [1-6] Release**: float, 20.0 to 2000.0 ms, default 200.0 ms.
- **Resonance [1-6] Persistence**: float, 0.0% to 100.0%, default 50.0%.
- **Resonance [1-6] Mode**: choice, Static (0) or Dynamic (1), default Dynamic.
- **Resonance [1-6] Placement**: choice (6 placements), default Stereo Linked.
- **Resonance [1-6] Freeze**: bool, default Off. Freezes current dynamic gain cut.

### Unwired / Unimplemented Parameter Group
The following parameters are registered in the APVTS layout but have no functionality in the current build:
- **Band [1-12] Parameters**: `enabled`, `stage`, `type`, `frequency`, `gain`, `q`, `slope`, `route`, `dynamicMode`, `dynamicRange`, `threshold`, `ratio`, `attack`, `hold`, `release`, `knee`, `detectorSource`, `detectorFrequency`, `detectorQ`, `trackingSource`, `trackingHarmonic`, `lookahead`.
- **Sidechain Parameters**: `Sidechain::source`, `Sidechain::lockInEnabled`, `Sidechain::lockInAmount`, `Sidechain::carveMaxGain`, `Sidechain::carveThreshold`.
- **Mid/Side Parameters**: `MidSide::sideHighPass`, `MidSide::monoGuardEnabled`, `MidSide::monoGuardProtection`.
- **Note Follower Parameters**: `NoteFollower::trackingSource`, `NoteFollower::harmonic`, `centsOffset`, `confidenceThreshold`, `smoothing`, `holdTime`, `returnTime`, `minFreq`, `maxFreq`, `midiPriority`.

---

## Metering and Visualization
- **FFT Spectrum Overlay**: Aggregates and displays real-time input and output FFT curves.
- **Percentile Cloud**: Visualizes dynamic energy distribution across frequency bands.
- **EQ Response Curve Workspace**: Renders active filter curves. (Note: Only displays the curves; the actual parametric filter execution is disabled in the DSP thread).

## Preset System
Five factory presets are included under `Resources/Presets/`:
1. `Bass_ClangControl`: Dynamic control of low-mid bass frequencies.
2. `Diag_FindResonances`: Optimization setup for locating cabinet peaks.
3. `Lead_SingingFocus`: Mid-forward boost configuration for solos.
4. `PreAmp_TightSixString`: HPF and chug settings for rhythm tracking.
5. `Rhythm_ModernCleanup`: Standard fizz and mud cleanup.
Presets are loaded and saved through host-session state serialization utilizing standard APVTS XML.

## Latency
- The plugin reports `0` samples of latency.
- Although `lookahead` parameters exist, the lookahead delay buffer and latency reporting calls (`setLatencySamples`) are not integrated into the active signal path.

## Supported Formats
- VST3, Standalone.

## Known Limitations
- **Parametric EQ Disabled**: The 12 parametric EQ bands (`ParametricBand` class) are completely bypassed in `ShredEQAudioProcessor::processBlock`. Tuning frequency, gain, and Q curves on the UI workspace will not alter the output audio.
- **Lookahead & Sidechain Inoperative**: Lookahead delay, sidechain carving, note follower tracking, and mono guard options are unwired in the DSP engine.
- **Tuning UI Link**: Note tracking parameters do not affect filter frequencies in real time.
