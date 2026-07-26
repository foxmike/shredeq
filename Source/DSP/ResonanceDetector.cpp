#include "ResonanceDetector.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace ShredEQ::DSP
{
namespace
{
constexpr double defaultSampleRate = 44100.0;
constexpr double minimumSampleRate = 1000.0;
constexpr double maximumSampleRate = 768000.0;
constexpr float minimumSpectrumDb = -240.0f;
constexpr float maximumSpectrumDb = 60.0f;
constexpr float maximumUsefulExcessDb = 30.0f;
constexpr double octaveHalfWindowRatio = 1.122462048309373; // 2^(1/6)
constexpr float minimumCandidateScore = 0.018f;
constexpr float trustedPitchConfidence = 0.55f;
constexpr int maximumHarmonic = 16;

template <typename Value>
[[nodiscard]] Value finiteOr (Value value, Value fallback) noexcept
{
    return std::isfinite (value) ? value : fallback;
}

[[nodiscard]] float clamp01 (float value) noexcept
{
    return std::clamp (finiteOr (value, 0.0f), 0.0f, 1.0f);
}

[[nodiscard]] float confidenceForHarmonicRejection (
    const PitchResult& pitch) noexcept
{
    switch (pitch.state)
    {
        case PitchTrackingState::tracking:
        case PitchTrackingState::locked:
        case PitchTrackingState::hold:
            return pitch.confidence;

        case PitchTrackingState::searching:
        case PitchTrackingState::silent:
        case PitchTrackingState::lowConfidence:
            // This remains below the detector's pitch-trust knee, preventing a
            // stale or explicitly uncertain f0 from suppressing candidates.
            return std::min (pitch.confidence, 0.25f);
    }

    return 0.0f;
}
} // namespace

void ResonanceDetector::prepare (
    double sampleRate, int fftSizeOrMaxBins)
{
    preparedSampleRate = std::clamp (
        finiteOr (sampleRate, defaultSampleRate),
        minimumSampleRate, maximumSampleRate);

    const auto requested = std::clamp (
        fftSizeOrMaxBins, 2, maximumPreparedBins - 1);
    const auto capacity = static_cast<std::size_t> (requested) + 1;
    baselineDb.assign (capacity, minimumSpectrumDb);
    excessDb.assign (capacity, 0.0f);
    previousExcessDb.assign (capacity, 0.0f);
    persistence.assign (capacity, 0.0f);
    prefixSum.assign (capacity + 1, 0.0);
    reset();
}

void ResonanceDetector::reset() noexcept
{
    std::fill (baselineDb.begin(), baselineDb.end(), minimumSpectrumDb);
    std::fill (excessDb.begin(), excessDb.end(), 0.0f);
    std::fill (previousExcessDb.begin(), previousExcessDb.end(), 0.0f);
    std::fill (persistence.begin(), persistence.end(), 0.0f);
    std::fill (prefixSum.begin(), prefixSum.end(), 0.0);

    liveCandidates = {};
    scanSummary = {};
    scanSlots = {};
    scanSlotCount = 0;
    scanning = false;
    previousFundamentalHz = 0.0;
    previousPitchConfidence = 0.0f;
    previousFftSize = 0;
    previousNumBins = 0;
    previousSequence = 0;
    hasPreviousFrame = false;
}

void ResonanceDetector::setEnabled (bool shouldBeEnabled) noexcept
{
    enabled = shouldBeEnabled;
}

bool ResonanceDetector::isEnabled() const noexcept
{
    return enabled;
}

void ResonanceDetector::setPersistenceParameters (
    float newLambda, float newThresholdDb) noexcept
{
    persistenceLambda = std::clamp (
        finiteOr (newLambda, 0.92f), 0.0f, 0.9999f);
    excessThresholdDb = std::clamp (
        finiteOr (newThresholdDb, 4.0f), 0.0f, 30.0f);
}

void ResonanceDetector::setDiagnosticFrequencyRange (
    double minimumHz, double maximumHz) noexcept
{
    auto low = std::clamp (
        finiteOr (minimumHz, 150.0), 20.0, 20000.0);
    auto high = std::clamp (
        finiteOr (maximumHz, 16000.0), 20.0, 24000.0);
    if (low > high)
        std::swap (low, high);
    minimumDiagnosticHz = low;
    maximumDiagnosticHz = high;
}

bool ResonanceDetector::processControlRate (
    const Analysis::SpectralAnalysisEngine::Snapshot& snapshot,
    double fundamentalHz, float pitchConfidence) noexcept
{
    return analyzeSnapshot (snapshot, fundamentalHz, pitchConfidence);
}

bool ResonanceDetector::processControlRate (
    const Analysis::SpectralAnalysisEngine::Snapshot& snapshot,
    const PitchResult& pitch) noexcept
{
    return analyzeSnapshot (
        snapshot, pitch.frequencyHz,
        confidenceForHarmonicRejection (pitch));
}

bool ResonanceDetector::analyzeSnapshot (
    const Analysis::SpectralAnalysisEngine::Snapshot& snapshot,
    const PitchResult& pitch) noexcept
{
    return analyzeSnapshot (
        snapshot, pitch.frequencyHz,
        confidenceForHarmonicRejection (pitch));
}

bool ResonanceDetector::analyzeSnapshot (
    const Analysis::SpectralAnalysisEngine::Snapshot& snapshot,
    double fundamentalHz, float pitchConfidence) noexcept
{
    liveCandidates = {};

    const auto bins = snapshot.numBins;
    const auto sampleRate = finiteOr (snapshot.sampleRate, 0.0);
    if (! enabled || bins < 3 || snapshot.fftSize <= 0
        || sampleRate < minimumSampleRate
        || static_cast<std::size_t> (bins) > baselineDb.size()
        || snapshot.logMagnitudesDb.size()
               < static_cast<std::size_t> (bins)
        || prefixSum.size() <= static_cast<std::size_t> (bins))
        return false;

    if (snapshot.sequence != 0 && snapshot.sequence == previousSequence)
        return false;

    if (previousFftSize != 0
        && (previousFftSize != snapshot.fftSize
            || previousNumBins != bins
            || std::abs (preparedSampleRate - sampleRate) > 0.5))
        clearFrameState();

    preparedSampleRate = sampleRate;
    previousFftSize = snapshot.fftSize;
    previousNumBins = bins;
    liveCandidates.sourceSequence = snapshot.sequence;

    prefixSum[0] = 0.0;
    for (int bin = 0; bin < bins; ++bin)
    {
        const auto value = std::clamp (
            finiteOr (snapshot.logMagnitudesDb[
                          static_cast<std::size_t> (bin)],
                      minimumSpectrumDb),
            minimumSpectrumDb, maximumSpectrumDb);
        prefixSum[static_cast<std::size_t> (bin) + 1] =
            prefixSum[static_cast<std::size_t> (bin)]
            + static_cast<double> (value);
    }

    const auto binHz = sampleRate / static_cast<double> (snapshot.fftSize);
    for (int bin = 0; bin < bins; ++bin)
    {
        const auto frequency = static_cast<double> (bin) * binHz;
        auto lowBin = 0;
        auto highBin = std::min (bins - 1, bin + 1);
        if (frequency > 0.0)
        {
            lowBin = std::max (
                0, static_cast<int> (std::floor (
                       frequency / octaveHalfWindowRatio / binHz)));
            highBin = std::min (
                bins - 1, static_cast<int> (std::ceil (
                              frequency * octaveHalfWindowRatio / binHz)));
        }

        const auto count = std::max (1, highBin - lowBin + 1);
        const auto sum =
            prefixSum[static_cast<std::size_t> (highBin) + 1]
            - prefixSum[static_cast<std::size_t> (lowBin)];
        baselineDb[static_cast<std::size_t> (bin)] =
            static_cast<float> (sum / static_cast<double> (count));

        const auto spectrum = std::clamp (
            finiteOr (snapshot.logMagnitudesDb[
                          static_cast<std::size_t> (bin)],
                      minimumSpectrumDb),
            minimumSpectrumDb, maximumSpectrumDb);
        excessDb[static_cast<std::size_t> (bin)] = std::clamp (
            spectrum - baselineDb[static_cast<std::size_t> (bin)],
            -maximumUsefulExcessDb, maximumUsefulExcessDb);

        const auto hit =
            excessDb[static_cast<std::size_t> (bin)] > excessThresholdDb
                ? 1.0f
                : 0.0f;
        persistence[static_cast<std::size_t> (bin)] = clamp01 (
            persistenceLambda
                * persistence[static_cast<std::size_t> (bin)]
            + (1.0f - persistenceLambda) * hit);
    }

    const auto safeF0 =
        std::isfinite (fundamentalHz) && fundamentalHz >= 20.0
                && fundamentalHz <= 5000.0
            ? fundamentalHz
            : 0.0;
    const auto safePitchConfidence = clamp01 (pitchConfidence);
    const auto pitchTrust = safeF0 > 0.0
        ? clamp01 ((safePitchConfidence - 0.25f) / 0.65f)
        : 0.0f;
    const auto previousPitchTrusted =
        previousFundamentalHz > 0.0
        && previousPitchConfidence >= trustedPitchConfidence;

    const auto firstBin = std::max (
        1, static_cast<int> (std::ceil (minimumDiagnosticHz / binHz)));
    const auto lastBin = std::min (
        bins - 2,
        static_cast<int> (std::floor (
            std::min (maximumDiagnosticHz, sampleRate * 0.5) / binHz)));

    for (int bin = firstBin; bin <= lastBin; ++bin)
    {
        const auto index = static_cast<std::size_t> (bin);
        const auto excess = excessDb[index];
        if (excess <= excessThresholdDb
            || excess < excessDb[index - 1]
            || excess <= excessDb[index + 1])
            continue;

        const auto frequency = static_cast<double> (bin) * binHz;
        const auto neighbourMean =
            0.5f * (excessDb[index - 1] + excessDb[index + 1]);

        // Narrowness N: 0.2 baseline plus peak sharpness over about 8 dB.
        const auto narrowness = std::clamp (
            0.2f + 0.8f * (excess - neighbourMean) / 8.0f,
            0.2f, 1.0f);

        // W: broad guitar/cab emphasis without discarding lows or air.
        float perceptualWeight = 1.0f;
        if (frequency < 300.0)
            perceptualWeight = static_cast<float> (
                0.6 + 0.4 * (frequency - minimumDiagnosticHz)
                              / std::max (1.0, 300.0 - minimumDiagnosticHz));
        else if (frequency > 8000.0)
            perceptualWeight = static_cast<float> (
                1.0 - 0.35 * (frequency - 8000.0)
                                / std::max (1.0,
                                    maximumDiagnosticHz - 8000.0));
        perceptualWeight = std::clamp (perceptualWeight, 0.55f, 1.0f);

        // F: a fixed peak matches a nearby preceding-frame peak.
        float fixedMatch = hasPreviousFrame ? 0.0f : 0.55f;
        if (hasPreviousFrame)
        {
            for (int offset = -2; offset <= 2; ++offset)
            {
                const auto previousBin = bin + offset;
                if (previousBin < 1 || previousBin >= bins - 1)
                    continue;
                const auto previousIndex =
                    static_cast<std::size_t> (previousBin);
                const auto peakStrength = clamp01 (
                    (previousExcessDb[previousIndex]
                     - excessThresholdDb)
                    / 8.0f);
                const auto proximity =
                    1.0f - 0.22f * static_cast<float> (std::abs (offset));
                fixedMatch = std::max (
                    fixedMatch, peakStrength * proximity);
            }
        }
        const auto stability = 0.35f + 0.65f * clamp01 (fixedMatch);

        auto nearHarmonic = false;
        auto harmonicProximity = 0.0f;
        if (pitchTrust > 0.0f)
        {
            const auto harmonic =
                static_cast<int> (std::lround (frequency / safeF0));
            if (harmonic >= 1 && harmonic <= maximumHarmonic)
            {
                const auto expected = safeF0 * harmonic;
                const auto cents = std::abs (
                    1200.0 * std::log2 (frequency / expected));
                harmonicProximity = clamp01 (
                    static_cast<float> ((45.0 - cents) / 45.0));
                nearHarmonic = harmonicProximity > 0.0f;
            }
        }

        auto tracksPitch = false;
        auto motionEvidence = 0.0f;
        const auto f0Ratio =
            safeF0 > 0.0 && previousPitchTrusted
                ? safeF0 / previousFundamentalHz
                : 1.0;
        if (hasPreviousFrame && pitchTrust > 0.0f
            && previousPitchTrusted && f0Ratio > 0.0
            && std::abs (std::log2 (f0Ratio)) > 0.015)
        {
            const auto predictedPreviousFrequency = frequency / f0Ratio;
            const auto predictedBin = static_cast<int> (
                std::lround (predictedPreviousFrequency / binHz));
            if (predictedBin >= 1 && predictedBin < bins - 1
                && std::abs (predictedBin - bin) >= 1)
            {
                const auto predictedIndex =
                    static_cast<std::size_t> (predictedBin);
                motionEvidence = clamp01 (
                    (previousExcessDb[predictedIndex]
                     - excessThresholdDb)
                    / 8.0f);
                tracksPitch = motionEvidence > 0.15f;
            }
        }

        // Confident harmonic and motion evidence reduce, but never erase, a
        // real spectral excess. Low-confidence pitch leaves this near 1.
        // If f0 moved but this peak remained in the same bins, it is positive
        // evidence for a fixed cabinet resonance even if it happens to lie
        // near a harmonic of the new note.
        const auto crossNoteFixedEvidence =
            (previousPitchTrusted && pitchTrust > 0.0f
             && std::abs (std::log2 (f0Ratio)) > 0.015)
                ? clamp01 (fixedMatch - motionEvidence)
                : 0.0f;
        const auto harmonicPenalty =
            1.0f - pitchTrust * 0.48f * harmonicProximity
                           * (1.0f - 0.65f * crossNoteFixedEvidence);
        const auto motionPenalty =
            1.0f - pitchTrust * 0.45f * motionEvidence;
        const auto independence =
            std::clamp (harmonicPenalty * motionPenalty, 0.22f, 1.0f);

        const auto normalizedExcess = clamp01 (
            (excess - excessThresholdDb)
            / std::max (1.0f,
                maximumUsefulExcessDb - excessThresholdDb));
        const auto score = finiteOr (
            normalizedExcess * persistence[index] * narrowness
                * perceptualWeight * stability,
            0.0f);
        const auto confidence = clamp01 (
            score * independence
            + 0.08f * persistence[index] * independence
            + 0.08f * crossNoteFixedEvidence * persistence[index]);
        if (score < minimumCandidateScore || confidence <= 0.0f)
            continue;

        auto left = bin;
        auto right = bin;
        const auto halfHeight = std::max (
            excessThresholdDb, excess * 0.5f);
        while (left > firstBin && bin - left < 32
               && excessDb[static_cast<std::size_t> (left - 1)]
                      >= halfHeight)
            --left;
        while (right < lastBin && right - bin < 32
               && excessDb[static_cast<std::size_t> (right + 1)]
                      >= halfHeight)
            ++right;
        const auto bandwidth = std::max (
            binHz, static_cast<double> (right - left + 1) * binHz);

        ResonanceCandidate candidate;
        candidate.centerFrequencyHz = std::clamp (
            finiteOr (frequency, minimumDiagnosticHz),
            minimumDiagnosticHz, maximumDiagnosticHz);
        candidate.bandwidthHz = finiteOr (bandwidth, binHz);
        candidate.qHint = static_cast<float> (std::clamp (
            candidate.centerFrequencyHz
                / std::max (candidate.bandwidthHz, binHz),
            3.0, 40.0));
        candidate.excessDb = excess;
        candidate.persistence = persistence[index];
        candidate.score = score;
        candidate.confidence = confidence;
        candidate.nearDetectedHarmonic = nearHarmonic;
        candidate.tracksPitchMotion = tracksPitch;

        if ((nearHarmonic || tracksPitch) && pitchTrust > 0.25f)
            candidate.classification =
                ResonanceClassification::noteLinkedLowerConfidence;
        else if (frequency < 800.0)
            candidate.classification =
                ResonanceClassification::boxResonance;
        else if (frequency >= 4000.0
                 && persistence[index] < 0.72f)
            candidate.classification =
                ResonanceClassification::intermittentFizzCluster;
        else
            candidate.classification =
                ResonanceClassification::persistentCabinet;

        insertRanked (liveCandidates, candidate);
    }

    if (scanning)
    {
        for (std::size_t index = 0; index < liveCandidates.count; ++index)
            accumulateScan (liveCandidates.candidates[index]);
    }

    std::copy_n (
        excessDb.begin(), static_cast<std::size_t> (bins),
        previousExcessDb.begin());
    previousFundamentalHz = safeF0;
    previousPitchConfidence = safePitchConfidence;
    previousSequence = snapshot.sequence;
    hasPreviousFrame = true;
    return true;
}

const ResonanceDetector::CandidateList&
ResonanceDetector::getCandidates() const noexcept
{
    return liveCandidates;
}

void ResonanceDetector::beginScan() noexcept
{
    scanSlots = {};
    scanSummary = {};
    scanSlotCount = 0;
    scanning = true;
}

void ResonanceDetector::endScan() noexcept
{
    buildScanSummary();
    scanning = false;
}

bool ResonanceDetector::isScanning() const noexcept
{
    return scanning;
}

const ResonanceDetector::CandidateList&
ResonanceDetector::getScanSummary() const noexcept
{
    return scanSummary;
}

void ResonanceDetector::clearFrameState() noexcept
{
    std::fill (previousExcessDb.begin(), previousExcessDb.end(), 0.0f);
    std::fill (persistence.begin(), persistence.end(), 0.0f);
    previousFundamentalHz = 0.0;
    previousPitchConfidence = 0.0f;
    previousSequence = 0;
    hasPreviousFrame = false;
}

void ResonanceDetector::insertRanked (
    CandidateList& list, const ResonanceCandidate& candidate) noexcept
{
    auto position = std::size_t { 0 };
    while (position < list.count
           && list.candidates[position].confidence
                  >= candidate.confidence)
        ++position;

    if (position >= maximumCandidates)
        return;

    const auto newCount =
        std::min (maximumCandidates, list.count + 1);
    for (auto index = newCount - 1; index > position; --index)
        list.candidates[index] = list.candidates[index - 1];
    list.candidates[position] = candidate;
    list.count = newCount;
}

void ResonanceDetector::accumulateScan (
    const ResonanceCandidate& candidate) noexcept
{
    std::size_t slotIndex = scanSlotCount;
    for (std::size_t index = 0; index < scanSlotCount; ++index)
    {
        const auto& existing = scanSlots[index].representative;
        const auto mergeWidth = std::max (
            { existing.bandwidthHz, candidate.bandwidthHz,
              candidate.centerFrequencyHz * 0.018 });
        if (std::abs (existing.centerFrequencyHz
                      - candidate.centerFrequencyHz)
            <= mergeWidth)
        {
            slotIndex = index;
            break;
        }
    }

    if (slotIndex == scanSlotCount)
    {
        if (scanSlotCount < maximumScanSlots)
        {
            ++scanSlotCount;
        }
        else
        {
            slotIndex = 0;
            auto weakest = std::numeric_limits<float>::max();
            for (std::size_t index = 0; index < scanSlotCount; ++index)
            {
                const auto strength =
                    scanSlots[index].observations > 0
                        ? scanSlots[index].confidenceSum
                              / static_cast<float> (
                                  scanSlots[index].observations)
                        : 0.0f;
                if (strength < weakest)
                {
                    weakest = strength;
                    slotIndex = index;
                }
            }
            if (candidate.confidence <= weakest)
                return;
            scanSlots[slotIndex] = {};
        }
    }

    auto& slot = scanSlots[slotIndex];
    const auto weight = std::max (0.01f, candidate.confidence);
    slot.weightedFrequencySum += candidate.centerFrequencyHz * weight;
    slot.weightedBandwidthSum += candidate.bandwidthHz * weight;
    slot.confidenceSum += weight;
    slot.maximumExcessDb =
        std::max (slot.maximumExcessDb, candidate.excessDb);
    ++slot.observations;

    if (slot.observations == 1
        || candidate.confidence > slot.representative.confidence)
        slot.representative = candidate;

    slot.representative.centerFrequencyHz =
        slot.weightedFrequencySum / slot.confidenceSum;
    slot.representative.bandwidthHz =
        slot.weightedBandwidthSum / slot.confidenceSum;
    slot.representative.excessDb = slot.maximumExcessDb;
}

void ResonanceDetector::buildScanSummary() noexcept
{
    scanSummary = {};
    scanSummary.sourceSequence = liveCandidates.sourceSequence;

    std::uint32_t maximumObservations = 1;
    for (std::size_t index = 0; index < scanSlotCount; ++index)
        maximumObservations = std::max (
            maximumObservations, scanSlots[index].observations);

    for (std::size_t index = 0; index < scanSlotCount; ++index)
    {
        auto candidate = scanSlots[index].representative;
        const auto recurrence = std::sqrt (
            static_cast<float> (scanSlots[index].observations)
            / static_cast<float> (maximumObservations));
        candidate.persistence = clamp01 (
            std::max (candidate.persistence, recurrence));
        candidate.confidence = clamp01 (
            candidate.confidence * (0.55f + 0.45f * recurrence));
        candidate.score = clamp01 (
            candidate.score * (0.6f + 0.4f * recurrence));
        candidate.qHint = static_cast<float> (std::clamp (
            candidate.centerFrequencyHz
                / std::max (candidate.bandwidthHz, 1.0),
            3.0, 40.0));
        insertRanked (scanSummary, candidate);
    }
}

} // namespace ShredEQ::DSP
