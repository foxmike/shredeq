// =============================================================================
// ShredEQ - Multiband Crossover / Engine Reconstruction Unit Tests
// =============================================================================
// Covers the public API of Source/DSP/MultibandCrossover and MultibandEngine
// per .abracapocus/SHREDEQ_SPEC.md §5.3 (crossover defaults/ranges) and §21.4
// (multiband reconstruction):
//   * Reconstruction: unity zone gains + dynamics bypassed => flat magnitude
//     vs dry within +/-0.5 dB (NOT sample-identical; LR4 phase is
//     frequency-dependent).
//   * Enabling the engine at unity settings produces no large level jump vs
//     bypass (RMS within tolerance).
//   * Crossover motion: sweeping crossover frequencies yields finite, NaN/Inf
//     free output with bounded peak transients vs a static-crossover control.
//   * Ordering protection: out-of-order targets resolve to f1 < f2 < f3.
//   * Default crossover centers approximate 140 / 900 / 4500 Hz after prepare.
//   * Solo/mute: soloing one zone suppresses others; mute silences that zone.
//   * reset() clears filter memory (finite impulse output; silence after reset).
//
// Uses juce::UnitTest only. Tolerance-based float compares with clear messages.
// =============================================================================

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>

#include "Source/DSP/MultibandCrossover.h"
#include "Source/DSP/MultibandEngine.h"
#include "Source/DSP/MultibandZone.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstddef>

namespace
{

using namespace ShredEQ::DSP;

constexpr double pi = 3.14159265358979323846264338327950288;

constexpr double sampleRate = 48000.0;

// A block length that is large enough for steady-state measurement and a
// multiple of the bin spacing used by the probe frequencies below.
constexpr int probeLength = 24000;   // 0.5 s at 48 kHz -> 2 Hz DFT bin spacing

// Probe frequencies: all integer multiples of (sampleRate / probeLength) = 2 Hz
// so the direct DFT lands on a bin with no spectral leakage. Chosen to sit in
// each of the four zones and away from the default crossover centers.
constexpr std::array<double, 5> probeHz { 100.0, 300.0, 1000.0, 3000.0, 8000.0 };

// Direct (Goertzel-free) single-bin DFT magnitude in dB. The probe frequencies
// are exact integer bins of probeLength at sampleRate, so no window is needed.
double binMagnitudeDb (const float* data, int numSamples,
                       double frequencyHz, double sampleRateHz)
{
    const double w = 2.0 * pi * frequencyHz / sampleRateHz;
    std::complex<double> acc { 0.0, 0.0 };
    for (int n = 0; n < numSamples; ++n)
        acc += std::complex<double> { static_cast<double> (data[n]), 0.0 }
               * std::polar (1.0, -w * static_cast<double> (n));
    const double mag = std::abs (acc) / static_cast<double> (numSamples);
    return 20.0 * std::log10 (std::max (mag, 1.0e-300));
}

double rmsDb (const float* data, int numSamples)
{
    double sum = 0.0;
    for (int n = 0; n < numSamples; ++n)
        sum += static_cast<double> (data[n]) * static_cast<double> (data[n]);
    const double rms = std::sqrt (sum / static_cast<double> (numSamples));
    return 20.0 * std::log10 (std::max (rms, 1.0e-9));
}

double maxAbs (const float* data, int numSamples)
{
    double m = 0.0;
    for (int n = 0; n < numSamples; ++n)
        m = std::max (m, std::abs (static_cast<double> (data[n])));
    return m;
}

double maxSampleDelta (const float* data, int numSamples)
{
    double m = 0.0;
    for (int n = 1; n < numSamples; ++n)
        m = std::max (m, std::abs (static_cast<double> (data[n])
                                   - static_cast<double> (data[n - 1])));
    return m;
}

bool allFinite (const float* data, int numSamples)
{
    for (int n = 0; n < numSamples; ++n)
        if (! std::isfinite (static_cast<double> (data[n])))
            return false;
    return true;
}

// Builds a mono multi-sine probe at the configured probe frequencies.
juce::AudioBuffer<float> makeMultiSineProbe (int numSamples, double amplitude)
{
    juce::AudioBuffer<float> buffer (1, numSamples);
    buffer.clear();
    auto* d = buffer.getWritePointer (0);
    for (int n = 0; n < numSamples; ++n)
    {
        double s = 0.0;
        for (const auto f : probeHz)
            s += amplitude * std::sin (2.0 * pi * f * static_cast<double> (n) / sampleRate);
        d[n] = static_cast<float> (s);
    }
    return buffer;
}

juce::AudioBuffer<float> makeDeterministicNoise (int numSamples, unsigned int seed)
{
    juce::AudioBuffer<float> buffer (1, numSamples);
    buffer.clear();
    auto* d = buffer.getWritePointer (0);
    unsigned int state = seed ? seed : 0x12345678u;
    for (int n = 0; n < numSamples; ++n)
    {
        // xorshift32 -> uniform [0,1) -> map to [-1,1)
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        const double u = static_cast<double> (state) / 4294967296.0;
        d[n] = static_cast<float> (2.0 * u - 1.0);
    }
    return buffer;
}

// Configures an engine for "transparent unity" operation: enabled, all zone
// gains at 0 dB, dynamics bypassed so only static unity gain is applied.
void configureUnityEngine (MultibandEngine& engine)
{
    engine.setEnabled (true);
    for (std::size_t i = 0; i < 4; ++i)
    {
        engine.setZoneGainDb (i, 0.0);
        engine.setZoneBypass (i, true);
        engine.setZoneSolo (i, false);
        engine.setZoneMute (i, false);
    }
}

} // namespace

