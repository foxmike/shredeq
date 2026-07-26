#pragma once

#include "../Analysis/SpectralAnalysisEngine.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace ShredEQ::UI
{

/**
    Bounded data pipeline for the spectral percentile cloud and dynamic-range
    map. This class deliberately contains no JUCE Component or paint code; a
    later GUI phase consumes its published Snapshot.

    prepare(), reset(), and processNewFrames() belong on an analyzer worker or
    message-adjacent thread, never in processBlock(). GUI readers may call
    readLatestSnapshot() concurrently. PercentileCloud reads the shared
    SpectralAnalysisEngine snapshot and never owns an FFT/STFT.
*/
class PercentileCloud final
{
public:
    enum class HistoryWindow
    {
        seconds2,
        seconds5,
        seconds10,
        seconds30
    };

    struct Snapshot
    {
        std::vector<float> q10Db;
        std::vector<float> q50Db;
        std::vector<float> q90Db;
        std::vector<float> q99Db;
        std::vector<float> dynamicRangeDb;
        std::uint64_t sourceSequence = 0;
        int numBins = 0;
        double sampleRate = 0.0;
        bool valid = false;

        void prepare (int maxBins);

        [[nodiscard]] std::span<const float> q10() const noexcept;
        [[nodiscard]] std::span<const float> median() const noexcept;
        [[nodiscard]] std::span<const float> q90() const noexcept;
        [[nodiscard]] std::span<const float> q99() const noexcept;
        [[nodiscard]] std::span<const float> dynamicRange() const noexcept;
    };

    /**
        Allocates all ring, histogram, engine-snapshot, and publication memory.
        History is sampled at no more than 60 visual frames per second, so the
        fixed ring capacity is ceil(maxHistorySeconds * 60). A scan longer than
        that bound retains the newest prepared-capacity portion.
    */
    void prepare (double sampleRate, int maxBins, double maxHistorySeconds);
    void reset() noexcept;

    void setEnabled (bool shouldBeEnabled) noexcept;
    [[nodiscard]] bool isEnabled() const noexcept;

    void setHistoryWindow (HistoryWindow window) noexcept;
    /** Selects the nearest supported duration: 2, 5, 10, or 30 seconds. */
    void setHistoryWindow (double seconds) noexcept;
    [[nodiscard]] double getHistoryWindowSeconds() const noexcept;

    /**
        Frozen mode retains the last completed statistics and does not read the
        engine or perform percentile work.
    */
    void setFrozen (bool shouldBeFrozen) noexcept;
    [[nodiscard]] bool isFrozen() const noexcept;

    /**
        Starting a scan clears the previous history on the next update. While
        active, samples accumulate without time-window eviction, up to the
        fixed capacity reserved by prepare().
    */
    void setScanActive (bool shouldBeActive) noexcept;
    [[nodiscard]] bool isScanActive() const noexcept;

    /**
        Reads at most one new coherent frame from the shared engine. Returns
        true only when a frame was accepted and new statistics were published.
    */
    [[nodiscard]] bool processNewFrames (
        const Analysis::SpectralAnalysisEngine& engine) noexcept;
    [[nodiscard]] bool update (
        const Analysis::SpectralAnalysisEngine& engine) noexcept
    {
        return processNewFrames (engine);
    }

    /**
        Copies a coherent publication into caller-prepared storage without
        locks or allocation. Snapshot::prepare(maxBins) must be called first.
    */
    [[nodiscard]] bool readLatestSnapshot (
        Snapshot& destination) const noexcept;

    [[nodiscard]] int getPreparedMaxBins() const noexcept;
    [[nodiscard]] int getHistoryCapacity() const noexcept;
    [[nodiscard]] int getStoredFrameCount() const noexcept;

private:
    static constexpr int histogramBinCount = 512;
    static constexpr float quietFloorDb = -160.0f;
    static constexpr float histogramCeilingDb = 32.0f;
    static constexpr double maximumVisualFramesPerSecond = 60.0;

    void clearHistory() noexcept;
    void handleScanTransition() noexcept;
    [[nodiscard]] bool frameConfigurationChanged() const noexcept;
    [[nodiscard]] bool shouldAcceptFrame() const noexcept;
    void evictExpiredFrames (std::uint64_t newestTimestamp) noexcept;
    void removeOldestFrame() noexcept;
    void addCurrentFrame() noexcept;
    void computeAndPublish() noexcept;
    void publishEmpty() noexcept;

    [[nodiscard]] static int histogramIndex (float valueDb) noexcept;
    [[nodiscard]] float percentileForBin (
        int bin, double percentile) const noexcept;
    [[nodiscard]] double selectedWindowSeconds() const noexcept;

    Analysis::SpectralAnalysisEngine::Snapshot engineSnapshot;
    std::vector<float> historyDb;
    std::vector<std::uint64_t> historyTimestamps;
    std::vector<std::uint32_t> histogramCounts;

    std::unique_ptr<std::atomic<float>[]> publishedQ10;
    std::unique_ptr<std::atomic<float>[]> publishedQ50;
    std::unique_ptr<std::atomic<float>[]> publishedQ90;
    std::unique_ptr<std::atomic<float>[]> publishedQ99;
    std::unique_ptr<std::atomic<float>[]> publishedDynamicRange;
    mutable std::atomic<std::uint64_t> publicationGuard { 0 };
    std::atomic<std::uint64_t> publishedSourceSequence { 0 };
    std::atomic<int> publishedNumBins { 0 };
    std::atomic<double> publishedSampleRate { 0.0 };
    std::atomic<bool> publishedValid { false };

    std::atomic<bool> enabled { true };
    std::atomic<bool> frozen { false };
    std::atomic<bool> scanActive { false };
    std::atomic<HistoryWindow> historyWindow { HistoryWindow::seconds5 };

    double preparedSampleRate = 0.0;
    double preparedMaxHistorySeconds = 0.0;
    double activeSampleRate = 0.0;
    int preparedMaxBins = 0;
    int activeNumBins = 0;
    int activeFftSize = 0;
    int historyCapacity = 0;
    int historyStart = 0;
    int historyCount = 0;
    bool observedScanActive = false;
    std::uint64_t lastSourceSequence = 0;
    std::uint64_t lastSourceTimestamp = 0;
    std::uint64_t lastAcceptedTimestamp = 0;
};

} // namespace ShredEQ::UI
