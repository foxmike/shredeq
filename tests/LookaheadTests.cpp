// =============================================================================
// ShredEQ - Lookahead Delay Unit Tests
// =============================================================================
// Covers the public DSP surface of Source/DSP/LookaheadDelay per
// .abracapocus/SHREDEQ_SPEC.md §19.2 (Optional Lookahead: 0.5/1/2/5 ms modes,
// exact reported latency, no hidden latency) and §21.2 stability:
//   * Off mode: getLatencySamples()==0 and true pass-through
//   * 0.5/1/2/5 ms modes report exact integer latency at a fixed sample rate
//   * Impulse emerges at the expected delay index ±1 sample
//   * Mode change never produces NaN/Inf; after settle delay matches new mode
//   * reset() clears residual so a subsequent impulse is not polluted
//
// Uses juce::UnitTest only — no Catch2/Google Test. Only public APIs are
// exercised; no production DSP code is rewritten.
// =============================================================================

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>

#include "Source/DSP/LookaheadDelay.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace
{

using namespace ShredEQ::DSP;

constexpr double kSampleRate = 48000.0;
constexpr int kChannels = 2;

double millisecondsForMode (LookaheadDelay::Mode mode) noexcept
{
    switch (mode)
    {
        case LookaheadDelay::Mode::Milliseconds0_5: return 0.5;
        case LookaheadDelay::Mode::Milliseconds1:   return 1.0;
        case LookaheadDelay::Mode::Milliseconds2:   return 2.0;
        case LookaheadDelay::Mode::Milliseconds5:   return 5.0;
        case LookaheadDelay::Mode::Off:
        default:                                    return 0.0;
    }
}

int expectedLatency (LookaheadDelay::Mode mode) noexcept
{
    return static_cast<int> (std::round (
        millisecondsForMode (mode) * kSampleRate / 1000.0));
}

bool allFinite (const juce::AudioBuffer<float>& buffer)
{
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        for (int n = 0; n < buffer.getNumSamples(); ++n)
            if (! std::isfinite (buffer.getSample (ch, n)))
                return false;
    return true;
}

// Index of the sample with the largest absolute value on channel 0 (the
// impulse emergence index for a single-impulse input).
int peakIndex (const juce::AudioBuffer<float>& buffer)
{
    int idx = 0;
    float peak = -1.0f;
    for (int n = 0; n < buffer.getNumSamples(); ++n)
    {
        const auto v = std::abs (buffer.getSample (0, n));
        if (v > peak)
        {
            peak = v;
            idx = n;
        }
    }
    return idx;
}

} // namespace

// =============================================================================

class LookaheadTests final : public juce::UnitTest
{
public:
    LookaheadTests()
        : juce::UnitTest ("LookaheadTests", "ShredEQ/DSP") {}

    void runTest() override
    {
        testOffModeZeroLatencyPassThrough();
        testModeLatencyAndImpulseDelay();
        testModeChangeNoNaNAndSettle();
        testResetClearsResidual();
    }

    // -----------------------------------------------------------------------
    // §19.2: Off mode reports zero latency and is a true pass-through.
    // -----------------------------------------------------------------------

    void testOffModeZeroLatencyPassThrough()
    {
        beginTest ("LookaheadDelay: Off mode => 0 latency and pass-through within tolerance");

        LookaheadDelay delay;
        delay.setMode (LookaheadDelay::Mode::Off);
        delay.prepare (kSampleRate, 256, kChannels);

        expectEquals (delay.getLatencySamples(), 0,
                      "Off mode must report zero latency");
        expect (delay.getMode() == LookaheadDelay::Mode::Off,
                "getMode must report Off");

        juce::AudioBuffer<float> input (kChannels, 2048);
        for (int n = 0; n < input.getNumSamples(); ++n)
        {
            const double t = static_cast<double> (n) / kSampleRate;
            const auto v = static_cast<float> (0.3 * std::sin (2.0 * 3.14159265358979323846 * 440.0 * t));
            for (int ch = 0; ch < kChannels; ++ch)
                input.setSample (ch, n, v);
        }

        juce::AudioBuffer<float> work (kChannels, input.getNumSamples());
        for (int ch = 0; ch < kChannels; ++ch)
            work.copyFrom (ch, 0, input, ch, 0, input.getNumSamples());

        delay.process (work);

        expect (allFinite (work), "Off mode output must be finite");

        double maxErr = 0.0;
        for (int ch = 0; ch < kChannels; ++ch)
            for (int n = 0; n < work.getNumSamples(); ++n)
                maxErr = std::max (maxErr, std::abs (static_cast<double> (work.getSample (ch, n))
                                                     - static_cast<double> (input.getSample (ch, n))));
        expectWithinAbsoluteError (maxErr, 0.0, 1.0e-9,
                                   "Off mode must be a true pass-through; maxErr=" + juce::String (maxErr));
    }

    // -----------------------------------------------------------------------
    // §19.2: each 0.5/1/2/5 ms mode reports the exact integer latency and an
    // impulse emerges at the expected delay index ±1 sample.
    // -----------------------------------------------------------------------

