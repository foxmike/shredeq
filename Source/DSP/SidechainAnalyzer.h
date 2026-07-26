#pragma once

#include "BiquadCoefficients.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace ShredEQ::DSP
{

/**
    Lightweight external-sidechain masking analyzer.

    Sixteen half-octave-spaced band-pass detectors cover approximately 50 Hz
    to 9 kHz (subject to Nyquist). All storage is fixed and all filter
    coefficients are created by prepare(); process() allocates nothing.
*/
class SidechainAnalyzer final
{
public:
    static constexpr std::size_t numBands = 16;
    static constexpr std::size_t historyDepth = 96;

    using BandValues = std::array<float, numBands>;
    using DominantFlags = std::array<bool, numBands>;

    struct CollisionFrame
    {
        BandValues masking {};
        DominantFlags sidechainDominant {};
        std::uint64_t sequence = 0;
    };

    SidechainAnalyzer() noexcept;

    void prepare (double sampleRate, int maxBlockSize) noexcept;
    void reset() noexcept;

    /**
        Accumulates linked main/sidechain power for one block. Missing channel
        pointers, zero sidechain channels, and silent sidechain blocks publish
        zero masking and zero carve requests.
    */
    void process (const float* const* mainChannels, int numMainChannels,
                  const float* const* sidechainChannels,
                  int numSidechainChannels, int numSamples) noexcept;

    void setEnabled (bool shouldBeEnabled) noexcept;
    [[nodiscard]] bool isEnabled() const noexcept;

    /** Sidechain gate transition in dBFS RMS (default -72 dBFS). */
    void setActivityThresholdDb (float thresholdDb) noexcept;
    [[nodiscard]] float getActivityThresholdDb() const noexcept;

    /** Parameters used by the analyzer's requested G_carve snapshot. */
    void setCarveParameters (float maximumDepthDb, float scaleDb,
                             float maskingThreshold) noexcept;

    [[nodiscard]] float getBandCenterHz (std::size_t band) const noexcept;
    [[nodiscard]] BandValues getBandCentersHz() const noexcept;
    [[nodiscard]] BandValues getLatestOverlap() const noexcept;
    [[nodiscard]] BandValues getLatestDominance() const noexcept;
    [[nodiscard]] BandValues getLatestMasking() const noexcept;
    [[nodiscard]] BandValues getLatestCarveRequestDb() const noexcept;
    [[nodiscard]] DominantFlags getLatestDominantSourceFlags() const noexcept;

    [[nodiscard]] std::size_t getCollisionHistorySize() const noexcept;
    /** age 0 is newest. Returns false when that much history is unavailable. */
    [[nodiscard]] bool getCollisionFrame (
        std::size_t age, CollisionFrame& destination) const noexcept;

private:
    struct FilterState
    {
        double z1 = 0.0;
        double z2 = 0.0;
    };

    struct HistorySlot
    {
        std::array<std::atomic<float>, numBands> masking {};
        std::array<std::atomic<bool>, numBands> dominant {};
        std::atomic<std::uint64_t> sequence { 0 };
        std::atomic<std::uint64_t> guard { 0 };
    };

    [[nodiscard]] static double processFilter (
        double input, const BiquadCoefficients& coefficients,
        FilterState& state) noexcept;
    void publishZeroFrame() noexcept;
    void publishFrame (const BandValues& overlap,
                       const BandValues& dominance,
                       const BandValues& masking,
                       const DominantFlags& dominant) noexcept;
    [[nodiscard]] BandValues readPublished (
        const std::array<std::atomic<float>, numBands>& values) const noexcept;

    std::atomic<bool> enabled { true };
    std::atomic<float> activityThresholdDb { -72.0f };
    std::atomic<float> maximumCarveDepthDb { 6.0f };
    std::atomic<float> carveScaleDb { 12.0f };
    std::atomic<float> carveThreshold { 0.2f };

    double currentSampleRate = 44100.0;
    std::array<float, numBands> bandCentersHz {};
    std::array<BiquadCoefficients, numBands> detectorCoefficients {};
    std::array<FilterState, numBands> mainFilterStates {};
    std::array<FilterState, numBands> sidechainFilterStates {};
    BandValues smoothedMainPower {};
    BandValues smoothedSidechainPower {};

    std::array<std::atomic<float>, numBands> publishedOverlap {};
    std::array<std::atomic<float>, numBands> publishedDominance {};
    std::array<std::atomic<float>, numBands> publishedMasking {};
    std::array<std::atomic<float>, numBands> publishedCarveRequestDb {};
    std::array<std::atomic<bool>, numBands> publishedDominant {};
    std::atomic<std::uint64_t> latestGuard { 0 };

    std::array<HistorySlot, historyDepth> history {};
    std::atomic<std::size_t> newestHistorySlot { 0 };
    std::atomic<std::size_t> validHistoryFrames { 0 };
    std::uint64_t nextSequence = 1;
};

} // namespace ShredEQ::DSP