class MultibandTests final : public juce::UnitTest
{
public:
    MultibandTests()
        : juce::UnitTest ("MultibandTests", "ShredEQ/DSP") {}

    void runTest() override
    {
        testDefaultCrossoverCenters();
        testReconstructionFlatMagnitude();
        testEnableLevelJump();
        testCrossoverMotionFiniteAndClickBounded();
        testOrderingProtection();
        testSoloSuppressesOtherZones();
        testMuteSilencesZone();
        testResetClearsFilterMemory();
    }

    void testDefaultCrossoverCenters()
    {
        beginTest ("Default crossover centers approximate 140 / 900 / 4500 Hz after prepare");

        MultibandEngine engine;
        engine.prepare (sampleRate, probeLength);
        engine.reset();

        // Effective frequencies after prepare/reset must track the spec
        // defaults (§5.3) to within a tight tolerance.
        expectWithinAbsoluteError (engine.getEffectiveCrossoverHz (0), 140.0, 1.0,
                                   "Weight/Body default crossover");
        expectWithinAbsoluteError (engine.getEffectiveCrossoverHz (1), 900.0, 1.0,
                                   "Body/Bite default crossover");
        expectWithinAbsoluteError (engine.getEffectiveCrossoverHz (2), 4500.0, 1.0,
                                   "Bite/Fizz default crossover");

        // Targets must also report the defaults (orderedTargets of the
        // unmodified state).
        expectWithinAbsoluteError (engine.getTargetCrossoverHz (0), 140.0, 1.0);
        expectWithinAbsoluteError (engine.getTargetCrossoverHz (1), 900.0, 1.0);
        expectWithinAbsoluteError (engine.getTargetCrossoverHz (2), 4500.0, 1.0);
    }

