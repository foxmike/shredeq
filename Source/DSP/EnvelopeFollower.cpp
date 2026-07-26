#include "EnvelopeFollower.h"

#include <algorithm>
#include <cmath>

namespace ShredEQ::DSP
{
namespace
{
[[nodiscard]] double sanitizeSampleRate (double sampleRate,
                                         double fallback) noexcept
{
    return std::isfinite (sampleRate) && sampleRate > 0.0
               ? sampleRate
               : fallback;
}

[[nodiscard]] double sanitizeTimeMs (double timeMs) noexcept
{
    return std::isfinite (timeMs) ? std::max (0.0, timeMs) : 0.0;
}

[[nodiscard]] double timeToAlpha (double timeMs, double sampleRate) noexcept
{
    if (timeMs <= 0.0)
        return 0.0;

    const auto timeSeconds = timeMs * 0.001;
    return std::exp (-1.0 / (sampleRate * timeSeconds));
}

[[nodiscard]] double finiteOrZero (double value) noexcept
{
    return std::isfinite (value) ? value : 0.0;
}
} // namespace

void EnvelopeFollower::prepare (double sampleRate) noexcept
{
    currentSampleRate = sanitizeSampleRate (sampleRate, defaultSampleRate);
    updateCoefficients();
    reset();
}

void EnvelopeFollower::reset() noexcept
{
    envelope = 0.0;
}

void EnvelopeFollower::setAttackMs (double attackMs) noexcept
{
    attackTimeMs = sanitizeTimeMs (attackMs);
    updateCoefficients();
}

void EnvelopeFollower::setReleaseMs (double releaseMs) noexcept
{
    releaseTimeMs = sanitizeTimeMs (releaseMs);
    updateCoefficients();
}

void EnvelopeFollower::setTimeConstants (double attackMs,
                                         double releaseMs) noexcept
{
    attackTimeMs = sanitizeTimeMs (attackMs);
    releaseTimeMs = sanitizeTimeMs (releaseMs);
    updateCoefficients();
}

float EnvelopeFollower::processSample (float sample) noexcept
{
    const auto magnitude =
        std::isfinite (sample) ? std::abs (static_cast<double> (sample)) : 0.0;
    const auto alpha = magnitude > envelope ? attackAlpha : releaseAlpha;
    envelope = alpha * envelope + (1.0 - alpha) * magnitude;

    if (envelope < denormalThreshold)
        envelope = 0.0;

    return static_cast<float> (envelope);
}

float EnvelopeFollower::levelLinear() const noexcept
{
    return static_cast<float> (envelope);
}

float EnvelopeFollower::levelDb() const noexcept
{
    return static_cast<float> (
        20.0 * std::log10 (envelope + detectorEpsilon));
}

void EnvelopeFollower::updateCoefficients() noexcept
{
    attackAlpha = timeToAlpha (attackTimeMs, currentSampleRate);
    releaseAlpha = timeToAlpha (releaseTimeMs, currentSampleRate);
}

double computeSoftKneeCurveDb (double levelAboveThresholdDb,
                               double kneeWidthDb) noexcept
{
    const auto u = finiteOrZero (levelAboveThresholdDb);
    const auto knee = std::isfinite (kneeWidthDb)
                          ? std::max (0.0, kneeWidthDb)
                          : 0.0;

    if (knee <= 0.0)
        return std::max (0.0, u);

    const auto halfKnee = 0.5 * knee;
    if (u <= -halfKnee)
        return 0.0;
    if (u >= halfKnee)
        return u;

    const auto kneePosition = u + halfKnee;
    return kneePosition * kneePosition / (2.0 * knee);
}

double computeSoftKneeGainDb (double levelDb, double thresholdDb,
                              double ratio, double kneeWidthDb,
                              double maximumRangeDb, bool upward) noexcept
{
    const auto safeLevel = finiteOrZero (levelDb);
    const auto safeThreshold = finiteOrZero (thresholdDb);
    const auto safeRatio =
        std::isfinite (ratio) ? std::max (1.0, ratio) : 1.0;
    const auto safeRange = std::isfinite (maximumRangeDb)
                               ? std::max (0.0, maximumRangeDb)
                               : 0.0;
    const auto curve =
        computeSoftKneeCurveDb (safeLevel - safeThreshold, kneeWidthDb);
    const auto action =
        std::min (safeRange, (1.0 - 1.0 / safeRatio) * curve);

    return upward ? action : -action;
}

void GainSmootherDb::prepare (double sampleRate) noexcept
{
    currentSampleRate = sanitizeSampleRate (sampleRate, defaultSampleRate);
    updateCoefficients();
    reset();
}

void GainSmootherDb::reset (double gainDb) noexcept
{
    gain = finiteOrZero (gainDb);
}

void GainSmootherDb::setAttackMs (double attackMs) noexcept
{
    attackTimeMs = sanitizeTimeMs (attackMs);
    updateCoefficients();
}

void GainSmootherDb::setReleaseMs (double releaseMs) noexcept
{
    releaseTimeMs = sanitizeTimeMs (releaseMs);
    updateCoefficients();
}

void GainSmootherDb::setTimeConstants (double attackMs,
                                       double releaseMs) noexcept
{
    attackTimeMs = sanitizeTimeMs (attackMs);
    releaseTimeMs = sanitizeTimeMs (releaseMs);
    updateCoefficients();
}

double GainSmootherDb::processSample (double targetGainDb) noexcept
{
    const auto target = finiteOrZero (targetGainDb);
    const auto alpha =
        std::abs (target) > std::abs (gain) ? attackAlpha : releaseAlpha;
    gain = alpha * gain + (1.0 - alpha) * target;

    if (std::abs (gain) < denormalThreshold)
        gain = 0.0;

    return gain;
}

double GainSmootherDb::currentGainDb() const noexcept
{
    return gain;
}

void GainSmootherDb::updateCoefficients() noexcept
{
    attackAlpha = timeToAlpha (attackTimeMs, currentSampleRate);
    releaseAlpha = timeToAlpha (releaseTimeMs, currentSampleRate);
}

void TransientSustainSeparator::prepare (double sampleRate) noexcept
{
    fastFollower.prepare (sampleRate);
    slowFollower.prepare (sampleRate);
    reset();
}

void TransientSustainSeparator::reset() noexcept
{
    fastFollower.reset();
    slowFollower.reset();
    transient = 0.0f;
    sustain = 0.0f;
}

void TransientSustainSeparator::setFastAttackMs (double attackMs) noexcept
{
    fastFollower.setAttackMs (attackMs);
}

void TransientSustainSeparator::setFastReleaseMs (double releaseMs) noexcept
{
    fastFollower.setReleaseMs (releaseMs);
}

void TransientSustainSeparator::setFastTimeConstants (
    double attackMs, double releaseMs) noexcept
{
    fastFollower.setTimeConstants (attackMs, releaseMs);
}

void TransientSustainSeparator::setSlowAttackMs (double attackMs) noexcept
{
    slowFollower.setAttackMs (attackMs);
}

void TransientSustainSeparator::setSlowReleaseMs (double releaseMs) noexcept
{
    slowFollower.setReleaseMs (releaseMs);
}

void TransientSustainSeparator::setSlowTimeConstants (
    double attackMs, double releaseMs) noexcept
{
    slowFollower.setTimeConstants (attackMs, releaseMs);
}

void TransientSustainSeparator::setK (double newK) noexcept
{
    k = std::isfinite (newK) ? std::max (0.0, newK) : 0.0;
}

void TransientSustainSeparator::setLambda (double newLambda) noexcept
{
    lambda = std::isfinite (newLambda) ? std::max (0.0, newLambda) : 0.0;
}

TransientSustainLevels TransientSustainSeparator::processSample (
    float sample) noexcept
{
    const auto fast = static_cast<double> (fastFollower.processSample (sample));
    const auto slow = static_cast<double> (slowFollower.processSample (sample));

    transient = static_cast<float> (std::max (0.0, fast - k * slow));
    sustain =
        static_cast<float> (std::max (0.0, slow - lambda * transient));

    return { transient, sustain };
}

float TransientSustainSeparator::fastEnvelope() const noexcept
{
    return fastFollower.levelLinear();
}

float TransientSustainSeparator::slowEnvelope() const noexcept
{
    return slowFollower.levelLinear();
}

float TransientSustainSeparator::transientLevel() const noexcept
{
    return transient;
}

float TransientSustainSeparator::sustainLevel() const noexcept
{
    return sustain;
}

} // namespace ShredEQ::DSP
