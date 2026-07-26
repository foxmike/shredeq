#include "AnalysisFifo.h"

#include <algorithm>
#include <cstring>
#include <limits>

namespace ShredEQ::Analysis
{

void AnalysisFifo::prepare (int capacitySamples, int numChannels)
{
    usableCapacity = std::clamp (
        capacitySamples, 1, std::numeric_limits<int>::max() - 1);
    const auto storageSize = usableCapacity + 1;
    const auto safeNumChannels = std::max (1, numChannels);

    channelStorage.assign (
        static_cast<std::size_t> (safeNumChannels),
        std::vector<float> (static_cast<std::size_t> (storageSize), 0.0f));
    timestampStorage.assign (
        static_cast<std::size_t> (storageSize), std::uint64_t {});
    fifo.setTotalSize (storageSize);
    reset();
}

void AnalysisFifo::reset() noexcept
{
    fifo.reset();
    droppedFrames.store (0, std::memory_order_relaxed);
    droppedPushes.store (0, std::memory_order_relaxed);
}

int AnalysisFifo::push (
    const float* const* channels, int numChannels, int numSamples,
    std::uint64_t firstSampleTimestamp) noexcept
{
    if (numSamples <= 0 || channelStorage.empty())
        return 0;

    int start1 = 0;
    int size1 = 0;
    int start2 = 0;
    int size2 = 0;
    fifo.prepareToWrite (
        numSamples, start1, size1, start2, size2);
    const auto written = size1 + size2;

    const auto copyBlock = [&] (int storageStart, int sourceStart, int count)
    {
        if (count <= 0)
            return;

        for (std::size_t channel = 0; channel < channelStorage.size();
             ++channel)
        {
            auto* output = channelStorage[channel].data() + storageStart;
            const auto* input =
                channels != nullptr
                    && static_cast<int> (channel) < numChannels
                    ? channels[channel]
                    : nullptr;
            if (input != nullptr)
                std::memcpy (
                    output, input + sourceStart,
                    static_cast<std::size_t> (count) * sizeof (float));
            else
                std::fill_n (output, count, 0.0f);
        }

        for (int index = 0; index < count; ++index)
            timestampStorage[static_cast<std::size_t> (
                storageStart + index)] =
                firstSampleTimestamp
                + static_cast<std::uint64_t> (sourceStart + index);
    };

    copyBlock (start1, 0, size1);
    copyBlock (start2, size1, size2);
    fifo.finishedWrite (written);

    const auto dropped = numSamples - written;
    if (dropped > 0)
    {
        droppedFrames.fetch_add (
            static_cast<std::uint64_t> (dropped),
            std::memory_order_relaxed);
        droppedPushes.fetch_add (1, std::memory_order_relaxed);
    }

    return written;
}

int AnalysisFifo::pop (
    float* const* destinations, int numDestinationChannels, int numSamples,
    std::uint64_t* timestamps) noexcept
{
    if (numSamples <= 0 || channelStorage.empty())
        return 0;

    int start1 = 0;
    int size1 = 0;
    int start2 = 0;
    int size2 = 0;
    fifo.prepareToRead (
        numSamples, start1, size1, start2, size2);

    const auto copyBlock = [&] (
        int storageStart, int destinationStart, int count)
    {
        if (count <= 0)
            return;

        const auto channelsToCopy = std::min (
            static_cast<int> (channelStorage.size()),
            std::max (0, numDestinationChannels));
        for (int channel = 0; channel < channelsToCopy; ++channel)
        {
            if (destinations == nullptr || destinations[channel] == nullptr)
                continue;
            std::memcpy (
                destinations[channel] + destinationStart,
                channelStorage[static_cast<std::size_t> (channel)].data()
                    + storageStart,
                static_cast<std::size_t> (count) * sizeof (float));
        }
        copyTimestamps (
            storageStart, count, timestamps, destinationStart);
    };

    copyBlock (start1, 0, size1);
    copyBlock (start2, size1, size2);
    const auto read = size1 + size2;
    fifo.finishedRead (read);
    return read;
}

int AnalysisFifo::popMono (
    float* destination, int numSamples, std::uint64_t* timestamps,
    int numChannelsToMix) noexcept
{
    if (destination == nullptr || numSamples <= 0 || channelStorage.empty())
        return 0;

    int start1 = 0;
    int size1 = 0;
    int start2 = 0;
    int size2 = 0;
    fifo.prepareToRead (
        numSamples, start1, size1, start2, size2);

    const auto channelsToMix = std::clamp (
        numChannelsToMix, 1, static_cast<int> (channelStorage.size()));
    const auto gain = 1.0f / static_cast<float> (channelsToMix);
    const auto mixBlock = [&] (
        int storageStart, int destinationStart, int count)
    {
        for (int index = 0; index < count; ++index)
        {
            float sum = 0.0f;
            for (int channel = 0; channel < channelsToMix; ++channel)
                sum += channelStorage[static_cast<std::size_t> (channel)]
                                     [static_cast<std::size_t> (
                                         storageStart + index)];
            destination[destinationStart + index] = sum * gain;
        }
        copyTimestamps (
            storageStart, count, timestamps, destinationStart);
    };

    mixBlock (start1, 0, size1);
    mixBlock (start2, size1, size2);
    const auto read = size1 + size2;
    fifo.finishedRead (read);
    return read;
}

int AnalysisFifo::discard (int numSamples) noexcept
{
    if (numSamples <= 0)
        return 0;

    int start1 = 0;
    int size1 = 0;
    int start2 = 0;
    int size2 = 0;
    fifo.prepareToRead (
        numSamples, start1, size1, start2, size2);
    const auto discarded = size1 + size2;
    fifo.finishedRead (discarded);
    return discarded;
}

int AnalysisFifo::getNumReady() const noexcept
{
    return fifo.getNumReady();
}

int AnalysisFifo::getFreeSpace() const noexcept
{
    return fifo.getFreeSpace();
}

int AnalysisFifo::getCapacity() const noexcept
{
    return usableCapacity;
}

int AnalysisFifo::getNumChannels() const noexcept
{
    return static_cast<int> (channelStorage.size());
}

std::uint64_t AnalysisFifo::getDroppedFrameCount() const noexcept
{
    return droppedFrames.load (std::memory_order_relaxed);
}

std::uint64_t AnalysisFifo::getDroppedPushCount() const noexcept
{
    return droppedPushes.load (std::memory_order_relaxed);
}

void AnalysisFifo::copyTimestamps (
    int start, int count, std::uint64_t* destination,
    int destinationOffset) const noexcept
{
    if (destination == nullptr || count <= 0)
        return;
    std::memcpy (
        destination + destinationOffset,
        timestampStorage.data() + start,
        static_cast<std::size_t> (count) * sizeof (std::uint64_t));
}

} // namespace ShredEQ::Analysis
