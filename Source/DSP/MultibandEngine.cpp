#include "MultibandEngine.h"
#include <juce_audio_basics/juce_audio_basics.h>
#include <algorithm>

namespace ShredEQ::DSP
{

void MultibandEngine::prepare (double sampleRate, int maxBlockSize) noexcept
{
    crossover.prepare (sampleRate, maxBlockSize);
    for (std::size_t i = 0; i < zones.size(); ++i)
    {
        zones[i].prepare (sampleRate, maxBlockSize);
        // Size the buffer to 8 channels (maximumChannels) and maxBlockSize.
        // Third argument keepExistingContent=false, fourth clearExtraSpace=false, fifth avoidReallocating=true
        zoneBuffers[i].setSize (8, maxBlockSize, false, false, true);
    }
}

void MultibandEngine::reset() noexcept
{
    crossover.reset();
    for (auto& zone : zones)
        zone.reset();
    for (auto& buf : zoneBuffers)
        buf.clear();
}

void MultibandEngine::process (juce::AudioBuffer<float>& buffer,
                              const juce::AudioBuffer<float>* sidechainBuffer) noexcept
{
    if (! enabled)
        return; // True bypass: no filtering, no spectral tilt

    const auto numChannels = buffer.getNumChannels();
    const auto numSamples = buffer.getNumSamples();

    if (numChannels <= 0 || numSamples <= 0)
        return;

    // Ensure we don't exceed our maximum channel capacity
    const auto processChannels = std::min (static_cast<int> (MultibandCrossover::maximumChannels), numChannels);

    // 1. Crossover Split
    // We must pass an array of pointers to the zone buffers.
    std::array<juce::AudioBuffer<float>*, 4> zonePointers {
        &zoneBuffers[0], &zoneBuffers[1], &zoneBuffers[2], &zoneBuffers[3]
    };

    if (! crossover.processSplit (buffer, zonePointers))
        return;

    // Extract sidechain pointers if available
    const float* const* sidechainChannels = nullptr;
    int numSidechainChannels = 0;
    if (sidechainBuffer != nullptr)
    {
        sidechainChannels = sidechainBuffer->getArrayOfReadPointers();
        numSidechainChannels = sidechainBuffer->getNumChannels();
    }

    // Check if any zone is soloed
    bool anySoloed = false;
    for (const auto& zone : zones)
    {
        if (zone.isSolo())
        {
            anySoloed = true;
            break;
        }
    }

    // 2. Process each zone and apply solo/mute rules
    for (std::size_t i = 0; i < zones.size(); ++i)
    {
        // Process the zone. Bypass and internal mute states are handled inside MultibandZone::process.
        zones[i].process (zoneBuffers[i].getArrayOfWritePointers(),
                          processChannels,
                          numSamples,
                          sidechainChannels,
                          numSidechainChannels);

        // Apply solo/mute logic: if muted or (any soloed and this one not soloed), clear output.
        // We still run zones[i].process before this so that detectors and baseline trackers remain warm
        // even when a zone is muted or unsoloed.
        const bool shouldMute = zones[i].isMute() || (anySoloed && ! zones[i].isSolo());
        if (shouldMute)
        {
            // Clear only the processed channels/samples to avoid clearing unallocated parts
            for (int ch = 0; ch < processChannels; ++ch)
            {
                if (auto* writePtr = zoneBuffers[i].getWritePointer (ch))
                    juce::FloatVectorOperations::clear (writePtr, numSamples);
            }
        }
    }

    // 3. Recombine via complementary sum
    std::array<const juce::AudioBuffer<float>*, 4> constZonePointers {
        &zoneBuffers[0], &zoneBuffers[1], &zoneBuffers[2], &zoneBuffers[3]
    };

    (void) MultibandCrossover::sumZones (constZonePointers, buffer);
}

void MultibandEngine::setEnabled (bool shouldBeEnabled) noexcept
{
    enabled = shouldBeEnabled;
}

bool MultibandEngine::isEnabled() const noexcept
{
    return enabled;
}

void MultibandEngine::setCrossoverHz (std::size_t index, double frequencyHz) noexcept
{
    crossover.setCrossoverHz (index, frequencyHz);
}

void MultibandEngine::setCrossoversHz (double weightBodyHz, double bodyBiteHz, double biteFizzHz) noexcept
{
    crossover.setCrossoversHz (weightBodyHz, bodyBiteHz, biteFizzHz);
}

double MultibandEngine::getTargetCrossoverHz (std::size_t index) const noexcept
{
    return crossover.getTargetCrossoverHz (index);
}

double MultibandEngine::getEffectiveCrossoverHz (std::size_t index) const noexcept
{
    return crossover.getEffectiveCrossoverHz (index);
}

// Per-zone settings
void MultibandEngine::setZoneGainDb (std::size_t zoneIndex, double gainDb) noexcept
{
    if (zoneIndex < zones.size())
        zones[zoneIndex].setGainDb (gainDb);
}

double MultibandEngine::getZoneGainDb (std::size_t zoneIndex) const noexcept
{
    return zoneIndex < zones.size() ? zones[zoneIndex].getGainDb() : 0.0;
}

void MultibandEngine::setZoneThresholdDb (std::size_t zoneIndex, double thresholdDb) noexcept
{
    if (zoneIndex < zones.size())
        zones[zoneIndex].setThresholdDb (thresholdDb);
}

double MultibandEngine::getZoneThresholdDb (std::size_t zoneIndex) const noexcept
{
    return zoneIndex < zones.size() ? zones[zoneIndex].getThresholdDb() : 0.0;
}

void MultibandEngine::setZoneRatio (std::size_t zoneIndex, double ratio) noexcept
{
    if (zoneIndex < zones.size())
        zones[zoneIndex].setRatio (ratio);
}

double MultibandEngine::getZoneRatio (std::size_t zoneIndex) const noexcept
{
    return zoneIndex < zones.size() ? zones[zoneIndex].getRatio() : 1.0;
}

void MultibandEngine::setZoneAttackMs (std::size_t zoneIndex, double attackMs) noexcept
{
    if (zoneIndex < zones.size())
        zones[zoneIndex].setAttackMs (attackMs);
}

double MultibandEngine::getZoneAttackMs (std::size_t zoneIndex) const noexcept
{
    return zoneIndex < zones.size() ? zones[zoneIndex].getAttackMs() : 0.0;
}

void MultibandEngine::setZoneReleaseMs (std::size_t zoneIndex, double releaseMs) noexcept
{
    if (zoneIndex < zones.size())
        zones[zoneIndex].setReleaseMs (releaseMs);
}

double MultibandEngine::getZoneReleaseMs (std::size_t zoneIndex) const noexcept
{
    return zoneIndex < zones.size() ? zones[zoneIndex].getReleaseMs() : 0.0;
}

void MultibandEngine::setZoneTransientEmphasis (std::size_t zoneIndex, double transientEmphasis) noexcept
{
    if (zoneIndex < zones.size())
        zones[zoneIndex].setTransientEmphasis (transientEmphasis);
}

double MultibandEngine::getZoneTransientEmphasis (std::size_t zoneIndex) const noexcept
{
    return zoneIndex < zones.size() ? zones[zoneIndex].getTransientEmphasis() : 0.0;
}

void MultibandEngine::setZoneSustainEmphasis (std::size_t zoneIndex, double sustainEmphasis) noexcept
{
    if (zoneIndex < zones.size())
        zones[zoneIndex].setSustainEmphasis (sustainEmphasis);
}

double MultibandEngine::getZoneSustainEmphasis (std::size_t zoneIndex) const noexcept
{
    return zoneIndex < zones.size() ? zones[zoneIndex].getSustainEmphasis() : 0.0;
}

void MultibandEngine::setZonePlacement (std::size_t zoneIndex, MultibandZone::Placement placement) noexcept
{
    if (zoneIndex < zones.size())
        zones[zoneIndex].setPlacement (placement);
}

MultibandZone::Placement MultibandEngine::getZonePlacement (std::size_t zoneIndex) const noexcept
{
    return zoneIndex < zones.size() ? zones[zoneIndex].getPlacement() : MultibandZone::Placement::stereoLinked;
}

void MultibandEngine::setZoneDetectorSource (std::size_t zoneIndex, DetectorSource detectorSource) noexcept
{
    if (zoneIndex < zones.size())
        zones[zoneIndex].setDetectorSource (detectorSource);
}

DetectorSource MultibandEngine::getZoneDetectorSource (std::size_t zoneIndex) const noexcept
{
    return zoneIndex < zones.size() ? zones[zoneIndex].getDetectorSource() : DetectorSource::bandLimitedInternal;
}

void MultibandEngine::setZoneAutoThreshold (std::size_t zoneIndex, bool autoThreshold) noexcept
{
    if (zoneIndex < zones.size())
        zones[zoneIndex].setAutoThreshold (autoThreshold);
}

bool MultibandEngine::isZoneAutoThreshold (std::size_t zoneIndex) const noexcept
{
    return zoneIndex < zones.size() ? zones[zoneIndex].isAutoThreshold() : false;
}

void MultibandEngine::setZoneUpward (std::size_t zoneIndex, bool upward) noexcept
{
    if (zoneIndex < zones.size())
        zones[zoneIndex].setUpward (upward);
}

bool MultibandEngine::isZoneUpward (std::size_t zoneIndex) const noexcept
{
    return zoneIndex < zones.size() ? zones[zoneIndex].isUpward() : false;
}

void MultibandEngine::setZoneSolo (std::size_t zoneIndex, bool solo) noexcept
{
    if (zoneIndex < zones.size())
        zones[zoneIndex].setSolo (solo);
}

bool MultibandEngine::isZoneSolo (std::size_t zoneIndex) const noexcept
{
    return zoneIndex < zones.size() ? zones[zoneIndex].isSolo() : false;
}

void MultibandEngine::setZoneMute (std::size_t zoneIndex, bool mute) noexcept
{
    if (zoneIndex < zones.size())
        zones[zoneIndex].setMute (mute);
}

bool MultibandEngine::isZoneMute (std::size_t zoneIndex) const noexcept
{
    return zoneIndex < zones.size() ? zones[zoneIndex].isMute() : false;
}

void MultibandEngine::setZoneBypass (std::size_t zoneIndex, bool bypass) noexcept
{
    if (zoneIndex < zones.size())
        zones[zoneIndex].setBypass (bypass);
}

bool MultibandEngine::isZoneBypass (std::size_t zoneIndex) const noexcept
{
    return zoneIndex < zones.size() ? zones[zoneIndex].isBypass() : false;
}

void MultibandEngine::setZoneDynamicRangeDb (std::size_t zoneIndex, double rangeDb) noexcept
{
    if (zoneIndex < zones.size())
        zones[zoneIndex].setDynamicRangeDb (rangeDb);
}

double MultibandEngine::getZoneDynamicRangeDb (std::size_t zoneIndex) const noexcept
{
    return zoneIndex < zones.size() ? zones[zoneIndex].getDynamicRangeDb() : 0.0;
}

void MultibandEngine::setZoneHoldMs (std::size_t zoneIndex, double holdMs) noexcept
{
    if (zoneIndex < zones.size())
        zones[zoneIndex].setHoldMs (holdMs);
}

double MultibandEngine::getZoneHoldMs (std::size_t zoneIndex) const noexcept
{
    return zoneIndex < zones.size() ? zones[zoneIndex].getHoldMs() : 0.0;
}

float MultibandEngine::getZoneGainReductionDb (std::size_t zoneIndex) const noexcept
{
    return zoneIndex < zones.size() ? zones[zoneIndex].getGainReductionDb() : 0.0f;
}

float MultibandEngine::getZoneEnergy (std::size_t zoneIndex) const noexcept
{
    return zoneIndex < zones.size() ? zones[zoneIndex].getZoneEnergy() : 0.0f;
}

float MultibandEngine::getZoneEnergyDb (std::size_t zoneIndex) const noexcept
{
    return zoneIndex < zones.size() ? zones[zoneIndex].getZoneEnergyDb() : -120.0f;
}

} // namespace ShredEQ::DSP
