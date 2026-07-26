#include "ChugStabilizer.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <algorithm>
#include <cmath>

namespace ShredEQ::DSP
{
namespace
{
constexpr double detectorEpsilon = 1.0e-12;

[[nodiscard]] double finiteOr (double value, double fallback) noexcept
{
    return std::isfinite (value) ? value : fallback;
}

[[nodiscard]] double coefficientForMs (double timeMs,
                                       double sampleRate) noexcept
{
    if (timeMs <= 0.0)
        return 0.0;

    return std::exp (-1.0 / (sampleRate * timeMs * 0.001));
}

[[nodiscard]] double followMagnitude (double magnitude, double current,
                                      double attackCoefficient,
                                      double releaseCoefficient) noexcept
{
    const auto coefficient =
        magnitude > current ? attackCoefficient : releaseCoefficient;
    const auto result =
        coefficient * current + (1.0 - coefficient) * magnitude;
    return result < 1.0e-30 ? 0.0 : result;
}

[[nodiscard]] double linearToDb (double value) noexcept
{
    return std::max (-120.0, 20.0 * std::log10 (value + detectorEpsilon));
}

[[nodiscard]] double bandwidthToQ (double octaves) noexcept
{
    const auto ratio = std::pow (2.0, octaves);
    return std::sqrt (ratio) / std::max (ratio - 1.0, 1.0e-6);
}
} // namespace

void ChugStabilizer::prepare (double sampleRate, int maxBlockSize) noexcept
{
    (void) maxBlockSize;
    currentSampleRate =
        std::isfinite (sampleRate) && sampleRate > 1000.0 ? sampleRate : 44100.0;
    reductionFilter.prepare (currentSampleRate);
    updateDetectorCoefficients (targetFrequencyHz.load(),
                                bandwidthOctaves.load());
    reset();
}

void ChugStabilizer::reset() noexcept
{
    lowDetectorState = {};
    midDetectorState = {};
    pickDetectorState = {};
    reductionFilter.reset();
    lowEnvelope = 0.0;
    midEnvelope = 0.0;
    broadFastEnvelope = 0.0;
    broadSlowEnvelope = 0.0;
    pickEnvelope = 0.0;
    autoLowBaseline = 1.0e-4;
    previousLowDb = detectorFloorDb;
    reductionDb = 0.0;
    holdSamplesRemaining = 0;
    eventSamples = 0;

    visualEvent.store (false);
    visualLowDb.store (static_cast<float> (detectorFloorDb));
    visualMidDb.store (static_cast<float> (detectorFloorDb));
    visualReductionDb.store (0.0f);
    visualCurveGainDb.store (0.0f);
    visualEventDurationMs.store (0.0f);
    visualTrigger.store (TriggerSource::none);
}

void ChugStabilizer::process (juce::AudioBuffer<float>& buffer) noexcept
{
    process (buffer.getArrayOfWritePointers(), buffer.getNumChannels(),
             buffer.getNumSamples());
}

void ChugStabilizer::process (float* const* channels, int numChannels,
                              int numSamples) noexcept
{
    if (channels == nullptr || numChannels <= 0 || numSamples <= 0)
        return;

    const auto activeChannels = std::min (numChannels, 2);
    if (channels[0] == nullptr
        || (activeChannels > 1 && channels[1] == nullptr))
        return;

    if (!enabled.load())
    {
        // Do not leave detector/filter history suspended across bypass. This
        // is state-only work and does not touch the pass-through samples.
        reset();
        return;
    }

    const auto frequency =
        std::clamp (finiteOr (targetFrequencyHz.load(), 120.0),
                    minimumFrequencyHz, maximumFrequencyHz);
    const auto bandwidth =
        std::clamp (finiteOr (bandwidthOctaves.load(), 1.2),
                    minimumBandwidthOctaves, maximumBandwidthOctaves);
    if (frequency != configuredDetectorFrequencyHz
        || bandwidth != configuredDetectorBandwidth)
        updateDetectorCoefficients (frequency, bandwidth);

    const auto q = bandwidthToQ (bandwidth);
    const auto supportGain =
        std::clamp (finiteOr (staticSupportGainDb.load(), 0.0), -6.0, 6.0);
    const auto type = filterType.load() == FilterType::lowShelf
                          ? FilterKind::lowShelf
                          : FilterKind::bell;
    const auto audition = listenMode.load();

    for (int sampleIndex = 0; sampleIndex < numSamples; ++sampleIndex)
    {
        const auto inputLeft = channels[0][sampleIndex];
        const auto inputRight =
            activeChannels > 1 ? channels[1][sampleIndex] : inputLeft;
        const auto detectorInput =
            activeChannels > 1
                ? 0.5 * (static_cast<double> (inputLeft)
                         + static_cast<double> (inputRight))
                : static_cast<double> (inputLeft);

        const auto snapshot = detect (detectorInput);
        const auto smoothedReduction =
            smoothReduction (snapshot.targetReductionDb);
        const auto curveGainDb = supportGain - smoothedReduction;

        reductionFilter.setParameters (frequency, curveGainDb, q, type);

        const auto processedLeft =
            reductionFilter.processSample (inputLeft, 0);
        const auto processedRight =
            activeChannels > 1
                ? reductionFilter.processSample (inputRight, 1)
                : processedLeft;

        if (audition == ListenMode::removedContribution)
        {
            channels[0][sampleIndex] = inputLeft - processedLeft;
            if (activeChannels > 1)
                channels[1][sampleIndex] = inputRight - processedRight;
        }
        else
        {
            channels[0][sampleIndex] = processedLeft;
            if (activeChannels > 1)
                channels[1][sampleIndex] = processedRight;
        }

        if (snapshot.event)
            ++eventSamples;
        else if (smoothedReduction <= 0.01)
            eventSamples = 0;

        visualEvent.store (snapshot.event);
        visualLowDb.store (static_cast<float> (snapshot.lowDb));
        visualMidDb.store (static_cast<float> (snapshot.midDb));
        visualReductionDb.store (static_cast<float> (smoothedReduction));
        visualCurveGainDb.store (static_cast<float> (curveGainDb));
        visualEventDurationMs.store (
            static_cast<float> (1000.0 * static_cast<double> (eventSamples)
                                / currentSampleRate));
        visualTrigger.store (snapshot.trigger);
    }
}

ChugStabilizer::DetectorSnapshot
ChugStabilizer::detect (double monoInput) noexcept
{
    const auto safeInput = std::isfinite (monoInput) ? monoInput : 0.0;
    const auto lowSample =
        processBiquad (safeInput, lowDetectorCoefficients, lowDetectorState);
    const auto midSample =
        processBiquad (safeInput, midDetectorCoefficients, midDetectorState);
    const auto pickSample =
        processBiquad (safeInput, pickDetectorCoefficients, pickDetectorState);

    const auto detectorAttack = coefficientForMs (1.0, currentSampleRate);
    const auto detectorRelease = coefficientForMs (55.0, currentSampleRate);
    lowEnvelope = followMagnitude (std::abs (lowSample), lowEnvelope,
                                   detectorAttack, detectorRelease);
    midEnvelope = followMagnitude (std::abs (midSample), midEnvelope,
                                   detectorAttack, detectorRelease);
    pickEnvelope = followMagnitude (
        std::abs (pickSample), pickEnvelope,
        coefficientForMs (0.2, currentSampleRate),
        coefficientForMs (18.0, currentSampleRate));
    broadFastEnvelope = followMagnitude (
        std::abs (safeInput), broadFastEnvelope,
        coefficientForMs (0.2, currentSampleRate),
        coefficientForMs (12.0, currentSampleRate));
    broadSlowEnvelope = followMagnitude (
        std::abs (safeInput), broadSlowEnvelope,
        coefficientForMs (12.0, currentSampleRate),
        coefficientForMs (90.0, currentSampleRate));

    const auto lowDb = linearToDb (lowEnvelope);
    const auto midDb = linearToDb (midEnvelope);

    // A slow baseline supplies T_low in automatic mode. Its rise is
    // deliberately slower than its fall so a chug cannot immediately raise
    // its own threshold.
    const auto baselineCoefficient =
        lowEnvelope > autoLowBaseline
            ? coefficientForMs (2500.0, currentSampleRate)
            : coefficientForMs (700.0, currentSampleRate);
    autoLowBaseline =
        baselineCoefficient * autoLowBaseline
        + (1.0 - baselineCoefficient) * lowEnvelope;

    const auto threshold = effectiveThresholdDb();
    const auto derivativeDbPerMs =
        std::max (0.0, (lowDb - previousLowDb)
                           * currentSampleRate / 1000.0);
    previousLowDb = lowDb;

    // Spec 7.2:
    // C[n] = w1*max(0,L_low-T_low)
    //      + w2*max(0,dL_low/dt-T_d)
    //      + w3*max(0,L_low-L_mid-Delta).
    constexpr double derivativeThresholdDbPerMs = 0.02;
    constexpr double dominanceDeltaDb = 4.0;
    const auto energyTerm = std::max (0.0, lowDb - threshold);
    const auto derivativeTerm =
        std::max (0.0, derivativeDbPerMs - derivativeThresholdDbPerMs);
    const auto dominanceTerm =
        std::max (0.0, lowDb - midDb - dominanceDeltaDb);

    const auto transientSens =
        std::clamp (finiteOr (transientSensitivity.load(), 0.5), 0.0, 1.0);
    const auto dominanceSens =
        std::clamp (finiteOr (dominanceSensitivity.load(), 0.5), 0.0, 1.0);
    constexpr double w1 = 1.0 / 12.0;
    const auto w2 = (0.2 + 1.8 * transientSens) / 0.8;
    const auto w3 = (0.2 + 1.8 * dominanceSens) / 12.0;
    auto score = w1 * energyTerm + w2 * derivativeTerm
                 + w3 * dominanceTerm;

    // A pick has fast broadband/high-frequency excess, while a palm mute has
    // low/mid dominance. Only the former receives preservation.
    const auto broadbandTransientDb =
        20.0 * std::log10 ((broadFastEnvelope + pickEnvelope + detectorEpsilon)
                           / (broadSlowEnvelope + detectorEpsilon));
    if (preservePick.load() && dominanceTerm <= 0.0
        && broadbandTransientDb > 3.0)
    {
        const auto preservation =
            std::clamp ((broadbandTransientDb - 3.0) / 12.0, 0.0, 0.8);
        score *= 1.0 - preservation;
    }

    TriggerSource trigger = TriggerSource::none;
    auto largestContribution = 0.0;
    const std::array<std::pair<double, TriggerSource>, 3> contributions {{
        { w1 * energyTerm, TriggerSource::rawEnergy },
        { w2 * derivativeTerm, TriggerSource::transientAcceleration },
        { w3 * dominanceTerm, TriggerSource::dominance }
    }};
    for (const auto& contribution : contributions)
    {
        if (contribution.first > largestContribution)
        {
            largestContribution = contribution.first;
            trigger = contribution.second;
        }
    }

    const auto cappedScore = std::clamp (score, 0.0, 1.0);
    const auto maxReduction =
        std::clamp (finiteOr (maximumReductionDb.load(), 8.0), 0.0, 18.0);
    return { lowDb, midDb, score, cappedScore * maxReduction, trigger,
             score > 0.02 };
}

double ChugStabilizer::smoothReduction (
    double targetReductionDb) noexcept
{
    const auto target =
        std::clamp (finiteOr (targetReductionDb, 0.0), 0.0, 18.0);
    const auto attack =
        std::clamp (finiteOr (attackTimeMs.load(), 3.0), 0.2, 20.0);
    const auto hold =
        std::clamp (finiteOr (holdTimeMs.load(), 25.0), 0.0, 80.0);
    const auto release =
        std::clamp (finiteOr (releaseTimeMs.load(), 100.0), 15.0, 350.0);

    if (target > reductionDb)
    {
        const auto coefficient = coefficientForMs (attack, currentSampleRate);
        reductionDb =
            coefficient * reductionDb + (1.0 - coefficient) * target;
        holdSamplesRemaining = static_cast<std::uint64_t> (
            hold * 0.001 * currentSampleRate);
    }
    else if (holdSamplesRemaining > 0)
    {
        --holdSamplesRemaining;
    }
    else
    {
        const auto coefficient = coefficientForMs (release, currentSampleRate);
        reductionDb =
            coefficient * reductionDb + (1.0 - coefficient) * target;
    }

    if (reductionDb < 1.0e-9)
        reductionDb = 0.0;
    return reductionDb;
}

double ChugStabilizer::processBiquad (
    double input, const BiquadCoefficients& coefficients,
    BiquadState& state) noexcept
{
    const auto output = coefficients.b0 * input + state.z1;
    state.z1 =
        coefficients.b1 * input - coefficients.a1 * output + state.z2;
    state.z2 = coefficients.b2 * input - coefficients.a2 * output;
    if (std::abs (state.z1) < stateFlushThreshold)
        state.z1 = 0.0;
    if (std::abs (state.z2) < stateFlushThreshold)
        state.z2 = 0.0;
    return output;
}

void ChugStabilizer::updateDetectorCoefficients (
    double frequencyHz, double detectorBandwidthOctaves) noexcept
{
    configuredDetectorFrequencyHz =
        std::clamp (frequencyHz, minimumFrequencyHz, maximumFrequencyHz);
    configuredDetectorBandwidth =
        std::clamp (detectorBandwidthOctaves, minimumBandwidthOctaves,
                    maximumBandwidthOctaves);
    lowDetectorCoefficients = BiquadCoefficients::makeSecondOrder (
        FilterKind::bandPass, currentSampleRate,
        configuredDetectorFrequencyHz,
        bandwidthToQ (configuredDetectorBandwidth), 0.0);
    midDetectorCoefficients = BiquadCoefficients::makeSecondOrder (
        FilterKind::bandPass, currentSampleRate, 800.0, 0.55, 0.0);
    pickDetectorCoefficients = BiquadCoefficients::makeSecondOrder (
        FilterKind::highPass, currentSampleRate, 1800.0, 0.707, 0.0);
}

double ChugStabilizer::effectiveThresholdDb() const noexcept
{
    if (!automaticThreshold.load())
        return std::clamp (finiteOr (manualThresholdDb.load(), -30.0),
                           minimumThresholdDb, maximumThresholdDb);

    return std::clamp (linearToDb (autoLowBaseline) + 8.0,
                       minimumThresholdDb, -6.0);
}

void ChugStabilizer::setEnabled (bool value) noexcept
{
    enabled.store (value);
}

bool ChugStabilizer::isEnabled() const noexcept
{
    return enabled.load();
}

void ChugStabilizer::setTargetFrequency (double value) noexcept
{
    targetFrequencyHz.store (
        std::clamp (finiteOr (value, 120.0), minimumFrequencyHz,
                    maximumFrequencyHz));
}

double ChugStabilizer::getTargetFrequency() const noexcept
{
    return targetFrequencyHz.load();
}

void ChugStabilizer::setBandwidthOctaves (double value) noexcept
{
    bandwidthOctaves.store (
        std::clamp (finiteOr (value, 1.2), minimumBandwidthOctaves,
                    maximumBandwidthOctaves));
}

double ChugStabilizer::getBandwidthOctaves() const noexcept
{
    return bandwidthOctaves.load();
}

void ChugStabilizer::setMaximumReductionDb (double value) noexcept
{
    maximumReductionDb.store (
        std::clamp (finiteOr (value, 8.0), 0.0, 18.0));
}

double ChugStabilizer::getMaximumReductionDb() const noexcept
{
    return maximumReductionDb.load();
}

void ChugStabilizer::setThresholdDb (double value) noexcept
{
    manualThresholdDb.store (
        std::clamp (finiteOr (value, -30.0), minimumThresholdDb,
                    maximumThresholdDb));
}

double ChugStabilizer::getThresholdDb() const noexcept
{
    return manualThresholdDb.load();
}

void ChugStabilizer::setAutomaticThreshold (bool value) noexcept
{
    automaticThreshold.store (value);
}

bool ChugStabilizer::isAutomaticThreshold() const noexcept
{
    return automaticThreshold.load();
}

void ChugStabilizer::setAttackMs (double value) noexcept
{
    attackTimeMs.store (std::clamp (finiteOr (value, 3.0), 0.2, 20.0));
}

void ChugStabilizer::setHoldMs (double value) noexcept
{
    holdTimeMs.store (std::clamp (finiteOr (value, 25.0), 0.0, 80.0));
}

void ChugStabilizer::setReleaseMs (double value) noexcept
{
    releaseTimeMs.store (
        std::clamp (finiteOr (value, 100.0), 15.0, 350.0));
}

double ChugStabilizer::getAttackMs() const noexcept
{
    return attackTimeMs.load();
}

double ChugStabilizer::getHoldMs() const noexcept
{
    return holdTimeMs.load();
}

double ChugStabilizer::getReleaseMs() const noexcept
{
    return releaseTimeMs.load();
}

void ChugStabilizer::setPreservePick (bool value) noexcept
{
    preservePick.store (value);
}

bool ChugStabilizer::isPreservePickEnabled() const noexcept
{
    return preservePick.load();
}

void ChugStabilizer::setTransientSensitivity (double value) noexcept
{
    transientSensitivity.store (
        std::clamp (finiteOr (value, 0.5), 0.0, 1.0));
}

void ChugStabilizer::setDominanceSensitivity (double value) noexcept
{
    dominanceSensitivity.store (
        std::clamp (finiteOr (value, 0.5), 0.0, 1.0));
}

void ChugStabilizer::setStaticSupportGainDb (double value) noexcept
{
    staticSupportGainDb.store (
        std::clamp (finiteOr (value, 0.0), -6.0, 6.0));
}

double ChugStabilizer::getStaticSupportGainDb() const noexcept
{
    return staticSupportGainDb.load();
}

void ChugStabilizer::setFilterType (FilterType value) noexcept
{
    filterType.store (value);
}

ChugStabilizer::FilterType ChugStabilizer::getFilterType() const noexcept
{
    return filterType.load();
}

void ChugStabilizer::setListenMode (ListenMode value) noexcept
{
    listenMode.store (value);
}

ChugStabilizer::ListenMode ChugStabilizer::getListenMode() const noexcept
{
    return listenMode.load();
}

void ChugStabilizer::setListenRemoved (bool value) noexcept
{
    listenMode.store (value ? ListenMode::removedContribution
                            : ListenMode::normal);
}

bool ChugStabilizer::getDetectedEvent() const noexcept
{
    return visualEvent.load();
}

float ChugStabilizer::getLowBandLevelDb() const noexcept
{
    return visualLowDb.load();
}

float ChugStabilizer::getLastGainReductionDb() const noexcept
{
    return visualReductionDb.load();
}

ChugStabilizer::TriggerSource
ChugStabilizer::getTriggerSource() const noexcept
{
    return visualTrigger.load();
}

ChugVisualizationData ChugStabilizer::getVisualizationData() const noexcept
{
    ChugVisualizationData data;
    data.detectedEvent = visualEvent.load();
    data.lowBandLevelDb = visualLowDb.load();
    data.midBandLevelDb = visualMidDb.load();
    data.lastGainReductionDb = visualReductionDb.load();
    data.currentFrequencyHz =
        static_cast<float> (targetFrequencyHz.load());
    data.currentBandwidthOctaves =
        static_cast<float> (bandwidthOctaves.load());
    data.currentCurveGainDb = visualCurveGainDb.load();
    data.eventDurationMs = visualEventDurationMs.load();
    data.triggerSource = visualTrigger.load();
    return data;
}

} // namespace ShredEQ::DSP
