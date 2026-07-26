#pragma once

#include "AnalysisFifo.h"

#include <juce_core/juce_core.h>
#include <juce_dsp/juce_dsp.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace ShredEQ::Analysis
{

/**
    The single shared STFT owner for resonance analysis, spectrum display,
    percentile clouds, persistence maps, and future sidechain collision views.

    One audio thread may call pushAudio(); one owned worker consumes the FIFO.
    Any number of control/GUI/analysis readers may call readLatestSnapshot().
    Snapshot samples and metadata are atomic and guarded by a generation
    counter, so readers never lock or block processBlock. A reader must call
    Snapshot::prepare() off its critical path after engine prepare/re-prepare;
    readLatestSnapshot() never resizes or allocates and returns false if the
    storage size is wrong or a coherent generation cannot be obtained quickly.

    This class publishes only the latest frame. Later percentile/persistence
    consumers must own their bounded histories; they must not add another STFT.
*/
class SpectralAnalysisEngine final : private juce::Thread
{
public:
    struct Snapshot
    {
        std::vector<float> magnitudes;
        std::vector<float> logMagnitudesDb;
        std::uint64_t sequence = 0;
        std::uint64_t sampleTimestamp = 0;
        int fftSize = 0;
        int numBins = 0;
        double sampleRate = 0.0;

        void prepare (int bins);
        [[nodiscard]] double binToHz (int bin) const noexcept;
    };

    SpectralAnalysisEngine();
    ~SpectralAnalysisEngine() override;

    /**
        Allocates all FIFO, FFT, window, work, and publication memory and starts
        the worker. fftOrderOrSize accepts an order from 5 through 15 or an
        explicit size of at least 32; explicit sizes are rounded up to a
        supported power of two. A non-positive hop defaults to fftSize / 4.

        This lifecycle method must not overlap pushAudio() or snapshot reads.
        fifoChannels reserves planar hooks; the STFT downmix uses its first two.
    */
    void prepare (
        double sampleRate, int maxBlockSize, int fftOrderOrSize,
        int hopSize = 0, int fifoChannels = 4);

    /** Convenience for callers whose UI/configuration stores an FFT order. */
    void prepareWithOrder (
        double sampleRate, int maxBlockSize, int fftOrder,
        int hopSize = 0, int fifoChannels = 4);

    /**
        Requests an asynchronous rolling-window reset. Configuration and memory
        remain unchanged.
    */
    void reset() noexcept;

    /**
        Audio-thread entry point: timestamp bookkeeping and FIFO copies only.
        Returns stored sample frames. It does no FFT, allocation, locking, or
        worker notification.
    */
    [[nodiscard]] int pushAudio (
        const float* const* channels, int numChannels,
        int numSamples) noexcept;

    void setEnabled (bool shouldBeEnabled) noexcept;
    [[nodiscard]] bool isEnabled() const noexcept;

    /**
        Set to zero when every analyzer consumer is hidden/idle. This prevents
        FIFO copies and lets the worker sleep with no FFT work. Default is one.
    */
    void setNumActiveConsumers (int numConsumers) noexcept;
    [[nodiscard]] int getNumActiveConsumers() const noexcept;

    [[nodiscard]] bool readLatestSnapshot (Snapshot& destination) const noexcept;

    [[nodiscard]] int getFftSize() const noexcept;
    [[nodiscard]] int getHopSize() const noexcept;
    [[nodiscard]] int getNumBins() const noexcept;
    [[nodiscard]] double getSampleRate() const noexcept;
    [[nodiscard]] double binToHz (int bin) const noexcept;

    [[nodiscard]] std::uint64_t getDroppedAudioFrameCount() const noexcept;
    [[nodiscard]] std::uint64_t getSkippedStftFrameCount() const noexcept;

private:
    void run() override;
    void clearWorkerState() noexcept;
    void processAvailableAudio() noexcept;
    void appendSample (float sample, std::uint64_t timestamp,
                       int& framesComputedThisCycle) noexcept;
    void computeAndPublishFrame (std::uint64_t timestamp) noexcept;
    void publishEmptySnapshot() noexcept;

    [[nodiscard]] static int sanitiseFftSize (
        int requestedOrderOrSize) noexcept;
    [[nodiscard]] bool processingWanted() const noexcept;

    static constexpr float magnitudeEpsilon = 1.0e-12f;
    static constexpr int maximumFftSize = 32768;
    static constexpr int minimumFftSize = 32;
    static constexpr int maximumPreparedBlockSize = 65536;
    static constexpr int maximumStoredChannels = 8;
    static constexpr int maximumFramesPerWorkerCycle = 4;

    AnalysisFifo audioFifo;
    std::unique_ptr<juce::dsp::FFT> fft;
    std::vector<float> hannWindow;
    std::vector<float> rollingWindow;
    std::vector<float> fftData;
    std::vector<float> magnitudeWork;
    std::vector<float> logMagnitudeWork;
    std::vector<float> fifoReadScratch;
    std::vector<std::uint64_t> fifoTimestampScratch;

    std::unique_ptr<std::atomic<float>[]> publishedMagnitudes;
    std::unique_ptr<std::atomic<float>[]> publishedLogMagnitudes;
    mutable std::atomic<std::uint64_t> publicationGuard { 0 };
    std::atomic<std::uint64_t> publishedSequence { 0 };
    std::atomic<std::uint64_t> publishedTimestamp { 0 };

    std::atomic<bool> enabled { true };
    std::atomic<int> activeConsumers { 1 };
    std::atomic<bool> resetRequested { false };
    std::atomic<std::uint64_t> nextAudioTimestamp { 0 };
    std::atomic<std::uint64_t> skippedStftFrames { 0 };

    double currentSampleRate = 44100.0;
    int currentFftSize = minimumFftSize;
    int currentHopSize = minimumFftSize / 4;
    int currentNumBins = minimumFftSize / 2 + 1;
    int rollingWritePosition = 0;
    int rollingSampleCount = 0;
    int samplesSinceLastFrame = 0;
    std::uint64_t previousInputTimestamp = 0;
    bool hasPreviousInputTimestamp = false;
    bool hasComputedFirstFrame = false;
};

} // namespace ShredEQ::Analysis
