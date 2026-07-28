#pragma once

#include "BiquadCoefficients.h"
#include "SidechainAnalyzer.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace juce
{
template <typename SampleType>
class AudioBuffer;
}

namespace ShredEQ::DSP
{

enum class MaskingCarverMode
{
    suggestion = 0,
    automatic
};

enum class MaskingCarverPlacement
{
    stereo = 0,
    midOnly,
    sideOnly
};

/**
    Bounded sidechain carve processor. Six bells are available, but automatic
    mode never assigns or enables them: the host/user must explicitly configure
    and enable every audible carver band.
*/
class MaskingCarver final
{
public:
    static constexpr std::size_t maxBands = 6;
    static constexpr std::size_t maxSuggestions = 6;

    struct Suggestion
    {
        float centerHz = 1000.0f;
        float q = 1.4f;
        float suggestedDepthDb = 0.0f;
        float masking = 0.0f;
        std::size_t analyzerBand = 0;
    };

    MaskingCarver() noexcept = default;

    void prepare (double sampleRate, int maxBlockSize) noexcept;
    void reset() noexcept;

    void process (float* const* channels, int numChannels,
                  int numSamples) noexcept;
    void process (juce::AudioBuffer<float>& buffer) noexcept;

    void setEnabled (bool shouldBeEnabled) noexcept;
    [[nodiscard]] bool isEnabled() const noexcept;
    void setMode (MaskingCarverMode newMode) noexcept;
    [[nodiscard]] MaskingCarverMode getMode() const noexcept;
    void setPlacement (MaskingCarverPlacement newPlacement) noexcept;
    [[nodiscard]] MaskingCarverPlacement getPlacement() const noexcept;

    /** G = -min(maximumDepthDb, scaleDb * max(0, masking-threshold)). */
    void setCarveParameters (float maximumDepthDb, float scaleDb,
                             float maskingThreshold) noexcept;
    void setMixAmount (float amountPercent) noexcept;
    void setAttackReleaseMs (float attackMs, float releaseMs) noexcept;

    void configureBand (std::size_t carverBand, std::size_t analyzerBand,
                        float centerHz, float q) noexcept;
    void setBandEnabled (std::size_t carverBand,
                         bool shouldBeEnabled) noexcept;
    [[nodiscard]] bool isBandEnabled (
        std::size_t carverBand) const noexcept;

    /** Copies analyzer metrics into fixed storage and refreshes suggestions. */
    void importAnalyzerMetrics (const SidechainAnalyzer& analyzer) noexcept;
    void importAnalyzerMetrics (
        const SidechainAnalyzer::BandValues& masking,
        const SidechainAnalyzer::BandValues& bandCentersHz) noexcept;

    [[nodiscard]] std::size_t getNumSuggestions() const noexcept;
    [[nodiscard]] Suggestion getSuggestion (std::size_t index) const noexcept;
    bool updateSuggestion (std::size_t index,
                           const Suggestion& suggestion) noexcept;
    /** Explicitly accepts one descriptor into a user-selected carver band. */
    bool acceptSuggestion (std::size_t suggestionIndex,
                           std::size_t carverBand,
                           bool enableBand = true) noexcept;
    void clearSuggestions() noexcept;

    [[nodiscard]] float getCurrentGainDb (
        std::size_t carverBand) const noexcept;

private:
    struct FilterState
    {
        double z1 = 0.0;
        double z2 = 0.0;
    };

    struct CarverBand
    {
        std::atomic<bool> enabled { false };
        std::atomic<std::size_t> analyzerBand { 0 };
        std::atomic<float> centerHz { 1000.0f };
        std::atomic<float> q { 1.4f };
        std::atomic<float> currentGainDb { 0.0f };
        std::array<FilterState, 2> states {};
    };

    struct SuggestionSlot
    {
        std::atomic<float> centerHz { 1000.0f };
        std::atomic<float> q { 1.4f };
        std::atomic<float> suggestedDepthDb { 0.0f };
        std::atomic<float> masking { 0.0f };
        std::atomic<std::size_t> analyzerBand { 0 };
    };

    [[nodiscard]] float requestedGainDb (float masking) const noexcept;
    void refreshSuggestions (
        const SidechainAnalyzer::BandValues& masking,
        const SidechainAnalyzer::BandValues& centers) noexcept;
    [[nodiscard]] BiquadCoefficients makeBell (
        float centerHz, float q, float gainDb) const noexcept;
    [[nodiscard]] static double processFilter (
        double input, const BiquadCoefficients& coefficients,
        FilterState& state) noexcept;

    std::atomic<bool> enabled { false };
    std::atomic<MaskingCarverMode> mode {
        MaskingCarverMode::suggestion
    };
    std::atomic<MaskingCarverPlacement> placement {
        MaskingCarverPlacement::stereo
    };
    std::atomic<float> maximumDepthDb { 6.0f };
    std::atomic<float> scaleDb { 12.0f };
    std::atomic<float> maskingThreshold { 0.2f };
    std::atomic<float> mixAmount { 1.0f };
    std::atomic<float> attackTimeMs { 15.0f };
    std::atomic<float> releaseTimeMs { 180.0f };

    double currentSampleRate = 44100.0;
    std::array<CarverBand, maxBands> bands {};
    std::array<std::atomic<float>, SidechainAnalyzer::numBands>
        importedMasking {};

    std::array<SuggestionSlot, maxSuggestions> suggestions {};
    std::atomic<std::size_t> suggestionCount { 0 };
    std::atomic<std::uint64_t> suggestionGuard { 0 };
};

} // namespace ShredEQ::DSP
