#pragma once

#include "ParametricBand.h"
#include <vector>
#include <complex>

namespace ShredEQ::DSP
{

enum class PhaseUnit
{
    radians,
    degrees
};

enum class StageFilter
{
    all,
    stageA,
    stageB
};

struct BandSnapshot
{
    bool enabled = false;
    BandFilterType filterType = BandFilterType::bell;
    double frequencyHz = 1000.0;
    double gainDb = 0.0;
    double q = 0.7071067811865476;
    FilterSlope slope = FilterSlope::dB12;
    StereoRoutingMode routingMode = StereoRoutingMode::linked;
    BandStage stage = BandStage::stageA;
};

/**
    Computes composite frequency-response (magnitude and phase) for a parametric EQ cascade.
    
    This class precalculates log-spaced frequency points, avoiding heap allocations during
    computations. It is thread-safe for use on the message thread and is real-time safe
    when calling compute() because it performs zero dynamic memory allocations in compute().
    
    @note The setSize() method allocates memory and must NOT be called on the audio thread.
*/
class ResponseComputer
{
public:
    /**
        Constructs a ResponseComputer with a default size of 512 points and a default
        sample rate of 44100.0 Hz.
    */
    ResponseComputer() noexcept;

    /**
        Resizes the preallocated log-spaced frequency points.
        This must be called off the audio thread.
    */
    void setSize (int numPoints, double sampleRate, double minFrequencyHz = 10.0) noexcept;

    /**
        Returns the number of frequency points.
    */
    [[nodiscard]] int getNumPoints() const noexcept { return numPoints; }

    /**
        Returns the precalculated frequency points.
    */
    [[nodiscard]] const std::vector<double>& getFrequencies() const noexcept { return frequencies; }

    /**
        Computes the composite response given a list of BandSnapshot.
        
        @param snapshots The snapshots of the bands.
        @param sampleRate The sample rate used for the computation.
        @param magnitudeDbOut Pointer to a buffer of at least getNumPoints() floats to receive the magnitude in dB.
        @param phaseOut Optional pointer to a buffer of at least getNumPoints() floats to receive the phase. Pass nullptr if not needed.
        @param phaseUnit The unit for phase output (radians or degrees).
        @param stageFilter Filter to include only Stage A, Stage B, or all bands.
        @param channelIndex The channel index to compute (0 = Left/Mid, 1 = Right/Side).
        @param isMidSideDomain True if the routing domain is Mid/Side, false if it is Left/Right.
    */
    void compute (const std::vector<BandSnapshot>& snapshots,
                  double sampleRate,
                  float* magnitudeDbOut,
                  float* phaseOut = nullptr,
                  PhaseUnit phaseUnit = PhaseUnit::radians,
                  StageFilter stageFilter = StageFilter::all,
                  int channelIndex = 0,
                  bool isMidSideDomain = false) const noexcept;

    /**
        Computes the composite response given a const ParametricEQ view.
        
        @param eq The parametric EQ view to read from.
        @param sampleRate The sample rate used for the computation.
        @param magnitudeDbOut Pointer to a buffer of at least getNumPoints() floats to receive the magnitude in dB.
        @param phaseOut Optional pointer to a buffer of at least getNumPoints() floats to receive the phase. Pass nullptr if not needed.
        @param phaseUnit The unit for phase output (radians or degrees).
        @param stageFilter Filter to include only Stage A, Stage B, or all bands.
        @param channelIndex The channel index to compute (0 = Left/Mid, 1 = Right/Side).
        @param isMidSideDomain True if the routing domain is Mid/Side, false if it is Left/Right.
    */
    void compute (const ParametricEQ& eq,
                  double sampleRate,
                  float* magnitudeDbOut,
                  float* phaseOut = nullptr,
                  PhaseUnit phaseUnit = PhaseUnit::radians,
                  StageFilter stageFilter = StageFilter::all,
                  int channelIndex = 0,
                  bool isMidSideDomain = false) const noexcept;

    /**
        Computes the composite response given a list/array of const ParametricBand views.
        
        @param bands Pointer to the first ParametricBand.
        @param numBands The number of bands in the array.
        @param sampleRate The sample rate used for the computation.
        @param magnitudeDbOut Pointer to a buffer of at least getNumPoints() floats to receive the magnitude in dB.
        @param phaseOut Optional pointer to a buffer of at least getNumPoints() floats to receive the phase. Pass nullptr if not needed.
        @param phaseUnit The unit for phase output (radians or degrees).
        @param stageFilter Filter to include only Stage A, Stage B, or all bands.
        @param channelIndex The channel index to compute (0 = Left/Mid, 1 = Right/Side).
        @param isMidSideDomain True if the routing domain is Mid/Side, false if it is Left/Right.
    */
    void compute (const ParametricBand* bands,
                  int numBands,
                  double sampleRate,
                  float* magnitudeDbOut,
                  float* phaseOut = nullptr,
                  PhaseUnit phaseUnit = PhaseUnit::radians,
                  StageFilter stageFilter = StageFilter::all,
                  int channelIndex = 0,
                  bool isMidSideDomain = false) const noexcept;

private:
    int numPoints = 512;
    double currentSampleRate = 44100.0;
    double minFreq = 10.0;
    std::vector<double> frequencies;

    void updateFrequencies() noexcept;
};

} // namespace ShredEQ::DSP
