#include "PluginEditor.h"

#include "ParameterIDs.h"

#include <cmath>

namespace
{
constexpr int headerHeight = 76;
constexpr int inspectorMinimumHeight = 176;
constexpr int layoutGap = 7;
constexpr int viewModeRadioGroup = 7101;

float parameterValue (juce::AudioProcessorValueTreeState& apvts,
                      const char* parameterID,
                      float fallback = 0.0f) noexcept
{
    if (const auto* value = apvts.getRawParameterValue (parameterID))
        return value->load();
    return fallback;
}

template <typename Component>
void addHeaderComponent (juce::Component& parent, Component& component,
                         const juce::String& tooltip)
{
    component.setTooltip (tooltip);
    parent.addAndMakeVisible (component);
}

void populateChoice (juce::AudioProcessorValueTreeState& apvts,
                     const char* parameterID,
                     juce::ComboBox& selector)
{
    if (auto* parameter = apvts.getParameter (parameterID))
    {
        const auto choices = parameter->getAllValueStrings();
        for (int index = 0; index < choices.size(); ++index)
            selector.addItem (choices[index], index + 1);
    }
}
} // namespace

ShredEQAudioProcessorEditor::ShredEQAudioProcessorEditor (
    ShredEQAudioProcessor& processorToUse)
    : AudioProcessorEditor (&processorToUse),
      audioProcessor (processorToUse),
      inspector (processorToUse.getAPVTS()),
      intelligenceDrawer (
          processorToUse.getAPVTS(),
          {
              [this] { audioProcessor.startResonanceScan (5.0); },
              {},
              {},
              [this] (bool shouldAudition)
              {
                  audioProcessor.getResonanceSuppressor()
                      .setAuditionCard (0, shouldAudition);
              },
              [this] (bool shouldPreview)
              {
                  if (viewMode == ViewMode::dynamics)
                      workspace.setShowSidechainCollisions (shouldPreview);
              },
              {},
              {},
              [this]
              {
                  if (! audioProcessor.isResonanceScanning())
                      return juce::String { "Ready" };
                  return "Scanning "
                       + juce::String (juce::roundToInt (
                             audioProcessor.getResonanceScanProgress() * 100.0))
                       + "%";
              },
              {},
              {}
          })
{
    setLookAndFeel (&lookAndFeel);
    setOpaque (true);
    setWantsKeyboardFocus (true);

    configureHeader();
    configureWorkspace();

    addAndMakeVisible (workspace);
    addAndMakeVisible (inspector);
    addAndMakeVisible (intelligenceDrawer);

    auto& apvts = audioProcessor.getAPVTS();
    profileAttachment = std::make_unique<ComboAttachment> (
        apvts, ShredEQ::ParameterIDs::profileIndex, profileSelector);
    tuningAttachment = std::make_unique<ComboAttachment> (
        apvts, ShredEQ::ParameterIDs::tuningIndex, tuningSelector);
    abAttachment = std::make_unique<ComboAttachment> (
        apvts, ShredEQ::ParameterIDs::abState, abSelector);
    bypassAttachment = std::make_unique<ButtonAttachment> (
        apvts, ShredEQ::ParameterIDs::globalBypass, bypassButton);
    analyzerAttachment = std::make_unique<ButtonAttachment> (
        apvts, ShredEQ::ParameterIDs::analyzerEnable, analyzerButton);

    responseBands.resize (ShredEQ::ParameterIDs::bands.size());
    setViewMode (ViewMode::sculpt);
    refreshBandStates();
    refreshResponseCurves();

    setResizable (true, true);
    setResizeLimits (980, 620, 2560, 1600);
    setSize (1360, 820);
    startTimerHz (30);
}

ShredEQAudioProcessorEditor::~ShredEQAudioProcessorEditor()
{
    stopTimer();

    // Attachments must release their component listeners before the controls
    // and the editor-scoped LookAndFeel are dismantled.
    analyzerAttachment.reset();
    bypassAttachment.reset();
    abAttachment.reset();
    tuningAttachment.reset();
    profileAttachment.reset();
    setLookAndFeel (nullptr);
}