    void testModeLatencyAndImpulseDelay()
    {
        const std::array<LookaheadDelay::Mode, 4> modes {
            LookaheadDelay::Mode::Milliseconds0_5,
            LookaheadDelay::Mode::Milliseconds1,
            LookaheadDelay::Mode::Milliseconds2,
            LookaheadDelay::Mode::Milliseconds5
        };

        for (const auto mode : modes)
        {
            const auto expLat = expectedLatency (mode);
            beginTest ("LookaheadDelay: mode " + juce::String (millisecondsForMode (mode))
                       + " ms reports exact latency and impulse delay within ±1 sample");

            LookaheadDelay delay;
            delay.setMode (mode);
            delay.prepare (kSampleRate, 256, kChannels);

            expectEquals (delay.getLatencySamples(), expLat,
                          "Mode must report exact round(ms*sr/1000) latency");

            // Reset so the ring is clear, then feed a single impulse followed
            // by silence long enough for it to emerge at the delay tap.
            delay.reset();

            const int bufferLen = expLat + 16;
            juce::AudioBuffer<float> buffer (kChannels, bufferLen);
            buffer.clear();
            buffer.setSample (0, 0, 1.0f);
            buffer.setSample (1, 0, 1.0f);

            delay.process (buffer);

            expect (allFinite (buffer), "Impulse response must be finite");

            const int peak = peakIndex (buffer);
            expect (std::abs (peak - expLat) <= 1,
                    "Impulse must emerge within ±1 sample of reported latency; expected="
                    + juce::String (expLat) + " peak=" + juce::String (peak));

            // The emerged impulse must be near unity (delay line is lossless).
            const auto peakValue = std::abs (buffer.getSample (0, peak));
            expectWithinAbsoluteError (peakValue, 1.0f, 0.05f,
                                       "Impulse amplitude must be preserved through the delay line");
        }
    }

    // -----------------------------------------------------------------------
    // §21.2: mode change produces no NaN/Inf; after the crossfade settles the
    // delay matches the new mode.
    // -----------------------------------------------------------------------

    void testModeChangeNoNaNAndSettle()
    {
        beginTest ("LookaheadDelay: mode change produces no NaN/Inf and settles to new delay");

        LookaheadDelay delay;
        delay.setMode (LookaheadDelay::Mode::Off);
        delay.prepare (kSampleRate, 256, kChannels);
        delay.reset();

        const int transitionSamples = static_cast<int> (std::round (
            5.0 /*crossfade ms*/ * kSampleRate / 1000.0));

        // Drive with a steady tone through the transition.
        juce::AudioBuffer<float> tone (kChannels, transitionSamples * 2);
        for (int n = 0; n < tone.getNumSamples(); ++n)
        {
            const double t = static_cast<double> (n) / kSampleRate;
            const auto v = static_cast<float> (0.3 * std::sin (2.0 * 3.14159265358979323846 * 440.0 * t));
            for (int ch = 0; ch < kChannels; ++ch)
                tone.setSample (ch, n, v);
        }

        delay.setMode (LookaheadDelay::Mode::Milliseconds2);
        delay.process (tone);

        expect (allFinite (tone), "Output during mode-change crossfade must remain finite");

        // After the crossfade has settled, an impulse must emerge at the new
        // mode's delay index.
        const auto expLat = expectedLatency (LookaheadDelay::Mode::Milliseconds2);

        juce::AudioBuffer<float> probe (kChannels, expLat + 16);
        probe.clear();
        probe.setSample (0, 0, 1.0f);
        probe.setSample (1, 0, 1.0f);
        delay.process (probe);

        expect (allFinite (probe), "Post-settle impulse response must be finite");

        const int peak = peakIndex (probe);
        expect (std::abs (peak - expLat) <= 1,
                "After settle, impulse must emerge at the new mode delay; expected="
                + juce::String (expLat) + " peak=" + juce::String (peak));

        expectEquals (delay.getLatencySamples(), expLat,
                      "Reported latency must match the new mode after change");
    }

    // -----------------------------------------------------------------------
    // reset() clears residual content so a subsequent impulse is not polluted
    // by pre-reset audio.
    // -----------------------------------------------------------------------

    void testResetClearsResidual()
    {
        beginTest ("LookaheadDelay: reset() clears residual so a subsequent impulse is clean");

        LookaheadDelay delay;
        delay.setMode (LookaheadDelay::Mode::Milliseconds5);
        delay.prepare (kSampleRate, 256, kChannels);
        delay.reset();

        const auto expLat = expectedLatency (LookaheadDelay::Mode::Milliseconds5);

        // Fill the delay line with loud content.
        juce::AudioBuffer<float> loud (kChannels, expLat * 2);
        for (int n = 0; n < loud.getNumSamples(); ++n)
        {
            const double t = static_cast<double> (n) / kSampleRate;
            const auto v = static_cast<float> (0.9 * std::sin (2.0 * 3.14159265358979323846 * 220.0 * t));
            for (int ch = 0; ch < kChannels; ++ch)
                loud.setSample (ch, n, v);
        }
        delay.process (loud);

        // Reset, then feed a single impulse followed by silence.
        delay.reset();

        juce::AudioBuffer<float> impulse (kChannels, expLat + 8);
        impulse.clear();
        impulse.setSample (0, 0, 1.0f);
        impulse.setSample (1, 0, 1.0f);
        delay.process (impulse);

        expect (allFinite (impulse), "Post-reset impulse response must be finite");

        // Every sample strictly before the expected delay tap must be ~0:
        // the pre-reset content must not pollute the output.
        for (int ch = 0; ch < kChannels; ++ch)
        {
            for (int n = 0; n < expLat; ++n)
            {
                const auto v = std::abs (impulse.getSample (ch, n));
                expect (v < 1.0e-4f,
                        "Pre-delay sample " + juce::String (n)
                        + " must be ~0 after reset; got " + juce::String (v));
            }
        }

        const int peak = peakIndex (impulse);
        expect (std::abs (peak - expLat) <= 1,
                "Impulse must emerge at the expected delay after reset; expected="
                + juce::String (expLat) + " peak=" + juce::String (peak));
    }
};

// Static registration: discovered by juce::UnitTestRunner::runAllTests().
static LookaheadTests lookaheadTestsInstance;
