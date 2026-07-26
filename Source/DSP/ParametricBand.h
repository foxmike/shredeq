#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include "BiquadCoefficients.h"
#include "TPTFilter.h"
#include "DynamicProcessor.h"
#include <array>
#include <atomic>

namespace ShredEQ::DSP
{

enum class StereoRoutingMode
{
    linked = 0,
    unlinked,
    left,
    right,
    mid,
    side
};

enum class BandStage
{
    stageA = 0,
    stageB
};

enum class BandFilterType
{
    bell = 0,
    notch,
    lowShelf,
    highShelf,
    lowPass,
    highPass,
    bandPass,
    tilt,
    resonantHighPass,
    resonantLowPass,
    flatDynamic
};

/**
    Maps a BandFilterType choice index enum to ShredEQ::DSP::FilterKind.
*/
inline FilterKind toFilterKind (BandFilterType type) noexcept
{
    switch (type)
    {
        case BandFilterType::bell:             return FilterKind::bell;
        case BandFilterType::notch:            return FilterKind::notch;
        case BandFilterType::lowShelf:         return FilterKind::lowShelf;
        case BandFilterType::highShelf:        return FilterKind::highShelf;
        case BandFilterType::lowPass:          return FilterKind::lowPass;
        case BandFilterType::highPass:         return FilterKind::highPass;
        case BandFilterType::bandPass:         return FilterKind::bandPass;
        case BandFilterType::tilt:             return FilterKind::tilt;
        case BandFilterType::resonantHighPass: return FilterKind::resonantHighPass;
        case BandFilterType::resonantLowPass:  return FilterKind::resonantLowPass;
        case BandFilterType::flatDynamic:      return FilterKind::bypass;
    }
    return FilterKind::bypass;
}

/**
    Represents a single band in the parametric equalizer.
    
    CRITICAL: Mid/side encoding and decoding are owned by a later stage router.
    The band processes channels in whichever domain is passed in.
    The routing mode determines which channel(s) are filtered based on the current domain.
    If the domain is Left/Right (isMidSideDomain = false):
      - linked / unlinked: filters both channels (0 and 1)
      - left: filters channel 0 (Left)
      - right: filters channel 1 (Right)
      - mid / side: pass-through (do nothing)
    If the domain is Mid/Side (isMidSideDomain = true):
      - linked / unlinked: filters both channels (0 and 1)
      - mid: filters channel 0 (Mid)
      - side: filters channel 1 (Side)
      - left / right: pass-through (do nothing)
*/
class ParametricBand
{
public:
    ParametricBand() noexcept;

    void prepare (double sampleRate, int maxBlockSize) noexcept;
    void reset() noexcept;

    /** Processes the audio.
        Crucially, this band does NOT do M/S conversion internally.
        It expects the input domain to match isMidSide.
    */
    void process (float* const* channels, int numChannels, int numSamples, bool isMidSide, const float* const* sidechainChannels = nullptr, int numSidechainChannels = 0) noexcept;
    void process (juce::AudioBuffer<float>& buffer, bool isMidSide, const float* const* sidechainChannels = nullptr, int numSidechainChannels = 0) noexcept;

    // Getters and Setters
    void setEnabled (bool shouldBeEnabled) noexcept;
    bool isEnabled() const noexcept;

    void setStage (BandStage newStage) noexcept;
    BandStage getStage() const noexcept;

    void setFrequency (double newFreq) noexcept;
    double getFrequency() const noexcept;

    void setGainDb (double newGainDb) noexcept;
    double getGainDb() const noexcept;

    void setQ (double newQ) noexcept;
    double getQ() const noexcept;

    void setFilterType (BandFilterType newType) noexcept;
    BandFilterType getFilterType() const noexcept;

    void setSlope (FilterSlope newSlope) noexcept;
    void setSlope (int slopeDbPerOctave) noexcept;
    FilterSlope getSlope() const noexcept;

    void setRoutingMode (StereoRoutingMode newMode) noexcept;
    StereoRoutingMode getRoutingMode() const noexcept;

    void setUseTpt (bool useTpt) noexcept;
    bool getUseTpt() const noexcept;

    void setModulated (bool modulated) noexcept;
    bool getModulated() const noexcept;

    // Dynamic EQ setters and getters
    void setDynamicMode (int mode) noexcept;
    int getDynamicMode() const noexcept;
    
    void setDynamicRangeDb (double range) noexcept;
    double getDynamicRangeDb() const noexcept;
    
    void setThresholdDb (double thresh) noexcept;
    double getThresholdDb() const noexcept;
    
    void setRatio (double r) noexcept;
    double getRatio() const noexcept;
    
    void setAttackMs (double a) noexcept;
    double getAttackMs() const noexcept;
    
    void setHoldMs (double h) noexcept;
    double getHoldMs() const noexcept;
    
    void setReleaseMs (double r) noexcept;
    double getReleaseMs() const noexcept;
    
