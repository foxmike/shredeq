// =============================================================================
// ShredEQ - Mid/Side + Sidechain Masking Unit Tests
// =============================================================================
// Covers the public DSP surface of Source/DSP/MidSideRouter,
// Source/DSP/SidechainAnalyzer, and Source/DSP/MaskingCarver per
// .abracapocus/SHREDEQ_SPEC.md:
//   * §10.2  Masking Metric (Overlap * Dominance * A_S; bounded G_carve)
//   * §11.1  Mid/Side guitar-bus processing (side high-pass 120-300 Hz)
//   * §11.2  energy-preserving orthonormal M/S matrix
//   * §21.6  bounded recommendations, no audible change until applied
//
// Uses juce::UnitTest only — no Catch2/Google Test. Only public APIs are
// exercised; no production DSP code is rewritten.
// =============================================================================

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>

#include "Source/DSP/MidSideRouter.h"
#include "Source/DSP/SidechainAnalyzer.h"
#include "Source/DSP/MaskingCarver.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace
{

using namespace ShredEQ::DSP;

constexpr double kSampleRate = 48000.0;
constexpr double kPi = 3.14159265358979323846;

// Fill a stereo buffer with an arbitrary L/R sine pair. If panSign is +1, R is
// in-phase (mono content); if -1, R is anti-phase (pure side content).
void fillSineStereo (juce::AudioBuffer<float>& buffer, double frequencyHz,
                     double amplitude, double panSign)
{
    for (int n = 0; n < buffer.getNumSamples(); ++n)
    {
        const auto v = static_cast<float> (
            amplitude * std::sin (2.0 * kPi * frequencyHz
                                  * static_cast<double> (n) / kSampleRate));
        buffer.setSample (0, n, v);
        buffer.setSample (1, n, static_cast<float> (panSign) * v);
    }
}

// Mono-compatible tone (L == R).
void fillMonoSine (juce::AudioBuffer<float>& buffer, double frequencyHz,
                   double amplitude)
{
    fillSineStereo (buffer, frequencyHz, amplitude, 1.0);
}

double rmsChannel (const juce::AudioBuffer<float>& buffer, int channel,
                   int startSample = 0, int numSamples = -1)
{
    const int total = buffer.getNumSamples();
    const int start = std::max (0, startSample);
    const int count = numSamples < 0 ? (total - start) : std::min (numSamples, total - start);
    if (count <= 0)
        return 0.0;
    double sumSq = 0.0;
    for (int n = 0; n < count; ++n)
    {
        const auto v = static_cast<double> (buffer.getSample (channel, start + n));
        sumSq += v * v;
    }
    return std::sqrt (sumSq / static_cast<double> (count));
}

// RMS of the tail half of a channel (post parameter-smoothing settle).
double rmsTail (const juce::AudioBuffer<float>& buffer, int channel)
{
    const int start = buffer.getNumSamples() / 2;
    return rmsChannel (buffer, channel, start);
}

bool allFinite (const juce::AudioBuffer<float>& buffer)
{
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        for (int n = 0; n < buffer.getNumSamples(); ++n)
            if (! std::isfinite (buffer.getSample (ch, n)))
                return false;
    return true;
}

// Index of the analyzer band whose centre is closest to frequencyHz.
std::size_t nearestAnalyzerBand (const SidechainAnalyzer& analyzer, double frequencyHz)
{
    std::size_t best = 0;
    double bestDist = std::numeric_limits<double>::max();
    for (std::size_t b = 0; b < SidechainAnalyzer::numBands; ++b)
    {
        const auto center = static_cast<double> (analyzer.getBandCenterHz (b));
        const auto dist = std::abs (center - frequencyHz);
        if (dist < bestDist)
        {
            bestDist = dist;
            best = b;
        }
    }
    return best;
}

} // namespace

// =============================================================================

class MidSideTests final : public juce::UnitTest
{
public:
    MidSideTests()
        : juce::UnitTest ("MidSideTests", "ShredEQ/DSP") {}

    void runTest() override
    {
        testEncodeDecodeRoundTrip();
        testMonoToneConcentratesMid();
        testSideHighPassAttenuatesLowSide();
        testDisabledRouterIsBypass();
        testMaskingMetricStrongCollision();
        testSilentSidechainZeroMasking();
        testSuggestionModeIsBypass();
        testMaskingCarverDisabledBypass();
        testMaskingCarverEnabledCarvesBand();
    }

    // -----------------------------------------------------------------------
    // §11.2: energy-preserving orthonormal M/S matrix; encode/decode inverse.
    // -----------------------------------------------------------------------

