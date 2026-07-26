# k

## Goal

A/B comparison, gain-matched bypass, profiles, factory presets, and delta audition.

A/B state + gain-matched bypass: extend StateManager. Click-safe crossfade. Rolling RMS measurement.

Profile system: placement + tuning profiles. See SHREDEQ_SPEC.md sections 15, 25.

Factory presets: categorized, gain-conscious. Resources/Presets/.

Delta audition: wire RejectedSignalRouter modes to GUI.

## Notes

- Async: Wave 1: k.1 (A/B + gain match) and k.2 (Profiles) are independent. Wave 2: k.3 (factory presets) depends on k.2 for profile categories. Wave 3: k.4 (delta audition + processBlock wiring + CMakeLists) depends on k.1 — sole Processor/Editor/CMakeLists owner. Wave 4: k.5 (tests) depends on all.
- Risk: StateManager, PluginProcessor, PluginEditor modified — k.4 is fan-in.
