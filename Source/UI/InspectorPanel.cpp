#include "InspectorPanel.h"

#include "../ParameterIDs.h"
#include "ShredEQLookAndFeel.h"

#include <array>
#include <cmath>

namespace ShredEQ::UI
{
namespace
{
constexpr int outerMargin = 10;
constexpr int titleHeight = 24;
constexpr int targetCellWidth = 108;
constexpr int gridGap = 6;
constexpr int cellHeight = 58;

void initialiseCaption (juce::Label& label)
{
    label.setJustificationType (juce::Justification::centredLeft);
    label.setColour (juce::Label::textColourId,
                     ShredEQLookAndFeel::secondaryTextColour);
    label.setFont (juce::FontOptions { 11.0f });
    label.setInterceptsMouseClicks (false, false);
}
} // namespace

InspectorPanel::SliderField::SliderField()
{
    initialiseCaption (caption);
    addAndMakeVisible (caption);

    slider.setSliderStyle (juce::Slider::LinearHorizontal);
    slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    addAndMakeVisible (slider);

    readout.setEditable (false, true, false);
    readout.setJustificationType (juce::Justification::centredRight);
    readout.setColour (juce::Label::backgroundColourId,
                       ShredEQLookAndFeel::panelColour);
    readout.setColour (juce::Label::outlineColourId,
                       ShredEQLookAndFeel::outlineColour);
    readout.setTooltip ("Double-click to enter a value");
    readout.onTextChange = [this]
    {
        if (readout.isBeingEdited())
            return;

        slider.setValue (slider.getValueFromText (readout.getText()),
                         juce::sendNotificationSync);
        refreshReadout();
    };
    addAndMakeVisible (readout);

    slider.onValueChange = [this] { refreshReadout(); };
}

void InspectorPanel::SliderField::setCaption (const juce::String& text)
{
    caption.setText (text, juce::dontSendNotification);
    slider.setTooltip (text + ": drag, wheel, or type in the value field");
}

void InspectorPanel::SliderField::refreshReadout()
{
    readout.setText (slider.getTextFromValue (slider.getValue()),
                     juce::dontSendNotification);
}

void InspectorPanel::SliderField::resized()
{
    auto area = getLocalBounds();
    caption.setBounds (area.removeFromTop (16));
    readout.setBounds (area.removeFromRight (juce::jmin (72, area.getWidth() / 2))
                           .reduced (1, 7));
    slider.setBounds (area.reduced (3, 7));
}

InspectorPanel::ChoiceField::ChoiceField()
{
    initialiseCaption (caption);
    addAndMakeVisible (caption);
    addAndMakeVisible (combo);
}

void InspectorPanel::ChoiceField::setCaption (const juce::String& text)
{
    caption.setText (text, juce::dontSendNotification);
    combo.setTooltip (text);
}

void InspectorPanel::ChoiceField::resized()
{
    auto area = getLocalBounds();
    caption.setBounds (area.removeFromTop (16));
    combo.setBounds (area.reduced (1, 7));
}

InspectorPanel::InspectorPanel (juce::AudioProcessorValueTreeState& state)
    : apvts (state)
{
    setOpaque (true);

    bandTitle.setFont (juce::FontOptions { 14.0f });
    bandTitle.setJustificationType (juce::Justification::centredLeft);
    bandTitle.setColour (juce::Label::textColourId,
                         ShredEQLookAndFeel::selectionColour);
    addAndMakeVisible (bandTitle);

    emptyState.setText ("Select a band to edit its parameters",
                        juce::dontSendNotification);
    emptyState.setJustificationType (juce::Justification::centred);
    emptyState.setColour (juce::Label::textColourId,
                          ShredEQLookAndFeel::secondaryTextColour);
    addAndMakeVisible (emptyState);

    frequency.setCaption ("Frequency");
    gain.setCaption ("Gain");
    q.setCaption ("Q");
    filterType.setCaption ("Filter Type");
    slope.setCaption ("Slope");
    stage.setCaption ("Stage");
    routing.setCaption ("Routing");
    dynamicMode.setCaption ("Dynamic Mode");
    threshold.setCaption ("Threshold");
    dynamicRange.setCaption ("Range");
    attack.setCaption ("Attack");
    hold.setCaption ("Hold");
    release.setCaption ("Release");
    knee.setCaption ("Knee");
    detectorSource.setCaption ("Detector Source");
    detectorFrequency.setCaption ("Detector Filter");
    detectorQ.setCaption ("Detector Q");
    trackingSource.setCaption ("Tracking Source");

    for (auto* component : getGridComponents())
        addAndMakeVisible (*component);

    enabledParameterButton.setClickingTogglesState (true);
    enabledParameterButton.setVisible (false);
    addChildComponent (enabledParameterButton);

    trackingEnable.setClickingTogglesState (true);
    trackingEnable.setTooltip ("Enable tracking; source Off is the disabled state");
    trackingEnable.onClick = [this]
    {
        if (! trackingEnable.getToggleState())
            trackingSource.combo.setSelectedId (1, juce::sendNotificationSync);
        else if (trackingSource.combo.getSelectedId() <= 1)
            trackingSource.combo.setSelectedId (2, juce::sendNotificationSync);
    };

    soloButton.setClickingTogglesState (true);
    soloButton.setColour (juce::ToggleButton::textColourId,
                          ShredEQLookAndFeel::auditionColour);
    soloButton.setTooltip ("Audition only the selected band");
    soloButton.onClick = [this]
    {
        if (selectedBand >= 0 && onSoloChanged)
            onSoloChanged (selectedBand, soloButton.getToggleState());
    };

    deltaButton.setClickingTogglesState (true);
    deltaButton.setColour (juce::ToggleButton::textColourId,
                           ShredEQLookAndFeel::auditionColour);
    deltaButton.setTooltip ("Hear the signal removed by this band");
    deltaButton.onClick = [this]
    {
        if (selectedBand >= 0 && onDeltaChanged)
            onDeltaChanged (selectedBand, deltaButton.getToggleState());
    };

    bypassButton.setClickingTogglesState (true);
    bypassButton.setColour (juce::ToggleButton::textColourId,
                            ShredEQLookAndFeel::bypassColour);
    bypassButton.setTooltip ("Bypass the selected band");
    bypassButton.onClick = [this]
    {
        const auto shouldBeEnabled = ! bypassButton.getToggleState();
        if (enabledParameterButton.getToggleState() != shouldBeEnabled)
            enabledParameterButton.triggerClick();
    };

    setEditorVisible (false);
    startTimerHz (15);
}

InspectorPanel::~InspectorPanel()
{
    stopTimer();
    clearAttachments();
}

void InspectorPanel::paint (juce::Graphics& g)
{
    g.fillAll (ShredEQLookAndFeel::raisedPanelColour);
    g.setColour (ShredEQLookAndFeel::outlineColour);
    g.drawHorizontalLine (0, 0.0f, static_cast<float> (getWidth()));
}

void InspectorPanel::resized()
{
    auto area = getLocalBounds().reduced (outerMargin, 5);
    bandTitle.setBounds (area.removeFromTop (titleHeight));
    emptyState.setBounds (area);

    const auto components = getGridComponents();
    const auto availableWidth = juce::jmax (1, area.getWidth());
    const auto columns = juce::jlimit (
        4, 8, (availableWidth + gridGap) / (targetCellWidth + gridGap));
    const auto rows = static_cast<int> (
        std::ceil (static_cast<double> (components.size()) / columns));
    const auto availableHeight = juce::jmax (1, area.getHeight());
    const auto rowHeight = juce::jmin (
        cellHeight,
        juce::jmax (42, (availableHeight - gridGap * (rows - 1)) / rows));
    const auto columnWidth =
        (availableWidth - gridGap * (columns - 1)) / columns;

    for (std::size_t index = 0; index < components.size(); ++index)
    {
        const auto column = static_cast<int> (index) % columns;
        const auto row = static_cast<int> (index) / columns;
        components[index]->setBounds (
            area.getX() + column * (columnWidth + gridGap),
            area.getY() + row * (rowHeight + gridGap),
            columnWidth, rowHeight);
    }
}

void InspectorPanel::setSelectedBand (int bandIndex)
{
    namespace IDs = ShredEQ::ParameterIDs;

    const auto isValid = bandIndex >= 0
                      && bandIndex < static_cast<int> (IDs::bands.size());
    if (! isValid)
    {
        clearAttachments();
        selectedBand = -1;
        bandTitle.setText ({}, juce::dontSendNotification);
        setEditorVisible (false);
        return;
    }

    if (selectedBand == bandIndex && enabledAttachment != nullptr)
        return;

    clearAttachments();
    selectedBand = bandIndex;
    const auto& band = IDs::bands[static_cast<std::size_t> (bandIndex)];

    attachSlider (frequency, band.frequency);
    attachSlider (gain, band.gain);
    attachSlider (q, band.q);
    attachChoice (filterType, band.type);
    attachChoice (slope, band.slope);
    attachChoice (stage, band.stage);
    attachChoice (routing, band.route);

    attachChoice (dynamicMode, band.dynamicMode);
    attachSlider (threshold, band.threshold);
    attachSlider (dynamicRange, band.dynamicRange);
    attachSlider (attack, band.attack);
    attachSlider (hold, band.hold);
    attachSlider (release, band.release);
    attachSlider (knee, band.knee);

    attachChoice (detectorSource, band.detectorSource);
    attachSlider (detectorFrequency, band.detectorFrequency);
    attachSlider (detectorQ, band.detectorQ);
    attachChoice (trackingSource, band.trackingSource);

    if (apvts.getParameter (band.enabled) != nullptr)
        enabledAttachment =
            std::make_unique<ButtonAttachment> (apvts, band.enabled,
                                                enabledParameterButton);

    bandTitle.setText ("BAND " + juce::String (bandIndex + 1),
                       juce::dontSendNotification);
    soloButton.setToggleState (false, juce::dontSendNotification);
    deltaButton.setToggleState (false, juce::dontSendNotification);
    setEditorVisible (true);
    timerCallback();
}

void InspectorPanel::setSoloState (bool shouldSolo)
{
    soloButton.setToggleState (shouldSolo, juce::dontSendNotification);
}

void InspectorPanel::setDeltaState (bool shouldHearRemoved)
{
    deltaButton.setToggleState (shouldHearRemoved,
                                juce::dontSendNotification);
}

int InspectorPanel::getPreferredHeightForWidth (int width) const noexcept
{
    const auto availableWidth = juce::jmax (1, width - outerMargin * 2);
    const auto columns = juce::jlimit (
        4, 8, (availableWidth + gridGap) / (targetCellWidth + gridGap));
    constexpr int numberOfControls = 22;
    const auto rows = (numberOfControls + columns - 1) / columns;
    return 10 + titleHeight + rows * cellHeight + (rows - 1) * gridGap;
}

void InspectorPanel::timerCallback()
{
    if (selectedBand < 0)
        return;

    bypassButton.setToggleState (
        ! enabledParameterButton.getToggleState(),
        juce::dontSendNotification);
    trackingEnable.setToggleState (
        trackingSource.combo.getSelectedId() > 1,
        juce::dontSendNotification);
}

void InspectorPanel::clearAttachments()
{
    enabledAttachment.reset();
    comboAttachments.clear();
    sliderAttachments.clear();
}

void InspectorPanel::attachSlider (SliderField& field,
                                    const char* parameterID)
{
    const auto exists = apvts.getParameter (parameterID) != nullptr;
    field.setEnabled (exists);
    if (! exists)
        return;

    sliderAttachments.push_back (
        std::make_unique<SliderAttachment> (apvts, parameterID, field.slider));
    field.refreshReadout();
}

void InspectorPanel::attachChoice (ChoiceField& field,
                                    const char* parameterID)
{
    auto* parameter = apvts.getParameter (parameterID);
    field.combo.clear (juce::dontSendNotification);
    field.setEnabled (parameter != nullptr);
    if (parameter == nullptr)
        return;

    const auto values = parameter->getAllValueStrings();
    for (int index = 0; index < values.size(); ++index)
        field.combo.addItem (values[index], index + 1);

    comboAttachments.push_back (
        std::make_unique<ComboBoxAttachment> (apvts, parameterID, field.combo));
}

void InspectorPanel::setEditorVisible (bool shouldBeVisible)
{
    bandTitle.setVisible (shouldBeVisible);
    emptyState.setVisible (! shouldBeVisible);
    for (auto* component : getGridComponents())
        component->setVisible (shouldBeVisible);

    repaint();
}

std::vector<juce::Component*> InspectorPanel::getGridComponents()
{
    return {
        &frequency, &gain, &q, &filterType, &slope, &stage, &routing,
        &dynamicMode, &threshold, &dynamicRange, &attack, &hold, &release,
        &knee, &detectorSource, &detectorFrequency, &detectorQ,
        &trackingEnable, &trackingSource, &soloButton, &deltaButton,
        &bypassButton
    };
}

} // namespace ShredEQ::UI
