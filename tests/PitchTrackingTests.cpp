// =============================================================================
// ShredEQ - Pitch Tracking & Note Follower Unit Tests
// =============================================================================
// Covers the public DSP surfaces of Source/DSP/PitchDetector.* and
// Source/DSP/NoteFollower.* (plus a light ParametricBand integration smoke)
// per .abracapocus/SHREDEQ_SPEC.md §9.2–9.5 and §21.7 (Pitch Tracking):
//   * YIN on steady sines near guitar/bass fundamentals (E2/A2/E4) estimates
//     f0 within a documented tolerance once tracking is Locked / high
//     confidence.
//   * Low bass fundamental (~45 Hz) still yields a finite, in-range f0 given
//     the configured analysis window / downsample.
//   * Silence / near-zero noise yields a low-confidence / non-Locked state
//     (no spurious stable high-confidence pitch).
//   * Noise / inharmonic chord-like mixture does not falsely claim a strong
//     monophonic lock (low or unstable confidence is acceptable).
//   * MIDI noteToFrequency and Lowest/Highest/Last priority selection match
//     f0 = 440 * 2^((m-69)/12) exactly within float tolerance.
//   * NoteFollower harmonic/cents targeting, log-smoothed frequency
//     monotonicity, and low-confidence hold-then-fade/return behavior leave
//     frequency finite and approach home/neutral per API defaults.
//   * Optional light ParametricBand check: enabling a follower and processing
//     audio stays finite (no public getter exists for the modulated band
//     center, so the tracking-toward assertion is intentionally skipped per
//     the task's "if public getter exists" clause).
//
// Uses juce::UnitTest only — no third-party test frameworks. Only documented
// public APIs and tolerances are asserted; no production DSP is rewritten.
// =============================================================================

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>

#include "Source/DSP/PitchDetector.h"
#include "Source/DSP/NoteFollower.h"
#include "Source/DSP/ParametricBand.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace
{

using namespace ShredEQ::DSP;

constexpr double kPi = 3.1415926535897932384626433832795;
constexpr double kSampleRate = 44100.0;

// Documented tolerance for steady-sine YIN estimates. The detector conditions
// the signal with a 20 Hz one-pole high-pass and a box-filter decimation to
// ~12 kHz before YIN with parabolic tau refinement. For clean monophonic
// sines this is comfortably inside 2% relative Hz (~34 cents), which is also
// tight enough for EQ band targeting per §9.3.
constexpr double kSteadyRelativeTolerance = 0.02;

// Low-bass tolerance is relaxed slightly because the analysis window / tau
// magnitude grows and parabolic refinement margin grows with it.
constexpr double kLowBassRelativeTolerance = 0.03;

struct CollectedResult
{
    double frequencyHz = 0.0;
    float confidence = 0.0f;
    PitchTrackingState state = PitchTrackingState::searching;
};

// Deterministic linear-congruential noise generator (no std::random dependency
// so results are reproducible from a clean clone).
struct DeterministicNoise
{
    std::uint64_t state = 0x123456789abcdefULL;
    float next() noexcept
    {
        state = state * 6364136223846793005ULL + 1442695040888963407ULL;
        const double u = static_cast<double> (state >> 33) / static_cast<double> (1ULL << 31);
        return static_cast<float> (2.0 * u - 1.0);
    }
};

// Pushes a synthetic steady sine through the detector in chunks, invoking
// processControlRate() after each chunk and collecting every published
// result. Phase is continuous across chunks so the tone is truly steady.
std::vector<CollectedResult> runSteadyTone (
    PitchDetector& detector,
    double frequencyHz,
    int totalSamples,
    int chunkSize,
    float amplitude = 0.3f)
{
    std::vector<float> buffer (static_cast<std::size_t> (chunkSize));
    std::vector<CollectedResult> results;
    double phase = 0.0;
    const double increment = 2.0 * kPi * frequencyHz / kSampleRate;

    int pushed = 0;
    while (pushed < totalSamples)
    {
        const int n = std::min (chunkSize, totalSamples - pushed);
        for (int i = 0; i < n; ++i)
        {
            buffer[static_cast<std::size_t> (i)] =
                amplitude * static_cast<float> (std::sin (phase));
            phase += increment;
        }
        [[maybe_unused]] const auto pushedFrames =
            detector.pushBlock (buffer.data(), n);
        while (detector.processControlRate())
        {
            const auto r = detector.getLatestResult();
            results.push_back ({ r.frequencyHz, r.confidence, r.state });
        }
        pushed += n;
    }

    return results;
}

// Generic chunked push of arbitrary sample data with control-rate draining.
std::vector<CollectedResult> runSignal (
    PitchDetector& detector,
    const std::vector<float>& samples,
    int chunkSize)
{
    std::vector<float> buffer (static_cast<std::size_t> (chunkSize));
    std::vector<CollectedResult> results;
    int pushed = 0;
    const int total = static_cast<int> (samples.size());
    while (pushed < total)
    {
        const int n = std::min (chunkSize, total - pushed);
        for (int i = 0; i < n; ++i)
            buffer[static_cast<std::size_t> (i)] =
                samples[static_cast<std::size_t> (pushed + i)];
        [[maybe_unused]] const auto pushedFrames =
            detector.pushBlock (buffer.data(), n);
        while (detector.processControlRate())
        {
            const auto r = detector.getLatestResult();
            results.push_back ({ r.frequencyHz, r.confidence, r.state });
        }
        pushed += n;
    }
    return results;
}

bool isLockedHighConfidence (const CollectedResult& r, float threshold = 0.8f)
{
    return r.state == PitchTrackingState::locked && r.confidence >= threshold;
}

} // namespace

