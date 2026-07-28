#pragma once

#include "BandNode.h"
#include "PercentileCloud.h"

#include <array>
#include <functional>
#include <span>

namespace ShredEQ::UI
{

/**
    Main log-frequency EQ editing surface. Data views are non-owning: their
    backing storage must remain valid until replaced or cleared.
*/
class EQWorkspace final : public juce::Component
{
public:
    static constexpr int maximumBands = 12;

    enum class Layer
    {
        grid,
        preSpectrum,
        postSpectrum,
        percentileCloud,
        dynamicRangeMap,
        resonanceCandidates,
        noteGuides,
        sidechainCollisions,
        compositeResponse,
        stageAResponse,
        adaptiveResponse,
        stageBResponse,
        phaseResponse,
        bandNodes,
        count
    };

    enum class EditedParameter
    {
        frequency,
        gain,
        q,
        dynamicRange
    };

    struct CurveView
    {
        std::span<const double> frequencyHz;
        std::span<const float> values;

        [[nodiscard]] bool isValid() const noexcept
        {
            return ! values.empty()
                && (frequencyHz.empty() || frequencyHz.size() == values.size());
        }
    };

    struct ResonanceCandidate
    {
        float frequencyHz = 1000.0f;
        float confidence = 0.0f;
        float excessDb = 0.0f;
    };

    struct NoteGuide
    {
        float frequencyHz = 440.0f;
        juce::String label;
        float emphasis = 0.5f;
    };

    struct SidechainCollision
    {
        float lowFrequencyHz = 100.0f;
        float highFrequencyHz = 200.0f;
        float intensity = 0.0f;
    };

    struct BandState
    {
        bool visible = false;
        float frequencyHz = 1000.0f;
        float gainDb = 0.0f;
        float q = 0.707f;
        BandNode::VisualState visual;
    };

    EQWorkspace();

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&,
                         const juce::MouseWheelDetails&) override;
    void mouseMagnify (const juce::MouseEvent&, float scaleFactor) override;
    bool keyPressed (const juce::KeyPress&) override;

    void setFrequencyRange (float minimumHz, float maximumHz);
    [[nodiscard]] float getMinimumFrequency() const noexcept;
    [[nodiscard]] float getMaximumFrequency() const noexcept;
    void setGainRangeDb (float absoluteRangeDb);
    [[nodiscard]] float getGainRangeDb() const noexcept;

    [[nodiscard]] float frequencyToX (float frequencyHz) const noexcept;
    [[nodiscard]] float xToFrequency (float x) const noexcept;
    [[nodiscard]] float gainToY (float gainDb) const noexcept;
    [[nodiscard]] float yToGain (float y) const noexcept;
    [[nodiscard]] juce::Rectangle<float> getPlotBounds() const noexcept;

    void setLayerVisible (Layer, bool);
    [[nodiscard]] bool isLayerVisible (Layer) const noexcept;
    void setShowGrid (bool value)                 { setLayerVisible (Layer::grid, value); }
    void setShowPreSpectrum (bool value)          { setLayerVisible (Layer::preSpectrum, value); }
    void setShowPostSpectrum (bool value)         { setLayerVisible (Layer::postSpectrum, value); }
    void setShowPercentileCloud (bool value)      { setLayerVisible (Layer::percentileCloud, value); }
    void setShowDynamicRangeMap (bool value)      { setLayerVisible (Layer::dynamicRangeMap, value); }
    void setShowResonanceCandidates (bool value)  { setLayerVisible (Layer::resonanceCandidates, value); }
    void setShowNoteGuides (bool value)           { setLayerVisible (Layer::noteGuides, value); }
    void setShowSidechainCollisions (bool value)  { setLayerVisible (Layer::sidechainCollisions, value); }
    void setShowCompositeResponse (bool value)    { setLayerVisible (Layer::compositeResponse, value); }
    void setShowStageAResponse (bool value)       { setLayerVisible (Layer::stageAResponse, value); }
    void setShowAdaptiveResponse (bool value)     { setLayerVisible (Layer::adaptiveResponse, value); }
    void setShowStageBResponse (bool value)       { setLayerVisible (Layer::stageBResponse, value); }
    void setShowPhaseResponse (bool value)        { setLayerVisible (Layer::phaseResponse, value); }
    void setShowBandNodes (bool value)            { setLayerVisible (Layer::bandNodes, value); }

