#include "LookaheadDelay.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace ShredEQ::DSP
{

void LookaheadDelay::prepare (double sampleRate, int maxBlockSize, int numChannels)
{
    juce::ignoreUnused (maxBlockSize);

    const auto validSampleRate = std::isfinite (sampleRate) && sampleRate > 0.0
                                   ? sampleRate
                                   : 44100.0;
    preparedSampleRate.store (validSampleRate);

    const auto maximumDelaySamples = static_cast<int> (
        std::ceil (maximumDelayMilliseconds * validSampleRate / 1000.0));

    ringLength = std::max (2, maximumDelaySamples + 1);
    preparedChannels = std::max (0, numChannels);
    delayBuffer.setSize (preparedChannels, ringLength, false, true, false);

    const auto mode = requestedMode.load();
    requestedDelaySamples.store (samplesForMode (mode));

    transitionLengthSamples = std::max (
        1, static_cast<int> (std::round (
               crossfadeMilliseconds * validSampleRate / 1000.0)));

    reset();
}

void LookaheadDelay::reset() noexcept
{
    delayBuffer.clear();
    writeIndex = 0;

    currentDelaySamples = requestedDelaySamples.load();
    transitionFromDelaySamples = currentDelaySamples;
    transitionToDelaySamples = currentDelaySamples;
    transitionPosition = 0;
    transitionActive = false;
}

void LookaheadDelay::process (juce::AudioBuffer<float>& buffer) noexcept
{
    const auto numSamples = buffer.getNumSamples();
    const auto numChannels = std::min (buffer.getNumChannels(), preparedChannels);

    if (numSamples <= 0 || numChannels <= 0 || ringLength <= 0)
        return;

    auto* const* channelData = buffer.getArrayOfWritePointers();

    for (int sample = 0; sample < numSamples; ++sample)
    {
        beginTransitionIfNeeded();

        auto fromReadIndex = writeIndex - transitionFromDelaySamples;
        if (fromReadIndex < 0)
            fromReadIndex += ringLength;

        auto toReadIndex = writeIndex - transitionToDelaySamples;
        if (toReadIndex < 0)
            toReadIndex += ringLength;

        const auto mix = transitionActive && transitionLengthSamples > 1
                           ? static_cast<float> (transitionPosition)
                                 / static_cast<float> (transitionLengthSamples - 1)
                           : 1.0f;

        for (int channel = 0; channel < numChannels; ++channel)
        {
            auto* const samples = channelData[channel];
            const auto input = samples[sample];
            delayBuffer.setSample (channel, writeIndex, input);

            if (transitionActive)
            {
                const auto from = delayBuffer.getSample (channel, fromReadIndex);
                const auto to = delayBuffer.getSample (channel, toReadIndex);
                samples[sample] = from + mix * (to - from);
            }
            else if (currentDelaySamples > 0)
            {
                samples[sample] = delayBuffer.getSample (channel, toReadIndex);
            }
            // A settled Off mode deliberately leaves the output untouched.
        }

        if (++writeIndex == ringLength)
            writeIndex = 0;

        if (transitionActive)
        {
            ++transitionPosition;
            if (transitionPosition >= transitionLengthSamples)
            {
                currentDelaySamples = transitionToDelaySamples;
                transitionFromDelaySamples = currentDelaySamples;
                transitionPosition = 0;
                transitionActive = false;
            }
        }
    }
}

void LookaheadDelay::setMode (Mode newMode) noexcept
{
    switch (newMode)
    {
        case Mode::Off:
        case Mode::Milliseconds0_5:
        case Mode::Milliseconds1:
        case Mode::Milliseconds2:
        case Mode::Milliseconds5:
            break;
        default:
            newMode = Mode::Off;
            break;
    }

    requestedMode.store (newMode);
    requestedDelaySamples.store (samplesForMode (newMode));
}

LookaheadDelay::Mode LookaheadDelay::getMode() const noexcept
{
    return requestedMode.load();
}

void LookaheadDelay::setDelayMilliseconds (double milliseconds) noexcept
{
    if (!std::isfinite (milliseconds) || milliseconds <= 0.0)
    {
        setMode (Mode::Off);
        return;
    }

    struct ModeMilliseconds
    {
        Mode mode;
        double milliseconds;
    };

    static constexpr std::array<ModeMilliseconds, 5> allowedModes {{
        { Mode::Off, 0.0 },
        { Mode::Milliseconds0_5, 0.5 },
        { Mode::Milliseconds1, 1.0 },
        { Mode::Milliseconds2, 2.0 },
        { Mode::Milliseconds5, 5.0 }
    }};

    auto nearest = allowedModes.front();
    auto nearestDistance = std::abs (milliseconds - nearest.milliseconds);

    for (const auto candidate : allowedModes)
    {
        const auto distance = std::abs (milliseconds - candidate.milliseconds);
        if (distance < nearestDistance)
        {
            nearest = candidate;
            nearestDistance = distance;
        }
    }

    setMode (nearest.mode);
}

int LookaheadDelay::getLatencySamples() const noexcept
{
    return requestedDelaySamples.load();
}

double LookaheadDelay::millisecondsForMode (Mode mode) noexcept
{
    switch (mode)
    {
        case Mode::Milliseconds0_5: return 0.5;
        case Mode::Milliseconds1:   return 1.0;
        case Mode::Milliseconds2:   return 2.0;
        case Mode::Milliseconds5:   return 5.0;
        case Mode::Off:
        default:                    return 0.0;
    }
}

int LookaheadDelay::samplesForMode (Mode mode) const noexcept
{
    return static_cast<int> (std::round (
        millisecondsForMode (mode) * preparedSampleRate.load() / 1000.0));
}

void LookaheadDelay::beginTransitionIfNeeded() noexcept
{
    if (transitionActive)
        return;

    const auto requested = std::clamp (
        requestedDelaySamples.load(), 0, ringLength - 1);

    if (requested == currentDelaySamples)
    {
        transitionFromDelaySamples = currentDelaySamples;
        transitionToDelaySamples = currentDelaySamples;
        return;
    }

    transitionFromDelaySamples = currentDelaySamples;
    transitionToDelaySamples = requested;
    transitionPosition = 0;
    transitionActive = true;
}

} // namespace ShredEQ::DSP
