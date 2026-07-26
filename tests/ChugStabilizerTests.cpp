// =============================================================================
// ShredEQ - ChugStabilizer Unit Tests
// =============================================================================
// Covers the public DSP surface of Source/DSP/ChugStabilizer per
// .abracapocus/SHREDEQ_SPEC.md §7 (Chug Stabilizer controls/detection) and
// §21.5 chug fixtures intent:
//   * Synthetic palm-mute-like low burst produces measurable gain reduction
//     within maxReduction
//   * Steady open-chord-like mid-dominant tone is not permanently thinned
//     (GR near 0 after settle, far below the burst case)
//   * Preserve Pick reduces GR for a pick-like broadband transient with normal
//     low-band dominance versus Preserve Pick off
//   * Attack/hold/release influence timing: faster attack reaches GR sooner on
//     a step burst; release recovers toward 0 after the burst ends
//   * Disabled module approximates bypass (output matches input within tol)
//   * Visualization getters report reduction and a defined trigger source when
//     an event fires
//
// Uses juce::UnitTest only — no Catch2/Google Test. Only public APIs are
// exercised; no production DSP code is rewritten.
// =============================================================================

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>

#include "Source/DSP/ChugStabilizer.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace
{

using namespace ShredEQ::DSP;

constexpr double kSampleRate = 48000.0;
constexpr double kPi = 3.14159265358979323846;

// Build a 2-channel buffer with both channels carrying the same mono signal
// described by `fillSample(n, sr)`.
template <typename Fill>
juce::AudioBuffer<float> makeMonoBuffer (int numSamples, Fill fillSample)
{
    juce::AudioBuffer<float> buffer (2, numSamples);
    buffer.clear();
    for (int n = 0; n < numSamples; ++n)
    {
        const auto v = static_cast<float> (fillSample (n, kSampleRate));
        buffer.setSample (0, n, v);
        buffer.setSample (1, n, v);
    }
    return buffer;
}

// A gated sine burst: amplitude `amp` of `frequencyHz` sine for the first
// `burstSamples` samples, then silence.
struct GatedBurst
{
    double frequencyHz;
    double amplitude;
    int burstSamples;

    double operator() (int n, double sr) const
    {
        if (n >= burstSamples)
            return 0.0;
        return amplitude * std::sin (2.0 * kPi * frequencyHz
                                     * static_cast<double> (n) / sr);
    }
};

// Process the stabilizer sample-by-sample (1-sample blocks) so we can capture
// the per-sample gain-reduction / event trajectory through the public getters.
struct Trace
{
    std::vector<float> grDb;
    std::vector<bool> event;
    std::vector<ChugTriggerSource> trigger;
};

Trace traceProcess (ChugStabilizer& cs, const juce::AudioBuffer<float>& input)
{
    Trace t;
    t.grDb.reserve (static_cast<std::size_t> (input.getNumSamples()));
    t.event.reserve (static_cast<std::size_t> (input.getNumSamples()));
    t.trigger.reserve (static_cast<std::size_t> (input.getNumSamples()));

    juce::AudioBuffer<float> tmp (2, 1);
    for (int n = 0; n < input.getNumSamples(); ++n)
    {
        tmp.setSample (0, 0, input.getSample (0, n));
        tmp.setSample (1, 0, input.getSample (1, n));
        cs.process (tmp);

        t.grDb.push_back (cs.getLastGainReductionDb());
        t.event.push_back (cs.getDetectedEvent());
        t.trigger.push_back (cs.getTriggerSource());
    }
    return t;
}

float maxGr (const Trace& t)
{
    float m = 0.0f;
    for (auto v : t.grDb)
        m = std::max (m, v);
    return m;
}

} // namespace

// =============================================================================

class ChugStabilizerTests final : public juce::UnitTest
{
public:
    ChugStabilizerTests()
        : juce::UnitTest ("ChugStabilizerTests", "ShredEQ/DSP") {}

