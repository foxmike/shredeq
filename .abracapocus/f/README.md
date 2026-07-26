# f

## Goal

Note and harmonic follower with pitch detection. MUST be implemented before resonance detection because the resonance system's musical-harmonic rejection depends on an estimated f0.

Pitch detector (Source/DSP/PitchDetector.h/.cpp): YIN algorithm on conditioned downsampled signal. Runs at lower control rate on background thread. Audio thread supplies frames via lock-free FIFO. Returns: estimated frequency, confidence, timestamp, tracking state.

MIDI note tracking: f0 = 440*2^((m-69)/12) from host MIDI. Priority: lowest/highest/last.

Note follower (Source/DSP/NoteFollower.h/.cpp): harmonic targeting f_target = f0*h*2^(c/1200). Log-frequency smoothing. Confidence handling. Integrate into ParametricBand: any band can optionally attach a NoteFollower.

See SHREDEQ_SPEC.md section 9 for complete DSP math.

## Notes

- Async: Wave 1: f.1 (PitchDetector) standalone. Wave 2: f.2 (NoteFollower + ParametricBand integration + CMakeLists) depends on f.1. Wave 3: f.3 (tests) depends on f.1, f.2.
- Risk: ParametricBand modified — serialize after phases B-C. PitchDetector background thread shares pattern with later analyzer threads.