void ShredEQAudioProcessorEditor::configureHeader()
{
    auto& apvts = audioProcessor.getAPVTS();

    productLabel.setText ("SHREDEQ", juce::dontSendNotification);
    productLabel.setFont (juce::FontOptions (18.0f, juce::Font::bold));
    productLabel.setColour (juce::Label::textColourId,
                            ShredEQLookAndFeel::analysisAccentColour);
    addAndMakeVisible (productLabel);

    populateChoice (apvts, ShredEQ::ParameterIDs::profileIndex,
                    profileSelector);
    populateChoice (apvts, ShredEQ::ParameterIDs::tuningIndex,
                    tuningSelector);
    populateChoice (apvts, ShredEQ::ParameterIDs::abState, abSelector);
    profileSelector.setTextWhenNothingSelected ("Profile");
    tuningSelector.setTextWhenNothingSelected ("Tuning");
    abSelector.setTextWhenNothingSelected ("A/B");

    addHeaderComponent (*this, presetButton, "Browse processor programs");
    addHeaderComponent (*this, profileSelector, "Signal/profile context");
    addHeaderComponent (*this, tuningSelector, "Instrument tuning");
    addHeaderComponent (*this, abSelector, "A/B comparison slot");
    addHeaderComponent (*this, copyAToBButton,
                        "Copy A to B (StateManager is not exposed yet)");
    addHeaderComponent (*this, undoButton,
                        "Undo (no processor undo manager is exposed yet)");
    addHeaderComponent (*this, redoButton,
                        "Redo (no processor undo manager is exposed yet)");
    addHeaderComponent (*this, bypassButton, "Global processing bypass");
    addHeaderComponent (*this, gainMatchButton,
                        "Gain-matched audition (StateManager is not exposed yet)");
    addHeaderComponent (*this, analyzerButton, "Enable spectrum analysis");
    addHeaderComponent (*this, latencyLabel, "Reported host latency");
    addHeaderComponent (*this, cpuLabel, "UI CPU indicator placeholder");
    addHeaderComponent (*this, helpButton, "Help");
    addHeaderComponent (*this, menuButton, "Plugin menu");

    // These controls remain explicit instead of pretending to mutate state
    // that the processor does not currently own.
    copyAToBButton.setEnabled (false);
    undoButton.setEnabled (false);
    redoButton.setEnabled (false);
    gainMatchButton.setEnabled (false);

    latencyLabel.setJustificationType (juce::Justification::centred);
    cpuLabel.setJustificationType (juce::Justification::centred);
    cpuLabel.setText ("CPU --", juce::dontSendNotification);

    presetButton.onClick = [this] { showPresetMenu(); };
    helpButton.onClick = [this]
    {
        juce::AlertWindow::showMessageBoxAsync (
            juce::MessageBoxIconType::InfoIcon,
            "ShredEQ",
            "Sculpt edits the EQ, Diagnose emphasizes analysis overlays, and "
            "Dynamics emphasizes detector and sidechain views.");
    };
    menuButton.onClick = [this] { showPluginMenu(); };

    for (auto* button : { &sculptButton, &diagnoseButton, &dynamicsButton })
    {
        button->setClickingTogglesState (true);
        button->setRadioGroupId (viewModeRadioGroup);
        addAndMakeVisible (*button);
    }
    sculptButton.onClick = [this] { setViewMode (ViewMode::sculpt); };
    diagnoseButton.onClick = [this] { setViewMode (ViewMode::diagnose); };
    dynamicsButton.onClick = [this] { setViewMode (ViewMode::dynamics); };
}

void ShredEQAudioProcessorEditor::configureWorkspace()
{
    workspace.onSelectedBandChanged = [this] (int bandIndex)
    {
        inspector.setSelectedBand (bandIndex);
    };
    workspace.onCreateBellBand = [this] (float frequencyHz)
    {
        createBellBand (frequencyHz);
    };
    workspace.onBandEdited =
        [this] (int bandIndex,
                ShredEQ::UI::EQWorkspace::EditedParameter parameter,
                float value)
        {
            setBandParameter (bandIndex, parameter, value, true);
        };
    workspace.onEditGesture =
        [this] (int bandIndex,
                ShredEQ::UI::EQWorkspace::EditedParameter parameter,
                bool isStarting)
        {
            setParameterGesture (bandIndex, parameter, isStarting);
        };
    workspace.onDeleteBand = [this] (int) { deleteSelectedBand(); };
}

