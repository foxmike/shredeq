#include "MaskingCarver.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <algorithm>
#include <cmath>

namespace ShredEQ::DSP
{
namespace
{
constexpr double defaultSampleRate = 44100.0;
constexpr double minimumSampleRate = 1000.0;
constexpr double maximumSampleRate = 768000.0;
constexpr double stateFlushThreshold = 1.0e-24;

[[nodiscard]] float finiteOr (float value, float fallback) noexcept
{
    return std::isfinite (value) ? value : fallback;
}
} // namespace

void MaskingCarver::prepare (
    double sampleRate, int maxBlockSize) noexcept
{
    static_cast<void> (maxBlockSize);
    currentSampleRate = std::clamp (
        std::isfinite (sampleRate) ? sampleRate : defaultSampleRate,
        minimumSampleRate, maximumSampleRate);
    reset();
}

void MaskingCarver::reset() noexcept
{
    for (auto& band : bands)
    {
        band.currentGainDb.store (0.0f, std::memory_order_relaxed);
        band.states = {};
    }
    for (auto& value : importedMasking)
        value.store (0.0f, std::memory_order_relaxed);
}

void MaskingCarver::process (
    float* const* channels, int numChannels, int numSamples) noexcept
{
    if (! enabled.load (std::memory_order_relaxed)
        || mode.load (std::memory_order_relaxed)
               != MaskingCarverMode::automatic
        || channels == nullptr || numChannels <= 0 || numSamples <= 0
        || channels[0] == nullptr)
        return;

    bool anyEnabled = false;
    for (const auto& band : bands)
        anyEnabled = anyEnabled
                     || band.enabled.load (std::memory_order_relaxed);
    if (! anyEnabled)
        return;

    const auto channelCount = std::min (numChannels, 2);
    const auto activePlacement = placement.load (std::memory_order_relaxed);
    const auto attackMs = attackTimeMs.load (std::memory_order_relaxed);
    const auto releaseMs = releaseTimeMs.load (std::memory_order_relaxed);

    for (auto& band : bands)
    {
        if (! band.enabled.load (std::memory_order_relaxed))
            continue;

        const auto sourceBand = std::min (
            band.analyzerBand.load (std::memory_order_relaxed),
            SidechainAnalyzer::numBands - 1);
        const auto targetGain = requestedGainDb (
            importedMasking[sourceBand].load (std::memory_order_relaxed));
        auto currentGain =
            band.currentGainDb.load (std::memory_order_relaxed);
        const auto timeMs =
            targetGain < currentGain ? attackMs : releaseMs;
        const auto smoothingCoefficient = static_cast<float> (
            1.0 - std::exp (
                      -1.0
                      / (0.001 * std::max (0.1f, timeMs)
                         * currentSampleRate)));
        const auto center = band.centerHz.load (std::memory_order_relaxed);
        const auto q = band.q.load (std::memory_order_relaxed);

        for (int sample = 0; sample < numSamples; ++sample)
        {
            currentGain +=
                smoothingCoefficient
                * (targetGain - currentGain);
            const auto coefficients =
                makeBell (center, q, currentGain);

            if (activePlacement == MaskingCarverPlacement::stereo
                || channelCount == 1)
            {
                channels[0][sample] = static_cast<float> (processFilter (
                    channels[0][sample], coefficients, band.states[0]));
                if (channelCount > 1 && channels[1] != nullptr)
                    channels[1][sample] = static_cast<float> (processFilter (
                        channels[1][sample], coefficients, band.states[1]));
                continue;
            }

            if (channels[1] == nullptr)
                continue;
            const auto left = static_cast<double> (channels[0][sample]);
            const auto right = static_cast<double> (channels[1][sample]);
            auto mid = 0.5 * (left + right);
            auto side = 0.5 * (left - right);
            if (activePlacement == MaskingCarverPlacement::midOnly)
                mid = processFilter (mid, coefficients, band.states[0]);
            else
                side = processFilter (side, coefficients, band.states[1]);
            channels[0][sample] = static_cast<float> (mid + side);
            channels[1][sample] = static_cast<float> (mid - side);
        }
        band.currentGainDb.store (currentGain, std::memory_order_relaxed);
    }
}

void MaskingCarver::process (juce::AudioBuffer<float>& buffer) noexcept
{
    process (buffer.getArrayOfWritePointers(), buffer.getNumChannels(),
             buffer.getNumSamples());
}

void MaskingCarver::setEnabled (bool shouldBeEnabled) noexcept
{
    enabled.store (shouldBeEnabled, std::memory_order_relaxed);
}

bool MaskingCarver::isEnabled() const noexcept
{
    return enabled.load (std::memory_order_relaxed);
}

void MaskingCarver::setMode (MaskingCarverMode newMode) noexcept
{
    mode.store (newMode, std::memory_order_relaxed);
}

MaskingCarverMode MaskingCarver::getMode() const noexcept
{
    return mode.load (std::memory_order_relaxed);
}

void MaskingCarver::setPlacement (
    MaskingCarverPlacement newPlacement) noexcept
{
    placement.store (newPlacement, std::memory_order_relaxed);
}

MaskingCarverPlacement MaskingCarver::getPlacement() const noexcept
{
    return placement.load (std::memory_order_relaxed);
}

void MaskingCarver::setCarveParameters (
    float newMaximumDepthDb, float newScaleDb,
    float newMaskingThreshold) noexcept
{
    maximumDepthDb.store (
        std::clamp (finiteOr (newMaximumDepthDb, 6.0f), 0.0f, 18.0f),
        std::memory_order_relaxed);
    scaleDb.store (
        std::clamp (finiteOr (newScaleDb, 12.0f), 0.0f, 48.0f),
        std::memory_order_relaxed);
    maskingThreshold.store (
        std::clamp (finiteOr (newMaskingThreshold, 0.2f), 0.0f, 1.0f),
        std::memory_order_relaxed);
}

void MaskingCarver::setAttackReleaseMs (
    float attackMs, float releaseMs) noexcept
{
    attackTimeMs.store (
        std::clamp (finiteOr (attackMs, 15.0f), 0.1f, 2000.0f),
        std::memory_order_relaxed);
    releaseTimeMs.store (
        std::clamp (finiteOr (releaseMs, 180.0f), 0.1f, 5000.0f),
        std::memory_order_relaxed);
}

void MaskingCarver::configureBand (
    std::size_t carverBand, std::size_t analyzerBand,
    float centerHz, float q) noexcept
{
    if (carverBand >= maxBands)
        return;
    auto& band = bands[carverBand];
    band.analyzerBand.store (
        std::min (analyzerBand, SidechainAnalyzer::numBands - 1),
        std::memory_order_relaxed);
    const auto maximumFrequency = static_cast<float> (
        std::max (20.0, currentSampleRate * 0.45));
    band.centerHz.store (
        std::clamp (finiteOr (centerHz, 1000.0f), 20.0f,
                    maximumFrequency),
        std::memory_order_relaxed);
    band.q.store (
        std::clamp (finiteOr (q, 1.4f), 0.25f, 12.0f),
        std::memory_order_relaxed);
}

void MaskingCarver::setBandEnabled (
    std::size_t carverBand, bool shouldBeEnabled) noexcept
{
    if (carverBand < maxBands)
        bands[carverBand].enabled.store (
            shouldBeEnabled, std::memory_order_relaxed);
}

bool MaskingCarver::isBandEnabled (
    std::size_t carverBand) const noexcept
{
    return carverBand < maxBands
           && bands[carverBand].enabled.load (std::memory_order_relaxed);
}

void MaskingCarver::importAnalyzerMetrics (
    const SidechainAnalyzer& analyzer) noexcept
{
    importAnalyzerMetrics (
        analyzer.getLatestMasking(), analyzer.getBandCentersHz());
}

void MaskingCarver::importAnalyzerMetrics (
    const SidechainAnalyzer::BandValues& masking,
    const SidechainAnalyzer::BandValues& bandCentersHz) noexcept
{
    for (std::size_t band = 0; band < SidechainAnalyzer::numBands; ++band)
    {
        importedMasking[band].store (
            std::clamp (finiteOr (masking[band], 0.0f), 0.0f, 1.0f),
            std::memory_order_relaxed);
    }
    refreshSuggestions (masking, bandCentersHz);
}

std::size_t MaskingCarver::getNumSuggestions() const noexcept
{
    return suggestionCount.load (std::memory_order_acquire);
}

MaskingCarver::Suggestion MaskingCarver::getSuggestion (
    std::size_t index) const noexcept
{
    if (index >= suggestionCount.load (std::memory_order_acquire))
        return {};
    for (int attempt = 0; attempt < 4; ++attempt)
    {
        const auto before =
            suggestionGuard.load (std::memory_order_acquire);
        if ((before & 1U) != 0U)
            continue;
        Suggestion result;
        result.centerHz =
            suggestions[index].centerHz.load (std::memory_order_relaxed);
        result.q = suggestions[index].q.load (std::memory_order_relaxed);
        result.suggestedDepthDb =
            suggestions[index].suggestedDepthDb.load (
                std::memory_order_relaxed);
        result.masking =
            suggestions[index].masking.load (std::memory_order_relaxed);
        result.analyzerBand =
            suggestions[index].analyzerBand.load (
                std::memory_order_relaxed);
        if (before == suggestionGuard.load (std::memory_order_acquire))
            return result;
    }
    return {};
}

bool MaskingCarver::updateSuggestion (
    std::size_t index, const Suggestion& suggestion) noexcept
{
    if (index >= suggestionCount.load (std::memory_order_acquire))
        return false;
    auto safe = suggestion;
    safe.centerHz = std::clamp (
        finiteOr (safe.centerHz, 1000.0f), 20.0f,
        static_cast<float> (std::max (20.0, 0.45 * currentSampleRate)));
    safe.q = std::clamp (finiteOr (safe.q, 1.4f), 0.25f, 12.0f);
    safe.suggestedDepthDb = std::clamp (
        finiteOr (safe.suggestedDepthDb, 0.0f), 0.0f,
        maximumDepthDb.load (std::memory_order_relaxed));
    safe.masking = std::clamp (
        finiteOr (safe.masking, 0.0f), 0.0f, 1.0f);
    safe.analyzerBand = std::min (
        safe.analyzerBand, SidechainAnalyzer::numBands - 1);
    suggestionGuard.fetch_add (1, std::memory_order_acq_rel);
    suggestions[index].centerHz.store (
        safe.centerHz, std::memory_order_relaxed);
    suggestions[index].q.store (safe.q, std::memory_order_relaxed);
    suggestions[index].suggestedDepthDb.store (
        safe.suggestedDepthDb, std::memory_order_relaxed);
    suggestions[index].masking.store (
        safe.masking, std::memory_order_relaxed);
    suggestions[index].analyzerBand.store (
        safe.analyzerBand, std::memory_order_relaxed);
    suggestionGuard.fetch_add (1, std::memory_order_release);
    return true;
}

bool MaskingCarver::acceptSuggestion (
    std::size_t suggestionIndex, std::size_t carverBand,
    bool enableBand) noexcept
{
    if (suggestionIndex >= getNumSuggestions() || carverBand >= maxBands)
        return false;
    const auto suggestion = getSuggestion (suggestionIndex);
    configureBand (
        carverBand, suggestion.analyzerBand,
        suggestion.centerHz, suggestion.q);
    setBandEnabled (carverBand, enableBand);
    return true;
}

void MaskingCarver::clearSuggestions() noexcept
{
    suggestionGuard.fetch_add (1, std::memory_order_acq_rel);
    for (auto& suggestion : suggestions)
    {
        suggestion.centerHz.store (1000.0f, std::memory_order_relaxed);
        suggestion.q.store (1.4f, std::memory_order_relaxed);
        suggestion.suggestedDepthDb.store (
            0.0f, std::memory_order_relaxed);
        suggestion.masking.store (0.0f, std::memory_order_relaxed);
        suggestion.analyzerBand.store (0, std::memory_order_relaxed);
    }
    suggestionCount.store (0, std::memory_order_relaxed);
    suggestionGuard.fetch_add (1, std::memory_order_release);
}

float MaskingCarver::getCurrentGainDb (
    std::size_t carverBand) const noexcept
{
    return carverBand < maxBands
               ? bands[carverBand].currentGainDb.load (
                     std::memory_order_relaxed)
               : 0.0f;
}

float MaskingCarver::requestedGainDb (float masking) const noexcept
{
    const auto safeMasking =
        std::clamp (finiteOr (masking, 0.0f), 0.0f, 1.0f);
    return -std::min (
        maximumDepthDb.load (std::memory_order_relaxed),
        scaleDb.load (std::memory_order_relaxed)
            * std::max (
                0.0f,
                safeMasking
                    - maskingThreshold.load (std::memory_order_relaxed)));
}

void MaskingCarver::refreshSuggestions (
    const SidechainAnalyzer::BandValues& masking,
    const SidechainAnalyzer::BandValues& centers) noexcept
{
    std::array<bool, SidechainAnalyzer::numBands> selected {};
    std::array<Suggestion, maxSuggestions> refreshed {};
    std::size_t count = 0;

    while (count < maxSuggestions)
    {
        std::size_t bestBand = SidechainAnalyzer::numBands;
        auto bestMasking =
            maskingThreshold.load (std::memory_order_relaxed);
        for (std::size_t band = 0; band < SidechainAnalyzer::numBands; ++band)
        {
            const auto value =
                std::clamp (finiteOr (masking[band], 0.0f), 0.0f, 1.0f);
            if (! selected[band] && value > bestMasking)
            {
                bestBand = band;
                bestMasking = value;
            }
        }
        if (bestBand == SidechainAnalyzer::numBands)
            break;

        selected[bestBand] = true;
        const auto requested = -requestedGainDb (bestMasking);
        if (requested <= 0.0f)
            continue;
        refreshed[count++] = {
            std::max (20.0f, finiteOr (centers[bestBand], 1000.0f)),
            1.4f,
            requested,
            bestMasking,
            bestBand
        };
    }

    suggestionGuard.fetch_add (1, std::memory_order_acq_rel);
    for (std::size_t index = 0; index < maxSuggestions; ++index)
    {
        suggestions[index].centerHz.store (
            refreshed[index].centerHz, std::memory_order_relaxed);
        suggestions[index].q.store (
            refreshed[index].q, std::memory_order_relaxed);
        suggestions[index].suggestedDepthDb.store (
            refreshed[index].suggestedDepthDb,
            std::memory_order_relaxed);
        suggestions[index].masking.store (
            refreshed[index].masking, std::memory_order_relaxed);
        suggestions[index].analyzerBand.store (
            refreshed[index].analyzerBand, std::memory_order_relaxed);
    }
    suggestionCount.store (count, std::memory_order_relaxed);
    suggestionGuard.fetch_add (1, std::memory_order_release);
}

BiquadCoefficients MaskingCarver::makeBell (
    float centerHz, float q, float gainDb) const noexcept
{
    return BiquadCoefficients::makeSecondOrder (
        FilterKind::bell, currentSampleRate,
        static_cast<double> (centerHz), static_cast<double> (q),
        static_cast<double> (std::clamp (
            gainDb,
            -maximumDepthDb.load (std::memory_order_relaxed), 0.0f)));
}

double MaskingCarver::processFilter (
    double input, const BiquadCoefficients& coefficients,
    FilterState& state) noexcept
{
    const auto output = coefficients.b0 * input + state.z1;
    state.z1 =
        coefficients.b1 * input - coefficients.a1 * output + state.z2;
    state.z2 = coefficients.b2 * input - coefficients.a2 * output;
    if (! std::isfinite (output) || ! std::isfinite (state.z1)
        || ! std::isfinite (state.z2))
    {
        state = {};
        return 0.0;
    }
    if (std::abs (state.z1) < stateFlushThreshold)
        state.z1 = 0.0;
    if (std::abs (state.z2) < stateFlushThreshold)
        state.z2 = 0.0;
    return output;
}

} // namespace ShredEQ::DSP
