// =============================================================================
// ShredEQ - TightCut Unit Tests
// =============================================================================
// Covers the public DSP surface of Source/DSP/TightCut per
// .abracapocus/SHREDEQ_SPEC.md §6 (Tight Cut controls) and §21.5 chug-fixture
// intent where it touches Tight Cut's listen-to-removed behaviour:
//   * HPF enabled attenuates a low sine below cutoff relative to a high sine
//     above cutoff
//   * Higher slope yields more attenuation below cutoff than lower slope at
//     the same cutoff (spot-check 12 vs 36 dB/oct)
//   * Punch bell linked mode: effective punch frequency tracks r*f_c when
//     cutoff changes; unlinked keeps an independent punch frequency
//   * Listen-to-removed mode produces more low-band energy in the output than
//     the normal filtered path for a low-frequency probe
//   * reset() clears filter memory (no NaNs; finite output after impulse)
//
// Uses juce::UnitTest only — no Catch2/Google Test. Only public APIs are
// exercised; no production DSP code is rewritten.
// =============================================================================

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>

#include "Source/DSP/TightCut.h"

#include <algorithm>
#include <cmath>

namespace
{

using namespace ShredEQ::DSP;

constexpr double kSampleRate = 48000.0;
constexpr double kPi = 3.14159265358979323846;

// Fill a stereo buffer (both channels identical) with a steady sine of the
// given frequency/amplitude over the whole buffer.
void fillSine (juce::AudioBuffer<float>& buffer, double frequencyHz,
               double amplitude)
{
    for (int n = 0; n < buffer.getNumSamples(); ++n)
    {
        const auto v = static_cast<float> (
            amplitude * std::sin (2.0 * kPi * frequencyHz
                                  * static_cast<double> (n) / kSampleRate));
        buffer.setSample (0, n, v);
        buffer.setSample (1, n, v);
    }
}

// RMS of the tail half of channel 0 (post parameter-smoothing settle).
double rmsTail (const juce::AudioBuffer<float>& buffer)
{
    const int start = buffer.getNumSamples() / 2;
    const int count = buffer.getNumSamples() - start;
    double sumSq = 0.0;
    for (int n = start; n < buffer.getNumSamples(); ++n)
    {
        const auto v = static_cast<double> (buffer.getSample (0, n));
        sumSq += v * v;
    }
    return std::sqrt (sumSq / static_cast<double> (count));
}

// Render a TightCut over a copy of `input` and return the processed buffer.
juce::AudioBuffer<float> render (TightCut& tc, const juce::AudioBuffer<float>& input)
{
    juce::AudioBuffer<float> out (2, input.getNumSamples());
    for (int ch = 0; ch < 2; ++ch)
        out.copyFrom (ch, 0, input, ch, 0, input.getNumSamples());
    tc.process (out.getArrayOfWritePointers(), 2, out.getNumSamples());
    return out;
}

} // namespace

// =============================================================================

class TightCutTests final : public juce::UnitTest
{
public:
    TightCutTests()
        : juce::UnitTest ("TightCutTests", "ShredEQ/DSP") {}

    void runTest() override
    {
        testHpfAttenuatesLowRelativeToHigh();
        testHigherSlopeAttenuatesMoreBelowCutoff();
        testPunchLinkedTracksCutoff();
        testPunchUnlinkedIndependent();
        testListenRemovedMoreLowEnergyThanNormal();
        testResetClearsMemory();
    }

    // -----------------------------------------------------------------------

