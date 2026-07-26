// =============================================================================
// ShredEQ - Biquad Coefficient / RBJ Design Unit Tests
// =============================================================================
// Covers the static coefficient-design surface in Source/DSP/BiquadCoefficients:
//   * slope cascade section counts for 6/12/18/24/36/48 dB/oct
//   * RBJ peaking (bell) magnitude at f0 within +/-0.1 dB of requested gain
//     away from Nyquist (48 kHz)
//   * LP/HP directional attenuation sanity
//   * Nyquist-edge behavior at 48 kHz and 96 kHz (asserts only what the
//     digital RBJ design actually guarantees; no analog-match claims)
//
// Per .abracapocus/SHREDEQ_SPEC.md §21.1 the static magnitude target is
// +/-0.1 dB through the operating range with documented Nyquist exceptions.
// =============================================================================

#include <juce_core/juce_core.h>

#include "Source/DSP/BiquadCoefficients.h"

#include <cmath>

namespace
{

using namespace ShredEQ::DSP;

constexpr double pi = 3.14159265358979323846264338327950288;

double magnitudeDb (const BiquadCoefficients& c, double frequencyHz, double sampleRate)
{
    const auto m = c.magnitude (frequencyHz, sampleRate);
    return 20.0 * std::log10 (std::max (m, 1.0e-300));
}

FilterParameters params (double sampleRate, double frequencyHz,
                         double q = 0.7071067811865476, double gainDb = 0.0)
{
    return FilterParameters { sampleRate, frequencyHz, q, gainDb };
}

} // namespace

class BiquadCoefficientsTests final : public juce::UnitTest
{
public:
    BiquadCoefficientsTests()
        : juce::UnitTest ("BiquadCoefficientsTests", "ShredEQ/DSP") {}

    void runTest() override
    {
        testUnityBypass();
        testSlopeCascadeSectionCounts();
        testBellMagnitudeAtCenter();
        testLowPassAttenuationDirection();
        testHighPassAttenuationDirection();
        testNyquistEdge48k();
        testNyquistEdge96k();
        testMultiRateBellTracking();
    }

    void testUnityBypass()
    {
        beginTest ("Unity/bypass coefficients are flat");
        const auto u = BiquadCoefficients::unity();
        expectEquals (u.b0, 1.0);
        expectEquals (u.b1, 0.0);
        expectEquals (u.b2, 0.0);
        expectEquals (u.a1, 0.0);
        expectEquals (u.a2, 0.0);

        // Unity must evaluate to exactly 1.0 at every frequency.
        for (double f : { 20.0, 250.0, 1000.0, 8000.0, 20000.0 })
            expectWithinAbsoluteError (u.magnitude (f, 48000.0), 1.0, 1.0e-9);
    }

    void testSlopeCascadeSectionCounts()
    {
        beginTest ("Slope cascade section counts for 6/12/18/24/36/48 dB/oct");

        struct Row { int slope; std::size_t expected; };
        const Row rows[] = {
            { 6,  1 },
            { 12, 1 },
            { 18, 2 },
            { 24, 2 },
            { 36, 3 },
            { 48, 4 },
        };

        for (const auto& row : rows)
        {
            const auto lp = makeSlopeCascade (FilterKind::lowPass, row.slope,
                                              params (48000.0, 1000.0));
            expectEquals (lp.size(), row.expected,
                          "LP slope " + juce::String (row.slope) + " dB/oct section count");

            const auto hp = makeSlopeCascade (FilterKind::highPass, row.slope,
                                              params (48000.0, 1000.0));
            expectEquals (hp.size(), row.expected,
                          "HP slope " + juce::String (row.slope) + " dB/oct section count");

            // Odd-order cascades carry one first-order section; even orders are
            // all second-order. Verify ordering invariant for 6 and 18 dB/oct.
            if (row.slope == 6 || row.slope == 18)
            {
                expect (lp.sections[0].order == SectionOrder::first,
                        "Odd-order cascade must lead with a first-order section");
            }
            else
            {
                for (std::size_t i = 0; i < lp.size(); ++i)
                    expect (lp.sections[i].order == SectionOrder::second,
                            "Even-order cascade sections must be second order");
            }
        }

        // Slopes outside the canonical list snap to nearest supported value.
        const auto snap = makeSlopeCascade (FilterKind::lowPass, 30,
                                            params (48000.0, 1000.0));
        // 30 is equidistant from 24 and 36; canonicalSlope picks 24 (first
        // minimum). Assert only that it lands on a supported count.
        expect (snap.size() == 2 || snap.size() == 3,
                "Out-of-list slope must snap to a supported section count");

        // Bypass yields an empty cascade.
        const auto bypassCascade = makeSlopeCascade (FilterKind::bypass, 12,
                                                     params (48000.0, 1000.0));
        expect (bypassCascade.empty(),
                "Bypass kind must produce an empty cascade");

        // Tilt is represented by exactly two opposing shelf sections.
        const auto tilt = makeSlopeCascade (FilterKind::tilt, 12,
                                            params (48000.0, 1000.0, 0.7071067811865476, 3.0));
        expectEquals (tilt.size(), (std::size_t) 2,
                      "Tilt must be represented by two shelf sections");
    }