    void testReconstructionFlatMagnitude()
    {
        beginTest ("Reconstruction: unity + bypassed dynamics => flat magnitude vs dry (±0.5 dB)");

        // Dry probe.
        auto dry = makeMultiSineProbe (probeLength, 0.08);

        // Wet probe through engine at unity.
        MultibandEngine engine;
        engine.prepare (sampleRate, probeLength);
        engine.reset();
        configureUnityEngine (engine);

        auto wet = dry;
        engine.process (wet);

        auto* dryData = dry.getReadPointer (0);
        auto* wetData = wet.getReadPointer (0);

        // Output must be finite everywhere.
        expect (allFinite (wetData, probeLength),
                "Reconstructed output must be finite across the block");

        // Magnitude at each probe bin must match dry within ±0.5 dB. This is
        // a magnitude test only: LR4 phase is frequency-dependent, so the
        // complex samples are NOT sample-identical (see enable/level and
        // reset tests for non-sample-equality sanity).
        for (const auto f : probeHz)
        {
            const auto dryDb = binMagnitudeDb (dryData, probeLength, f, sampleRate);
            const auto wetDb = binMagnitudeDb (wetData, probeLength, f, sampleRate);
            const auto deltaDb = wetDb - dryDb;
            expectWithinAbsoluteError (deltaDb, 0.0, 0.5,
                                       "Reconstruction magnitude at " + juce::String (f, 1)
                                       + " Hz: dry=" + juce::String (dryDb, 4)
                                       + " dB wet=" + juce::String (wetDb, 4)
                                       + " dB delta=" + juce::String (deltaDb, 4) + " dB");
        }

        // Sanity: reconstruction is NOT sample-identical (LR4 phase network).
        // At least one sample must differ beyond float epsilon.
        bool anyDifference = false;
        for (int n = 0; n < probeLength; ++n)
        {
            if (std::abs (static_cast<double> (wetData[n])
                          - static_cast<double> (dryData[n])) > 1.0e-6)
            {
                anyDifference = true;
                break;
            }
        }
        expect (anyDifference,
                "Reconstruction must not be sample-identical to dry (LR4 phase)");
    }

    void testEnableLevelJump()
    {
        beginTest ("Enabling engine at unity does not produce a large level jump vs bypass");

        auto dry = makeDeterministicNoise (probeLength, 0xC0FFEEu);
        // Scale to a realistic peak around -6 dBFS.
        dry.applyGain (0, 0, probeLength, 0.4f);

        // Bypass run: engine disabled (true bypass).
        auto bypassRun = dry;
        {
            MultibandEngine engine;
            engine.prepare (sampleRate, probeLength);
            engine.reset();
            engine.setEnabled (false);
            engine.process (bypassRun);
        }

        // Enabled run: unity settings, dynamics bypassed.
        auto enabledRun = dry;
        {
            MultibandEngine engine;
            engine.prepare (sampleRate, probeLength);
            engine.reset();
            configureUnityEngine (engine);
            engine.process (enabledRun);
        }

        auto* bypassData = bypassRun.getReadPointer (0);
        auto* enabledData = enabledRun.getReadPointer (0);

        expect (allFinite (enabledData, probeLength),
                "Enabled run must be finite");

        const auto bypassRmsDb = rmsDb (bypassData, probeLength);
        const auto enabledRmsDb = rmsDb (enabledData, probeLength);
        const auto deltaDb = enabledRmsDb - bypassRmsDb;

        // No large level jump: RMS within ±0.5 dB (reconstruction is flat in
        // magnitude, so steady-state RMS must match the bypass path closely).
        expectWithinAbsoluteError (deltaDb, 0.0, 0.5,
                                   "Enable level jump RMS delta=" + juce::String (deltaDb, 4)
                                   + " dB (bypass=" + juce::String (bypassRmsDb, 4)
                                   + " dB enabled=" + juce::String (enabledRmsDb, 4) + " dB)");

        // Note: a time-domain PEAK ratio is intentionally NOT asserted here.
        // The LR4 reconstruction is an all-pass network: it preserves RMS /
        // energy (flat magnitude) but reshapes time-domain peaks via
        // frequency-dependent phase rotation, so the sample-peak of the wet
        // signal can legitimately differ from the dry peak by several dB
        // without any actual level jump. RMS is the correct level-jump metric.
    }

