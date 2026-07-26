#include "SidechainAnalyzer.h"

#include <algorithm>
#include <cmath>

namespace ShredEQ::DSP
{
namespace
{
constexpr double defaultSampleRate = 44100.0;
constexpr double minimumSampleRate = 1000.0;
constexpr double maximumSampleRate = 768000.0;
constexpr double lowestBandCenterHz = 50.0;
constexpr double bandsPerOctave = 2.0;
// Half-octave edge frequencies around each centre yield this constant Q.
constexpr double detectorQ = 2.028114981647472;
constexpr double powerEpsilon = 1.0e-12;
constexpr double stateFlushThreshold = 1.0e-24;
constexpr double powerReleaseSeconds = 0.080;
constexpr double maximumDetectorInput = 1.0e6;

[[nodiscard]] double finiteOr (double value, double fallback) noexcept
{
    return std::isfinite (value) ? value : fallback;
}

[[nodiscard]] float unitFinite (double value) noexcept
{
    if (! std::isfinite (value))
        return 0.0f;
    return static_cast<float> (std::clamp (value, 0.0, 1.0));
}

[[nodiscard]] double safeAudioSample (float value) noexcept
{
    if (! std::isfinite (value))
        return 0.0;
    return std::clamp (
        static_cast<double> (value),
        -maximumDetectorInput, maximumDetectorInput);
}
} // namespace

SidechainAnalyzer::SidechainAnalyzer() noexcept
{
    prepare (defaultSampleRate, 0);
}

void SidechainAnalyzer::prepare (
    double sampleRate, int maxBlockSize) noexcept
{
    static_cast<void> (maxBlockSize);
    currentSampleRate = std::clamp (
        finiteOr (sampleRate, defaultSampleRate),
        minimumSampleRate, maximumSampleRate);

    const auto maximumCenter =
        std::max (20.0, 0.45 * currentSampleRate);
    for (std::size_t band = 0; band < numBands; ++band)
    {
        const auto nominal =
            lowestBandCenterHz
            * std::pow (
                2.0, static_cast<double> (band) / bandsPerOctave);
        const auto center = std::min (nominal, maximumCenter);
        bandCentersHz[band] = static_cast<float> (center);
        detectorCoefficients[band] =
            BiquadCoefficients::makeSecondOrder (
                FilterKind::bandPass, currentSampleRate, center, detectorQ);
    }

    reset();
}

void SidechainAnalyzer::reset() noexcept
{
    mainFilterStates = {};
    sidechainFilterStates = {};
    smoothedMainPower = {};
    smoothedSidechainPower = {};
    nextSequence = 1;
    newestHistorySlot.store (0, std::memory_order_relaxed);
    validHistoryFrames.store (0, std::memory_order_relaxed);

    for (auto& slot : history)
    {
        slot.guard.store (0, std::memory_order_relaxed);
        slot.sequence.store (0, std::memory_order_relaxed);
        for (std::size_t band = 0; band < numBands; ++band)
        {
            slot.masking[band].store (0.0f, std::memory_order_relaxed);
            slot.dominant[band].store (false, std::memory_order_relaxed);
        }
    }

    publishZeroFrame();
    validHistoryFrames.store (0, std::memory_order_relaxed);
}

void SidechainAnalyzer::process (
    const float* const* mainChannels, int numMainChannels,
    const float* const* sidechainChannels, int numSidechainChannels,
    int numSamples) noexcept
{
    if (! enabled.load (std::memory_order_relaxed) || numSamples <= 0)
        return;

    if (mainChannels == nullptr || numMainChannels <= 0
        || sidechainChannels == nullptr || numSidechainChannels <= 0)
    {
        mainFilterStates = {};
        sidechainFilterStates = {};
        smoothedMainPower = {};
        smoothedSidechainPower = {};
        publishZeroFrame();
        return;
    }

    const auto mainCount = std::clamp (numMainChannels, 1, 2);
    const auto sidechainCount = std::clamp (numSidechainChannels, 1, 2);
    if (mainChannels[0] == nullptr || sidechainChannels[0] == nullptr)
    {
        mainFilterStates = {};
        sidechainFilterStates = {};
        smoothedMainPower = {};
        smoothedSidechainPower = {};
        publishZeroFrame();
        return;
    }

    BandValues blockMainPower {};
    BandValues blockSidechainPower {};

    for (int sample = 0; sample < numSamples; ++sample)
    {
        double main = safeAudioSample (mainChannels[0][sample]);
        double sidechain = safeAudioSample (sidechainChannels[0][sample]);
        if (mainCount > 1 && mainChannels[1] != nullptr)
            main =
                0.5 * (main + safeAudioSample (mainChannels[1][sample]));
        if (sidechainCount > 1 && sidechainChannels[1] != nullptr)
            sidechain =
                0.5 * (sidechain
                       + safeAudioSample (sidechainChannels[1][sample]));

        for (std::size_t band = 0; band < numBands; ++band)
        {
            const auto filteredMain = processFilter (
                main, detectorCoefficients[band], mainFilterStates[band]);
            const auto filteredSidechain = processFilter (
                sidechain, detectorCoefficients[band],
                sidechainFilterStates[band]);
            blockMainPower[band] +=
                static_cast<float> (filteredMain * filteredMain);
            blockSidechainPower[band] +=
                static_cast<float> (filteredSidechain * filteredSidechain);
        }
    }

    const auto inverseSamples = 1.0 / static_cast<double> (numSamples);
    const auto release = std::exp (
        -static_cast<double> (numSamples)
        / (powerReleaseSeconds * currentSampleRate));
    const auto gatePower = std::pow (
        10.0, static_cast<double> (
                  activityThresholdDb.load (std::memory_order_relaxed))
                  / 10.0);
    const auto gateFloor = gatePower * 0.25;

    BandValues overlap {};
    BandValues dominance {};
    BandValues masking {};
    DominantFlags dominant {};

    for (std::size_t band = 0; band < numBands; ++band)
    {
        const auto mainPower =
            static_cast<double> (blockMainPower[band]) * inverseSamples;
        const auto sidechainPower =
            static_cast<double> (blockSidechainPower[band]) * inverseSamples;

        smoothedMainPower[band] = static_cast<float> (
            std::max (mainPower,
                      release * static_cast<double> (
                                    smoothedMainPower[band])));
        smoothedSidechainPower[band] = static_cast<float> (
            std::max (sidechainPower,
                      release * static_cast<double> (
                                    smoothedSidechainPower[band])));

        const auto mainP =
            std::max (0.0, static_cast<double> (smoothedMainPower[band]));
        const auto sidechainP =
            std::max (0.0, static_cast<double> (
                               smoothedSidechainPower[band]));
        const auto overlapValue =
            std::min (mainP, sidechainP)
            / (std::max (mainP, sidechainP) + powerEpsilon);
        const auto dominanceValue =
            sidechainP / (mainP + sidechainP + powerEpsilon);
        const auto activity = std::clamp (
            (sidechainP - gateFloor)
                / std::max (powerEpsilon, gatePower - gateFloor),
            0.0, 1.0);

        overlap[band] = unitFinite (overlapValue);
        dominance[band] = unitFinite (dominanceValue);
        masking[band] = unitFinite (
            overlapValue * dominanceValue * activity);
        dominant[band] = sidechainP > mainP && activity > 0.0;
    }

    publishFrame (overlap, dominance, masking, dominant);
}

void SidechainAnalyzer::setEnabled (bool shouldBeEnabled) noexcept
{
    enabled.store (shouldBeEnabled, std::memory_order_relaxed);
}

bool SidechainAnalyzer::isEnabled() const noexcept
{
    return enabled.load (std::memory_order_relaxed);
}

void SidechainAnalyzer::setActivityThresholdDb (float thresholdDb) noexcept
{
    activityThresholdDb.store (
        std::clamp (
            std::isfinite (thresholdDb) ? thresholdDb : -72.0f,
            -120.0f, -12.0f),
        std::memory_order_relaxed);
}

float SidechainAnalyzer::getActivityThresholdDb() const noexcept
{
    return activityThresholdDb.load (std::memory_order_relaxed);
}

void SidechainAnalyzer::setCarveParameters (
    float maximumDepthDb, float scaleDb, float maskingThreshold) noexcept
{
    maximumCarveDepthDb.store (
        std::clamp (
            std::isfinite (maximumDepthDb) ? maximumDepthDb : 6.0f,
            0.0f, 18.0f),
        std::memory_order_relaxed);
    carveScaleDb.store (
        std::clamp (
            std::isfinite (scaleDb) ? scaleDb : 12.0f, 0.0f, 48.0f),
        std::memory_order_relaxed);
    carveThreshold.store (
        std::clamp (
            std::isfinite (maskingThreshold) ? maskingThreshold : 0.2f,
            0.0f, 1.0f),
        std::memory_order_relaxed);
}

float SidechainAnalyzer::getBandCenterHz (std::size_t band) const noexcept
{
    return band < numBands ? bandCentersHz[band] : 0.0f;
}

SidechainAnalyzer::BandValues
SidechainAnalyzer::getBandCentersHz() const noexcept
{
    return bandCentersHz;
}

SidechainAnalyzer::BandValues
SidechainAnalyzer::getLatestOverlap() const noexcept
{
    return readPublished (publishedOverlap);
}

SidechainAnalyzer::BandValues
SidechainAnalyzer::getLatestDominance() const noexcept
{
    return readPublished (publishedDominance);
}

SidechainAnalyzer::BandValues
SidechainAnalyzer::getLatestMasking() const noexcept
{
    return readPublished (publishedMasking);
}

SidechainAnalyzer::BandValues
SidechainAnalyzer::getLatestCarveRequestDb() const noexcept
{
    return readPublished (publishedCarveRequestDb);
}

SidechainAnalyzer::DominantFlags
SidechainAnalyzer::getLatestDominantSourceFlags() const noexcept
{
    DominantFlags result {};
    for (int attempt = 0; attempt < 4; ++attempt)
    {
        const auto before = latestGuard.load (std::memory_order_acquire);
        if ((before & 1U) != 0U)
            continue;
        for (std::size_t band = 0; band < numBands; ++band)
            result[band] =
                publishedDominant[band].load (std::memory_order_relaxed);
        if (before == latestGuard.load (std::memory_order_acquire))
            return result;
    }
    return {};
}

std::size_t SidechainAnalyzer::getCollisionHistorySize() const noexcept
{
    return validHistoryFrames.load (std::memory_order_acquire);
}

bool SidechainAnalyzer::getCollisionFrame (
    std::size_t age, CollisionFrame& destination) const noexcept
{
    const auto available =
        validHistoryFrames.load (std::memory_order_acquire);
    if (age >= available || age >= historyDepth)
        return false;

    const auto newest =
        newestHistorySlot.load (std::memory_order_acquire);
    const auto index = (newest + historyDepth - age) % historyDepth;
    const auto& slot = history[index];

    for (int attempt = 0; attempt < 4; ++attempt)
    {
        const auto before = slot.guard.load (std::memory_order_acquire);
        if ((before & 1U) != 0U)
            continue;
        for (std::size_t band = 0; band < numBands; ++band)
        {
            destination.masking[band] =
                slot.masking[band].load (std::memory_order_relaxed);
            destination.sidechainDominant[band] =
                slot.dominant[band].load (std::memory_order_relaxed);
        }
        destination.sequence =
            slot.sequence.load (std::memory_order_relaxed);
        if (before == slot.guard.load (std::memory_order_acquire))
            return true;
    }
    return false;
}

double SidechainAnalyzer::processFilter (
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

void SidechainAnalyzer::publishZeroFrame() noexcept
{
    publishFrame ({}, {}, {}, {});
}

void SidechainAnalyzer::publishFrame (
    const BandValues& overlap, const BandValues& dominance,
    const BandValues& masking, const DominantFlags& dominant) noexcept
{
    const auto maximumDepth =
        maximumCarveDepthDb.load (std::memory_order_relaxed);
    const auto scale = carveScaleDb.load (std::memory_order_relaxed);
    const auto threshold = carveThreshold.load (std::memory_order_relaxed);

    latestGuard.fetch_add (1, std::memory_order_acq_rel);
    for (std::size_t band = 0; band < numBands; ++band)
    {
        const auto safeMasking = std::clamp (masking[band], 0.0f, 1.0f);
        const auto carve = -std::min (
            maximumDepth,
            scale * std::max (0.0f, safeMasking - threshold));
        publishedOverlap[band].store (
            std::clamp (overlap[band], 0.0f, 1.0f),
            std::memory_order_relaxed);
        publishedDominance[band].store (
            std::clamp (dominance[band], 0.0f, 1.0f),
            std::memory_order_relaxed);
        publishedMasking[band].store (
            safeMasking, std::memory_order_relaxed);
        publishedCarveRequestDb[band].store (
            std::isfinite (carve) ? carve : 0.0f,
            std::memory_order_relaxed);
        publishedDominant[band].store (
            dominant[band], std::memory_order_relaxed);
    }
    latestGuard.fetch_add (1, std::memory_order_release);

    const auto previousCount =
        validHistoryFrames.load (std::memory_order_relaxed);
    const auto index =
        previousCount == 0
            ? std::size_t { 0 }
            : (newestHistorySlot.load (std::memory_order_relaxed) + 1)
                  % historyDepth;
    auto& slot = history[index];
    slot.guard.fetch_add (1, std::memory_order_acq_rel);
    for (std::size_t band = 0; band < numBands; ++band)
    {
        slot.masking[band].store (
            std::clamp (masking[band], 0.0f, 1.0f),
            std::memory_order_relaxed);
        slot.dominant[band].store (
            dominant[band], std::memory_order_relaxed);
    }
    slot.sequence.store (nextSequence++, std::memory_order_relaxed);
    slot.guard.fetch_add (1, std::memory_order_release);
    newestHistorySlot.store (index, std::memory_order_release);
    validHistoryFrames.store (
        std::min (historyDepth, previousCount + 1),
        std::memory_order_release);
}

SidechainAnalyzer::BandValues SidechainAnalyzer::readPublished (
    const std::array<std::atomic<float>, numBands>& values) const noexcept
{
    BandValues result {};
    for (int attempt = 0; attempt < 4; ++attempt)
    {
        const auto before = latestGuard.load (std::memory_order_acquire);
        if ((before & 1U) != 0U)
            continue;
        for (std::size_t band = 0; band < numBands; ++band)
            result[band] = values[band].load (std::memory_order_relaxed);
        if (before == latestGuard.load (std::memory_order_acquire))
            return result;
    }
    return {};
}

} // namespace ShredEQ::DSP
