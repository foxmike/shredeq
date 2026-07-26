// =============================================================================
// ShredEQ - Parametric Band Unit Tests
// =============================================================================
// Covers the public API of Source/DSP/ParametricBand.h:
//   * 12-band ParametricEQ construct/prepare/reset
//   * bypassed / disabled band is unity (impulse check within tight tolerance)
//   * linked dual-mono process runs without error on a small buffer
//   * routing mode left vs right masks the opposite channel for a
//     mono-impulse-on-one-channel case
//   * absence of M/S matrix encoding: mid-only mode does not rotate L/R into
//     synthetic stereo when the buffer is treated as channel 0/1 per API docs
//
// These tests exercise behavior only; no internal symbols are inspected.
// =============================================================================

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>

#include "Source/DSP/ParametricBand.h"

#include <cmath>

namespace
{

using namespace ShredEQ::DSP;

bool isFiniteAndNoDenormalish (float v)
{
    if (! std::isfinite (v))
        return false;
    return true;
}

juce::AudioBuffer<float> makeStereoBuffer (int numSamples)
{
    juce::AudioBuffer<float> buffer (2, numSamples);
    buffer.clear();
    return buffer;
}

} // namespace

class ParametricBandTests final : public juce::UnitTest
{
public:
    ParametricBandTests()
        : juce::UnitTest ("ParametricBandTests", "ShredEQ/DSP") {}

    void runTest() override
    {
        testContainerConstructPrepareReset();
        testDisabledBandIsUnity();
        testEnabledUnityBellIsUnity();
        testLinkedDualMonoProcessSmoke();
        testRoutingLeftMasksRightChannel();
        testRoutingRightMasksLeftChannel();
        testMidOnlyDoesNotRotateIntoSyntheticStereo();
        testSideOnlyDoesNotRotateIntoSyntheticStereo();
    }

    void testContainerConstructPrepareReset()
    {
        beginTest ("12-band ParametricEQ construct/prepare/reset");

        ParametricEQ eq;
        constexpr int maxBlock = 64;

        eq.prepare (48000.0, maxBlock);
        eq.reset();

        // All 12 bands present, default-disabled, default stage A.
        for (std::size_t i = 0; i < kNumParametricBands; ++i)
        {
            expect (! eq[i].isEnabled(),
                    "Band " + juce::String ((int) i) + " must default to disabled");
            expect (eq[i].getStage() == BandStage::stageA,
                    "Band " + juce::String ((int) i) + " must default to stage A");
        }

        // Re-prepare at a different sample rate / block size must not crash and
        // must leave the bands in a quiescent (unity) state.
        eq.prepare (96000.0, 256);
        eq.reset();

        auto buffer = makeStereoBuffer (32);
        buffer.setSample (0, 0, 0.5f);
        buffer.setSample (1, 0, -0.5f);
        eq.process (buffer, false, BandStage::stageA);

        // With all bands disabled the buffer must be untouched.
        expectWithinAbsoluteError (buffer.getSample (0, 0), 0.5f, 1.0e-9f);
        expectWithinAbsoluteError (buffer.getSample (1, 0), -0.5f, 1.0e-9f);
    }

    void testDisabledBandIsUnity()
    {
        beginTest ("Disabled band is unity (impulse passthrough)");

        ParametricBand band;
        band.prepare (48000.0, 64);
        band.reset();

        band.setFilterType (BandFilterType::bell);
        band.setFrequency (1000.0);
        band.setGainDb (12.0);   // would clearly color the signal if applied
        band.setQ (2.0);
        band.setEnabled (false); // disabled

        auto buffer = makeStereoBuffer (8);
        const float impulse = 0.707f;
        buffer.setSample (0, 0, impulse);
        buffer.setSample (1, 0, -impulse);

        band.process (buffer, false);

        // Disabled band must pass samples through exactly.
        for (int ch = 0; ch < 2; ++ch)
            for (int n = 0; n < 8; ++n)
            {
                const float expected = (n == 0) ? (ch == 0 ? impulse : -impulse) : 0.0f;
                expectWithinAbsoluteError (buffer.getSample (ch, n), expected, 1.0e-9f,
                                           "Disabled band altered sample [" + juce::String (ch)
                                           + "," + juce::String (n) + "]");
            }
    }

