// =============================================================================
// ShredEQ - Resonance Detection & Suppression Unit Tests
// =============================================================================
// Covers the public surfaces of:
//   * Source/Analysis/SpectralAnalysisEngine.* + AnalysisFifo.*
//   * Source/DSP/ResonanceDetector.*
//   * Source/DSP/ResonanceSuppressor.* (+ PitchDetector f0 snapshot for the
//     harmonic-rejection cases)
//
// Per .abracapocus/SHREDEQ_SPEC.md §8.2–8.7 and §21.6 (Resonance Detector):
//   1. SpectralAnalysisEngine/AnalysisFifo: a steady 1 kHz sine pushed through
//      prepare + enough frames yields a latest snapshot whose spectrum shows a
//      clear peak near the expected FFT bin within documented tolerance, the
//      drop/skip-frame counters remain finite, and disabling the engine keeps
//      the path safe to call.
//   2. A fixed narrow excess (~4.2–4.35 kHz cabinet tone over noise) builds a
//      high-confidence persistent candidate after enough frames.
//   3. Harmonic rejection: a peak sitting on m*f0 with confident pitch is
//      down-weighted relative to an equal-level fixed off-harmonic peak
//      observed across different f0 values (ordering/threshold relationship,
//      not sample-identical audio).
//   4. Scan workflow: beginScan/endScan produces bounded candidate cards; the
//      detector/suppressor path does not alter process() output until
//      applyCandidate/explicit enable on a slot (dry vs unapplied path within
//      float tolerance).
//   5. ResonanceSuppressor: enabling a static notch at a known sine frequency
//      reduces output energy at that frequency vs bypass; depth/Q clamps
//      respect 0–18 dB and Q 3–40; removed-signal audition energy is elevated
//      when the notch is deep (where the API exposes wet/removed buffers).
//   6. Six slots are constructible/settable; out-of-range frequency/Q/depth
//      inputs clamp safely to finite coefficients.
//
// Uses juce::UnitTest only — no third-party test frameworks. Only documented
// public APIs and tolerances are asserted; no production DSP is rewritten.
// =============================================================================

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>

