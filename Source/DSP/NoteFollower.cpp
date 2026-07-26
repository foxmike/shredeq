#include "NoteFollower.h"
#include <cmath>
#include <algorithm>

namespace ShredEQ::DSP
{

NoteFollower::NoteFollower() noexcept
{
    reset();
}

void NoteFollower::prepare (double sampleRate) noexcept
{
    currentSampleRate = sampleRate > 0.0 ? sampleRate : 44100.0;
    updateBeta();
    reset();
}

void NoteFollower::reset() noexcept
{
    hasValidFrequency = false;
    lastValidF0 = 0.0;
    const double home = homeFrequencyHz.load();
    lastValidTargetFreq = home;
    currentTargetFreq = home;
    smoothedLogFreq = std::log (std::max (1e-5, home));
    smoothedFreq = home;
    elapsedHoldTimeMs = 0.0;
    elapsedReturnTimeMs = 0.0;
    followGain = 0.0f;
    bypassed = false;
}

void NoteFollower::setEnabled (bool shouldBeEnabled) noexcept { enabled = shouldBeEnabled; }
bool NoteFollower::isEnabled() const noexcept { return enabled.load(); }

void NoteFollower::setTrackingSource (int source) noexcept { trackingSource = source; }
int NoteFollower::getTrackingSource() const noexcept { return trackingSource.load(); }

void NoteFollower::setHarmonic (int harmonic) noexcept { harmonicMultiplier = std::clamp (harmonic, 1, 8); }
int NoteFollower::getHarmonic() const noexcept { return harmonicMultiplier.load(); }

void NoteFollower::setCentsOffset (double cents) noexcept { centsOffset = cents; }
double NoteFollower::getCentsOffset() const noexcept { return centsOffset.load(); }

void NoteFollower::setConfidenceThreshold (double threshold) noexcept { confidenceThreshold = std::clamp (threshold, 0.0, 1.0); }
double NoteFollower::getConfidenceThreshold() const noexcept { return confidenceThreshold.load(); }

void NoteFollower::setSmoothing (double smoothingMs) noexcept
{
    smoothingTimeMs = std::clamp (smoothingMs, 0.0, 10000.0);
    updateBeta();
}
double NoteFollower::getSmoothing() const noexcept { return smoothingTimeMs.load(); }

void NoteFollower::setHoldTimeMs (double htMs) noexcept { holdTimeMs = std::clamp (htMs, 0.0, 10000.0); }
double NoteFollower::getHoldTimeMs() const noexcept { return holdTimeMs.load(); }

void NoteFollower::setReturnTimeMs (double rtMs) noexcept { returnTimeMs = std::clamp (rtMs, 0.0, 10000.0); }
double NoteFollower::getReturnTimeMs() const noexcept { return returnTimeMs.load(); }

void NoteFollower::setHomeFrequencyHz (double homeFreq) noexcept { homeFrequencyHz = std::clamp (homeFreq, 15.0, 24000.0); }
double NoteFollower::getHomeFrequencyHz() const noexcept { return homeFrequencyHz.load(); }

void NoteFollower::setFrequencyLimits (double minFreq, double maxFreq) noexcept
{
    minFrequencyHz = std::clamp (minFreq, 15.0, 24000.0);
    maxFrequencyHz = std::clamp (maxFreq, 15.0, 24000.0);
}
double NoteFollower::getMinFrequencyHz() const noexcept { return minFrequencyHz.load(); }
double NoteFollower::getMaxFrequencyHz() const noexcept { return maxFrequencyHz.load(); }

void NoteFollower::setMidiPriority (int priority) noexcept { midiPriority = priority; }
int NoteFollower::getMidiPriority() const noexcept { return midiPriority.load(); }

void NoteFollower::setListenEnabled (bool listen) noexcept { listenEnabled = listen; }
bool NoteFollower::isListenEnabled() const noexcept { return listenEnabled.load(); }

void NoteFollower::setLowConfidenceBehavior (LowConfidenceBehavior behavior) noexcept { lowConfidenceBehavior = behavior; }
NoteFollower::LowConfidenceBehavior NoteFollower::getLowConfidenceBehavior() const noexcept { return lowConfidenceBehavior.load(); }

float NoteFollower::getFollowGain() const noexcept { return followGain; }
bool NoteFollower::isBypassed() const noexcept { return bypassed; }

double NoteFollower::getSmoothedFrequencyHz() const noexcept { return smoothedFreq; }

void NoteFollower::updateBeta() noexcept
{
    const double tau = smoothingTimeMs.load() / 1000.0;
    if (tau <= 0.0 || currentSampleRate <= 0.0)
    {
        beta = 0.0;
    }
    else
    {
        beta = std::exp (-1.0 / (currentSampleRate * tau));
    }
}

void NoteFollower::update (double f0, float confidence, int numSamples) noexcept
{
    if (numSamples <= 0)
        return;

    const double dtMs = (static_cast<double> (numSamples) / currentSampleRate) * 1000.0;
    const double thresh = confidenceThreshold.load();

    if (confidence >= thresh && f0 > 0.0)
    {
        hasValidFrequency = true;
        lastValidF0 = f0;
        
        const double h = static_cast<double> (harmonicMultiplier.load());
        const double c = centsOffset.load();
        double f_target = f0 * h * std::pow (2.0, c / 1200.0);
        f_target = std::clamp (f_target, minFrequencyHz.load(), maxFrequencyHz.load());
        
        lastValidTargetFreq = f_target;
        currentTargetFreq = f_target;
        
        elapsedHoldTimeMs = 0.0;
        elapsedReturnTimeMs = 0.0;
        followGain = 1.0f;
        bypassed = false;
    }
    else
    {
        if (hasValidFrequency)
        {
            elapsedHoldTimeMs += dtMs;
            const double holdTime = holdTimeMs.load();
            
            if (elapsedHoldTimeMs <= holdTime)
            {
                currentTargetFreq = lastValidTargetFreq;
                followGain = 1.0f;
                bypassed = false;
            }
            else
            {
                elapsedReturnTimeMs += dtMs;
                const double returnTime = returnTimeMs.load();
                double progress = 0.0;
                if (returnTime > 0.0)
                    progress = std::min (1.0, elapsedReturnTimeMs / returnTime);
                else
                    progress = 1.0;
                
                const auto behavior = lowConfidenceBehavior.load();
                if (behavior == LowConfidenceBehavior::hold)
                {
                    currentTargetFreq = lastValidTargetFreq;
                    followGain = 1.0f;
                    bypassed = false;
                }
                else if (behavior == LowConfidenceBehavior::returnToHome)
                {
                    const double home = homeFrequencyHz.load();
                    currentTargetFreq = lastValidTargetFreq + progress * (home - lastValidTargetFreq);
                    followGain = 1.0f;
                    bypassed = false;
                }
                else if (behavior == LowConfidenceBehavior::fadeToNeutral)
                {
                    currentTargetFreq = lastValidTargetFreq;
                    followGain = 1.0f - static_cast<float> (progress);
                    bypassed = false;
                }
                else if (behavior == LowConfidenceBehavior::bypass)
                {
                    currentTargetFreq = lastValidTargetFreq;
                    followGain = 1.0f - static_cast<float> (progress);
                    if (progress >= 1.0)
                        bypassed = true;
                    else
                        bypassed = false;
                }
            }
        }
        else
        {
            // No valid frequency has been tracked yet
            currentTargetFreq = homeFrequencyHz.load();
            followGain = 0.0f;
            bypassed = false;
        }
    }

    // Now run log-frequency smoothing
    const double targetLogFreq = std::log (std::max (1e-5, currentTargetFreq));
    if (beta <= 0.0)
    {
        smoothedLogFreq = targetLogFreq;
    }
    else
    {
        const double betaN = std::pow (beta, numSamples);
        smoothedLogFreq = betaN * smoothedLogFreq + (1.0 - betaN) * targetLogFreq;
    }
    smoothedFreq = std::exp (smoothedLogFreq);

    if (!std::isfinite (smoothedLogFreq))
        smoothedLogFreq = std::log (std::max (1e-5, homeFrequencyHz.load()));
    if (!std::isfinite (smoothedFreq))
        smoothedFreq = homeFrequencyHz.load();
}

void NoteFollower::update (double audioF0, float audioConfidence, double midiF0, float midiConfidence, int numSamples) noexcept
{
    const auto source = trackingSource.load();
    if (source == 0) // AudioMonophonic
    {
        update (audioF0, audioConfidence, numSamples);
    }
    else // MIDI
    {
        update (midiF0, midiConfidence, numSamples);
    }
}

void NoteFollower::update (const PitchResult& audioResult, const PitchDetector::MidiTracker& midiTracker, int numSamples) noexcept
{
    const auto source = trackingSource.load();
    if (source == 0) // AudioMonophonic
    {
        update (audioResult.frequencyHz, audioResult.confidence, numSamples);
    }
    else // MIDI
    {
        const double midiFreq = midiTracker.getFrequencyHz();
        const float midiConf = (midiTracker.getSelectedNote() >= 0) ? 1.0f : 0.0f;
        update (midiFreq, midiConf, numSamples);
    }
}

} // namespace ShredEQ::DSP