void ShredEQAudioProcessorEditor::paint (juce::Graphics& graphics)
{
    graphics.fillAll (ShredEQLookAndFeel::backgroundColour);
    graphics.setColour (ShredEQLookAndFeel::panelColour);
    graphics.fillRect (getLocalBounds().removeFromTop (headerHeight));
    graphics.setColour (ShredEQLookAndFeel::outlineColour);
    graphics.drawHorizontalLine (headerHeight - 1, 0.0f,
                                 static_cast<float> (getWidth()));
}

void ShredEQAudioProcessorEditor::resized()
{
    auto bounds = getLocalBounds();
    auto header = bounds.removeFromTop (headerHeight).reduced (8, 6);
    auto firstRow = header.removeFromTop (30);
    header.removeFromTop (4);
    auto secondRow = header;

    productLabel.setBounds (firstRow.removeFromLeft (112));
    firstRow.removeFromLeft (5);
    presetButton.setBounds (firstRow.removeFromLeft (86));
    firstRow.removeFromLeft (5);
    profileSelector.setBounds (firstRow.removeFromLeft (
        juce::jlimit (126, 190, getWidth() / 8)));
    firstRow.removeFromLeft (5);
    tuningSelector.setBounds (firstRow.removeFromLeft (
        juce::jlimit (114, 170, getWidth() / 9)));
    firstRow.removeFromLeft (5);
    abSelector.setBounds (firstRow.removeFromLeft (58));
    firstRow.removeFromLeft (4);
    bypassButton.setBounds (firstRow.removeFromLeft (72));
    firstRow.removeFromLeft (4);
    analyzerButton.setBounds (firstRow.removeFromLeft (86));
    menuButton.setBounds (firstRow.removeFromRight (54));
    firstRow.removeFromRight (3);
    helpButton.setBounds (firstRow.removeFromRight (30));

    auto secondLeft = secondRow.removeFromLeft (250);
    copyAToBButton.setBounds (secondLeft.removeFromLeft (47));
    secondLeft.removeFromLeft (4);
    undoButton.setBounds (secondLeft.removeFromLeft (53));
    secondLeft.removeFromLeft (4);
    redoButton.setBounds (secondLeft.removeFromLeft (53));
    secondLeft.removeFromLeft (4);
    gainMatchButton.setBounds (secondLeft.removeFromLeft (82));

    auto secondRight = secondRow.removeFromRight (145);
    latencyLabel.setBounds (secondRight.removeFromLeft (82));
    secondRight.removeFromLeft (3);
    cpuLabel.setBounds (secondRight);

    const auto modeWidth = juce::jmin (270, secondRow.getWidth());
    auto modes = secondRow.withSizeKeepingCentre (modeWidth,
                                                  secondRow.getHeight());
    sculptButton.setBounds (modes.removeFromLeft (modeWidth / 3));
    diagnoseButton.setBounds (modes.removeFromLeft (modeWidth / 3));
    dynamicsButton.setBounds (modes);

    bounds.reduce (layoutGap, layoutGap);

    // The intelligence drawer spans the workspace and inspector height so its
    // scrollable modules retain useful room at the minimum window size.
    auto drawerArea = bounds.removeFromRight (
        intelligenceDrawer.getPreferredWidth());
    intelligenceDrawer.setBounds (drawerArea);
    bounds.removeFromRight (layoutGap);

    const auto preferredInspector =
        inspector.getPreferredHeightForWidth (bounds.getWidth());
    const auto inspectorHeight = juce::jlimit (
        inspectorMinimumHeight,
        juce::jmax (inspectorMinimumHeight, bounds.getHeight() / 2),
        preferredInspector);
    auto inspectorArea = bounds.removeFromBottom (inspectorHeight);
    bounds.removeFromBottom (layoutGap);
    workspace.setBounds (bounds);
    inspector.setBounds (inspectorArea);
}

