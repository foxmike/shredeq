#pragma once

#include "ResonanceDetector.h"
#include "BiquadCoefficients.h"
#include "ParametricBand.h"
#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <cmath>

namespace ShredEQ::DSP
{

/**
    A single suppressor slot with a notch/bell filter and parameter smoothing.
*/
class SuppressorSlot final
{
public:
    SuppressorSlot() noexcept;

    void prepare (double sampleRate) noexcept;
    void reset() noexcept;

    void setEnabled (bool shouldBeEnabled) noexcept { enabled = shouldBeEnabled; }
    [[nodiscard]] bool isEnabled() const noexcept { return enabled; }

    void setFrequency (double newFreq) noexcept;
    [[nodiscard]] double getFrequency() const noexcept { return targetFrequency; }

    void setQ (double newQ) noexcept;
    [[nodiscard]] double getQ() const noexcept { return targetQ; }

    void setMaxDepthDb (double newDepth) noexcept { targetMaxDepth = newDepth; }
    [[nodiscard]] double getMaxDepthDb() const noexcept { return targetMaxDepth; }

    void setAttackMs (double attack) noexcept { attackMs = attack; }
    [[nodiscard]] double getAttackMs() const noexcept { return attackMs; }

    void setReleaseMs (double release) noexcept { releaseMs = release; }
    [[nodiscard]] double getReleaseMs() const noexcept { return releaseMs; }

    void setPersistenceRequirement (double persistence) noexcept { persistenceRequirement = persistence; }
    [[nodiscard]] double getPersistenceRequirement() const noexcept { return persistenceRequirement; }

    void setIsStatic (bool staticMode) noexcept { isStaticMode = staticMode; }
    [[nodiscard]] bool getIsStatic() const noexcept { return isStaticMode; }

    void setPlacement (StereoRoutingMode newPlacement) noexcept { placement = newPlacement; }
    [[nodiscard]] StereoRoutingMode getPlacement() const noexcept { return placement; }

    void setConfidence (float conf) noexcept { confidence = conf; }
    [[nodiscard]] float getConfidence() const noexcept { return confidence; }

    void setFreeze (bool shouldFreeze) noexcept { freeze = shouldFreeze; }
    [[nodiscard]] bool getFreeze() const noexcept { return freeze; }

    void setSolo (bool shouldSolo) noexcept { solo = shouldSolo; }
    [[nodiscard]] bool getSolo() const noexcept { return solo; }

    void setRemovedAudition (bool shouldAudition) noexcept { removedAudition = shouldAudition; }
    [[nodiscard]] bool getRemovedAudition() const noexcept { return removedAudition; }

    void loadFromCandidate (const ResonanceCandidate& candidate, bool dynamic) noexcept;

    void updateDepthTarget (const ResonanceDetector& detector) noexcept;
    void updateEnvelope (double sampleRate) noexcept;
    void processSample (double& left, double& right, double sampleRate) noexcept;

    // Direct coefficient calculation for manual/audition use
    void calculateCoefficients (double sampleRate, double freq, double q, double depthDb) noexcept;

private:
    struct BiquadState
    {
        double s1 = 0.0;
        double s2 = 0.0;

        void reset() noexcept
        {
            s1 = 0.0;
            s2 = 0.0;
        }

        inline double process (double sample, double b0, double b1, double b2, double a1, double a2) noexcept
        {
            const double output = b0 * sample + s1;
            s1 = b1 * sample - a1 * output + s2;
            s2 = b2 * sample - a2 * output;

            if (std::abs (s1) < 1.0e-24) s1 = 0.0;
            if (std::abs (s2) < 1.0e-24) s2 = 0.0;

            return output;
        }
    };

    bool enabled = false;
    double targetFrequency = 3000.0;
    double targetQ = 10.0;
    double targetMaxDepth = 6.0;
    double attackMs = 10.0;
    double releaseMs = 200.0;
    double persistenceRequirement = 50.0;
    bool isStaticMode = false;
    StereoRoutingMode placement = StereoRoutingMode::linked;
    float confidence = 0.0f;
    bool freeze = false;
    bool solo = false;
    bool removedAudition = false;