    void setKneeDb (double k) noexcept;
    double getKneeDb() const noexcept;
    
    void setDetectorSource (int source) noexcept;
    int getDetectorSource() const noexcept;
    
    void setDetectorFrequency (double freq) noexcept;
    double getDetectorFrequency() const noexcept;
    
    void setDetectorQ (double qVal) noexcept;
    double getDetectorQ() const noexcept;
    
    void setTrackingSource (int source) noexcept;
    int getTrackingSource() const noexcept;
    
    void setTrackingHarmonic (int harmonic) noexcept;
    int getTrackingHarmonic() const noexcept;
    
    void setLookaheadMs (double ms) noexcept;
    double getLookaheadMs() const noexcept;

    void setDynamicsEnabled (bool dynamicsShouldBeEnabled) noexcept;
    bool isDynamicsEnabled() const noexcept;

    DynamicVisualizationData getDynamicVisualizationData() const noexcept;

private:
    bool shouldUseTpt() const noexcept;
    bool shouldFilterChannel (int channelIndex, bool isMidSideDomain) const noexcept;
    void updateActiveCoefficients (double f, double g, double q) noexcept;

    std::atomic<bool> enabled { false };
    std::atomic<BandStage> stage { BandStage::stageA };
    
    double targetFrequency = 1000.0;
    double targetGainDb = 0.0;
    double targetQ = 0.7071067811865476;

    std::atomic<BandFilterType> filterType { BandFilterType::bell };
    std::atomic<FilterSlope> slope { FilterSlope::dB12 };
    std::atomic<StereoRoutingMode> routingMode { StereoRoutingMode::linked };
    std::atomic<bool> useTpt { false };
    std::atomic<bool> isModulated { false };

    // Dynamic EQ configuration
    std::atomic<int> dynamicMode { 0 };
    std::atomic<double> dynamicRangeDb { 0.0 };
    std::atomic<double> thresholdDb { -24.0 };
    std::atomic<double> ratio { 2.0 };
    std::atomic<double> attackMs { 10.0 };
    std::atomic<double> holdMs { 0.0 };
    std::atomic<double> releaseMs { 150.0 };
    std::atomic<double> kneeDb { 6.0 };
    std::atomic<int> detectorSource { 1 };
    std::atomic<double> detectorFrequency { 1000.0 };
    std::atomic<double> detectorQ { 1.0 };
    std::atomic<int> trackingSource { 0 };
    std::atomic<int> trackingHarmonic { 0 };
    std::atomic<double> lookaheadMs { 0.0 };
    std::atomic<bool> dynamicsEnabled { false };

    // Smoothing
    using FrequencySmoother = juce::SmoothedValue<double, juce::ValueSmoothingTypes::Multiplicative>;
    using LinearSmoother = juce::SmoothedValue<double, juce::ValueSmoothingTypes::Linear>;

    FrequencySmoother smoothedFrequency { 1000.0 };
    LinearSmoother smoothedGain { 0.0 };
    LinearSmoother smoothedQ { 0.7071067811865476 };

    double currentSampleRate = 44100.0;

    // Cache of last state to detect discrete parameter updates
    BandFilterType lastType = BandFilterType::bell;
    FilterSlope lastSlope = FilterSlope::dB12;
    bool lastUseTpt = false;

    // Static biquad cascade
    CoefficientCascade currentCascade;

    struct BiquadSectionState
    {
        double s1 = 0.0;
        double s2 = 0.0;
    };

    struct BiquadCascadeState
    {
        std::array<BiquadSectionState, maxCascadeSections> sections {};
    };

    std::array<BiquadCascadeState, 2> biquadStates;

    // Continuously modulated TPT SVG path
    TPTFilter tptFilter;

    // Dynamic Processor instance and last output tracking
    DynamicProcessor dynamicProcessor;
    float lastOutputL = 0.0f;
    float lastOutputR = 0.0f;
};

inline constexpr int kNumParametricBands = 12;

class ParametricEQ
{
public:
    ParametricEQ() noexcept = default;

    void prepare (double sampleRate, int maxBlockSize) noexcept;
    void reset() noexcept;

    void process (float* const* channels, int numChannels, int numSamples, bool isMidSide, BandStage stage, const float* const* sidechainChannels = nullptr, int numSidechainChannels = 0) noexcept;
    void process (juce::AudioBuffer<float>& buffer, bool isMidSide, BandStage stage, const float* const* sidechainChannels = nullptr, int numSidechainChannels = 0) noexcept;

    ParametricBand& operator[] (std::size_t index) noexcept { return bands[index]; }
    const ParametricBand& operator[] (std::size_t index) const noexcept { return bands[index]; }

    std::array<ParametricBand, kNumParametricBands>& getBands() noexcept { return bands; }
    const std::array<ParametricBand, kNumParametricBands>& getBands() const noexcept { return bands; }

private:
    std::array<ParametricBand, kNumParametricBands> bands;
};

} // namespace ShredEQ::DSP