    void testEncodeDecodeRoundTrip()
    {
        beginTest ("MidSideRouter: encode then decode restores L/R within tight tolerance");

        MidSideRouter router;
        router.setEnabled (true);
        router.setSideHighPassEnabled (false);
        router.prepare (kSampleRate, 256);

        juce::AudioBuffer<float> original (2, 4096);
        // Two distinct, non-harmonic sines per channel for a non-trivial signal.
        for (int n = 0; n < original.getNumSamples(); ++n)
        {
            const double t = static_cast<double> (n) / kSampleRate;
            original.setSample (0, n, static_cast<float> (0.3 * std::sin (2.0 * kPi * 440.0 * t)
                                                         + 0.12 * std::sin (2.0 * kPi * 1517.0 * t)));
            original.setSample (1, n, static_cast<float> (0.27 * std::sin (2.0 * kPi * 613.0 * t)
                                                         + 0.10 * std::sin (2.0 * kPi * 2934.0 * t)));
        }

        juce::AudioBuffer<float> work (2, original.getNumSamples());
        for (int ch = 0; ch < 2; ++ch)
            work.copyFrom (ch, 0, original, ch, 0, original.getNumSamples());

        router.processEncode (work);
        expect (router.isMidSideDomain(), "Router should report M/S domain after encode");
        router.processDecode (work);
        expect (! router.isMidSideDomain(), "Router should leave M/S domain after decode");

        expect (allFinite (work), "Round-trip output must be finite");

        double maxErr = 0.0;
        for (int ch = 0; ch < 2; ++ch)
        {
            for (int n = 0; n < work.getNumSamples(); ++n)
            {
                const auto err = std::abs (static_cast<double> (work.getSample (ch, n))
                                           - static_cast<double> (original.getSample (ch, n)));
                maxErr = std::max (maxErr, err);
            }
        }
        expectWithinAbsoluteError (maxErr, 0.0, 1.0e-6,
                                   "Encode/decode must be an energy-preserving inverse; maxErr="
                                   + juce::String (maxErr));
    }

    // -----------------------------------------------------------------------
    // §11.2: identical L=R tone → energy in Mid, near-zero Side.
    // -----------------------------------------------------------------------

    void testMonoToneConcentratesMid()
    {
        beginTest ("MidSideRouter: identical L=R tone concentrates energy in Mid, near-zero Side");

        MidSideRouter router;
        router.setEnabled (true);
        router.setSideHighPassEnabled (false);
        router.prepare (kSampleRate, 256);

        juce::AudioBuffer<float> buffer (2, 8192);
        fillMonoSine (buffer, 880.0, 0.5);

        juce::AudioBuffer<float> work (2, buffer.getNumSamples());
        for (int ch = 0; ch < 2; ++ch)
            work.copyFrom (ch, 0, buffer, ch, 0, buffer.getNumSamples());

        router.processEncode (work);

        // After encode: ch0 = Mid, ch1 = Side.
        const double midRms = rmsTail (work, 0);
        const double sideRms = rmsTail (work, 1);
        const double inputRms = 0.5 / std::sqrt (2.0);

        expect (std::isfinite (midRms) && std::isfinite (sideRms));
        // Mid amplitude = L*sqrt(2); RMS = inputRms * sqrt(2).
        expectWithinAbsoluteError (midRms, inputRms * std::sqrt (2.0), inputRms * 0.05,
                                   "Mid should carry the summed mono energy");
        expect (sideRms < inputRms * 1.0e-3,
                "Side should be near-zero for identical L=R; sideRms=" + juce::String (sideRms));

        router.processDecode (work);
        double maxErr = 0.0;
        for (int ch = 0; ch < 2; ++ch)
            for (int n = 0; n < work.getNumSamples(); ++n)
                maxErr = std::max (maxErr, std::abs (static_cast<double> (work.getSample (ch, n))
                                                     - static_cast<double> (buffer.getSample (ch, n))));
        expectWithinAbsoluteError (maxErr, 0.0, 1.0e-6,
                                   "Decode must restore original mono tone; maxErr=" + juce::String (maxErr));
    }

    // -----------------------------------------------------------------------
    // §11.1: side high-pass in 120-300 Hz attenuates low-frequency side
    // content relative to a bypassed HPF.
    // -----------------------------------------------------------------------

