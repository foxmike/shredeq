# g

## Goal

Resonance detection, anti-fizz suppression, and shared spectral analysis subsystem.

Shared spectral analysis engine (Source/Analysis/SpectralAnalysisEngine.h/.cpp, Source/Analysis/AnalysisFifo.h/.cpp): STFT with Hann window, configurable FFT size, hop. Background thread receives audio via juce::AbstractFifo. This is the SINGLE spectral subsystem — resonance detector, percentile cloud, spectrum display, persistence map, and sidechain collision analysis all consume data from this service. Skip frames when overloaded. Fixed memory bounds.

Resonance detector (Source/DSP/ResonanceDetector.h/.cpp): persistence analysis, candidate scoring. Musical-harmonic rejection uses PitchDetector f0 from phase F — fixed peaks that persist across different notes get higher confidence than peaks following harmonics.

Suppressor slots (Source/DSP/ResonanceSuppressor.h/.cpp): 6 dynamic notch filters. Scan workflow with candidate cards.

See SHREDEQ_SPEC.md section 8 for complete DSP math.

## Notes

- Async: Wave 1: g.1 (SpectralAnalysisEngine + AnalysisFifo) standalone. Wave 2: g.2 (ResonanceDetector) depends on g.1 and uses PitchDetector from phase F. Wave 3: g.3 (ResonanceSuppressor + scan + processBlock + CMakeLists) depends on g.2 — sole Processor/CMakeLists owner. Wave 4: g.4 (tests) depends on all.
- Risk: SpectralAnalysisEngine introduces background thread and AbstractFifo. PluginProcessor fan-in in g.3. ResonanceDetector depends on PitchDetector from phase F.
