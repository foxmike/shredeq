#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <atomic>

namespace ShredEQ::DSP
{

/**
    Optional global lookahead delay.

    The delay memory is allocated by prepare() for every prepared channel and
    for the maximum 5 ms mode. process() performs no allocation or locking.

    getLatencySamples() is the host-latency contract: after changing the mode,
    the owning AudioProcessor must pass that exact value to
    AudioProcessor::setLatencySamples(). Off always reports zero and is an
    in-place true bypass. A short internal crossfade makes changes between
    delay taps click-free.
*/
class LookaheadDelay final
{
public:
    enum class Mode
    {
        Off = 0,
        Milliseconds0_5,
        Milliseconds1,
        Milliseconds2,
        Milliseconds5
    };

    LookaheadDelay() noexcept = default;
    ~LookaheadDelay() noexcept = default;

    /** Allocates the circular buffer. This must not be called on the audio thread. */
    void prepare (double sampleRate, int maxBlockSize, int numChannels);

    /** Clears all delayed audio and cancels any in-progress delay transition. */
    void reset() noexcept;

    /** Processes all prepared channels in-place. */
    void process (juce::AudioBuffer<float>& buffer) noexcept;

    void setMode (Mode newMode) noexcept;
    [[nodiscard]] Mode getMode() const noexcept;

    /**
        Selects the nearest allowed delay: 0, 0.5, 1, 2, or 5 ms.
        Non-finite and non-positive values select Off.
    */
    void setDelayMilliseconds (double milliseconds) noexcept;

    /** Returns the exact integer latency for the selected mode. */
    [[nodiscard]] int getLatencySamples() const noexcept;

private:
    [[nodiscard]] static double millisecondsForMode (Mode mode) noexcept;
    [[nodiscard]] int samplesForMode (Mode mode) const noexcept;
    void beginTransitionIfNeeded() noexcept;

    static constexpr double maximumDelayMilliseconds = 5.0;
    static constexpr double crossfadeMilliseconds = 5.0;

    juce::AudioBuffer<float> delayBuffer;

    std::atomic<Mode> requestedMode { Mode::Off };
    std::atomic<int> requestedDelaySamples { 0 };
    std::atomic<double> preparedSampleRate { 44100.0 };

    int ringLength = 0;
    int writeIndex = 0;
    int preparedChannels = 0;

    int currentDelaySamples = 0;
    int transitionFromDelaySamples = 0;
    int transitionToDelaySamples = 0;
    int transitionLengthSamples = 1;
    int transitionPosition = 0;
    bool transitionActive = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LookaheadDelay)
};

} // namespace ShredEQ::DSP