    void testSideHighPassAttenuatesLowSide()
    {
        beginTest ("MidSideRouter: side HPF (120-300 Hz) attenuates low-frequency side vs bypass");

        auto renderSideRms = [] (bool hpfEnabled, float cutoffHz)
        {
            MidSideRouter router;
            router.setEnabled (true);
            router.setSideHighPassEnabled (hpfEnabled);
            router.setSideHighPassHz (cutoffHz);
            router.prepare (kSampleRate, 256);

            juce::AudioBuffer<float> buffer (2, 16384);
            // Pure side content at a low frequency below the cutoff.
            fillSineStereo (buffer, 60.0, 0.5, -1.0);

            router.processEncode (buffer);
            return rmsTail (buffer, 1); // Side channel after encode
        };

        const double bypassRms = renderSideRms (false, 200.0f);
        const double hpfRms = renderSideRms (true, 200.0f);

        expect (std::isfinite (bypassRms) && std::isfinite (hpfRms));
        expect (hpfRms < bypassRms * 0.5,
                "Side HPF at 200 Hz must attenuate 60 Hz side content vs bypass; bypass="
                + juce::String (bypassRms) + " hpf=" + juce::String (hpfRms));

        // Cutoff is clamped into the documented 120-300 Hz range.
        MidSideRouter clampRouter;
        clampRouter.setSideHighPassHz (50.0f);
        expectWithinAbsoluteError (clampRouter.getSideHighPassHz(), 120.0f, 1.0e-3f,
                                   "Side HPF cutoff must clamp to 120 Hz lower bound");
        clampRouter.setSideHighPassHz (1000.0f);
        expectWithinAbsoluteError (clampRouter.getSideHighPassHz(), 300.0f, 1.0e-3f,
                                   "Side HPF cutoff must clamp to 300 Hz upper bound");
    }

    // -----------------------------------------------------------------------
    // Disabled router leaves the buffer unchanged (true bypass).
    // -----------------------------------------------------------------------

    void testDisabledRouterIsBypass()
    {
        beginTest ("MidSideRouter: disabled router leaves buffer unchanged within tolerance");

        MidSideRouter router;
        router.setEnabled (false);
        router.setSideHighPassEnabled (true);
        router.setSideHighPassHz (200.0f);
        router.prepare (kSampleRate, 256);

        juce::AudioBuffer<float> original (2, 2048);
        fillSineStereo (original, 220.0, 0.4, -1.0);

        juce::AudioBuffer<float> work (2, original.getNumSamples());
        for (int ch = 0; ch < 2; ++ch)
            work.copyFrom (ch, 0, original, ch, 0, original.getNumSamples());

        router.processEncode (work);
        expect (! router.isMidSideDomain(), "Disabled router must not enter M/S domain");
        router.processDecode (work);

        double maxErr = 0.0;
        for (int ch = 0; ch < 2; ++ch)
            for (int n = 0; n < work.getNumSamples(); ++n)
                maxErr = std::max (maxErr, std::abs (static_cast<double> (work.getSample (ch, n))
                                                     - static_cast<double> (original.getSample (ch, n))));
        expectWithinAbsoluteError (maxErr, 0.0, 1.0e-9,
                                   "Disabled router must be a true bypass; maxErr=" + juce::String (maxErr));
    }

    // -----------------------------------------------------------------------
    // §10.2: synthetic main+sidechain in the same band → Masking > 0 and
    // G_carve ≤ 0 with |G_carve| ≤ G_max. Silent sidechain → Masking ≈ 0.
    // -----------------------------------------------------------------------

    void testMaskingMetricStrongCollision()
    {
        beginTest ("SidechainAnalyzer: strong same-band collision yields Masking>0, G_carve<=0, |G_carve|<=G_max");

        SidechainAnalyzer analyzer;
        analyzer.setEnabled (true);
        analyzer.setActivityThresholdDb (-72.0f);
        analyzer.setCarveParameters (6.0f, 12.0f, 0.2f); // G_max=6, K=12, T=0.2
        analyzer.prepare (kSampleRate, 256);
        analyzer.reset();

        // Choose a tone that lands cleanly in one analyzer band.
        const double collisionHz = 800.0;
        const auto band = nearestAnalyzerBand (analyzer, collisionHz);

        juce::AudioBuffer<float> main (2, 8192);
        juce::AudioBuffer<float> sidechain (2, 8192);
        fillMonoSine (main, collisionHz, 0.5);
        fillMonoSine (sidechain, collisionHz, 0.5);

        const float* mainCh[2] = { main.getReadPointer (0), main.getReadPointer (1) };
        const float* sideCh[2] = { sidechain.getReadPointer (0), sidechain.getReadPointer (1) };

        // Process several blocks so the smoothed power/release settles.
        for (int block = 0; block < 4; ++block)
        {
            const int offset = block * 2048;
            analyzer.process (mainCh, 2, sideCh, 2, 2048);
            juce::ignoreUnused (offset);
        }

        const auto masking = analyzer.getLatestMasking();
        const auto carve = analyzer.getLatestCarveRequestDb();

        expect (masking[band] > 0.0f,
                "Masking must be > 0 in collision band " + juce::String (band)
                + "; got " + juce::String (masking[band]));
        expect (carve[band] <= 0.0f,
                "G_carve must be non-positive; got " + juce::String (carve[band]));
        expect (std::abs (carve[band]) <= 6.0f + 1.0e-4f,
                "|G_carve| must be bounded by G_max=6; got " + juce::String (carve[band]));

        // Sanity: the requested carve is consistent with the documented formula.
        const float expectedCarve = -std::min (6.0f, 12.0f * std::max (0.0f, masking[band] - 0.2f));
        expectWithinAbsoluteError (carve[band], expectedCarve, 1.0e-3f,
                                   "G_carve must match -min(G_max, K*max(0, Masking-T))");
    }

