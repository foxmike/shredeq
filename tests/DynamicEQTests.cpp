// =============================================================================
// ShredEQ - Dynamic EQ Unit Tests
// =============================================================================
// Covers the public DSP surface backing the per-band dynamic EQ, per
// .abracapocus/SHREDEQ_SPEC.md §4.2–4.4 and the §21.3 checklist:
//   * EnvelopeFollower attack/release ballistics and dB mapping
//   * Soft-knee gain computation (below-knee, above-knee cap, mid-knee
//     continuity/monotonicity)
//   * TransientSustainSeparator burst vs steady behavior
//   * DynamicProcessor modes: staticEq unity, downward/compression gain
//     reduction, upward boost, transientOnly vs sustainOnly, sidechained
//     external tap
//   * Light ParametricBand smoke: dynamics-disabled still processes;
//     dynamics-enabled changes effective output vs static
//
// Uses juce::UnitTest only — no Catch2/Google Test. Only public APIs are
// exercised; no production DSP code is rewritten.
// =============================================================================

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>

#include "Source/DSP/EnvelopeFollower.h"
#include "Source/DSP/DynamicProcessor.h"
#include "Source/DSP/ParametricBand.h"

#include <algorithm>
#include <cmath>

namespace
{

using namespace ShredEQ::DSP;

constexpr double kSampleRate = 48000.0;

[[nodiscard]] double toDb (double linear)
{
    return 20.0 * std::log10 (std::max (linear, 1.0e-12));
}

// Runs `follower` for `numSamples` of a constant `magnitude` step and returns
// the resulting linear envelope level.
float runStep (EnvelopeFollower& follower, float magnitude, int numSamples)
{
    float last = 0.0f;
    for (int i = 0; i < numSamples; ++i)
        last = follower.processSample (magnitude);
    return last;
}

// Feeds `magnitude` for `numSamples` into `proc` with a default context whose
// primary/external signals are both set to `magnitude`. Returns the final
// gain-reduction dB (absolute smoothed gain).
float runProcessor (DynamicProcessor& proc, float primarySample,
                    float externalSidechain, int numSamples)
{
    DetectorContext ctx;
    ctx.left = primarySample;
    ctx.right = primarySample;
    ctx.mid = primarySample;
    ctx.side = 0.0f;
    ctx.externalSidechain = externalSidechain;
    ctx.input = primarySample;
    ctx.output = 0.0f;

    float gr = 0.0f;
    float lastMult = 1.0f;
    for (int i = 0; i < numSamples; ++i)
    {
        lastMult = proc.processSample (primarySample, ctx);
        gr = proc.currentGainReductionDb();
    }
    juce::ignoreUnused (lastMult);
    return gr;
}

} // namespace

// =============================================================================

class DynamicEQTests final : public juce::UnitTest
{
public:
    DynamicEQTests()
        : juce::UnitTest ("DynamicEQTests", "ShredEQ/DSP") {}

    void runTest() override
    {
        testEnvelopeFollowerAttackTiming();
        testEnvelopeFollowerReleaseDecay();
        testEnvelopeFollowerLevelDbTracking();
        testSoftKneeBelowThreshold();
        testSoftKneeAboveThresholdCap();
        testSoftKneeMidKneeContinuity();
        testTransientSustainBurst();
        testTransientSustainSteady();
        testDynamicProcessorStaticEqUnity();
        testDynamicProcessorDownwardGainReduction();
        testDynamicProcessorCompressionGainReduction();
        testDynamicProcessorUpwardBoost();
        testDynamicProcessorTransientVsSustainBurst();
        testDynamicProcessorTransientVsSustainSteady();
        testDynamicProcessorSidechainedExternalTap();
        testParametricBandDynamicsDisabledProcesses();
        testParametricBandDynamicsEnabledChangesOutput();
    }

    // -----------------------------------------------------------------------
    // EnvelopeFollower
    // -----------------------------------------------------------------------