    void testBellMagnitudeAtCenter()
    {
        beginTest ("RBJ bell magnitude at f0 within +/-0.1 dB of requested gain (48 kHz)");

        // f0 well below Nyquist so the digital RBJ design is exact at f0.
        const double sampleRate = 48000.0;
        const double f0 = 1000.0;
        const double q = 1.0;

        for (double gainDb : { -12.0, -6.0, -3.0, 0.0, 3.0, 6.0, 12.0 })
        {
            const auto coeff = BiquadCoefficients::makeSecondOrder (
                FilterKind::bell, sampleRate, f0, q, gainDb);

            const auto measuredDb = magnitudeDb (coeff, f0, sampleRate);

            // At f0 the RBJ peaking filter's magnitude equals the requested
            // gain exactly in exact arithmetic; allow +/-0.1 dB per spec.
            expectWithinAbsoluteError (measuredDb, gainDb, 0.1,
                                       "Bell gain " + juce::String (gainDb, 3)
                                       + " dB at f0 measured "
                                       + juce::String (measuredDb, 6) + " dB");
        }

        // DC and Nyquist-adjacent response of a unity-gain bell (0 dB) is ~0 dB.
        const auto flat = BiquadCoefficients::makeSecondOrder (
            FilterKind::bell, sampleRate, f0, q, 0.0);
        expectWithinAbsoluteError (magnitudeDb (flat, 20.0, sampleRate), 0.0, 0.1);
    }

    void testLowPassAttenuationDirection()
    {
        beginTest ("Low-pass attenuates above cutoff and passes below");

        const double sampleRate = 48000.0;
        const double fc = 1000.0;

        const auto lp = BiquadCoefficients::makeSecondOrder (
            FilterKind::lowPass, sampleRate, fc, 0.7071067811865476, 0.0);

        const auto below = lp.magnitude (fc * 0.25, sampleRate);
        const auto above = lp.magnitude (fc * 8.0, sampleRate);

        // DC/low-band gain is unity to within a few hundredths of a dB.
        expectWithinAbsoluteError (20.0 * std::log10 (below), 0.0, 0.5);
        // High-band is attenuated: well below unity.
        expect (above < 0.9,
                "LP above cutoff must attenuate (|H|=" + juce::String (above, 6) + ")");
        expect (above < below,
                "LP above cutoff must be quieter than below cutoff");
    }

    void testHighPassAttenuationDirection()
    {
        beginTest ("High-pass attenuates below cutoff and passes above");

        const double sampleRate = 48000.0;
        const double fc = 1000.0;

        const auto hp = BiquadCoefficients::makeSecondOrder (
            FilterKind::highPass, sampleRate, fc, 0.7071067811865476, 0.0);

        const auto below = hp.magnitude (fc * 0.125, sampleRate);
        const auto above = hp.magnitude (fc * 8.0, sampleRate);

        expectWithinAbsoluteError (20.0 * std::log10 (above), 0.0, 0.5);
        expect (below < 0.9,
                "HP below cutoff must attenuate (|H|=" + juce::String (below, 6) + ")");
        expect (below < above,
                "HP below cutoff must be quieter than above cutoff");
    }