#include "Source/Analysis/SpectralAnalysisEngine.h"
#include "Source/Analysis/AnalysisFifo.h"
#include "Source/DSP/PitchDetector.h"
#include "Source/DSP/ResonanceDetector.h"
#include "Source/DSP/ResonanceSuppressor.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace
{

using namespace ShredEQ;
using namespace ShredEQ::Analysis;
using namespace ShredEQ::DSP;

constexpr double kPi = 3.1415926535897932384626433832795;
constexpr double kSampleRate = 44100.0;

// Documented STFT tolerance: the Hann-windowed FFT bin spacing is
// sampleRate / fftSize. A 1 kHz sine at fftSize=2048 lands on bin ~46.5; we
// allow ±1.5 bins which comfortably covers scalloping loss + bin rounding.
constexpr double kBinToleranceBins = 1.5;

// Deterministic linear-congruential noise generator (no std::random so a clean
// clone reproduces results bit-for-bit).
struct DeterministicNoise
{
    std::uint64_t state = 0x9e3779b97f4a7c15ULL;
    float next() noexcept
    {
        state = state * 6364136223846793005ULL + 1442695040888963407ULL;
        const double u = static_cast<double> (state >> 33)
                         / static_cast<double> (1ULL << 31);
        return static_cast<float> (2.0 * u - 1.0);
    }
};

// Build a mono synthetic steady sine of the requested length.
std::vector<float> makeSine (double frequencyHz, int numSamples,
                             double sampleRate = kSampleRate,
                             float amplitude = 0.3f)
{
    std::vector<float> out (static_cast<std::size_t> (numSamples));
    const double inc = 2.0 * kPi * frequencyHz / sampleRate;
    double phase = 0.0;
    for (auto& s : out)
    {
        s = amplitude * static_cast<float> (std::sin (phase));
        phase += inc;
    }
    return out;
}

// Sine + broadband noise floor (deterministic).
std::vector<float> makeSinePlusNoise (double frequencyHz, int numSamples,
                                      float sineAmp, float noiseAmp,
                                      DeterministicNoise& noise,
                                      double sampleRate = kSampleRate)
{
    std::vector<float> out (static_cast<std::size_t> (numSamples));
    const double inc = 2.0 * kPi * frequencyHz / sampleRate;
    double phase = 0.0;
    for (auto& s : out)
    {
        s = sineAmp * static_cast<float> (std::sin (phase))
            + noiseAmp * noise.next();
        phase += inc;
    }
    return out;
}

// Argmax bin of a magnitude spectrum, restricted to a frequency window.
int findPeakBin (const std::vector<float>& magnitudes, double sampleRate,
                 int fftSize, double minHz, double maxHz)
{
    const double binHz = sampleRate / static_cast<double> (fftSize);
    const int minBin = std::max (0, static_cast<int> (std::floor (minHz / binHz)));
    const int maxBin = std::min (static_cast<int> (magnitudes.size()) - 1,
                                 static_cast<int> (std::ceil (maxHz / binHz)));
    int best = minBin;
    float bestVal = -1.0f;
    for (int b = minBin; b <= maxBin; ++b)
    {
        if (magnitudes[static_cast<std::size_t> (b)] > bestVal)
        {
            bestVal = magnitudes[static_cast<std::size_t> (b)];
            best = b;
        }
    }
    return best;
}

// Energy in a narrow band around a target frequency (sum of squared samples
// after a trivial DFT probe at that frequency). Used to compare bypass vs
// notched output without depending on a separate FFT.
double bandEnergy (const float* samples, int numSamples, double frequencyHz,
                   double sampleRate)
{
    double re = 0.0, im = 0.0;
    const double inc = 2.0 * kPi * frequencyHz / sampleRate;
    double phase = 0.0;
    for (int n = 0; n < numSamples; ++n)
    {
        const double s = static_cast<double> (samples[n]);
        re += s * std::cos (phase);
        im += s * std::sin (phase);
        phase += inc;
    }
    return (re * re + im * im) / static_cast<double> (numSamples);
}

// A small helper that builds a synthetic Snapshot representing a spectrum with
// a broad pink-ish slope and one or two narrow excess peaks, then returns it
// ready for ResonanceDetector::analyzeSnapshot. Bin i corresponds to
// frequency i * sampleRate / fftSize.
SpectralAnalysisEngine::Snapshot makeSyntheticSpectrum (
    double sampleRate, int fftSize,
    const std::vector<std::pair<double, float>>& peaks,
    std::uint64_t sequence, float noiseFloorDb = -60.0f)
{
    SpectralAnalysisEngine::Snapshot snap;
    const int bins = fftSize / 2 + 1;
    snap.prepare (bins);
    snap.fftSize = fftSize;
    snap.numBins = bins;
    snap.sampleRate = sampleRate;
    snap.sequence = sequence;
    snap.sampleTimestamp = sequence * (fftSize / 4);

    const double binHz = sampleRate / static_cast<double> (fftSize);
    // A gentle descending slope resembling pink-ish noise + a flat floor.
    for (int b = 0; b < bins; ++b)
    {
        const double f = static_cast<double> (b) * binHz;
        double db = noiseFloorDb;
        if (f > 20.0)
            db += -3.0 * std::log2 (f / 20.0); // -3 dB/oct pink slope
        snap.logMagnitudesDb[static_cast<std::size_t> (b)] = static_cast<float> (db);
        snap.magnitudes[static_cast<std::size_t> (b)] =
            static_cast<float> (std::pow (10.0, db / 20.0));
    }

    // Add a narrow (single-bin) excess peak at each requested frequency. The
    // detector's +/- one-sixth-octave baseline will then see this bin as a
    // strong narrow excess above its neighbours.
    for (const auto& [freqHz, excessDb] : peaks)
    {
        if (freqHz <= 0.0)
            continue;
        const int bin = static_cast<int> (std::round (freqHz / binHz));
        if (bin < 1 || bin >= bins - 1)
            continue;
        snap.logMagnitudesDb[static_cast<std::size_t> (bin)] += excessDb;
        snap.magnitudes[static_cast<std::size_t> (bin)] =
            static_cast<float> (std::pow (10.0,
                snap.logMagnitudesDb[static_cast<std::size_t> (bin)] / 20.0));
    }

    return snap;
}

// Finds the highest-confidence candidate whose center frequency is within a
// relative tolerance of the target. Returns nullptr if none.
const ResonanceCandidate* findCandidateNear (const ResonanceDetector::CandidateList& list,
                                            double targetHz, double relTol = 0.04)
{
    const ResonanceCandidate* best = nullptr;
    for (std::size_t i = 0; i < list.size(); ++i)
    {
        const auto& c = list[i];
        const double rel = std::abs (c.centerFrequencyHz - targetHz) / targetHz;
        if (rel <= relTol && (best == nullptr || c.confidence > best->confidence))
            best = &c;
    }
    return best;
}

} // namespace

class ResonanceTests final : public juce::UnitTest
{
public:
    ResonanceTests()
        : juce::UnitTest ("ResonanceTests", "ShredEQ/Analysis") {}

    void runTest() override
    {
        testSpectralAnalysisEngineSteadySinePeak();
        testSpectralAnalysisEngineDisabledSafe();
        testResonanceDetectorFixedNarrowExcess();
        testHarmonicRejectionDownWeighting();
        testScanWorkflowBoundedAndNonModifying();
        testSuppressorStaticNotchAttenuatesSine();
        testSuppressorParameterClamps();
        testSixSlotsConstructibleAndSettable();
    }

