# ShredEQ — Operations Manual

## Plugin Overview
ShredEQ is a parametric, dynamic, and multiband equalizer designed specifically for electric guitar and bass. It target-corrects common high-gain issues such as palm-muted low-frequency mud, cabinet/room resonances, and excessive high-frequency fizz. The plugin integrates parametric EQ, dynamic processing, and automatic resonance suppression with an analysis layer to provide subtractive control that maintains pick transient clarity and instrument identity.

## Intended Use
ShredEQ is designed to be placed in guitar/bass chains to clean up frequency mud, track and suppress resonances, and control dynamics across four frequency zones.

## Signal Flow
The internal audio signal path runs from input to output through the following sequential stages:
1. **Input Conditioning (`inputGain`)**: Applies pre-processing gain (-24 to +24 dB).
2. **Tight Cut (`TightCut::enabled`)**: A high-pass filter with an optional punch bell. If configured as `preStageA`, it processes the signal first.
3. **Chug Stabilizer (`Chug::enabled`)**: A dynamic bandpass attenuation processor configured to clamp low-frequency energy spikes (palm-mutes) in the 60–320 Hz target band.
4. **Parametric Stage A**: Processes enabled parametric bands assigned to Stage A.
5. **Tight Cut (Alternate Placement)**: Processes after Stage A when Placement is set to Post Stage A.
6. **Guitar Intelligence & Analysis**: Runs pitch and spectral analysis. At control rate, the pitch detector feeds enabled note-following bands and the resonance detector.
7. **Resonance Suppressor (`resonanceSlots`)**: Processes enabled static or dynamic resonance slots between 150 Hz and 16 kHz.
8. **Four-Zone Multiband Engine (`multibandEnabled`)**: Divides the spectrum into four independently controlled dynamics zones.
9. **Sidechain Carve**: When Mix Lock-In is enabled and the selected key is available, applies detector-driven masking cuts.
10. **Parametric Stage B**: Processes enabled parametric bands assigned to Stage B.
11. **Stereo Safety**: Applies Side High-Pass and, when enabled on stereo input, Mono Guard protection.
12. **Output Gain / Trim (`outputGain`, `outputTrim`)**: Applies final master gain.

## Controls and Parameters
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
- **Global Lookahead (`lookaheadMode`)**: choice, Off, 0.5 ms, 1 ms, 2 ms, or 5 ms, default Off. The current build remains at zero reported latency for all selections.

### Parametric Bands
ShredEQ provides 12 parametric bands. Each band has its own Enabled state, Stage A/B placement, filter type, frequency, gain, Q, slope, and stereo routing. A disabled band is bypassed regardless of its other settings.

- **Filter Types**: Bell, Notch, Low Shelf, High Shelf, Low Pass, High Pass, Band Pass, Tilt, Resonant High Pass, Resonant Low Pass, and Flat Dynamic.
- **Routing**: Stereo Linked, Stereo Unlinked, Left, Right, Mid, or Side. Mid and Side routing require a stereo main input; mono input has no separate Mid/Side domain.
- **Dynamic Mode**: Static, Downward, Upward, Compression, Expansion, Transient Only, Sustain Only, or External Sidechain. Static mode applies the band as a conventional EQ. Select any non-Static mode to activate Threshold, Dynamic Range, Ratio, Attack, Hold, Release, Knee, and detector controls.
- **Detector Source**: Full-band Internal, Band-limited Internal, Left, Right, Mid, Side, External Sidechain, Input Minus Output, Transient, or Sustain. Band-limited detection uses Detector Frequency and Detector Q. Mid and Side detector sources require stereo input. External Sidechain detection requires a routed sidechain bus.
- **Tracking Source**: Off, Audio, or MIDI. A band follows pitch only when the band is enabled and its effective tracking source is not Off. A band-level Audio or MIDI selection overrides the global Note Tracking Source; Off inherits the global selection.
- **Tracking Harmonic**: Selects the harmonic multiple used to move the band's center frequency. Global harmonic, cents offset, confidence, smoothing, hold, return, and frequency-limit settings shape the tracking response.
- **Lookahead**: Off, 0.5, 1, 2, or 5 ms. The current build operates at zero reported latency for all selections.

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

