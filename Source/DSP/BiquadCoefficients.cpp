#include "BiquadCoefficients.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace ShredEQ::DSP
{
namespace
{
constexpr double pi = 3.14159265358979323846264338327950288;
constexpr double minimumFrequencyHz = 15.0;
constexpr double maximumFrequencyHz = 24000.0;
constexpr double minimumQ = 0.10;
constexpr double maximumQ = 36.0;
constexpr double minimumGainDb = -24.0;
constexpr double maximumGainDb = 24.0;
constexpr double minimumSampleRate = 1000.0;
constexpr double maximumSampleRate = 768000.0;
constexpr double nyquistMargin = 0.999999;

double finiteOr (double value, double fallback) noexcept
{
    return std::isfinite (value) ? value : fallback;
}

FilterParameters sanitise (FilterParameters parameters) noexcept
{
    parameters.sampleRate = std::clamp (
        finiteOr (parameters.sampleRate, 44100.0),
        minimumSampleRate, maximumSampleRate);

    const auto highestFrequency = std::min (
        maximumFrequencyHz, nyquistMargin * 0.5 * parameters.sampleRate);

    parameters.frequencyHz = std::clamp (
        finiteOr (parameters.frequencyHz, 1000.0),
        minimumFrequencyHz, highestFrequency);
    parameters.q = std::clamp (
        finiteOr (parameters.q, 0.7071067811865476), minimumQ, maximumQ);
    parameters.gainDb = std::clamp (
        finiteOr (parameters.gainDb, 0.0), minimumGainDb, maximumGainDb);
    return parameters;
}

BiquadCoefficients normalised (double b0, double b1, double b2,
                               double a0, double a1, double a2,
                               SectionOrder order) noexcept
{
    if (! std::isfinite (a0) || std::abs (a0) < 1.0e-15)
        return BiquadCoefficients::unity();

    const auto inverseA0 = 1.0 / a0;
    BiquadCoefficients result {
        b0 * inverseA0, b1 * inverseA0, b2 * inverseA0,
        a1 * inverseA0, a2 * inverseA0, order
    };

    if (! std::isfinite (result.b0) || ! std::isfinite (result.b1)
        || ! std::isfinite (result.b2) || ! std::isfinite (result.a1)
        || ! std::isfinite (result.a2))
        return BiquadCoefficients::unity();

    return result;
}

bool isLowPass (FilterKind kind) noexcept
{
    return kind == FilterKind::lowPass || kind == FilterKind::resonantLowPass;
}

bool isHighPass (FilterKind kind) noexcept
{
    return kind == FilterKind::highPass || kind == FilterKind::resonantHighPass;
}

bool isShelf (FilterKind kind) noexcept
{
    return kind == FilterKind::lowShelf || kind == FilterKind::highShelf;
}

int canonicalSlope (int slope) noexcept
{
    constexpr std::array supported { 6, 12, 18, 24, 36, 48 };
    auto closest = supported.front();
    auto closestDistance = std::abs (slope - closest);

    for (const auto candidate : supported)
    {
        const auto distance = std::abs (slope - candidate);
        if (distance < closestDistance)
        {
            closest = candidate;
            closestDistance = distance;
        }
    }

    return closest;
}
} // namespace

BiquadCoefficients BiquadCoefficients::unity() noexcept
{
    return {};
}

BiquadCoefficients BiquadCoefficients::makeFirstOrder (
    FilterKind kind, const FilterParameters& rawParameters) noexcept
{
    const auto parameters = sanitise (rawParameters);
    const auto k = std::tan (pi * parameters.frequencyHz / parameters.sampleRate);
    const auto amplitude = std::pow (10.0, parameters.gainDb / 20.0);
    const auto denominator = 1.0 + k;

    if (isLowPass (kind))
        return normalised (k, k, 0.0, denominator, k - 1.0, 0.0,
                           SectionOrder::first);

    if (isHighPass (kind))
        return normalised (1.0, -1.0, 0.0, denominator, k - 1.0, 0.0,
                           SectionOrder::first);

    if (kind == FilterKind::lowShelf)
        return normalised (1.0 + amplitude * k, amplitude * k - 1.0, 0.0,
                           denominator, k - 1.0, 0.0, SectionOrder::first);

    if (kind == FilterKind::highShelf)
        return normalised (amplitude + k, k - amplitude, 0.0,
                           denominator, k - 1.0, 0.0, SectionOrder::first);

    auto result = unity();
    result.order = SectionOrder::first;
    return result;
}

BiquadCoefficients BiquadCoefficients::makeSecondOrder (
    FilterKind kind, const FilterParameters& rawParameters) noexcept
{
    const auto parameters = sanitise (rawParameters);
    const auto omega = 2.0 * pi * parameters.frequencyHz / parameters.sampleRate;
    const auto sine = std::sin (omega);
    const auto cosine = std::cos (omega);
    const auto alpha = sine / (2.0 * parameters.q);
    const auto amplitude = std::pow (10.0, parameters.gainDb / 40.0);

    switch (kind)
    {
        case FilterKind::bell:
            return normalised (1.0 + alpha * amplitude, -2.0 * cosine,
                               1.0 - alpha * amplitude,
                               1.0 + alpha / amplitude, -2.0 * cosine,
                               1.0 - alpha / amplitude, SectionOrder::second);

        case FilterKind::notch:
            return normalised (1.0, -2.0 * cosine, 1.0,
                               1.0 + alpha, -2.0 * cosine, 1.0 - alpha,
                               SectionOrder::second);

        case FilterKind::lowPass:
        case FilterKind::resonantLowPass:
            return normalised ((1.0 - cosine) * 0.5, 1.0 - cosine,
                               (1.0 - cosine) * 0.5,
                               1.0 + alpha, -2.0 * cosine, 1.0 - alpha,
                               SectionOrder::second);

        case FilterKind::highPass:
        case FilterKind::resonantHighPass:
            return normalised ((1.0 + cosine) * 0.5, -(1.0 + cosine),
                               (1.0 + cosine) * 0.5,
                               1.0 + alpha, -2.0 * cosine, 1.0 - alpha,
                               SectionOrder::second);

        case FilterKind::bandPass:
            // Constant 0 dB peak-gain form from the RBJ cookbook.
            return normalised (alpha, 0.0, -alpha,
                               1.0 + alpha, -2.0 * cosine, 1.0 - alpha,
                               SectionOrder::second);

        case FilterKind::lowShelf:
        {
            const auto beta = 2.0 * std::sqrt (amplitude) * alpha;
            return normalised (
                amplitude * ((amplitude + 1.0) - (amplitude - 1.0) * cosine + beta),
                2.0 * amplitude * ((amplitude - 1.0) - (amplitude + 1.0) * cosine),
                amplitude * ((amplitude + 1.0) - (amplitude - 1.0) * cosine - beta),
                (amplitude + 1.0) + (amplitude - 1.0) * cosine + beta,
                -2.0 * ((amplitude - 1.0) + (amplitude + 1.0) * cosine),
                (amplitude + 1.0) + (amplitude - 1.0) * cosine - beta,
                SectionOrder::second);
        }

        case FilterKind::highShelf:
        {
            const auto beta = 2.0 * std::sqrt (amplitude) * alpha;
            return normalised (
                amplitude * ((amplitude + 1.0) + (amplitude - 1.0) * cosine + beta),
                -2.0 * amplitude * ((amplitude - 1.0) + (amplitude + 1.0) * cosine),
                amplitude * ((amplitude + 1.0) + (amplitude - 1.0) * cosine - beta),
                (amplitude + 1.0) - (amplitude - 1.0) * cosine + beta,
                2.0 * ((amplitude - 1.0) - (amplitude + 1.0) * cosine),
                (amplitude + 1.0) - (amplitude - 1.0) * cosine - beta,
                SectionOrder::second);
        }

        case FilterKind::bypass:
        case FilterKind::tilt:
            return unity();
    }

    return unity();
}

BiquadCoefficients BiquadCoefficients::makeFirstOrder (
    FilterKind kind, double sampleRate, double frequencyHz, double gainDb) noexcept
{
    return makeFirstOrder (kind, { sampleRate, frequencyHz,
                                   0.7071067811865476, gainDb });
}

BiquadCoefficients BiquadCoefficients::makeSecondOrder (
    FilterKind kind, double sampleRate, double frequencyHz,
    double q, double gainDb) noexcept
{
    return makeSecondOrder (kind, { sampleRate, frequencyHz, q, gainDb });
}

std::complex<double> BiquadCoefficients::response (
    double frequencyHz, double sampleRate) const noexcept
{
    const auto safeSampleRate = std::clamp (
        finiteOr (sampleRate, 44100.0), minimumSampleRate, maximumSampleRate);
    const auto safeFrequency = std::clamp (
        finiteOr (frequencyHz, 0.0), 0.0, 0.5 * safeSampleRate);
    const auto omega = 2.0 * pi * safeFrequency / safeSampleRate;
    const auto z1 = std::polar (1.0, -omega);
    const auto z2 = z1 * z1;
    const auto numerator = b0 + b1 * z1 + b2 * z2;
    const auto denominator = 1.0 + a1 * z1 + a2 * z2;

    if (std::abs (denominator) <= std::numeric_limits<double>::min())
        return {};

    return numerator / denominator;
}

double BiquadCoefficients::magnitude (
    double frequencyHz, double sampleRate) const noexcept
{
    return std::abs (response (frequencyHz, sampleRate));
}

bool CoefficientCascade::push (const BiquadCoefficients& section) noexcept
{
    if (count >= sections.size())
        return false;

    sections[count++] = section;
    return true;
}

std::complex<double> CoefficientCascade::response (
    double frequencyHz, double sampleRate) const noexcept
{
    std::complex<double> result { 1.0, 0.0 };
    for (std::size_t index = 0; index < count; ++index)
        result *= sections[index].response (frequencyHz, sampleRate);
    return result;
}

double CoefficientCascade::magnitude (
    double frequencyHz, double sampleRate) const noexcept
{
    return std::abs (response (frequencyHz, sampleRate));
}

double CoefficientCascade::magnitudeDb (
    double frequencyHz, double sampleRate, double floorDb) const noexcept
{
    const auto safeFloor = std::min (finiteOr (floorDb, -300.0), 0.0);
    const auto magnitudeFloor = std::pow (10.0, safeFloor / 20.0);
    return 20.0 * std::log10 (std::max (magnitude (frequencyHz, sampleRate),
                                        magnitudeFloor));
}

CoefficientCascade makeSlopeCascade (
    FilterKind kind, FilterSlope slope,
    const FilterParameters& parameters) noexcept
{
    return makeSlopeCascade (kind, static_cast<int> (slope), parameters);
}

CoefficientCascade makeSlopeCascade (
    FilterKind kind, int slopeDbPerOctave,
    const FilterParameters& rawParameters) noexcept
{
    CoefficientCascade result;
    auto parameters = sanitise (rawParameters);

    if (kind == FilterKind::bypass)
        return result;

    if (kind == FilterKind::tilt)
    {
        auto lowParameters = parameters;
        auto highParameters = parameters;
        lowParameters.gainDb = -0.5 * parameters.gainDb;
        highParameters.gainDb = 0.5 * parameters.gainDb;
        (void) result.push (BiquadCoefficients::makeSecondOrder (
            FilterKind::lowShelf, lowParameters));
        (void) result.push (BiquadCoefficients::makeSecondOrder (
            FilterKind::highShelf, highParameters));
        return result;
    }

    if (! isLowPass (kind) && ! isHighPass (kind) && ! isShelf (kind))
    {
        (void) result.push (
            BiquadCoefficients::makeSecondOrder (kind, parameters));
        return result;
    }

    const auto slope = canonicalSlope (slopeDbPerOctave);
    const auto hasFirstOrder = slope == 6 || slope == 18;
    const auto secondOrderCount = slope / 12;
    const auto totalSectionCount = secondOrderCount + (hasFirstOrder ? 1 : 0);

    if (isShelf (kind))
        parameters.gainDb /= static_cast<double> (totalSectionCount);

    // Odd-order cascades deliberately put their first-order section first.
    if (hasFirstOrder)
        (void) result.push (
            BiquadCoefficients::makeFirstOrder (kind, parameters));

    for (int section = 0; section < secondOrderCount; ++section)
        (void) result.push (
            BiquadCoefficients::makeSecondOrder (kind, parameters));

    return result;
}

std::complex<double> evaluateResponse (
    const BiquadCoefficients& section,
    double frequencyHz, double sampleRate) noexcept
{
    return section.response (frequencyHz, sampleRate);
}

double evaluateMagnitude (
    const BiquadCoefficients& section,
    double frequencyHz, double sampleRate) noexcept
{
    return section.magnitude (frequencyHz, sampleRate);
}

std::complex<double> evaluateResponse (
    const CoefficientCascade& cascade,
    double frequencyHz, double sampleRate) noexcept
{
    return cascade.response (frequencyHz, sampleRate);
}

double evaluateMagnitude (
    const CoefficientCascade& cascade,
    double frequencyHz, double sampleRate) noexcept
{
    return cascade.magnitude (frequencyHz, sampleRate);
}

} // namespace ShredEQ::DSP