class PitchTrackingTests final : public juce::UnitTest
{
public:
    PitchTrackingTests()
        : juce::UnitTest ("PitchTrackingTests", "ShredEQ/DSP") {}

    void runTest() override
    {
        testSteadySineGuitarFundamentals();
        testLowBassFundamental();
        testSilenceAndNearZeroNoise();
        testNoiseAndChordLikeMixture();
        testMidiNoteFrequencyExact();
        testMidiPriorityLowestHighestLast();
        testNoteFollowerHarmonicAndCentsTarget();
        testNoteFollowerLogSmoothingMonotonic();
        testNoteFollowerLowConfidenceHoldFade();
        testNoteFollowerLowConfidenceReturnToHome();
        testParametricBandFollowerLightIntegration();
    }

    // -----------------------------------------------------------------------
    // 1. Steady sines near guitar/bass fundamentals.
    // -----------------------------------------------------------------------
    void testSteadySineGuitarFundamentals()
    {
        beginTest ("YIN tracks steady sines near guitar/bass fundamentals");

        // E2 ~82.41 Hz, A2 ~110.00 Hz, E4 ~329.63 Hz per §21.7.
        const struct
        {
            const char* name;
            double frequencyHz;
        } cases[] = {
            { "E2", 82.41 },
            { "A2", 110.00 },
            { "E4", 329.63 }
        };

        for (const auto& c : cases)
        {
            PitchDetector detector;
            detector.prepare (kSampleRate, 512);
            detector.setFrequencyRange (30.0, 1500.0);
            detector.setYinThreshold (0.15);
            detector.setSilenceThreshold (1.0e-4);

            const auto results = runSteadyTone (detector, c.frequencyHz, 14000, 512);

            expect (! results.empty(),
                    juce::String (c.name) + ": no control-rate results published");

            const auto& last = results.back();
            const double relErr = std::abs (last.frequencyHz - c.frequencyHz) / c.frequencyHz;

            // Documented tolerance: 2% relative Hz (~34 cents) for clean sines.
            expect (relErr <= kSteadyRelativeTolerance,
                    juce::String (c.name) + ": f0=" + juce::String (last.frequencyHz, 4)
                    + " expected=" + juce::String (c.frequencyHz, 4)
                    + " relErr=" + juce::String (relErr, 5));

            expect (last.confidence > 0.5f,
                    juce::String (c.name) + ": confidence " + juce::String (last.confidence, 3)
                    + " not high enough");

            expect (last.state == PitchTrackingState::locked,
                    juce::String (c.name) + ": expected Locked tracking state, got state="
                    + juce::String (static_cast<int> (last.state)));
        }
    }

