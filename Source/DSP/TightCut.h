#pragma once

#include "BiquadCoefficients.h"

#include <array>
#include <atomic>
#include <cstddef>

namespace ShredEQ::DSP
{

enum class TightCutPlacement
{
    preStageA = 0,
    postStageA
};

enum class TightCutRouting
{
    stereo = 0,
    mid,
    side
};

/**
    Resonant high-pass and linked punch filter.

    Mid/side matrixing is deliberately owned by the surrounding stage router.
    Pass isMidSideDomain=true when channels 0 and 1 already contain Mid and
    Side. In the normal Left/Right domain, mid- and side-only routes pass
    through unchanged.
*/
class TightCut final
{
public:
    using Placement = TightCutPlacement;
    using Routing = TightCutRouting;

    TightCut() noexcept = default;

    void prepare (double sampleRate, int maxBlockSize) noexcept;
    void reset() noexcept;

    void process (float* const* channels, int numChannels, int numSamples,
                  bool isMidSideDomain = false) noexcept;

    void setCutoff (double frequencyHz) noexcept;
    void setFrequency (double frequencyHz) noexcept { setCutoff (frequencyHz); }
    [[nodiscard]] double getCutoff() const noexcept;
    [[nodiscard]] double getFrequency() const noexcept { return getCutoff(); }

    void setSlope (FilterSlope newSlope) noexcept;
    void setSlope (int slopeDbPerOctave) noexcept;
    [[nodiscard]] FilterSlope getSlope() const noexcept;

    void setResonance (double q) noexcept;
    [[nodiscard]] double getResonance() const noexcept;

    void setPunchLinked (bool shouldBeLinked) noexcept;
    [[nodiscard]] bool isPunchLinked() const noexcept;
    void setPunchRatio (double ratio) noexcept;
    [[nodiscard]] double getPunchRatio() const noexcept;
    void setPunchFrequency (double frequencyHz) noexcept;
    [[nodiscard]] double getPunchFrequency() const noexcept;
    [[nodiscard]] double getEffectivePunchFrequency() const noexcept;
    void setPunchGainDb (double gainDb) noexcept;
    [[nodiscard]] double getPunchGainDb() const noexcept;
    void setPunchQ (double q) noexcept;
    [[nodiscard]] double getPunchQ() const noexcept;

    void setPlacement (Placement newPlacement) noexcept;
    [[nodiscard]] Placement getPlacement() const noexcept;
    void setRouting (Routing newRouting) noexcept;
    [[nodiscard]] Routing getRouting() const noexcept;
    void setRoutingMode (Routing newRouting) noexcept { setRouting (newRouting); }
    [[nodiscard]] Routing getRoutingMode() const noexcept { return getRouting(); }

    void setEnabled (bool shouldBeEnabled) noexcept;
    [[nodiscard]] bool isEnabled() const noexcept;
    void setListenRemoved (bool shouldListen) noexcept;
    [[nodiscard]] bool isListenRemoved() const noexcept;
    [[nodiscard]] bool isListenActive() const noexcept;

private:
    struct SectionState
    {
        double s1 = 0.0;
        double s2 = 0.0;
    };

    struct CascadeState
    {
        std::array<SectionState, maxCascadeSections> sections {};
    };

    struct LinearRamp
    {
        void reset (double value) noexcept;
        void setTarget (double value, int durationSamples) noexcept;
        [[nodiscard]] double next() noexcept;

        double current = 0.0;
        double target = 0.0;
        double increment = 0.0;
        int remaining = 0;
    };

    struct ParameterSmoother
    {
        void reset (double value) noexcept;
        void setTarget (double value, int durationSamples) noexcept;
        [[nodiscard]] double next() noexcept;

        double current = 0.0;
        double target = 0.0;
        double multiplier = 1.0;
        int remaining = 0;
    };

    [[nodiscard]] static double processSection (
        double input, const BiquadCoefficients& coefficients,
        SectionState& state) noexcept;
    [[nodiscard]] static double processCascade (
        double input, const CoefficientCascade& cascade,
        CascadeState& state) noexcept;
    [[nodiscard]] bool routeSelectsChannel (
        int channel, bool isMidSideDomain) const noexcept;
    void beginSlopeTransition (FilterSlope newSlope) noexcept;
    void updateCoefficients (double cutoffHz, double resonance,
                             double punchHz, double punchGainDb,
                             double punchQ) noexcept;
    [[nodiscard]] int smoothingSamples() const noexcept;

    static constexpr double minimumCutoffHz = 25.0;
    static constexpr double maximumCutoffHz = 220.0;
    static constexpr double minimumResonance = 0.5;
    static constexpr double maximumResonance = 2.5;
    static constexpr double minimumPunchRatio = 0.75;
    static constexpr double maximumPunchRatio = 1.5;
    static constexpr double minimumPunchFrequencyHz =
        minimumCutoffHz * minimumPunchRatio;
    static constexpr double maximumPunchFrequencyHz =
        maximumCutoffHz * maximumPunchRatio;
    static constexpr double minimumPunchGainDb = 0.0;
    static constexpr double maximumPunchGainDb = 8.0;
    static constexpr double minimumPunchQ = 0.35;
    static constexpr double maximumPunchQ = 3.0;
    static constexpr double smoothingTimeSeconds = 0.02;
    static constexpr double stateFlushThreshold = 1.0e-24;

    std::atomic<double> targetCutoffHz { 80.0 };
    std::atomic<double> targetResonance { 0.707 };
    std::atomic<double> targetPunchRatio { 1.0 };
    std::atomic<double> targetPunchFrequencyHz { 80.0 };
    std::atomic<double> targetPunchGainDb { 0.0 };
    std::atomic<double> targetPunchQ { 0.707 };
    std::atomic<FilterSlope> targetSlope { FilterSlope::dB24 };
    std::atomic<Placement> placement { Placement::preStageA };
    std::atomic<Routing> routing { Routing::stereo };
    std::atomic<bool> punchLinked { true };
    std::atomic<bool> enabled { false };
    std::atomic<bool> listenRemoved { false };

    ParameterSmoother cutoffSmoother;
    ParameterSmoother resonanceSmoother;
    ParameterSmoother punchFrequencySmoother;
    ParameterSmoother punchGainSmoother;
    ParameterSmoother punchQSmoother;
    LinearRamp slopeCrossfade;
    LinearRamp enabledMix;
    LinearRamp listenMix;
    std::array<LinearRamp, 2> routeMix;

    std::array<CoefficientCascade, 2> highPassCascades;
    std::array<std::array<CascadeState, 2>, 2> highPassStates;
    BiquadCoefficients punchCoefficients;
    std::array<SectionState, 2> punchStates;

    double currentSampleRate = 44100.0;
    FilterSlope currentSlope = FilterSlope::dB24;
    FilterSlope fadingFromSlope = FilterSlope::dB24;
    int currentHighPassBank = 0;
};

} // namespace ShredEQ::DSP