    void testHpfAttenuatesLowRelativeToHigh()
    {
        beginTest ("TightCut HPF: low sine below cutoff attenuated vs high sine above cutoff");

        // Cutoff 100 Hz, slope 24 dB/oct, punch bell flat (0 dB gain) so the
        // HPF is the dominant shaping element.
        TightCut tc;
        tc.setSlope (static_cast<int> (FilterSlope::dB24));
        tc.setCutoff (100.0);
        tc.setResonance (0.707);
        tc.setPunchLinked (true);
        tc.setPunchRatio (1.0);
        tc.setPunchGainDb (0.0);
        tc.setPunchQ (0.707);
        tc.setRouting (TightCut::Routing::stereo);
        tc.setEnabled (true);
        tc.setListenRemoved (false);
        tc.prepare (kSampleRate, 256);

        juce::AudioBuffer<float> low (2, 8192), high (2, 8192);
        fillSine (low, 30.0, 0.5);   // ~1.74 oct below 100 Hz
        fillSine (high, 400.0, 0.5); // ~2 oct above 100 Hz

        const auto lowOut = render (tc, low);
        const auto highOut = render (tc, high);

        const auto lowRms = rmsTail (lowOut);
        const auto highRms = rmsTail (highOut);
        const auto inputRms = 0.5 / std::sqrt (2.0);

        expect (std::isfinite (lowRms) && std::isfinite (highRms),
                "Output RMS must be finite");

        expect (lowRms < highRms,
                "Low sine below cutoff must be quieter than high sine above; low="
                + juce::String (lowRms) + " high=" + juce::String (highRms));

        expect (lowRms < inputRms * 0.5,
                "Low sine must be meaningfully attenuated below cutoff; lowRms="
                + juce::String (lowRms) + " inputRms=" + juce::String (inputRms));

        expectWithinAbsoluteError (highRms, inputRms, inputRms * 0.25,
                                   "High sine above cutoff should pass near unity");
    }

    // -----------------------------------------------------------------------

    void testHigherSlopeAttenuatesMoreBelowCutoff()
    {
        beginTest ("TightCut: higher slope (36 vs 12 dB/oct) attenuates more below cutoff");

        auto renderSlope = [] (int slopeDbPerOctave)
        {
            TightCut tc;
            tc.setSlope (slopeDbPerOctave); // canonicalised to supported value
            tc.setCutoff (100.0);
            tc.setResonance (0.707);
            tc.setPunchLinked (true);
            tc.setPunchRatio (1.0);
            tc.setPunchGainDb (0.0);
            tc.setPunchQ (0.707);
            tc.setRouting (TightCut::Routing::stereo);
            tc.setEnabled (true);
            tc.setListenRemoved (false);
            tc.prepare (kSampleRate, 256);

            juce::AudioBuffer<float> probe (2, 8192);
            fillSine (probe, 30.0, 0.5);
            return rmsTail (render (tc, probe));
        };

        const auto rms12 = renderSlope (12);
        const auto rms36 = renderSlope (36);

        expect (std::isfinite (rms12) && std::isfinite (rms36));
        expect (rms36 < rms12,
                "36 dB/oct must attenuate below cutoff more than 12 dB/oct; 12="
                + juce::String (rms12) + " 36=" + juce::String (rms36));
    }

    // -----------------------------------------------------------------------

    void testPunchLinkedTracksCutoff()
    {
        beginTest ("TightCut punch linked: effective punch freq tracks r*f_c when cutoff changes");

        TightCut tc;
        tc.prepare (kSampleRate, 256);
        tc.setPunchLinked (true);
        tc.setPunchRatio (1.2);

        tc.setCutoff (80.0);
        const auto eff80 = tc.getEffectivePunchFrequency();
        expectWithinAbsoluteError (eff80, 80.0 * 1.2, 1.0e-6,
                                   "Linked punch freq at cutoff 80 should be 96 Hz");

        tc.setCutoff (150.0);
        const auto eff150 = tc.getEffectivePunchFrequency();
        expectWithinAbsoluteError (eff150, 150.0 * 1.2, 1.0e-6,
                                   "Linked punch freq at cutoff 150 should be 180 Hz");

        expect (eff150 > eff80,
                "Linked punch freq must rise with cutoff; 80->"
                + juce::String (eff80) + " 150->" + juce::String (eff150));
    }