    void runTest() override
    {
        testBurstProducesGainReduction();
        testSteadyMidToneNotThinned();
        testPreservePickReducesGr();
        testAttackReleaseTiming();
        testDisabledBypass();
        testVisualizationGetters();
    }

    // -----------------------------------------------------------------------

    void testBurstProducesGainReduction()
    {
        beginTest ("ChugStabilizer: low burst produces non-unity GR within maxReduction");

        ChugStabilizer cs;
        cs.prepare (kSampleRate, 256);
        cs.setEnabled (true);
        cs.setTargetFrequency (120.0);
        cs.setBandwidthOctaves (1.2);
        cs.setMaximumReductionDb (8.0);
        cs.setAutomaticThreshold (true);
        cs.setAttackMs (3.0);
        cs.setHoldMs (25.0);
        cs.setReleaseMs (100.0);

        // Strong low-band burst (palm-mute-like): 100 Hz at near-unity for
        // ~10 ms, then silence.
        const int burstSamples = static_cast<int> (0.010 * kSampleRate);
        const auto input = makeMonoBuffer (4000, GatedBurst { 100.0, 0.9, burstSamples });
        const auto trace = traceProcess (cs, input);

        const auto peakGr = maxGr (trace);
        const auto maxReduction = static_cast<float> (cs.getMaximumReductionDb());

        expect (std::isfinite (peakGr),
                "Peak GR must be finite");
        expect (peakGr > 1.0f,
                "Burst must produce meaningful gain reduction; peakGr="
                + juce::String (peakGr));
        expect (peakGr <= maxReduction + 0.01f,
                "GR must be capped by maxReduction; peakGr="
                + juce::String (peakGr) + " max=" + juce::String (maxReduction));
    }

    // -----------------------------------------------------------------------

    void testSteadyMidToneNotThinned()
    {
        beginTest ("ChugStabilizer: steady mid-dominant tone is not permanently thinned");

        ChugStabilizer cs;
        cs.prepare (kSampleRate, 256);
        cs.setEnabled (true);
        cs.setTargetFrequency (120.0);
        cs.setBandwidthOctaves (1.2);
        cs.setMaximumReductionDb (8.0);
        cs.setAutomaticThreshold (true);
        cs.setAttackMs (3.0);
        cs.setHoldMs (25.0);
        cs.setReleaseMs (100.0);
        cs.setStaticSupportGainDb (0.0);

        // Steady mid-dominant open-chord-like tone: 800 Hz sine at moderate
        // amplitude, long enough to settle fully.
        const auto input = makeMonoBuffer (20000, [] (int n, double sr)
        {
            return 0.5 * std::sin (2.0 * kPi * 800.0
                                    * static_cast<double> (n) / sr);
        });

        const auto trace = traceProcess (cs, input);

        // Final GR after settle.
        const auto finalGr = trace.grDb.back();
        expect (std::isfinite (finalGr));
        expect (finalGr < 0.1f,
                "Steady mid tone must not accumulate reduction; finalGr="
                + juce::String (finalGr));

        // Burst case for comparison: must be far above the steady case.
        ChugStabilizer cs2;
        cs2.prepare (kSampleRate, 256);
        cs2.setEnabled (true);
        cs2.setTargetFrequency (120.0);
        cs2.setMaximumReductionDb (8.0);
        const int burstSamples = static_cast<int> (0.010 * kSampleRate);
        const auto burst = makeMonoBuffer (4000, GatedBurst { 100.0, 0.9, burstSamples });
        const auto burstTrace = traceProcess (cs2, burst);
        const auto burstPeak = maxGr (burstTrace);

        expect (burstPeak > 1.0f,
                "Reference burst must produce GR for comparison; got "
                + juce::String (burstPeak));
        expect (finalGr < burstPeak * 0.1f,
                "Steady GR must be far below burst GR; finalGr="
                + juce::String (finalGr) + " burstPeak=" + juce::String (burstPeak));
    }

    // -----------------------------------------------------------------------

