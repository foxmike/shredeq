#include "SpectralAnalysisEngine.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace ShredEQ::Analysis
{
namespace
{
constexpr double defaultSampleRate = 44100.0;
constexpr double minimumSampleRate = 1000.0;
constexpr double maximumSampleRate = 768000.0;
constexpr double twoPi = 6.283185307179586476925286766559;

[[nodiscard]] int nextPowerOfTwo (int value) noexcept
{
    auto result = 1;
    while (result < value
           && result <= std::numeric_limits<int>::max() / 2)
        result *= 2;
    return result;
}

[[nodiscard]] int orderForSize (int size) noexcept
{
    auto order = 0;
    while ((1 << order) < size)
        ++order;
    return order;
}
} // namespace

void SpectralAnalysisEngine::Snapshot::prepare (int bins)
{
    const auto safeBins = std::max (0, bins);
    magnitudes.assign (static_cast<std::size_t> (safeBins), 0.0f);
    logMagnitudesDb.assign (
        static_cast<std::size_t> (safeBins),
        20.0f * std::log10 (SpectralAnalysisEngine::magnitudeEpsilon));
}

double SpectralAnalysisEngine::Snapshot::binToHz (int bin) const noexcept
{
    if (fftSize <= 0 || sampleRate <= 0.0 || bin < 0 || bin >= numBins)
        return 0.0;
    return static_cast<double> (bin) * sampleRate
           / static_cast<double> (fftSize);
}

SpectralAnalysisEngine::SpectralAnalysisEngine()
    : juce::Thread ("ShredEQ spectral analysis")
{
}

SpectralAnalysisEngine::~SpectralAnalysisEngine()
{
    signalThreadShouldExit();
    notify();
    stopThread (2000);
}

void SpectralAnalysisEngine::prepare (
    double sampleRate, int maxBlockSize, int fftOrderOrSize, int hopSize,
    int fifoChannels)
{
    signalThreadShouldExit();
    notify();
    stopThread (2000);

    currentSampleRate = std::clamp (
        std::isfinite (sampleRate) ? sampleRate : defaultSampleRate,
        minimumSampleRate, maximumSampleRate);
    currentFftSize = sanitiseFftSize (fftOrderOrSize);
    currentNumBins = currentFftSize / 2 + 1;
    currentHopSize = hopSize > 0
                         ? std::clamp (hopSize, 1, currentFftSize)
                         : std::max (1, currentFftSize / 4);

    fft = std::make_unique<juce::dsp::FFT> (
        orderForSize (currentFftSize));
    hannWindow.resize (static_cast<std::size_t> (currentFftSize));
    if (currentFftSize == 1)
    {
        hannWindow[0] = 1.0f;
    }
    else
    {
        for (int index = 0; index < currentFftSize; ++index)
            hannWindow[static_cast<std::size_t> (index)] =
                static_cast<float> (
                    0.5
                    - 0.5 * std::cos (
                                twoPi * static_cast<double> (index)
                                / static_cast<double> (
                                    currentFftSize - 1)));
    }

    rollingWindow.assign (
        static_cast<std::size_t> (currentFftSize), 0.0f);
    fftData.assign (
        static_cast<std::size_t> (2 * currentFftSize), 0.0f);
    magnitudeWork.assign (
        static_cast<std::size_t> (currentNumBins), 0.0f);
    logMagnitudeWork.assign (
        static_cast<std::size_t> (currentNumBins),
        20.0f * std::log10 (magnitudeEpsilon));

    const auto safeBlockSize = std::clamp (
        maxBlockSize, 1, maximumPreparedBlockSize);
    fifoReadScratch.assign (
        static_cast<std::size_t> (safeBlockSize), 0.0f);
    fifoTimestampScratch.assign (
        static_cast<std::size_t> (safeBlockSize), std::uint64_t {});

    const auto desiredCapacity = std::max (
        currentFftSize * 8, safeBlockSize * 16);
    audioFifo.prepare (
        nextPowerOfTwo (desiredCapacity),
        std::clamp (fifoChannels, 2, maximumStoredChannels));

    publishedMagnitudes =
        std::make_unique<std::atomic<float>[]> (
            static_cast<std::size_t> (currentNumBins));
    publishedLogMagnitudes =
        std::make_unique<std::atomic<float>[]> (
            static_cast<std::size_t> (currentNumBins));

    publicationGuard.store (0, std::memory_order_relaxed);
    publishedSequence.store (0, std::memory_order_relaxed);
    publishedTimestamp.store (0, std::memory_order_relaxed);
    nextAudioTimestamp.store (0, std::memory_order_relaxed);
    skippedStftFrames.store (0, std::memory_order_relaxed);
    clearWorkerState();
    publishEmptySnapshot();

    resetRequested.store (false, std::memory_order_relaxed);
    startThread();
}

void SpectralAnalysisEngine::prepareWithOrder (
    double sampleRate, int maxBlockSize, int fftOrder, int hopSize,
    int fifoChannels)
{
    const auto safeOrder = std::clamp (fftOrder, 5, 15);
    prepare (
        sampleRate, maxBlockSize, 1 << safeOrder, hopSize, fifoChannels);
}

void SpectralAnalysisEngine::reset() noexcept
{
    resetRequested.store (true, std::memory_order_release);
    notify();
}

int SpectralAnalysisEngine::pushAudio (
    const float* const* channels, int numChannels, int numSamples) noexcept
{
    if (numSamples <= 0)
        return 0;

    const auto firstTimestamp = nextAudioTimestamp.fetch_add (
        static_cast<std::uint64_t> (numSamples),
        std::memory_order_relaxed);
    if (! processingWanted())
        return 0;

    return audioFifo.push (
        channels, numChannels, numSamples, firstTimestamp);
}

void SpectralAnalysisEngine::setEnabled (bool shouldBeEnabled) noexcept
{
    const auto changed =
        enabled.exchange (
            shouldBeEnabled, std::memory_order_acq_rel)
        != shouldBeEnabled;
    if (changed)
        resetRequested.store (true, std::memory_order_release);
    notify();
}

bool SpectralAnalysisEngine::isEnabled() const noexcept
{
    return enabled.load (std::memory_order_relaxed);
}

void SpectralAnalysisEngine::setNumActiveConsumers (
    int numConsumers) noexcept
{
    const auto safeCount = std::max (0, numConsumers);
    const auto changed =
        activeConsumers.exchange (
            safeCount, std::memory_order_acq_rel)
        != safeCount;
    if (changed)
        resetRequested.store (true, std::memory_order_release);
    notify();
}

int SpectralAnalysisEngine::getNumActiveConsumers() const noexcept
{
    return activeConsumers.load (std::memory_order_relaxed);
}

bool SpectralAnalysisEngine::readLatestSnapshot (
    Snapshot& destination) const noexcept
{
    if (publishedMagnitudes == nullptr
        || publishedLogMagnitudes == nullptr
        || destination.magnitudes.size()
               < static_cast<std::size_t> (currentNumBins)
        || destination.logMagnitudesDb.size()
               < static_cast<std::size_t> (currentNumBins))
        return false;

    for (int attempt = 0; attempt < 4; ++attempt)
    {
        const auto before =
            publicationGuard.load (std::memory_order_acquire);
        if ((before & 1U) != 0U)
            continue;

        for (int bin = 0; bin < currentNumBins; ++bin)
        {
            destination.magnitudes[static_cast<std::size_t> (bin)] =
                publishedMagnitudes[static_cast<std::size_t> (bin)].load (
                    std::memory_order_relaxed);
            destination.logMagnitudesDb[static_cast<std::size_t> (bin)] =
                publishedLogMagnitudes[static_cast<std::size_t> (bin)].load (
                    std::memory_order_relaxed);
        }
        destination.sequence =
            publishedSequence.load (std::memory_order_relaxed);
        destination.sampleTimestamp =
            publishedTimestamp.load (std::memory_order_relaxed);
        destination.fftSize = currentFftSize;
        destination.numBins = currentNumBins;
        destination.sampleRate = currentSampleRate;

        const auto after =
            publicationGuard.load (std::memory_order_acquire);
        if (before == after)
            return true;
    }

    return false;
}

int SpectralAnalysisEngine::getFftSize() const noexcept
{
    return currentFftSize;
}

int SpectralAnalysisEngine::getHopSize() const noexcept
{
    return currentHopSize;
}

int SpectralAnalysisEngine::getNumBins() const noexcept
{
    return currentNumBins;
}

double SpectralAnalysisEngine::getSampleRate() const noexcept
{
    return currentSampleRate;
}

double SpectralAnalysisEngine::binToHz (int bin) const noexcept
{
    if (bin < 0 || bin >= currentNumBins || currentFftSize <= 0)
        return 0.0;
    return static_cast<double> (bin) * currentSampleRate
           / static_cast<double> (currentFftSize);
}

std::uint64_t
SpectralAnalysisEngine::getDroppedAudioFrameCount() const noexcept
{
    return audioFifo.getDroppedFrameCount();
}

std::uint64_t
SpectralAnalysisEngine::getSkippedStftFrameCount() const noexcept
{
    return skippedStftFrames.load (std::memory_order_relaxed);
}

void SpectralAnalysisEngine::run()
{
    while (! threadShouldExit())
    {
        if (resetRequested.exchange (false, std::memory_order_acq_rel))
        {
            const auto discardedForReset =
                audioFifo.discard (audioFifo.getNumReady());
            static_cast<void> (discardedForReset);
            clearWorkerState();
        }

        if (! processingWanted())
        {
            wait (100);
            continue;
        }

        if (audioFifo.getNumReady() <= 0)
        {
            wait (4);
            continue;
        }

        processAvailableAudio();
    }
}

void SpectralAnalysisEngine::clearWorkerState() noexcept
{
    std::fill (rollingWindow.begin(), rollingWindow.end(), 0.0f);
    rollingWritePosition = 0;
    rollingSampleCount = 0;
    samplesSinceLastFrame = 0;
    previousInputTimestamp = 0;
    hasPreviousInputTimestamp = false;
    hasComputedFirstFrame = false;
}

void SpectralAnalysisEngine::processAvailableAudio() noexcept
{
    const auto ready = audioFifo.getNumReady();
    const auto backlogTarget = currentFftSize + currentHopSize;
    if (ready > std::max (backlogTarget, audioFifo.getCapacity() * 3 / 4))
    {
        auto toDiscard = ready - backlogTarget;
        toDiscard -= toDiscard % currentHopSize;
        const auto discarded = audioFifo.discard (toDiscard);
        if (discarded > 0)
        {
            skippedStftFrames.fetch_add (
                static_cast<std::uint64_t> (
                    (discarded + currentHopSize - 1) / currentHopSize),
                std::memory_order_relaxed);
            clearWorkerState();
        }
    }

    auto framesComputed = 0;
    while (! threadShouldExit() && processingWanted())
    {
        const auto available = audioFifo.getNumReady();
        if (available <= 0)
            break;

        const auto wanted = std::min (
            available, static_cast<int> (fifoReadScratch.size()));
        const auto read = audioFifo.popMono (
            fifoReadScratch.data(), wanted, fifoTimestampScratch.data(), 2);
        if (read <= 0)
            break;

        for (int index = 0; index < read; ++index)
            appendSample (
                fifoReadScratch[static_cast<std::size_t> (index)],
                fifoTimestampScratch[static_cast<std::size_t> (index)],
                framesComputed);
    }
}

void SpectralAnalysisEngine::appendSample (
    float sample, std::uint64_t timestamp,
    int& framesComputedThisCycle) noexcept
{
    if (hasPreviousInputTimestamp
        && timestamp != previousInputTimestamp + 1)
        clearWorkerState();

    previousInputTimestamp = timestamp;
    hasPreviousInputTimestamp = true;

    const auto safeSample = std::isfinite (sample) ? sample : 0.0f;
    rollingWindow[static_cast<std::size_t> (rollingWritePosition)] =
        safeSample;
    rollingWritePosition = (rollingWritePosition + 1) % currentFftSize;
    rollingSampleCount = std::min (
        currentFftSize, rollingSampleCount + 1);

    if (! hasComputedFirstFrame)
    {
        if (rollingSampleCount < currentFftSize)
            return;
        hasComputedFirstFrame = true;
        samplesSinceLastFrame = 0;
    }
    else
    {
        ++samplesSinceLastFrame;
        if (samplesSinceLastFrame < currentHopSize)
            return;
        samplesSinceLastFrame = 0;
    }

    if (framesComputedThisCycle < maximumFramesPerWorkerCycle)
    {
        computeAndPublishFrame (timestamp);
        ++framesComputedThisCycle;
    }
    else
    {
        skippedStftFrames.fetch_add (1, std::memory_order_relaxed);
    }
}

void SpectralAnalysisEngine::computeAndPublishFrame (
    std::uint64_t timestamp) noexcept
{
    if (fft == nullptr || rollingSampleCount < currentFftSize)
        return;

    std::fill (fftData.begin(), fftData.end(), 0.0f);
    for (int index = 0; index < currentFftSize; ++index)
    {
        const auto historyIndex =
            (rollingWritePosition + index) % currentFftSize;
        fftData[static_cast<std::size_t> (index)] =
            rollingWindow[static_cast<std::size_t> (historyIndex)]
            * hannWindow[static_cast<std::size_t> (index)];
    }

    fft->performFrequencyOnlyForwardTransform (fftData.data(), true);
    for (int bin = 0; bin < currentNumBins; ++bin)
    {
        auto magnitude = fftData[static_cast<std::size_t> (bin)];
        if (! std::isfinite (magnitude) || magnitude < 0.0f)
            magnitude = 0.0f;
        magnitudeWork[static_cast<std::size_t> (bin)] = magnitude;

        auto decibels =
            20.0f * std::log10 (magnitude + magnitudeEpsilon);
        if (! std::isfinite (decibels))
            decibels = 20.0f * std::log10 (magnitudeEpsilon);
        logMagnitudeWork[static_cast<std::size_t> (bin)] = decibels;
    }

    publicationGuard.fetch_add (1, std::memory_order_acq_rel);
    for (int bin = 0; bin < currentNumBins; ++bin)
    {
        publishedMagnitudes[static_cast<std::size_t> (bin)].store (
            magnitudeWork[static_cast<std::size_t> (bin)],
            std::memory_order_relaxed);
        publishedLogMagnitudes[static_cast<std::size_t> (bin)].store (
            logMagnitudeWork[static_cast<std::size_t> (bin)],
            std::memory_order_relaxed);
    }
    publishedTimestamp.store (timestamp, std::memory_order_relaxed);
    publishedSequence.fetch_add (1, std::memory_order_relaxed);
    publicationGuard.fetch_add (1, std::memory_order_release);
}

void SpectralAnalysisEngine::publishEmptySnapshot() noexcept
{
    publicationGuard.fetch_add (1, std::memory_order_acq_rel);
    const auto floorDb = 20.0f * std::log10 (magnitudeEpsilon);
    for (int bin = 0; bin < currentNumBins; ++bin)
    {
        publishedMagnitudes[static_cast<std::size_t> (bin)].store (
            0.0f, std::memory_order_relaxed);
        publishedLogMagnitudes[static_cast<std::size_t> (bin)].store (
            floorDb, std::memory_order_relaxed);
    }
    publishedSequence.store (0, std::memory_order_relaxed);
    publishedTimestamp.store (0, std::memory_order_relaxed);
    publicationGuard.fetch_add (1, std::memory_order_release);
}

int SpectralAnalysisEngine::sanitiseFftSize (
    int requestedOrderOrSize) noexcept
{
    if (requestedOrderOrSize >= 5 && requestedOrderOrSize <= 15)
        return 1 << requestedOrderOrSize;

    const auto clamped = std::clamp (
        requestedOrderOrSize, minimumFftSize, maximumFftSize);
    return std::min (maximumFftSize, nextPowerOfTwo (clamped));
}

bool SpectralAnalysisEngine::processingWanted() const noexcept
{
    return enabled.load (std::memory_order_relaxed)
           && activeConsumers.load (std::memory_order_relaxed) > 0;
}

} // namespace ShredEQ::Analysis