    void testEnvelopeFollowerAttackTiming()
    {
        beginTest ("EnvelopeFollower: faster attack reaches target sooner");

        constexpr float target = 1.0f;
        // Intermediate window: long enough for the fast follower to mostly
        // converge but short of the slow follower's full settling.
        constexpr int window = 200;

        EnvelopeFollower fast;
        fast.prepare (kSampleRate);
        fast.setTimeConstants (1.0, 50.0); // 1 ms attack

        EnvelopeFollower slow;
        slow.prepare (kSampleRate);
        slow.setTimeConstants (100.0, 500.0); // 100 ms attack

        const auto fastLevel = runStep (fast, target, window);
        const auto slowLevel = runStep (slow, target, window);

        // Both must be finite and below the target (asymptotic approach).
        expect (std::isfinite (fastLevel) && fastLevel > 0.0f);
        expect (std::isfinite (slowLevel) && slowLevel > 0.0f);
        expect (fastLevel < target + 1.0e-6f);
        expect (slowLevel < target + 1.0e-6f);

        // Faster attack must be closer to the target at the intermediate
        // window, i.e. its level is higher (both approach from below).
        expect (fastLevel > slowLevel,
                "Fast attack should track ahead of slow attack; got fast="
                + juce::String (fastLevel) + " slow=" + juce::String (slowLevel));
        expect (std::abs (fastLevel - target) < std::abs (slowLevel - target),
                "Fast attack should be closer to target");
    }

    void testEnvelopeFollowerReleaseDecay()
    {
        beginTest ("EnvelopeFollower: release decays after input drops");

        EnvelopeFollower follower;
        follower.prepare (kSampleRate);
        follower.setTimeConstants (1.0, 50.0); // 50 ms release

        // Establish a steady envelope near unity.
        const float peak = runStep (follower, 1.0f, 2000);
        expectWithinAbsoluteError (peak, 1.0f, 0.05f);

        // Drop input to silence and let the release act.
        const float afterDecay = runStep (follower, 0.0f, 2000);

        expect (std::isfinite (afterDecay));
        expect (afterDecay < peak,
                "Envelope must decay after input drops; peak="
                + juce::String (peak) + " after=" + juce::String (afterDecay));
        expect (afterDecay < peak * 0.5f,
                "Envelope should decay meaningfully below peak after release window");
        expect (afterDecay >= 0.0f);
    }

    void testEnvelopeFollowerLevelDbTracking()
    {
        beginTest ("EnvelopeFollower: levelDb tracks 20*log10 of steady envelope");

        EnvelopeFollower follower;
        follower.prepare (kSampleRate);
        follower.setTimeConstants (1.0, 50.0);

        constexpr float magnitude = 0.5f;
        runStep (follower, magnitude, 5000);

        const auto expectedDb = static_cast<float> (toDb (magnitude));
        expectWithinAbsoluteError (follower.levelDb(), expectedDb, 0.1f,
                                   "Steady levelDb should match 20*log10(|x|)");
    }

    // -----------------------------------------------------------------------
    // Soft-knee gain computation
    // -----------------------------------------------------------------------

    void testSoftKneeBelowThreshold()
    {
        beginTest ("Soft-knee: far below threshold yields ~0 dB gain change");

        // Level 30 dB below a 0 dB threshold with a 6 dB knee: u = -30,
        // well below -K/2 = -3, so F_K = 0 and downward gain is ~0.
        const auto g = computeSoftKneeGainDb (-30.0, 0.0, 4.0, 6.0, 12.0, false);
        expectWithinAbsoluteError (g, 0.0, 1.0e-9,
                                   "Below-knee downward gain must be ~0 dB");
    }

    void testSoftKneeAboveThresholdCap()
    {
        beginTest ("Soft-knee: deep above threshold approaches (1-1/R)*u capped by D_max");

        constexpr double threshold = -20.0;
        constexpr double level = 20.0; // u = 40 dB, deep above K/2 for K=6
        constexpr double ratio = 4.0;
        constexpr double knee = 6.0;
        constexpr double range = 12.0;

        const auto gDown = computeSoftKneeGainDb (level, threshold, ratio, knee, range, false);
        // Uncapped would be -(1 - 1/4) * 40 = -30 dB; capped to -D_max = -12.
        expectWithinAbsoluteError (gDown, -range, 1.0e-9,
                                   "Downward gain must be capped at -D_max");

        const auto gUp = computeSoftKneeGainDb (level, threshold, ratio, knee, range, true);
        expectWithinAbsoluteError (gUp, +range, 1.0e-9,
                                   "Upward gain must be capped at +D_max");

        // Just-above-knee (still hard-knee region, u = K/2 exactly): F_K = u,
        // action = (1 - 1/R) * u, no cap.
        const double u = knee * 0.5; // = 3
        const auto gHard = computeSoftKneeGainDb (threshold + u, threshold, ratio, knee, range, false);
        expectWithinAbsoluteError (gHard, -((1.0 - 1.0 / ratio) * u), 1.0e-9,
                                   "Hard-knee region gain must equal -(1-1/R)*u");
    }

