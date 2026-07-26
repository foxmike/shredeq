#pragma once

#include "BiquadCoefficients.h"

#include <array>
#include <cstddef>

namespace ShredEQ::DSP
{

/**
    Two-channel topology-preserving-transform state-variable filter.

    This is the modulation-stable path for continuously swept bands. Static
    bands can use the less expensive biquad path. Parameter updates preserve
    the TPT integrator state and may be made at audio rate.
*/
class TPTFilter final
{
public:
    using Type = FilterKind;

    static constexpr std::size_t numberOfChannels = 2;

    TPTFilter() noexcept = default;

    void prepare (double sampleRate) noexcept;
    void reset() noexcept;

    void setParameters (double frequencyHz, double gainDb, double q,
                        Type type) noexcept;

    /** Processes one channel. channel must be less than numberOfChannels. */
    [[nodiscard]] float processSample (float input,
                                       std::size_t channel = 0) noexcept;

    /** Convenience helper for independent left and right channel state. */
    void processStereo (float& left, float& right) noexcept;

private:
    struct ChannelState
    {
        double integrator1 = 0.0;
        double integrator2 = 0.0;
    };

    void updateCoefficients() noexcept;
    [[nodiscard]] double process (double input, ChannelState& state) const noexcept;

    static constexpr double defaultSampleRate = 44100.0;
    static constexpr double minimumFrequencyHz = 1.0;
    static constexpr double minimumQ = 0.01;
    static constexpr double maximumQ = 1000.0;
    static constexpr double stateFlushThreshold = 1.0e-24;

    std::array<ChannelState, numberOfChannels> states {};

    double currentSampleRate = defaultSampleRate;
    double frequency = 1000.0;
    double gain = 0.0;
    double resonanceQ = 0.7071067811865476;
    Type filterType = Type::bell;

    // TPT integrator and output-mix coefficients.
    double a1 = 1.0;
    double a2 = 0.0;
    double a3 = 0.0;
    double damping = 1.4142135623730951;
    double mixInput = 1.0;
    double mixBand = 0.0;
    double mixLow = 0.0;
};

} // namespace ShredEQ::DSP
