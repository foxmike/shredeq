#pragma once

#include "BiquadCoefficients.h"

#include <array>
#include <atomic>
#include <cstddef>

namespace juce
{
template <typename SampleType>
class AudioBuffer;
}

namespace ShredEQ::DSP
{

/**
    Four-zone, fourth-order Linkwitz-Riley crossover.

    The caller owns the four output buffers. This keeps preparation independent
    of the host channel layout and guarantees that processSplit() does not
    allocate. Output buffers must be distinct, already sized, and may not alias
    one another.

    The three splits form a phase-compensated serial tree. Later split
    all-passes are applied to earlier branches, so summing unity-gain zones has
    flat magnitude (with the causal, frequency-dependent phase of the combined
    all-pass network). Reconstruction is intentionally not sample-identical to
    an unfiltered dry signal.
*/
class MultibandCrossover final
{
public:
    enum class Zone : std::size_t
    {
        weight = 0,
        body,
        bite,
        fizzAir,
        count
    };

    static constexpr std::size_t numberOfZones =
        static_cast<std::size_t> (Zone::count);
    static constexpr std::size_t numberOfCrossovers = 3;
    static constexpr std::size_t maximumChannels = 8;

    static constexpr std::array<double, numberOfCrossovers>
        defaultCrossoverHz { 140.0, 900.0, 4500.0 };
    static constexpr std::array<double, numberOfCrossovers>
        minimumCrossoverHz { 40.0, 150.0, 1000.0 };
    static constexpr std::array<double, numberOfCrossovers>
        maximumCrossoverHz { 500.0, 3000.0, 12000.0 };

    MultibandCrossover() noexcept = default;

    void prepare (double sampleRate, int maxBlockSize) noexcept;
    void reset() noexcept;

    /**
        Splits mono or multichannel floating-point input into Weight, Body,
        Bite, and Fizz/Air. Returns false without processing when arguments are
        invalid or numChannels exceeds maximumChannels.
    */
    [[nodiscard]] bool processSplit (
        const float* const* inputChannels, int numChannels, int numSamples,
        float* const* weightChannels, float* const* bodyChannels,
        float* const* biteChannels, float* const* fizzAirChannels) noexcept;

    [[nodiscard]] bool processSplit (
        const double* const* inputChannels, int numChannels, int numSamples,
        double* const* weightChannels, double* const* bodyChannels,
        double* const* biteChannels, double* const* fizzAirChannels) noexcept;

    [[nodiscard]] bool processSplit (
        const juce::AudioBuffer<float>& input,
        std::array<juce::AudioBuffer<float>*, numberOfZones> zones) noexcept;

    [[nodiscard]] bool processSplit (
        const juce::AudioBuffer<double>& input,
        std::array<juce::AudioBuffer<double>*, numberOfZones> zones) noexcept;

    /** Convenience alias for engines that name their split operation process. */
    [[nodiscard]] bool process (
        const juce::AudioBuffer<float>& input,
        std::array<juce::AudioBuffer<float>*, numberOfZones> zones) noexcept
    {
        return processSplit (input, zones);
    }

    [[nodiscard]] bool process (
        const juce::AudioBuffer<double>& input,
        std::array<juce::AudioBuffer<double>*, numberOfZones> zones) noexcept
    {
        return processSplit (input, zones);
    }

    /** Sums four processed zone buffers into an already-sized output buffer. */
    [[nodiscard]] static bool sumZones (
        const float* const* weightChannels,
        const float* const* bodyChannels,
        const float* const* biteChannels,
        const float* const* fizzAirChannels,
        float* const* outputChannels, int numChannels,
        int numSamples) noexcept;

    [[nodiscard]] static bool sumZones (
        const double* const* weightChannels,
        const double* const* bodyChannels,
        const double* const* biteChannels,
        const double* const* fizzAirChannels,
        double* const* outputChannels, int numChannels,
        int numSamples) noexcept;

    [[nodiscard]] static bool sumZones (
        const std::array<const juce::AudioBuffer<float>*, numberOfZones>& zones,
        juce::AudioBuffer<float>& output) noexcept;

    [[nodiscard]] static bool sumZones (
        const std::array<const juce::AudioBuffer<double>*, numberOfZones>& zones,
        juce::AudioBuffer<double>& output) noexcept;

