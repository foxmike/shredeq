# e

## Goal

Four-zone multiband engine with LR4 crossovers and per-zone dynamics.

See SHREDEQ_SPEC.md section 5 for full architecture.

LR4 crossover (Source/DSP/MultibandCrossover.h/.cpp): complementary serial tree, click-free movement, ordering protection. Reconstruction test: all zones unity = flat magnitude (tolerance-based, not sample-identical — LR4 has flat magnitude but frequency-dependent phase).

Zone processing (Source/DSP/MultibandZone.h/.cpp): per-zone dynamics reusing EnvelopeFollower.

Multiband engine (Source/DSP/MultibandEngine.h/.cpp): orchestrates split, zone processing, recombination.

## Notes

- Async: Wave 1: e.1 (MultibandCrossover) standalone. Wave 2: e.2 (MultibandZone) depends on e.1. Wave 3: e.3 (MultibandEngine + processBlock + CMakeLists) depends on e.1, e.2 — sole owner of Processor and CMakeLists. Wave 4: e.4 (tests) depends on all.
- Risk: PluginProcessor fan-in in e.3.