bool ShredEQAudioProcessorEditor::keyPressed (const juce::KeyPress& key)
{
    if (key.getKeyCode() == juce::KeyPress::deleteKey
        || key.getKeyCode() == juce::KeyPress::backspaceKey)
    {
        return workspace.keyPressed (key);
    }

    // APVTS was constructed without an UndoManager. Leave host-reserved
    // command keys unconsumed until the processor exposes one.
    if (key.getModifiers().isCommandDown()
        && (key.getTextCharacter() == 'z'
            || key.getTextCharacter() == 'Z'))
        return false;

    return AudioProcessorEditor::keyPressed (key);
}

void ShredEQAudioProcessorEditor::timerCallback()
{
    refreshBandStates();
    refreshResponseCurves();

    const auto latencySamples = audioProcessor.getLatencySamples();
    const auto sampleRate = audioProcessor.getSampleRate();
    const auto latencyMs = sampleRate > 0.0
        ? 1000.0 * static_cast<double> (latencySamples) / sampleRate
        : 0.0;
    latencyLabel.setText (
        juce::String (latencySamples) + " smp / "
            + juce::String (latencyMs, 1) + " ms",
        juce::dontSendNotification);
}

void ShredEQAudioProcessorEditor::setViewMode (ViewMode mode)
{
    viewMode = mode;
    sculptButton.setToggleState (mode == ViewMode::sculpt,
                                 juce::dontSendNotification);
    diagnoseButton.setToggleState (mode == ViewMode::diagnose,
                                   juce::dontSendNotification);
    dynamicsButton.setToggleState (mode == ViewMode::dynamics,
                                   juce::dontSendNotification);

    const auto diagnose = mode == ViewMode::diagnose;
    const auto dynamics = mode == ViewMode::dynamics;
    workspace.setShowPercentileCloud (diagnose);
    workspace.setShowResonanceCandidates (diagnose);
    workspace.setShowDynamicRangeMap (diagnose || dynamics);
    workspace.setShowSidechainCollisions (dynamics);
    workspace.setShowPreSpectrum (diagnose);
    workspace.setShowPostSpectrum (diagnose || dynamics);

    // Response curves and editable nodes deliberately remain in every mode.
    workspace.setShowCompositeResponse (true);
    workspace.setShowStageAResponse (true);
    workspace.setShowAdaptiveResponse (true);
    workspace.setShowStageBResponse (true);
    workspace.setShowBandNodes (true);
}

void ShredEQAudioProcessorEditor::refreshBandStates()
{
    namespace IDs = ShredEQ::ParameterIDs;
    auto& apvts = audioProcessor.getAPVTS();

    for (int index = 0; index < ShredEQ::UI::EQWorkspace::maximumBands;
         ++index)
    {
        const auto& ids = IDs::bands[static_cast<std::size_t> (index)];
        const auto enabled = parameterValue (apvts, ids.enabled) >= 0.5f;
        const auto filterType = juce::jlimit (
            0, 10, juce::roundToInt (parameterValue (apvts, ids.type)));
        const auto stage = parameterValue (apvts, ids.stage) >= 0.5f
                         ? ShredEQ::DSP::BandStage::stageB
                         : ShredEQ::DSP::BandStage::stageA;
        const auto routing = juce::jlimit (
            0, 5, juce::roundToInt (parameterValue (apvts, ids.route)));
        const auto dynamicMode = juce::roundToInt (
            parameterValue (apvts, ids.dynamicMode));

        ShredEQ::UI::EQWorkspace::BandState band;
        band.visible = enabled;
        band.frequencyHz = parameterValue (apvts, ids.frequency, 1000.0f);
        band.gainDb = parameterValue (apvts, ids.gain);
        band.q = parameterValue (apvts, ids.q, 0.707f);
        band.visual.filterType =
            static_cast<ShredEQ::DSP::BandFilterType> (filterType);
        band.visual.stage = stage;
        band.visual.routing =
            static_cast<ShredEQ::DSP::StereoRoutingMode> (routing);
        band.visual.staticGainDb = band.gainDb;
        band.visual.dynamicRangeDb =
            parameterValue (apvts, ids.dynamicRange);
        band.visual.currentGainDb = band.gainDb;
        band.visual.dynamicsEnabled = dynamicMode != 0;
        band.visual.tracking =
            parameterValue (apvts, ids.trackingSource) >= 0.5f;
        band.visual.sidechain = dynamicMode == 7;
        band.visual.bypassed = ! enabled;
        workspace.setBandState (index, band);

        auto& snapshot = responseBands[static_cast<std::size_t> (index)];
        snapshot.enabled = enabled;
        snapshot.filterType = band.visual.filterType;
        snapshot.frequencyHz = band.frequencyHz;
        snapshot.gainDb = band.gainDb;
        snapshot.q = band.q;
        constexpr std::array<ShredEQ::DSP::FilterSlope, 6> slopes {
            ShredEQ::DSP::FilterSlope::dB6,
            ShredEQ::DSP::FilterSlope::dB12,
            ShredEQ::DSP::FilterSlope::dB18,
            ShredEQ::DSP::FilterSlope::dB24,
            ShredEQ::DSP::FilterSlope::dB36,
            ShredEQ::DSP::FilterSlope::dB48
        };
        snapshot.slope = slopes[static_cast<std::size_t> (juce::jlimit (
            0, 5, juce::roundToInt (parameterValue (apvts, ids.slope))))];
        snapshot.routingMode = band.visual.routing;
        snapshot.stage = stage;
    }

    const auto selected = workspace.getSelectedBandIndex();
    if (selected >= 0
        && parameterValue (
               apvts,
               IDs::bands[static_cast<std::size_t> (selected)].enabled)
               < 0.5f)
    {
        workspace.setSelectedBandIndex (-1);
    }
}