    void testCrossoverMotionFiniteAndClickBounded()
    {
        beginTest ("Crossover motion: sweeping frequencies yields finite, click-bounded output");

        constexpr int blockSize = 1024;
        constexpr int numBlocks = 48;

        // Build a steady mid-band sine probe that spans the whole sweep.
        const int totalSamples = blockSize * numBlocks;
        juce::AudioBuffer<float> probe (1, totalSamples);
        probe.clear();
        {
            auto* d = probe.getWritePointer (0);
            const double f = 1000.0;
            for (int n = 0; n < totalSamples; ++n)
                d[n] = static_cast<float> (0.3 * std::sin (2.0 * pi * f * n / sampleRate));
        }

        // Moving-crossover run: sweep all three crossovers across the blocks.
        juce::AudioBuffer<float> moving (1, totalSamples);
        moving.clear();
        {
            MultibandEngine engine;
            engine.prepare (sampleRate, blockSize);
            engine.reset();
            configureUnityEngine (engine);

            for (int b = 0; b < numBlocks; ++b)
            {
                const double t = static_cast<double> (b)
                                 / static_cast<double> (numBlocks - 1);
                // f0 in [80, 400], f1 in [400, 2500], f2 in [2000, 10000].
                const double f0 = 80.0 + t * (400.0 - 80.0);
                const double f1 = 400.0 + t * (2500.0 - 400.0);
                const double f2 = 2000.0 + t * (10000.0 - 2000.0);
                engine.setCrossoversHz (f0, f1, f2);

                juce::AudioBuffer<float> block (1, blockSize);
                block.copyFrom (0, 0, probe, 0, b * blockSize, blockSize);
                engine.process (block);
                moving.copyFrom (0, b * blockSize, block, 0, 0, blockSize);
            }
        }

        // Static-crossover control run: defaults held constant.
        juce::AudioBuffer<float> control (1, totalSamples);
        control.clear();
        {
            MultibandEngine engine;
            engine.prepare (sampleRate, blockSize);
            engine.reset();
            configureUnityEngine (engine);

            for (int b = 0; b < numBlocks; ++b)
            {
                juce::AudioBuffer<float> block (1, blockSize);
                block.copyFrom (0, 0, probe, 0, b * blockSize, blockSize);
                engine.process (block);
                control.copyFrom (0, b * blockSize, block, 0, 0, blockSize);
            }
        }

        auto* movingData = moving.getReadPointer (0);
        auto* controlData = control.getReadPointer (0);

        // 1. Finite, no NaN/Inf anywhere under motion.
        expect (allFinite (movingData, totalSamples),
                "Moving-crossover output must be finite (no NaN/Inf)");

        // 2. Bounded peak: no runaway gain under motion.
        const auto movingPeak = maxAbs (movingData, totalSamples);
        const auto controlPeak = maxAbs (controlData, totalSamples);
        expect (movingPeak < 2.0,
                "Moving-crossover peak must be bounded (peak="
                + juce::String (movingPeak, 6) + ")");
        // Moving peak should not wildly exceed the static control peak.
        const auto peakRatioDb = 20.0 * std::log10 (
            std::max (movingPeak, 1.0e-9) / std::max (controlPeak, 1.0e-9));
        expect (peakRatioDb < 6.0,
                "Moving peak vs control peak must be within 6 dB (ratio="
                + juce::String (peakRatioDb, 4) + " dB)");

        // 3. Click bound: max sample-to-sample delta under motion must not
        //    dramatically exceed the static-control delta. A click would show
        //    up as an impulsive spike in the first difference.
        const auto movingDelta = maxSampleDelta (movingData, totalSamples);
        const auto controlDelta = maxSampleDelta (controlData, totalSamples);
        // Allow 4x the control delta plus a small absolute floor for numerical
        // settling; the smoothing ramp is 20 ms so crossover changes are
        // gradual, never impulsive.
        const auto bound = std::max (4.0 * controlDelta, 0.05);
        expect (movingDelta <= bound,
                "Moving max delta=" + juce::String (movingDelta, 6)
                + " exceeds click bound " + juce::String (bound, 6)
                + " (control delta=" + juce::String (controlDelta, 6) + ")");

        // 4. Energy of moving run is within a reasonable band of the control.
        const auto movingRmsDb = rmsDb (movingData, totalSamples);
        const auto controlRmsDb = rmsDb (controlData, totalSamples);
        expectWithinAbsoluteError (movingRmsDb - controlRmsDb, 0.0, 6.0,
                                   "Moving RMS vs control RMS delta="
                                   + juce::String (movingRmsDb - controlRmsDb, 4) + " dB");
    }

