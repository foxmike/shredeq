#pragma once

#include "MultibandCrossover.h"
#include "MultibandZone.h"
#include <array>
#include <cstddef>

namespace ShredEQ::DSP
{

/**
    MultibandEngine orchestrates crossover splitting, per-zone dynamics processing,
    and complementary summation recombination.
*/
class MultibandEngine final
{
public:
    MultibandEngine() noexcept = default;
    ~MultibandEngine() noexcept = default;

    void prepare (double sampleRate, int maxBlockSize) noexcept;
    void reset() noexcept;

    /**
        Processes the audio block in-place.
        If the engine is disabled, it behaves as a true bypass.
    */
    void process (juce::AudioBuffer<float>& buffer,
                  const juce::AudioBuffer<float>* sidechainBuffer = nullptr) noexcept;

    // Engine controls
    void setEnabled (bool shouldBeEnabled) noexcept;
    [[nodiscard]] bool isEnabled() const noexcept;

    // Crossover frequency controls
    void setCrossoverHz (std::size_t index, double frequencyHz) noexcept;
    void setCrossoversHz (double weightBodyHz, double bodyBiteHz, double biteFizzHz) noexcept;

    [[nodiscard]] double getTargetCrossoverHz (std::size_t index) const noexcept;
    [[nodiscard]] double getEffectiveCrossoverHz (std::size_t index) const noexcept;

    // Per-zone settings getters & setters (index: 0=Weight, 1=Body, 2=Bite, 3=Fizz/Air)
    void setZoneGainDb (std::size_t zoneIndex, double gainDb) noexcept;
    [[nodiscard]] double getZoneGainDb (std::size_t zoneIndex) const noexcept;

    void setZoneThresholdDb (std::size_t zoneIndex, double thresholdDb) noexcept;
    [[nodiscard]] double getZoneThresholdDb (std::size_t zoneIndex) const noexcept;

    void setZoneRatio (std::size_t zoneIndex, double ratio) noexcept;
    [[nodiscard]] double getZoneRatio (std::size_t zoneIndex) const noexcept;

    void setZoneAttackMs (std::size_t zoneIndex, double attackMs) noexcept;
    [[nodiscard]] double getZoneAttackMs (std::size_t zoneIndex) const noexcept;

    void setZoneReleaseMs (std::size_t zoneIndex, double releaseMs) noexcept;
    [[nodiscard]] double getZoneReleaseMs (std::size_t zoneIndex) const noexcept;

    void setZoneTransientEmphasis (std::size_t zoneIndex, double transientEmphasis) noexcept;
    [[nodiscard]] double getZoneTransientEmphasis (std::size_t zoneIndex) const noexcept;

    void setZoneSustainEmphasis (std::size_t zoneIndex, double sustainEmphasis) noexcept;
    [[nodiscard]] double getZoneSustainEmphasis (std::size_t zoneIndex) const noexcept;

    void setZonePlacement (std::size_t zoneIndex, MultibandZone::Placement placement) noexcept;
    [[nodiscard]] MultibandZone::Placement getZonePlacement (std::size_t zoneIndex) const noexcept;

    void setZoneDetectorSource (std::size_t zoneIndex, DetectorSource detectorSource) noexcept;
    [[nodiscard]] DetectorSource getZoneDetectorSource (std::size_t zoneIndex) const noexcept;

    void setZoneAutoThreshold (std::size_t zoneIndex, bool autoThreshold) noexcept;
    [[nodiscard]] bool isZoneAutoThreshold (std::size_t zoneIndex) const noexcept;

    void setZoneUpward (std::size_t zoneIndex, bool upward) noexcept;
    [[nodiscard]] bool isZoneUpward (std::size_t zoneIndex) const noexcept;

    void setZoneSolo (std::size_t zoneIndex, bool solo) noexcept;
    [[nodiscard]] bool isZoneSolo (std::size_t zoneIndex) const noexcept;

    void setZoneMute (std::size_t zoneIndex, bool mute) noexcept;
    [[nodiscard]] bool isZoneMute (std::size_t zoneIndex) const noexcept;

    void setZoneBypass (std::size_t zoneIndex, bool bypass) noexcept;
    [[nodiscard]] bool isZoneBypass (std::size_t zoneIndex) const noexcept;

    void setZoneDynamicRangeDb (std::size_t zoneIndex, double rangeDb) noexcept;
    [[nodiscard]] double getZoneDynamicRangeDb (std::size_t zoneIndex) const noexcept;

    void setZoneHoldMs (std::size_t zoneIndex, double holdMs) noexcept;
    [[nodiscard]] double getZoneHoldMs (std::size_t zoneIndex) const noexcept;

    // Visualisation/GUI getters
    [[nodiscard]] float getZoneGainReductionDb (std::size_t zoneIndex) const noexcept;
    [[nodiscard]] float getZoneEnergy (std::size_t zoneIndex) const noexcept;
    [[nodiscard]] float getZoneEnergyDb (std::size_t zoneIndex) const noexcept;

private:
    bool enabled { false };

    MultibandCrossover crossover;
    std::array<MultibandZone, 4> zones;
    std::array<juce::AudioBuffer<float>, 4> zoneBuffers;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MultibandEngine)
};

} // namespace ShredEQ::DSP