    void testSoftKneeMidKneeContinuity()
    {
        beginTest ("Soft-knee: mid-knee region is continuous and monotonic");

        constexpr double knee = 6.0;
        constexpr double halfKnee = knee * 0.5;

        // Boundary continuity: at u = -K/2 the curve must be 0 (matches the
        // below-knee piece); at u = +K/2 it must equal u (matches the
        // above-knee piece).
        expectWithinAbsoluteError (computeSoftKneeCurveDb (-halfKnee, knee), 0.0, 1.0e-9);
        expectWithinAbsoluteError (computeSoftKneeCurveDb (+halfKnee, knee), halfKnee, 1.0e-9);

        // Interior point: F_K(0) = (0 + K/2)^2 / (2K) = K/8.
        expectWithinAbsoluteError (computeSoftKneeCurveDb (0.0, knee), knee / 8.0, 1.0e-9);

        // Monotonic non-deasing sweep across the knee and beyond.
        double prev = -1.0;
        for (double u = -halfKnee - 1.0; u <= halfKnee + 1.0; u += 0.1)
        {
            const auto cur = computeSoftKneeCurveDb (u, knee);
            expect (cur >= prev - 1.0e-9,
                    "Soft-knee curve must be monotonic non-decreasing at u="
                    + juce::String (u));
            prev = cur;
        }

        // Continuity of the gain function across the knee boundaries for a
        // fixed configuration (no cap engaged).
        constexpr double threshold = 0.0;
        constexpr double ratio = 3.0;
        constexpr double range = 100.0; // large so cap does not engage
        const auto gBelow = computeSoftKneeGainDb (threshold - halfKnee, threshold, ratio, knee, range, false);
        const auto gAbove = computeSoftKneeGainDb (threshold + halfKnee, threshold, ratio, knee, range, false);
        expectWithinAbsoluteError (gBelow, 0.0, 1.0e-9);
        expectWithinAbsoluteError (gAbove, -((1.0 - 1.0 / ratio) * halfKnee), 1.0e-9);

        // Spot-check the interior maps to the quadratic action.
        const auto gMid = computeSoftKneeGainDb (threshold, threshold, ratio, knee, range, false);
        expectWithinAbsoluteError (gMid, -((1.0 - 1.0 / ratio) * (knee / 8.0)), 1.0e-9);
    }

    // -----------------------------------------------------------------------
    // TransientSustainSeparator
    // -----------------------------------------------------------------------

    void testTransientSustainBurst()
    {
        beginTest ("Transient/sustain: impulse burst yields transient > sustain initially");

        TransientSustainSeparator sep;
        sep.prepare (kSampleRate);
        sep.setFastTimeConstants (1.0, 50.0);
        sep.setSlowTimeConstants (5.0, 500.0);
        sep.setK (1.0);
        sep.setLambda (0.0);

        // Short impulse-like burst: 30 samples of unity, then silence.
        float transientPeak = 0.0f;
        float sustainAtPeak = 0.0f;
        for (int i = 0; i < 30; ++i)
        {
            auto levels = sep.processSample (1.0f);
            if (levels.transient > transientPeak)
            {
                transientPeak = levels.transient;
                sustainAtPeak = levels.sustain;
            }
        }

        expect (transientPeak > sustainAtPeak,
                "Burst must produce transient > sustain initially; got t="
                + juce::String (transientPeak) + " s=" + juce::String (sustainAtPeak));
        expect (transientPeak > 0.05f,
                "Transient component must be non-negligible on a burst");
    }

    void testTransientSustainSteady()
    {
        beginTest ("Transient/sustain: long steady tone yields sustain dominance, transient near 0");

        TransientSustainSeparator sep;
        sep.prepare (kSampleRate);
        sep.setFastTimeConstants (1.0, 50.0);
        sep.setSlowTimeConstants (5.0, 500.0);
        sep.setK (1.0);
        sep.setLambda (0.0);

        // Long steady signal: both followers converge to the magnitude, so
        // transient = max(0, fast - k*slow) -> 0 and sustain -> magnitude.
        for (int i = 0; i < 15000; ++i)
            (void) sep.processSample (0.5f);

        const auto t = sep.transientLevel();
        const auto s = sep.sustainLevel();
        expect (t < 0.01f,
                "Transient must be near 0 after settling; got " + juce::String (t));
        expect (s > 0.4f,
                "Sustain must dominate after settling; got " + juce::String (s));
    }