    void testOrderingProtection()
    {
        beginTest ("Ordering protection: out-of-order targets resolve to f1 < f2 < f3");

        MultibandEngine engine;
        engine.prepare (sampleRate, 256);
        engine.reset();

        // Deliberately out-of-order: f0 very high, f1 very low, f2 mid.
        engine.setCrossoversHz (3000.0, 100.0, 2000.0);

        const auto t0 = engine.getTargetCrossoverHz (0);
        const auto t1 = engine.getTargetCrossoverHz (1);
        const auto t2 = engine.getTargetCrossoverHz (2);

        // Targets must be strictly ordered after the ordering guard.
        expect (t0 < t1,
                "Target f0 (" + juce::String (t0, 2) + ") must be < f1 ("
                + juce::String (t1, 2) + ")");
        expect (t1 < t2,
                "Target f1 (" + juce::String (t1, 2) + ") must be < f2 ("
                + juce::String (t2, 2) + ")");

        // Process a block so the effective frequencies propagate, then assert
        // the effective ordering also holds.
        juce::AudioBuffer<float> buffer (1, 256);
        buffer.clear();
        buffer.setSample (0, 0, 0.5f);
        configureUnityEngine (engine);
        engine.process (buffer);

        const auto e0 = engine.getEffectiveCrossoverHz (0);
        const auto e1 = engine.getEffectiveCrossoverHz (1);
        const auto e2 = engine.getEffectiveCrossoverHz (2);

        expect (e0 < e1,
                "Effective f0 (" + juce::String (e0, 2) + ") must be < f1 ("
                + juce::String (e1, 2) + ")");
        expect (e1 < e2,
                "Effective f1 (" + juce::String (e1, 2) + ") must be < f2 ("
                + juce::String (e2, 2) + ")");

        // Output must remain finite despite the reordered automation.
        expect (allFinite (buffer.getReadPointer (0), 256),
                "Output under out-of-order crossovers must be finite");

        // Direct crossover API agrees.
        MultibandCrossover crossover;
        crossover.prepare (sampleRate, 256);
        crossover.reset();
        crossover.setCrossoversHz (480.0, 160.0, 11000.0);
        const auto c = crossover.getEffectiveCrossoversHz();
        expect (c[0] < c[1] && c[1] < c[2],
                "Crossover effective array must be ordered ("
                + juce::String (c[0], 2) + " < " + juce::String (c[1], 2)
                + " < " + juce::String (c[2], 2) + ")");
    }