    // -----------------------------------------------------------------------
    // 2. Low bass fundamental (~45 Hz).
    // -----------------------------------------------------------------------
    void testLowBassFundamental()
    {
        beginTest ("Low bass fundamental (~45 Hz) yields finite in-range f0");

        // 45 Hz is above the default 30 Hz configured minimum, so the
        // prepare-time window / downsample supports it.
        PitchDetector detector;
        detector.prepare (kSampleRate, 512);
        detector.setFrequencyRange (30.0, 1500.0);
        detector.setYinThreshold (0.15);
        detector.setSilenceThreshold (1.0e-4);

        const double targetHz = 45.0;
        const auto results = runSteadyTone (detector, targetHz, 16000, 512);

        expect (! results.empty(), "Low bass: no results published");

        const auto& last = results.back();
        expect (std::isfinite (last.frequencyHz),
                "Low bass: f0 not finite");
        expect (last.frequencyHz > 0.0,
                "Low bass: f0 must be positive when signal is tonal");
        expect (last.state != PitchTrackingState::silent,
                "Low bass: must not be classified silent for a tonal input");

        const double relErr = std::abs (last.frequencyHz - targetHz) / targetHz;
        // Documented relaxed tolerance for low bass.
        expect (relErr <= kLowBassRelativeTolerance,
                "Low bass: f0=" + juce::String (last.frequencyHz, 4)
                + " relErr=" + juce::String (relErr, 5));
    }

    // -----------------------------------------------------------------------
    // 3. Silence / near-zero noise → low confidence, non-Locked.
    // -----------------------------------------------------------------------
    void testSilenceAndNearZeroNoise()
    {
        beginTest ("Silence / near-zero noise yields low-confidence non-Locked state");

        {
            PitchDetector detector;
            detector.prepare (kSampleRate, 512);
            detector.setFrequencyRange (30.0, 1500.0);
            detector.setSilenceThreshold (1.0e-4);

            std::vector<float> silence (12000, 0.0f);
            const auto results = runSignal (detector, silence, 512);

            expect (! results.empty(), "Silence: no results published");
            for (const auto& r : results)
            {
                expect (r.state != PitchTrackingState::locked,
                        "Silence: must not reach Locked state");
                expect (r.confidence <= 0.2f,
                        "Silence: confidence must be low, got "
                        + juce::String (r.confidence, 4));
            }
            const auto& last = results.back();
            expect (last.state == PitchTrackingState::silent,
                    "Silence: expected Silent state, got "
                    + juce::String (static_cast<int> (last.state)));
            expectWithinAbsoluteError (last.frequencyHz, 0.0, 1.0e-9,
                                       "Silence: f0 must be zero");
        }

        {
            // Near-zero noise whose RMS stays below the silence threshold.
            PitchDetector detector;
            detector.prepare (kSampleRate, 512);
            detector.setFrequencyRange (30.0, 1500.0);
            detector.setSilenceThreshold (1.0e-4);

            DeterministicNoise noise;
            std::vector<float> quiet (12000);
            for (auto& s : quiet)
                s = noise.next() * 1.0e-5f; // RMS ~ 3e-6, well below 1e-4

            const auto results = runSignal (detector, quiet, 512);
            expect (! results.empty(), "Near-zero noise: no results published");
            for (const auto& r : results)
            {
                expect (r.state != PitchTrackingState::locked,
                        "Near-zero noise: must not reach Locked");
                expect (r.confidence <= 0.2f,
                        "Near-zero noise: confidence must be low");
            }
        }
    }

