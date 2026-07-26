#pragma once

#include <array>
#include <complex>
#include <cstddef>

namespace ShredEQ::DSP
{

/**
    Filter responses use the RBJ digital formulas evaluated at the requested
    digital frequency. They are minimum-phase, but are not claimed to match an
    arbitrary analogue response close to Nyquist.
*/
enum class FilterKind
{
    bypass,
    bell,
    peak = bell,
    notch,
    lowShelf,
    highShelf,
    lowPass,
    highPass,
    bandPass,
    tilt,
    resonantHighPass,
    resonantLowPass
};

enum class FilterSlope : int
{
    dB6 = 6,
    dB12 = 12,
    dB18 = 18,
    dB24 = 24,
    dB36 = 36,
    dB48 = 48
};

enum class SectionOrder : unsigned char
{
    first = 1,
    second = 2
};

struct FilterParameters
{
    double sampleRate = 44100.0;
    double frequencyHz = 1000.0;
    double q = 0.7071067811865476;
    double gainDb = 0.0;
};

/**
    Coefficients for:
      H(z) = (b0 + b1 z^-1 + b2 z^-2) / (1 + a1 z^-1 + a2 z^-2)

    A first-order section stores zero in b2 and a2. All factory results are
    normalized by a0 and are safe to copy into audio-rate filter state.
*/
struct BiquadCoefficients
{
    double b0 = 1.0;
    double b1 = 0.0;
    double b2 = 0.0;
    double a1 = 0.0;
    double a2 = 0.0;
    SectionOrder order = SectionOrder::second;

    [[nodiscard]] static BiquadCoefficients unity() noexcept;

    [[nodiscard]] static BiquadCoefficients makeFirstOrder (
        FilterKind kind, const FilterParameters& parameters) noexcept;

    [[nodiscard]] static BiquadCoefficients makeSecondOrder (
        FilterKind kind, const FilterParameters& parameters) noexcept;

    [[nodiscard]] static BiquadCoefficients makeFirstOrder (
        FilterKind kind, double sampleRate, double frequencyHz,
        double gainDb = 0.0) noexcept;

    [[nodiscard]] static BiquadCoefficients makeSecondOrder (
        FilterKind kind, double sampleRate, double frequencyHz,
        double q = 0.7071067811865476, double gainDb = 0.0) noexcept;

    [[nodiscard]] std::complex<double> response (
        double frequencyHz, double sampleRate) const noexcept;

    [[nodiscard]] double magnitude (
        double frequencyHz, double sampleRate) const noexcept;
};

// Four biquads are sufficient for 48 dB/oct; the extra slot permits an
// odd-order first-order section or the two-section tilt representation.
inline constexpr std::size_t maxCascadeSections = 5;

struct CoefficientCascade
{
    std::array<BiquadCoefficients, maxCascadeSections> sections {};
    std::size_t count = 0;

    [[nodiscard]] bool push (const BiquadCoefficients& section) noexcept;
    [[nodiscard]] bool empty() const noexcept { return count == 0; }
    [[nodiscard]] std::size_t size() const noexcept { return count; }

    [[nodiscard]] const BiquadCoefficients* begin() const noexcept
    {
        return sections.data();
    }

    [[nodiscard]] const BiquadCoefficients* end() const noexcept
    {
        return sections.data() + count;
    }

    [[nodiscard]] std::complex<double> response (
        double frequencyHz, double sampleRate) const noexcept;

    [[nodiscard]] double magnitude (
        double frequencyHz, double sampleRate) const noexcept;

    [[nodiscard]] double magnitudeDb (
        double frequencyHz, double sampleRate,
        double floorDb = -300.0) const noexcept;
};

/**
    Builds the requested LP/HP or shelf slope without allocation:
      6: 1x first order, 12: 1x biquad, 18: first order + biquad,
      24: 2x biquad, 36: 3x biquad, 48: 4x biquad.

    Resonant LP/HP are their ordinary RBJ counterparts with the supplied Q.
    For shelves, gainDb is divided between sections so the cascade's asymptotic
    gain remains the requested value. Other kinds are returned as a single
    second-order section; tilt is represented by opposing low/high shelves.
*/
[[nodiscard]] CoefficientCascade makeSlopeCascade (
    FilterKind kind, FilterSlope slope,
    const FilterParameters& parameters) noexcept;

[[nodiscard]] CoefficientCascade makeSlopeCascade (
    FilterKind kind, int slopeDbPerOctave,
    const FilterParameters& parameters) noexcept;

[[nodiscard]] std::complex<double> evaluateResponse (
    const BiquadCoefficients& section,
    double frequencyHz, double sampleRate) noexcept;

[[nodiscard]] double evaluateMagnitude (
    const BiquadCoefficients& section,
    double frequencyHz, double sampleRate) noexcept;

[[nodiscard]] std::complex<double> evaluateResponse (
    const CoefficientCascade& cascade,
    double frequencyHz, double sampleRate) noexcept;

[[nodiscard]] double evaluateMagnitude (
    const CoefficientCascade& cascade,
    double frequencyHz, double sampleRate) noexcept;

} // namespace ShredEQ::DSP
