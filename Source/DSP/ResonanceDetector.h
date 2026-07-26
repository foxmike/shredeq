#pragma once

#include "../Analysis/SpectralAnalysisEngine.h"
#include "PitchDetector.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace ShredEQ::DSP
{

enum class ResonanceClassification
{
    persistentCabinet = 0,
    intermittentFizzCluster,
    boxResonance,
    noteLinkedLowerConfidence,
    PersistentCabinet = persistentCabinet,
    IntermittentFizzCluster = intermittentFizzCluster,
    BoxResonance = boxResonance,
    NoteLinkedLowerConfidence = noteLinkedLowerConfidence
};

struct ResonanceCandidate
{
    double centerFrequencyHz = 0.0;
    double bandwidthHz = 0.0;
    float qHint = 3.0f;
    float excessDb = 0.0f;
    float persistence = 0.0f;
    float score = 0.0f;
    float confidence = 0.0f;
    ResonanceClassification classification =
        ResonanceClassification::persistentCabinet;
    bool nearDetectedHarmonic = false;
    bool tracksPitchMotion = false;
};

/**
    Control-rate-only detector for fixed and semi-fixed spectral resonances.

    It consumes SpectralAnalysisEngine snapshots and external PitchResult
    values, and never reads, writes, or filters audio. All vectors are sized by
    prepare(); snapshot analysis and scan accumulation do not allocate.

    The broad baseline is a +/- one-sixth-octave box smooth in log-frequency.
    Narrowness is local peak sharpness relative to adjacent excess bins.
    Perceptual weight gently favours the 300 Hz--8 kHz guitar/cabinet region.
    Stability compares each peak with the preceding frame. Pitch rejection
    down-weights peaks close to harmonics 1--16 and peaks whose motion follows
    a confident changing f0. With uncertain f0 the spectral score is retained.
*/
class ResonanceDetector final
{
public:
    static constexpr std::size_t maximumCandidates = 12;

    struct CandidateList
    {
        std::array<ResonanceCandidate, maximumCandidates> candidates {};
        std::size_t count = 0;
        std::uint64_t sourceSequence = 0;

        [[nodiscard]] const ResonanceCandidate* begin() const noexcept
        {
            return candidates.data();
        }

        [[nodiscard]] const ResonanceCandidate* end() const noexcept
        {
            return candidates.data() + count;
        }

        [[nodiscard]] std::size_t size() const noexcept
        {
            return count;
        }

        [[nodiscard]] bool empty() const noexcept
        {
            return count == 0;
        }

        [[nodiscard]] const ResonanceCandidate& operator[] (
            std::size_t index) const noexcept
        {
            return candidates[index];
        }
    };

    ResonanceDetector() noexcept = default;

    /**
        fftSizeOrMaxBins is an allocation bound. Allocating value + 1 supports
        either an FFT size (whose snapshot uses roughly half as many bins) or
        an already-computed maximum bin count.
    */
    void prepare (double sampleRate, int fftSizeOrMaxBins);
    void reset() noexcept;

    void setEnabled (bool shouldBeEnabled) noexcept;
    [[nodiscard]] bool isEnabled() const noexcept;

    void setPersistenceParameters (
        float persistenceLambda, float excessThresholdDb) noexcept;
    void setDiagnosticFrequencyRange (
        double minimumHz, double maximumHz) noexcept;

    [[nodiscard]] bool processControlRate (
        const Analysis::SpectralAnalysisEngine::Snapshot& snapshot,
        double fundamentalHz = 0.0,
        float pitchConfidence = 0.0f) noexcept;

    [[nodiscard]] bool processControlRate (
        const Analysis::SpectralAnalysisEngine::Snapshot& snapshot,
        const PitchResult& pitch) noexcept;

    [[nodiscard]] bool analyzeSnapshot (
        const Analysis::SpectralAnalysisEngine::Snapshot& snapshot,
        double fundamentalHz = 0.0,
        float pitchConfidence = 0.0f) noexcept;

    [[nodiscard]] bool analyzeSnapshot (
        const Analysis::SpectralAnalysisEngine::Snapshot& snapshot,
        const PitchResult& pitch) noexcept;

    [[nodiscard]] const CandidateList& getCandidates() const noexcept;

    /** Starts a bounded scan accumulator and clears its previous summary. */
    void beginScan() noexcept;

    /**
        Freezes the bounded scan summary into getScanSummary(). Live analysis
        may continue, but the frozen summary is not changed until beginScan().
    */
    void endScan() noexcept;
    [[nodiscard]] bool isScanning() const noexcept;
    [[nodiscard]] const CandidateList& getScanSummary() const noexcept;

private:
    static constexpr std::size_t maximumScanSlots = 24;
    static constexpr int maximumPreparedBins = 32769;

    struct ScanSlot
    {
        ResonanceCandidate representative {};
        double weightedFrequencySum = 0.0;
        double weightedBandwidthSum = 0.0;
        float confidenceSum = 0.0f;
        float maximumExcessDb = 0.0f;
        std::uint32_t observations = 0;
    };

    void clearFrameState() noexcept;
    void insertRanked (
        CandidateList& list, const ResonanceCandidate& candidate) noexcept;
    void accumulateScan (const ResonanceCandidate& candidate) noexcept;
    void buildScanSummary() noexcept;

    std::vector<float> baselineDb;
    std::vector<float> excessDb;
    std::vector<float> previousExcessDb;
    std::vector<float> persistence;
    std::vector<double> prefixSum;

    CandidateList liveCandidates {};
    CandidateList scanSummary {};
    std::array<ScanSlot, maximumScanSlots> scanSlots {};
    std::size_t scanSlotCount = 0;

    double preparedSampleRate = 44100.0;
    double previousFundamentalHz = 0.0;
    float previousPitchConfidence = 0.0f;
    float persistenceLambda = 0.92f;
    float excessThresholdDb = 4.0f;
    double minimumDiagnosticHz = 150.0;
    double maximumDiagnosticHz = 16000.0;
    int previousFftSize = 0;
    int previousNumBins = 0;
    std::uint64_t previousSequence = 0;
    bool enabled = true;
    bool scanning = false;
    bool hasPreviousFrame = false;
};

} // namespace ShredEQ::DSP
