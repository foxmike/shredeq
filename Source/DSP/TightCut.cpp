#include "TightCut.h"

#include <algorithm>
#include <cmath>

namespace ShredEQ::DSP
{
namespace
{
[[nodiscard]] double finiteOr (double value, double fallback) noexcept
{
    return std::isfinite (value) ? value : fallback;
}

[[nodiscard]] FilterSlope canonicalTightCutSlope (int slope) noexcept
{
    constexpr std::array supported { 12, 18, 24, 36 };
    auto closest = supported.front();
    auto distance = std::abs (slope - closest);

    for (const auto candidate : supported)
    {
        const auto candidateDistance = std::abs (slope - candidate);
        if (candidateDistance < distance)
        {
            closest = candidate;
            distance = candidateDistance;
        }
    }

    return static_cast<FilterSlope> (closest);
}

[[nodiscard]] double flushDenormal (double value) noexcept
{
    return std::abs (value) < 1.0e-24 ? 0.0 : value;
}
} // namespace

void TightCut::LinearRamp::reset (double value) noexcept
{
    current = target = value;
    increment = 0.0;
    remaining = 0;
}

void TightCut::LinearRamp::setTarget (double value, int durationSamples) noexcept
{
    if (value == target)
        return;

    target = value;
    remaining = std::max (durationSamples, 0);
    if (remaining == 0)
    {
        reset (target);
        return;
    }

    increment = (target - current) / static_cast<double> (remaining);
}

double TightCut::LinearRamp::next() noexcept
{
    if (remaining > 0)
    {
        current += increment;
        if (--remaining == 0)
            current = target;
    }
    return current;
}

void TightCut::ParameterSmoother::reset (double value) noexcept
{
    current = target = value;
    multiplier = 1.0;
    remaining = 0;
}

void TightCut::ParameterSmoother::setTarget (
    double value, int durationSamples) noexcept
{
    if (value == target)
        return;

    target = value;
    remaining = std::max (durationSamples, 0);
    if (remaining == 0 || current <= 0.0 || target <= 0.0)
    {
        reset (target);
        return;
    }

    multiplier = std::exp (
        std::log (target / current) / static_cast<double> (remaining));
}

double TightCut::ParameterSmoother::next() noexcept
{
    if (remaining > 0)
    {
        current *= multiplier;
        if (--remaining == 0)
            current = target;
    }
    return current;
}

void TightCut::prepare (double sampleRate, int maxBlockSize) noexcept
{
    (void) maxBlockSize;
    currentSampleRate =
        std::isfinite (sampleRate) && sampleRate >= 1000.0 ? sampleRate : 44100.0;
    reset();
}

void TightCut::reset() noexcept
{
    highPassStates = {};
    punchStates = {};
    currentSlope = targetSlope.load (std::memory_order_relaxed);
    fadingFromSlope = currentSlope;
    currentHighPassBank = 0;

    const auto cutoff = targetCutoffHz.load (std::memory_order_relaxed);
    const auto resonance = targetResonance.load (std::memory_order_relaxed);
    const auto punchFrequency = getEffectivePunchFrequency();
    const auto gain = targetPunchGainDb.load (std::memory_order_relaxed);
    const auto q = targetPunchQ.load (std::memory_order_relaxed);

    cutoffSmoother.reset (cutoff);
    resonanceSmoother.reset (resonance);
    punchFrequencySmoother.reset (punchFrequency);
    punchGainSmoother.reset (gain);
    punchQSmoother.reset (q);
    slopeCrossfade.reset (1.0);
    enabledMix.reset (enabled.load (std::memory_order_relaxed) ? 1.0 : 0.0);
    listenMix.reset (
        listenRemoved.load (std::memory_order_relaxed) ? 1.0 : 0.0);

    const auto selected = routing.load (std::memory_order_relaxed)
                              == Routing::stereo;
    routeMix[0].reset (selected ? 1.0 : 0.0);
    routeMix[1].reset (selected ? 1.0 : 0.0);
    updateCoefficients (cutoff, resonance, punchFrequency, gain, q);
}

void TightCut::process (float* const* channels, int numChannels,
                        int numSamples, bool isMidSideDomain) noexcept
{
    const auto channelCount = std::clamp (numChannels, 0, 2);
    if (channels == nullptr || channelCount == 0 || numSamples <= 0)
        return;

    const auto rampLength = smoothingSamples();
    cutoffSmoother.setTarget (
        targetCutoffHz.load (std::memory_order_relaxed), rampLength);
    resonanceSmoother.setTarget (
        targetResonance.load (std::memory_order_relaxed), rampLength);
    punchFrequencySmoother.setTarget (
        getEffectivePunchFrequency(), rampLength);
    punchGainSmoother.setTarget (
        targetPunchGainDb.load (std::memory_order_relaxed), rampLength);
    punchQSmoother.setTarget (
        targetPunchQ.load (std::memory_order_relaxed), rampLength);
    enabledMix.setTarget (
        enabled.load (std::memory_order_relaxed) ? 1.0 : 0.0, rampLength);
    listenMix.setTarget (
        listenRemoved.load (std::memory_order_relaxed) ? 1.0 : 0.0, rampLength);

    const auto requestedSlope = targetSlope.load (std::memory_order_relaxed);
    if (requestedSlope != currentSlope)
        beginSlopeTransition (requestedSlope);

    for (int channel = 0; channel < channelCount; ++channel)
        routeMix[static_cast<std::size_t> (channel)].setTarget (
            routeSelectsChannel (channel, isMidSideDomain) ? 1.0 : 0.0,
            rampLength);

    for (int sample = 0; sample < numSamples; ++sample)
    {
        const auto cutoff = cutoffSmoother.next();
        const auto resonance = resonanceSmoother.next();
        const auto punchFrequency = punchFrequencySmoother.next();
        const auto gain = punchGainSmoother.next();
        const auto punchQ = punchQSmoother.next();
        updateCoefficients (
            cutoff, resonance, punchFrequency, gain, punchQ);

        const auto slopeMix = slopeCrossfade.next();
        const auto activeMix = enabledMix.next();
        const auto removedMix = listenMix.next();
        const auto oldBank = 1 - currentHighPassBank;

        for (int channel = 0; channel < channelCount; ++channel)
        {
            auto* const data = channels[channel];
            if (data == nullptr)
                continue;

            const auto dry = static_cast<double> (data[sample]);
            const auto highPassed = processCascade (
                dry, highPassCascades[static_cast<std::size_t> (
                         currentHighPassBank)],
                highPassStates[static_cast<std::size_t> (
                    currentHighPassBank)][static_cast<std::size_t> (channel)]);

            auto blendedHighPass = highPassed;
            if (slopeCrossfade.remaining > 0 || slopeMix < 1.0)
            {
                const auto oldHighPassed = processCascade (
                    dry, highPassCascades[static_cast<std::size_t> (oldBank)],
                    highPassStates[static_cast<std::size_t> (oldBank)]
                                  [static_cast<std::size_t> (channel)]);
                blendedHighPass =
                    oldHighPassed + slopeMix * (highPassed - oldHighPassed);
            }

            const auto punched = processSection (
                blendedHighPass, punchCoefficients,
                punchStates[static_cast<std::size_t> (channel)]);
            const auto removed = dry - blendedHighPass;
            const auto audition =
                punched + removedMix * (removed - punched);
            const auto routeAmount =
                routeMix[static_cast<std::size_t> (channel)].next();
            const auto wetAmount = activeMix * routeAmount;
            data[sample] =
                static_cast<float> (dry + wetAmount * (audition - dry));
        }
    }
}

double TightCut::processSection (
    double input, const BiquadCoefficients& coefficients,
    SectionState& state) noexcept
{
    const auto output = coefficients.b0 * input + state.s1;
    state.s1 = flushDenormal (
        coefficients.b1 * input - coefficients.a1 * output + state.s2);
    state.s2 = flushDenormal (
        coefficients.b2 * input - coefficients.a2 * output);
    return output;
}

double TightCut::processCascade (
    double input, const CoefficientCascade& cascade,
    CascadeState& state) noexcept
{
    auto output = input;
    for (std::size_t section = 0; section < cascade.size(); ++section)
        output = processSection (
            output, cascade.sections[section], state.sections[section]);
    return output;
}

bool TightCut::routeSelectsChannel (
    int channel, bool isMidSideDomain) const noexcept
{
    const auto mode = routing.load (std::memory_order_relaxed);
    if (mode == Routing::stereo)
        return true;
    if (! isMidSideDomain)
        return false;
    return (mode == Routing::mid && channel == 0)
        || (mode == Routing::side && channel == 1);
}

void TightCut::beginSlopeTransition (FilterSlope newSlope) noexcept
{
    fadingFromSlope = currentSlope;
    currentSlope = newSlope;
    currentHighPassBank = 1 - currentHighPassBank;
    highPassStates[static_cast<std::size_t> (currentHighPassBank)] = {};
    slopeCrossfade.reset (0.0);
    slopeCrossfade.setTarget (1.0, smoothingSamples());
}

void TightCut::updateCoefficients (
    double cutoffHz, double resonance, double punchHz,
    double punchGainDb, double punchQ) noexcept
{
    FilterParameters highPassParameters {
        currentSampleRate, cutoffHz, resonance, 0.0
    };
    highPassCascades[static_cast<std::size_t> (currentHighPassBank)] =
        makeSlopeCascade (
            FilterKind::resonantHighPass, currentSlope, highPassParameters);

    if (slopeCrossfade.remaining > 0)
        highPassCascades[static_cast<std::size_t> (1 - currentHighPassBank)] =
            makeSlopeCascade (
                FilterKind::resonantHighPass, fadingFromSlope,
                highPassParameters);

    punchCoefficients = BiquadCoefficients::makeSecondOrder (
        FilterKind::bell,
        { currentSampleRate, punchHz, punchQ, punchGainDb });
}

int TightCut::smoothingSamples() const noexcept
{
    return std::max (
        1, static_cast<int> (std::round (
               smoothingTimeSeconds * currentSampleRate)));
}

void TightCut::setCutoff (double frequencyHz) noexcept
{
    targetCutoffHz.store (
        std::clamp (finiteOr (frequencyHz, 80.0),
                    minimumCutoffHz, maximumCutoffHz),
        std::memory_order_relaxed);
}

double TightCut::getCutoff() const noexcept
{
    return targetCutoffHz.load (std::memory_order_relaxed);
}

void TightCut::setSlope (FilterSlope newSlope) noexcept
{
    setSlope (static_cast<int> (newSlope));
}

void TightCut::setSlope (int slopeDbPerOctave) noexcept
{
    targetSlope.store (
        canonicalTightCutSlope (slopeDbPerOctave), std::memory_order_relaxed);
}

FilterSlope TightCut::getSlope() const noexcept
{
    return targetSlope.load (std::memory_order_relaxed);
}

void TightCut::setResonance (double q) noexcept
{
    targetResonance.store (
        std::clamp (finiteOr (q, 0.707),
                    minimumResonance, maximumResonance),
        std::memory_order_relaxed);
}

double TightCut::getResonance() const noexcept
{
    return targetResonance.load (std::memory_order_relaxed);
}

void TightCut::setPunchLinked (bool shouldBeLinked) noexcept
{
    punchLinked.store (shouldBeLinked, std::memory_order_relaxed);
}

bool TightCut::isPunchLinked() const noexcept
{
    return punchLinked.load (std::memory_order_relaxed);
}

void TightCut::setPunchRatio (double ratio) noexcept
{
    targetPunchRatio.store (
        std::clamp (finiteOr (ratio, 1.0),
                    minimumPunchRatio, maximumPunchRatio),
        std::memory_order_relaxed);
}

double TightCut::getPunchRatio() const noexcept
{
    return targetPunchRatio.load (std::memory_order_relaxed);
}

void TightCut::setPunchFrequency (double frequencyHz) noexcept
{
    targetPunchFrequencyHz.store (
        std::clamp (finiteOr (frequencyHz, 80.0),
                    minimumPunchFrequencyHz, maximumPunchFrequencyHz),
        std::memory_order_relaxed);
}

double TightCut::getPunchFrequency() const noexcept
{
    return targetPunchFrequencyHz.load (std::memory_order_relaxed);
}

double TightCut::getEffectivePunchFrequency() const noexcept
{
    if (! punchLinked.load (std::memory_order_relaxed))
        return getPunchFrequency();

    return std::clamp (
        getCutoff() * getPunchRatio(),
        minimumPunchFrequencyHz, maximumPunchFrequencyHz);
}

void TightCut::setPunchGainDb (double gainDb) noexcept
{
    targetPunchGainDb.store (
        std::clamp (finiteOr (gainDb, 0.0),
                    minimumPunchGainDb, maximumPunchGainDb),
        std::memory_order_relaxed);
}

double TightCut::getPunchGainDb() const noexcept
{
    return targetPunchGainDb.load (std::memory_order_relaxed);
}

void TightCut::setPunchQ (double q) noexcept
{
    targetPunchQ.store (
        std::clamp (finiteOr (q, 0.707), minimumPunchQ, maximumPunchQ),
        std::memory_order_relaxed);
}

double TightCut::getPunchQ() const noexcept
{
    return targetPunchQ.load (std::memory_order_relaxed);
}

void TightCut::setPlacement (Placement newPlacement) noexcept
{
    placement.store (newPlacement, std::memory_order_relaxed);
}

TightCut::Placement TightCut::getPlacement() const noexcept
{
    return placement.load (std::memory_order_relaxed);
}

void TightCut::setRouting (Routing newRouting) noexcept
{
    routing.store (newRouting, std::memory_order_relaxed);
}

TightCut::Routing TightCut::getRouting() const noexcept
{
    return routing.load (std::memory_order_relaxed);
}

void TightCut::setEnabled (bool shouldBeEnabled) noexcept
{
    enabled.store (shouldBeEnabled, std::memory_order_relaxed);
}

bool TightCut::isEnabled() const noexcept
{
    return enabled.load (std::memory_order_relaxed);
}

void TightCut::setListenRemoved (bool shouldListen) noexcept
{
    listenRemoved.store (shouldListen, std::memory_order_relaxed);
}

bool TightCut::isListenRemoved() const noexcept
{
    return listenRemoved.load (std::memory_order_relaxed);
}

bool TightCut::isListenActive() const noexcept
{
    return isEnabled() && isListenRemoved();
}

} // namespace ShredEQ::DSP
