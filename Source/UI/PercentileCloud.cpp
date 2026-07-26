#include "PercentileCloud.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace ShredEQ::UI
{
namespace
{
[[nodiscard]] std::size_t safeSizeProduct (int first, int second) noexcept
{
    if (first <= 0 || second <= 0)
        return 0;

    const auto a = static_cast<std::size_t> (first);
    const auto b = static_cast<std::size_t> (second);
    if (a > std::numeric_limits<std::size_t>::max() / b)
        return 0;
    return a * b;
}

[[nodiscard]] std::span<const float> activeSpan (
    const std::vector<float>& values, int numBins) noexcept
{
    const auto count = std::min (
        values.size(),
        static_cast<std::size_t> (std::max (0, numBins)));
    return { values.data(), count };
}
} // namespace

void PercentileCloud::Snapshot::prepare (int maxBins)
{
    const auto safeBins = std::max (0, maxBins);
    q10Db.assign (static_cast<std::size_t> (safeBins), quietFloorDb);
    q50Db.assign (static_cast<std::size_t> (safeBins), quietFloorDb);
    q90Db.assign (static_cast<std::size_t> (safeBins), quietFloorDb);
    q99Db.assign (static_cast<std::size_t> (safeBins), quietFloorDb);
    dynamicRangeDb.assign (static_cast<std::size_t> (safeBins), 0.0f);
    sourceSequence = 0;
    numBins = 0;
    sampleRate = 0.0;
    valid = false;
}

std::span<const float> PercentileCloud::Snapshot::q10() const noexcept
{
    return activeSpan (q10Db, numBins);
}

std::span<const float> PercentileCloud::Snapshot::median() const noexcept
{
    return activeSpan (q50Db, numBins);
}

std::span<const float> PercentileCloud::Snapshot::q90() const noexcept
{
    return activeSpan (q90Db, numBins);
}

std::span<const float> PercentileCloud::Snapshot::q99() const noexcept
{
    return activeSpan (q99Db, numBins);
}

std::span<const float>
PercentileCloud::Snapshot::dynamicRange() const noexcept
{
    return activeSpan (dynamicRangeDb, numBins);
}

void PercentileCloud::prepare (
    double sampleRate, int maxBins, double maxHistorySeconds)
{
    preparedSampleRate =
        std::isfinite (sampleRate) && sampleRate > 0.0 ? sampleRate : 44100.0;
    preparedMaxBins = std::max (0, maxBins);
    preparedMaxHistorySeconds =
        std::isfinite (maxHistorySeconds) && maxHistorySeconds > 0.0
            ? maxHistorySeconds
            : 30.0;

    const auto capacityAsDouble = std::ceil (
        preparedMaxHistorySeconds * maximumVisualFramesPerSecond);
    historyCapacity = capacityAsDouble
                              > static_cast<double> (
                                    std::numeric_limits<int>::max())
                          ? 0
                          : std::max (1, static_cast<int> (capacityAsDouble));

    const auto historySize =
        safeSizeProduct (preparedMaxBins, historyCapacity);
    if (preparedMaxBins <= 0 || historySize == 0)
    {
        preparedMaxBins = 0;
        historyCapacity = 0;
    }

    historyDb.assign (historySize, quietFloorDb);
    historyTimestamps.assign (
        static_cast<std::size_t> (historyCapacity), std::uint64_t {});
    histogramCounts.assign (
        safeSizeProduct (preparedMaxBins, histogramBinCount),
        std::uint32_t {});
    engineSnapshot.prepare (preparedMaxBins);

    const auto publicationBins =
        static_cast<std::size_t> (preparedMaxBins);
    publishedQ10 =
        std::make_unique<std::atomic<float>[]> (publicationBins);
    publishedQ50 =
        std::make_unique<std::atomic<float>[]> (publicationBins);
    publishedQ90 =
        std::make_unique<std::atomic<float>[]> (publicationBins);
    publishedQ99 =
        std::make_unique<std::atomic<float>[]> (publicationBins);
    publishedDynamicRange =
        std::make_unique<std::atomic<float>[]> (publicationBins);

    clearHistory();
    publishEmpty();
}

void PercentileCloud::reset() noexcept
{
    clearHistory();
    publishEmpty();
}

void PercentileCloud::setEnabled (bool shouldBeEnabled) noexcept
{
    enabled.store (shouldBeEnabled, std::memory_order_release);
}

bool PercentileCloud::isEnabled() const noexcept
{
    return enabled.load (std::memory_order_relaxed);
}

void PercentileCloud::setHistoryWindow (HistoryWindow window) noexcept
{
    historyWindow.store (window, std::memory_order_release);
}

void PercentileCloud::setHistoryWindow (double seconds) noexcept
{
    if (! std::isfinite (seconds))
        seconds = 5.0;

    constexpr double durations[] { 2.0, 5.0, 10.0, 30.0 };
    constexpr HistoryWindow windows[] {
        HistoryWindow::seconds2,
        HistoryWindow::seconds5,
        HistoryWindow::seconds10,
        HistoryWindow::seconds30
    };

    auto best = 0;
    for (auto index = 1; index < 4; ++index)
        if (std::abs (seconds - durations[index])
            < std::abs (seconds - durations[best]))
            best = index;
    setHistoryWindow (windows[best]);
}

double PercentileCloud::getHistoryWindowSeconds() const noexcept
{
    return selectedWindowSeconds();
}

void PercentileCloud::setFrozen (bool shouldBeFrozen) noexcept
{
    frozen.store (shouldBeFrozen, std::memory_order_release);
}

bool PercentileCloud::isFrozen() const noexcept
{
    return frozen.load (std::memory_order_relaxed);
}

void PercentileCloud::setScanActive (bool shouldBeActive) noexcept
{
    scanActive.store (shouldBeActive, std::memory_order_release);
}

bool PercentileCloud::isScanActive() const noexcept
{
    return scanActive.load (std::memory_order_relaxed);
}

bool PercentileCloud::processNewFrames (
    const Analysis::SpectralAnalysisEngine& engine) noexcept
{
    if (! enabled.load (std::memory_order_acquire)
        || frozen.load (std::memory_order_acquire)
        || preparedMaxBins <= 0 || historyCapacity <= 0)
        return false;

    handleScanTransition();

    if (! engine.readLatestSnapshot (engineSnapshot))
        return false;

    if (engineSnapshot.numBins <= 0
        || engineSnapshot.numBins > preparedMaxBins
        || engineSnapshot.logMagnitudesDb.size()
               < static_cast<std::size_t> (engineSnapshot.numBins))
        return false;

    if (frameConfigurationChanged())
    {
        clearHistory();
        activeNumBins = engineSnapshot.numBins;
        activeFftSize = engineSnapshot.fftSize;
        activeSampleRate = engineSnapshot.sampleRate;
    }
    else if (lastSourceSequence != 0
             && (engineSnapshot.sequence < lastSourceSequence
                 || engineSnapshot.sampleTimestamp < lastSourceTimestamp))
    {
        clearHistory();
    }

    if (engineSnapshot.sequence == 0
        || engineSnapshot.sequence == lastSourceSequence)
        return false;

    lastSourceSequence = engineSnapshot.sequence;
    lastSourceTimestamp = engineSnapshot.sampleTimestamp;

    if (! shouldAcceptFrame())
        return false;

    if (! scanActive.load (std::memory_order_acquire))
        evictExpiredFrames (engineSnapshot.sampleTimestamp);

    addCurrentFrame();
    lastAcceptedTimestamp = engineSnapshot.sampleTimestamp;
    computeAndPublish();
    return true;
}

bool PercentileCloud::readLatestSnapshot (
    Snapshot& destination) const noexcept
{
    if (publishedQ10 == nullptr || publishedQ50 == nullptr
        || publishedQ90 == nullptr || publishedQ99 == nullptr
        || publishedDynamicRange == nullptr)
        return false;

    for (auto attempt = 0; attempt < 4; ++attempt)
    {
        const auto before =
            publicationGuard.load (std::memory_order_acquire);
        if ((before & 1U) != 0U)
            continue;

        const auto bins =
            publishedNumBins.load (std::memory_order_relaxed);
        if (bins < 0 || bins > preparedMaxBins
            || destination.q10Db.size()
                   < static_cast<std::size_t> (bins)
            || destination.q50Db.size()
                   < static_cast<std::size_t> (bins)
            || destination.q90Db.size()
                   < static_cast<std::size_t> (bins)
            || destination.q99Db.size()
                   < static_cast<std::size_t> (bins)
            || destination.dynamicRangeDb.size()
                   < static_cast<std::size_t> (bins))
            return false;

        for (auto bin = 0; bin < bins; ++bin)
        {
            const auto index = static_cast<std::size_t> (bin);
            destination.q10Db[index] =
                publishedQ10[index].load (std::memory_order_relaxed);
            destination.q50Db[index] =
                publishedQ50[index].load (std::memory_order_relaxed);
            destination.q90Db[index] =
                publishedQ90[index].load (std::memory_order_relaxed);
            destination.q99Db[index] =
                publishedQ99[index].load (std::memory_order_relaxed);
            destination.dynamicRangeDb[index] =
                publishedDynamicRange[index].load (
                    std::memory_order_relaxed);
        }
        destination.sourceSequence =
            publishedSourceSequence.load (std::memory_order_relaxed);
        destination.numBins = bins;
        destination.sampleRate =
            publishedSampleRate.load (std::memory_order_relaxed);
        destination.valid =
            publishedValid.load (std::memory_order_relaxed);

        const auto after =
            publicationGuard.load (std::memory_order_acquire);
        if (before == after)
            return true;
    }

    return false;
}

int PercentileCloud::getPreparedMaxBins() const noexcept
{
    return preparedMaxBins;
}

int PercentileCloud::getHistoryCapacity() const noexcept
{
    return historyCapacity;
}

int PercentileCloud::getStoredFrameCount() const noexcept
{
    return historyCount;
}

void PercentileCloud::clearHistory() noexcept
{
    std::fill (
        histogramCounts.begin(), histogramCounts.end(), std::uint32_t {});
    historyStart = 0;
    historyCount = 0;
    activeNumBins = 0;
    activeFftSize = 0;
    activeSampleRate = 0.0;
    lastSourceSequence = 0;
    lastSourceTimestamp = 0;
    lastAcceptedTimestamp = 0;
}

void PercentileCloud::handleScanTransition() noexcept
{
    const auto nowActive =
        scanActive.load (std::memory_order_acquire);
    if (nowActive && ! observedScanActive)
        clearHistory();
    observedScanActive = nowActive;
}

bool PercentileCloud::frameConfigurationChanged() const noexcept
{
    const auto sampleRateDifference =
        std::abs (activeSampleRate - engineSnapshot.sampleRate);
    return activeNumBins != engineSnapshot.numBins
           || activeFftSize != engineSnapshot.fftSize
           || sampleRateDifference
                  > std::numeric_limits<double>::epsilon()
                        * std::max (
                            std::abs (activeSampleRate),
                            std::abs (engineSnapshot.sampleRate));
}

bool PercentileCloud::shouldAcceptFrame() const noexcept
{
    if (historyCount == 0 || lastAcceptedTimestamp == 0
        || engineSnapshot.sampleTimestamp <= lastAcceptedTimestamp)
        return true;

    const auto rate =
        engineSnapshot.sampleRate > 0.0
            ? engineSnapshot.sampleRate
            : preparedSampleRate;
    const auto minimumSamples = std::max<std::uint64_t> (
        1, static_cast<std::uint64_t> (
               std::floor (rate / maximumVisualFramesPerSecond)));
    return engineSnapshot.sampleTimestamp - lastAcceptedTimestamp
           >= minimumSamples;
}

void PercentileCloud::evictExpiredFrames (
    std::uint64_t newestTimestamp) noexcept
{
    const auto rate =
        activeSampleRate > 0.0 ? activeSampleRate : preparedSampleRate;
    const auto requestedSeconds =
        std::min (selectedWindowSeconds(), preparedMaxHistorySeconds);
    const auto windowSamples = static_cast<std::uint64_t> (
        std::max (1.0, std::floor (requestedSeconds * rate)));

    while (historyCount > 0)
    {
        const auto oldest =
            historyTimestamps[static_cast<std::size_t> (historyStart)];
        if (newestTimestamp <= oldest
            || newestTimestamp - oldest <= windowSamples)
            break;
        removeOldestFrame();
    }
}

void PercentileCloud::removeOldestFrame() noexcept
{
    if (historyCount <= 0 || activeNumBins <= 0)
        return;

    const auto frameOffset =
        static_cast<std::size_t> (historyStart)
        * static_cast<std::size_t> (preparedMaxBins);
    for (auto bin = 0; bin < activeNumBins; ++bin)
    {
        const auto value = historyDb[
            frameOffset + static_cast<std::size_t> (bin)];
        const auto histogramOffset =
            static_cast<std::size_t> (bin * histogramBinCount);
        auto& count = histogramCounts[
            histogramOffset
            + static_cast<std::size_t> (histogramIndex (value))];
        if (count > 0)
            --count;
    }

    historyStart = (historyStart + 1) % historyCapacity;
    --historyCount;
}

void PercentileCloud::addCurrentFrame() noexcept
{
    if (historyCount == historyCapacity)
        removeOldestFrame();

    const auto writeFrame =
        (historyStart + historyCount) % historyCapacity;
    const auto frameOffset =
        static_cast<std::size_t> (writeFrame)
        * static_cast<std::size_t> (preparedMaxBins);

    for (auto bin = 0; bin < activeNumBins; ++bin)
    {
        auto value =
            engineSnapshot.logMagnitudesDb[static_cast<std::size_t> (bin)];
        if (! std::isfinite (value))
            value = quietFloorDb;
        value = std::clamp (value, quietFloorDb, histogramCeilingDb);

        historyDb[frameOffset + static_cast<std::size_t> (bin)] = value;
        const auto histogramOffset =
            static_cast<std::size_t> (bin * histogramBinCount);
        ++histogramCounts[
            histogramOffset
            + static_cast<std::size_t> (histogramIndex (value))];
    }

    historyTimestamps[static_cast<std::size_t> (writeFrame)] =
        engineSnapshot.sampleTimestamp;
    ++historyCount;
}

void PercentileCloud::computeAndPublish() noexcept
{
    publicationGuard.fetch_add (1, std::memory_order_acq_rel);

    for (auto bin = 0; bin < activeNumBins; ++bin)
    {
        const auto q10Value = percentileForBin (bin, 0.10);
        const auto q50Value = percentileForBin (bin, 0.50);
        const auto q90Value = percentileForBin (bin, 0.90);
        const auto q99Value = percentileForBin (bin, 0.99);
        const auto index = static_cast<std::size_t> (bin);
        publishedQ10[index].store (q10Value, std::memory_order_relaxed);
        publishedQ50[index].store (q50Value, std::memory_order_relaxed);
        publishedQ90[index].store (q90Value, std::memory_order_relaxed);
        publishedQ99[index].store (q99Value, std::memory_order_relaxed);
        publishedDynamicRange[index].store (
            std::max (0.0f, q90Value - q10Value),
            std::memory_order_relaxed);
    }

    publishedSourceSequence.store (
        engineSnapshot.sequence, std::memory_order_relaxed);
    publishedNumBins.store (activeNumBins, std::memory_order_relaxed);
    publishedSampleRate.store (
        activeSampleRate, std::memory_order_relaxed);
    publishedValid.store (historyCount > 0, std::memory_order_relaxed);
    publicationGuard.fetch_add (1, std::memory_order_release);
}

void PercentileCloud::publishEmpty() noexcept
{
    publicationGuard.fetch_add (1, std::memory_order_acq_rel);
    for (auto bin = 0; bin < preparedMaxBins; ++bin)
    {
        const auto index = static_cast<std::size_t> (bin);
        publishedQ10[index].store (quietFloorDb, std::memory_order_relaxed);
        publishedQ50[index].store (quietFloorDb, std::memory_order_relaxed);
        publishedQ90[index].store (quietFloorDb, std::memory_order_relaxed);
        publishedQ99[index].store (quietFloorDb, std::memory_order_relaxed);
        publishedDynamicRange[index].store (
            0.0f, std::memory_order_relaxed);
    }
    publishedSourceSequence.store (0, std::memory_order_relaxed);
    publishedNumBins.store (0, std::memory_order_relaxed);
    publishedSampleRate.store (
        preparedSampleRate, std::memory_order_relaxed);
    publishedValid.store (false, std::memory_order_relaxed);
    publicationGuard.fetch_add (1, std::memory_order_release);
}

int PercentileCloud::histogramIndex (float valueDb) noexcept
{
    const auto normalised =
        (std::clamp (valueDb, quietFloorDb, histogramCeilingDb)
         - quietFloorDb)
        / (histogramCeilingDb - quietFloorDb);
    return std::clamp (
        static_cast<int> (normalised
                          * static_cast<float> (histogramBinCount)),
        0, histogramBinCount - 1);
}

float PercentileCloud::percentileForBin (
    int bin, double percentile) const noexcept
{
    if (historyCount <= 0 || bin < 0 || bin >= activeNumBins)
        return quietFloorDb;

    const auto rank = static_cast<std::uint32_t> (
        std::ceil (
            std::clamp (percentile, 0.0, 1.0)
            * static_cast<double> (historyCount)));
    const auto target = std::max<std::uint32_t> (1, rank);
    const auto histogramOffset =
        static_cast<std::size_t> (bin * histogramBinCount);

    std::uint32_t cumulative = 0;
    for (auto bucket = 0; bucket < histogramBinCount; ++bucket)
    {
        cumulative += histogramCounts[
            histogramOffset + static_cast<std::size_t> (bucket)];
        if (cumulative >= target)
        {
            const auto bucketWidth =
                (histogramCeilingDb - quietFloorDb)
                / static_cast<float> (histogramBinCount);
            return quietFloorDb
                   + (static_cast<float> (bucket) + 0.5f) * bucketWidth;
        }
    }

    return histogramCeilingDb;
}

double PercentileCloud::selectedWindowSeconds() const noexcept
{
    switch (historyWindow.load (std::memory_order_acquire))
    {
        case HistoryWindow::seconds2:
            return 2.0;
        case HistoryWindow::seconds5:
            return 5.0;
        case HistoryWindow::seconds10:
            return 10.0;
        case HistoryWindow::seconds30:
            return 30.0;
    }
    return 5.0;
}

} // namespace ShredEQ::UI
