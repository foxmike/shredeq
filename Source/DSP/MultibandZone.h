#pragma once

#include "EnvelopeFollower.h"
#include "DynamicProcessor.h"
#include <juce_audio_basics/juce_audio_basics.h>
#include <atomic>
#include <array>

namespace ShredEQ::DSP
{

/**
    Per-zone multiband dynamics processor and volume controls.
    Represents one of Weight, Body, Bite, or Fizz/Air.
*/
class MultibandZone final
{
public:
    enum class Placement
    {
        stereoLinked = 0,
        stereoUnlinked,
        left,
        right,
        mid,
        side
    };

    enum class DynamicsMode
    {
        downward = 0,
        upward
    };

    MultibandZone() noexcept = default;
    ~MultibandZone() noexcept = default;

    void prepare (double sampleRate, int maxBlockSize) noexcept;
    void reset() noexcept;

    /**
        Processes mono or stereo audio channels.
    */
    void process (float* const* channels, int numChannels, int numSamples,
                  const float* const* sidechainChannels = nullptr, int numSidechainChannels = 0,
                  bool isMidSideDomain = false) noexcept;

    /**
        Mono convenience wrapper for process.
    */
    void process (float* samples, int numSamples) noexcept;

    // Setters (with clamping to sensible ranges)
    void setGainDb (double newGainDb) noexcept;
    void setThresholdDb (double newThresholdDb) noexcept;
    void setRatio (double newRatio) noexcept;
    void setAttackMs (double newAttackMs) noexcept;
    void setHoldMs (double newHoldMs) noexcept;
    void setReleaseMs (double newReleaseMs) noexcept;
    void setDynamicRangeDb (double newRangeDb) noexcept;
    void setUpward (bool shouldBeUpward) noexcept;
    void setTransientEmphasis (double newTransientEmphasis) noexcept;
    void setSustainEmphasis (double newSustainEmphasis) noexcept;
    void setSolo (bool shouldSolo) noexcept;
    void setMute (bool shouldMute) noexcept;
    void setBypass (bool shouldBypass) noexcept;
    void setDetectorSource (DetectorSource newSource) noexcept;
    void setPlacement (Placement newPlacement) noexcept;
    void setAutoThreshold (bool shouldAutoThreshold) noexcept;

    // Getters
    [[nodiscard]] double getGainDb() const noexcept { return targetGainDb.load (std::memory_order_relaxed); }
    [[nodiscard]] double getThresholdDb() const noexcept { return targetThresholdDb.load (std::memory_order_relaxed); }
    [[nodiscard]] double getRatio() const noexcept { return ratio.load (std::memory_order_relaxed); }
    [[nodiscard]] double getAttackMs() const noexcept { return attackMs.load (std::memory_order_relaxed); }
    [[nodiscard]] double getHoldMs() const noexcept { return holdMs.load (std::memory_order_relaxed); }
    [[nodiscard]] double getReleaseMs() const noexcept { return releaseMs.load (std::memory_order_relaxed); }
    [[nodiscard]] double getDynamicRangeDb() const noexcept { return dynamicRangeDb.load (std::memory_order_relaxed); }
    [[nodiscard]] bool isUpward() const noexcept { return upward.load (std::memory_order_relaxed); }
    [[nodiscard]] double getTransientEmphasis() const noexcept { return transientEmphasis.load (std::memory_order_relaxed); }
    [[nodiscard]] double getSustainEmphasis() const noexcept { return sustainEmphasis.load (std::memory_order_relaxed); }
    [[nodiscard]] bool isSolo() const noexcept { return solo.load (std::memory_order_relaxed); }
    [[nodiscard]] bool isMute() const noexcept { return mute.load (std::memory_order_relaxed); }
    [[nodiscard]] bool isBypass() const noexcept { return bypass.load (std::memory_order_relaxed); }
    [[nodiscard]] DetectorSource getDetectorSource() const noexcept { return detectorSource.load (std::memory_order_relaxed); }
    [[nodiscard]] Placement getPlacement() const noexcept { return placement.load (std::memory_order_relaxed); }
    [[nodiscard]] bool isAutoThreshold() const noexcept { return autoThreshold.load (std::memory_order_relaxed); }

    [[nodiscard]] float getGainReductionDb() const noexcept { return lastGainReductionDb.load (std::memory_order_relaxed); }
    [[nodiscard]] float getZoneEnergy() const noexcept;
    [[nodiscard]] float getZoneEnergyDb() const noexcept;

private:
    void updateTimeConstants() noexcept;
    [[nodiscard]] double getEffectiveThresholdDb (double manualThresholdDb, double baselineDb) const noexcept;

    std::atomic<double> targetGainDb { 0.0 };
    std::atomic<double> targetThresholdDb { -24.0 };
    std::atomic<double> ratio { 2.0 };
    std::atomic<double> attackMs { 10.0 };
    std::atomic<double> holdMs { 0.0 };
    std::atomic<double> releaseMs { 150.0 };
    std::atomic<double> dynamicRangeDb { 12.0 };
    std::atomic<bool> upward { false };
    std::atomic<double> transientEmphasis { 0.0 };
    std::atomic<double> sustainEmphasis { 0.0 };
    std::atomic<bool> solo { false };
    std::atomic<bool> mute { false };
    std::atomic<bool> bypass { false };
    std::atomic<DetectorSource> detectorSource { DetectorSource::bandLimitedInternal };
    std::atomic<Placement> placement { Placement::stereoLinked };
    std::atomic<bool> autoThreshold { true };

    std::atomic<float> lastGainReductionDb { 0.0f };

    using LinearSmoother = juce::SmoothedValue<double, juce::ValueSmoothingTypes::Linear>;
    LinearSmoother smoothedGain;
    LinearSmoother smoothedThreshold;

    std::array<EnvelopeFollower, 2> envelopeFollowers;
    std::array<TransientSustainSeparator, 2> separators;
    std::array<GainSmootherDb, 2> gainSmoothers;

    EnvelopeFollower zoneEnergyFollower;

    std::array<double, 2> autoBaseline { { 0.0, 0.0 } };
    std::array<double, 2> heldGainTargetDb { { 0.0, 0.0 } };
    std::array<int, 2> holdTimer { { 0, 0 } };

    double currentSampleRate = 44100.0;
};

} // namespace ShredEQ::DSP
