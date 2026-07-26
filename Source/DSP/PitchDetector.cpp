#include "PitchDetector.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace ShredEQ::DSP
{
namespace
{
constexpr double twoPi = 6.283185307179586476925286766559;

[[nodiscard]] double finiteOr (double value, double fallback) noexcept
{
    return std::isfinite (value) ? value : fallback;
}

[[nodiscard]] std::size_t nextPowerOfTwo (std::size_t value) noexcept
{
    std::size_t result = 1;
    while (result < value
           && result <= std::numeric_limits<std::size_t>::max() / 2)
        result *= 2;
    return result;
}
} // namespace

void PitchDetector::prepare (double sampleRate, int maxBlockSize)
{
    inputSampleRate = std::clamp (
        finiteOr (sampleRate, defaultSampleRate),
        minimumSupportedSampleRate, maximumSupportedSampleRate);

    downsampleFactor = std::max (
        1, static_cast<int> (
               std::ceil (inputSampleRate / targetAnalysisSampleRate)));
    effectiveSampleRate =
        inputSampleRate / static_cast<double> (downsampleFactor);
    highPassCoefficient =
        std::exp (-twoPi * conditioningHighPassHz / inputSampleRate);

    // Capacity supports the complete configurable guitar/bass range. Requests
    // below this fixed prepare-time bound are clamped.
    maximumTauCapacity = std::max<std::size_t> (
        2, static_cast<std::size_t> (
               std::ceil (
                   effectiveSampleRate / minimumSupportedFrequencyHz)) + 1);
    const auto windowSize = 2 * maximumTauCapacity + 1;
    const auto blockFrames = static_cast<std::size_t> (
        std::max (1, maxBlockSize) / downsampleFactor + 2);
    const auto fifoCapacity = nextPowerOfTwo (std::max (
        minimumFifoCapacity,
        std::max (windowSize * 8, blockFrames * 8)));

    fifo.assign (fifoCapacity, {});
    history.assign (windowSize, 0.0f);
    analysisWindow.assign (windowSize, 0.0f);
    difference.assign (maximumTauCapacity + 1, 0.0);
    cumulativeMeanNormalizedDifference.assign (
        maximumTauCapacity + 1, 1.0);

    // About 100 control updates per second, bounded to at least one frame.
    analysisHopSize = std::max<std::size_t> (
        1, static_cast<std::size_t> (
               std::llround (effectiveSampleRate / 100.0)));
    setFrequencyRange (
        minimumFrequencyHz.load (std::memory_order_relaxed),
        maximumFrequencyHz.load (std::memory_order_relaxed));
    reset();
}

void PitchDetector::reset() noexcept
{
    fifoWritePosition.store (0, std::memory_order_relaxed);
    fifoReadPosition.store (0, std::memory_order_relaxed);
    droppedFrames.store (0, std::memory_order_relaxed);

    std::fill (history.begin(), history.end(), 0.0f);
    std::fill (analysisWindow.begin(), analysisWindow.end(), 0.0f);
    std::fill (difference.begin(), difference.end(), 0.0);
    std::fill (cumulativeMeanNormalizedDifference.begin(),
               cumulativeMeanNormalizedDifference.end(), 1.0);
    historyWritePosition = 0;
    historySampleCount = 0;
    samplesSinceAnalysis = 0;
    newestHistoryTimestamp = 0;

    previousInput = 0.0;
    previousHighPassed = 0.0;
    decimationAccumulator = 0.0;
    decimationPhase = 0;
    nextInputTimestamp = 0;

    stableResultCount = 0;
    invalidResultCount = 0;
    lastValidFrequencyHz = 0.0;
    lastValidConfidence = 0.0f;
    publish ({});
}

bool PitchDetector::pushSample (
    float sample, std::uint64_t sampleTimestamp) noexcept
{
    const auto safeInput =
        std::clamp (finiteOr (static_cast<double> (sample), 0.0), -4.0, 4.0);
    auto highPassed =
        highPassCoefficient
        * (previousHighPassed + safeInput - previousInput);
    if (! std::isfinite (highPassed)
        || std::abs (highPassed) < 1.0e-30)
        highPassed = 0.0;

    previousInput = safeInput;
    previousHighPassed = highPassed;
    decimationAccumulator += highPassed;
    ++decimationPhase;
    nextInputTimestamp = sampleTimestamp + 1;

    if (decimationPhase < downsampleFactor)
        return true;

    const auto decimated = static_cast<float> (
        std::clamp (
            decimationAccumulator / static_cast<double> (downsampleFactor),
            -4.0, 4.0));
    decimationAccumulator = 0.0;
    decimationPhase = 0;
    return enqueueDecimated (decimated, sampleTimestamp);
}

bool PitchDetector::pushSample (float sample) noexcept
{
    return pushSample (sample, nextInputTimestamp);
}

std::size_t PitchDetector::pushBlock (
    const float* samples, int numSamples,
    std::uint64_t firstSampleTimestamp) noexcept
{
    if (samples == nullptr || numSamples <= 0 || fifo.empty())
        return 0;

    std::size_t framesWritten = 0;
    for (int index = 0; index < numSamples; ++index)
    {
        const auto phaseBefore = decimationPhase;
        const auto accepted = pushSample (
            samples[index],
            firstSampleTimestamp + static_cast<std::uint64_t> (index));
        if (phaseBefore + 1 >= downsampleFactor && accepted)
            ++framesWritten;
    }
    return framesWritten;
}

std::size_t PitchDetector::pushBlock (
    const float* samples, int numSamples) noexcept
{
    return pushBlock (samples, numSamples, nextInputTimestamp);
}

bool PitchDetector::processControlRate() noexcept
{
    if (fifo.empty() || history.empty())
        return false;

    SampleFrame frame;
    bool consumedAny = false;
    while (dequeue (frame))
    {
        appendHistory (frame);
        consumedAny = true;
    }

    if (! consumedAny || historySampleCount < history.size()
        || samplesSinceAnalysis < analysisHopSize)
        return false;

    samplesSinceAnalysis %= analysisHopSize;
    publish (analyseWindow());
    return true;
}

PitchResult PitchDetector::getLatestResult() const noexcept
{
    PitchResult result;
    for (int attempt = 0; attempt < 4; ++attempt)
    {
        const auto before = resultSequence.load (std::memory_order_acquire);
        if ((before & 1U) != 0U)
            continue;

        result.frequencyHz =
            resultFrequencyHz.load (std::memory_order_relaxed);
        result.confidence =
            resultConfidence.load (std::memory_order_relaxed);
        result.sampleTimestamp =
            resultTimestamp.load (std::memory_order_relaxed);
        result.state = resultState.load (std::memory_order_relaxed);

        const auto after = resultSequence.load (std::memory_order_acquire);
        if (before == after)
            return result;
    }

    result.frequencyHz =
        resultFrequencyHz.load (std::memory_order_relaxed);
    result.confidence =
        resultConfidence.load (std::memory_order_relaxed);
    result.sampleTimestamp =
        resultTimestamp.load (std::memory_order_relaxed);
    result.state = resultState.load (std::memory_order_relaxed);
    return result;
}

void PitchDetector::setFrequencyRange (
    double minimumHz, double maximumHz) noexcept
{
    auto safeMinimum = finiteOr (minimumHz, defaultMinimumHz);
    auto safeMaximum = finiteOr (maximumHz, defaultMaximumHz);
    safeMinimum = std::clamp (
        safeMinimum, minimumSupportedFrequencyHz,
        std::max (minimumSupportedFrequencyHz,
                  0.45 * effectiveSampleRate));
    safeMaximum = std::clamp (
        safeMaximum, safeMinimum,
        std::max (safeMinimum, 0.45 * effectiveSampleRate));
    minimumFrequencyHz.store (safeMinimum, std::memory_order_relaxed);
    maximumFrequencyHz.store (safeMaximum, std::memory_order_relaxed);
}

void PitchDetector::setYinThreshold (double absoluteThreshold) noexcept
{
    yinThreshold.store (
        std::clamp (finiteOr (absoluteThreshold, defaultYinThreshold),
                    0.01, 0.99),
        std::memory_order_relaxed);
}

void PitchDetector::setSilenceThreshold (double rmsLinear) noexcept
{
    silenceThreshold.store (
        std::clamp (finiteOr (rmsLinear, defaultSilenceRms), 0.0, 1.0),
        std::memory_order_relaxed);
}

double PitchDetector::getEffectiveSampleRate() const noexcept
{
    return effectiveSampleRate;
}

int PitchDetector::getDownsampleFactor() const noexcept
{
    return downsampleFactor;
}

std::size_t PitchDetector::getDroppedFrameCount() const noexcept
{
    return droppedFrames.load (std::memory_order_relaxed);
}

bool PitchDetector::enqueueDecimated (
    float sample, std::uint64_t timestamp) noexcept
{
    if (fifo.empty())
        return false;

    const auto write =
        fifoWritePosition.load (std::memory_order_relaxed);
    const auto read =
        fifoReadPosition.load (std::memory_order_acquire);
    if (write - read >= fifo.size())
    {
        droppedFrames.fetch_add (1, std::memory_order_relaxed);
        return false;
    }

    fifo[static_cast<std::size_t> (write % fifo.size())] = {
        sample, timestamp
    };
    fifoWritePosition.store (write + 1, std::memory_order_release);
    return true;
}

bool PitchDetector::dequeue (SampleFrame& frame) noexcept
{
    const auto read =
        fifoReadPosition.load (std::memory_order_relaxed);
    const auto write =
        fifoWritePosition.load (std::memory_order_acquire);
    if (read == write)
        return false;

    frame = fifo[static_cast<std::size_t> (read % fifo.size())];
    fifoReadPosition.store (read + 1, std::memory_order_release);
    return true;
}

void PitchDetector::appendHistory (const SampleFrame& frame) noexcept
{
    history[historyWritePosition] =
        std::isfinite (frame.sample) ? frame.sample : 0.0f;
    historyWritePosition = (historyWritePosition + 1) % history.size();
    historySampleCount = std::min (history.size(), historySampleCount + 1);
    ++samplesSinceAnalysis;
    newestHistoryTimestamp = frame.timestamp;
}

PitchResult PitchDetector::analyseWindow() noexcept
{
    PitchResult result;
    result.sampleTimestamp = newestHistoryTimestamp;

    for (std::size_t index = 0; index < history.size(); ++index)
        analysisWindow[index] =
            history[(historyWritePosition + index) % history.size()];

    double energy = 0.0;
    for (const auto sample : analysisWindow)
        energy += static_cast<double> (sample) * sample;
    const auto rms =
        std::sqrt (energy / static_cast<double> (analysisWindow.size()));
    if (! std::isfinite (rms)
        || rms < silenceThreshold.load (std::memory_order_relaxed))
    {
        stableResultCount = 0;
        invalidResultCount = 0;
        lastValidFrequencyHz = 0.0;
        lastValidConfidence = 0.0f;
        result.state = PitchTrackingState::silent;
        return result;
    }

    const auto configuredMinimum =
        minimumFrequencyHz.load (std::memory_order_relaxed);
    const auto configuredMaximum =
        maximumFrequencyHz.load (std::memory_order_relaxed);
    if (! std::isfinite (effectiveSampleRate)
        || effectiveSampleRate <= 0.0
        || ! std::isfinite (configuredMinimum)
        || ! std::isfinite (configuredMaximum)
        || configuredMinimum <= 0.0
        || configuredMaximum < configuredMinimum)
    {
        result.state = PitchTrackingState::lowConfidence;
        return result;
    }

    const auto minimumTau = std::max<std::size_t> (
        2, static_cast<std::size_t> (
               std::ceil (effectiveSampleRate / configuredMaximum)));
    const auto maximumTau = std::min (
        maximumTauCapacity - 1,
        static_cast<std::size_t> (
            std::ceil (effectiveSampleRate / configuredMinimum)));
    const auto normalizationMaximumTau = maximumTau + 1;
    if (minimumTau > maximumTau
        || normalizationMaximumTau >= difference.size()
        || analysisWindow.size() <= normalizationMaximumTau)
    {
        result.state = PitchTrackingState::lowConfidence;
        return result;
    }

    // A constant comparison length avoids biasing large lags with fewer terms:
    // d_t(tau) = sum_j (x_j - x_(j+tau))^2.
    const auto comparisonLength =
        analysisWindow.size() - normalizationMaximumTau;
    difference[0] = 0.0;
    for (std::size_t tau = 1; tau <= normalizationMaximumTau; ++tau)
    {
        double sum = 0.0;
        for (std::size_t index = 0; index < comparisonLength; ++index)
        {
            const auto delta =
                static_cast<double> (analysisWindow[index])
                - static_cast<double> (analysisWindow[index + tau]);
            sum += delta * delta;
        }
        difference[tau] = std::isfinite (sum) ? sum : 0.0;
    }

    // Exact cumulative-mean normalization from SHREDEQ_SPEC 9.2:
    // d'(0)=1, d'(tau)=d(tau)/((1/tau)*sum_(j=1..tau)d(j)).
    cumulativeMeanNormalizedDifference[0] = 1.0;
    double runningSum = 0.0;
    for (std::size_t tau = 1; tau <= normalizationMaximumTau; ++tau)
    {
        runningSum += difference[tau];
        const auto mean = runningSum / static_cast<double> (tau);
        cumulativeMeanNormalizedDifference[tau] =
            mean > std::numeric_limits<double>::epsilon()
                ? difference[tau] / mean
                : 1.0;
    }

    const auto threshold =
        yinThreshold.load (std::memory_order_relaxed);
    std::size_t candidateTau = 0;
    for (auto tau = minimumTau; tau <= maximumTau; ++tau)
    {
        const auto value = cumulativeMeanNormalizedDifference[tau];
        if (value < threshold
            && value <= cumulativeMeanNormalizedDifference[tau - 1]
            && value < cumulativeMeanNormalizedDifference[tau + 1])
        {
            candidateTau = tau;
            break;
        }
    }

    if (candidateTau == 0)
    {
        ++invalidResultCount;
        stableResultCount = 0;
        if (lastValidFrequencyHz > 0.0 && invalidResultCount <= 2)
        {
            lastValidConfidence *= 0.5f;
            result.frequencyHz = lastValidFrequencyHz;
            result.confidence = lastValidConfidence;
            result.state = PitchTrackingState::hold;
        }
        else
        {
            lastValidFrequencyHz = 0.0;
            lastValidConfidence = 0.0f;
            result.state = PitchTrackingState::lowConfidence;
        }
        return result;
    }

    const auto left =
        cumulativeMeanNormalizedDifference[candidateTau - 1];
    const auto centre =
        cumulativeMeanNormalizedDifference[candidateTau];
    const auto right =
        cumulativeMeanNormalizedDifference[candidateTau + 1];
    const auto denominator = left - 2.0 * centre + right;
    auto refinedTau = static_cast<double> (candidateTau);
    if (std::isfinite (denominator)
        && std::abs (denominator) > 1.0e-12)
        refinedTau += std::clamp (
            0.5 * (left - right) / denominator, -0.5, 0.5);

    const auto frequency = effectiveSampleRate / refinedTau;
    const auto confidence = std::clamp (1.0 - centre, 0.0, 1.0);
    if (! std::isfinite (refinedTau) || refinedTau <= 0.0
        || ! std::isfinite (frequency)
        || frequency < configuredMinimum * 0.995
        || frequency > configuredMaximum * 1.005
        || ! std::isfinite (confidence))
    {
        ++invalidResultCount;
        stableResultCount = 0;
        result.state = PitchTrackingState::lowConfidence;
        return result;
    }

    const auto stable =
        lastValidFrequencyHz > 0.0
        && std::abs (std::log2 (frequency / lastValidFrequencyHz)) < 0.03;
    stableResultCount = stable ? stableResultCount + 1 : 1;
    invalidResultCount = 0;
    const auto boundedFrequency = std::clamp (
        frequency, configuredMinimum, configuredMaximum);
    lastValidFrequencyHz = boundedFrequency;
    lastValidConfidence = static_cast<float> (confidence);

    result.frequencyHz = boundedFrequency;
    result.confidence = static_cast<float> (confidence);
    result.state = stableResultCount >= 3
                       ? PitchTrackingState::locked
                       : PitchTrackingState::tracking;
    return result;
}

void PitchDetector::publish (const PitchResult& incoming) noexcept
{
    PitchResult result = incoming;
    if (! std::isfinite (result.frequencyHz) || result.frequencyHz < 0.0)
        result.frequencyHz = 0.0;
    if (! std::isfinite (result.confidence))
        result.confidence = 0.0f;
    result.confidence = std::clamp (result.confidence, 0.0f, 1.0f);

    resultSequence.fetch_add (1, std::memory_order_acq_rel);
    resultFrequencyHz.store (result.frequencyHz, std::memory_order_relaxed);
    resultConfidence.store (result.confidence, std::memory_order_relaxed);
    resultTimestamp.store (result.sampleTimestamp, std::memory_order_relaxed);
    resultState.store (result.state, std::memory_order_relaxed);
    resultSequence.fetch_add (1, std::memory_order_release);
}

void PitchDetector::MidiTracker::reset() noexcept
{
    held.fill (false);
    pressOrder.fill (0);
    nextOrder = 1;
}

void PitchDetector::MidiTracker::noteOn (int midiNote) noexcept
{
    if (midiNote < 0 || midiNote >= static_cast<int> (held.size()))
        return;

    held[static_cast<std::size_t> (midiNote)] = true;
    pressOrder[static_cast<std::size_t> (midiNote)] = nextOrder++;
    if (nextOrder == 0)
    {
        // A practically unreachable wrap is handled deterministically.
        nextOrder = 1;
        for (std::size_t note = 0; note < held.size(); ++note)
            pressOrder[note] = held[note] ? nextOrder++ : 0;
    }
}

void PitchDetector::MidiTracker::noteOff (int midiNote) noexcept
{
    if (midiNote < 0 || midiNote >= static_cast<int> (held.size()))
        return;

    held[static_cast<std::size_t> (midiNote)] = false;
    pressOrder[static_cast<std::size_t> (midiNote)] = 0;
}

void PitchDetector::MidiTracker::setPriority (
    MidiPriority newPriority) noexcept
{
    priority = newPriority;
}

PitchDetector::MidiPriority
PitchDetector::MidiTracker::getPriority() const noexcept
{
    return priority;
}

int PitchDetector::MidiTracker::getSelectedNote() const noexcept
{
    if (priority == MidiPriority::lowest)
    {
        for (std::size_t note = 0; note < held.size(); ++note)
            if (held[note])
                return static_cast<int> (note);
        return -1;
    }

    if (priority == MidiPriority::highest)
    {
        for (std::size_t note = held.size(); note-- > 0;)
            if (held[note])
                return static_cast<int> (note);
        return -1;
    }

    int selected = -1;
    std::uint64_t newest = 0;
    for (std::size_t note = 0; note < held.size(); ++note)
    {
        if (held[note] && pressOrder[note] >= newest)
        {
            newest = pressOrder[note];
            selected = static_cast<int> (note);
        }
    }
    return selected;
}

double PitchDetector::MidiTracker::getFrequencyHz() const noexcept
{
    return midiNoteToFrequency (getSelectedNote());
}

double PitchDetector::MidiTracker::midiNoteToFrequency (
    int midiNote) noexcept
{
    return PitchDetector::midiNoteToFrequency (midiNote);
}

double PitchDetector::midiNoteToFrequency (int midiNote) noexcept
{
    if (midiNote < 0 || midiNote > 127)
        return 0.0;
    return 440.0 * std::exp2 (
        (static_cast<double> (midiNote) - 69.0) / 12.0);
}

} // namespace ShredEQ::DSP