void ShredEQAudioProcessorEditor::refreshResponseCurves()
{
    const auto sampleRate = audioProcessor.getSampleRate() > 0.0
                          ? audioProcessor.getSampleRate() : 44100.0;
    if (std::abs (sampleRate - responseSampleRate) > 0.5)
    {
        responseSampleRate = sampleRate;
        responseComputer.setSize (
            static_cast<int> (compositeResponse.size()),
            responseSampleRate, 20.0);
    }

    responseComputer.compute (
        responseBands, responseSampleRate, compositeResponse.data(),
        phaseResponse.data(), ShredEQ::DSP::PhaseUnit::degrees);
    responseComputer.compute (
        responseBands, responseSampleRate, stageAResponse.data(), nullptr,
        ShredEQ::DSP::PhaseUnit::degrees,
        ShredEQ::DSP::StageFilter::stageA);
    responseComputer.compute (
        responseBands, responseSampleRate, stageBResponse.data(), nullptr,
        ShredEQ::DSP::PhaseUnit::degrees,
        ShredEQ::DSP::StageFilter::stageB);

    const auto& frequencies = responseComputer.getFrequencies();
    workspace.setCompositeResponseView ({ frequencies, compositeResponse });
    workspace.setStageAResponseView ({ frequencies, stageAResponse });
    workspace.setStageBResponseView ({ frequencies, stageBResponse });
    workspace.setPhaseResponseView ({ frequencies, phaseResponse });
}

const char* ShredEQAudioProcessorEditor::parameterFor (
    int bandIndex,
    ShredEQ::UI::EQWorkspace::EditedParameter parameter) const noexcept
{
    if (! juce::isPositiveAndBelow (
            bandIndex, ShredEQ::UI::EQWorkspace::maximumBands))
        return nullptr;

    const auto& ids =
        ShredEQ::ParameterIDs::bands[static_cast<std::size_t> (bandIndex)];
    switch (parameter)
    {
        case ShredEQ::UI::EQWorkspace::EditedParameter::frequency:
            return ids.frequency;
        case ShredEQ::UI::EQWorkspace::EditedParameter::gain:
            return ids.gain;
        case ShredEQ::UI::EQWorkspace::EditedParameter::q:
            return ids.q;
        case ShredEQ::UI::EQWorkspace::EditedParameter::dynamicRange:
            return ids.dynamicRange;
    }
    return nullptr;
}