    // -----------------------------------------------------------------------
    // DynamicProcessor
    // -----------------------------------------------------------------------

    void testDynamicProcessorStaticEqUnity()
    {
        beginTest ("DynamicProcessor: staticEq mode yields unity modulation");

        DynamicProcessor proc;
        proc.prepare (kSampleRate);
        proc.setMode (DynamicMode::staticEq);
        proc.setStaticGainDb (6.0);

        DetectorContext ctx;
        ctx.input = 0.5f;
        for (int i = 0; i < 100; ++i)
        {
            const auto m = proc.processSample (0.5f, ctx);
            expectWithinAbsoluteError (m, 1.0f, 1.0e-9f,
                                       "staticEq must produce unity multiplier");
        }
        expectWithinAbsoluteError (proc.currentGainReductionDb(), 0.0f, 1.0e-6f,
                                   "staticEq must produce no gain reduction");
    }

    void testDynamicProcessorDownwardGainReduction()
    {
        beginTest ("DynamicProcessor: downwardDynamic above threshold reduces gain within range cap");

        DynamicProcessor proc;
        proc.prepare (kSampleRate);
        proc.setMode (DynamicMode::downwardDynamic);
        proc.setThresholdDb (-20.0);
        proc.setRatio (4.0);
        proc.setKneeDb (0.0);
        proc.setRangeDb (12.0);
        proc.setStaticGainDb (6.0);
        proc.setAttackMs (1.0);
        proc.setReleaseMs (50.0);

        // Steady 0 dBFS input → level 0 dB, well above -20 dB threshold.
        const auto gr = runProcessor (proc, 1.0f, 0.0f, 5000);

        expect (gr > 1.0f,
                "Downward must produce meaningful GR above threshold; got " + juce::String (gr));
        expect (gr <= 12.0f + 0.01f,
                "GR must be capped by D_max; got " + juce::String (gr));
    }

    void testDynamicProcessorCompressionGainReduction()
    {
        beginTest ("DynamicProcessor: compression above threshold reduces gain (GR > 0)");

        DynamicProcessor proc;
        proc.prepare (kSampleRate);
        proc.setMode (DynamicMode::compression);
        proc.setThresholdDb (-20.0);
        proc.setRatio (4.0);
        proc.setKneeDb (0.0);
        proc.setRangeDb (12.0);
        proc.setStaticGainDb (6.0); // positive static gain → downward action
        proc.setAttackMs (1.0);
        proc.setReleaseMs (50.0);

        const auto grPos = runProcessor (proc, 1.0f, 0.0f, 5000);
        expect (grPos > 1.0f,
                "Compression with positive static gain must reduce gain; got " + juce::String (grPos));
        expect (grPos <= 12.0f + 0.01f);

        // Negative static gain: compression moves the cut toward 0 dB (upward
        // action), still reporting non-zero |g_target| as GR.
        proc.setStaticGainDb (-6.0);
        const auto grNeg = runProcessor (proc, 1.0f, 0.0f, 5000);
        expect (grNeg > 1.0f,
                "Compression with negative static gain must still act; got " + juce::String (grNeg));
    }

    void testDynamicProcessorUpwardBoost()
    {
        beginTest ("DynamicProcessor: upward mode increases modulation when configured");

        DynamicProcessor proc;
        proc.prepare (kSampleRate);
        proc.setMode (DynamicMode::upwardDynamic);
        proc.setThresholdDb (-20.0);
        proc.setRatio (4.0);
        proc.setKneeDb (0.0);
        proc.setRangeDb (12.0);
        proc.setStaticGainDb (6.0); // positive boost → upward increases it
        proc.setAttackMs (1.0);
        proc.setReleaseMs (50.0);

        DetectorContext ctx;
        ctx.input = 1.0f;
        float lastMult = 1.0f;
        for (int i = 0; i < 5000; ++i)
            lastMult = proc.processSample (1.0f, ctx);

        // multiplier = 1 + g_target / staticGainDb, with g_target > 0 → > 1.
        expect (lastMult > 1.0f + 1.0e-3f,
                "Upward mode with positive static gain must increase multiplier; got "
                + juce::String (lastMult));
        expect (proc.currentGainReductionDb() > 1.0f,
                "Upward mode must report non-zero gain action");
    }