    // -----------------------------------------------------------------------
    // 4. Noise / chord-like mixture must not falsely claim strong lock.
    // -----------------------------------------------------------------------
    void testNoiseAndChordLikeMixture()
    {
        beginTest ("Noise and inharmonic chord mixture do not claim strong monophonic lock");

        // Deterministic white noise: YIN should find no stable periodicity.
        {
            PitchDetector detector;
            detector.prepare (kSampleRate, 512);
            detector.setFrequencyRange (30.0, 1500.0);
            detector.setYinThreshold (0.15);
            detector.setSilenceThreshold (1.0e-4);

            DeterministicNoise noise;
            std::vector<float> noiseSignal (14000);
            for (auto& s : noiseSignal)
                s = noise.next() * 0.3f;

            const auto results = runSignal (detector, noiseSignal, 512);
            expect (! results.empty(), "Noise: no results published");

            int lockedCount = 0;
            for (const auto& r : results)
                if (isLockedHighConfidence (r))
                    ++lockedCount;

            // Acceptance: low or unstable confidence. A sustained strong lock
            // on white noise would be a false monophonic claim.
            expect (lockedCount < static_cast<int> (results.size()),
                    "Noise: every result was strong-Locked (false monophonic claim)");
            expect (lockedCount < static_cast<int> (results.size()) / 2,
                    "Noise: too many strong-Locked results ("
                    + juce::String (lockedCount) + "/"
                    + juce::String (static_cast<int> (results.size())) + ")");
        }

        // Polyphonic chord-like mixture: a major chord (A2 / C#3 / E3) with
        // two harmonics per note. This is the §9.2 "Does not claim chord
        // tracking" case — a monophonic YIN detector must not falsely report
        // a strong, stable monophonic lock on a harmonically dense chord.
        // Empirically the dense spectrum produces no clean CMND dip below the
        // YIN threshold, so the detector reports low confidence rather than
        // a fabricated fundamental.
        {
            PitchDetector detector;
            detector.prepare (kSampleRate, 512);
            detector.setFrequencyRange (30.0, 1500.0);
            detector.setYinThreshold (0.15);
            detector.setSilenceThreshold (1.0e-4);

            const double chordFreqs[] = { 110.0, 220.0,   // A2 + 2nd harmonic
                                          138.6, 277.2,   // C#3 + 2nd harmonic
                                          164.8, 329.6 }; // E3 + 2nd harmonic
            std::vector<float> mixture (14000);
            for (int n = 0; n < 14000; ++n)
            {
                const double t = static_cast<double> (n) / kSampleRate;
                double v = 0.0;
                for (const double f : chordFreqs)
                    v += std::sin (2.0 * kPi * f * t);
                mixture[static_cast<std::size_t> (n)] = static_cast<float> (0.08 * v);
            }

            const auto results = runSignal (detector, mixture, 512);
            expect (! results.empty(), "Chord mixture: no results published");

            int lockedCount = 0;
            int lowConfOrSilentCount = 0;
            for (const auto& r : results)
            {
                if (isLockedHighConfidence (r))
                    ++lockedCount;
                if (r.state == PitchTrackingState::lowConfidence
                    || r.state == PitchTrackingState::silent
                    || r.state == PitchTrackingState::hold)
                    ++lowConfOrSilentCount;
            }

            // Acceptance (§21.7 / §9.2): low or unstable confidence is
            // acceptable; a sustained strong monophonic lock on a polyphonic
            // chord is the false claim we reject.
            expect (lockedCount == 0,
                    "Chord mixture: detector falsely strong-Locked on polyphonic input ("
                    + juce::String (lockedCount) + "/"
                    + juce::String (static_cast<int> (results.size())) + ")");
            expect (lowConfOrSilentCount > 0,
                    "Chord mixture: expected low-confidence/silent/hold states, got none");
        }
    }

