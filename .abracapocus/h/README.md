# h

## Goal

External sidechain masking, stage-level M/S processing, and optional lookahead with PDC.

Stage-level M/S router (Source/DSP/MidSideRouter.h/.cpp): performs M/S encode/decode ONCE around the parametric stages, not per-band. Bands process whichever domain the router provides. Side high-pass (configurable 120-300Hz) lives in the router.

Sidechain masking analyzer (Source/DSP/SidechainAnalyzer.h/.cpp): ERB/octave band accumulation, overlap/dominance/masking metrics. Collision map data for GUI.

Masking carver (Source/DSP/MaskingCarver.h/.cpp): applies bounded carve gains using a small number of dynamic bell filters. Suggestion mode converts collisions into editable parametric bands. Auto mode applies only explicitly enabled carver bands.

Mono Guard (Source/DSP/MonoGuard.h/.cpp): frequency-dependent L/R coherence, protection mode.

Lookahead delay (Source/DSP/LookaheadDelay.h/.cpp): preallocated circular buffer. Off, 0.5, 1, 2, 5ms. Reports exact latency via setLatencySamples. Zero samples when disabled. Click-free delay changes.

Wire all into processBlock. v1.0 uses host external sidechain only.

## Notes

- Async: Wave 1: h.1 (MidSideRouter) and h.2 (SidechainAnalyzer + MaskingCarver) and h.3 (LookaheadDelay) are independent. Wave 2: h.4 (MonoGuard) depends on h.1. Wave 3: h.5 (processBlock wiring + CMakeLists) depends on all — sole Processor/CMakeLists owner. Wave 4: h.6 (tests) depends on h.1, h.2, h.3.
- Risk: PluginProcessor heavily modified — fan-in in h.5. Lookahead must call setLatencySamples. Sidechain gracefully skipped in standalone.
