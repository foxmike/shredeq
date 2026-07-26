#include "MultibandCrossover.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <algorithm>
#include <cmath>

namespace ShredEQ::DSP
{
namespace
{
[[nodiscard]] double finiteOr (double value, double fallback) noexcept
{
    return std::isfinite (value) ? value : fallback;
}

template <typename SampleType>
[[nodiscard]] bool validChannelPointers (
    const SampleType* const* input,
    SampleType* const* weight, SampleType* const* body,
    SampleType* const* bite, SampleType* const* fizzAir,
    int numChannels) noexcept
{
    if (input == nullptr || weight == nullptr || body == nullptr
        || bite == nullptr || fizzAir == nullptr)
        return false;

    for (int channel = 0; channel < numChannels; ++channel)
        if (input[channel] == nullptr || weight[channel] == nullptr
            || body[channel] == nullptr || bite[channel] == nullptr
            || fizzAir[channel] == nullptr)
            return false;

    return true;
}

template <typename SampleType>
[[nodiscard]] bool validOutputPointers (
    const SampleType* const* weight, const SampleType* const* body,
    const SampleType* const* bite, const SampleType* const* fizzAir,
    SampleType* const* output, int numChannels) noexcept
{
    if (weight == nullptr || body == nullptr || bite == nullptr
        || fizzAir == nullptr || output == nullptr)
        return false;

    for (int channel = 0; channel < numChannels; ++channel)
        if (weight[channel] == nullptr || body[channel] == nullptr
            || bite[channel] == nullptr || fizzAir[channel] == nullptr
            || output[channel] == nullptr)
            return false;

    return true;
}
} // namespace

void MultibandCrossover::FrequencySmoother::reset (double value) noexcept
{
    current = target = value;
    multiplier = 1.0;
    remaining = 0;
}

void MultibandCrossover::FrequencySmoother::setTarget (
    double value, int durationSamples) noexcept
{
    if (value == target)
        return;

    target = value;
    remaining = std::max (durationSamples, 0);
    if (remaining == 0 || current <= 0.0 || target <= 0.0)
    {
        reset (target);
        return;
    }

    multiplier = std::exp (
        std::log (target / current) / static_cast<double> (remaining));
}

double MultibandCrossover::FrequencySmoother::next() noexcept
{
    if (remaining > 0)
    {
        current *= multiplier;
        if (--remaining == 0)
            current = target;
    }
    return current;
}

void MultibandCrossover::prepare (
    double sampleRate, int maxBlockSize) noexcept
{
    currentSampleRate =
        std::isfinite (sampleRate) && sampleRate >= 1000.0
            ? sampleRate
            : 44100.0;
    (void) maxBlockSize;
    reset();
}

void MultibandCrossover::reset() noexcept
{
    channelStates = {};

    const auto targets = orderedTargets();
    for (std::size_t index = 0; index < numberOfCrossovers; ++index)
    {
        frequencySmoothers[index].reset (targets[index]);
        effectiveFrequencies[index].store (
            targets[index], std::memory_order_relaxed);
    }
    updateCoefficients (targets);
}

bool MultibandCrossover::processSplit (
    const float* const* inputChannels, int numChannels, int numSamples,
    float* const* weightChannels, float* const* bodyChannels,
    float* const* biteChannels, float* const* fizzAirChannels) noexcept
{
    return processSplitImpl (
        inputChannels, numChannels, numSamples, weightChannels,
        bodyChannels, biteChannels, fizzAirChannels);
}

bool MultibandCrossover::processSplit (
    const double* const* inputChannels, int numChannels, int numSamples,
    double* const* weightChannels, double* const* bodyChannels,
    double* const* biteChannels, double* const* fizzAirChannels) noexcept
{
    return processSplitImpl (
        inputChannels, numChannels, numSamples, weightChannels,
        bodyChannels, biteChannels, fizzAirChannels);
}

template <typename SampleType>
bool MultibandCrossover::processSplitImpl (
    const SampleType* const* inputChannels,
    int numChannels, int numSamples,
    SampleType* const* weightChannels,
    SampleType* const* bodyChannels,
    SampleType* const* biteChannels,
    SampleType* const* fizzAirChannels) noexcept
{
    if (numChannels <= 0
        || numChannels > static_cast<int> (maximumChannels)
        || numSamples <= 0
        || ! validChannelPointers (
            inputChannels, weightChannels, bodyChannels,
            biteChannels, fizzAirChannels, numChannels))
        return false;

    const auto targets = orderedTargets();
    const auto rampLength = smoothingSamples();
    for (std::size_t index = 0; index < numberOfCrossovers; ++index)
        frequencySmoothers[index].setTarget (targets[index], rampLength);

    for (int sample = 0; sample < numSamples; ++sample)
    {
        std::array<double, numberOfCrossovers> frequencies {};
        for (std::size_t index = 0; index < numberOfCrossovers; ++index)
            frequencies[index] = frequencySmoothers[index].next();

        // Endpoint ordering is protected above; enforce the guard on every
        // interpolated sample as well so oppositely moving automation lanes
        // can never momentarily collide.
        frequencies[1] = std::max (
            frequencies[1], frequencies[0] + orderingGuardHz);
        frequencies[2] = std::max (
            frequencies[2], frequencies[1] + orderingGuardHz);

        for (std::size_t index = 0; index < numberOfCrossovers; ++index)
            effectiveFrequencies[index].store (
                frequencies[index], std::memory_order_relaxed);
        if (frequencies != configuredFrequencies)
            updateCoefficients (frequencies);

        for (int channel = 0; channel < numChannels; ++channel)
        {
            auto& state = channelStates[static_cast<std::size_t> (channel)];
            const auto input =
                static_cast<double> (inputChannels[channel][sample]);

            const auto weightUncompensated = processPair (
                input, coefficients[0].lowPass,
                state.splits[0].lowPass);
            const auto aboveWeight = processPair (
                input, coefficients[0].highPass,
                state.splits[0].highPass);

            const auto bodyUncompensated = processPair (
                aboveWeight, coefficients[1].lowPass,
                state.splits[1].lowPass);
            const auto aboveBody = processPair (
                aboveWeight, coefficients[1].highPass,
                state.splits[1].highPass);

            const auto bite = processPair (
                aboveBody, coefficients[2].lowPass,
                state.splits[2].lowPass);
            const auto fizzAir = processPair (
                aboveBody, coefficients[2].highPass,
                state.splits[2].highPass);

            // LP^2 + HP^2 at each LR4 split is a fourth-order all-pass.
            // Applying later split all-passes to lower branches makes the
            // unity sum A1 * A2 * A3, hence flat in magnitude.
            auto weight = processSection (
                weightUncompensated, coefficients[1].allPass,
                state.weightAtSecondSplit.section);
            weight = processSection (
                weight, coefficients[2].allPass,
                state.weightAtThirdSplit.section);
            const auto body = processSection (
                bodyUncompensated, coefficients[2].allPass,
                state.bodyAtThirdSplit.section);

            weightChannels[channel][sample] =
                static_cast<SampleType> (weight);
            bodyChannels[channel][sample] =
                static_cast<SampleType> (body);
            biteChannels[channel][sample] =
                static_cast<SampleType> (bite);
            fizzAirChannels[channel][sample] =
                static_cast<SampleType> (fizzAir);
        }
    }

    return true;
}

template <typename SampleType>
[[nodiscard]] bool processAudioBuffer (
    MultibandCrossover& crossover,
    const juce::AudioBuffer<SampleType>& input,
    std::array<juce::AudioBuffer<SampleType>*,
               MultibandCrossover::numberOfZones> zones) noexcept
{
    const auto numChannels = input.getNumChannels();
    const auto numSamples = input.getNumSamples();
    for (auto* zone : zones)
        if (zone == nullptr || zone->getNumChannels() < numChannels
            || zone->getNumSamples() < numSamples)
            return false;

    return crossover.processSplit (
        input.getArrayOfReadPointers(), numChannels, numSamples,
        zones[0]->getArrayOfWritePointers(),
        zones[1]->getArrayOfWritePointers(),
        zones[2]->getArrayOfWritePointers(),
        zones[3]->getArrayOfWritePointers());
}

bool MultibandCrossover::processSplit (
    const juce::AudioBuffer<float>& input,
    std::array<juce::AudioBuffer<float>*, numberOfZones> zones) noexcept
{
    return processAudioBuffer (*this, input, zones);
}

bool MultibandCrossover::processSplit (
    const juce::AudioBuffer<double>& input,
    std::array<juce::AudioBuffer<double>*, numberOfZones> zones) noexcept
{
    return processAudioBuffer (*this, input, zones);
}

bool MultibandCrossover::sumZones (
    const float* const* weightChannels,
    const float* const* bodyChannels,
    const float* const* biteChannels,
    const float* const* fizzAirChannels,
    float* const* outputChannels, int numChannels,
    int numSamples) noexcept
{
    return sumZonesImpl (
        weightChannels, bodyChannels, biteChannels, fizzAirChannels,
        outputChannels, numChannels, numSamples);
}

bool MultibandCrossover::sumZones (
    const double* const* weightChannels,
    const double* const* bodyChannels,
    const double* const* biteChannels,
    const double* const* fizzAirChannels,
    double* const* outputChannels, int numChannels,
    int numSamples) noexcept
{
    return sumZonesImpl (
        weightChannels, bodyChannels, biteChannels, fizzAirChannels,
        outputChannels, numChannels, numSamples);
}

template <typename SampleType>
bool MultibandCrossover::sumZonesImpl (
    const SampleType* const* weightChannels,
    const SampleType* const* bodyChannels,
    const SampleType* const* biteChannels,
    const SampleType* const* fizzAirChannels,
    SampleType* const* outputChannels,
    int numChannels, int numSamples) noexcept
{
    if (numChannels <= 0 || numSamples <= 0
        || ! validOutputPointers (
            weightChannels, bodyChannels, biteChannels,
            fizzAirChannels, outputChannels, numChannels))
        return false;

    for (int channel = 0; channel < numChannels; ++channel)
        for (int sample = 0; sample < numSamples; ++sample)
            outputChannels[channel][sample] =
                weightChannels[channel][sample]
                + bodyChannels[channel][sample]
                + biteChannels[channel][sample]
                + fizzAirChannels[channel][sample];

    return true;
}

template <typename SampleType>
[[nodiscard]] bool sumAudioBuffers (
    const std::array<const juce::AudioBuffer<SampleType>*,
                     MultibandCrossover::numberOfZones>& zones,
    juce::AudioBuffer<SampleType>& output) noexcept
{
    const auto numChannels = output.getNumChannels();
    const auto numSamples = output.getNumSamples();
    for (const auto* zone : zones)
        if (zone == nullptr || zone->getNumChannels() < numChannels
            || zone->getNumSamples() < numSamples)
            return false;

    return MultibandCrossover::sumZones (
        zones[0]->getArrayOfReadPointers(),
        zones[1]->getArrayOfReadPointers(),
        zones[2]->getArrayOfReadPointers(),
        zones[3]->getArrayOfReadPointers(),
        output.getArrayOfWritePointers(), numChannels, numSamples);
}

bool MultibandCrossover::sumZones (
    const std::array<const juce::AudioBuffer<float>*, numberOfZones>& zones,
    juce::AudioBuffer<float>& output) noexcept
{
    return sumAudioBuffers (zones, output);
}

bool MultibandCrossover::sumZones (
    const std::array<const juce::AudioBuffer<double>*, numberOfZones>& zones,
    juce::AudioBuffer<double>& output) noexcept
{
    return sumAudioBuffers (zones, output);
}

void MultibandCrossover::setCrossoverHz (
    std::size_t crossoverIndex, double frequencyHz) noexcept
{
    if (crossoverIndex >= numberOfCrossovers)
        return;

    const auto fallback = defaultCrossoverHz[crossoverIndex];
    targetFrequencies[crossoverIndex].store (
        std::clamp (
            finiteOr (frequencyHz, fallback),
            minimumCrossoverHz[crossoverIndex],
            maximumCrossoverHz[crossoverIndex]),
        std::memory_order_relaxed);
}

void MultibandCrossover::setCrossoversHz (
    double weightBodyHz, double bodyBiteHz,
    double biteFizzHz) noexcept
{
    setCrossoverHz (0, weightBodyHz);
    setCrossoverHz (1, bodyBiteHz);
    setCrossoverHz (2, biteFizzHz);
}

void MultibandCrossover::setWeightBodyCrossoverHz (
    double frequencyHz) noexcept
{
    setCrossoverHz (0, frequencyHz);
}

void MultibandCrossover::setBodyBiteCrossoverHz (
    double frequencyHz) noexcept
{
    setCrossoverHz (1, frequencyHz);
}

void MultibandCrossover::setBiteFizzCrossoverHz (
    double frequencyHz) noexcept
{
    setCrossoverHz (2, frequencyHz);
}

double MultibandCrossover::getTargetCrossoverHz (
    std::size_t crossoverIndex) const noexcept
{
    if (crossoverIndex >= numberOfCrossovers)
        return 0.0;
    return orderedTargets()[crossoverIndex];
}

double MultibandCrossover::getEffectiveCrossoverHz (
    std::size_t crossoverIndex) const noexcept
{
    if (crossoverIndex >= numberOfCrossovers)
        return 0.0;
    return effectiveFrequencies[crossoverIndex].load (
        std::memory_order_relaxed);
}

std::array<double, MultibandCrossover::numberOfCrossovers>
MultibandCrossover::getEffectiveCrossoversHz() const noexcept
{
    return {
        getEffectiveCrossoverHz (0),
        getEffectiveCrossoverHz (1),
        getEffectiveCrossoverHz (2)
    };
}

double MultibandCrossover::processSection (
    double input, const BiquadCoefficients& section,
    SectionState& state) noexcept
{
    const auto output = section.b0 * input + state.s1;
    state.s1 =
        section.b1 * input - section.a1 * output + state.s2;
    state.s2 =
        section.b2 * input - section.a2 * output;
    if (std::abs (state.s1) < stateFlushThreshold)
        state.s1 = 0.0;
    if (std::abs (state.s2) < stateFlushThreshold)
        state.s2 = 0.0;
    return output;
}

double MultibandCrossover::processPair (
    double input, const BiquadCoefficients& section,
    std::array<SectionState, 2>& states) noexcept
{
    auto output = processSection (input, section, states[0]);
    return processSection (output, section, states[1]);
}

BiquadCoefficients MultibandCrossover::makeAllPass (
    const BiquadCoefficients& butterworthSection) noexcept
{
    // For matched Butterworth sections, BW2_LP^2 + BW2_HP^2 has numerator
    // D(z) reverse(D(z)) and denominator D(z)^2. Cancelling the common D(z)
    // leaves this second-order all-pass.
    return {
        butterworthSection.a2,
        butterworthSection.a1,
        1.0,
        butterworthSection.a1,
        butterworthSection.a2,
        SectionOrder::second
    };
}

void MultibandCrossover::updateCoefficients (
    const std::array<double, numberOfCrossovers>& frequencies) noexcept
{
    for (std::size_t index = 0; index < numberOfCrossovers; ++index)
    {
        const FilterParameters parameters {
            currentSampleRate, frequencies[index], butterworthQ, 0.0
        };
        coefficients[index].lowPass =
            BiquadCoefficients::makeSecondOrder (
                FilterKind::lowPass, parameters);
        coefficients[index].highPass =
            BiquadCoefficients::makeSecondOrder (
                FilterKind::highPass, parameters);
        coefficients[index].allPass =
            makeAllPass (coefficients[index].lowPass);
    }
    configuredFrequencies = frequencies;
}

std::array<double, MultibandCrossover::numberOfCrossovers>
MultibandCrossover::orderedTargets() const noexcept
{
    std::array<double, numberOfCrossovers> result {};
    for (std::size_t index = 0; index < numberOfCrossovers; ++index)
        result[index] = std::clamp (
            finiteOr (
                targetFrequencies[index].load (std::memory_order_relaxed),
                defaultCrossoverHz[index]),
            minimumCrossoverHz[index], maximumCrossoverHz[index]);

    // The ranges leave enough room on both sides of the middle crossover.
    // Preserve its automated value and move colliding outer boundaries to
    // the nearest valid ordered position.
    result[0] = std::min (result[0], result[1] - orderingGuardHz);
    result[2] = std::max (result[2], result[1] + orderingGuardHz);
    result[0] = std::clamp (
        result[0], minimumCrossoverHz[0], maximumCrossoverHz[0]);
    result[2] = std::clamp (
        result[2], minimumCrossoverHz[2], maximumCrossoverHz[2]);

    // Keep the implemented digital filters ordered below Nyquist even at
    // unusual host sample rates. At ordinary rates this does not alter the
    // specification ranges.
    const auto nyquistLimit = 0.499 * currentSampleRate;
    result[2] = std::min (result[2], nyquistLimit);
    result[1] = std::min (result[1], result[2] - orderingGuardHz);
    result[0] = std::min (result[0], result[1] - orderingGuardHz);
    return result;
}

int MultibandCrossover::smoothingSamples() const noexcept
{
    return std::max (
        1, static_cast<int> (
               std::round (currentSampleRate * smoothingTimeSeconds)));
}

} // namespace ShredEQ::DSP