    // -----------------------------------------------------------------------
    // 5. MIDI noteToFrequency exact per f0 = 440 * 2^((m-69)/12).
    // -----------------------------------------------------------------------
    void testMidiNoteFrequencyExact()
    {
        beginTest ("MIDI noteToFrequency matches 440*2^((m-69)/12) exactly");

        const int notes[] = { 0, 57, 60, 64, 67, 69, 72, 81, 96, 127 };

        for (const int m : notes)
        {
            const double expected = 440.0 * std::exp2 ((static_cast<double> (m) - 69.0) / 12.0);
            const double actualStatic = PitchDetector::midiNoteToFrequency (m);
            const double actualMember = PitchDetector::MidiTracker::midiNoteToFrequency (m);

            expectWithinAbsoluteError (actualStatic, expected, 1.0e-6,
                                       "Static midiNoteToFrequency(" + juce::String (m)
                                       + ") mismatch");
            expectWithinAbsoluteError (actualMember, expected, 1.0e-6,
                                       "MidiTracker::midiNoteToFrequency(" + juce::String (m)
                                       + ") mismatch");
        }

        // Out-of-range notes return 0 deterministically.
        expectWithinAbsoluteError (PitchDetector::midiNoteToFrequency (-1), 0.0, 0.0,
                                   "Negative MIDI note must map to 0");
        expectWithinAbsoluteError (PitchDetector::midiNoteToFrequency (128), 0.0, 0.0,
                                   "MIDI note > 127 must map to 0");

        // Spot-check standard tuning anchors.
        expectWithinAbsoluteError (PitchDetector::midiNoteToFrequency (69), 440.0, 1.0e-9,
                                   "A4 (69) must be 440 Hz");
        expectWithinAbsoluteError (PitchDetector::midiNoteToFrequency (81), 880.0, 1.0e-9,
                                   "A5 (81) must be 880 Hz");
        expectWithinAbsoluteError (PitchDetector::midiNoteToFrequency (57), 220.0, 1.0e-9,
                                   "A3 (57) must be 220 Hz");
    }

    // -----------------------------------------------------------------------
    // 5b. MIDI priority Lowest / Highest / Last.
    // -----------------------------------------------------------------------
    void testMidiPriorityLowestHighestLast()
    {
        beginTest ("MIDI priority Lowest/Highest/Last selects expected held note");

        PitchDetector::MidiTracker tracker;
        tracker.reset();

        // No notes held → -1, frequency 0.
        expect (tracker.getSelectedNote() == -1, "Empty tracker must return -1");
        expectWithinAbsoluteError (tracker.getFrequencyHz(), 0.0, 1.0e-9,
                                   "Empty tracker frequency must be 0");

        // Hold a C-E-G chord: 60, 64, 67.
        tracker.noteOn (60);
        tracker.noteOn (64);
        tracker.noteOn (67);

        tracker.setPriority (PitchDetector::MidiPriority::lowest);
        expect (tracker.getSelectedNote() == 60, "Lowest must select 60");
        expectWithinAbsoluteError (
            tracker.getFrequencyHz(),
            PitchDetector::midiNoteToFrequency (60), 1.0e-9,
            "Lowest frequency mismatch");

        tracker.setPriority (PitchDetector::MidiPriority::highest);
        expect (tracker.getSelectedNote() == 67, "Highest must select 67");
        expectWithinAbsoluteError (
            tracker.getFrequencyHz(),
            PitchDetector::midiNoteToFrequency (67), 1.0e-9,
            "Highest frequency mismatch");

        tracker.setPriority (PitchDetector::MidiPriority::last);
        // Last pressed that remains held is 67.
        expect (tracker.getSelectedNote() == 67, "Last must select most recent (67)");

        // Release 67 → most recent still held is 64.
        tracker.noteOff (67);
        expect (tracker.getSelectedNote() == 64,
                "Last after noteOff(67) must select 64");

        tracker.setPriority (PitchDetector::MidiPriority::lowest);
        expect (tracker.getSelectedNote() == 60, "Lowest after release must be 60");

        tracker.setPriority (PitchDetector::MidiPriority::highest);
        expect (tracker.getSelectedNote() == 64, "Highest after release must be 64");

        // Release everything.
        tracker.noteOff (64);
        tracker.noteOff (60);
        expect (tracker.getSelectedNote() == -1, "All released must return -1");
        expect (tracker.getPriority() == PitchDetector::MidiPriority::highest,
                "Priority getter must round-trip");

        // Out-of-range note events are ignored and do not corrupt state.
        tracker.noteOn (-5);
        tracker.noteOn (200);
        expect (tracker.getSelectedNote() == -1,
                "Out-of-range noteOn must be ignored");
    }

