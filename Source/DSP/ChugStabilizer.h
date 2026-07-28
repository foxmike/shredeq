#pragma once

#include "BiquadCoefficients.h"
#include "TPTFilter.h"

#include <array>
#include <atomic>
#include <cstdint>

namespace juce
{
template <typename SampleType>
class AudioBuffer;
}

namespace ShredEQ::DSP
{

enum class ChugTriggerSource
{
    none = 0,
    rawEnergy,
    dominance,
    transientAcceleration
};

enum class ChugListenMode
{
    normal = 0,
    removedContribution
};

enum class ChugFilterType
{
    wideBell = 0,
    lowShelf
};

struct ChugVisualizationData
{
    bool detectedEvent = false;
    float lowBandLevelDb = -120.0f;
    float midBandLevelDb = -120.0f;
    float lastGainReductionDb = 0.0f;
    float currentFrequencyHz = 120.0f;
    float currentBandwidthOctaves = 1.2f;
    float currentCurveGainDb = 0.0f;
    float eventDurationMs = 0.0f;
    ChugTriggerSource triggerSource = ChugTriggerSource::none;
};

/**
    Frequency-selective palm-mute stabilizer.

    Detection and gain reduction are stereo-linked. The processor never gates:
    outside detected events its normal output is either the unmodified input or
    the configured static support curve.
*/
class ChugStabilizer final
{
public:
    using TriggerSource = ChugTriggerSource;
    using ListenMode = ChugListenMode;
    using FilterType = ChugFilterType;

    ChugStabilizer() noexcept = default;

    void prepare (double sampleRate, int maxBlockSize) noexcept;
    void reset() noexcept;

    void process (float* const* channels, int numChannels,
                  int numSamples) noexcept;
    void process (juce::AudioBuffer<float>& buffer) noexcept;

    void setEnabled (bool shouldBeEnabled) noexcept;
    [[nodiscard]] bool isEnabled() const noexcept;

    void setTargetFrequency (double frequencyHz) noexcept;
    void setFrequency (double frequencyHz) noexcept
    {
        setTargetFrequency (frequencyHz);
    }
    [[nodiscard]] double getTargetFrequency() const noexcept;
    [[nodiscard]] double getFrequency() const noexcept
    {
        return getTargetFrequency();
    }

    void setBandwidthOctaves (double octaves) noexcept;
    void setBandwidth (double octaves) noexcept
    {
        setBandwidthOctaves (octaves);
    }
    [[nodiscard]] double getBandwidthOctaves() const noexcept;

    void setMaximumReductionDb (double reductionDb) noexcept;
    void setMaxReductionDb (double amountDb) noexcept
    {
        setMaximumReductionDb (amountDb);
    }
    void setMaxReduction (double amountDb) noexcept
    {
        setMaximumReductionDb (amountDb);
    }
    [[nodiscard]] double getMaximumReductionDb() const noexcept;

    void setThresholdDb (double thresholdDb) noexcept;
    [[nodiscard]] double getThresholdDb() const noexcept;
    void setAutomaticThreshold (bool shouldUseAutomaticThreshold) noexcept;
    void setAutoThreshold (bool shouldUseAutomaticThreshold) noexcept
    {
        setAutomaticThreshold (shouldUseAutomaticThreshold);
    }
    [[nodiscard]] bool isAutomaticThreshold() const noexcept;

    void setAttackMs (double attackMs) noexcept;
    void setHoldMs (double holdMs) noexcept;
    void setReleaseMs (double releaseMs) noexcept;
    [[nodiscard]] double getAttackMs() const noexcept;
    [[nodiscard]] double getHoldMs() const noexcept;
    [[nodiscard]] double getReleaseMs() const noexcept;

    void setPreservePick (bool shouldPreservePick) noexcept;
    void setPreservePickAmount (double amount) noexcept;
    [[nodiscard]] bool isPreservePickEnabled() const noexcept;
    void setTransientSensitivity (double sensitivity) noexcept;
    void setTransientSens (double sensitivity) noexcept
    {
        setTransientSensitivity (sensitivity);
    }
    void setDominanceSensitivity (double sensitivity) noexcept;
    void setDominanceSens (double sensitivity) noexcept
    {
        setDominanceSensitivity (sensitivity);
    }

    void setStaticSupportGainDb (double gainDb) noexcept;
    void setStaticSupport (double gainDb) noexcept
    {
        setStaticSupportGainDb (gainDb);
    }
    [[nodiscard]] double getStaticSupportGainDb() const noexcept;