    void setPreSpectrumView (CurveView);
    void setPostSpectrumView (CurveView);
    void setCompositeResponseView (CurveView);
    void setStageAResponseView (CurveView);
    void setAdaptiveResponseView (CurveView);
    void setStageBResponseView (CurveView);
    /** Phase values are degrees and are displayed over -180..+180. */
    void setPhaseResponseView (CurveView);
    void clearCurveViews();

    /** The snapshot remains owned by the caller. Pass nullptr to clear it. */
    void setPercentileCloudSnapshot (const PercentileCloud::Snapshot*);
    void setResonanceCandidates (std::span<const ResonanceCandidate>);
    void setNoteGuides (std::span<const NoteGuide>);
    void setSidechainCollisions (std::span<const SidechainCollision>);

    void setBandState (int bandIndex, const BandState&);
    void setBandCurrentGainDb (int bandIndex, float currentGainDb);
    void setBandDynamicActivity (int bandIndex, float activity);
    [[nodiscard]] const BandState* getBandState (int bandIndex) const noexcept;
    [[nodiscard]] BandNode* getBandNode (int bandIndex) noexcept;
    [[nodiscard]] const BandNode* getBandNode (int bandIndex) const noexcept;

    void setSelectedBandIndex (int bandIndex);
    [[nodiscard]] int getSelectedBandIndex() const noexcept;

    // Editor/APVTS integration callbacks. Values are already clamped to the
    // workspace's supported ranges.
    std::function<void (float frequencyHz)> onCreateBellBand;
    std::function<void (float frequencyHz)> onRequestFilterTypeSelection;
    std::function<void (int bandIndex)> onRequestNumericalEntry;
    std::function<void (int bandIndex, EditedParameter, float value)> onBandEdited;
    std::function<void (int bandIndex, EditedParameter, bool gestureIsStarting)> onEditGesture;
    std::function<void (int bandIndex)> onDeleteBand;
    std::function<void (int bandIndex)> onSoloBand;
    std::function<void (int bandIndex)> onHearRemoved;
    std::function<void (int bandIndex)> onSelectedBandChanged;

private:
    struct Hit
    {
        int bandIndex = -1;
        BandNode::Handle handle = BandNode::Handle::none;
        bool solo = false;
        bool hearRemoved = false;
    };

    [[nodiscard]] Hit findHit (juce::Point<float>) const noexcept;
    void updateNodePositions();
    void updateBandValue (int, EditedParameter, float);
    void paintGrid (juce::Graphics&) const;
    void paintSpectrum (juce::Graphics&, CurveView, juce::Colour) const;
    void paintPercentileCloud (juce::Graphics&) const;
    void paintDynamicRangeMap (juce::Graphics&) const;
    void paintResonances (juce::Graphics&) const;
    void paintNotes (juce::Graphics&) const;
    void paintCollisions (juce::Graphics&) const;
    void paintResponse (juce::Graphics&, CurveView, juce::Colour,
                        float thickness) const;
    void paintPhase (juce::Graphics&) const;
    [[nodiscard]] juce::Path makePath (CurveView,
                                       std::function<float (float)> yMapper) const;
    [[nodiscard]] float spectrumDbToY (float valueDb) const noexcept;

    static constexpr std::size_t layerCount =
        static_cast<std::size_t> (Layer::count);
    std::array<bool, layerCount> layers {};
    std::array<BandNode, maximumBands> nodes;
    std::array<BandState, maximumBands> bands;

    CurveView preSpectrum;
    CurveView postSpectrum;
    CurveView compositeResponse;
    CurveView stageAResponse;
    CurveView adaptiveResponse;
    CurveView stageBResponse;
    CurveView phaseResponse;
    const PercentileCloud::Snapshot* percentileSnapshot = nullptr;
    std::span<const ResonanceCandidate> resonances;
    std::span<const NoteGuide> notes;
    std::span<const SidechainCollision> collisions;

    float minimumFrequencyHz = 20.0f;
    float maximumFrequencyHz = 20000.0f;
    float gainRangeDb = 24.0f;
    int selectedBandIndex = -1;

    Hit dragHit;
    juce::Point<float> dragStart;
    float dragStartFrequency = 1000.0f;
    float dragStartGain = 0.0f;
    float dragStartRange = 0.0f;
    bool gestureActive = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EQWorkspace)
};

} // namespace ShredEQ::UI
