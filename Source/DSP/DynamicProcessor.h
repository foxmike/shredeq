#pragma once

#include "EnvelopeFollower.h"
#include "TPTFilter.h"
#include <array>
#include <cstdint>

namespace ShredEQ::DSP
{

/**
    DynamicMode defines the operational mode of the dynamic EQ band.
*/
enum class DynamicMode
{
    /** Static EQ mode. Applies no dynamic modulation. processSample returns 1.0. */
    staticEq = 0,

    /** Downward dynamic EQ. Acts when the detector level is above the threshold.
        Always applies a negative gain target (g_target <= 0) to cut the signal,
        which reduces the boost of a positive EQ or deepens the cut of a negative EQ.
        Uses the (1 - 1/R) ratio factor. */
    downwardDynamic,

    /** Upward dynamic EQ. Acts when the detector level is above the threshold.
        Always applies a positive gain target (g_target >= 0) to boost the signal,
        which increases the boost of a positive EQ or reduces the cut of a negative EQ.
        Uses the (1 - 1/R) ratio factor. */
    upwardDynamic,

    /** Compression band. Acts when the detector level is above the threshold.
        Compresses/reduces the static EQ towards 0 dB.
        If staticGainDb is positive, applies a negative gain target (g_target <= 0).
        If staticGainDb is negative, applies a positive gain target (g_target >= 0).
        Uses the (1 - 1/R) ratio factor. */
    compression,

    /** Expansion band (downward expansion). Acts when the detector level is below the threshold.
        Attenuates the EQ band when the level drops below the threshold, pulling it towards 0 dB.
        Always applies a negative gain target (g_target <= 0) with a ratio factor of (R - 1). */
    expansion,

    /** Transient-only band. Feeds the detector from the transient component.
        Operates as downward compression (g_target <= 0) when transient level is above threshold. */
    transientOnly,

    /** Sustain-only band. Feeds the detector from the sustain component.
        Operates as downward compression (g_target <= 0) when sustain level is above threshold. */
    sustainOnly,

    /** Externally sidechained band. Uses the external sidechain sample from the DetectorContext.
        If the external sidechain signal is not active/provided, falls back to internal.
        Operates as downward compression. */
    sidechained
};

/**
    DetectorSource defines the signal source routed into the level detector.
*/
enum class DetectorSource
{
    fullBandInternal = 0,
    bandLimitedInternal,
    left,
    right,
    mid,
    side,
    externalSidechain,
    inputMinusOutput,
    transientComponent,
    sustainComponent
};

/**
    DetectorContext holds input/sidechain audio samples for level detection.
*/
struct DetectorContext
{
    float left = 0.0f;
    float right = 0.0f;
    float mid = 0.0f;
    float side = 0.0f;
    float externalSidechain = 0.0f;
    float input = 0.0f;
    float output = 0.0f;
};

/**
    DynamicVisualizationData stores visualization information for the UI.
*/
struct DynamicVisualizationData
{
    float staticGainDb = 0.0f;
    float maxDynamicRangeDb = 0.0f;
    float currentGainDb = 0.0f;
    float thresholdDb = 0.0f;
    float detectorLevelDb = -120.0f;
    uint32_t reactionFlags = 0; // bitmask: 1 = transient, 2 = sustain, 4 = sidechain

    static constexpr int historySize = 256;
    std::array<float, historySize> gainReductionHistory {};
    int writeIndex = 0;
};

/**
    Per-band dynamic processor implementing all dynamic modes and detector sources.
*/
class DynamicProcessor final
{
public:
    DynamicProcessor() noexcept;
    ~DynamicProcessor() noexcept = default;

    void prepare (double sampleRate) noexcept;
    void reset() noexcept;

    /** Processes a single sample of audio.
        Returns the linear gain multiplier to scale the static EQ boost/cut.
    */
    float processSample (float primarySample, const DetectorContext& context) noexcept;

    /** Processes a single sample of audio, updating the static gain.
        Returns the linear gain multiplier to scale the static EQ boost/cut.
    */
    float processSample (float primarySample, const DetectorContext& context, float staticGainDb) noexcept;

    /** Processes a block of primary samples and computes linear multipliers. */
    void process (const float* primaryInput, float* multiplierOutput, int numSamples, const DetectorContext* contexts = nullptr) noexcept;

    // Setters
    void setMode (DynamicMode newMode) noexcept;
    void setDetectorSource (DetectorSource newSource) noexcept;
    void setThresholdDb (double newThresholdDb) noexcept;
    void setRatio (double newRatio) noexcept;
    void setKneeDb (double newKneeDb) noexcept;
    void setRangeDb (double newRangeDb) noexcept;
    void setAttackMs (double newAttackMs) noexcept;
    void setHoldMs (double newHoldMs) noexcept;
    void setReleaseMs (double newReleaseMs) noexcept;
    void setStaticGainDb (double newStaticGainDb) noexcept;
    void setDetectorFrequency (double newFreqHz) noexcept;
    void setDetectorQ (double newQ) noexcept;

    // Getters
    [[nodiscard]] DynamicMode getMode() const noexcept { return mode; }
    [[nodiscard]] DetectorSource getDetectorSource() const noexcept { return detectorSource; }
    [[nodiscard]] double getThresholdDb() const noexcept { return thresholdDb; }
    [[nodiscard]] double getRatio() const noexcept { return ratio; }
    [[nodiscard]] double getKneeDb() const noexcept { return kneeDb; }
    [[nodiscard]] double getRangeDb() const noexcept { return rangeDb; }
    [[nodiscard]] double getAttackMs() const noexcept { return attackMs; }
    [[nodiscard]] double getHoldMs() const noexcept { return holdMs; }
    [[nodiscard]] double getReleaseMs() const noexcept { return releaseMs; }
    [[nodiscard]] double getStaticGainDb() const noexcept { return staticGainDb; }
    [[nodiscard]] double getDetectorFrequency() const noexcept { return detectorFrequency; }
    [[nodiscard]] double getDetectorQ() const noexcept { return detectorQ; }

    [[nodiscard]] float currentGainLinear() const noexcept { return lastGainLinear; }
    [[nodiscard]] float currentGainChangeDb() const noexcept { return lastGainChangeDb; }
    [[nodiscard]] float currentGainReductionDb() const noexcept { return lastGainReductionDb; }
    [[nodiscard]] DynamicVisualizationData getVisualizationData() const noexcept { return visualizationData; }

    void pushGainReductionHistory (float grDb) noexcept;

private:
    void updateBandPassFilter() noexcept;

    DynamicMode mode = DynamicMode::staticEq;
    DetectorSource detectorSource = DetectorSource::fullBandInternal;

    double thresholdDb = -24.0;
    double ratio = 2.0;
    double kneeDb = 6.0;
    double rangeDb = 12.0;
    double attackMs = 10.0;
    double holdMs = 0.0;
    double releaseMs = 150.0;
    double staticGainDb = 0.0;
    double detectorFrequency = 1000.0;
    double detectorQ = 1.0;

    double currentSampleRate = 44100.0;

    EnvelopeFollower envelopeFollower;
    TransientSustainSeparator separator;
    GainSmootherDb gainSmoother;
    TPTFilter bandPassFilter;

    DynamicVisualizationData visualizationData;

    float lastGainLinear = 1.0f;
    float lastGainChangeDb = 0.0f;
    float lastGainReductionDb = 0.0f;
    double heldTargetDb = 0.0;
    int holdSamplesRemaining = 0;
};

} // namespace ShredEQ::DSP