    // -----------------------------------------------------------------------
    // 6. NoteFollower harmonic / cents targeting.
    // -----------------------------------------------------------------------
    void testNoteFollowerHarmonicAndCentsTarget()
    {
        beginTest ("NoteFollower harmonic/cents targeting matches f_target = f0*h*2^(c/1200)");

        NoteFollower follower;
        follower.prepare (kSampleRate);
        follower.setEnabled (true);
        follower.setTrackingSource (0); // AudioMonophonic
        follower.setConfidenceThreshold (0.5);
        follower.setSmoothing (0.0); // immediate, no log lag
        follower.setHomeFrequencyHz (1000.0);
        follower.setFrequencyLimits (20.0, 20000.0);

        // h=2, c=0 → f_target ≈ 2 * f0.
        follower.setHarmonic (2);
        follower.setCentsOffset (0.0);
        follower.update (110.0, 1.0f, 64);
        expectWithinAbsoluteError (
            follower.getSmoothedFrequencyHz(), 220.0, 1.0e-3,
            "h=2,c=0 must yield ~2*f0");

        // h=1, c=+100 → multiply by 2^(100/1200).
        follower.setHarmonic (1);
        follower.setCentsOffset (100.0);
        follower.update (110.0, 1.0f, 64);
        const double expectedCents = 110.0 * std::pow (2.0, 100.0 / 1200.0);
        expectWithinAbsoluteError (
            follower.getSmoothedFrequencyHz(), expectedCents, 1.0e-3,
            "h=1,c=+100 must yield f0*2^(100/1200)");

        // h=1, c=-1200 → one octave down (exactly f0/2).
        follower.setHarmonic (1);
        follower.setCentsOffset (-1200.0);
        follower.update (440.0, 1.0f, 64);
        expectWithinAbsoluteError (
            follower.getSmoothedFrequencyHz(), 220.0, 1.0e-3,
            "c=-1200 must drop one octave");

        // Output must always be finite.
        expect (std::isfinite (follower.getSmoothedFrequencyHz()),
                "Smoothed frequency must be finite");
        expect (follower.getFollowGain() == 1.0f,
                "Follow gain must be 1.0 while confidently tracking");
        expect (! follower.isBypassed(),
                "Follower must not be bypassed while confidently tracking");
    }

    // -----------------------------------------------------------------------
    // 6b. NoteFollower log smoothing moves monotonically toward target.
    // -----------------------------------------------------------------------
    void testNoteFollowerLogSmoothingMonotonic()
    {
        beginTest ("NoteFollower log smoothing moves monotonically toward target");

        NoteFollower follower;
        follower.prepare (kSampleRate);
        follower.setEnabled (true);
        follower.setHarmonic (1);
        follower.setCentsOffset (0.0);
        follower.setSmoothing (50.0); // nonzero → beta in (0,1)
        follower.setHomeFrequencyHz (1000.0);
        follower.setFrequencyLimits (20.0, 20000.0);
        follower.setConfidenceThreshold (0.5);

        // Establish a starting point at the home frequency.
        follower.update (1000.0, 1.0f, 64);
        double previous = follower.getSmoothedFrequencyHz();
        expect (std::isfinite (previous), "Starting frequency must be finite");

        // Step the target down to 220 Hz; log smoothing must move monotonically
        // toward it without overshooting past the target.
        constexpr int kSteps = 60;
        for (int i = 0; i < kSteps; ++i)
        {
            follower.update (220.0, 1.0f, 64);
            const double current = follower.getSmoothedFrequencyHz();

            expect (std::isfinite (current), "Smoothed frequency must stay finite");
            expect (current <= previous + 1.0e-9,
                    "Smoothing must be monotonic non-increasing toward lower target"
                    " (step " + juce::String (i) + ", cur="
                    + juce::String (current, 6) + ", prev="
                    + juce::String (previous, 6) + ")");
            previous = current;
        }

        // Must have moved measurably toward 220 Hz but remain above it.
        expect (previous < 1000.0, "Smoothing must move away from home toward target");
        expect (previous > 220.0, "Smoothing must not overshoot below the target");
        expect (previous > 220.0 && previous < 700.0,
                "Smoothing should be progressing toward target, got "
                + juce::String (previous, 4));
    }