    // Dynamic state
    double targetDynamicDepth = 0.0;
    double candidateTargetDepth = 0.0;
    double currentDepthDb = 0.0;
    double detectorEnvelope = 0.0;

    // Smoothed values for audio parameter sweeps
    juce::SmoothedValue<double, juce::ValueSmoothingTypes::Multiplicative> smoothedFrequency { 3000.0 };
    juce::SmoothedValue<double, juce::ValueSmoothingTypes::Linear> smoothedQ { 10.0 };

    // Coefficient cache
    double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;
    double lastFreq = 0.0;
    double lastQ = 0.0;
    double lastSampleRate = 0.0;
    double cachedCosine = 0.0;
    double cachedAlpha = 0.0;
    BiquadCoefficients detectorCoefficients;

    std::array<BiquadState, 2> channelStates;
    std::array<BiquadState, 2> detectorStates;
};

/**
    Manages the resonance suppressor's six slots and the scan workflow.
*/
class ResonanceSuppressor final
{
public:
    static constexpr int numSlots = 6; // expandable to 8 in v1.1
    static constexpr int maxScanCards = 12;
    static constexpr int maxExclusions = 16;

    struct CandidateCard
    {
        ResonanceCandidate candidate;
        bool rejected = false;
        bool intentional = false;
        bool excluded = false;
        double adjustedDepthDb = -1.0;
    };

    ResonanceSuppressor() noexcept;

    void prepare (double sampleRate) noexcept;
    void reset() noexcept;

    void setEnabled (bool shouldBeEnabled) noexcept { enabled = shouldBeEnabled; }
    [[nodiscard]] bool isEnabled() const noexcept { return enabled; }

    void process (float* const* channels, int numChannels, int numSamples) noexcept;
    void process (juce::AudioBuffer<float>& buffer) noexcept;

    // Slot Access
    [[nodiscard]] SuppressorSlot& getSlot (int index) noexcept { return slots[static_cast<std::size_t> (index)]; }
    [[nodiscard]] const SuppressorSlot& getSlot (int index) const noexcept { return slots[static_cast<std::size_t> (index)]; }

    // Scan Workflow APIs
    void startScan (double durationSeconds, double sampleRate, ResonanceDetector& detector) noexcept;
    void updateScan (int numSamples, ResonanceDetector& detector) noexcept;
    void endScan (ResonanceDetector& detector) noexcept;
    void cancelScan() noexcept;

    [[nodiscard]] bool isScanning() const noexcept { return scanningActive; }
    [[nodiscard]] double getScanProgress() const noexcept;

    [[nodiscard]] int getNumCandidateCards() const noexcept { return numCandidateCards; }
    [[nodiscard]] CandidateCard getCandidateCard (int index) const noexcept;

    void rejectCandidate (int index) noexcept;
    void classifyIntentional (int index) noexcept;
    void excludeCandidate (int index) noexcept;
    void adjustCandidateDepth (int index, double newDepthDb) noexcept;

    void applyCandidate (int slotIndex, const ResonanceCandidate& candidate, bool dynamic) noexcept;

    // Audition temporary card helper
    void setAuditionCard (int index, bool listenRemoved) noexcept;
    [[nodiscard]] int getAuditionCardIndex() const noexcept { return auditionCardIndex; }
    [[nodiscard]] bool getAuditionListenRemoved() const noexcept { return auditionListenRemoved; }

private:
    bool enabled = true;
    double currentSampleRate = 44100.0;

    std::array<SuppressorSlot, numSlots> slots;

    // Scan state
    bool scanningActive = false;
    double scanDurationSeconds = 5.0;
    double scanSampleCount = 0.0;
    double scanTargetSamples = 0.0;

    std::array<CandidateCard, maxScanCards> candidateCards {};
    int numCandidateCards = 0;

    std::array<double, maxExclusions> excludedFrequencies {};
    int numExcludedFrequencies = 0;

    // Temporary audition filter and state
    int auditionCardIndex = -1;
    bool auditionListenRemoved = false;
    SuppressorSlot auditionFilter;
};

} // namespace ShredEQ::DSP