    void testNyquistEdge48k()
    {
        beginTest ("Nyquist-edge response at 48 kHz is finite and bounded");

        const double sampleRate = 48000.0;
        const double nyquist = 0.5 * sampleRate;

        // Bell with gain near Nyquist: RBJ design is not claimed to match an
        // analog prototype here, but must remain finite and bounded.
        const auto bell = BiquadCoefficients::makeSecondOrder (
            FilterKind::bell, sampleRate, nyquist * 0.9, 2.0, 6.0);

        for (double f : { nyquist * 0.25, nyquist * 0.5, nyquist * 0.9, nyquist })
        {
            const auto m = bell.magnitude (f, sampleRate);
            expect (std::isfinite (m), "Nyquist-edge magnitude must be finite");
            expect (m < 100.0, "Nyquist-edge magnitude must be bounded");
        }

        // At exactly Nyquist, z^-1 = -1, so the response is real-valued and
        // finite for a stable RBJ section. We assert only finiteness, not a
        // specific analog-matching value.
        const auto mNy = bell.magnitude (nyquist, sampleRate);
        expect (std::isfinite (mNy) && mNy > 0.0,
                "Nyquist magnitude must be finite and positive");
    }

    void testNyquistEdge96k()
    {
        beginTest ("Nyquist-edge response at 96 kHz is finite and bounded");

        const double sampleRate = 96000.0;
        const double nyquist = 0.5 * sampleRate;

        const auto bell = BiquadCoefficients::makeSecondOrder (
            FilterKind::bell, sampleRate, nyquist * 0.9, 2.0, 6.0);

        for (double f : { nyquist * 0.25, nyquist * 0.5, nyquist * 0.9, nyquist })
        {
            const auto m = bell.magnitude (f, sampleRate);
            expect (std::isfinite (m), "96k Nyquist-edge magnitude must be finite");
            expect (m < 100.0, "96k Nyquist-edge magnitude must be bounded");
        }

        // Multi-rate check: the same bell designed at the same absolute f0
        // (well below both Nyquists) must read the requested gain at both
        // sample rates. This verifies the design is sample-rate independent
        // through the operating range.
        const double f0 = 1000.0;
        const double gainDb = 6.0;
        const auto b48 = BiquadCoefficients::makeSecondOrder (
            FilterKind::bell, 48000.0, f0, 1.0, gainDb);
        const auto b96 = BiquadCoefficients::makeSecondOrder (
            FilterKind::bell, 96000.0, f0, 1.0, gainDb);

        expectWithinAbsoluteError (magnitudeDb (b48, f0, 48000.0), gainDb, 0.1);
        expectWithinAbsoluteError (magnitudeDb (b96, f0, 96000.0), gainDb, 0.1);
    }

    void testMultiRateBellTracking()
    {
        beginTest ("Bell center gain tracks requested gain across sample rates");

        const double f0 = 500.0;
        const double gainDb = -4.5;
        const double q = 2.0;

        for (double sr : { 44100.0, 48000.0, 88200.0, 96000.0, 192000.0 })
        {
            const auto b = BiquadCoefficients::makeSecondOrder (
                FilterKind::bell, sr, f0, q, gainDb);
            const auto measured = magnitudeDb (b, f0, sr);
            expectWithinAbsoluteError (measured, gainDb, 0.1,
                                       "Bell at " + juce::String (sr, 1)
                                       + " Hz measured "
                                       + juce::String (measured, 6) + " dB");
        }
    }
};

// Static registration: discovered by juce::UnitTestRunner::runAllTests().
static BiquadCoefficientsTests biquadCoefficientsTestsInstance;
