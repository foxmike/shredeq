#include "TPTFilter.h"

#include <algorithm>
#include <cassert>
#include <cmath>

namespace ShredEQ::DSP
{
namespace
{
constexpr double pi = 3.14159265358979323846264338327950288;

[[nodiscard]] double flushDenormal (double value, double threshold) noexcept
{
    return std::abs (value) < threshold ? 0.0 : value;
}
} // namespace

void TPTFilter::prepare (double sampleRate) noexcept
{
    const auto validSampleRate =
        std::isfinite (sampleRate) && sampleRate > 2.0 * minimumFrequencyHz;
    currentSampleRate = validSampleRate ? sampleRate : defaultSampleRate;
    reset();
    updateCoefficients();
}

void TPTFilter::reset() noexcept
{
    states = {};
}

void TPTFilter::setParameters (double frequencyHz, double gainDb, double q,
                               Type type) noexcept
{
    frequency = std::isfinite (frequencyHz) ? frequencyHz : 1000.0;
    gain = std::isfinite (gainDb) ? gainDb : 0.0;
    resonanceQ = std::isfinite (q) ? q : 0.7071067811865476;
    filterType = type;
    updateCoefficients();
}

void TPTFilter::updateCoefficients() noexcept
{
    const auto nyquistGuard = currentSampleRate * 0.499;
    const auto safeFrequency =
        std::clamp (frequency, minimumFrequencyHz,
                    std::max (minimumFrequencyHz, nyquistGuard));
    const auto safeQ = std::clamp (resonanceQ, minimumQ, maximumQ);
    const auto amplitude =
        std::pow (10.0, std::clamp (gain, -120.0, 120.0) / 40.0);

    auto g = std::tan (pi * safeFrequency / currentSampleRate);
    damping = 1.0 / safeQ;
    mixInput = 0.0;
    mixBand = 0.0;
    mixLow = 0.0;

    switch (filterType)
    {
        case Type::bell:
            damping /= amplitude;
            mixInput = 1.0;
            mixBand = damping * (amplitude * amplitude - 1.0);
            break;

        case Type::lowShelf:
            g /= std::sqrt (amplitude);
            mixInput = 1.0;
            mixBand = damping * (amplitude - 1.0);
            mixLow = amplitude * amplitude - 1.0;
            break;

        case Type::highShelf:
            g *= std::sqrt (amplitude);
            mixInput = amplitude * amplitude;
            mixBand = damping * amplitude * (1.0 - amplitude);
            mixLow = 1.0 - amplitude * amplitude;
            break;

        case Type::lowPass:
        case Type::resonantLowPass:
            mixLow = 1.0;
            break;

        case Type::highPass:
        case Type::resonantHighPass:
            mixInput = 1.0;
            mixBand = -damping;
            mixLow = -1.0;
            break;

        case Type::bandPass:
            mixBand = 1.0;
            break;

        case Type::bypass:
        case Type::notch:
        case Type::tilt:
        default:
            mixInput = 1.0;
            break;
    }

    a1 = 1.0 / (1.0 + g * (g + damping));
    a2 = g * a1;
    a3 = g * a2;
}

double TPTFilter::process (double input, ChannelState& state) const noexcept
{
    // Zero-delay-feedback SVF (trapezoidal integrators). Unlike a direct-form
    // biquad, changing these coefficients does not reinterpret stored delays.
    const auto v3 = input - state.integrator2;
    const auto band =
        a1 * state.integrator1 + a2 * v3;
    const auto low =
        state.integrator2 + a2 * state.integrator1 + a3 * v3;

    state.integrator1 =
        flushDenormal (2.0 * band - state.integrator1, stateFlushThreshold);
    state.integrator2 =
        flushDenormal (2.0 * low - state.integrator2, stateFlushThreshold);

    return mixInput * input + mixBand * band + mixLow * low;
}

float TPTFilter::processSample (float input, std::size_t channel) noexcept
{
    assert (channel < numberOfChannels);
    if (channel >= numberOfChannels)
        return input;

    return static_cast<float> (process (input, states[channel]));
}

void TPTFilter::processStereo (float& left, float& right) noexcept
{
    left = static_cast<float> (process (left, states[0]));
    right = static_cast<float> (process (right, states[1]));
}

} // namespace ShredEQ::DSP