    void setFilterType (FilterType newType) noexcept;
    [[nodiscard]] FilterType getFilterType() const noexcept;
    void setListenMode (ListenMode newMode) noexcept;
    [[nodiscard]] ListenMode getListenMode() const noexcept;
    void setListenRemoved (bool shouldListen) noexcept;

    [[nodiscard]] bool getDetectedEvent() const noexcept;
    [[nodiscard]] float getLowBandLevelDb() const noexcept;
    [[nodiscard]] float getLastGainReductionDb() const noexcept;
    [[nodiscard]] TriggerSource getTriggerSource() const noexcept;
    [[nodiscard]] ChugVisualizationData getVisualizationData() const noexcept;

private:
    struct BiquadState
    {
        double z1 = 0.0;
        double z2 = 0.0;
    };

    struct DetectorSnapshot
    {
        double lowDb = -120.0;
        double midDb = -120.0;
        double score = 0.0;
        double targetReductionDb = 0.0;
        TriggerSource trigger = TriggerSource::none;
        bool event = false;
    };

    [[nodiscard]] static double processBiquad (
        double input, const BiquadCoefficients& coefficients,
        BiquadState& state) noexcept;
    [[nodiscard]] DetectorSnapshot detect (double monoInput) noexcept;
    [[nodiscard]] double smoothReduction (
        double targetReductionDb) noexcept;
    void updateDetectorCoefficients (double frequencyHz,
                                     double bandwidthOctaves) noexcept;
    [[nodiscard]] double effectiveThresholdDb() const noexcept;

    static constexpr double minimumFrequencyHz = 60.0;
    static constexpr double maximumFrequencyHz = 320.0;
    static constexpr double minimumBandwidthOctaves = 0.3;
    static constexpr double maximumBandwidthOctaves = 2.5;
    static constexpr double minimumThresholdDb = -90.0;
    static constexpr double maximumThresholdDb = 0.0;
    static constexpr double detectorFloorDb = -120.0;
    static constexpr double stateFlushThreshold = 1.0e-24;

    std::atomic<bool> enabled { false };
    std::atomic<double> targetFrequencyHz { 120.0 };
    std::atomic<double> bandwidthOctaves { 1.2 };
    std::atomic<double> maximumReductionDb { 8.0 };
    std::atomic<double> manualThresholdDb { -30.0 };
    std::atomic<bool> automaticThreshold { true };
    std::atomic<double> attackTimeMs { 3.0 };
    std::atomic<double> holdTimeMs { 25.0 };
    std::atomic<double> releaseTimeMs { 100.0 };
    std::atomic<double> preservePickAmount { 0.5 };
    std::atomic<double> transientSensitivity { 0.5 };
    std::atomic<double> dominanceSensitivity { 0.5 };
    std::atomic<double> staticSupportGainDb { 0.0 };
    std::atomic<FilterType> filterType { FilterType::wideBell };
    std::atomic<ListenMode> listenMode { ListenMode::normal };

    std::atomic<bool> visualEvent { false };
    std::atomic<float> visualLowDb { -120.0f };
    std::atomic<float> visualMidDb { -120.0f };
    std::atomic<float> visualReductionDb { 0.0f };
    std::atomic<float> visualCurveGainDb { 0.0f };
    std::atomic<float> visualEventDurationMs { 0.0f };
    std::atomic<TriggerSource> visualTrigger { TriggerSource::none };

    BiquadCoefficients lowDetectorCoefficients;
    BiquadCoefficients midDetectorCoefficients;
    BiquadCoefficients pickDetectorCoefficients;
    BiquadState lowDetectorState;
    BiquadState midDetectorState;
    BiquadState pickDetectorState;
    TPTFilter reductionFilter;

    double currentSampleRate = 44100.0;
    double configuredDetectorFrequencyHz = 0.0;
    double configuredDetectorBandwidth = 0.0;
    double lowEnvelope = 0.0;
    double midEnvelope = 0.0;
    double broadFastEnvelope = 0.0;
    double broadSlowEnvelope = 0.0;
    double pickEnvelope = 0.0;
    double autoLowBaseline = 1.0e-4;
    double previousLowDb = detectorFloorDb;
    double reductionDb = 0.0;
    std::uint64_t holdSamplesRemaining = 0;
    std::uint64_t eventSamples = 0;
};

} // namespace ShredEQ::DSP
