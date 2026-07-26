#pragma once

#include "PitchDetector.h"
#include <atomic>
#include <cmath>

namespace ShredEQ::DSP
{

class NoteFollower final
{
public:
    enum class LowConfidenceBehavior
    {
        hold = 0,
        returnToHome,
        fadeToNeutral,
        bypass,
        Hold = hold,
        ReturnToHome = returnToHome,
        FadeToNeutral = fadeToNeutral,
        Bypass = bypass
    };

    NoteFollower() noexcept;

    void prepare (double sampleRate) noexcept;
    void reset() noexcept;

    // Setters and Getters
    void setEnabled (bool shouldBeEnabled) noexcept;
    bool isEnabled() const noexcept;

    void setTrackingSource (int source) noexcept;
    int getTrackingSource() const noexcept;

    void setHarmonic (int harmonic) noexcept;
    int getHarmonic() const noexcept;

    void setCentsOffset (double cents) noexcept;
    double getCentsOffset() const noexcept;

    void setConfidenceThreshold (double threshold) noexcept;
    double getConfidenceThreshold() const noexcept;

    void setSmoothing (double smoothingMs) noexcept;
    double getSmoothing() const noexcept;

    void setHoldTimeMs (double holdTimeMs) noexcept;
    double getHoldTimeMs() const noexcept;

    void setReturnTimeMs (double returnTimeMs) noexcept;
    double getReturnTimeMs() const noexcept;

    void setHomeFrequencyHz (double homeFreq) noexcept;
    double getHomeFrequencyHz() const noexcept;

    void setFrequencyLimits (double minFreq, double maxFreq) noexcept;
    double getMinFrequencyHz() const noexcept;
    double getMaxFrequencyHz() const noexcept;

    void setMidiPriority (int priority) noexcept;
    int getMidiPriority() const noexcept;

    void setListenEnabled (bool listen) noexcept;
    bool isListenEnabled() const noexcept;

    void setLowConfidenceBehavior (LowConfidenceBehavior behavior) noexcept;
    LowConfidenceBehavior getLowConfidenceBehavior() const noexcept;

    float getFollowGain() const noexcept;
    bool isBypassed() const noexcept;

    // Update methods
    void update (double f0, float confidence, int numSamples) noexcept;
    void update (double audioF0, float audioConfidence, double midiF0, float midiConfidence, int numSamples) noexcept;
    void update (const PitchResult& audioResult, const PitchDetector::MidiTracker& midiTracker, int numSamples) noexcept;

    double getSmoothedFrequencyHz() const noexcept;

private:
    void updateBeta() noexcept;

    std::atomic<bool> enabled { false };
    std::atomic<int> trackingSource { 0 }; // 0 = AudioMonophonic, 1 = MIDI
    std::atomic<int> harmonicMultiplier { 1 };
    std::atomic<double> centsOffset { 0.0 };
    std::atomic<double> confidenceThreshold { 0.5 };
    std::atomic<double> smoothingTimeMs { 100.0 };
    std::atomic<double> holdTimeMs { 200.0 };
    std::atomic<double> returnTimeMs { 1000.0 };
    std::atomic<double> homeFrequencyHz { 1000.0 };
    std::atomic<double> minFrequencyHz { 20.0 };
    std::atomic<double> maxFrequencyHz { 20000.0 };
    std::atomic<int> midiPriority { 2 }; // default last
    std::atomic<bool> listenEnabled { false };
    std::atomic<LowConfidenceBehavior> lowConfidenceBehavior { LowConfidenceBehavior::fadeToNeutral };

    double currentSampleRate = 44100.0;
    double beta = 0.0;

    // State variables
    bool hasValidFrequency = false;
    double lastValidF0 = 0.0;
    double lastValidTargetFreq = 1000.0;
    double currentTargetFreq = 1000.0;
    double smoothedLogFreq = std::log (1000.0);
    double smoothedFreq = 1000.0;
    double elapsedHoldTimeMs = 0.0;
    double elapsedReturnTimeMs = 0.0;
    float followGain = 0.0f;
    bool bypassed = false;
};

} // namespace ShredEQ::DSP
