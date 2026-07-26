#include "MidSideRouter.h"
#include <algorithm>
#include <cmath>

namespace ShredEQ::DSP
{

MidSideRouter::MidSideRouter() noexcept
{
    smoothedFrequency.setCurrentAndTargetValue (150.0);
}

void MidSideRouter::prepare (double sampleRate, int maxBlockSize) noexcept
{
    juce::ignoreUnused (maxBlockSize);
    currentSampleRate = (std::isfinite (sampleRate) && sampleRate > 0.0) ? sampleRate : 44100.0;

    smoothedFrequency.reset (currentSampleRate, 0.02); // 20 ms smoothing duration
    sideHPF.prepare (currentSampleRate);

    reset();
}

void MidSideRouter::reset() noexcept
{
    sideHPF.reset();
    smoothedFrequency.setCurrentAndTargetValue (targetFrequency.load());
    inMidSideDomain.store (false);
}

void MidSideRouter::setEnabled (bool shouldBeEnabled) noexcept
{
    enabled.store (shouldBeEnabled);
    if (!shouldBeEnabled)
    {
        inMidSideDomain.store (false);
    }
}

void MidSideRouter::setSideHighPassEnabled (bool shouldBeEnabled) noexcept
{
    sideHighPassEnabled.store (shouldBeEnabled);
}

void MidSideRouter::setSideHighPassHz (float frequencyHz) noexcept
{
    if (std::isfinite (frequencyHz))
    {
        targetFrequency.store (std::clamp (frequencyHz, 120.0f, 300.0f));
    }
}

float MidSideRouter::getSideHighPassHz() const noexcept
{
    return targetFrequency.load();
}

bool MidSideRouter::isMidSideDomain() const noexcept
{
    return inMidSideDomain.load();
}

void MidSideRouter::processEncode (float* left, float* right, int numSamples) noexcept
{
    const double targetHz = static_cast<double> (targetFrequency.load());
    smoothedFrequency.setTargetValue (targetHz);

    if (enabled.load())
    {
        inMidSideDomain.store (true);

        if (left != nullptr && right != nullptr)
        {
            const bool hpfActive = sideHighPassEnabled.load();

            for (int i = 0; i < numSamples; ++i)
            {
                float l = left[i];
                float r = right[i];

                float mid = (l + r) * invSqrt2;
                float side = (l - r) * invSqrt2;

                if (hpfActive)
                {
                    double currentFreq = smoothedFrequency.isSmoothing() ? smoothedFrequency.getNextValue() : smoothedFrequency.getCurrentValue();
                    if (!std::isfinite (currentFreq))
                        currentFreq = 150.0;

                    const double nyquistGuard = currentSampleRate * 0.499;
                    currentFreq = std::clamp (currentFreq, 120.0, std::max (120.0, nyquistGuard));

                    sideHPF.setParameters (currentFreq, 0.0, 0.7071067811865476, TPTFilter::Type::highPass);
                    side = sideHPF.processSample (side, 0);
                }
                else
                {
                    if (smoothedFrequency.isSmoothing())
                        smoothedFrequency.getNextValue();
                }

                left[i] = mid;
                right[i] = side;
            }
        }
    }
    else
    {
        // Router is bypassed. Advance the frequency smoother to prevent jumps when enabled
        if (smoothedFrequency.isSmoothing())
        {
            for (int i = 0; i < numSamples; ++i)
                smoothedFrequency.getNextValue();
        }
    }
}

void MidSideRouter::processDecode (float* left, float* right, int numSamples) noexcept
{
    if (enabled.load())
    {
        inMidSideDomain.store (false);

        if (left != nullptr && right != nullptr)
        {
            for (int i = 0; i < numSamples; ++i)
            {
                float mid = left[i];
                float side = right[i];

                float l = (mid + side) * invSqrt2;
                float r = (mid - side) * invSqrt2;

                left[i] = l;
                right[i] = r;
            }
        }
    }
}

void MidSideRouter::processEncode (float* const* channels, int numChannels, int numSamples) noexcept
{
    if (channels != nullptr && numChannels >= 2)
    {
        processEncode (channels[0], channels[1], numSamples);
    }
    else
    {
        // Mono or invalid channels, advance smoother
        const double targetHz = static_cast<double> (targetFrequency.load());
        smoothedFrequency.setTargetValue (targetHz);
        if (smoothedFrequency.isSmoothing())
        {
            for (int i = 0; i < numSamples; ++i)
                smoothedFrequency.getNextValue();
        }
    }
}

void MidSideRouter::processDecode (float* const* channels, int numChannels, int numSamples) noexcept
{
    if (channels != nullptr && numChannels >= 2)
    {
        processDecode (channels[0], channels[1], numSamples);
    }
}

void MidSideRouter::processEncode (juce::AudioBuffer<float>& buffer) noexcept
{
    processEncode (buffer.getArrayOfWritePointers(), buffer.getNumChannels(), buffer.getNumSamples());
}

void MidSideRouter::processDecode (juce::AudioBuffer<float>& buffer) noexcept
{
    processDecode (buffer.getArrayOfWritePointers(), buffer.getNumChannels(), buffer.getNumSamples());
}

void MidSideRouter::encode (float& left, float& right) noexcept
{
    float l = left;
    float r = right;
    left = (l + r) * invSqrt2;
    right = (l - r) * invSqrt2;
}

void MidSideRouter::decode (float& mid, float& side) noexcept
{
    float m = mid;
    float s = side;
    mid = (m + s) * invSqrt2;
    side = (m - s) * invSqrt2;
}

} // namespace ShredEQ::DSP
