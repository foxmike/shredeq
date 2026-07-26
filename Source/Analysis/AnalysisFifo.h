#pragma once

#include <juce_core/juce_core.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace ShredEQ::Analysis
{

/**
    Fixed-capacity planar audio FIFO for one audio producer and one analysis
    consumer.

    prepare() and reset() must only be called while producer and consumer are
    stopped. push() is allocation-free and never waits. When capacity is
    exhausted it writes the available prefix and drops the newest sample
    frames; the producer never moves the consumer-owned read position.

    Every stored channel has independent planar storage. This permits L/R and
    optional sidechain taps to share the same timestamped FIFO. popMono()
    downmixes the first requested channels (normally L/R), while pop() exposes
    the stored planes for future stereo or sidechain analyzers.
*/
class AnalysisFifo final
{
public:
    AnalysisFifo() noexcept = default;

    void prepare (int capacitySamples, int numChannels);
    void reset() noexcept;

    /**
        Copies as many complete multi-channel sample frames as will fit and
        returns that count. Missing/null input channels are stored as silence.
    */
    [[nodiscard]] int push (
        const float* const* channels, int numChannels, int numSamples,
        std::uint64_t firstSampleTimestamp) noexcept;

    /**
        Reads planar sample frames and advances the FIFO. Null destination
        planes are allowed. timestamps, when non-null, receives one timestamp
        per returned sample frame.
    */
    [[nodiscard]] int pop (
        float* const* destinations, int numDestinationChannels, int numSamples,
        std::uint64_t* timestamps = nullptr) noexcept;

    /**
        Downmixes the first min(numChannelsToMix, storedChannels) planes with
        equal gain and advances the FIFO. This is the engine's primary STFT
        input helper; passing 2 selects the main stereo L/R hooks.
    */
    [[nodiscard]] int popMono (
        float* destination, int numSamples,
        std::uint64_t* timestamps = nullptr,
        int numChannelsToMix = 2) noexcept;

    /** Advances only the consumer position. Used to skip stale analyzer data. */
    [[nodiscard]] int discard (int numSamples) noexcept;

    [[nodiscard]] int getNumReady() const noexcept;
    [[nodiscard]] int getFreeSpace() const noexcept;
    [[nodiscard]] int getCapacity() const noexcept;
    [[nodiscard]] int getNumChannels() const noexcept;

    /** A frame is one sample time across all stored channels. */
    [[nodiscard]] std::uint64_t getDroppedFrameCount() const noexcept;
    [[nodiscard]] std::uint64_t getDroppedPushCount() const noexcept;

private:
    void copyTimestamps (
        int start, int count, std::uint64_t* destination,
        int destinationOffset) const noexcept;

    juce::AbstractFifo fifo { 1 };
    std::vector<std::vector<float>> channelStorage;
    std::vector<std::uint64_t> timestampStorage;
    int usableCapacity = 0;

    std::atomic<std::uint64_t> droppedFrames { 0 };
    std::atomic<std::uint64_t> droppedPushes { 0 };
};

} // namespace ShredEQ::Analysis