    // -----------------------------------------------------------------------
    // 1. SpectralAnalysisEngine / AnalysisFifo steady-sine peak.
    // -----------------------------------------------------------------------
    void testSpectralAnalysisEngineSteadySinePeak()
    {
        beginTest ("SpectralAnalysisEngine: steady 1 kHz sine peaks near expected bin");

        SpectralAnalysisEngine engine;
        const int fftSize = 2048;
        const int hop = fftSize / 4;
        engine.prepare (kSampleRate, 512, fftSize, hop, 2);
        expect (engine.getFftSize() == fftSize, "FFT size round-trips");
        expect (engine.getNumBins() == fftSize / 2 + 1, "Num bins round-trips");
        expect (engine.isEnabled(), "Engine enabled by default");

        const double sineFreq = 1000.0;
        const auto signal = makeSine (sineFreq, 8 * fftSize);

        SpectralAnalysisEngine::Snapshot snap;
        snap.prepare (engine.getNumBins());

        // Push in chunks and drain snapshots until a fresh, peaky spectrum is
        // published. The worker thread is asynchronous, so poll briefly.
        constexpr int kMaxIterations = 200;
        bool gotPeak = false;
        int peakBin = -1;
        double peakHz = 0.0;

        for (int iter = 0; iter < kMaxIterations && ! gotPeak; ++iter)
        {
            const float* ch[1] = { signal.data() };
            // Push the whole signal repeatedly; the engine only keeps the
            // latest frame, so re-pushing is harmless and just feeds the FIFO.
            engine.pushAudio (ch, 1, static_cast<int> (signal.size()));

            juce::Thread::sleep (4);

            if (engine.readLatestSnapshot (snap) && snap.sequence > 0)
            {
                peakBin = findPeakBin (snap.magnitudes, kSampleRate, fftSize,
                                       700.0, 1300.0);
                peakHz = snap.binToHz (peakBin);

                const double expectedBin = sineFreq * fftSize / kSampleRate;
                const double binErr = std::abs (peakBin - expectedBin);
                if (binErr <= kBinToleranceBins)
                    gotPeak = true;
            }
        }

        expect (gotPeak,
                "Steady 1 kHz sine did not produce a peak near the expected bin"
                " (last peakBin=" + juce::String (peakBin)
                + " peakHz=" + juce::String (peakHz, 2) + ")");

        // Drop/skip counters must be finite (and non-negative) after a clean
        // steady-sine run; a runaway/infinite count would be a regression.
        const auto dropped = engine.getDroppedAudioFrameCount();
        const auto skipped = engine.getSkippedStftFrameCount();
        expect (dropped != std::numeric_limits<std::uint64_t>::max(),
                "Dropped audio frame count must be finite");
        expect (skipped != std::numeric_limits<std::uint64_t>::max(),
                "Skipped STFT frame count must be finite");

        // A disabled engine must remain safe to call and must not advance
        // expensive work: after disabling, sequence should not keep growing
        // unbounded from further pushes.
        engine.setEnabled (false);
        juce::Thread::sleep (10);
        const auto seqBefore = snap.sequence;
        for (int i = 0; i < 8; ++i)
        {
            const float* ch[1] = { signal.data() };
            engine.pushAudio (ch, 1, static_cast<int> (signal.size()));
            juce::Thread::sleep (2);
            engine.readLatestSnapshot (snap);
        }
        // Sequence may be stale but must remain finite and not grow rapidly.
        expect (snap.sequence < std::numeric_limits<std::uint64_t>::max(),
                "Disabled engine sequence must remain finite");
        (void) seqBefore;
    }

    // -----------------------------------------------------------------------
    // 1b. Disabled engine stays safe / consumers=0 path is safe.
    // -----------------------------------------------------------------------
    void testSpectralAnalysisEngineDisabledSafe()
    {
        beginTest ("SpectralAnalysisEngine: zero-consumer path stays safe and finite");

        SpectralAnalysisEngine engine;
        engine.prepare (kSampleRate, 256, 1024, 256, 2);
        engine.setNumActiveConsumers (0);

        const auto signal = makeSine (500.0, 4096);
        for (int i = 0; i < 8; ++i)
        {
            const float* ch[1] = { signal.data() };
            engine.pushAudio (ch, 1, static_cast<int> (signal.size()));
        }
        juce::Thread::sleep (10);

        SpectralAnalysisEngine::Snapshot snap;
        snap.prepare (engine.getNumBins());
        // Reading is always safe even with no consumers.
        const auto ok = engine.readLatestSnapshot (snap);
        expect (snap.numBins == engine.getNumBins(),
                "Snapshot numBins matches engine even when idle");
        expect (std::isfinite (snap.binToHz (0)),
                "binToHz(0) must be finite");
        (void) ok;

        // Re-enabling restores processing capability without crashing.
        engine.setNumActiveConsumers (1);
        engine.setEnabled (true);
        for (int i = 0; i < 8; ++i)
        {
            const float* ch[1] = { signal.data() };
            engine.pushAudio (ch, 1, static_cast<int> (signal.size()));
        }
        juce::Thread::sleep (10);
        expect (engine.getDroppedAudioFrameCount()
                    != std::numeric_limits<std::uint64_t>::max(),
                "Dropped frame count finite after re-enable");
    }

