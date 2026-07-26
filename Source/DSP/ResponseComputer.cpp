#include "ResponseComputer.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <array>

namespace ShredEQ::DSP
{
namespace
{
constexpr double pi = 3.14159265358979323846264338327950288;

bool shouldFilterBandForChannel (StereoRoutingMode mode, int channelIndex, bool isMidSideDomain) noexcept
{
    if (channelIndex < 0 || channelIndex >= 2)
        return false;

    if (mode == StereoRoutingMode::linked || mode == StereoRoutingMode::unlinked)
        return true;

    if (isMidSideDomain)
    {
        if (mode == StereoRoutingMode::mid && channelIndex == 0)
            return true;
        if (mode == StereoRoutingMode::side && channelIndex == 1)
            return true;
    }
    else
    {
        if (mode == StereoRoutingMode::left && channelIndex == 0)
            return true;
        if (mode == StereoRoutingMode::right && channelIndex == 1)
            return true;
    }

    return false;
}

bool shouldIncludeStage (BandStage stage, StageFilter filter) noexcept
{
    if (filter == StageFilter::all)
        return true;
    if (filter == StageFilter::stageA && stage == BandStage::stageA)
        return true;
    if (filter == StageFilter::stageB && stage == BandStage::stageB)
        return true;
    return false;
}
} // namespace

ResponseComputer::ResponseComputer() noexcept
{
    updateFrequencies();
}

void ResponseComputer::setSize (int newNumPoints, double sampleRate, double minFrequencyHz) noexcept
{
    numPoints = std::max (1, newNumPoints);
    currentSampleRate = sampleRate > 0.0 ? sampleRate : 44100.0;
    minFreq = std::max (1.0, minFrequencyHz);
    updateFrequencies();
}

void ResponseComputer::updateFrequencies() noexcept
{
    frequencies.resize (static_cast<size_t> (numPoints));
    if (numPoints <= 0)
        return;

    const double nyquist = 0.5 * (currentSampleRate > 0.0 ? currentSampleRate : 44100.0);
    const double logMin = std::log (std::max (1.0, minFreq));
    const double logMax = std::log (std::max (logMin + 1.0, nyquist));

    if (numPoints == 1)
    {
        frequencies[0] = minFreq;
        return;
    }

    const double step = (logMax - logMin) / (numPoints - 1);
    for (int i = 0; i < numPoints; ++i)
    {
        frequencies[static_cast<size_t> (i)] = std::exp (logMin + i * step);
    }
}

void ResponseComputer::compute (const std::vector<BandSnapshot>& snapshots,
                                double sampleRate,
                                float* magnitudeDbOut,
                                float* phaseOut,
                                PhaseUnit phaseUnit,
                                StageFilter stageFilter,
                                int channelIndex,
                                bool isMidSideDomain) const noexcept
{
    if (numPoints <= 0 || magnitudeDbOut == nullptr)
        return;

    const double sr = sampleRate > 0.0 ? sampleRate : currentSampleRate;
    
    std::array<CoefficientCascade, 32> activeCascades;
    size_t numActive = 0;

    for (const auto& band : snapshots)
    {
        if (band.enabled && band.filterType != BandFilterType::flatDynamic
            && shouldFilterBandForChannel (band.routingMode, channelIndex, isMidSideDomain)
            && shouldIncludeStage (band.stage, stageFilter))
        {
            FilterParameters params;
            params.sampleRate = sr;
            params.frequencyHz = band.frequencyHz;
            params.gainDb = band.gainDb;
            params.q = band.q;

            if (numActive < activeCascades.size())
            {
                activeCascades[numActive++] = makeSlopeCascade (toFilterKind (band.filterType), band.slope, params);
            }
        }
    }

    for (int i = 0; i < numPoints; ++i)
    {
        const double f = frequencies[static_cast<size_t> (i)];
        std::complex<double> H_total { 1.0, 0.0 };

        for (size_t k = 0; k < numActive; ++k)
        {
            H_total *= activeCascades[k].response (f, sr);
        }

        const double magnitude = std::abs (H_total);
        magnitudeDbOut[i] = static_cast<float> (20.0 * std::log10 (std::max (magnitude, 1e-15)));

        if (phaseOut != nullptr)
        {
            double phase = std::arg (H_total);
            if (phaseUnit == PhaseUnit::degrees)
            {
                phase *= (180.0 / pi);
            }
            phaseOut[i] = static_cast<float> (phase);
        }
    }
}

void ResponseComputer::compute (const ParametricEQ& eq,
                                double sampleRate,
                                float* magnitudeDbOut,
                                float* phaseOut,
                                PhaseUnit phaseUnit,
                                StageFilter stageFilter,
                                int channelIndex,
                                bool isMidSideDomain) const noexcept
{
    compute (eq.getBands().data(), static_cast<int> (eq.getBands().size()),
             sampleRate, magnitudeDbOut, phaseOut, phaseUnit, stageFilter,
             channelIndex, isMidSideDomain);
}

void ResponseComputer::compute (const ParametricBand* bands,
                                int numBands,
                                double sampleRate,
                                float* magnitudeDbOut,
                                float* phaseOut,
                                PhaseUnit phaseUnit,
                                StageFilter stageFilter,
                                int channelIndex,
                                bool isMidSideDomain) const noexcept
{
    if (numPoints <= 0 || magnitudeDbOut == nullptr)
        return;

    if (bands == nullptr || numBands <= 0)
    {
        std::fill_n (magnitudeDbOut, numPoints, 0.0f);
        if (phaseOut != nullptr)
            std::fill_n (phaseOut, numPoints, 0.0f);
        return;
    }

    const double sr = sampleRate > 0.0 ? sampleRate : currentSampleRate;
    
    std::array<CoefficientCascade, 32> activeCascades;
    size_t numActive = 0;

    for (int i = 0; i < numBands; ++i)
    {
        const auto& band = bands[i];
        if (band.isEnabled() && band.getFilterType() != BandFilterType::flatDynamic
            && shouldFilterBandForChannel (band.getRoutingMode(), channelIndex, isMidSideDomain)
            && shouldIncludeStage (band.getStage(), stageFilter))
        {
            FilterParameters params;
            params.sampleRate = sr;
            params.frequencyHz = band.getFrequency();
            params.gainDb = band.getGainDb();
            params.q = band.getQ();

            if (numActive < activeCascades.size())
            {
                activeCascades[numActive++] = makeSlopeCascade (toFilterKind (band.getFilterType()), band.getSlope(), params);
            }
        }
    }

    for (int i = 0; i < numPoints; ++i)
    {
        const double f = frequencies[static_cast<size_t> (i)];
        std::complex<double> H_total { 1.0, 0.0 };

        for (size_t k = 0; k < numActive; ++k)
        {
            H_total *= activeCascades[k].response (f, sr);
        }

        const double magnitude = std::abs (H_total);
        magnitudeDbOut[i] = static_cast<float> (20.0 * std::log10 (std::max (magnitude, 1e-15)));

        if (phaseOut != nullptr)
        {
            double phase = std::arg (H_total);
            if (phaseUnit == PhaseUnit::degrees)
            {
                phase *= (180.0 / pi);
            }
            phaseOut[i] = static_cast<float> (phase);
        }
    }
}

} // namespace ShredEQ::DSP
