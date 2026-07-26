# b

## Goal

Parametric EQ engine: coefficient library, TPT state-variable filters, 12-band parametric system, and cascade response.

Biquad coefficient library (Source/DSP/BiquadCoefficients.h/.cpp): RBJ Audio EQ Cookbook coefficients for all filter types. Slope support requires both first-order and second-order sections: 6dB/oct = one first-order, 12dB/oct = one biquad, 18dB/oct = first-order + biquad, 24dB/oct = two biquads, 36dB/oct = three biquads, 48dB/oct = four biquads. Use standard RBJ coefficient formulas for v1.0. Test actual magnitude response near Nyquist rather than claiming arbitrary analog matching.

TPT state-variable filter (Source/DSP/TPTFilter.h/.cpp): topology-preserving transform for modulation-stable sweeps. Bell, shelf, LP, HP, BP.

Parametric band class (Source/DSP/ParametricBand.h/.cpp): wraps biquad cascade or TPT per band. Holds: enabled, stageA/B, frequency, gain, Q, type, slope, stereo routing MODE (linked/unlinked/L/R/mid/side). Bands do NOT perform their own M/S encoding — they process whichever channel domain the stage router provides. 12 bands total.

Response computer (Source/DSP/ResponseComputer.h/.cpp): composite H(e^jw) at ~512 log-spaced points on message thread.

All DSP: juce::SmoothedValue, no heap allocation in process methods.

## Notes

- Async: Wave 1: b.1 (BiquadCoefficients) and b.2 (TPTFilter) are independent new modules. Wave 2: b.3 (ParametricBand) depends on b.1, b.2. Wave 3: b.4 (ResponseComputer) depends on b.3. b.4 is sole owner of CMakeLists.txt. Wave 4: b.5 (tests) depends on b.1, b.2, b.3.
- Risk: CMakeLists.txt updated by b.4 only. First-order filter sections needed alongside biquads.