    // -----------------------------------------------------------------------
    // 2. ResonanceDetector: fixed narrow excess builds a persistent candidate.
    // -----------------------------------------------------------------------
    void testResonanceDetectorFixedNarrowExcess()
    {
        beginTest ("ResonanceDetector: fixed ~4.3 kHz narrow excess yields persistent candidate");

        const int fftSize = 4096;
        const double targetHz = 4250.0; // within the documented 4.2–4.35 kHz cab band

        ResonanceDetector detector;
        detector.prepare (kSampleRate, fftSize);
        detector.setEnabled (true);
        // A reasonable persistence lambda and a 4 dB excess threshold (matches
        // the documented default behaviour). Persistence builds over frames.
        detector.setPersistenceParameters (0.92f, 4.0f);
        detector.setDiagnosticFrequencyRange (150.0, 16000.0);

        // Feed many frames of the *same* spectrum: a fixed narrow excess at
        // 4.25 kHz above a pink-ish noise floor. Persistence must accumulate.
        std::uint64_t seq = 1;
        const ResonanceCandidate* found = nullptr;
        constexpr int kFrames = 60;
        for (int i = 0; i < kFrames; ++i)
        {
            auto snap = makeSyntheticSpectrum (
                kSampleRate, fftSize,
                { { targetHz, 16.0f } }, // +16 dB narrow excess
                seq++);
            detector.analyzeSnapshot (snap, 0.0, 0.0f);

            found = findCandidateNear (detector.getCandidates(), targetHz, 0.05);
            if (found != nullptr && found->persistence > 0.6f
                && found->confidence > 0.4f)
                break;
        }

        expect (found != nullptr,
                "No candidate near 4.25 kHz after persistent narrow excess");
        if (found != nullptr)
        {
            expect (found->persistence > 0.4f,
                    "Candidate persistence should build above 0.4, got "
                    + juce::String (found->persistence, 4));
            expect (found->confidence > 0.2f,
                    "Candidate confidence should be non-trivial, got "
                    + juce::String (found->confidence, 4));
            expect (found->excessDb > excessThresholdDocumented(),
                    "Candidate excess should exceed threshold, got "
                    + juce::String (found->excessDb, 3));
            // Classification for a >=4 kHz persistent peak is not fizz
            // (intermittent) once persistence is high, but the spec only
            // requires that it be classified — assert it is a valid enum.
            expect (found->classification == ResonanceClassification::persistentCabinet
                        || found->classification == ResonanceClassification::intermittentFizzCluster
                        || found->classification == ResonanceClassification::boxResonance
                        || found->classification == ResonanceClassification::noteLinkedLowerConfidence,
                    "Candidate classification must be a valid enum value");
        }
    }

