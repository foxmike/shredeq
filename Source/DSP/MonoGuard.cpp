#include "MonoGuard.h"
#include <algorithm>
#include <cmath>

namespace ShredEQ::DSP
{

MonoGuard::MonoGuard() noexcept
{
    enabled.store (false);
    protectionEnabled.store (false);
    protectionAmount.store (50.0f);

    const double fMin = 30.0;
    const double fMax = 18000.0;
    const double logRatio = std::log (fMax / fMin);
    const double spacing = logRatio / static_cast<double> (numBands - 1);

    for (int b = 0; b < numBands; ++b)
    {
        double freq = fMin * std::exp (static_cast<double> (b) * spacing);
        centerFrequencies[static_cast<size_t> (b)] = static_cast<float> (freq);
    }

    reset();
}

void MonoGuard::prepare (double sampleRate, int maxBlockSize) noexcept
{
    juce::ignoreUnused (maxBlockSize);
    currentSampleRate = (std::isfinite (sampleRate) && sampleRate > 0.0) ? sampleRate : 44100.0;

    const double fMin = 30.0;
    const double fMax = 18000.0;
    const double logRatio = std::log (fMax / fMin);
    const double spacing = logRatio / static_cast<double> (numBands - 1);
    const double bwOct = spacing / std::log (2.0);
    const double Q = std::sqrt (std::pow (2.0, bwOct)) / (std::pow (2.0, bwOct) - 1.0);

    for (int b = 0; b < numBands; ++b)
    {
        double freq = fMin * std::exp (static_cast<double> (b) * spacing);
        centerFrequencies[static_cast<size_t> (b)] = static_cast<float> (freq);
        filters[static_cast<size_t> (b)].prepare (currentSampleRate);
        filters[static_cast<size_t> (b)].setParameters (freq, 0.0, Q, FilterKind::bandPass);
    }

    updateSmoothingCoeff();
    reset();
}

void MonoGuard::reset() noexcept
{
    for (int b = 0; b < numBands; ++b)
    {
        filters[static_cast<size_t> (b)].reset();
        P_LR[static_cast<size_t> (b)] = 0.0;
        P_LL[static_cast<size_t> (b)] = 0.0;
        P_RR[static_cast<size_t> (b)] = 0.0;
        smoothedRho[static_cast<size_t> (b)].store (1.0f);
        smoothedRisk[static_cast<size_t> (b)].store (0.0f);
        bandAtRisk[static_cast<size_t> (b)].store (false);
    }
}

void MonoGuard::setEnabled (bool shouldBeEnabled) noexcept
{
    enabled.store (shouldBeEnabled);
}

void MonoGuard::setProtectionEnabled (bool shouldBeEnabled) noexcept
{
    protectionEnabled.store (shouldBeEnabled);
}

void MonoGuard::setProtectionAmount (float amountPercent) noexcept
{
    if (std::isfinite (amountPercent))
        protectionAmount.store (std::clamp (amountPercent, 0.0f, 100.0f));
}

float MonoGuard::getBandCenterFrequency (int bandIndex) const noexcept
{
    if (bandIndex >= 0 && bandIndex < numBands)
        return centerFrequencies[static_cast<size_t> (bandIndex)];
    return 0.0f;
}

float MonoGuard::getRho (int bandIndex) const noexcept
{
    if (bandIndex >= 0 && bandIndex < numBands)
        return smoothedRho[static_cast<size_t> (bandIndex)].load();
    return 1.0f;
}

float MonoGuard::getRisk (int bandIndex) const noexcept
{
    if (bandIndex >= 0 && bandIndex < numBands)
        return smoothedRisk[static_cast<size_t> (bandIndex)].load();
    return 0.0f;
}

bool MonoGuard::isBandAtRisk (int bandIndex) const noexcept
{
    if (bandIndex >= 0 && bandIndex < numBands)
        return bandAtRisk[static_cast<size_t> (bandIndex)].load();
    return false;
}

uint32_t MonoGuard::getRiskMask() const noexcept
{
    uint32_t mask = 0;
    for (int b = 0; b < numBands; ++b)
    {
        if (bandAtRisk[static_cast<size_t> (b)].load())
            mask |= (1U << b);
    }
    return mask;
}

void MonoGuard::updateTargetGains (
    std::array<float, numBands>& targetGain) noexcept
{
    targetGain.fill (1.0f);
    const bool protect = protectionEnabled.load();
    const float amount =
        std::clamp (protectionAmount.load() / 100.0f, 0.0f, 1.0f);
    const float sideToMidLimit = 1.0f + 10.0f * (1.0f - amount);

    for (int band = 0; band < numBands; ++band)
    {
        const auto index = static_cast<std::size_t> (band);
        const double denominator =
            std::sqrt (std::max (0.0, P_LL[index] * P_RR[index]))
            + static_cast<double> (eps);
        const float rho = std::clamp (
            static_cast<float> (P_LR[index] / denominator), -1.0f, 1.0f);
        smoothedRho[index].store (rho);

        const float midPower = std::max (
            0.0f, static_cast<float> (
                0.5 * (P_LL[index] + P_RR[index] + 2.0 * P_LR[index])));
        const float sidePower = std::max (
            0.0f, static_cast<float> (
                0.5 * (P_LL[index] + P_RR[index] - 2.0 * P_LR[index])));
        const float sideToMid = sidePower / (midPower + eps);
        const float risk =
            std::clamp ((0.2f - rho) / 1.2f, 0.0f, 1.0f);
        smoothedRisk[index].store (risk);
        bandAtRisk[index].store (rho < 0.1f && sidePower > 1.0e-7f);

        if (protect && sideToMid > sideToMidLimit && rho < 0.2f)
        {
            const float totalPower =
                static_cast<float> (P_LL[index] + P_RR[index]);
            const float gate = std::clamp (
                (totalPower - 1.0e-8f) / 9.0e-8f, 0.0f, 1.0f);
            const float limitedGain = std::clamp (
                std::sqrt (sideToMidLimit / (sideToMid + eps)),
                0.05f, 1.0f);
            targetGain[index] =
                1.0f - (1.0f - limitedGain) * risk * gate;
        }
    }
}

void MonoGuard::process (float* left, float* right, int numSamples) noexcept
{
    if (! enabled.load())
        return;

    if (left == nullptr || right == nullptr || numSamples <= 0)
        return;

    const bool protect = protectionEnabled.load();
    std::array<float, numBands> targetGain;
    updateTargetGains (targetGain);

    for (int i = 0; i < numSamples; ++i)
    {
        const float lVal = left[static_cast<size_t> (i)];
        const float rVal = right[static_cast<size_t> (i)];

        float deltaSide = 0.0f;

        for (int b = 0; b < numBands; ++b)
        {
            float lb = lVal;
            float rb = rVal;
            filters[static_cast<size_t> (b)].processStereo (lb, rb);

            const double pLR = static_cast<double> (lb * rb);
            const double pLL = static_cast<double> (lb * lb);
            const double pRR = static_cast<double> (rb * rb);

            // Update running average
            P_LR[static_cast<size_t> (b)] = alpha * P_LR[static_cast<size_t> (b)] + (1.0 - alpha) * pLR;
            P_LL[static_cast<size_t> (b)] = alpha * P_LL[static_cast<size_t> (b)] + (1.0 - alpha) * pLL;
            P_RR[static_cast<size_t> (b)] = alpha * P_RR[static_cast<size_t> (b)] + (1.0 - alpha) * pRR;

            if (protect)
            {
                const float sb = (lb - rb) * invSqrt2;
                deltaSide += (1.0f - targetGain[static_cast<size_t> (b)]) * sb;
            }
        }

        // Refresh within the current block so a freshly prepared processor
        // does not pass its entire first block before protection can react.
        if ((i & 31) == 31)
            updateTargetGains (targetGain);

        if (protect)
        {
            const float mid = (lVal + rVal) * invSqrt2;
            const float side = (lVal - rVal) * invSqrt2;
            const float newSide = side - deltaSide;
            left[static_cast<size_t> (i)] = (mid + newSide) * invSqrt2;
            right[static_cast<size_t> (i)] = (mid - newSide) * invSqrt2;
        }
    }
}

void MonoGuard::processMS (float* mid, float* side, int numSamples) noexcept
{
    if (! enabled.load())
        return;

    if (mid == nullptr || side == nullptr || numSamples <= 0)
        return;

    const bool protect = protectionEnabled.load();
    std::array<float, numBands> targetGain;
    updateTargetGains (targetGain);

    for (int i = 0; i < numSamples; ++i)
    {
        const float midVal = mid[static_cast<size_t> (i)];
        const float sideVal = side[static_cast<size_t> (i)];

        // Reconstruct Left and Right for analysis
        const float lVal = (midVal + sideVal) * invSqrt2;
        const float rVal = (midVal - sideVal) * invSqrt2;

        float deltaSide = 0.0f;

        for (int b = 0; b < numBands; ++b)
        {
            float lb = lVal;
            float rb = rVal;
            filters[static_cast<size_t> (b)].processStereo (lb, rb);

            const double pLR = static_cast<double> (lb * rb);
            const double pLL = static_cast<double> (lb * lb);
            const double pRR = static_cast<double> (rb * rb);

            // Update running average
            P_LR[static_cast<size_t> (b)] = alpha * P_LR[static_cast<size_t> (b)] + (1.0 - alpha) * pLR;
            P_LL[static_cast<size_t> (b)] = alpha * P_LL[static_cast<size_t> (b)] + (1.0 - alpha) * pLL;
            P_RR[static_cast<size_t> (b)] = alpha * P_RR[static_cast<size_t> (b)] + (1.0 - alpha) * pRR;

            if (protect)
            {
                const float sb = (lb - rb) * invSqrt2;
                deltaSide += (1.0f - targetGain[static_cast<size_t> (b)]) * sb;
            }
        }

        if ((i & 31) == 31)
            updateTargetGains (targetGain);

        if (protect)
        {
            side[static_cast<size_t> (i)] = sideVal - deltaSide;
        }
    }
}

void MonoGuard::process (float* const* channels, int numChannels, int numSamples) noexcept
{
    if (channels != nullptr && numChannels >= 2)
    {
        process (channels[0], channels[1], numSamples);
    }
}

void MonoGuard::processMS (float* const* channels, int numChannels, int numSamples) noexcept
{
    if (channels != nullptr && numChannels >= 2)
    {
        processMS (channels[0], channels[1], numSamples);
    }
}

void MonoGuard::process (juce::AudioBuffer<float>& buffer) noexcept
{
    process (buffer.getArrayOfWritePointers(), buffer.getNumChannels(), buffer.getNumSamples());
}

void MonoGuard::processMS (juce::AudioBuffer<float>& buffer) noexcept
{
    processMS (buffer.getArrayOfWritePointers(), buffer.getNumChannels(), buffer.getNumSamples());
}

void MonoGuard::updateSmoothingCoeff() noexcept
{
    const double timeConstantSeconds = 0.1; // 100 ms
    alpha = static_cast<float> (std::exp (-1.0 / (currentSampleRate * timeConstantSeconds)));
    alpha = std::clamp (alpha, 0.0f, 0.99999f);
}

} // namespace ShredEQ::DSP