    void testSilentSidechainZeroMasking()
    {
        beginTest ("SidechainAnalyzer: silent sidechain yields ~0 masking");

        SidechainAnalyzer analyzer;
        analyzer.setEnabled (true);
        analyzer.setActivityThresholdDb (-72.0f);
        analyzer.setCarveParameters (6.0f, 12.0f, 0.2f);
        analyzer.prepare (kSampleRate, 256);
        analyzer.reset();

        juce::AudioBuffer<float> main (2, 8192);
        juce::AudioBuffer<float> sidechain (2, 8192);
        fillMonoSine (main, 800.0, 0.5);
        sidechain.clear();

        const float* mainCh[2] = { main.getReadPointer (0), main.getReadPointer (1) };
        const float* sideCh[2] = { sidechain.getReadPointer (0), sidechain.getReadPointer (1) };

        for (int block = 0; block < 4; ++block)
            analyzer.process (mainCh, 2, sideCh, 2, 2048);

        const auto masking = analyzer.getLatestMasking();
        const auto carve = analyzer.getLatestCarveRequestDb();
        for (std::size_t b = 0; b < SidechainAnalyzer::numBands; ++b)
        {
            expect (masking[b] < 1.0e-4f,
                    "Silent sidechain must yield ~0 masking in band " + juce::String ((int) b)
                    + "; got " + juce::String (masking[b]));
            expectWithinAbsoluteError (carve[b], 0.0f, 1.0e-4f,
                                       "Silent sidechain must yield ~0 G_carve in band " + juce::String ((int) b));
        }
    }

    // -----------------------------------------------------------------------
    // §21.6: suggestion mode never modifies sound until explicitly applied.
    // -----------------------------------------------------------------------

    void testSuggestionModeIsBypass()
    {
        beginTest ("MaskingCarver: suggestion mode does not change process output");

        MaskingCarver carver;
        carver.setEnabled (true);
        carver.setMode (MaskingCarverMode::suggestion);
        carver.setPlacement (MaskingCarverPlacement::stereo);
        carver.setCarveParameters (6.0f, 12.0f, 0.2f);
        carver.prepare (kSampleRate, 256);

        // Import strong masking so a suggestion is generated, then accept it
        // into a carver band. Suggestion mode still must not process audio.
        SidechainAnalyzer::BandValues masking {};
        SidechainAnalyzer::BandValues centers {};
        for (std::size_t b = 0; b < SidechainAnalyzer::numBands; ++b)
            centers[b] = 800.0f;
        masking[8] = 1.0f; // strong collision in band 8
        carver.importAnalyzerMetrics (masking, centers);

        expect (carver.getNumSuggestions() > 0,
                "Strong masking fixture should produce at least one suggestion");

        const auto accepted = carver.acceptSuggestion (0, 0, true);
        expect (accepted, "acceptSuggestion should succeed for a valid index");
        expect (carver.isBandEnabled (0),
                "Accepted suggestion band should be enabled");

        juce::AudioBuffer<float> original (2, 4096);
        fillMonoSine (original, 800.0, 0.5);
        juce::AudioBuffer<float> work (2, original.getNumSamples());
        for (int ch = 0; ch < 2; ++ch)
            work.copyFrom (ch, 0, original, ch, 0, original.getNumSamples());

        carver.process (work);

        double maxErr = 0.0;
        for (int ch = 0; ch < 2; ++ch)
            for (int n = 0; n < work.getNumSamples(); ++n)
                maxErr = std::max (maxErr, std::abs (static_cast<double> (work.getSample (ch, n))
                                                     - static_cast<double> (original.getSample (ch, n))));
        expectWithinAbsoluteError (maxErr, 0.0, 1.0e-9,
                                   "Suggestion mode must be a true bypass even with an enabled band; maxErr="
                                   + juce::String (maxErr));

        // Switching to automatic mode with the enabled band now carves audio.
        carver.setMode (MaskingCarverMode::automatic);
        juce::AudioBuffer<float> work2 (2, original.getNumSamples());
        for (int ch = 0; ch < 2; ++ch)
            work2.copyFrom (ch, 0, original, ch, 0, original.getNumSamples());
        carver.process (work2);

        const double bypassRms = rmsTail (original, 0);
        const auto carvedRms = rmsTail (work2, 0);
        expect (carvedRms < bypassRms * 0.9,
                "Automatic mode with enabled band must reduce band energy; bypass="
                + juce::String (bypassRms) + " carved=" + juce::String (carvedRms));
    }