    // -----------------------------------------------------------------------
    // 6c. NoteFollower low-confidence hold-then-fade (default FadeToNeutral).
    // -----------------------------------------------------------------------
    void testNoteFollowerLowConfidenceHoldFade()
    {
        beginTest ("NoteFollower low-confidence hold-then-fade keeps frequency finite");

        NoteFollower follower;
        follower.prepare (kSampleRate);
        follower.setEnabled (true);
        follower.setHarmonic (1);
        follower.setCentsOffset (0.0);
        follower.setSmoothing (0.0);
        follower.setHomeFrequencyHz (1000.0);
        follower.setFrequencyLimits (20.0, 20000.0);
        follower.setConfidenceThreshold (0.5);
        follower.setHoldTimeMs (100.0);
        follower.setReturnTimeMs (200.0);
        follower.setLowConfidenceBehavior (NoteFollower::LowConfidenceBehavior::fadeToNeutral);

        // Establish a valid tracked frequency.
        follower.update (220.0, 1.0f, 64);
        expectWithinAbsoluteError (follower.getSmoothedFrequencyHz(), 220.0, 1.0e-3,
                                   "Established target must be 2*f0");
        expect (follower.getFollowGain() == 1.0f, "Gain must be 1.0 while tracking");

        // 64 samples ≈ 1.45 ms; 30 calls ≈ 43 ms < 100 ms hold → still holding.
        for (int i = 0; i < 30; ++i)
        {
            follower.update (0.0, 0.0f, 64); // confidence below threshold
            expect (std::isfinite (follower.getSmoothedFrequencyHz()),
                    "Frequency must remain finite during hold");
            expect (follower.getFollowGain() == 1.0f,
                    "Gain must remain 1.0 during hold phase");
        }

        // Continue past hold into the fade phase.
        for (int i = 0; i < 220; ++i)
        {
            follower.update (0.0, 0.0f, 64);
            expect (std::isfinite (follower.getSmoothedFrequencyHz()),
                    "Frequency must remain finite during fade");
            expect (follower.getFollowGain() >= 0.0f
                    && follower.getFollowGain() <= 1.0f + 1.0e-6f,
                    "Gain must stay in [0,1] during fade");
        }

        // After hold (~100 ms) + return (~200 ms) the gain must have faded
        // toward neutral (≈0). Total pushed here is well beyond 300 ms.
        expect (follower.getFollowGain() <= 0.01f,
                "Gain must fade toward 0 after hold+return, got "
                + juce::String (follower.getFollowGain(), 5));
    }