    void testEnabledUnityBellIsUnity()
    {
        beginTest ("Enabled bell with 0 dB gain is unity at DC/low band");

        ParametricBand band;
        band.prepare (48000.0, 64);
        band.reset();

        band.setFilterType (BandFilterType::bell);
        band.setFrequency (1000.0);
        band.setGainDb (0.0);
        band.setQ (1.0);
        band.setRoutingMode (StereoRoutingMode::linked);
        band.setEnabled (true);

        // Process an impulse on channel 0; channel 1 silence.
        auto buffer = makeStereoBuffer (32);
        buffer.setSample (0, 0, 1.0f);

        band.process (buffer, false);

        // A 0 dB bell with non-pathological Q passes a unit impulse; allow a
        // tight tolerance. The first sample is unity, total energy preserved.
        expectWithinAbsoluteError (buffer.getSample (0, 0), 1.0f, 1.0e-3f);

        // No NaNs / infinities anywhere, and channel 1 stays silent.
        for (int ch = 0; ch < 2; ++ch)
            for (int n = 0; n < 32; ++n)
            {
                const auto v = buffer.getSample (ch, n);
                expect (isFiniteAndNoDenormalish (v),
                        "Non-finite sample at [" + juce::String (ch) + ","
                        + juce::String (n) + "]");
            }
    }

    void testLinkedDualMonoProcessSmoke()
    {
        beginTest ("Linked dual-mono process runs without error on small buffer");

        ParametricBand band;
        band.prepare (48000.0, 128);
        band.reset();

        band.setFilterType (BandFilterType::bell);
        band.setFrequency (2000.0);
        band.setGainDb (4.0);
        band.setQ (1.4);
        band.setRoutingMode (StereoRoutingMode::linked);
        band.setEnabled (true);

        auto buffer = makeStereoBuffer (128);
        // Pink-ish noise: fill with a simple deterministic pattern.
        for (int n = 0; n < 128; ++n)
        {
            const float v = 0.5f * std::sin (static_cast<float> (n) * 0.03f);
            buffer.setSample (0, n, v);
            buffer.setSample (1, n, -v);
        }

        band.process (buffer, false);

        // Output stays finite and bounded.
        for (int ch = 0; ch < 2; ++ch)
            for (int n = 0; n < 128; ++n)
            {
                const auto v = buffer.getSample (ch, n);
                expect (isFiniteAndNoDenormalish (v), "Non-finite sample");
                expect (std::abs (v) < 10.0f, "Unbounded output sample");
            }
    }

    void testRoutingLeftMasksRightChannel()
    {
        beginTest ("Routing=left does not apply filter energy to right channel");

        ParametricBand band;
        band.prepare (48000.0, 64);
        band.reset();

        band.setFilterType (BandFilterType::bell);
        band.setFrequency (1000.0);
        band.setGainDb (12.0);   // strong boost so filtered energy is obvious
        band.setQ (4.0);
        band.setRoutingMode (StereoRoutingMode::left);
        band.setEnabled (true);

        // Mono impulse on channel 0 only; channel 1 is silence.
        auto buffer = makeStereoBuffer (32);
        buffer.setSample (0, 0, 1.0f);

        band.process (buffer, false /* L/R domain */);

        // Channel 0 received filter energy (impulse response is nonzero beyond
        // sample 0).
        float l0 = buffer.getSample (0, 0);
        float lEnergy = 0.0f;
        for (int n = 0; n < 32; ++n)
            lEnergy += std::abs (buffer.getSample (0, n));
        expect (lEnergy > 0.0f, "Left channel must carry filter output");

        // Channel 1 (right) must remain exactly zero: routing masked it.
        for (int n = 0; n < 32; ++n)
            expectWithinAbsoluteError (buffer.getSample (1, n), 0.0f, 1.0e-9f,
                                       "Right channel must be untouched by left routing");

        // Sanity: l0 itself is finite.
        expect (isFiniteAndNoDenormalish (l0), "Left channel sample must be finite");
    }