    void testPunchUnlinkedIndependent()
    {
        beginTest ("TightCut punch unlinked: punch freq independent of cutoff");

        TightCut tc;
        tc.prepare (kSampleRate, 256);
        tc.setPunchLinked (false);
        tc.setPunchFrequency (100.0);

        tc.setCutoff (80.0);
        const auto eff80 = tc.getEffectivePunchFrequency();
        expectWithinAbsoluteError (eff80, 100.0, 1.0e-6,
                                   "Unlinked punch freq must equal set value at cutoff 80");

        tc.setCutoff (150.0);
        const auto eff150 = tc.getEffectivePunchFrequency();
        expectWithinAbsoluteError (eff150, 100.0, 1.0e-6,
                                   "Unlinked punch freq must equal set value at cutoff 150");

        expectWithinAbsoluteError (eff80, eff150, 1.0e-9,
                                   "Unlinked punch freq must not change with cutoff");
    }

    // -----------------------------------------------------------------------

    void testListenRemovedMoreLowEnergyThanNormal()
    {
        beginTest ("TightCut listen-to-removed: more low-band energy than normal path");

        auto renderMode = [] (bool listenRemoved)
        {
            TightCut tc;
            tc.setSlope (static_cast<int> (FilterSlope::dB24));
            tc.setCutoff (100.0);
            tc.setResonance (0.707);
            tc.setPunchLinked (true);
            tc.setPunchRatio (1.0);
            tc.setPunchGainDb (0.0);
            tc.setPunchQ (0.707);
            tc.setRouting (TightCut::Routing::stereo);
            tc.setEnabled (true);
            tc.setListenRemoved (listenRemoved);
            tc.prepare (kSampleRate, 256);

            juce::AudioBuffer<float> probe (2, 8192);
            fillSine (probe, 30.0, 0.5); // low-frequency probe below cutoff
            return rmsTail (render (tc, probe));
        };

        const auto normalRms = renderMode (false);
        const auto listenRms = renderMode (true);

        expect (std::isfinite (normalRms) && std::isfinite (listenRms));
        expect (listenRms > normalRms * 2.0,
                "Listen-to-removed must expose more low-band energy than the normal "
                "filtered path; normal=" + juce::String (normalRms)
                + " listen=" + juce::String (listenRms));
    }

    // -----------------------------------------------------------------------

    void testResetClearsMemory()
    {
        beginTest ("TightCut reset(): clears filter memory, finite output after impulse");

        TightCut tc;
        tc.setSlope (static_cast<int> (FilterSlope::dB24));
        tc.setCutoff (100.0);
        tc.setResonance (0.707);
        tc.setPunchGainDb (6.0);
        tc.setEnabled (true);
        tc.prepare (kSampleRate, 256);

        // Drive loud signal to populate filter memory.
        juce::AudioBuffer<float> loud (2, 4096);
        fillSine (loud, 80.0, 0.95);
        auto loudOut = render (tc, loud);
        for (int ch = 0; ch < 2; ++ch)
            for (int n = 0; n < loudOut.getNumSamples(); ++n)
                expect (std::isfinite (loudOut.getSample (ch, n)),
                        "Loud render must produce finite samples");

        // Reset, then feed a single impulse followed by silence.
        tc.reset();

        juce::AudioBuffer<float> impulse (2, 1024);
        impulse.clear();
        impulse.setSample (0, 0, 1.0f);
        impulse.setSample (1, 0, 1.0f);
        const auto impOut = render (tc, impulse);

        bool anyNonZero = false;
        for (int ch = 0; ch < 2; ++ch)
        {
            for (int n = 0; n < impOut.getNumSamples(); ++n)
            {
                const auto v = impOut.getSample (ch, n);
                expect (std::isfinite (v),
                        "Post-reset output must be finite at sample "
                        + juce::String (n));
                if (std::abs (v) > 1.0e-9f)
                    anyNonZero = true;
            }
        }
        expect (anyNonZero,
                "Impulse after reset must produce a non-trivial finite response");
    }
};

// Static registration: discovered by juce::UnitTestRunner::runAllTests().
static TightCutTests tightCutTestsInstance;