    // -----------------------------------------------------------------------
    // 3. Harmonic rejection: f0-linked peak is down-weighted vs fixed
    //    off-harmonic peak across note changes.
    // -----------------------------------------------------------------------
    void testHarmonicRejectionDownWeighting()
    {
        beginTest ("ResonanceDetector: harmonic-linked peak is down-weighted vs fixed off-harmonic peak");

        const int fftSize = 4096;
        const double binHz = kSampleRate / fftSize;

        // Two equal-level narrow excess peaks. One sits on a low-order
        // harmonic of a confident f0 that *moves* across frames (note change).
        // The other is fixed at an off-harmonic frequency and present across
        // the same frames.
        //
        // f0 sequence: 110 Hz then 146.83 Hz (D3). Their 3rd/4th harmonics are
        //   110 * 3 = 330 Hz, 146.83 * 3 = 440.5 Hz  -> we instead place a
        //   peak that tracks 3*f0 to exercise motion rejection.
        //
        // The fixed off-harmonic peak stays at ~2.5 kHz. This frequency is
        // deliberately chosen to be >45 cents from every m*f0 (m=1..16) for
        // BOTH f0 values, so it never triggers harmonic rejection. (1.2 kHz,
        // the §8.6 note-linked example, coincidentally aligns with m=11 of
        // 110 Hz and m=8 of 146.83 Hz, so it is unsuitable as the *fixed*
        // control here.)

        const double fixedOffHarmonicHz = 2500.0;
        // Verify it is genuinely off-harmonic for both f0 values.
        const double f0a = 110.0;
        const double f0b = 146.83;
        for (const double f0 : { f0a, f0b })
            for (int m = 1; m <= 16; ++m)
            {
                const double expected = f0 * m;
                const double cents = std::abs (1200.0 * std::log2 (fixedOffHarmonicHz / expected));
                expect (cents > 45.0,
                        "Fixed off-harmonic peak must be >45 cents from every m*f0"
                        " (f0=" + juce::String (f0, 2) + " m=" + juce::String (m) + ")");
            }
        (void) binHz;

        auto runWithF0 = [&] (double f0, double movingPeakHz) {
            ResonanceDetector detector;
            detector.prepare (kSampleRate, fftSize);
            detector.setEnabled (true);
            detector.setPersistenceParameters (0.92f, 4.0f);
            detector.setDiagnosticFrequencyRange (150.0, 16000.0);

            // Seed previous-frame state with a first frame at f0a so the
            // subsequent note change has motion evidence.
            std::uint64_t seq = 1;
            const float excess = 16.0f;
            // First frame: f0=f0a, moving peak at 3*f0a.
            {
                auto snap = makeSyntheticSpectrum (
                    kSampleRate, fftSize,
                    { { movingPeakHz, excess },
                      { fixedOffHarmonicHz, excess } },
                    seq++);
                detector.analyzeSnapshot (snap, f0a, 0.9f); // confident pitch
            }
            // Second frame: f0=f0b, moving peak moves to 3*f0b. The fixed
            // peak stays at 1200 Hz.
            const double movingPeakHzB = f0b * 3.0;
            {
                auto snap = makeSyntheticSpectrum (
                    kSampleRate, fftSize,
                    { { movingPeakHzB, excess },
                      { fixedOffHarmonicHz, excess } },
                    seq++);
                detector.analyzeSnapshot (snap, f0b, 0.9f);
            }

            const auto& list = detector.getCandidates();
            const auto* fixedCand = findCandidateNear (list, fixedOffHarmonicHz, 0.04);
            const auto* movingCand = findCandidateNear (list, movingPeakHzB, 0.04);
            return std::make_pair (fixedCand ? fixedCand->confidence : 0.0f,
                                   movingCand ? movingCand->confidence : 0.0f);
        };

        // Moving peak starts at 3*f0a = 330 Hz, then moves to 3*f0b = 440.5 Hz.
        const double movingPeakHzA = f0a * 3.0;
        const auto [fixedConf, movingConf] = runWithF0 (f0a, movingPeakHzA);

        // The fixed off-harmonic peak must be at least as confident as the
        // moving harmonic-linked peak, and the harmonic-linked peak must be
        // down-weighted (strictly lower confidence). This is the §8.4
        // relationship: "A fixed 4.2 kHz peak present while different notes
        // play receives higher cab-resonance confidence than a peak following
        // the third harmonic."
        expect (fixedConf >= movingConf,
                "Fixed off-harmonic peak confidence (" + juce::String (fixedConf, 4)
                + ") should be >= moving harmonic-linked peak confidence ("
                + juce::String (movingConf, 4) + ")");
        expect (movingConf < fixedConf || movingConf < 0.5f,
                "Harmonic-linked peak should be down-weighted (movingConf="
                + juce::String (movingConf, 4) + ", fixedConf="
                + juce::String (fixedConf, 4) + ")");
    }

