#pragma once

#include "../DSP/ParametricBand.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace ShredEQ::UI
{

/**
    Visual representation of a parametric band. EQWorkspace owns the nodes and
    performs parameter editing; this component deliberately has no APVTS or
    editor dependency.
*/
class BandNode final : public juce::Component
{
public:
    enum class Handle
    {
        none,
        centre,
        range
    };

    struct VisualState
    {
        int bandNumber = 1;
        DSP::BandFilterType filterType = DSP::BandFilterType::bell;
        DSP::BandStage stage = DSP::BandStage::stageA;
        DSP::StereoRoutingMode routing = DSP::StereoRoutingMode::linked;
        float staticGainDb = 0.0f;
        float dynamicRangeDb = 0.0f;
        float currentGainDb = 0.0f;
        float dynamicActivity = 0.0f;
        bool dynamicsEnabled = false;
        bool tracking = false;
        bool sidechain = false;
        bool bypassed = false;
        bool selected = false;
    };

    BandNode();

    void paint (juce::Graphics&) override;

    void setVisualState (const VisualState&);
    [[nodiscard]] const VisualState& getVisualState() const noexcept;

    void setBandNumber (int);
    void setFilterType (DSP::BandFilterType);
    void setStage (DSP::BandStage);
    void setRouting (DSP::StereoRoutingMode);
    void setStaticGainDb (float);
    void setDynamicRangeDb (float);
    void setCurrentGainDb (float);
    void setDynamicActivity (float);
    void setDynamicsEnabled (bool);
    void setTracking (bool);
    void setSidechain (bool);
    void setBypassed (bool);
    void setSelected (bool);

    /** Positions are in this component's local coordinate system. */
    void setHandlePositions (juce::Point<float> centre,
                             juce::Point<float> range,
                             juce::Point<float> live);
    [[nodiscard]] juce::Point<float> getCentreHandlePosition() const noexcept;
    [[nodiscard]] juce::Point<float> getRangeHandlePosition() const noexcept;
    [[nodiscard]] juce::Point<float> getLivePosition() const noexcept;

    [[nodiscard]] Handle hitTestHandle (juce::Point<float>) const noexcept;
    [[nodiscard]] bool isCentreHandleHit (juce::Point<float>) const noexcept;
    [[nodiscard]] bool isRangeHandleHit (juce::Point<float>) const noexcept;
    [[nodiscard]] bool isSoloHit (juce::Point<float>) const noexcept;
    [[nodiscard]] bool isHearRemovedHit (juce::Point<float>) const noexcept;

    [[nodiscard]] juce::Rectangle<float> getSoloHitArea() const noexcept;
    [[nodiscard]] juce::Rectangle<float> getHearRemovedHitArea() const noexcept;

    static constexpr float centreHandleRadius = 10.0f;
    static constexpr float rangeHandleRadius = 6.0f;

private:
    void drawFilterGlyph (juce::Graphics&, juce::Rectangle<float>) const;
    void drawRoutingGlyph (juce::Graphics&, juce::Rectangle<float>) const;

    VisualState state;
    juce::Point<float> centrePosition { 32.0f, 32.0f };
    juce::Point<float> rangePosition { 32.0f, 14.0f };
    juce::Point<float> livePosition { 32.0f, 32.0f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BandNode)
};

} // namespace ShredEQ::UI
