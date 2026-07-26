#pragma once

namespace ShredEQ::DSP
{

/**
    Peak envelope detector with independent attack and release ballistics.

    The input to processSample may be either a signed audio sample or an
    already rectified detector value; rectification is applied internally.
*/
class EnvelopeFollower final
{
public:
    EnvelopeFollower() noexcept = default;

    void prepare (double sampleRate) noexcept;
    void reset() noexcept;

    void setAttackMs (double attackMs) noexcept;
    void setReleaseMs (double releaseMs) noexcept;
    void setTimeConstants (double attackMs, double releaseMs) noexcept;

    [[nodiscard]] float processSample (float sample) noexcept;

    [[nodiscard]] float levelLinear() const noexcept;
    [[nodiscard]] float levelDb() const noexcept;

private:
    void updateCoefficients() noexcept;

    static constexpr double defaultSampleRate = 44100.0;
    static constexpr double detectorEpsilon = 1.0e-12;
    static constexpr double denormalThreshold = 1.0e-30;

    double currentSampleRate = defaultSampleRate;
    double attackTimeMs = 10.0;
    double releaseTimeMs = 150.0;
    double attackAlpha = 0.0;
    double releaseAlpha = 0.0;
    double envelope = 0.0;
};

/**
    Evaluates the soft-knee transfer F_K(u), where u is the detector level
    above threshold in dB. A non-positive knee width selects a hard knee.
*/
[[nodiscard]] double computeSoftKneeCurveDb (
    double levelAboveThresholdDb, double kneeWidthDb) noexcept;

/**
    Computes a dynamic-EQ gain target in dB.

    Downward action returns a value in [-maximumRangeDb, 0]. Upward action
    reverses the sign and returns a value in [0, maximumRangeDb].
*/
[[nodiscard]] double computeSoftKneeGainDb (
    double levelDb, double thresholdDb, double ratio, double kneeWidthDb,
    double maximumRangeDb, bool upward = false) noexcept;

/**
    Optional gain-domain smoother, independent of detector ballistics.

    Attack is used while the absolute gain action increases and release while
    it returns toward zero.
*/
class GainSmootherDb final
{
public:
    GainSmootherDb() noexcept = default;

    void prepare (double sampleRate) noexcept;
    void reset (double gainDb = 0.0) noexcept;

    void setAttackMs (double attackMs) noexcept;
    void setReleaseMs (double releaseMs) noexcept;
    void setTimeConstants (double attackMs, double releaseMs) noexcept;

    [[nodiscard]] double processSample (double targetGainDb) noexcept;
    [[nodiscard]] double currentGainDb() const noexcept;

private:
    void updateCoefficients() noexcept;

    static constexpr double defaultSampleRate = 44100.0;
    static constexpr double denormalThreshold = 1.0e-30;

    double currentSampleRate = defaultSampleRate;
    double attackTimeMs = 5.0;
    double releaseTimeMs = 50.0;
    double attackAlpha = 0.0;
    double releaseAlpha = 0.0;
    double gain = 0.0;
};

struct TransientSustainLevels
{
    float transient = 0.0f;
    float sustain = 0.0f;
};

/**
    Separates detector magnitude into transient and sustain components using
    independently configured fast and slow envelope followers.
*/
class TransientSustainSeparator final
{
public:
    TransientSustainSeparator() noexcept = default;

    void prepare (double sampleRate) noexcept;
    void reset() noexcept;

    void setFastAttackMs (double attackMs) noexcept;
    void setFastReleaseMs (double releaseMs) noexcept;
    void setFastTimeConstants (double attackMs, double releaseMs) noexcept;

    void setSlowAttackMs (double attackMs) noexcept;
    void setSlowReleaseMs (double releaseMs) noexcept;
    void setSlowTimeConstants (double attackMs, double releaseMs) noexcept;

    void setK (double newK) noexcept;
    void setLambda (double newLambda) noexcept;

    [[nodiscard]] TransientSustainLevels processSample (float sample) noexcept;

    [[nodiscard]] float fastEnvelope() const noexcept;
    [[nodiscard]] float slowEnvelope() const noexcept;
    [[nodiscard]] float transientLevel() const noexcept;
    [[nodiscard]] float sustainLevel() const noexcept;

private:
    EnvelopeFollower fastFollower;
    EnvelopeFollower slowFollower;

    double k = 1.0;
    double lambda = 0.0;
    float transient = 0.0f;
    float sustain = 0.0f;
};

} // namespace ShredEQ::DSP