    // -----------------------------------------------------------------------
    // 6d. NoteFollower low-confidence ReturnToHome approaches home.
    // -----------------------------------------------------------------------
    void testNoteFollowerLowConfidenceReturnToHome()
    {
        beginTest ("NoteFollower low-confidence ReturnToHome approaches home frequency");

        NoteFollower follower;
        follower.prepare (kSampleRate);
        follower.setEnabled (true);
        follower.setHarmonic (1);
        follower.setCentsOffset (0.0);
        follower.setSmoothing (0.0);
        follower.setHomeFrequencyHz (1000.0);
        follower.setFrequencyLimits (20.0, 20000.0);
        follower.setConfidenceThreshold (0.5);
        follower.setHoldTimeMs (100.0);
        follower.setReturnTimeMs (200.0);
        follower.setLowConfidenceBehavior (NoteFollower::LowConfidenceBehavior::returnToHome);

        follower.update (220.0, 1.0f, 64);
        expectWithinAbsoluteError (follower.getSmoothedFrequencyHz(), 220.0, 1.0e-3,
                                   "Established target must be 220 Hz");

        // Run well past hold (100 ms) + return (200 ms). With smoothing=0 the
        // smoothed frequency equals currentTargetFreq, which interpolates from
        // the last valid target toward the home frequency.
        for (int i = 0; i < 260; ++i)
        {
            follower.update (0.0, 0.0f, 64);
            const double f = follower.getSmoothedFrequencyHz();
            expect (std::isfinite (f), "Frequency must remain finite during return");
            expect (f <= 1000.0 + 1.0e-6,
                    "ReturnToHome must not overshoot above home frequency");
        }

        expectWithinAbsoluteError (follower.getSmoothedFrequencyHz(), 1000.0, 1.0,
                                   "Frequency must approach home after hold+return");
        expect (follower.getFollowGain() == 1.0f,
                "ReturnToHome keeps gain at 1.0 (only target moves)");
    }

    // -----------------------------------------------------------------------
    // 7. Optional light ParametricBand follower integration.
    // -----------------------------------------------------------------------
    void testParametricBandFollowerLightIntegration()
    {
        beginTest ("ParametricBand with attached follower processes audio finitely");

        // No public getter exists for the modulated band-center frequency
        // (smoothedFrequency is private). Per the task's "if public getter
        // exists" clause, we skip the tracking-toward assertion and only
        // smoke-test that enabling the follower keeps processing finite.

        NoteFollower externalFollower;
        externalFollower.prepare (kSampleRate);
        externalFollower.setEnabled (true);
        externalFollower.setHarmonic (1);
        externalFollower.setCentsOffset (0.0);
        externalFollower.setSmoothing (0.0);
        externalFollower.setHomeFrequencyHz (1000.0);
        externalFollower.setFrequencyLimits (20.0, 20000.0);
        externalFollower.setConfidenceThreshold (0.5);
        externalFollower.update (880.0, 1.0f, 64);
        expect (std::isfinite (externalFollower.getSmoothedFrequencyHz()),
                "External follower must have a finite smoothed frequency");

        ParametricBand band;
        band.prepare (kSampleRate, 512);
        band.reset();
        band.attachNoteFollower (&externalFollower);
        band.setNoteFollowerEnabled (true);
        band.setEnabled (true);
        band.setFilterType (BandFilterType::bell);
        band.setFrequency (1000.0);
        band.setGainDb (3.0);
        band.setQ (1.0);
        band.setRoutingMode (StereoRoutingMode::linked);

        juce::AudioBuffer<float> buffer (2, 256);
        buffer.clear();
        for (int n = 0; n < 256; ++n)
            buffer.setSample (0, n, 0.3f * static_cast<float> (std::sin (2.0 * kPi * 880.0 * n / kSampleRate)));

        band.process (buffer, false);

        for (int ch = 0; ch < 2; ++ch)
            for (int n = 0; n < 256; ++n)
            {
                const float v = buffer.getSample (ch, n);
                expect (std::isfinite (v),
                        "Band output must be finite at [" + juce::String (ch)
                        + "," + juce::String (n) + "]");
                expect (std::abs (v) < 10.0f,
                        "Band output must be bounded at [" + juce::String (ch)
                        + "," + juce::String (n) + "]");
            }
    }
};

// Static registration: discovered by juce::UnitTestRunner::runAllTests().
static PitchTrackingTests pitchTrackingTestsInstance;
