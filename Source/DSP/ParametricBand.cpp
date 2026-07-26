#include "ParametricBand.h"
#include <algorithm>
#include <cmath>

namespace ShredEQ::DSP
{

ParametricBand::ParametricBand() noexcept
{
    attachedFollower = &noteFollower;

    smoothedFrequency.setCurrentAndTargetValue (targetFrequency);
    smoothedGain.setCurrentAndTargetValue (targetGainDb);
    smoothedQ.setCurrentAndTargetValue (targetQ);

    // Sync dynamic processor defaults
    dynamicProcessor.setMode (static_cast<DynamicMode> (dynamicMode.load()));
    dynamicProcessor.setRangeDb (dynamicRangeDb.load());
    dynamicProcessor.setThresholdDb (thresholdDb.load());
    dynamicProcessor.setRatio (ratio.load());
    dynamicProcessor.setAttackMs (attackMs.load());
    dynamicProcessor.setReleaseMs (releaseMs.load());
    dynamicProcessor.setKneeDb (kneeDb.load());
    dynamicProcessor.setDetectorSource (static_cast<DetectorSource> (detectorSource.load()));
    dynamicProcessor.setDetectorFrequency (detectorFrequency.load());
    dynamicProcessor.setDetectorQ (detectorQ.load());
}

void ParametricBand::prepare (double sampleRate, int maxBlockSize) noexcept
{
    juce::ignoreUnused (maxBlockSize);
    currentSampleRate = sampleRate > 0.0 ? sampleRate : 44100.0;

    smoothedFrequency.reset (currentSampleRate, 0.02);
    smoothedGain.reset (currentSampleRate, 0.02);
    smoothedQ.reset (currentSampleRate, 0.02);

    tptFilter.prepare (currentSampleRate);
    dynamicProcessor.prepare (currentSampleRate);
    noteFollower.prepare (currentSampleRate);

    reset();
}

void ParametricBand::reset() noexcept
{
    biquadStates = {};
    tptFilter.reset();
    dynamicProcessor.reset();
    noteFollower.reset();

    lastOutputL = 0.0f;
    lastOutputR = 0.0f;

    smoothedFrequency.setCurrentAndTargetValue (targetFrequency);
    smoothedGain.setCurrentAndTargetValue (targetGainDb);
    smoothedQ.setCurrentAndTargetValue (targetQ);

    lastType = filterType.load();
    lastSlope = slope.load();
    lastUseTpt = shouldUseTpt();

    updateActiveCoefficients (targetFrequency, targetGainDb, targetQ);
}

void ParametricBand::process (juce::AudioBuffer<float>& buffer, bool isMidSide, const float* const* sidechainChannels, int numSidechainChannels) noexcept
{
    process (buffer.getArrayOfWritePointers(), buffer.getNumChannels(), buffer.getNumSamples(), isMidSide, sidechainChannels, numSidechainChannels);
}

void ParametricBand::process (float* const* channels, int numChannels, int numSamples, bool isMidSide, const float* const* sidechainChannels, int numSidechainChannels) noexcept
{
    if (!enabled.load())
        return;

    applyNoteFollower();

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
    const bool isDynActive = dynamicsEnabled.load() && (dynamicMode.load() != static_cast<int> (DynamicMode::staticEq));

    if (isDynActive)
    {
        for (int sampleIndex = 0; sampleIndex < numSamples; ++sampleIndex)
        {
            const double f = smoothing ? smoothedFrequency.getNextValue() : smoothedFrequency.getCurrentValue();
            const double g = smoothing ? smoothedGain.getNextValue() : smoothedGain.getCurrentValue();
            const double q = smoothing ? smoothedQ.getNextValue() : smoothedQ.getCurrentValue();

            const float inputL = filterL ? channels[0][sampleIndex] : 0.0f;
            const float inputR = filterR ? channels[1][sampleIndex] : 0.0f;

            float inputMono = 0.0f;
            if (filterL && filterR)
                inputMono = isMidSide ? inputL : 0.5f * (inputL + inputR);
            else if (filterL)
                inputMono = inputL;
            else if (filterR)
                inputMono = inputR;

            float outputMono = 0.0f;
            if (filterL && filterR)
                outputMono = isMidSide ? lastOutputL : 0.5f * (lastOutputL + lastOutputR);
            else if (filterL)
                outputMono = lastOutputL;
            else if (filterR)
                outputMono = lastOutputR;

            float extSC = 0.0f;
            if (sidechainChannels != nullptr && numSidechainChannels > 0)
            {
                if (numSidechainChannels == 1)
                    extSC = sidechainChannels[0][sampleIndex];
                else
                    extSC = 0.5f * (sidechainChannels[0][sampleIndex] + sidechainChannels[1][sampleIndex]);
            }

            DetectorContext context;
            context.left = isMidSide ? (inputL + inputR) / 1.41421356237f : inputL;
            context.right = isMidSide ? (inputL - inputR) / 1.41421356237f : inputR;
            context.mid = isMidSide ? inputL : (inputL + inputR) / 1.41421356237f;
            context.side = isMidSide ? inputR : (inputL - inputR) / 1.41421356237f;
            context.externalSidechain = extSC;
            context.input = inputMono;
            context.output = outputMono;

            const float primarySample = inputMono;
            const float multiplier = dynamicProcessor.processSample (primarySample, context, static_cast<float> (g));

            const double modulatedGainDb = g * multiplier;
            updateActiveCoefficients (f, modulatedGainDb, q);

            if (tpt)
            {
                if (filterL)
                    channels[0][sampleIndex] = tptFilter.processSample (channels[0][sampleIndex], 0);
                if (filterR)
                    channels[1][sampleIndex] = tptFilter.processSample (channels[1][sampleIndex], 1);
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

            lastOutputL = filterL ? channels[0][sampleIndex] : 0.0f;
            lastOutputR = filterR ? channels[1][sampleIndex] : 0.0f;
        }
    }
    else
    {
        // Prior static path
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
                    channels[0][sampleIndex] = tptFilter.processSample (channels[0][sampleIndex], 0);
                if (filterR)
                    channels[1][sampleIndex] = tptFilter.processSample (channels[1][sampleIndex], 1);
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
    dynamicProcessor.setStaticGainDb (g);

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

// Dynamic EQ setters and getters

void ParametricBand::setDynamicMode (int mode) noexcept
{
    dynamicMode = mode;
    dynamicProcessor.setMode (static_cast<DynamicMode> (mode));
}

int ParametricBand::getDynamicMode() const noexcept
{
    return dynamicMode.load();
}

void ParametricBand::setDynamicRangeDb (double range) noexcept
{
    dynamicRangeDb = range;
    dynamicProcessor.setRangeDb (range);
}

double ParametricBand::getDynamicRangeDb() const noexcept
{
    return dynamicRangeDb.load();
}

void ParametricBand::setThresholdDb (double thresh) noexcept
{
    thresholdDb = thresh;
    dynamicProcessor.setThresholdDb (thresh);
}

double ParametricBand::getThresholdDb() const noexcept
{
    return thresholdDb.load();
}

void ParametricBand::setRatio (double r) noexcept
{
    ratio = r;
    dynamicProcessor.setRatio (r);
}

double ParametricBand::getRatio() const noexcept
{
    return ratio.load();
}

void ParametricBand::setAttackMs (double a) noexcept
{
    attackMs = a;
    dynamicProcessor.setAttackMs (a);
}

double ParametricBand::getAttackMs() const noexcept
{
    return attackMs.load();
}

void ParametricBand::setHoldMs (double h) noexcept
{
    holdMs = h;
}

double ParametricBand::getHoldMs() const noexcept
{
    return holdMs.load();
}

void ParametricBand::setReleaseMs (double r) noexcept
{
    releaseMs = r;
    dynamicProcessor.setReleaseMs (r);
}

double ParametricBand::getReleaseMs() const noexcept
{
    return releaseMs.load();
}

void ParametricBand::setKneeDb (double k) noexcept
{
    kneeDb = k;
    dynamicProcessor.setKneeDb (k);
}

double ParametricBand::getKneeDb() const noexcept
{
    return kneeDb.load();
}

void ParametricBand::setDetectorSource (int source) noexcept
{
    detectorSource = source;
    dynamicProcessor.setDetectorSource (static_cast<DetectorSource> (source));
}

int ParametricBand::getDetectorSource() const noexcept
{
    return detectorSource.load();
}

void ParametricBand::setDetectorFrequency (double freq) noexcept
{
    detectorFrequency = freq;
    dynamicProcessor.setDetectorFrequency (freq);
}

double ParametricBand::getDetectorFrequency() const noexcept
{
    return detectorFrequency.load();
}

void ParametricBand::setDetectorQ (double qVal) noexcept
{
    detectorQ = qVal;
    dynamicProcessor.setDetectorQ (qVal);
}

double ParametricBand::getDetectorQ() const noexcept
{
    return detectorQ.load();
}

void ParametricBand::setTrackingSource (int source) noexcept
{
    trackingSource = source;
    if (auto* follower = attachedFollower.load())
        follower->setTrackingSource (source);
}

int ParametricBand::getTrackingSource() const noexcept
{
    return trackingSource.load();
}

void ParametricBand::setTrackingHarmonic (int harmonic) noexcept
{
    trackingHarmonic = harmonic;
    if (auto* follower = attachedFollower.load())
        follower->setHarmonic (harmonic);
}

int ParametricBand::getTrackingHarmonic() const noexcept
{
    return trackingHarmonic.load();
}

void ParametricBand::attachNoteFollower (NoteFollower* follower) noexcept
{
    attachedFollower = follower != nullptr ? follower : &noteFollower;
    
    // Sync current settings to the new follower
    if (follower != nullptr)
    {
        follower->setTrackingSource (trackingSource.load());
        follower->setHarmonic (trackingHarmonic.load());
    }
}

void ParametricBand::setNoteFollowerEnabled (bool shouldBeEnabled) noexcept
{
    noteFollowerEnabled = shouldBeEnabled;
}

bool ParametricBand::isNoteFollowerEnabled() const noexcept
{
    return noteFollowerEnabled.load();
}

void ParametricBand::applyNoteFollower() noexcept
{
    if (noteFollowerEnabled.load())
    {
        if (auto* follower = attachedFollower.load())
        {
            if (follower->isEnabled() && !follower->isBypassed())
            {
                const double f_follower = follower->getSmoothedFrequencyHz();
                const float followGain = follower->getFollowGain();
                double f_target = followGain * f_follower + (1.0 - followGain) * targetFrequency;
                f_target = std::clamp (f_target, 15.0, 24000.0);
                smoothedFrequency.setTargetValue (f_target);
                return;
            }
        }
    }
    
    // Default to legacy static/manual target frequency
    smoothedFrequency.setTargetValue (targetFrequency);
}

void ParametricBand::setHarmonic (int harmonic) noexcept
{
    if (auto* follower = attachedFollower.load())
        follower->setHarmonic (harmonic);
}

void ParametricBand::setCentsOffset (double cents) noexcept
{
    if (auto* follower = attachedFollower.load())
        follower->setCentsOffset (cents);
}

void ParametricBand::setConfidenceThreshold (double threshold) noexcept
{
    if (auto* follower = attachedFollower.load())
        follower->setConfidenceThreshold (threshold);
}

void ParametricBand::setSmoothing (double smoothingMs) noexcept
{
    if (auto* follower = attachedFollower.load())
        follower->setSmoothing (smoothingMs);
}

void ParametricBand::setHoldTimeMs (double holdTimeMs) noexcept
{
    if (auto* follower = attachedFollower.load())
        follower->setHoldTimeMs (holdTimeMs);
}

void ParametricBand::setReturnTimeMs (double returnTimeMs) noexcept
{
    if (auto* follower = attachedFollower.load())
        follower->setReturnTimeMs (returnTimeMs);
}

void ParametricBand::setHomeFrequencyHz (double homeFreq) noexcept
{
    if (auto* follower = attachedFollower.load())
        follower->setHomeFrequencyHz (homeFreq);
}

void ParametricBand::setFrequencyLimits (double minFreq, double maxFreq) noexcept
{
    if (auto* follower = attachedFollower.load())
        follower->setFrequencyLimits (minFreq, maxFreq);
}

void ParametricBand::setLookaheadMs (double ms) noexcept
{
    lookaheadMs = ms;
}

double ParametricBand::getLookaheadMs() const noexcept
{
    return lookaheadMs.load();
}

void ParametricBand::setDynamicsEnabled (bool dynamicsShouldBeEnabled) noexcept
{
    dynamicsEnabled = dynamicsShouldBeEnabled;
}

bool ParametricBand::isDynamicsEnabled() const noexcept
{
    return dynamicsEnabled.load();
}

DynamicVisualizationData ParametricBand::getDynamicVisualizationData() const noexcept
{
    auto data = dynamicProcessor.getVisualizationData();
    if (!dynamicsEnabled.load() || dynamicMode.load() == static_cast<int> (DynamicMode::staticEq))
    {
        double g = smoothedGain.getCurrentValue();
        data.staticGainDb = static_cast<float> (g);
        data.currentGainDb = static_cast<float> (g);
        data.maxDynamicRangeDb = static_cast<float> (dynamicRangeDb.load());
        data.thresholdDb = static_cast<float> (thresholdDb.load());
        data.detectorLevelDb = -120.0f;
        data.reactionFlags = 0;
    }
    return data;
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

void ParametricEQ::process (juce::AudioBuffer<float>& buffer, bool isMidSide, BandStage stage, const float* const* sidechainChannels, int numSidechainChannels) noexcept
{
    process (buffer.getArrayOfWritePointers(), buffer.getNumChannels(), buffer.getNumSamples(), isMidSide, stage, sidechainChannels, numSidechainChannels);
}

void ParametricEQ::process (float* const* channels, int numChannels, int numSamples, bool isMidSide, BandStage stage, const float* const* sidechainChannels, int numSidechainChannels) noexcept
{
    for (auto& band : bands)
    {
        if (band.isEnabled() && band.getStage() == stage)
        {
            band.process (channels, numChannels, numSamples, isMidSide, sidechainChannels, numSidechainChannels);
        }
    }
}

} // namespace ShredEQ::DSP
