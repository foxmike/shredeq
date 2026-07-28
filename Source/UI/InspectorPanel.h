#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <functional>
#include <memory>
#include <vector>

namespace ShredEQ::UI
{

/**
    Full-width editor for every essential parameter of the selected EQ band.

    A negative or out-of-range band index clears the attachments and presents
    an empty state. Solo and delta audition are callbacks because the current
    parameter model does not persist either state per band.
*/
class InspectorPanel final : public juce::Component,
                             private juce::Timer
{
public:
    explicit InspectorPanel (juce::AudioProcessorValueTreeState&);
    ~InspectorPanel() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    void setSelectedBand (int bandIndex);
    [[nodiscard]] int getSelectedBand() const noexcept { return selectedBand; }

    /** Useful to the parent editor when audition state changes elsewhere. */
    void setSoloState (bool);
    void setDeltaState (bool);

    /** Preferred docking height for the current width. */
    [[nodiscard]] int getPreferredHeightForWidth (int width) const noexcept;

    std::function<void (int bandIndex, bool shouldSolo)> onSoloChanged;
    std::function<void (int bandIndex, bool shouldHearRemoved)> onDeltaChanged;

private:
    struct SliderField final : juce::Component
    {
        SliderField();
        void setCaption (const juce::String&);
        void refreshReadout();
        void resized() override;

        juce::Label caption;
        juce::Slider slider;
        juce::Label readout;
    };

    struct ChoiceField final : juce::Component
    {
        ChoiceField();
        void setCaption (const juce::String&);
        void resized() override;

        juce::Label caption;
        juce::ComboBox combo;
    };

    using SliderAttachment =
        juce::AudioProcessorValueTreeState::SliderAttachment;
    using ComboBoxAttachment =
        juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using ButtonAttachment =
        juce::AudioProcessorValueTreeState::ButtonAttachment;

    void timerCallback() override;
    void clearAttachments();
    void attachSlider (SliderField&, const char* parameterID);
    void attachChoice (ChoiceField&, const char* parameterID);
    void setEditorVisible (bool);
    [[nodiscard]] std::vector<juce::Component*> getGridComponents();

    juce::AudioProcessorValueTreeState& apvts;
    int selectedBand = -1;

    juce::Label bandTitle;
    juce::Label emptyState;

    SliderField frequency;
    SliderField gain;
    SliderField q;
    ChoiceField filterType;
    ChoiceField slope;
    ChoiceField stage;
    ChoiceField routing;

    ChoiceField dynamicMode;
    SliderField threshold;
    SliderField dynamicRange;
    SliderField attack;
    SliderField hold;
    SliderField release;
    SliderField knee;

    ChoiceField detectorSource;
    SliderField detectorFrequency;
    SliderField detectorQ;

    juce::ToggleButton trackingEnable { "Tracking" };
    ChoiceField trackingSource;
    juce::ToggleButton soloButton { "Solo" };
    juce::ToggleButton deltaButton { "Delta" };
    juce::ToggleButton bypassButton { "Bypass" };

    // ButtonAttachment is connected to this hidden implementation control.
    // bypassButton presents the inverse of the modeled "enabled" parameter.
    juce::ToggleButton enabledParameterButton;

    std::vector<std::unique_ptr<SliderAttachment>> sliderAttachments;
    std::vector<std::unique_ptr<ComboBoxAttachment>> comboAttachments;
    std::unique_ptr<ButtonAttachment> enabledAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (InspectorPanel)
};

} // namespace ShredEQ::UI