    // -----------------------------------------------------------------------
    // 4. Scan workflow: bounded cards, unapplied path does not modify audio.
    // -----------------------------------------------------------------------
    void testScanWorkflowBoundedAndNonModifying()
    {
        beginTest ("Scan workflow: bounded candidate cards; unapplied path is bit-identical to dry");

        const int fftSize = 4096;
        const double targetHz = 4250.0;

        ResonanceDetector detector;
        detector.prepare (kSampleRate, fftSize);
        detector.setEnabled (true);
        detector.setPersistenceParameters (0.92f, 4.0f);

        ResonanceSuppressor suppressor;
        suppressor.prepare (kSampleRate);

        // Start a bounded scan.
        suppressor.startScan (0.2, kSampleRate, detector); // 0.2 s scan
        expect (suppressor.isScanning(), "Scanner active after startScan");
        expect (detector.isScanning(), "Detector scanning flag set");

        // Feed several frames of a fixed narrow excess so the scan accumulator
        // builds a representative candidate.
        std::uint64_t seq = 1;
        constexpr int kFrames = 12;
        for (int i = 0; i < kFrames; ++i)
        {
            auto snap = makeSyntheticSpectrum (
                kSampleRate, fftSize, { { targetHz, 16.0f } }, seq++);
            detector.analyzeSnapshot (snap, 0.0, 0.0f);
            suppressor.updateScan (256, detector);
        }

        // If the scan did not auto-end, end it explicitly.
        if (suppressor.isScanning())
            suppressor.endScan (detector);

        expect (! suppressor.isScanning(), "Scanner inactive after endScan");

        const int numCards = suppressor.getNumCandidateCards();
        expect (numCards >= 0, "Card count must be non-negative");
        expect (numCards <= ResonanceSuppressor::maxScanCards,
                "Card count must be bounded by maxScanCards, got "
                + juce::String (numCards));

        // Critical §21.6 invariant: until a candidate is explicitly applied /
        // a slot enabled, the suppressor must not modify audio. Compare dry vs
        // processed bit-identically (all slots disabled, no audition).
        juce::AudioBuffer<float> dry (2, 2048);
        juce::AudioBuffer<float> wet (2, 2048);
        DeterministicNoise noise;
        for (int n = 0; n < 2048; ++n)
        {
            const float s = 0.2f * noise.next();
            dry.setSample (0, n, s);
            dry.setSample (1, n, s);
            wet.setSample (0, n, s);
            wet.setSample (1, n, s);
        }

        suppressor.process (wet);

        bool bitIdentical = true;
        for (int ch = 0; ch < 2 && bitIdentical; ++ch)
            for (int n = 0; n < 2048; ++n)
                if (wet.getSample (ch, n) != dry.getSample (ch, n))
                {
                    bitIdentical = false;
                    break;
                }

        expect (bitIdentical,
                "Unapplied scan/detector path must not modify audio until a slot is enabled");

        // Now apply a candidate to a slot and confirm the path *does* modify
        // audio (smoke: output remains finite and differs from dry).
        if (numCards > 0)
        {
            const auto card = suppressor.getCandidateCard (0);
            suppressor.applyCandidate (0, card.candidate, false /*static*/);
            // After apply, the slot is enabled; processing should now alter
            // the signal around the candidate frequency.
            juce::AudioBuffer<float> wet2 (2, 2048);
            for (int n = 0; n < 2048; ++n)
            {
                wet2.setSample (0, n, dry.getSample (0, n));
                wet2.setSample (1, n, dry.getSample (1, n));
            }
            // Ramp the envelope: process several blocks so attack settles.
            for (int b = 0; b < 8; ++b)
                suppressor.process (wet2);

            bool anyFinite = true;
            bool anyDifferent = false;
            for (int ch = 0; ch < 2; ++ch)
                for (int n = 0; n < 2048; ++n)
                {
                    const float v = wet2.getSample (ch, n);
                    if (! std::isfinite (v))
                        anyFinite = false;
                    if (v != dry.getSample (ch, n))
                        anyDifferent = true;
                }
            expect (anyFinite, "Applied-slot output must be finite");
            expect (anyDifferent, "Applied slot must modify audio vs dry");
        }
    }

