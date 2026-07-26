# a

## Goal

Project scaffold, build system, APVTS parameter infrastructure, and state engine.

Create the ShredEQ JUCE plugin project. Inspect ShredBoost and ShredSpace CMakeLists.txt for the known-working structure and follow the same conventions for plugin code, manufacturer code, company name, bundle ID, version, and copy-after-build policy.

CMakeLists.txt: juce_add_plugin targeting VST3 + Standalone. Enable NEEDS_MIDI_INPUT for host MIDI note following. CMakePresets.json with RelWithDebInfo configure preset and matching build/test presets. scripts/build_all.sh. JUCE as a git submodule.

PluginProcessor: configure main input bus, optional sidechain input bus, and output through AudioProcessor::BusesProperties. Implement isBusesLayoutSupported for mono, stereo, and stereo+sidechain layouts. Standalone must operate safely without sidechain. Constructor creates APVTS, processBlock stub, state save/restore with schema versioning.

ParameterIDs.h: complete stable parameter inventory for all v1.0 automatable controls. 12 bands x full parameter set (enabled, stage, type, frequency, gain, q, slope, route, dynamicMode, dynamicRange, threshold, ratio, attack, hold, release, knee, detectorSource, detectorFrequency, detectorQ, trackingSource, trackingHarmonic, lookahead). Global params: inputGain, outputGain, globalBypass, analyzerEnable, abState, profileIndex, tuningIndex. Tight Cut: enabled, frequency, slope, resonance, punchRatio, punchGain, punchQ, placement, routing, listenRemoved. Chug Stabilizer: enabled, frequency, bandwidth, maxReduction, threshold, attack, hold, release, preservePick, transientSens, dominanceSens. Multiband: enabled, crossover1-3 freq, per-zone gain/threshold/ratio/attack/release/transientEmphasis/sustainEmphasis/placement/detectorSource/autoThreshold/mode. Sidechain: source, lockInEnabled, lockInAmount, carveMaxGain, carveThreshold. M/S: sideHighPass, monoGuardEnabled, monoGuardProtection. Resonance suppressor: 6 slots x (enabled, frequency, q, maxDepth, attack, release, persistence, mode, placement, freeze). Note follower: trackingSource, harmonic, centsOffset, confidenceThreshold, smoothing, holdTime, returnTime, minFreq, maxFreq, midiPriority. Lookahead: mode (off/0.5/1/2/5ms). Output trim.

State engine: XML state with schema_version, A/B slots, factory presets loaded from Resources/Presets/ (read-only), user presets use juce::File::getSpecialLocation for per-user app data. .gitignore: build/, cmake-build-*, state/, .abracapocus/state/, .abracapocus/logs/, .abracapocus/reports/.

## Notes

- Async: Wave 1: a.1 (CMakeLists + build + JUCE submodule) and a.4 (.gitignore + README + TestMain) are independent. Wave 2: a.2 (ParameterIDs + APVTS + PluginProcessor) depends on a.1. Wave 3: a.3 (StateManager + PluginEditor) depends on a.2. a.2 is the sole owner of CMakeLists.txt, PluginProcessor, and ParameterIDs in this phase.
- Risk: CMakeLists.txt is shared build root — a.2 is sole owner. JUCE submodule add requires network. Sidechain bus requires correct isBusesLayoutSupported.