### Sidechain, Note Tracking, and Stereo Safety
- **Sidechain Source**: External, Main Input, Mid, or Side. External requires the host to route a mono or stereo signal to the plugin's sidechain input. Main Input uses the conditioned program signal; Mid and Side derive the key from a stereo main input.
- **Mix Lock-In Enabled**: Enables sidechain analysis and masking carve. Carve Maximum, Carve Threshold, and Mix Lock-In Amount control the depth, trigger point, and applied amount. With External selected and no sidechain bus routed, the carve remains inactive.
- **Global Note Tracking Source**: Off, Audio Monophonic, or MIDI. Audio tracking works best with a clean, stable monophonic signal inside the configured minimum/maximum frequency range and above the confidence threshold. MIDI tracking requires note data routed from the host. Individual bands must also be enabled and configured to inherit or select a tracking source.
- **Side High-Pass**: Removes low-frequency energy from the Side component on stereo signals after Stage B.
- **Mono Guard**: Requires a stereo main signal. When enabled, it monitors L/R correlation and reduces risky Side energy according to Mono Guard Protection. It has no separate Side signal to protect on mono input.

## Operating Prerequisites and Interactions
- Enable the relevant module before judging its continuous controls: a parametric band, Tight Cut, Chug Stabilizer, each resonance slot, Multiband, Mix Lock-In, and Mono Guard all have independent enable states.
- For a parametric band to react dynamically, enable the band and choose a non-Static Dynamic Mode. Match Detector Source to the available signal; External Sidechain modes require a host-routed sidechain bus.
- Stage A bands run before resonance suppression, multiband processing, sidechain carve, and Stage B. Use Stage A for corrective filtering that should feed later detectors; use Stage B for final shaping after those processors.
- Tight Cut set to Pre Stage A runs before Chug and Stage A. Post Stage A runs after Stage A and before resonance suppression.
- Note following moves only enabled bands whose effective Tracking Source is Audio or MIDI. Set suitable confidence and frequency limits, then choose the desired harmonic and cents offset.
- Mid, Side, and Mono Guard operation require stereo main input. An external detector additionally requires an enabled sidechain bus with signal present.

## Stereo and Mono Behavior
Crossovers and dynamics blocks support selectable routing (Stereo, Mid, Side, Left, Right). The dynamic bands can also be run in stereo-linked or unlinked/dual-mono modes.

## Latency and Lookahead
The current build processes audio with zero reported latency. Non-Off global and per-band Lookahead selections do not change the audio delay or host PDC.

## Bypass Behavior
Controlled via the `globalBypass` parameter. When active, all dynamics and filtering blocks are bypassed, passing audio transparently.

## Presets and State Recall
Five factory presets are included under `Resources/Presets/`:
1. `Bass_ClangControl`: Dynamic control of low-mid bass frequencies.
2. `Diag_FindResonances`: Optimization setup for locating cabinet peaks.
3. `Lead_SingingFocus`: Mid-forward boost configuration for solos.
4. `PreAmp_TightSixString`: HPF and chug settings for rhythm tracking.
5. `Rhythm_ModernCleanup`: Standard fizz and mud cleanup.

Presets are loaded and saved through host-session state serialization utilizing standard APVTS XML.

## Metering and Visual Feedback
- **FFT Spectrum Overlay**: Aggregates and displays real-time input and output FFT curves.
- **Percentile Cloud**: Visualizes dynamic energy distribution across frequency bands.
- **EQ Response Curve Workspace**: Renders active filter curves.

## Supported Formats
- VST3, Standalone.