    // -----------------------------------------------------------------------
    // MaskingCarver disabled = true bypass.
    // -----------------------------------------------------------------------

    void testMaskingCarverDisabledBypass()
    {
        beginTest ("MaskingCarver: disabled carver is a true bypass");

        MaskingCarver carver;
        carver.setEnabled (false);
        carver.setMode (MaskingCarverMode::automatic);
        carver.setPlacement (MaskingCarverPlacement::stereo);
        carver.setCarveParameters (6.0f, 12.0f, 0.2f);
        carver.prepare (kSampleRate, 256);

        juce::AudioBuffer<float> original (2, 4096);
        fillMonoSine (original, 800.0, 0.5);
        juce::AudioBuffer<float> work (2, original.getNumSamples());
        for (int ch = 0; ch < 2; ++ch)
            work.copyFrom (ch, 0, original, ch, 0, original.getNumSamples());

        carver.process (work);

        double maxErr = 0.0;
        for (int ch = 0; ch < 2; ++ch)
            for (int n = 0; n < work.getNumSamples(); ++n)
                maxErr = std::max (maxErr, std::abs (static_cast<double> (work.getSample (ch, n))
                                                     - static_cast<double> (original.getSample (ch, n))));
        expectWithinAbsoluteError (maxErr, 0.0, 1.0e-9,
                                   "Disabled carver must be a true bypass; maxErr=" + juce::String (maxErr));
    }

    // -----------------------------------------------------------------------
    // Enabled band carves reduce main energy in the collision band vs bypass.
    // -----------------------------------------------------------------------

    void testMaskingCarverEnabledCarvesBand()
    {
        beginTest ("MaskingCarver: enabled band under strong masking reduces main band energy vs bypass");

        const double collisionHz = 800.0;

        auto render = [&] (bool enabled)
        {
            MaskingCarver carver;
            carver.setEnabled (enabled);
            carver.setMode (MaskingCarverMode::automatic);
            carver.setPlacement (MaskingCarverPlacement::stereo);
            carver.setCarveParameters (6.0f, 12.0f, 0.2f);
            carver.setAttackReleaseMs (1.0f, 1.0f); // fast settle for the test fixture
            carver.prepare (kSampleRate, 256);

            SidechainAnalyzer::BandValues masking {};
            SidechainAnalyzer::BandValues centers {};
            for (std::size_t b = 0; b < SidechainAnalyzer::numBands; ++b)
                centers[b] = static_cast<float> (collisionHz);
            masking[8] = 1.0f; // saturate masking → G_carve = -G_max
            carver.importAnalyzerMetrics (masking, centers);

            carver.configureBand (0, 8, static_cast<float> (collisionHz), 1.4f);
            carver.setBandEnabled (0, true);

            juce::AudioBuffer<float> buffer (2, 16384);
            fillMonoSine (buffer, collisionHz, 0.5);
            carver.process (buffer);
            return rmsTail (buffer, 0);
        };

        const double bypassRms = render (false);
        const auto carvedRms = render (true);

        expect (std::isfinite (bypassRms) && std::isfinite (carvedRms));
        expect (carvedRms < bypassRms * 0.7,
                "Enabled carve band must reduce main energy in the collision band; bypass="
                + juce::String (bypassRms) + " carved=" + juce::String (carvedRms));
    }
};

// Static registration: discovered by juce::UnitTestRunner::runAllTests().
static MidSideTests midSideTestsInstance;