    void testDynamicProcessorTransientVsSustainBurst()
    {
        beginTest ("DynamicProcessor: transientOnly vs sustainOnly on burst");

        // Short burst: transient detector fires; sustain detector barely
        // rises above the chosen threshold.
        auto runBurst = [] (DynamicMode mode)
        {
            DynamicProcessor proc;
            proc.prepare (kSampleRate);
            proc.setMode (mode);
            proc.setThresholdDb (-15.0);
            proc.setRatio (4.0);
            proc.setKneeDb (0.0);
            proc.setRangeDb (12.0);
            proc.setStaticGainDb (6.0);
            proc.setAttackMs (1.0);
            proc.setReleaseMs (50.0);

            DetectorContext ctx;
            ctx.input = 1.0f;
            float maxGr = 0.0f;
            for (int i = 0; i < 30; ++i)
            {
                proc.processSample (1.0f, ctx);
                maxGr = std::max (maxGr, proc.currentGainReductionDb());
            }
            // Brief release tail to let the smoother peak.
            for (int i = 0; i < 80; ++i)
            {
                proc.processSample (0.0f, ctx);
                maxGr = std::max (maxGr, proc.currentGainReductionDb());
            }
            return maxGr;
        };

        const auto grTransient = runBurst (DynamicMode::transientOnly);
        const auto grSustain = runBurst (DynamicMode::sustainOnly);

        expect (grTransient > 0.5f,
                "transientOnly must react to a burst; got " + juce::String (grTransient));
        expect (grSustain < 0.1f,
                "sustainOnly must stay quiet on a short burst; got " + juce::String (grSustain));
    }

    void testDynamicProcessorTransientVsSustainSteady()
    {
        beginTest ("DynamicProcessor: transientOnly vs sustainOnly on steady signal");

        auto runSteady = [] (DynamicMode mode)
        {
            DynamicProcessor proc;
            proc.prepare (kSampleRate);
            proc.setMode (mode);
            proc.setThresholdDb (-15.0);
            proc.setRatio (4.0);
            proc.setKneeDb (0.0);
            proc.setRangeDb (12.0);
            proc.setStaticGainDb (6.0);
            proc.setAttackMs (1.0);
            proc.setReleaseMs (50.0);

            DetectorContext ctx;
            ctx.input = 1.0f;
            for (int i = 0; i < 15000; ++i)
                proc.processSample (1.0f, ctx);
            return proc.currentGainReductionDb();
        };

        const auto grTransient = runSteady (DynamicMode::transientOnly);
        const auto grSustain = runSteady (DynamicMode::sustainOnly);

        expect (grTransient < 0.1f,
                "transientOnly must be inactive on steady signal; got " + juce::String (grTransient));
        expect (grSustain > 1.0f,
                "sustainOnly must react to steady signal; got " + juce::String (grSustain));
    }

    void testDynamicProcessorSidechainedExternalTap()
    {
        beginTest ("DynamicProcessor: sidechained mode uses external tap when provided");

        DynamicProcessor proc;
        proc.prepare (kSampleRate);
        proc.setMode (DynamicMode::sidechained);
        proc.setThresholdDb (-20.0);
        proc.setRatio (4.0);
        proc.setKneeDb (0.0);
        proc.setRangeDb (12.0);
        proc.setStaticGainDb (6.0);
        proc.setAttackMs (1.0);
        proc.setReleaseMs (50.0);

        // Primary silent, external sidechain loud → detector must follow the
        // external tap and produce gain reduction.
        const auto grWithSc = runProcessor (proc, 0.0f, 1.0f, 5000);
        expect (grWithSc > 1.0f,
                "Sidechained must react to external tap; got " + juce::String (grWithSc));

        // Sidechain reaction flag must be set.
        expect ((proc.getVisualizationData().reactionFlags & 0x4u) != 0u,
                "Sidechain reaction flag must be set in sidechained mode");

        // Both primary and sidechain silent → no gain reduction. Reset
        // first so no envelope/smoother state lingers from the loud run.
        proc.reset();
        const auto grSilent = runProcessor (proc, 0.0f, 0.0f, 5000);
        expect (grSilent < 0.1f,
                "Sidechained must be silent when external tap is silent; got " + juce::String (grSilent));

        expect (grWithSc > grSilent,
                "External tap must produce more GR than silent tap");
    }

    // -----------------------------------------------------------------------
    // Optional ParametricBand smoke
    // -----------------------------------------------------------------------

