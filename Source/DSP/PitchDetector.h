#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace ShredEQ::DSP
{

enum class PitchTrackingState
{
    searching = 0,
    tracking,
    locked,
    hold,
    silent,
    lowConfidence,
    Searching = searching,
    Tracking = tracking,
    Locked = locked,
    Hold = hold,
    Silent = silent,
    LowConfidence = lowConfidence
};

struct PitchResult
{
    double frequencyHz = 0.0;
    float confidence = 0.0f;
    std::uint64_t sampleTimestamp = 0;
    PitchTrackingState state = PitchTrackingState::searching;
};

/**
    Monophonic YIN pitch detector with an audio-to-analysis SPSC FIFO.

    prepare() and reset() must be called while neither producer nor consumer is
    active. pushSample()/pushBlock() are the only audio-thread operations.
    They apply a 20 Hz one-pole high-pass and box-filter decimation to about
    12 kHz, then enqueue fixed-size SampleFrames without allocating or blocking.

    processControlRate() is deliberately explicit rather than owning a thread.
    Call it periodically from a shared analyzer/background thread. It drains
    the FIFO and performs YIN only after a control-rate hop is available. This
    pattern lets later analyzers share one worker and keeps full-window YIN off
    the processBlock-critical path.
*/
class PitchDetector final
{
public:
    using Result = PitchResult;
    using TrackingState = PitchTrackingState;

    PitchDetector() noexcept = default;

    void prepare (double sampleRate, int maxBlockSize);
    void reset() noexcept;

    /** Pushes one input-rate mono sample with an explicit host sample time. */
    [[nodiscard]] bool pushSample (
        float sample, std::uint64_t sampleTimestamp) noexcept;

    /** Uses the detector's monotonically increasing input sample counter. */
    [[nodiscard]] bool pushSample (float sample) noexcept;

    /**
        Pushes an input-rate block and returns the number of decimated frames
        successfully enqueued. Invalid samples are replaced with silence.
    */
    [[nodiscard]] std::size_t pushBlock (
        const float* samples, int numSamples,
        std::uint64_t firstSampleTimestamp) noexcept;
    [[nodiscard]] std::size_t pushBlock (
        const float* samples, int numSamples) noexcept;

    /**
        Drains pending frames and, when a hop is ready, runs one YIN analysis.
        This method is for one non-audio consumer thread only.
        Returns true when a new result snapshot was published.
    */
    [[nodiscard]] bool processControlRate() noexcept;

    /**
        Bounded lock-free read of the most recently published result. A short
        sequence check normally yields one coherent generation; if the writer
        is preempted mid-publish, the method returns the individually safe
        atomic fields rather than ever waiting on the audio thread.
    */
    [[nodiscard]] PitchResult getLatestResult() const noexcept;

    void setFrequencyRange (double minimumHz, double maximumHz) noexcept;
    void setYinThreshold (double absoluteThreshold) noexcept;
    void setSilenceThreshold (double rmsLinear) noexcept;

    [[nodiscard]] double getEffectiveSampleRate() const noexcept;
    [[nodiscard]] int getDownsampleFactor() const noexcept;
    [[nodiscard]] std::size_t getDroppedFrameCount() const noexcept;

    enum class MidiPriority
    {
        lowest = 0,
        highest,
        last,
        Lowest = lowest,
        Highest = highest,
        Last = last
    };

    /**
        Allocation-free deterministic MIDI fundamental selector.
        Call noteOn/noteOff in MIDI event order; "last" selects the most
        recently pressed note that remains held.
    */
    class MidiTracker final
    {
    public:
        void reset() noexcept;
        void noteOn (int midiNote) noexcept;
        void noteOff (int midiNote) noexcept;
        void setPriority (MidiPriority newPriority) noexcept;

        [[nodiscard]] MidiPriority getPriority() const noexcept;
        [[nodiscard]] int getSelectedNote() const noexcept;
        [[nodiscard]] double getFrequencyHz() const noexcept;
        [[nodiscard]] static double midiNoteToFrequency (
            int midiNote) noexcept;

    private:
        std::array<bool, 128> held {};
        std::array<std::uint64_t, 128> pressOrder {};
        std::uint64_t nextOrder = 1;
        MidiPriority priority = MidiPriority::last;
    };

    [[nodiscard]] static double midiNoteToFrequency (int midiNote) noexcept;

private:
    struct SampleFrame
    {
        float sample = 0.0f;
        std::uint64_t timestamp = 0;
    };

    [[nodiscard]] bool enqueueDecimated (
        float sample, std::uint64_t timestamp) noexcept;
    [[nodiscard]] bool dequeue (SampleFrame& frame) noexcept;
    void appendHistory (const SampleFrame& frame) noexcept;
    [[nodiscard]] PitchResult analyseWindow() noexcept;
    void publish (const PitchResult& result) noexcept;

    static constexpr double defaultSampleRate = 44100.0;
    static constexpr double minimumSupportedSampleRate = 8000.0;
    static constexpr double maximumSupportedSampleRate = 768000.0;
    static constexpr double minimumSupportedFrequencyHz = 20.0;
    static constexpr double defaultMinimumHz = 30.0;
    static constexpr double defaultMaximumHz = 1500.0;
    static constexpr double targetAnalysisSampleRate = 12000.0;
    static constexpr double conditioningHighPassHz = 20.0;
    static constexpr double defaultYinThreshold = 0.15;
    static constexpr double defaultSilenceRms = 1.0e-4;
    static constexpr std::size_t minimumFifoCapacity = 1024;

    std::vector<SampleFrame> fifo;
    alignas (64) std::atomic<std::uint64_t> fifoWritePosition { 0 };
    alignas (64) std::atomic<std::uint64_t> fifoReadPosition { 0 };
    std::atomic<std::size_t> droppedFrames { 0 };

    std::vector<float> history;
    std::vector<float> analysisWindow;
    std::vector<double> difference;
    std::vector<double> cumulativeMeanNormalizedDifference;
    std::size_t historyWritePosition = 0;
    std::size_t historySampleCount = 0;
    std::size_t samplesSinceAnalysis = 0;
    std::size_t analysisHopSize = 1;
    std::size_t maximumTauCapacity = 1;
    std::uint64_t newestHistoryTimestamp = 0;

    double inputSampleRate = defaultSampleRate;
    double effectiveSampleRate = defaultSampleRate;
    int downsampleFactor = 1;
    double highPassCoefficient = 0.0;
    double previousInput = 0.0;
    double previousHighPassed = 0.0;
    double decimationAccumulator = 0.0;
    int decimationPhase = 0;
    std::uint64_t nextInputTimestamp = 0;

    std::atomic<double> minimumFrequencyHz { defaultMinimumHz };
    std::atomic<double> maximumFrequencyHz { defaultMaximumHz };
    std::atomic<double> yinThreshold { defaultYinThreshold };
    std::atomic<double> silenceThreshold { defaultSilenceRms };

    std::uint32_t stableResultCount = 0;
    std::uint32_t invalidResultCount = 0;
    double lastValidFrequencyHz = 0.0;
    float lastValidConfidence = 0.0f;

    mutable std::atomic<std::uint64_t> resultSequence { 0 };
    std::atomic<double> resultFrequencyHz { 0.0 };
    std::atomic<float> resultConfidence { 0.0f };
    std::atomic<std::uint64_t> resultTimestamp { 0 };
    std::atomic<PitchTrackingState> resultState {
        PitchTrackingState::searching
    };
};

} // namespace ShredEQ::DSP