    void setCrossoverHz (std::size_t crossoverIndex,
                         double frequencyHz) noexcept;
    void setCrossoversHz (
        double weightBodyHz, double bodyBiteHz,
        double biteFizzHz) noexcept;

    void setWeightBodyCrossoverHz (double frequencyHz) noexcept;
    void setBodyBiteCrossoverHz (double frequencyHz) noexcept;
    void setBiteFizzCrossoverHz (double frequencyHz) noexcept;

    [[nodiscard]] double getTargetCrossoverHz (
        std::size_t crossoverIndex) const noexcept;
    [[nodiscard]] double getEffectiveCrossoverHz (
        std::size_t crossoverIndex) const noexcept;
    [[nodiscard]] std::array<double, numberOfCrossovers>
        getEffectiveCrossoversHz() const noexcept;

private:
    struct SectionState
    {
        double s1 = 0.0;
        double s2 = 0.0;
    };

    struct LR4State
    {
        std::array<SectionState, 2> lowPass {};
        std::array<SectionState, 2> highPass {};
    };

    struct AllPassState
    {
        SectionState section;
    };

    struct ChannelState
    {
        std::array<LR4State, numberOfCrossovers> splits {};
        AllPassState weightAtSecondSplit;
        AllPassState weightAtThirdSplit;
        AllPassState bodyAtThirdSplit;
    };

    struct FrequencySmoother
    {
        void reset (double value) noexcept;
        void setTarget (double value, int durationSamples) noexcept;
        [[nodiscard]] double next() noexcept;

        double current = 0.0;
        double target = 0.0;
        double multiplier = 1.0;
        int remaining = 0;
    };

    struct SplitCoefficients
    {
        BiquadCoefficients lowPass;
        BiquadCoefficients highPass;
        BiquadCoefficients allPass;
    };

    template <typename SampleType>
    [[nodiscard]] bool processSplitImpl (
        const SampleType* const* inputChannels,
        int numChannels, int numSamples,
        SampleType* const* weightChannels,
        SampleType* const* bodyChannels,
        SampleType* const* biteChannels,
        SampleType* const* fizzAirChannels) noexcept;

    template <typename SampleType>
    [[nodiscard]] static bool sumZonesImpl (
        const SampleType* const* weightChannels,
        const SampleType* const* bodyChannels,
        const SampleType* const* biteChannels,
        const SampleType* const* fizzAirChannels,
        SampleType* const* outputChannels,
        int numChannels, int numSamples) noexcept;

    [[nodiscard]] static double processSection (
        double input, const BiquadCoefficients& coefficients,
        SectionState& state) noexcept;
    [[nodiscard]] static double processPair (
        double input, const BiquadCoefficients& coefficients,
        std::array<SectionState, 2>& states) noexcept;
    [[nodiscard]] static BiquadCoefficients makeAllPass (
        const BiquadCoefficients& butterworthSection) noexcept;

    void updateCoefficients (
        const std::array<double, numberOfCrossovers>& frequencies) noexcept;
    [[nodiscard]] std::array<double, numberOfCrossovers>
        orderedTargets() const noexcept;
    [[nodiscard]] int smoothingSamples() const noexcept;

    static constexpr double butterworthQ = 0.7071067811865475244;
    static constexpr double orderingGuardHz = 1.0;
    static constexpr double smoothingTimeSeconds = 0.02;
    static constexpr double stateFlushThreshold = 1.0e-24;

    std::array<std::atomic<double>, numberOfCrossovers> targetFrequencies {
        std::atomic<double> { defaultCrossoverHz[0] },
        std::atomic<double> { defaultCrossoverHz[1] },
        std::atomic<double> { defaultCrossoverHz[2] }
    };
    std::array<std::atomic<double>, numberOfCrossovers> effectiveFrequencies {
        std::atomic<double> { defaultCrossoverHz[0] },
        std::atomic<double> { defaultCrossoverHz[1] },
        std::atomic<double> { defaultCrossoverHz[2] }
    };

    std::array<FrequencySmoother, numberOfCrossovers> frequencySmoothers;
    std::array<SplitCoefficients, numberOfCrossovers> coefficients;
    std::array<double, numberOfCrossovers> configuredFrequencies {};
    std::array<ChannelState, maximumChannels> channelStates;

    double currentSampleRate = 44100.0;
};

} // namespace ShredEQ::DSP