    void testSoloSuppressesOtherZones()
    {
        beginTest ("Solo: soloing one zone suppresses the other zones in the sum");

        auto dry = makeMultiSineProbe (probeLength, 0.08);

        // Full reconstruction reference (no solo).
        auto fullRun = dry;
        {
            MultibandEngine engine;
            engine.prepare (sampleRate, probeLength);
            engine.reset();
            configureUnityEngine (engine);
            engine.process (fullRun);
        }

        // Solo the Bite zone (zone 2, mid band ~900 Hz–4.5 kHz).
        auto soloedRun = dry;
        {
            MultibandEngine engine;
            engine.prepare (sampleRate, probeLength);
            engine.reset();
            configureUnityEngine (engine);
            engine.setZoneSolo (2, true);
            engine.process (soloedRun);
        }

        auto* fullData = fullRun.getReadPointer (0);
        auto* soloedData = soloedRun.getReadPointer (0);

        expect (allFinite (soloedData, probeLength),
                "Soloed output must be finite");

        const auto fullRmsDb = rmsDb (fullData, probeLength);
        const auto soloedRmsDb = rmsDb (soloedData, probeLength);

        // Soloing one of four zones removes three bands from the sum, so the
        // total energy must drop noticeably (at least ~3 dB).
        expect (soloedRmsDb < fullRmsDb - 3.0,
                "Soloed RMS (" + juce::String (soloedRmsDb, 4)
                + " dB) must be at least 3 dB below full ("
                + juce::String (fullRmsDb, 4) + " dB)");

        // Frequency-selective check: soloing Bite (zone 2) must suppress the
        // low band (100 Hz, in Weight) and the high band (8000 Hz, in Fizz).
        const auto fullLowDb = binMagnitudeDb (fullData, probeLength, 100.0, sampleRate);
        const auto soloedLowDb = binMagnitudeDb (soloedData, probeLength, 100.0, sampleRate);
        expect (soloedLowDb < fullLowDb - 6.0,
                "Soloing Bite must suppress 100 Hz (full=" + juce::String (fullLowDb, 4)
                + " dB soloed=" + juce::String (soloedLowDb, 4) + " dB)");

        const auto fullHighDb = binMagnitudeDb (fullData, probeLength, 8000.0, sampleRate);
        const auto soloedHighDb = binMagnitudeDb (soloedData, probeLength, 8000.0, sampleRate);
        expect (soloedHighDb < fullHighDb - 6.0,
                "Soloing Bite must suppress 8000 Hz (full=" + juce::String (fullHighDb, 4)
                + " dB soloed=" + juce::String (soloedHighDb, 4) + " dB)");
    }

    void testMuteSilencesZone()
    {
        beginTest ("Mute: muting a zone silences that zone in the sum");

        auto dry = makeMultiSineProbe (probeLength, 0.08);

        // Mute ALL zones: output must be near silence.
        auto allMuted = dry;
        {
            MultibandEngine engine;
            engine.prepare (sampleRate, probeLength);
            engine.reset();
            configureUnityEngine (engine);
            for (std::size_t i = 0; i < 4; ++i)
                engine.setZoneMute (i, true);
            engine.process (allMuted);
        }

        auto* mutedData = allMuted.getReadPointer (0);
        expect (allFinite (mutedData, probeLength),
                "All-muted output must be finite");

        const auto mutedPeak = maxAbs (mutedData, probeLength);
        // With every zone muted the sum is exactly zero (zones are cleared
        // after processing), allow a tiny float floor.
        expect (mutedPeak < 1.0e-6f,
                "All zones muted must yield near-zero output (peak="
                + juce::String (mutedPeak, 8) + ")");

        // Muting a single zone reduces total energy relative to the full
        // reconstruction but does not silence the others.
        auto fullRun = dry;
        auto oneMuted = dry;
        {
            MultibandEngine engine;
            engine.prepare (sampleRate, probeLength);
            engine.reset();
            configureUnityEngine (engine);
            engine.process (fullRun);
        }
        {
            MultibandEngine engine;
            engine.prepare (sampleRate, probeLength);
            engine.reset();
            configureUnityEngine (engine);
            engine.setZoneMute (1, true); // mute Body
            engine.process (oneMuted);
        }

        auto* fullData = fullRun.getReadPointer (0);
        auto* oneMutedData = oneMuted.getReadPointer (0);

        const auto fullRmsDb = rmsDb (fullData, probeLength);
        const auto oneMutedRmsDb = rmsDb (oneMutedData, probeLength);
        expect (oneMutedRmsDb < fullRmsDb,
                "Muting Body must reduce RMS vs full (full="
                + juce::String (fullRmsDb, 4) + " dB muted="
                + juce::String (oneMutedRmsDb, 4) + " dB)");

        // And the 300 Hz probe (Body band, between 140 and 900 Hz) must drop
        // noticeably when Body is muted.
        const auto full300 = binMagnitudeDb (fullData, probeLength, 300.0, sampleRate);
        const auto muted300 = binMagnitudeDb (oneMutedData, probeLength, 300.0, sampleRate);
        expect (muted300 < full300 - 3.0,
                "Muting Body must attenuate 300 Hz (full=" + juce::String (full300, 4)
                + " dB muted=" + juce::String (muted300, 4) + " dB)");
    }

