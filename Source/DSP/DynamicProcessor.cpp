#include "DynamicProcessor.h"
#include <algorithm>
#include <cmath>

namespace ShredEQ::DSP
{

DynamicProcessor::DynamicProcessor() noexcept
{
    // Initialize separator with reasonable defaults
    separator.setFastTimeConstants (10.0, 150.0);
    separator.setSlowTimeConstants (50.0, 1000.0);
    separator.setK (1.0);
    separator.setLambda (0.0);

    reset();
}

void DynamicProcessor::prepare (double newSampleRate) noexcept
{
    currentSampleRate = newSampleRate;
    envelopeFollower.prepare (currentSampleRate);
    separator.prepare (currentSampleRate);
    gainSmoother.prepare (currentSampleRate);
    bandPassFilter.prepare (currentSampleRate);

    // Apply the time constants and bandpass settings
    setAttackMs (attackMs);
    setReleaseMs (releaseMs);
    updateBandPassFilter();
    reset();
}

void DynamicProcessor::reset() noexcept
{
    envelopeFollower.reset();
    separator.reset();
    gainSmoother.reset (0.0);
    bandPassFilter.reset();

    lastGainLinear = 1.0f;
    lastGainChangeDb = 0.0f;
    lastGainReductionDb = 0.0f;
    heldTargetDb = 0.0;
    holdSamplesRemaining = 0;

    visualizationData.staticGainDb = static_cast<float> (staticGainDb);
    visualizationData.maxDynamicRangeDb = static_cast<float> (rangeDb);
    visualizationData.currentGainDb = static_cast<float> (staticGainDb);
    visualizationData.thresholdDb = static_cast<float> (thresholdDb);
    visualizationData.detectorLevelDb = -120.0f;
    visualizationData.reactionFlags = 0;
    visualizationData.writeIndex = 0;
    visualizationData.gainReductionHistory.fill (0.0f);
}

float DynamicProcessor::processSample (float primarySample, const DetectorContext& context) noexcept
{
    float rawSample = primarySample;

    switch (detectorSource)
    {
        case DetectorSource::fullBandInternal:
        case DetectorSource::bandLimitedInternal:
        case DetectorSource::transientComponent:
        case DetectorSource::sustainComponent:
        {
            if (mode == DynamicMode::sidechained)
            {
                rawSample = (std::abs (context.externalSidechain) > 0.0f) ? context.externalSidechain : primarySample;
            }
            else
            {
                rawSample = primarySample;
            }
            break;
        }
        case DetectorSource::left:
            rawSample = context.left;
            break;
        case DetectorSource::right:
            rawSample = context.right;
            break;
        case DetectorSource::mid:
            rawSample = context.mid;
            break;
        case DetectorSource::side:
            rawSample = context.side;
            break;
        case DetectorSource::externalSidechain:
            rawSample = context.externalSidechain;
            break;
        case DetectorSource::inputMinusOutput:
            rawSample = context.input - context.output;
            break;
    }

    float filteredSample = rawSample;
    if (detectorSource == DetectorSource::bandLimitedInternal)
    {
        filteredSample = bandPassFilter.processSample (rawSample, 0);
    }

    const bool useTransient = (detectorSource == DetectorSource::transientComponent || mode == DynamicMode::transientOnly);
    const bool useSustain = (detectorSource == DetectorSource::sustainComponent || mode == DynamicMode::sustainOnly);

    float detectorLevelLinear = 0.0f;

    if (useTransient || useSustain)
    {
        auto levels = separator.processSample (filteredSample);
        detectorLevelLinear = useTransient ? levels.transient : levels.sustain;
    }
    else
    {
        detectorLevelLinear = envelopeFollower.processSample (filteredSample);
    }

    const double levelDb = 20.0 * std::log10 (static_cast<double> (detectorLevelLinear) + 1.0e-12);

    double g_target = 0.0;

    if (mode != DynamicMode::staticEq)
    {
        const double safeRatio = std::max (1.0, ratio);
        const double safeRange = std::abs (rangeDb);
        const double safeKnee = std::max (0.0, kneeDb);

        if (mode == DynamicMode::expansion)
        {
            const double u = thresholdDb - levelDb;
            const double curve = computeSoftKneeCurveDb (u, safeKnee);
            g_target = -std::min (safeRange, (safeRatio - 1.0) * curve);
        }
        else if (mode == DynamicMode::upwardDynamic)
        {
            const double u = levelDb - thresholdDb;
            const double curve = computeSoftKneeCurveDb (u, safeKnee);
            g_target = std::min (safeRange, (1.0 - 1.0 / safeRatio) * curve);
        }
        else if (mode == DynamicMode::compression)
        {
            const double u = levelDb - thresholdDb;
            const double curve = computeSoftKneeCurveDb (u, safeKnee);
            const double action = std::min (safeRange, (1.0 - 1.0 / safeRatio) * curve);
            g_target = (staticGainDb >= 0.0) ? -action : action;
        }
        else
        {
            // downwardDynamic, transientOnly, sustainOnly, sidechained
            const double u = levelDb - thresholdDb;
            const double curve = computeSoftKneeCurveDb (u, safeKnee);
            g_target = -std::min (safeRange, (1.0 - 1.0 / safeRatio) * curve);
        }
    }

    if (std::abs (g_target) > std::abs (heldTargetDb))
    {
        heldTargetDb = g_target;
        holdSamplesRemaining = static_cast<int> (
            std::max (0.0, holdMs) * 0.001 * currentSampleRate);
    }
    else if (holdSamplesRemaining > 0)
    {
        --holdSamplesRemaining;
    }
    else
    {
        heldTargetDb = g_target;
    }

    const double smoothedGainDb = gainSmoother.processSample (heldTargetDb);

    float multiplier = 1.0f;
    if (mode != DynamicMode::staticEq && std::abs (staticGainDb) > 1.0e-5)
    {
        multiplier = 1.0f + static_cast<float> (smoothedGainDb / staticGainDb);
    }

    lastGainLinear = multiplier;
    lastGainChangeDb = static_cast<float> (smoothedGainDb);
    lastGainReductionDb = static_cast<float> (std::abs (smoothedGainDb));

    // Update visualization data
    visualizationData.staticGainDb = static_cast<float> (staticGainDb);
    visualizationData.maxDynamicRangeDb = static_cast<float> (rangeDb);
    visualizationData.currentGainDb = static_cast<float> (staticGainDb + smoothedGainDb);
    visualizationData.thresholdDb = static_cast<float> (thresholdDb);
    visualizationData.detectorLevelDb = static_cast<float> (levelDb);

    uint32_t flags = 0;
    if (useTransient)
        flags |= (1 << 0);
    if (useSustain)
        flags |= (1 << 1);
    if (detectorSource == DetectorSource::externalSidechain || mode == DynamicMode::sidechained)
        flags |= (1 << 2);

    visualizationData.reactionFlags = flags;

    pushGainReductionHistory (lastGainReductionDb);

    return multiplier;
}

float DynamicProcessor::processSample (float primarySample, const DetectorContext& context, float currentStaticGainDb) noexcept
{
    staticGainDb = currentStaticGainDb;
    return processSample (primarySample, context);
}

void DynamicProcessor::process (const float* primaryInput, float* multiplierOutput, int numSamples, const DetectorContext* contexts) noexcept
{
    if (primaryInput == nullptr || multiplierOutput == nullptr || numSamples <= 0)
        return;

    for (int i = 0; i < numSamples; ++i)
    {
        DetectorContext ctx;
        if (contexts != nullptr)
        {
            ctx = contexts[i];
        }
        multiplierOutput[i] = processSample (primaryInput[i], ctx);
    }
}

void DynamicProcessor::setMode (DynamicMode newMode) noexcept
{
    mode = newMode;
}

void DynamicProcessor::setDetectorSource (DetectorSource newSource) noexcept
{
    detectorSource = newSource;
}

void DynamicProcessor::setThresholdDb (double newThresholdDb) noexcept
{
    thresholdDb = newThresholdDb;
}

void DynamicProcessor::setRatio (double newRatio) noexcept
{
    ratio = newRatio;
}

void DynamicProcessor::setKneeDb (double newKneeDb) noexcept
{
    kneeDb = newKneeDb;
}

void DynamicProcessor::setRangeDb (double newRangeDb) noexcept
{
    rangeDb = newRangeDb;
}

void DynamicProcessor::setAttackMs (double newAttackMs) noexcept
{
    attackMs = newAttackMs;
    envelopeFollower.setAttackMs (attackMs);
    gainSmoother.setAttackMs (attackMs);

    separator.setFastAttackMs (attackMs);
    separator.setSlowAttackMs (attackMs * 5.0);
}

void DynamicProcessor::setHoldMs (double newHoldMs) noexcept
{
    holdMs = std::clamp (newHoldMs, 0.0, 500.0);
}

void DynamicProcessor::setReleaseMs (double newReleaseMs) noexcept
{
    releaseMs = newReleaseMs;
    envelopeFollower.setReleaseMs (releaseMs);
    gainSmoother.setReleaseMs (releaseMs);

    separator.setFastReleaseMs (releaseMs);
    separator.setSlowReleaseMs (releaseMs * 5.0);
}

void DynamicProcessor::setStaticGainDb (double newStaticGainDb) noexcept
{
    staticGainDb = newStaticGainDb;
}

void DynamicProcessor::setDetectorFrequency (double newFreqHz) noexcept
{
    detectorFrequency = newFreqHz;
    updateBandPassFilter();
}

void DynamicProcessor::setDetectorQ (double newQ) noexcept
{
    detectorQ = newQ;
    updateBandPassFilter();
}

void DynamicProcessor::pushGainReductionHistory (float grDb) noexcept
{
    visualizationData.gainReductionHistory[visualizationData.writeIndex] = grDb;
    visualizationData.writeIndex = (visualizationData.writeIndex + 1) % DynamicVisualizationData::historySize;
}

void DynamicProcessor::updateBandPassFilter() noexcept
{
    bandPassFilter.setParameters (detectorFrequency, 0.0, detectorQ, FilterKind::bandPass);
}

} // namespace ShredEQ::DSP