    void testPreservePickReducesGr()
    {
        beginTest ("ChugStabilizer: Preserve Pick reduces GR for pick-like broadband transient");

        // Pick-like broadband transient with normal (non-dominant) low-band
        // energy: a short burst mixing a low and a mid component so the
        // broadband transient detector fires while low-band dominance is
        // ~neutral (dominanceTerm <= 0).
        auto renderPick = [] (bool preservePick)
        {
            ChugStabilizer cs;
            cs.prepare (kSampleRate, 256);
            cs.setEnabled (true);
            cs.setTargetFrequency (120.0);
            cs.setBandwidthOctaves (1.2);
            cs.setMaximumReductionDb (8.0);
            cs.setAutomaticThreshold (true);
            cs.setAttackMs (3.0);
            cs.setHoldMs (25.0);
            cs.setReleaseMs (100.0);
            cs.setPreservePick (preservePick);
            cs.setTransientSensitivity (0.5);
            cs.setDominanceSensitivity (0.5);

            const int burstSamples = static_cast<int> (0.0017 * kSampleRate); // ~80 samples
            const auto input = makeMonoBuffer (4000, [burstSamples] (int n, double sr)
            {
                if (n >= burstSamples)
                    return 0.0;
                return 0.4 * std::sin (2.0 * kPi * 100.0
                                       * static_cast<double> (n) / sr)
                     + 0.6 * std::sin (2.0 * kPi * 800.0
                                       * static_cast<double> (n) / sr);
            });
            return maxGr (traceProcess (cs, input));
        };

        const auto grOff = renderPick (false);
        const auto grOn = renderPick (true);

        expect (std::isfinite (grOff) && std::isfinite (grOn));
        expect (grOff > 0.1f,
                "Pick-like transient must still produce some GR with Preserve Pick off; grOff="
                + juce::String (grOff));
        expect (grOn < grOff,
                "Preserve Pick on must reduce GR versus off; on="
                + juce::String (grOn) + " off=" + juce::String (grOff));
    }

    // -----------------------------------------------------------------------

    void testAttackReleaseTiming()
    {
        beginTest ("ChugStabilizer: faster attack reaches GR sooner; release recovers after burst");

        auto runTiming = [] (double attackMs)
        {
            ChugStabilizer cs;
            cs.prepare (kSampleRate, 256);
            cs.setEnabled (true);
            cs.setTargetFrequency (120.0);
            cs.setBandwidthOctaves (1.2);
            cs.setMaximumReductionDb (8.0);
            cs.setAutomaticThreshold (true);
            cs.setAttackMs (attackMs);
            cs.setHoldMs (25.0);
            cs.setReleaseMs (80.0);

            // Step into a sustained strong low tone, then cut to silence.
            const int onSamples = static_cast<int> (0.060 * kSampleRate); // 60 ms
            const int offSamples = static_cast<int> (0.200 * kSampleRate); // 200 ms
            const auto input = makeMonoBuffer (onSamples + offSamples,
                                              [onSamples] (int n, double sr)
            {
                if (n >= onSamples)
                    return 0.0;
                return 0.9 * std::sin (2.0 * kPi * 100.0
                                        * static_cast<double> (n) / sr);
            });
            return traceProcess (cs, input);
        };

        const auto fast = runTiming (0.5);   // 0.5 ms attack
        const auto slow = runTiming (20.0);  // 20 ms attack

        // Intermediate window: ~1.3 ms in (64 samples). Fast attack should
        // be measurably ahead of slow attack.
        const int probe = 64;
        expect (probe < static_cast<int> (fast.grDb.size()));
        expect (probe < static_cast<int> (slow.grDb.size()));
        const auto fastGr = fast.grDb[probe];
        const auto slowGr = slow.grDb[probe];

        expect (fastGr > slowGr,
                "Fast attack should reach higher GR sooner than slow attack at sample "
                + juce::String (probe) + "; fast=" + juce::String (fastGr)
                + " slow=" + juce::String (slowGr));

        // Release: after the burst ends, GR must recover toward 0.
        const auto peakFast = maxGr (fast);
        const auto tailFast = fast.grDb.back();
        expect (std::isfinite (tailFast));
        expect (tailFast < peakFast * 0.5f,
                "Release must decay GR well below peak after silence; peak="
                + juce::String (peakFast) + " tail=" + juce::String (tailFast));
    }

