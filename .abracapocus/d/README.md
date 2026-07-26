# d

## Goal

Tight Cut resonant HPF with punch bell, and Chug Stabilizer.

See SHREDEQ_SPEC.md sections 6 and 7 for complete architecture and DSP math.

Tight Cut (Source/DSP/TightCut.h/.cpp): resonant HPF + linked punch bell. Listen-to-removed mode.

Chug Stabilizer (Source/DSP/ChugStabilizer.h/.cpp): palm-mute detection and dynamic reduction with Preserve Pick.

Wire both into PluginProcessor between input conditioning and parametric Stage A.

## Notes

- Async: Wave 1: d.1 (TightCut) and d.2 (ChugStabilizer) independent. Wave 2: d.3 (PluginProcessor + CMakeLists wiring) depends on d.1, d.2 — sole owner of Processor and CMakeLists. Wave 3: d.4 (tests) depends on d.1, d.2.
- Risk: PluginProcessor fan-in in d.3.
