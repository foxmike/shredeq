#include "ResonanceSuppressor.h"
#include <algorithm>
#include <cassert>

namespace ShredEQ::DSP
{

static constexpr double pi = 3.14159265358979323846;

SuppressorSlot::SuppressorSlot() noexcept
{
    reset();
}

void SuppressorSlot::prepare (double sampleRate) noexcept
{
    const auto validSampleRate = std::isfinite (sampleRate) && sampleRate > 300.0;
    const auto fs = validSampleRate ? sampleRate : 44100.0;

    smoothedFrequency.reset (fs, 0.02);
    smoothedQ.reset (fs, 0.02);
    smoothedFrequency.setCurrentAndTargetValue (targetFrequency);
    smoothedQ.setCurrentAndTargetValue (targetQ);

    lastFreq = 0.0;
    lastQ = 0.0;
    lastSampleRate = 0.0;

    calculateCoefficients (fs, targetFrequency, targetQ, currentDepthDb);
}

void SuppressorSlot::reset() noexcept
{
    channelStates[0].reset();
    channelStates[1].reset();
    currentDepthDb = 0.0;
    targetDynamicDepth = 0.0;
    smoothedFrequency.setCurrentAndTargetValue (targetFrequency);
    smoothedQ.setCurrentAndTargetValue (targetQ);
}

void SuppressorSlot::setFrequency (double newFreq) noexcept
{
    targetFrequency = std::clamp (newFreq, 150.0, 16000.0);
    smoothedFrequency.setTargetValue (targetFrequency);
}

void SuppressorSlot::setQ (double newQ) noexcept
{
    targetQ = std::clamp (newQ, 3.0, 40.0);
    smoothedQ.setTargetValue (targetQ);
}

void SuppressorSlot::loadFromCandidate (const ResonanceCandidate& candidate, bool dynamic) noexcept
{
    setEnabled (true);
    setFrequency (candidate.centerFrequencyHz);
    setQ (candidate.qHint);
    setMaxDepthDb (candidate.excessDb);
    setIsStatic (!dynamic);
    setConfidence (candidate.confidence);
    setFreeze (false);
    setSolo (false);
    setRemovedAudition (false);
    setAttackMs (10.0);
    setReleaseMs (200.0);
    setPersistenceRequirement (candidate.persistence * 100.0);
}

void SuppressorSlot::updateDepthTarget (const ResonanceDetector& detector) noexcept
{
    if (isStaticMode || !enabled)
    {
        targetDynamicDepth = 0.0;
        return;
    }

    const auto& candidates = detector.getCandidates();
    bool foundMatch = false;
    double bestExcess = 0.0;
    double bestPersistence = 0.0;

    for (const auto& candidate : candidates)
    {
        const double diff = std::abs (candidate.centerFrequencyHz - targetFrequency) / targetFrequency;
        if (diff < 0.10)
        {
            foundMatch = true;
            if (candidate.excessDb > bestExcess)
            {
                bestExcess = candidate.excessDb;
                bestPersistence = candidate.persistence;
            }
        }
    }

    if (foundMatch && (bestPersistence * 100.0 >= persistenceRequirement))
    {
        targetDynamicDepth = std::clamp (static_cast<double> (bestExcess), 0.0, targetMaxDepth);
    }
    else
    {
        targetDynamicDepth = 0.0;
    }
}

void SuppressorSlot::updateEnvelope (double sampleRate) noexcept
{
    if (freeze || !enabled)
        return;

    const double targetDepth = isStaticMode ? targetMaxDepth : targetDynamicDepth;
    const double timeMs = (targetDepth > currentDepthDb) ? attackMs : releaseMs;
    const double fs = sampleRate > 0.0 ? sampleRate : 44100.0;
    const double coeff = 1.0 - std::exp (-1.0 / (std::max (0.1, timeMs) * 0.001 * fs));

    currentDepthDb += coeff * (targetDepth - currentDepthDb);
}

void SuppressorSlot::calculateCoefficients (double sampleRate, double freq, double q, double depthDb) noexcept
{
    const bool freqOrQChanged = (freq < lastFreq || freq > lastFreq || q < lastQ || q > lastQ || sampleRate < lastSampleRate || sampleRate > lastSampleRate);

    if (freqOrQChanged)
    {
        lastFreq = freq;
        lastQ = q;
        lastSampleRate = sampleRate;

        const auto omega = 2.0 * pi * freq / sampleRate;
        cachedCosine = std::cos (omega);
        const auto sine = std::sin (omega);
        cachedAlpha = sine / (2.0 * q);
    }

    const auto amplitude = std::pow (10.0, -depthDb / 40.0);
    const auto inverseA0 = 1.0 / (1.0 + cachedAlpha / amplitude);

    b0 = (1.0 + cachedAlpha * amplitude) * inverseA0;
    b1 = (-2.0 * cachedCosine) * inverseA0;
    b2 = (1.0 - cachedAlpha * amplitude) * inverseA0;
    a1 = b1;
    a2 = (1.0 - cachedAlpha / amplitude) * inverseA0;
}

void SuppressorSlot::processSample (double& left, double& right, double sampleRate) noexcept
{
    if (!enabled)
        return;

    const double freq = smoothedFrequency.getNextValue();
    const double q = smoothedQ.getNextValue();

    calculateCoefficients (sampleRate, freq, q, currentDepthDb);

    if (currentDepthDb < 0.001)
        return;

    switch (placement)
    {
        case StereoRoutingMode::linked:
        case StereoRoutingMode::unlinked:
        {
            left = channelStates[0].process (left, b0, b1, b2, a1, a2);
            right = channelStates[1].process (right, b0, b1, b2, a1, a2);
            break;
        }
        case StereoRoutingMode::left:
        {
            left = channelStates[0].process (left, b0, b1, b2, a1, a2);
            break;
        }
        case StereoRoutingMode::right:
        {
            right = channelStates[1].process (right, b0, b1, b2, a1, a2);
            break;
        }
        case StereoRoutingMode::mid:
        {
            double mid = 0.5 * (left + right);
            double side = 0.5 * (left - right);
            mid = channelStates[0].process (mid, b0, b1, b2, a1, a2);
            left = mid + side;
            right = mid - side;
            break;
        }
        case StereoRoutingMode::side:
        {
            double mid = 0.5 * (left + right);
            double side = 0.5 * (left - right);
            side = channelStates[1].process (side, b0, b1, b2, a1, a2);
            left = mid + side;
            right = mid - side;
            break;
        }
    }
}

//------------------------------------------------------------------------------

ResonanceSuppressor::ResonanceSuppressor() noexcept
{
    reset();
}

void ResonanceSuppressor::prepare (double sampleRate) noexcept
{
    currentSampleRate = sampleRate;
    for (auto& slot : slots)
        slot.prepare (sampleRate);
    auditionFilter.prepare (sampleRate);
}

void ResonanceSuppressor::reset() noexcept
{
    for (auto& slot : slots)
        slot.reset();
    auditionFilter.reset();
    scanningActive = false;
    scanSampleCount = 0.0;
}

void ResonanceSuppressor::process (float* const* channels, int numChannels, int numSamples) noexcept
{
    if (!enabled || numChannels <= 0 || numSamples <= 0)
        return;

    // Check if any slot is actually active to avoid processing overhead
    bool anySlotActive = false;
    bool anySolo = false;
    bool anyRemovedAudition = false;

    if (auditionCardIndex >= 0 && auditionCardIndex < numCandidateCards)
    {
        anySlotActive = true;
    }
    else
    {
        for (const auto& slot : slots)
        {
            if (slot.isEnabled())
            {
                anySlotActive = true;
                if (slot.getSolo())
                    anySolo = true;
                if (slot.getRemovedAudition())
                    anyRemovedAudition = true;
            }
        }
    }

    if (!anySlotActive)
        return; // True bypass

    for (int s = 0; s < numSamples; ++s)
    {
        // Smoothly update slot envelopes
        if (auditionCardIndex < 0)
        {
            for (auto& slot : slots)
                slot.updateEnvelope (currentSampleRate);
        }
        else
        {
            auditionFilter.updateEnvelope (currentSampleRate);
        }

        double l = static_cast<double> (channels[0][s]);
        double r = (numChannels > 1) ? static_cast<double> (channels[1][s]) : 0.0;

        const double dryL = l;
        const double dryR = r;

        double wetL = l;
        double wetR = r;

        if (auditionCardIndex >= 0 && auditionCardIndex < numCandidateCards)
        {
            const auto cardIdx = static_cast<std::size_t> (auditionCardIndex);
            const auto& card = candidateCards[cardIdx].candidate;
            double depth = candidateCards[cardIdx].adjustedDepthDb >= 0.0
                           ? candidateCards[cardIdx].adjustedDepthDb
                           : card.excessDb;
            depth = std::clamp (depth, 0.0, 18.0);
            double freq = std::clamp (card.centerFrequencyHz, 150.0, 16000.0);
            double q = std::clamp (static_cast<double> (card.qHint), 3.0, 40.0);

            auditionFilter.setEnabled (true);
            auditionFilter.setFrequency (freq);
            auditionFilter.setQ (q);
            auditionFilter.setMaxDepthDb (depth);
            auditionFilter.setIsStatic (true);

            auditionFilter.processSample (wetL, wetR, currentSampleRate);

            if (auditionListenRemoved)
            {
                l = dryL - wetL;
                r = dryR - wetR;
            }
            else
            {
                l = wetL;
                r = wetR;
            }
        }
        else
        {
            for (auto& slot : slots)
            {
                if (slot.isEnabled())
                {
                    if (anySolo && !slot.getSolo())
                        continue;

                    slot.processSample (wetL, wetR, currentSampleRate);
                }
            }

            if (anyRemovedAudition)
            {
                l = dryL - wetL;
                r = dryR - wetR;
            }
            else
            {
                l = wetL;
                r = wetR;
            }
        }

        channels[0][s] = static_cast<float> (l);
        if (numChannels > 1)
            channels[1][s] = static_cast<float> (r);
    }
}

void ResonanceSuppressor::process (juce::AudioBuffer<float>& buffer) noexcept
{
    process (buffer.getArrayOfWritePointers(), buffer.getNumChannels(), buffer.getNumSamples());
}

void ResonanceSuppressor::startScan (double durationSeconds, double sampleRate, ResonanceDetector& detector) noexcept
{
    scanDurationSeconds = durationSeconds;
    scanTargetSamples = durationSeconds * sampleRate;
    scanSampleCount = 0.0;
    scanningActive = true;
    numCandidateCards = 0;

    detector.beginScan();
}

void ResonanceSuppressor::updateScan (int numSamples, ResonanceDetector& detector) noexcept
{
    if (!scanningActive)
        return;

    scanSampleCount += numSamples;
    if (scanSampleCount >= scanTargetSamples)
    {
        endScan (detector);
    }
}

void ResonanceSuppressor::endScan (ResonanceDetector& detector) noexcept
{
    if (!scanningActive)
        return;

    scanningActive = false;
    detector.endScan();

    const auto& summary = detector.getScanSummary();
    numCandidateCards = 0;

    for (std::size_t i = 0; i < summary.size() && numCandidateCards < maxScanCards; ++i)
    {
        const auto& cand = summary[i];

        bool isExcluded = false;
        for (int j = 0; j < numExcludedFrequencies; ++j)
        {
            if (std::abs (cand.centerFrequencyHz - excludedFrequencies[static_cast<std::size_t> (j)]) / excludedFrequencies[static_cast<std::size_t> (j)] < 0.05)
            {
                isExcluded = true;
                break;
            }
        }

        if (isExcluded)
            continue;

        CandidateCard card;
        card.candidate = cand;
        card.rejected = false;
        card.intentional = false;
        card.excluded = false;
        card.adjustedDepthDb = -1.0;

        candidateCards[static_cast<std::size_t> (numCandidateCards++)] = card;
    }
}

void ResonanceSuppressor::cancelScan() noexcept
{
    scanningActive = false;
    scanSampleCount = 0.0;
}

double ResonanceSuppressor::getScanProgress() const noexcept
{
    if (!scanningActive || scanTargetSamples <= 0.0)
        return 0.0;
    return std::clamp (scanSampleCount / scanTargetSamples, 0.0, 1.0);
}

ResonanceSuppressor::CandidateCard ResonanceSuppressor::getCandidateCard (int index) const noexcept
{
    if (index >= 0 && index < numCandidateCards)
        return candidateCards[static_cast<std::size_t> (index)];
    return {};
}

void ResonanceSuppressor::rejectCandidate (int index) noexcept
{
    if (index >= 0 && index < numCandidateCards)
        candidateCards[static_cast<std::size_t> (index)].rejected = true;
}

void ResonanceSuppressor::classifyIntentional (int index) noexcept
{
    if (index >= 0 && index < numCandidateCards)
        candidateCards[static_cast<std::size_t> (index)].intentional = true;
}

void ResonanceSuppressor::excludeCandidate (int index) noexcept
{
    if (index >= 0 && index < numCandidateCards)
    {
        candidateCards[static_cast<std::size_t> (index)].excluded = true;
        if (numExcludedFrequencies < maxExclusions)
        {
            excludedFrequencies[static_cast<std::size_t> (numExcludedFrequencies++)] =
                candidateCards[static_cast<std::size_t> (index)].candidate.centerFrequencyHz;
        }
    }
}

void ResonanceSuppressor::adjustCandidateDepth (int index, double newDepthDb) noexcept
{
    if (index >= 0 && index < numCandidateCards)
        candidateCards[static_cast<std::size_t> (index)].adjustedDepthDb = newDepthDb;
}

void ResonanceSuppressor::applyCandidate (int slotIndex, const ResonanceCandidate& candidate, bool dynamic) noexcept
{
    if (slotIndex >= 0 && slotIndex < numSlots)
    {
        slots[static_cast<std::size_t> (slotIndex)].loadFromCandidate (candidate, dynamic);
        slots[static_cast<std::size_t> (slotIndex)].prepare (currentSampleRate);
    }
}

void ResonanceSuppressor::setAuditionCard (int index, bool listenRemoved) noexcept
{
    auditionCardIndex = index;
    auditionListenRemoved = listenRemoved;
    if (index >= 0 && index < numCandidateCards)
    {
        auditionFilter.reset();
        auditionFilter.prepare (currentSampleRate);
    }
}

} // namespace ShredEQ::DSP
