#pragma once

#ifndef JucePlugin_Name
#define JucePlugin_Name "ShredEQ"
#endif

#include "PluginProcessor.h"
#include "DSP/ResponseComputer.h"
#include "UI/EQWorkspace.h"
#include "UI/InspectorPanel.h"
#include "UI/IntelligenceDrawer.h"
#include "UI/ShredEQLookAndFeel.h"

#include <array>
#include <memory>
#include <vector>

class ShredEQAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                          private juce::Timer
{
public:
    explicit ShredEQAudioProcessorEditor (ShredEQAudioProcessor&);
    ~ShredEQAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;

private:
    enum class ViewMode
    {
        sculpt,
        diagnose,
        dynamics
    };

    using ComboAttachment =
        juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using ButtonAttachment =
        juce::AudioProcessorValueTreeState::ButtonAttachment;

    void timerCallback() override;
    void configureHeader();
    void configureWorkspace();
    void setViewMode (ViewMode);
    void refreshBandStates();
    void refreshResponseCurves();
    void setBandParameter (int bandIndex,
                           ShredEQ::UI::EQWorkspace::EditedParameter,
                           float value,
                           bool notifyHost);
    void setParameterValue (const char* parameterID, float plainValue,
                            bool notifyHost);
    void setParameterGesture (int bandIndex,
                              ShredEQ::UI::EQWorkspace::EditedParameter,
                              bool isStarting);
    void createBellBand (float frequencyHz);
    void deleteSelectedBand();
    void showPresetMenu();
    void showPluginMenu();
    [[nodiscard]] const char* parameterFor (
        int bandIndex,
        ShredEQ::UI::EQWorkspace::EditedParameter) const noexcept;

    ShredEQAudioProcessor& audioProcessor;
    ShredEQLookAndFeel lookAndFeel;

    juce::Label productLabel;
    juce::TextButton presetButton { "Preset" };
    juce::ComboBox profileSelector;
    juce::ComboBox tuningSelector;
    juce::ComboBox abSelector;
    juce::TextButton copyAToBButton { "A>B" };
    juce::TextButton undoButton { "Undo" };
    juce::TextButton redoButton { "Redo" };
    juce::ToggleButton bypassButton { "Bypass" };
    juce::ToggleButton gainMatchButton { "G-Match" };
    juce::ToggleButton analyzerButton { "Analyzer" };
    juce::Label latencyLabel;
    juce::Label cpuLabel;
    juce::TextButton helpButton { "?" };
    juce::TextButton menuButton { "Menu" };

    juce::TextButton sculptButton { "Sculpt" };
    juce::TextButton diagnoseButton { "Diagnose" };
    juce::TextButton dynamicsButton { "Dynamics" };

    ShredEQ::UI::EQWorkspace workspace;
    ShredEQ::UI::InspectorPanel inspector;
    ShredEQ::UI::IntelligenceDrawer intelligenceDrawer;

    std::unique_ptr<ComboAttachment> profileAttachment;
    std::unique_ptr<ComboAttachment> tuningAttachment;
    std::unique_ptr<ComboAttachment> abAttachment;
    std::unique_ptr<ButtonAttachment> bypassAttachment;
    std::unique_ptr<ButtonAttachment> analyzerAttachment;

    ShredEQ::DSP::ResponseComputer responseComputer;
    std::vector<ShredEQ::DSP::BandSnapshot> responseBands;
    std::array<float, 512> compositeResponse {};
    std::array<float, 512> stageAResponse {};
    std::array<float, 512> stageBResponse {};
    std::array<float, 512> phaseResponse {};
    double responseSampleRate = 0.0;
    ViewMode viewMode = ViewMode::sculpt;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ShredEQAudioProcessorEditor)
};
