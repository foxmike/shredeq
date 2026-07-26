#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include "TPTFilter.h"
#include <atomic>

namespace ShredEQ::DSP
{

/**
    MidSideRouter — Stage-level Mid/Side encoder/decoder matrix.

    CRITICAL ARCHITECTURAL CONSTRAINTS:
    1. This class is the SOLE stage-level M/S matrix for parametric and related
       processing stages. Encoding is performed ONCE around the parametric stage
       chain (before), and decoding is performed ONCE after.
    2. ParametricBand must NOT embed its own encode/decode matrices internally;
       it simply filters channels in whichever domain (L/R or M/S) is supplied to it.
    3. The M/S encoding uses energy-preserving, orthonormal scaling:
       - Mid  = (Left + Right) / sqrt(2)
       - Side = (Left - Right) / sqrt(2)
       And the decode path performs the exact inverse:
       - Left  = (Mid + Side) / sqrt(2)
       - Right = (Mid - Side) / sqrt(2)
       This pair is energy-preserving and has 0 samples of processing latency.
    4. Side High-Pass Filter:
       - A configurable cutoff high-pass filter in the 120-300 Hz range.
       - Applies only to the Side channel after encoding / before decoding.
       - Implemented via a modulation-stable topology-preserving transform (TPT)
         state-variable filter (SVF) to prevent zipper noise and instability.
       - The HPF has a true bypass when disabled or when the M/S path is inactive.
*/
class MidSideRouter final
{
public:
    MidSideRouter() noexcept;
    ~MidSideRouter() noexcept = default;

    /** Prepares the router and side HPF for playback. */
    void prepare (double sampleRate, int maxBlockSize) noexcept;

    /** Resets the side HPF states and internal tracking flags. */
    void reset() noexcept;

    /** Enables or disables the entire MidSideRouter (M/S matrixing & side HPF). */
    void setEnabled (bool shouldBeEnabled) noexcept;

    /** Returns true if the router is currently enabled. */
    bool isEnabled() const noexcept { return enabled.load(); }

    /** Enables or disables the side high-pass filter. */
    void setSideHighPassEnabled (bool shouldBeEnabled) noexcept;

    /** Returns true if the side high-pass filter is enabled. */
    bool isSideHighPassEnabled() const noexcept { return sideHighPassEnabled.load(); }

    /** Sets the side high-pass cutoff frequency in Hz (internally clamped to 120-300 Hz). */
    void setSideHighPassHz (float frequencyHz) noexcept;

    /** Returns the target side high-pass cutoff frequency in Hz. */
    float getSideHighPassHz() const noexcept;

    /** Returns whether the processed buffer is currently in the Mid/Side domain. */
    bool isMidSideDomain() const noexcept;

    /**
        Encodes Left/Right stereo to Mid/Side in-place.
        Applies side high-pass filtering to the Side channel if enabled.
    */
    void processEncode (float* left, float* right, int numSamples) noexcept;

    /**
        Decodes Mid/Side stereo back to Left/Right in-place.
    */
    void processDecode (float* left, float* right, int numSamples) noexcept;

    /** Multi-channel buffer process interfaces. */
    void processEncode (float* const* channels, int numChannels, int numSamples) noexcept;
    void processDecode (float* const* channels, int numChannels, int numSamples) noexcept;

    /** JUCE AudioBuffer overloads. */
    void processEncode (juce::AudioBuffer<float>& buffer) noexcept;
    void processDecode (juce::AudioBuffer<float>& buffer) noexcept;

    /**
        Single-sample encoding helper.
        Mid = (Left + Right) / sqrt(2)
        Side = (Left - Right) / sqrt(2)
    */
    static void encode (float& left, float& right) noexcept;

    /**
        Single-sample decoding helper.
        Left = (Mid + Side) / sqrt(2)
        Right = (Mid - Side) / sqrt(2)
    */
    static void decode (float& mid, float& side) noexcept;

private:
    static constexpr float invSqrt2 = 0.7071067811865476f;

    std::atomic<bool> enabled { false };
    std::atomic<bool> sideHighPassEnabled { false };
    std::atomic<float> targetFrequency { 150.0f };
    std::atomic<bool> inMidSideDomain { false };

    double currentSampleRate = 44100.0;

    juce::SmoothedValue<double, juce::ValueSmoothingTypes::Multiplicative> smoothedFrequency { 150.0 };
    TPTFilter sideHPF;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MidSideRouter)
};

} // namespace ShredEQ::DSP
