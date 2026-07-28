#include "MultibandZone.h"
#include <algorithm>
#include <cmath>

namespace ShredEQ::DSP
{

void MultibandZone::prepare (double sampleRate, int maxBlockSize) noexcept
{
    juce::ignoreUnused (maxBlockSize);
    currentSampleRate = sampleRate;

    smoothedGain.reset (sampleRate, 0.02);
    smoothedThreshold.reset (sampleRate, 0.02);

    for (size_t ch = 0; ch < 2; ++ch)
    {
        envelopeFollowers[ch].prepare (sampleRate);
        separators[ch].prepare (sampleRate);
        gainSmoothers[ch].prepare (sampleRate);
    }
    zoneEnergyFollower.prepare (sampleRate);

    updateTimeConstants();
    reset();
}

void MultibandZone::reset() noexcept
{
    smoothedGain.setCurrentAndTargetValue (targetGainDb.load (std::memory_order_relaxed));
    smoothedThreshold.setCurrentAndTargetValue (targetThresholdDb.load (std::memory_order_relaxed));

    for (size_t ch = 0; ch < 2; ++ch)
    {
        envelopeFollowers[ch].reset();
        separators[ch].reset();
        gainSmoothers[ch].reset (0.0);
        autoBaseline[ch] = 0.0;
        heldGainTargetDb[ch] = 0.0;
        holdTimer[ch] = 0;
    }
    zoneEnergyFollower.reset();
    lastGainReductionDb.store (0.0f, std::memory_order_relaxed);
}

void MultibandZone::process (float* const* channels, int numChannels, int numSamples,
                             const float* const* sidechainChannels, int numSidechainChannels,
                             bool isMidSideDomain) noexcept
{
    if (channels == nullptr || numSamples <= 0 || numChannels <= 0)
        return;

    const auto sampleRate = currentSampleRate;
    const auto safeSampleRate = sampleRate > 0.0 ? sampleRate : 44100.0;
    const auto baselineAlpha = std::exp (-1.0 / (safeSampleRate * 2.0)); // 2-second time constant

    const auto currentRatio = ratio.load (std::memory_order_relaxed);
    const auto currentRangeDb = dynamicRangeDb.load (std::memory_order_relaxed);
    const auto isUpwardMode = upward.load (std::memory_order_relaxed);
    const auto currentHoldMs = holdMs.load (std::memory_order_relaxed);
    const auto te = transientEmphasis.load (std::memory_order_relaxed);
    const auto se = sustainEmphasis.load (std::memory_order_relaxed);
    const auto currentPlacement = placement.load (std::memory_order_relaxed);
    const auto currentDetectorSource = detectorSource.load (std::memory_order_relaxed);

    const float transientScalar = 1.0f + static_cast<float> (te) / 100.0f;
    const float sustainScalar = 1.0f + static_cast<float> (se) / 100.0f;

    for (int i = 0; i < numSamples; ++i)
    {
        const double currentGainDb = smoothedGain.getNextValue();
        const double currentThresholdDb = smoothedThreshold.getNextValue();

        if (mute.load (std::memory_order_relaxed))
        {
            for (int ch = 0; ch < numChannels; ++ch)
            {
                if (channels[ch] != nullptr)
                    channels[ch][i] = 0.0f;
            }
            continue;
        }

        // 1. Extract input samples
        const float inputL = (channels[0] != nullptr) ? channels[0][i] : 0.0f;
        const float inputR = (numChannels > 1 && channels[1] != nullptr) ? channels[1][i] : 0.0f;

        // 2. Decode/Encode to L/R and Mid/Side
        float l = 0.0f;
        float r = 0.0f;
        float mid = 0.0f;
        float side = 0.0f;

        if (isMidSideDomain)
        {
            mid = inputL;
            side = inputR;
            l = (mid + side) / 1.41421356237f;
            r = (mid - side) / 1.41421356237f;
        }
        else
        {
            l = inputL;
            r = inputR;
            mid = (l + r) / 1.41421356237f;
            side = (l - r) / 1.41421356237f;
        }

        // 3. Extract sidechain
        float extSC = 0.0f;
        if (sidechainChannels != nullptr && numSidechainChannels > 0)
        {
            if (numSidechainChannels == 1 && sidechainChannels[0] != nullptr)
                extSC = sidechainChannels[0][i];
            else if (numSidechainChannels > 1 && sidechainChannels[0] != nullptr && sidechainChannels[1] != nullptr)
                extSC = 0.5f * (sidechainChannels[0][i] + sidechainChannels[1][i]);
        }

        if (bypass.load (std::memory_order_relaxed))
        {
            // Apply static gain only
            const float staticGainLinear = static_cast<float> (std::pow (10.0, currentGainDb / 20.0));
            if (numChannels > 0 && channels[0] != nullptr) channels[0][i] = inputL * staticGainLinear;
            if (numChannels > 1 && channels[1] != nullptr) channels[1][i] = inputR * staticGainLinear;

            // Update followers so they remain warm
            float detSample = l;
            if (currentPlacement == Placement::stereoLinked)
                detSample = std::max (std::abs (l), std::abs (r));
            else if (currentPlacement == Placement::right)
                detSample = r;
            else if (currentPlacement == Placement::mid)
                detSample = mid;
            else if (currentPlacement == Placement::side)
                detSample = side;

            if (currentDetectorSource == DetectorSource::left) detSample = l;
            else if (currentDetectorSource == DetectorSource::right) detSample = r;
            else if (currentDetectorSource == DetectorSource::mid) detSample = mid;
            else if (currentDetectorSource == DetectorSource::side) detSample = side;
            else if (currentDetectorSource == DetectorSource::externalSidechain) detSample = extSC;

            (void) envelopeFollowers[0].processSample (detSample);
            (void) separators[0].processSample (detSample);
            if (numChannels > 1)
            {
                (void) envelopeFollowers[1].processSample (detSample);
                (void) separators[1].processSample (detSample);
            }
            continue;
        }

        // 4. Calculate dynamics gain(s)
        double dynGainDbL = 0.0;
        double dynGainDbR = 0.0;

        if (currentPlacement == Placement::stereoUnlinked && numChannels > 1)
        {
            // Unlinked dynamics
            for (size_t ch = 0; ch < 2; ++ch)
            {
                float detSample = (ch == 0) ? l : r;
                if (currentDetectorSource == DetectorSource::left) detSample = l;
                else if (currentDetectorSource == DetectorSource::right) detSample = r;
                else if (currentDetectorSource == DetectorSource::mid) detSample = mid;
                else if (currentDetectorSource == DetectorSource::side) detSample = side;
                else if (currentDetectorSource == DetectorSource::externalSidechain) detSample = extSC;

                // Update baseline level
                autoBaseline[ch] = baselineAlpha * autoBaseline[ch] + (1.0 - baselineAlpha) * std::abs (detSample);
                const double baselineDb = 20.0 * std::log10 (autoBaseline[ch] + 1.0e-12);

                float detectorLevelLinear = 0.0f;
                if (std::abs (te) > 1.0e-5 || std::abs (se) > 1.0e-5)
                {
                    auto levels = separators[ch].processSample (detSample);
                    detectorLevelLinear = std::max (0.0f, levels.transient * transientScalar + levels.sustain * sustainScalar);
                }
                else
                {
                    detectorLevelLinear = envelopeFollowers[ch].processSample (detSample);
                }

                const double levelDb = 20.0 * std::log10 (static_cast<double> (detectorLevelLinear) + 1.0e-12);
                const double threshold = getEffectiveThresholdDb (currentThresholdDb, baselineDb);
                const double g_target = computeSoftKneeGainDb (
                    levelDb, threshold, currentRatio, 3.0, currentRangeDb, isUpwardMode);

                // Apply hold time in sample domain
                if (std::abs (g_target) > std::abs (heldGainTargetDb[ch]))
                {
                    heldGainTargetDb[ch] = g_target;
                    holdTimer[ch] = static_cast<int> (currentHoldMs * 0.001 * safeSampleRate);
                }
                else
                {
                    if (holdTimer[ch] > 0)
                        --holdTimer[ch];
                    else
                        heldGainTargetDb[ch] = g_target;
                }

                const double dynGain = gainSmoothers[ch].processSample (heldGainTargetDb[ch]);
                if (ch == 0) dynGainDbL = dynGain;
                else dynGainDbR = dynGain;
            }
        }
        else
        {
            // Linked or single channel dynamics
            float detSample = l;
            if (numChannels > 1)
            {
                if (currentPlacement == Placement::stereoLinked)
                    detSample = std::max (std::abs (l), std::abs (r));
                else if (currentPlacement == Placement::left)
                    detSample = l;
                else if (currentPlacement == Placement::right)
                    detSample = r;
                else if (currentPlacement == Placement::mid)
                    detSample = mid;
                else if (currentPlacement == Placement::side)
                    detSample = side;
            }

            if (currentDetectorSource == DetectorSource::left) detSample = l;
            else if (currentDetectorSource == DetectorSource::right) detSample = r;
            else if (currentDetectorSource == DetectorSource::mid) detSample = mid;
            else if (currentDetectorSource == DetectorSource::side) detSample = side;
            else if (currentDetectorSource == DetectorSource::externalSidechain) detSample = extSC;

            // Update baseline level
            autoBaseline[0] = baselineAlpha * autoBaseline[0] + (1.0 - baselineAlpha) * std::abs (detSample);
            const double baselineDb = 20.0 * std::log10 (autoBaseline[0] + 1.0e-12);

            float detectorLevelLinear = 0.0f;
            if (std::abs (te) > 1.0e-5 || std::abs (se) > 1.0e-5)
            {
                auto levels = separators[0].processSample (detSample);
                detectorLevelLinear = std::max (0.0f, levels.transient * transientScalar + levels.sustain * sustainScalar);
            }
            else
            {
                detectorLevelLinear = envelopeFollowers[0].processSample (detSample);
            }

            const double levelDb = 20.0 * std::log10 (static_cast<double> (detectorLevelLinear) + 1.0e-12);
            const double threshold = getEffectiveThresholdDb (currentThresholdDb, baselineDb);
            const double g_target = computeSoftKneeGainDb (
                levelDb, threshold, currentRatio, 3.0, currentRangeDb, isUpwardMode);

            // Apply hold time
            if (std::abs (g_target) > std::abs (heldGainTargetDb[0]))
            {
                heldGainTargetDb[0] = g_target;
                holdTimer[0] = static_cast<int> (currentHoldMs * 0.001 * safeSampleRate);
            }
            else
            {
                if (holdTimer[0] > 0)
                    --holdTimer[0];
                else
                    heldGainTargetDb[0] = g_target;
            }

            const double dynGain = gainSmoothers[0].processSample (heldGainTargetDb[0]);
            dynGainDbL = dynGain;
            dynGainDbR = dynGain;
        }

        // Store last gain reduction for getters/GUI
        lastGainReductionDb.store (static_cast<float> (std::abs (dynGainDbL)), std::memory_order_relaxed);

        // 5. Track zone energy
        float zoneEnergySample = 0.0f;
        if (numChannels > 0)
        {
            if (numChannels == 1)
                zoneEnergySample = std::abs (channels[0][i]);
            else
                zoneEnergySample = std::max (std::abs (channels[0][i]), std::abs (channels[1][i]));
        }
        (void) zoneEnergyFollower.processSample (zoneEnergySample);

        // 6. Apply gain based on placement/routing
        double totalGainL_dB = currentGainDb;
        double totalGainR_dB = currentGainDb;

        // Apply dynamics to appropriate channel
        if (currentPlacement == Placement::stereoLinked || currentPlacement == Placement::stereoUnlinked)
        {
            totalGainL_dB += dynGainDbL;
            totalGainR_dB += dynGainDbR;
        }
        else if (currentPlacement == Placement::left)
        {
            totalGainL_dB += dynGainDbL;
        }
        else if (currentPlacement == Placement::right)
        {
            totalGainR_dB += dynGainDbR;
        }
        // For Mid and Side, they are processed in encoded domain if not in Mid/Side domain

        if (numChannels == 1)
        {
            // Mono
            const double totalGainDb = currentGainDb + dynGainDbL;
            if (channels[0] != nullptr)
                channels[0][i] = inputL * static_cast<float> (std::pow (10.0, totalGainDb / 20.0));
        }
        else
        {
            // Stereo
            if (isMidSideDomain)
            {
                if (currentPlacement == Placement::mid)
                    totalGainL_dB += dynGainDbL; // mid is channel 0
                else if (currentPlacement == Placement::side)
                    totalGainR_dB += dynGainDbR; // side is channel 1

                if (channels[0] != nullptr)
                    channels[0][i] = mid * static_cast<float> (std::pow (10.0, totalGainL_dB / 20.0));
                if (channels[1] != nullptr)
                    channels[1][i] = side * static_cast<float> (std::pow (10.0, totalGainR_dB / 20.0));
            }
            else
            {
                // Left and Right domain
                if (currentPlacement == Placement::mid)
                {
                    const double midGainDb = currentGainDb + dynGainDbL;
                    const double sideGainDb = currentGainDb;
                    const float mid_out = mid * static_cast<float> (std::pow (10.0, midGainDb / 20.0));
                    const float side_out = side * static_cast<float> (std::pow (10.0, sideGainDb / 20.0));
                    if (channels[0] != nullptr) channels[0][i] = (mid_out + side_out) / 1.41421356237f;
                    if (channels[1] != nullptr) channels[1][i] = (mid_out - side_out) / 1.41421356237f;
                }
                else if (currentPlacement == Placement::side)
                {
                    const double midGainDb = currentGainDb;
                    const double sideGainDb = currentGainDb + dynGainDbR;
                    const float mid_out = mid * static_cast<float> (std::pow (10.0, midGainDb / 20.0));
                    const float side_out = side * static_cast<float> (std::pow (10.0, sideGainDb / 20.0));
                    if (channels[0] != nullptr) channels[0][i] = (mid_out + side_out) / 1.41421356237f;
                    if (channels[1] != nullptr) channels[1][i] = (mid_out - side_out) / 1.41421356237f;
                }
                else
                {
                    if (channels[0] != nullptr)
                        channels[0][i] = l * static_cast<float> (std::pow (10.0, totalGainL_dB / 20.0));
                    if (channels[1] != nullptr)
                        channels[1][i] = r * static_cast<float> (std::pow (10.0, totalGainR_dB / 20.0));
                }
            }
        }
    }
}

void MultibandZone::process (float* samples, int numSamples) noexcept
{
    float* channels[1] = { samples };
    process (channels, 1, numSamples, nullptr, 0, false);
}

// Setters (with clamping to sensible guitar/bass ranges)
void MultibandZone::setGainDb (double newGainDb) noexcept
{
    targetGainDb.store (std::clamp (newGainDb, -24.0, 24.0), std::memory_order_relaxed);
    smoothedGain.setTargetValue (targetGainDb.load (std::memory_order_relaxed));
}

void MultibandZone::setThresholdDb (double newThresholdDb) noexcept
{
    targetThresholdDb.store (std::clamp (newThresholdDb, -90.0, 0.0), std::memory_order_relaxed);
    smoothedThreshold.setTargetValue (targetThresholdDb.load (std::memory_order_relaxed));
}

void MultibandZone::setRatio (double newRatio) noexcept
{
    ratio.store (std::clamp (newRatio, 1.0, 10.0), std::memory_order_relaxed);
}

void MultibandZone::setAttackMs (double newAttackMs) noexcept
{
    attackMs.store (std::clamp (newAttackMs, 0.1, 300.0), std::memory_order_relaxed);
    updateTimeConstants();
}

void MultibandZone::setHoldMs (double newHoldMs) noexcept
{
    holdMs.store (std::clamp (newHoldMs, 0.0, 500.0), std::memory_order_relaxed);
}

void MultibandZone::setReleaseMs (double newReleaseMs) noexcept
{
    releaseMs.store (std::clamp (newReleaseMs, 5.0, 3000.0), std::memory_order_relaxed);
    updateTimeConstants();
}

void MultibandZone::setDynamicRangeDb (double newRangeDb) noexcept
{
    dynamicRangeDb.store (std::clamp (newRangeDb, 0.0, 24.0), std::memory_order_relaxed);
}

void MultibandZone::setUpward (bool shouldBeUpward) noexcept
{
    upward.store (shouldBeUpward, std::memory_order_relaxed);
}

void MultibandZone::setTransientEmphasis (double newTransientEmphasis) noexcept
{
    transientEmphasis.store (std::clamp (newTransientEmphasis, -100.0, 100.0), std::memory_order_relaxed);
}

void MultibandZone::setSustainEmphasis (double newSustainEmphasis) noexcept
{
    sustainEmphasis.store (std::clamp (newSustainEmphasis, -100.0, 100.0), std::memory_order_relaxed);
}

void MultibandZone::setSolo (bool shouldSolo) noexcept
{
    solo.store (shouldSolo, std::memory_order_relaxed);
}

void MultibandZone::setMute (bool shouldMute) noexcept
{
    mute.store (shouldMute, std::memory_order_relaxed);
}

void MultibandZone::setBypass (bool shouldBypass) noexcept
{
    bypass.store (shouldBypass, std::memory_order_relaxed);
}

void MultibandZone::setDetectorSource (DetectorSource newSource) noexcept
{
    detectorSource.store (newSource, std::memory_order_relaxed);
}

void MultibandZone::setPlacement (Placement newPlacement) noexcept
{
    placement.store (newPlacement, std::memory_order_relaxed);
}

void MultibandZone::setAutoThreshold (bool shouldAutoThreshold) noexcept
{
    autoThreshold.store (shouldAutoThreshold, std::memory_order_relaxed);
}

float MultibandZone::getZoneEnergy() const noexcept
{
    return zoneEnergyFollower.levelLinear();
}

float MultibandZone::getZoneEnergyDb() const noexcept
{
    return zoneEnergyFollower.levelDb();
}

void MultibandZone::updateTimeConstants() noexcept
{
    const double att = attackMs.load (std::memory_order_relaxed);
    const double rel = releaseMs.load (std::memory_order_relaxed);

    for (size_t ch = 0; ch < 2; ++ch)
    {
        envelopeFollowers[ch].setTimeConstants (att, rel);
        gainSmoothers[ch].setTimeConstants (att, rel);

        separators[ch].setFastTimeConstants (att, rel);
        separators[ch].setSlowTimeConstants (att * 5.0, rel * 5.0);
    }
    zoneEnergyFollower.setTimeConstants (10.0, 150.0);
}

double MultibandZone::getEffectiveThresholdDb (double manualThresholdDb, double baselineDb) const noexcept
{
    if (! autoThreshold.load (std::memory_order_relaxed))
        return manualThresholdDb;

    // In auto mode the manual control remains a user offset around its
    // documented -24 dB default instead of becoming an inert parameter.
    return std::clamp (
        baselineDb + 8.0 + (manualThresholdDb + 24.0), -90.0, 0.0);
}

} // namespace ShredEQ::DSP