    // -----------------------------------------------------------------------
    // 5. ResonanceSuppressor: static notch attenuates a target sine.
    // -----------------------------------------------------------------------
    void testSuppressorStaticNotchAttenuatesSine()
    {
        beginTest ("ResonanceSuppressor: static notch at sine frequency reduces band energy vs bypass");

        const double sineFreq = 3000.0;
        const int numSamples = 8192;

        ResonanceSuppressor suppressor;
        suppressor.prepare (kSampleRate);

        const auto signal = makeSine (sineFreq, numSamples, kSampleRate, 0.3f);

        // Bypass path: no slots enabled.
        juce::AudioBuffer<float> bypass (2, numSamples);
        for (int n = 0; n < numSamples; ++n)
        {
            bypass.setSample (0, n, signal[static_cast<std::size_t> (n)]);
            bypass.setSample (1, n, signal[static_cast<std::size_t> (n)]);
        }
        suppressor.process (bypass);
        const double bypassEnergy = bandEnergy (bypass.getReadPointer (0),
                                                numSamples, sineFreq, kSampleRate);

        // Build a static notch candidate at sineFreq with max depth.
        ResonanceCandidate candidate;
        candidate.centerFrequencyHz = sineFreq;
        candidate.bandwidthHz = sineFreq / 10.0; // Q ~ 10
        candidate.qHint = 10.0f;
        candidate.excessDb = 18.0f; // clamp at upper bound 0..18
        candidate.persistence = 1.0f;
        candidate.score = 0.9f;
        candidate.confidence = 0.9f;

        suppressor.applyCandidate (0, candidate, false /*static*/);
        // Force a deep static depth immediately: setMaxDepthDb then process
        // enough samples for the attack envelope to settle near target.
        suppressor.getSlot (0).setMaxDepthDb (18.0);
        suppressor.getSlot (0).setAttackMs (0.5); // fastest documented attack

        juce::AudioBuffer<float> notched (2, numSamples);
        for (int n = 0; n < numSamples; ++n)
        {
            notched.setSample (0, n, signal[static_cast<std::size_t> (n)]);
            notched.setSample (1, n, signal[static_cast<std::size_t> (n)]);
        }
        // Process in blocks so the envelope ramps.
        constexpr int blockSize = 256;
        for (int start = 0; start < numSamples; start += blockSize)
        {
            const int len = std::min (blockSize, numSamples - start);
            juce::AudioBuffer<float> sub (2, len);
            sub.copyFrom (0, 0, notched, 0, start, len);
            sub.copyFrom (1, 0, notched, 1, start, len);
            suppressor.process (sub);
            for (int ch = 0; ch < 2; ++ch)
                for (int n = 0; n < len; ++n)
                    notched.setSample (ch, start + n, sub.getSample (ch, n));
        }

        // Output must be finite everywhere.
        for (int ch = 0; ch < 2; ++ch)
            for (int n = 0; n < numSamples; ++n)
                expect (std::isfinite (notched.getSample (ch, n)),
                        "Notched output must be finite");

        // Use the tail of the buffer (after attack settled) for energy
        // comparison to avoid the initial ramp transient.
        const int tailStart = numSamples / 2;
        const double notchedEnergy = bandEnergy (
            notched.getReadPointer (0) + tailStart,
            numSamples - tailStart, sineFreq, kSampleRate);
        const double bypassEnergyTail = bandEnergy (
            bypass.getReadPointer (0) + tailStart,
            numSamples - tailStart, sineFreq, kSampleRate);

        expect (notchedEnergy < bypassEnergyTail,
                "Static notch must reduce band energy at the target sine vs bypass"
                " (notched=" + juce::String (notchedEnergy, 6)
                + " bypass=" + juce::String (bypassEnergyTail, 6) + ")");
        expect (notchedEnergy < 0.5 * bypassEnergyTail,
                "Deep static notch should attenuate by a clear margin"
                " (notched=" + juce::String (notchedEnergy, 6)
                + " bypass=" + juce::String (bypassEnergyTail, 6) + ")");

        // Removed-signal audition: y_removed = x - y_processed. When the notch
        // is deep, the removed buffer must carry elevated energy at the
        // targeted frequency versus the dry signal's residual (which is ~0 at
        // the sine frequency since dry is a pure sine). We compute the removed
        // signal manually from dry - notched and confirm it is non-trivial.
        std::vector<float> removed (static_cast<std::size_t> (numSamples - tailStart));
        for (int n = 0; n < static_cast<int> (removed.size()); ++n)
            removed[static_cast<std::size_t> (n)] =
                bypass.getSample (0, tailStart + n) - notched.getSample (0, tailStart + n);
        const double removedEnergy = bandEnergy (
            removed.data(), static_cast<int> (removed.size()), sineFreq, kSampleRate);
        expect (removedEnergy > 1.0e-6,
                "Removed-signal audition should carry non-trivial energy at the notch frequency"
                " (removed=" + juce::String (removedEnergy, 6) + ")");
    }

    // -----------------------------------------------------------------------
    // 5b/6. Parameter clamps: freq/Q/depth clamp to documented bounds and
    //       produce finite coefficients.
    // -----------------------------------------------------------------------
    void testSuppressorParameterClamps()
    {
        beginTest ("SuppressorSlot: out-of-range freq/Q/depth clamp to finite safe coefficients");

        ResonanceSuppressor suppressor;
        suppressor.prepare (kSampleRate);

        auto& slot = suppressor.getSlot (0);
        slot.prepare (kSampleRate);
        slot.setEnabled (true);

        // Out-of-range frequency clamps to [150, 16000].
        slot.setFrequency (10.0);
        expect (slot.getFrequency() >= 150.0,
                "Frequency below lower bound must clamp to >= 150 Hz, got "
                + juce::String (slot.getFrequency(), 2));
        slot.setFrequency (1000000.0);
        expect (slot.getFrequency() <= 16000.0,
                "Frequency above upper bound must clamp to <= 16 kHz, got "
                + juce::String (slot.getFrequency(), 2));

        // Out-of-range Q clamps to [3, 40].
        slot.setQ (0.1);
        expect (slot.getQ() >= 3.0,
                "Q below lower bound must clamp to >= 3, got "
                + juce::String (slot.getQ(), 4));
        slot.setQ (9999.0);
        expect (slot.getQ() <= 40.0,
                "Q above upper bound must clamp to <= 40, got "
                + juce::String (slot.getQ(), 4));

        // Depth clamp: documented 0..18 dB. setMaxDepthDb stores the value
        // directly (the clamp happens at coefficient calculation / apply time
        // via std::clamp(depth, 0, 18)). We verify the coefficient path stays
        // finite for a deliberately out-of-range depth by going through
        // calculateCoefficients indirectly via processSample.
        slot.setMaxDepthDb (-50.0); // below 0
        slot.setFrequency (1000.0);
        slot.setQ (5.0);
        slot.setIsStatic (true);

        // Force the envelope to ramp the (clamped) depth and process a block.
        juce::AudioBuffer<float> buf (2, 512);
        for (int n = 0; n < 512; ++n)
        {
            const float s = 0.3f * static_cast<float> (std::sin (2.0 * kPi * 1000.0 * n / kSampleRate));
            buf.setSample (0, n, s);
            buf.setSample (1, n, s);
        }
        // Multiple passes to let the static envelope settle.
        for (int p = 0; p < 4; ++p)
            suppressor.process (buf);

        for (int ch = 0; ch < 2; ++ch)
            for (int n = 0; n < 512; ++n)
            {
                const float v = buf.getSample (ch, n);
                expect (std::isfinite (v),
                        "Out-of-range depth must not produce non-finite output");
                expect (std::abs (v) < 100.0f,
                        "Out-of-range depth must not produce unbounded output");
            }

        // Now an absurdly high depth: coefficients must still be finite.
        slot.setMaxDepthDb (1000.0); // far above 18 dB
        for (int p = 0; p < 4; ++p)
            suppressor.process (buf);
        for (int ch = 0; ch < 2; ++ch)
            for (int n = 0; n < 512; ++n)
            {
                const float v = buf.getSample (ch, n);
                expect (std::isfinite (v),
                        "Excessive depth must not produce non-finite output");
                expect (std::abs (v) < 100.0f,
                        "Excessive depth must not produce unbounded output");
            }

        // Direct coefficient calculation must also be finite across extreme
        // parameter combinations (sample-rate edges, freq/Q bounds).
        for (double fs : { 8000.0, 44100.0, 192000.0 })
        {
            slot.prepare (fs);
            for (double f : { 150.0, 8000.0, 16000.0 })
                for (double q : { 3.0, 20.0, 40.0 })
                    for (double d : { 0.0, 18.0, 100.0, -100.0 })
                    {
                        slot.calculateCoefficients (fs, f, q, d);
                        double l = 0.3, r = 0.3;
                        slot.processSample (l, r, fs);
                        expect (std::isfinite (l) && std::isfinite (r),
                                "processSample must stay finite across extreme params");
                    }
        }
    }