    void testResetClearsFilterMemory()
    {
        beginTest ("reset() clears filter memory: finite impulse response and silence after reset");

        MultibandEngine engine;
        engine.prepare (sampleRate, 1024);
        engine.reset();
        configureUnityEngine (engine);

        // 1. Drive a loud sustained signal to populate filter states.
        {
            juce::AudioBuffer<float> drive (1, 1024);
            auto* d = drive.getWritePointer (0);
            for (int n = 0; n < 1024; ++n)
                d[n] = static_cast<float> (0.9 * std::sin (2.0 * pi * 500.0 * n / sampleRate));
            engine.process (drive);
        }

        // 2. reset() must clear all filter memory.
        engine.reset();

        // 3. Processing silence immediately after reset must yield near-zero
        //    output: no residual ringing from the previous drive block.
        {
            juce::AudioBuffer<float> silence (1, 1024);
            silence.clear();
            engine.process (silence);
            auto* s = silence.getReadPointer (0);
            expect (allFinite (s, 1024),
                    "Silence after reset must be finite");
            const auto peak = maxAbs (s, 1024);
            expect (peak < 1.0e-4f,
                    "Silence after reset must not ring (peak="
                    + juce::String (peak, 8) + ")");
        }

        // 4. An impulse after reset produces a finite, bounded impulse
        //    response with no NaN/Inf.
        juce::AudioBuffer<float> impulse (1, 1024);
        impulse.clear();
        impulse.setSample (0, 0, 1.0f);
        engine.process (impulse);

        auto* ir = impulse.getReadPointer (0);
        expect (allFinite (ir, 1024),
                "Impulse response after reset must be finite (no NaN/Inf)");
        const auto irPeak = maxAbs (ir, 1024);
        expect (irPeak > 0.0,
                "Impulse response must be nonzero");
        expect (irPeak < 4.0,
                "Impulse response must be bounded (peak="
                + juce::String (irPeak, 6) + ")");

        // 5. reset() on the crossover directly also yields finite output and
        //    no NaNs on a subsequent split+sum.
        MultibandCrossover crossover;
        crossover.prepare (sampleRate, 512);
        crossover.reset();

        std::array<juce::AudioBuffer<float>, MultibandCrossover::numberOfZones> zones;
        for (auto& z : zones)
            z.setSize (1, 512);

        juce::AudioBuffer<float> impulse2 (1, 512);
        impulse2.clear();
        impulse2.setSample (0, 0, 1.0f);

        expect (crossover.processSplit (impulse2,
                 { { &zones[0], &zones[1], &zones[2], &zones[3] } }),
                "processSplit must succeed on valid buffers");

        juce::AudioBuffer<float> summed (1, 512);
        summed.clear();
        expect (MultibandCrossover::sumZones (
                 { { &zones[0], &zones[1], &zones[2], &zones[3] } }, summed),
                "sumZones must succeed on valid buffers");

        auto* summedData = summed.getReadPointer (0);
        expect (allFinite (summedData, 512),
                "Crossover split+sum after reset must be finite");
    }
};

// Static registration: discovered by juce::UnitTestRunner::runAllTests().
static MultibandTests multibandTestsInstance;
