#include "ParametricBand.h"
#include <algorithm>
#include <cmath>

namespace ShredEQ::DSP
{

ParametricBand::ParametricBand() noexcept
{
    smoothedFrequency.setCurrentAndTargetValue (targetFrequency);
    smoothedGain.setCurrentAndTargetValue (targetGainDb);
    smoothedQ.setCurrentAndTargetValue (targetQ);
}

void ParametricBand::prepare (double sampleRate, int maxBlockSize) noexcept
{
    currentSampleRate = sampleRate > 0.0 ? sampleRate : 44100.0;

    smoothedFrequency.reset (currentSampleRate, 0.02);
    smoothedGain.reset (currentSampleRate, 0.02);
    smoothedQ.reset (currentSampleRate, 0.02);

    tptFilter.prepare (currentSampleRate);

    reset();
}

void ParametricBand::reset() noexcept
{
    biquadStates = {};
    tptFilter.reset();

    smoothedFrequency.setCurrentAndTargetValue (targetFrequency);
    smoothedGain.setCurrentAndTargetValue (targetGainDb);
    smoothedQ.setCurrentAndTargetValue (targetQ);

    lastType = filterType.load();
    lastSlope = slope.load();
    lastUseTpt = shouldUseTpt();

    updateActiveCoefficients (targetFrequency, targetGainDb, targetQ);
}

void ParametricBand::process (juce::AudioBuffer<float>& buffer, bool isMidSide) noexcept
{
    process (buffer.getArrayOfWritePointers(), buffer.getNumChannels(), buffer.getNumSamples(), isMidSide);
}

void ParametricBand::process (float* const* channels, int numChannels, int numSamples, bool isMidSide) noexcept
{
    if (!enabled.load())
        return;

    const auto limitChannels = std::min (numChannels, 2);
    if (limitChannels <= 0 || numSamples <= 0)
        return;

    const bool filterL = shouldFilterChannel (0, isMidSide);
    const bool filterR = limitChannels > 1 && shouldFilterChannel (1, isMidSide);

    if (!filterL && !filterR)
    {
        // Smoothers must advance even when channel routing bypasses filtering
        const bool smoothing = smoothedFrequency.isSmoothing() || smoothedGain.isSmoothing() || smoothedQ.isSmoothing();
        if (smoothing)
        {
            for (int s = 0; s < numSamples; ++s)
            {
                smoothedFrequency.getNextValue();
                smoothedGain.getNextValue();
                smoothedQ.getNextValue();
            }
            updateActiveCoefficients (smoothedFrequency.getCurrentValue(),
                                      smoothedGain.getCurrentValue(),
                                      smoothedQ.getCurrentValue());
        }
        return;
    }

    const auto type = filterType.load();
    const auto sl = slope.load();
    const auto tpt = shouldUseTpt();
    const bool paramsChanged = (type != lastType || sl != lastSlope || tpt != lastUseTpt);

    if (paramsChanged)
    {
        lastType = type;
        lastSlope = sl;
        lastUseTpt = tpt;
    }

    const bool smoothing = smoothedFrequency.isSmoothing() || smoothedGain.isSmoothing() || smoothedQ.isSmoothing();

    if (paramsChanged && !smoothing)
    {
        updateActiveCoefficients (smoothedFrequency.getCurrentValue(),
                                  smoothedGain.getCurrentValue(),
                                  smoothedQ.getCurrentValue());
    }

    for (int sampleIndex = 0; sampleIndex < numSamples; ++sampleIndex)
    {
        if (smoothing)
        {
            const double f = smoothedFrequency.getNextValue();
            const double g = smoothedGain.getNextValue();
            const double q = smoothedQ.getNextValue();
            updateActiveCoefficients (f, g, q);
        }

        if (tpt)
        {
            if (filterL)
            {
                channels[0][sampleIndex] = tptFilter.processSample (channels[0][sampleIndex], 0);
            }
            if (filterR)
            {
                channels[1][sampleIndex] = tptFilter.processSample (channels[1][sampleIndex], 1);
            }
        }
        else
        {
            const auto activeSections = currentCascade.size();
            
            if (filterL)
            {
                double sample = static_cast<double> (channels[0][sampleIndex]);
                for (std::size_t i = 0; i < activeSections; ++i)
                {
                    const auto& coeffs = currentCascade.sections[i];
                    auto& state = biquadStates[0].sections[i];

                    const double output = coeffs.b0 * sample + state.s1;
                    state.s1 = coeffs.b1 * sample - coeffs.a1 * output + state.s2;
                    state.s2 = coeffs.b2 * sample - coeffs.a2 * output;

                    if (std::abs (state.s1) < 1.0e-24) state.s1 = 0.0;
                    if (std::abs (state.s2) < 1.0e-24) state.s2 = 0.0;

                    sample = output;
                }
                channels[0][sampleIndex] = static_cast<float> (sample);
            }
            if (filterR)
            {
                double sample = static_cast<double> (channels[1][sampleIndex]);
                for (std::size_t i = 0; i < activeSections; ++i)
                {
                    const auto& coeffs = currentCascade.sections[i];
                    auto& state = biquadStates[1].sections[i];

                    const double output = coeffs.b0 * sample + state.s1;
                    state.s1 = coeffs.b1 * sample - coeffs.a1 * output + state.s2;
                    state.s2 = coeffs.b2 * sample - coeffs.a2 * output;

                    if (std::abs (state.s1) < 1.0e-24) state.s1 = 0.0;
                    if (std::abs (state.s2) < 1.0e-24) state.s2 = 0.0;

                    sample = output;
                }
                channels[1][sampleIndex] = static_cast<float> (sample);
            }
        }
    }
}

void ParametricBand::setEnabled (bool shouldBeEnabled) noexcept { enabled = shouldBeEnabled; }
bool ParametricBand::isEnabled() const noexcept { return enabled.load(); }

void ParametricBand::setStage (BandStage newStage) noexcept { stage = newStage; }
BandStage ParametricBand::getStage() const noexcept { return stage.load(); }

void ParametricBand::setFrequency (double newFreq) noexcept
{
    targetFrequency = std::clamp (newFreq, 15.0, 24000.0);
    smoothedFrequency.setTargetValue (targetFrequency);
}
double ParametricBand::getFrequency() const noexcept { return targetFrequency; }

void ParametricBand::setGainDb (double newGainDb) noexcept
{
    targetGainDb = std::clamp (newGainDb, -24.0, 24.0);
    smoothedGain.setTargetValue (targetGainDb);
}
double ParametricBand::getGainDb() const noexcept { return targetGainDb; }

void ParametricBand::setQ (double newQ) noexcept
{
    targetQ = std::clamp (newQ, 0.1, 36.0);
    smoothedQ.setTargetValue (targetQ);
}
double ParametricBand::getQ() const noexcept { return targetQ; }

void ParametricBand::setFilterType (BandFilterType newType) noexcept
{
    filterType = newType;
}
BandFilterType ParametricBand::getFilterType() const noexcept { return filterType.load(); }

void ParametricBand::setSlope (FilterSlope newSlope) noexcept
{
    slope = newSlope;
}
void ParametricBand::setSlope (int slopeDbPerOctave) noexcept
{
    switch (slopeDbPerOctave)
    {
        case 6:  slope = FilterSlope::dB6;  break;
        case 12: slope = FilterSlope::dB12; break;
        case 18: slope = FilterSlope::dB18; break;
        case 24: slope = FilterSlope::dB24; break;
        case 36: slope = FilterSlope::dB36; break;
        case 48: slope = FilterSlope::dB48; break;
        default: slope = FilterSlope::dB12; break;
    }
}
FilterSlope ParametricBand::getSlope() const noexcept { return slope.load(); }

void ParametricBand::setRoutingMode (StereoRoutingMode newMode) noexcept
{
    routingMode = newMode;
}
StereoRoutingMode ParametricBand::getRoutingMode() const noexcept { return routingMode.load(); }

void ParametricBand::setUseTpt (bool shouldTpt) noexcept
{
    useTpt = shouldTpt;
}
bool ParametricBand::getUseTpt() const noexcept { return useTpt.load(); }

void ParametricBand::setModulated (bool modulated) noexcept
{
    isModulated = modulated;
}
bool ParametricBand::getModulated() const noexcept { return isModulated.load(); }

bool ParametricBand::shouldUseTpt() const noexcept
{
    return useTpt.load() || isModulated.load();
}

bool ParametricBand::shouldFilterChannel (int channelIndex, bool isMidSideDomain) const noexcept
{
    if (channelIndex < 0 || channelIndex >= 2)
        return false;

    const auto mode = routingMode.load();

    if (mode == StereoRoutingMode::linked || mode == StereoRoutingMode::unlinked)
        return true;

    if (isMidSideDomain)
    {
        if (mode == StereoRoutingMode::mid && channelIndex == 0)
            return true;
        if (mode == StereoRoutingMode::side && channelIndex == 1)
            return true;
    }
    else
    {
        if (mode == StereoRoutingMode::left && channelIndex == 0)
            return true;
        if (mode == StereoRoutingMode::right && channelIndex == 1)
            return true;
    }

    return false;
}

void ParametricBand::updateActiveCoefficients (double f, double g, double q) noexcept
{
    if (shouldUseTpt())
    {
        tptFilter.setParameters (f, g, q, toFilterKind (filterType.load()));
    }
    else
    {
        FilterParameters params;
        params.sampleRate = currentSampleRate;
        params.frequencyHz = f;
        params.gainDb = g;
        params.q = q;

        currentCascade = makeSlopeCascade (toFilterKind (filterType.load()), slope.load(), params);
    }
}

// ParametricEQ Container Implementation

void ParametricEQ::prepare (double sampleRate, int maxBlockSize) noexcept
{
    for (auto& band : bands)
        band.prepare (sampleRate, maxBlockSize);
}

void ParametricEQ::reset() noexcept
{
    for (auto& band : bands)
        band.reset();
}

void ParametricEQ::process (juce::AudioBuffer<float>& buffer, bool isMidSide, BandStage stage) noexcept
{
    process (buffer.getArrayOfWritePointers(), buffer.getNumChannels(), buffer.getNumSamples(), isMidSide, stage);
}

void ParametricEQ::process (float* const* channels, int numChannels, int numSamples, bool isMidSide, BandStage stage) noexcept
{
    for (auto& band : bands)
    {
        if (band.isEnabled() && band.getStage() == stage)
        {
            band.process (channels, numChannels, numSamples, isMidSide);
        }
    }
}

} // namespace ShredEQ::DSP