    void testParametricBandDynamicsDisabledProcesses()
    {
        beginTest ("ParametricBand smoke: dynamics disabled still processes audio");

        ParametricBand band;
        band.prepare (kSampleRate, 256);
        band.reset();

        band.setFilterType (BandFilterType::bell);
        band.setFrequency (1000.0);
        band.setGainDb (6.0);
        band.setQ (1.0);
        band.setRoutingMode (StereoRoutingMode::linked);
        band.setUseTpt (false);
        band.setDynamicsEnabled (false);
        band.setEnabled (true);

        // Drive a steady tone at the bell center frequency.
        juce::AudioBuffer<float> buffer (2, 4096);
        buffer.clear();
        for (int n = 0; n < buffer.getNumSamples(); ++n)
        {
            const float v = 0.5f * std::sin (2.0f * 3.14159265358979f * 1000.0f
                                             * static_cast<float> (n) / static_cast<float> (kSampleRate));
            buffer.setSample (0, n, v);
            buffer.setSample (1, n, v);
        }

        band.process (buffer, false);

        float maxAbs = 0.0f;
        for (int ch = 0; ch < 2; ++ch)
            for (int n = 0; n < buffer.getNumSamples(); ++n)
            {
                const auto v = buffer.getSample (ch, n);
                expect (std::isfinite (v), "Non-finite sample with dynamics disabled");
                maxAbs = std::max (maxAbs, std::abs (v));
            }
        // A 6 dB bell at center frequency must produce visible gain (> input
        // peak of 0.5). This confirms the static path processed the audio.
        expect (maxAbs > 0.5f + 1.0e-3f,
                "Static bell must apply gain at center frequency; maxAbs=" + juce::String (maxAbs));
    }

    void testParametricBandDynamicsEnabledChangesOutput()
    {
        beginTest ("ParametricBand smoke: dynamics enabled changes effective output vs static");

        auto render = [] (bool dynamicsEnabled)
        {
            ParametricBand band;
            band.prepare (kSampleRate, 256);
            band.reset();

            band.setFilterType (BandFilterType::bell);
            band.setFrequency (1000.0);
            band.setGainDb (12.0);
            band.setQ (1.0);
            band.setRoutingMode (StereoRoutingMode::linked);
            band.setUseTpt (false);
            band.setDetectorSource (static_cast<int> (DetectorSource::fullBandInternal));
            band.setDynamicMode (static_cast<int> (DynamicMode::downwardDynamic));
            band.setThresholdDb (-20.0);
            band.setRatio (4.0);
            band.setKneeDb (0.0);
            band.setDynamicRangeDb (12.0);
            band.setAttackMs (1.0);
            band.setReleaseMs (50.0);
            band.setDynamicsEnabled (dynamicsEnabled);
            band.setEnabled (true);

            juce::AudioBuffer<float> buffer (2, 8192);
            buffer.clear();
            for (int n = 0; n < buffer.getNumSamples(); ++n)
            {
                // Loud steady tone at the bell center: well above threshold.
                const float v = 0.9f * std::sin (2.0f * 3.14159265358979f * 1000.0f
                                                 * static_cast<float> (n) / static_cast<float> (kSampleRate));
                buffer.setSample (0, n, v);
                buffer.setSample (1, n, v);
            }

            band.process (buffer, false);

            // RMS of the tail (post-settling) of channel 0.
            double sumSq = 0.0;
            const int start = buffer.getNumSamples() / 2;
            const int count = buffer.getNumSamples() - start;
            for (int n = start; n < buffer.getNumSamples(); ++n)
                sumSq += static_cast<double> (buffer.getSample (0, n)) * buffer.getSample (0, n);
            return std::sqrt (sumSq / static_cast<double> (count));
        };

        const auto rmsStatic = render (false);
        const auto rmsDynamic = render (true);

        expect (std::isfinite (rmsStatic) && rmsStatic > 0.0);
        expect (std::isfinite (rmsDynamic) && rmsDynamic > 0.0);

        // With a loud input above threshold, downward dynamics must attenuate
        // the 12 dB boost, so the dynamic output must be markedly lower than
        // the static output.
        expect (rmsDynamic < rmsStatic * 0.75,
                "Dynamics-enabled output must differ from static; static=" + juce::String (rmsStatic)
                + " dynamic=" + juce::String (rmsDynamic));
    }
};

// Static registration: discovered by juce::UnitTestRunner::runAllTests().
static DynamicEQTests dynamicEQTestsInstance;