    // -----------------------------------------------------------------------

    void testDisabledBypass()
    {
        beginTest ("ChugStabilizer: disabled module approximates bypass");

        ChugStabilizer cs;
        cs.prepare (kSampleRate, 256);
        cs.setEnabled (false); // bypassed

        const auto input = makeMonoBuffer (2048, [] (int n, double sr)
        {
            return 0.4 * std::sin (2.0 * kPi * 110.0
                                    * static_cast<double> (n) / sr);
        });

        // Process in one block (disabled path passes samples through untouched).
        juce::AudioBuffer<float> out (2, input.getNumSamples());
        for (int ch = 0; ch < 2; ++ch)
            out.copyFrom (ch, 0, input, ch, 0, input.getNumSamples());
        cs.process (out);

        double maxDiff = 0.0;
        for (int ch = 0; ch < 2; ++ch)
        {
            for (int n = 0; n < input.getNumSamples(); ++n)
            {
                const auto diff = std::abs (out.getSample (ch, n)
                                            - input.getSample (ch, n));
                maxDiff = std::max (maxDiff, static_cast<double> (diff));
                expect (std::isfinite (out.getSample (ch, n)),
                        "Disabled output must be finite");
            }
        }

        expect (maxDiff < 1.0e-6,
                "Disabled module must pass input through; maxDiff=" + juce::String (maxDiff));
    }

    // -----------------------------------------------------------------------

    void testVisualizationGetters()
    {
        beginTest ("ChugStabilizer: visualization getters report reduction and trigger on event");

        ChugStabilizer cs;
        cs.prepare (kSampleRate, 256);
        cs.setEnabled (true);
        cs.setTargetFrequency (120.0);
        cs.setBandwidthOctaves (1.2);
        cs.setMaximumReductionDb (8.0);
        cs.setAutomaticThreshold (true);
        cs.setAttackMs (3.0);
        cs.setHoldMs (25.0);
        cs.setReleaseMs (100.0);

        const int burstSamples = static_cast<int> (0.020 * kSampleRate);
        const auto input = makeMonoBuffer (6000, GatedBurst { 100.0, 0.9, burstSamples });

        bool sawEvent = false;
        bool sawReduction = false;
        bool sawDefinedTrigger = false;
        ChugTriggerSource triggerWhenEvent = ChugTriggerSource::none;

        juce::AudioBuffer<float> tmp (2, 1);
        for (int n = 0; n < input.getNumSamples(); ++n)
        {
            tmp.setSample (0, 0, input.getSample (0, n));
            tmp.setSample (1, 0, input.getSample (1, n));
            cs.process (tmp);

            const auto vis = cs.getVisualizationData();
            if (vis.detectedEvent)
            {
                sawEvent = true;
                triggerWhenEvent = vis.triggerSource;
                if (vis.triggerSource != ChugTriggerSource::none)
                    sawDefinedTrigger = true;
            }
            if (vis.lastGainReductionDb > 0.1f)
                sawReduction = true;
        }

        expect (sawEvent,
                "Visualization must report a detected event during a strong low burst");
        expect (sawReduction,
                "Visualization must report non-zero reduction during a burst");
        expect (sawDefinedTrigger,
                "Visualization must report a defined trigger source when an event fires; got "
                + juce::String (static_cast<int> (triggerWhenEvent)));
        expect (cs.getTriggerSource() == triggerWhenEvent || ! sawEvent,
                "getTriggerSource() must agree with visualization trigger at event time");
    }
};

// Static registration: discovered by juce::UnitTestRunner::runAllTests().
static ChugStabilizerTests chugStabilizerTestsInstance;