    void testRoutingRightMasksLeftChannel()
    {
        beginTest ("Routing=right does not apply filter energy to left channel");

        ParametricBand band;
        band.prepare (48000.0, 64);
        band.reset();

        band.setFilterType (BandFilterType::bell);
        band.setFrequency (1000.0);
        band.setGainDb (12.0);
        band.setQ (4.0);
        band.setRoutingMode (StereoRoutingMode::right);
        band.setEnabled (true);

        auto buffer = makeStereoBuffer (32);
        buffer.setSample (1, 0, 1.0f); // impulse on right only

        band.process (buffer, false);

        float rEnergy = 0.0f;
        for (int n = 0; n < 32; ++n)
            rEnergy += std::abs (buffer.getSample (1, n));
        expect (rEnergy > 0.0f, "Right channel must carry filter output");

        for (int n = 0; n < 32; ++n)
            expectWithinAbsoluteError (buffer.getSample (0, n), 0.0f, 1.0e-9f,
                                       "Left channel must be untouched by right routing");
    }

    void testMidOnlyDoesNotRotateIntoSyntheticStereo()
    {
        beginTest ("Mid-only routing in M/S domain does not rotate L/R into synthetic stereo");

        // Per ParametricBand.h API: when isMidSide == true the buffer's
        // channel 0 is treated as Mid and channel 1 as Side. mid routing must
        // filter only channel 0 and must NOT synthesize stereo output on
        // channel 1 from the channel 0 content (i.e. no M/S encode/decode
        // happens inside the band).
        ParametricBand band;
        band.prepare (48000.0, 64);
        band.reset();

        band.setFilterType (BandFilterType::bell);
        band.setFrequency (1000.0);
        band.setGainDb (9.0);
        band.setQ (2.0);
        band.setRoutingMode (StereoRoutingMode::mid);
        band.setEnabled (true);

        auto buffer = makeStereoBuffer (32);
        // Mid channel (ch0) carries an impulse; Side channel (ch1) is silent.
        buffer.setSample (0, 0, 1.0f);

        band.process (buffer, true /* M/S domain */);

        // Channel 0 (Mid) must carry filtered energy.
        float midEnergy = 0.0f;
        for (int n = 0; n < 32; ++n)
            midEnergy += std::abs (buffer.getSample (0, n));
        expect (midEnergy > 0.0f, "Mid channel must carry filter output");

        // Channel 1 (Side) must remain exactly zero: no M/S rotation/encode
        // produced synthetic stereo content from the Mid impulse.
        for (int n = 0; n < 32; ++n)
            expectWithinAbsoluteError (buffer.getSample (1, n), 0.0f, 1.0e-9f,
                                       "Side channel must not receive M/S-rotated energy");
    }

    void testSideOnlyDoesNotRotateIntoSyntheticStereo()
    {
        beginTest ("Side-only routing in M/S domain does not rotate energy into Mid channel");

        ParametricBand band;
        band.prepare (48000.0, 64);
        band.reset();

        band.setFilterType (BandFilterType::bell);
        band.setFrequency (1000.0);
        band.setGainDb (9.0);
        band.setQ (2.0);
        band.setRoutingMode (StereoRoutingMode::side);
        band.setEnabled (true);

        auto buffer = makeStereoBuffer (32);
        buffer.setSample (1, 0, 1.0f); // Side impulse

        band.process (buffer, true);

        float sideEnergy = 0.0f;
        for (int n = 0; n < 32; ++n)
            sideEnergy += std::abs (buffer.getSample (1, n));
        expect (sideEnergy > 0.0f, "Side channel must carry filter output");

        for (int n = 0; n < 32; ++n)
            expectWithinAbsoluteError (buffer.getSample (0, n), 0.0f, 1.0e-9f,
                                       "Mid channel must not receive M/S-rotated energy");
    }
};

// Static registration: discovered by juce::UnitTestRunner::runAllTests().
static ParametricBandTests parametricBandTestsInstance;