void ShredEQAudioProcessorEditor::setBandParameter (
    int bandIndex,
    ShredEQ::UI::EQWorkspace::EditedParameter parameter,
    float value,
    bool notifyHost)
{
    if (const auto* id = parameterFor (bandIndex, parameter))
        setParameterValue (id, value, notifyHost);
}

void ShredEQAudioProcessorEditor::setParameterValue (
    const char* parameterID, float plainValue, bool notifyHost)
{
    if (auto* parameter = audioProcessor.getAPVTS().getParameter (parameterID))
    {
        const auto normalized = parameter->convertTo0to1 (plainValue);
        if (notifyHost)
            parameter->setValueNotifyingHost (normalized);
        else
            parameter->setValue (normalized);
    }
}

void ShredEQAudioProcessorEditor::setParameterGesture (
    int bandIndex,
    ShredEQ::UI::EQWorkspace::EditedParameter parameter,
    bool isStarting)
{
    if (const auto* id = parameterFor (bandIndex, parameter))
        if (auto* hostParameter =
                audioProcessor.getAPVTS().getParameter (id))
        {
            if (isStarting)
                hostParameter->beginChangeGesture();
            else
                hostParameter->endChangeGesture();
        }
}

void ShredEQAudioProcessorEditor::createBellBand (float frequencyHz)
{
    namespace IDs = ShredEQ::ParameterIDs;
    auto& apvts = audioProcessor.getAPVTS();
    for (int index = 0; index < ShredEQ::UI::EQWorkspace::maximumBands;
         ++index)
    {
        const auto& ids = IDs::bands[static_cast<std::size_t> (index)];
        if (parameterValue (apvts, ids.enabled) < 0.5f)
        {
            setParameterValue (ids.type, 0.0f, true);
            setParameterValue (ids.frequency, frequencyHz, true);
            setParameterValue (ids.enabled, 1.0f, true);
            refreshBandStates();
            workspace.setSelectedBandIndex (index);
            return;
        }
    }
}

void ShredEQAudioProcessorEditor::deleteSelectedBand()
{
    const auto selected = workspace.getSelectedBandIndex();
    if (! juce::isPositiveAndBelow (
            selected, ShredEQ::UI::EQWorkspace::maximumBands))
        return;

    setParameterValue (
        ShredEQ::ParameterIDs::bands[static_cast<std::size_t> (selected)].enabled,
        0.0f, true);
    workspace.setSelectedBandIndex (-1);
    refreshBandStates();
}

void ShredEQAudioProcessorEditor::showPresetMenu()
{
    juce::PopupMenu menu;
    menu.addSectionHeader ("Processor programs");
    const auto numberOfPrograms = juce::jmax (1, audioProcessor.getNumPrograms());
    for (int index = 0; index < numberOfPrograms; ++index)
    {
        auto name = audioProcessor.getProgramName (index);
        if (name.isEmpty())
            name = index == 0 ? "Current / Init" : "Program " + juce::String (index + 1);
        menu.addItem (index + 1, name,
                      true, index == audioProcessor.getCurrentProgram());
    }

    menu.showMenuAsync (
        juce::PopupMenu::Options().withTargetComponent (presetButton),
        [safeThis = juce::Component::SafePointer<
             ShredEQAudioProcessorEditor> (this)] (int result)
        {
            if (safeThis != nullptr && result > 0)
                safeThis->audioProcessor.setCurrentProgram (result - 1);
        });
}

void ShredEQAudioProcessorEditor::showPluginMenu()
{
    juce::PopupMenu menu;
    menu.addItem (1, "Reset view to Sculpt");
    menu.addItem (2, intelligenceDrawer.isExpanded()
                         ? "Collapse Intelligence" : "Expand Intelligence");
    menu.showMenuAsync (
        juce::PopupMenu::Options().withTargetComponent (menuButton),
        [safeThis = juce::Component::SafePointer<
             ShredEQAudioProcessorEditor> (this)] (int result)
        {
            if (safeThis == nullptr)
                return;
            if (result == 1)
                safeThis->setViewMode (ViewMode::sculpt);
            else if (result == 2)
            {
                safeThis->intelligenceDrawer.setExpanded (
                    ! safeThis->intelligenceDrawer.isExpanded());
                safeThis->resized();
            }
        });
}
