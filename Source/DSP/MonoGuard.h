#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include "TPTFilter.h"
#include <array>
#include <atomic>

namespace ShredEQ::DSP
{

/**
    MonoGuard — Frequency-dependent Left/Right coherence analysis and protection.

    CRITICAL ARCHITECTURAL CONSTRAINTS:
    1. ZERO intentional latency: MonoGuard must not introduce any delay to the signal path.
    2. Fixed memory: No heap allocations on the audio thread.
    3. True bypass of protection when disabled: Analysis may still update meters,
       but the audio path remains completely untouched when protection is disabled.
    4. Coordinate with MidSideRouter: Operates on Mid/Side buffers or Left/Right buffers.

    Frequency-dependent Coherence Calculation:
        S_LR(f) = E[X_L(f) * conj(X_R(f))]
        rho(f) = Re(S_LR(f)) / (sqrt(S_LL(f) * S_RR(f)) + eps)

    Band Spacing and Resolution:
        Logarithmically spaced bandpass filters from 30 Hz to 18 kHz.
        The default layout uses 24 bands, providing approximately 2/5 octave
        (0.401 octave) frequency resolution.
        The filter Q factor is calculated dynamically based on this spacing:
        Q = sqrt(2^BW) / (2^BW - 1) which yields Q ≈ 3.61.
        The expectation E[.] is implemented using a first-order smoothing filter
        with a default time constant of 100 ms.
*/
class MonoGuard final
{
public:
    static constexpr int numBands = 24;

    MonoGuard() noexcept;
    ~MonoGuard() noexcept = default;

    /** Prepares the analyzer and filter banks. */
    void prepare (double sampleRate, int maxBlockSize) noexcept;

    /** Resets the smoothing filters and SVF states. */
    void reset() noexcept;

    /** Enables or disables the coherence analysis and warning path. */
    void setEnabled (bool shouldBeEnabled) noexcept;
    bool isEnabled() const noexcept { return enabled.load(); }

    /** Enables or disables the protection path. */
    void setProtectionEnabled (bool shouldBeEnabled) noexcept;
    bool isProtectionEnabled() const noexcept { return protectionEnabled.load(); }

    /** Sets the protection amount (0.0 to 100.0 %). */
    void setProtectionAmount (float amountPercent) noexcept;
    float getProtectionAmount() const noexcept { return protectionAmount.load(); }

    /** Returns the number of analysis bands. */
    int getNumBands() const noexcept { return numBands; }

    /** Returns the center frequency of a given band. */
    float getBandCenterFrequency (int bandIndex) const noexcept;

    /** Returns the smoothed correlation/coherence (rho) for a given band in range [-1, 1]. */
    float getRho (int bandIndex) const noexcept;

    /** Returns the raw coherence risk factor for a given band [0, 1]. */
    float getRisk (int bandIndex) const noexcept;

    /** Returns true if the band is currently in warning/risk state. */
    bool isBandAtRisk (int bandIndex) const noexcept;

    /** Returns a 32-bit bitmask representing the risk state of all bands. */
    uint32_t getRiskMask() const noexcept;

    /** Processes L/R stereo buffers in-place. */
    void process (float* left, float* right, int numSamples) noexcept;

    /** Processes Mid/Side stereo buffers in-place, operating on Side. */
    void processMS (float* mid, float* side, int numSamples) noexcept;

    /** Processes planar multi-channel buffers. */
    void process (float* const* channels, int numChannels, int numSamples) noexcept;
    void processMS (float* const* channels, int numChannels, int numSamples) noexcept;

    /** Processes JUCE AudioBuffer overloads. */
    void process (juce::AudioBuffer<float>& buffer) noexcept;
    void processMS (juce::AudioBuffer<float>& buffer) noexcept;

private:
    static constexpr float invSqrt2 = 0.7071067811865476f;
    static constexpr float eps = 1e-12f;

    std::atomic<bool> enabled { false };
    std::atomic<bool> protectionEnabled { false };
    std::atomic<float> protectionAmount { 50.0f }; // In percentage (0.0 to 100.0)

    double currentSampleRate = 44100.0;
    float alpha = 0.999f; // Smoothing coefficient

    std::array<TPTFilter, numBands> filters;
    std::array<float, numBands> centerFrequencies {};

    // Smoothing states for cross-power and auto-powers
    std::array<double, numBands> P_LR {};
    std::array<double, numBands> P_LL {};
    std::array<double, numBands> P_RR {};

    // Published outputs for lock-free GUI access
    std::array<std::atomic<float>, numBands> smoothedRho {};
    std::array<std::atomic<float>, numBands> smoothedRisk {};
    std::array<std::atomic<bool>, numBands> bandAtRisk {};

    void updateSmoothingCoeff() noexcept;
    void updateTargetGains (
        std::array<float, numBands>& targetGain) noexcept;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MonoGuard)
};

} // namespace ShredEQ::DSP