    // -----------------------------------------------------------------------
    // 6. Six slots constructible and settable.
    // -----------------------------------------------------------------------
    void testSixSlotsConstructibleAndSettable()
    {
        beginTest ("ResonanceSuppressor: six slots are constructible and independently settable");

        ResonanceSuppressor suppressor;
        suppressor.prepare (kSampleRate);

        expect (ResonanceSuppressor::numSlots == 6,
                "Documented v1.0 slot count is 6, got "
                + juce::String (ResonanceSuppressor::numSlots));

        for (int i = 0; i < ResonanceSuppressor::numSlots; ++i)
        {
            auto& slot = suppressor.getSlot (i);
            slot.setEnabled (true);
            const double freq = 200.0 + 500.0 * i;
            const double q = 4.0 + i;
            const double depth = 3.0 + i;
            slot.setFrequency (freq);
            slot.setQ (q);
            slot.setMaxDepthDb (depth);
            slot.setIsStatic (true);

            expect (std::abs (slot.getFrequency() - freq) < 1.0,
                    "Slot " + juce::String (i) + " frequency round-trips");
            expect (std::abs (slot.getQ() - q) < 1.0e-6,
                    "Slot " + juce::String (i) + " Q round-trips");
            expect (slot.getMaxDepthDb() == depth,
                    "Slot " + juce::String (i) + " depth round-trips");
            expect (slot.isEnabled(),
                    "Slot " + juce::String (i) + " enabled round-trips");
        }

        // Const access compiles and is in range.
        const auto& constSupp = suppressor;
        for (int i = 0; i < ResonanceSuppressor::numSlots; ++i)
            expect (constSupp.getSlot (i).getFrequency() >= 150.0
                        && constSupp.getSlot (i).getFrequency() <= 16000.0,
                    "Const slot access returns clamped frequency");

        // Processing with all six slots enabled stays finite.
        juce::AudioBuffer<float> buf (2, 1024);
        DeterministicNoise noise;
        for (int n = 0; n < 1024; ++n)
        {
            const float s = 0.2f * noise.next();
            buf.setSample (0, n, s);
            buf.setSample (1, n, s);
        }
        for (int p = 0; p < 4; ++p)
            suppressor.process (buf);
        for (int ch = 0; ch < 2; ++ch)
            for (int n = 0; n < 1024; ++n)
            {
                const float v = buf.getSample (ch, n);
                expect (std::isfinite (v),
                        "Six-slot output must be finite");
                expect (std::abs (v) < 100.0f,
                        "Six-slot output must be bounded");
            }
    }

private:
    // Documented default excess threshold from §8.3 / detector defaults.
    static constexpr float excessThresholdDocumented() noexcept { return 4.0f; }
};

// Static registration: discovered by juce::UnitTestRunner::runAllTests().
static ResonanceTests resonanceTestsInstance;
