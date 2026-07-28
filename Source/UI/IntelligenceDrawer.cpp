#include "IntelligenceDrawer.h"

#include "../ParameterIDs.h"
#include "ShredEQLookAndFeel.h"

#include <array>
#include <utility>
#include <vector>

namespace ShredEQ::UI
{
namespace
{
namespace IDs = ShredEQ::ParameterIDs;

using APVTS = juce::AudioProcessorValueTreeState;
using SliderAttachment = APVTS::SliderAttachment;
using ComboAttachment = APVTS::ComboBoxAttachment;
using ButtonAttachment = APVTS::ButtonAttachment;

constexpr int outerMargin = 7;
constexpr int headerHeight = 30;
constexpr int statusHeight = 25;
constexpr int rowHeight = 34;
constexpr int actionHeight = 28;
constexpr int sectionGap = 7;

void resetParameter (APVTS& state, const char* parameterID)
{
    if (auto* parameter = state.getParameter (parameterID))
    {
        parameter->beginChangeGesture();
        parameter->setValueNotifyingHost (parameter->getDefaultValue());
        parameter->endChangeGesture();
    }
}

void setDiscreteParameterIndex (APVTS& state, const char* parameterID, int index)
{
    if (auto* parameter = state.getParameter (parameterID))
    {
        parameter->beginChangeGesture();
        parameter->setValueNotifyingHost (
            parameter->convertTo0to1 (static_cast<float> (index)));
        parameter->endChangeGesture();
    }
}

class ControlRow final : public juce::Component
{
public:
    enum class Type { slider, choice };

    ControlRow (APVTS& state, const char* parameterID,
                juce::String captionText, Type type)
        : caption (std::move (captionText)), controlType (type)
    {
        captionLabel.setText (caption, juce::dontSendNotification);
        captionLabel.setColour (juce::Label::textColourId,
                                ShredEQLookAndFeel::secondaryTextColour);
        addAndMakeVisible (captionLabel);

        if (type == Type::slider)
        {
            slider.setSliderStyle (juce::Slider::LinearHorizontal);
            slider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 74, 22);
            addAndMakeVisible (slider);
            if (state.getParameter (parameterID) != nullptr)
                sliderAttachment =
                    std::make_unique<SliderAttachment> (state, parameterID, slider);
            else
                slider.setEnabled (false);
        }
        else
        {
            if (auto* parameter = state.getParameter (parameterID))
            {
                const auto values = parameter->getAllValueStrings();
                for (int index = 0; index < values.size(); ++index)
                    combo.addItem (values[index], index + 1);
                comboAttachment =
                    std::make_unique<ComboAttachment> (state, parameterID, combo);
            }
            else
            {
                combo.setEnabled (false);
            }
            addAndMakeVisible (combo);
        }
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (4, 3);
        captionLabel.setBounds (area.removeFromLeft (94));
        if (controlType == Type::slider)
            slider.setBounds (area);
        else
            combo.setBounds (area.reduced (0, 2));
    }

private:
    juce::String caption;
    Type controlType;
    juce::Label captionLabel;
    juce::Slider slider;
    juce::ComboBox combo;
    std::unique_ptr<SliderAttachment> sliderAttachment;
    std::unique_ptr<ComboAttachment> comboAttachment;
};

class ModuleSection final : public juce::Component
{
public:
    ModuleSection (APVTS& stateToUse, juce::String moduleName,
                   const char* enableParameter)
        : state (stateToUse), name (std::move (moduleName))
    {
        title.setText (name, juce::dontSendNotification);
        title.setFont (juce::FontOptions { 14.0f, juce::Font::bold });
        addAndMakeVisible (title);

        enable.setButtonText ("Enable");
        addAndMakeVisible (enable);
        if (enableParameter != nullptr
            && state.getParameter (enableParameter) != nullptr)
            enableAttachment = std::make_unique<ButtonAttachment> (
                state, enableParameter, enable);

        disclosure.setButtonText ("More");
        disclosure.setClickingTogglesState (true);
        disclosure.onClick = [this]
        {
            updateSecondaryVisibility();
            if (onHeightChanged)
                onHeightChanged();
        };
        addAndMakeVisible (disclosure);

        reset.setButtonText ("Reset");
        reset.onClick = [this]
        {
            for (auto* id : resetIDs)
                resetParameter (state, id);
            if (onReset)
                onReset();
        };
        addAndMakeVisible (reset);

        status.setText ("Ready", juce::dontSendNotification);
        status.setColour (juce::Label::backgroundColourId,
                          ShredEQLookAndFeel::backgroundColour.withAlpha (0.65f));
        status.setColour (juce::Label::textColourId,
                          ShredEQLookAndFeel::analysisAccentColour);
        status.setJustificationType (juce::Justification::centredLeft);
        addAndMakeVisible (status);
    }

    ControlRow& addSlider (const char* id, const juce::String& caption,
                           bool secondary = false)
    {
        return addControl (id, caption, ControlRow::Type::slider, secondary);
    }

    ControlRow& addChoice (const char* id, const juce::String& caption,
                           bool secondary = false)
    {
        return addControl (id, caption, ControlRow::Type::choice, secondary);
    }

    juce::TextButton& addAction (const juce::String& text,
                                 std::function<void()> action,
                                 bool toggles = false)
    {
        auto button = std::make_unique<juce::TextButton> (text);
        button->setClickingTogglesState (toggles);
        button->onClick = std::move (action);
        auto& result = *button;
        addAndMakeVisible (result);
        actions.push_back (std::move (button));
        return result;
    }

    void addResetID (const char* id) { resetIDs.push_back (id); }

    void useSourceAsEnable (const char* sourceParameter)
    {
        enableAttachment.reset();
        enable.onClick = [this, sourceParameter]
        {
            setDiscreteParameterIndex (state, sourceParameter,
                                       enable.getToggleState() ? 1 : 0);
        };
        sourceEnableID = sourceParameter;
    }

    void syncSourceEnable()
    {
        if (sourceEnableID == nullptr)
            return;
        if (auto* raw = state.getRawParameterValue (sourceEnableID))
            enable.setToggleState (raw->load() >= 0.5f,
                                   juce::dontSendNotification);
    }

    void setStatus (const juce::String& text)
    {
        status.setText (text.isNotEmpty() ? text : "Ready",
                        juce::dontSendNotification);
    }

    [[nodiscard]] int preferredHeight() const noexcept
    {
        const auto visibleSecondary =
            disclosure.getToggleState() ? static_cast<int> (secondaryRows.size()) : 0;
        return headerHeight + statusHeight + 4
             + (static_cast<int> (primaryRows.size()) + visibleSecondary) * rowHeight
             + (actions.empty() ? 0 : actionHeight + 5) + 8;
    }

    void paint (juce::Graphics& g) override
    {
        const auto bounds = getLocalBounds().toFloat().reduced (0.5f);
        g.setColour (ShredEQLookAndFeel::panelColour);
        g.fillRoundedRectangle (bounds, 4.0f);
        g.setColour (ShredEQLookAndFeel::outlineColour);
        g.drawRoundedRectangle (bounds, 4.0f, 1.0f);
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (outerMargin);
        auto header = area.removeFromTop (headerHeight);
        enable.setBounds (header.removeFromRight (68).reduced (2, 3));
        reset.setBounds (header.removeFromRight (50).reduced (2, 3));
        disclosure.setBounds (header.removeFromRight (50).reduced (2, 3));
        title.setBounds (header);

        status.setBounds (area.removeFromTop (statusHeight).reduced (2, 1));
        area.removeFromTop (4);

        const auto layoutRows = [&area] (auto& rows)
        {
            for (auto& row : rows)
                row->setBounds (area.removeFromTop (rowHeight));
        };
        layoutRows (primaryRows);
        if (disclosure.getToggleState())
            layoutRows (secondaryRows);

        if (! actions.empty())
        {
            area.removeFromTop (5);
            auto actionArea = area.removeFromTop (actionHeight);
            const auto width = actionArea.getWidth()
                             / static_cast<int> (actions.size());
            for (auto& action : actions)
                action->setBounds (actionArea.removeFromLeft (width).reduced (2));
        }
    }

    std::function<void()> onHeightChanged;
    std::function<void()> onReset;

private:
    ControlRow& addControl (const char* id, const juce::String& caption,
                            ControlRow::Type type, bool secondary)
    {
        auto row = std::make_unique<ControlRow> (state, id, caption, type);
        auto& result = *row;
        addAndMakeVisible (result);
        (secondary ? secondaryRows : primaryRows).push_back (std::move (row));
        updateSecondaryVisibility();
        return result;
    }

    void updateSecondaryVisibility()
    {
        const auto visible = disclosure.getToggleState();
        for (auto& row : secondaryRows)
            row->setVisible (visible);
        resized();
    }

    APVTS& state;
    juce::String name;
    juce::Label title;
    juce::ToggleButton enable;
    juce::TextButton disclosure;
    juce::TextButton reset;
    juce::Label status;
    const char* sourceEnableID = nullptr;

    std::vector<std::unique_ptr<ControlRow>> primaryRows;
    std::vector<std::unique_ptr<ControlRow>> secondaryRows;
    std::vector<std::unique_ptr<juce::TextButton>> actions;
    std::vector<const char*> resetIDs;
    std::unique_ptr<ButtonAttachment> enableAttachment;
};
} // namespace

class IntelligenceDrawer::Content final : public juce::Component
{
public:
    Content (APVTS& stateToUse, Callbacks& callbacksToUse)
        : callbacks (callbacksToUse),
          state (stateToUse),
          tightCut (stateToUse, "TIGHT CUT", IDs::TightCut::enabled),
          chug (stateToUse, "CHUG STABILIZER", IDs::Chug::enabled),
          resonance (stateToUse, "RESONANCE SCAN", IDs::resonanceSlots[0].enabled),
          noteFollower (stateToUse, "NOTE FOLLOWER", nullptr),
          mixLockIn (stateToUse, "MIX LOCK-IN", IDs::Sidechain::lockInEnabled)
    {
        addAndMakeVisible (tightCut);
        addAndMakeVisible (chug);
        addAndMakeVisible (resonance);
        addAndMakeVisible (noteFollower);
        addAndMakeVisible (mixLockIn);

        configureTightCut();
        configureChug();
        configureResonance();
        configureNoteFollower();
        configureMixLockIn();

        for (auto* section : sections())
            section->onHeightChanged = [this] { relayout(); };
    }

    void refreshStatuses()
    {
        noteFollower.syncSourceEnable();
        tightCut.setStatus (statusOr (callbacks.tightCutStatus, "Parameter controlled"));
        chug.setStatus (statusOr (callbacks.chugStatus, "Monitoring chug events"));
        resonance.setStatus (statusOr (callbacks.resonanceStatus, "Scan ready"));
        noteFollower.setStatus (statusOr (callbacks.noteFollowerStatus, "Awaiting note"));
        mixLockIn.setStatus (statusOr (callbacks.mixLockInStatus, "External sidechain"));
    }

    void relayout()
    {
        const auto width = juce::jmax (1, getWidth());
        int y = 0;
        for (auto* section : sections())
        {
            const auto height = section->preferredHeight();
            section->setBounds (0, y, width, height);
            y += height + sectionGap;
        }
        setSize (width, juce::jmax (1, y - sectionGap));
    }

    void resized() override { relayout(); }

private:
    static juce::String statusOr (
        const std::function<juce::String()>& provider,
        const juce::String& fallback)
    {
        return provider ? provider() : fallback;
    }

    std::array<ModuleSection*, 5> sections()
    {
        return { &tightCut, &chug, &resonance, &noteFollower, &mixLockIn };
    }

    void configureTightCut()
    {
        tightCut.addSlider (IDs::TightCut::frequency, "Cutoff");
        tightCut.addChoice (IDs::TightCut::slope, "Slope");
        tightCut.addSlider (IDs::TightCut::punchGain, "Punch");
        tightCut.addSlider (IDs::TightCut::resonance, "Resonance", true);
        tightCut.addSlider (IDs::TightCut::punchRatio, "Punch Ratio", true);
        tightCut.addSlider (IDs::TightCut::punchQ, "Punch Q", true);
        tightCut.addChoice (IDs::TightCut::placement, "Placement", true);
        tightCut.addChoice (IDs::TightCut::routing, "Routing", true);
        const std::array<const char*, 10> ids {
            IDs::TightCut::enabled, IDs::TightCut::frequency,
            IDs::TightCut::slope, IDs::TightCut::resonance,
            IDs::TightCut::punchRatio, IDs::TightCut::punchGain,
            IDs::TightCut::punchQ, IDs::TightCut::placement,
            IDs::TightCut::routing, IDs::TightCut::listenRemoved
        };
        for (auto* id : ids)
            tightCut.addResetID (id);

        // The audition action is a modeled parameter and therefore attached.
        auto& audition = tightCut.addAction ("Delta", [] {}, true);
        tightCutAuditionAttachment = std::make_unique<ButtonAttachment> (
            stateFromSection(), IDs::TightCut::listenRemoved, audition);
    }

    void configureChug()
    {
        chug.addSlider (IDs::Chug::frequency, "Target");
        chug.addSlider (IDs::Chug::bandwidth, "Bandwidth");
        chug.addSlider (IDs::Chug::maxReduction, "Max Reduction");
        chug.addSlider (IDs::Chug::threshold, "Threshold", true);
        chug.addSlider (IDs::Chug::attack, "Attack", true);
        chug.addSlider (IDs::Chug::hold, "Hold", true);
        chug.addSlider (IDs::Chug::release, "Release", true);
        chug.addSlider (IDs::Chug::preservePick, "Preserve Pick", true);
        chug.addSlider (IDs::Chug::transientSens, "Transient", true);
        chug.addSlider (IDs::Chug::dominanceSens, "Dominance", true);

        const std::array<const char*, 11> ids {
            IDs::Chug::enabled, IDs::Chug::frequency, IDs::Chug::bandwidth,
            IDs::Chug::maxReduction, IDs::Chug::threshold, IDs::Chug::attack,
            IDs::Chug::hold, IDs::Chug::release, IDs::Chug::preservePick,
            IDs::Chug::transientSens, IDs::Chug::dominanceSens
        };
        for (auto* id : ids)
            chug.addResetID (id);
    }

    void configureResonance()
    {
        const auto& first = IDs::resonanceSlots[0];
        resonance.addSlider (first.frequency, "Candidate Hz");
        resonance.addSlider (first.maxDepth, "Max Depth");
        resonance.addSlider (first.q, "Q", true);
        resonance.addSlider (first.attack, "Attack", true);
        resonance.addSlider (first.release, "Release", true);
        resonance.addSlider (first.persistence, "Persistence", true);
        resonance.addChoice (first.mode, "Mode", true);
        resonance.addChoice (first.placement, "Placement", true);

        resonance.addAction ("Scan", [this]
        {
            if (callbacks.startResonanceScan)
                callbacks.startResonanceScan();
        });
        resonance.addAction ("Apply", [this]
        {
            if (callbacks.applyResonanceCandidate)
                callbacks.applyResonanceCandidate();
        });
        resonance.addAction ("Clear", [this]
        {
            if (callbacks.clearResonanceCandidates)
                callbacks.clearResonanceCandidates();
        });
        auto& audition = resonance.addAction ("Audition", [this] {}, true);
        audition.onClick = [this, &audition]
        {
            if (callbacks.auditionResonanceCandidate)
                callbacks.auditionResonanceCandidate (
                    audition.getToggleState());
        };

        for (const auto& slot : IDs::resonanceSlots)
        {
            const std::array<const char*, 10> ids {
                slot.enabled, slot.frequency, slot.q, slot.maxDepth,
                slot.attack, slot.release, slot.persistence, slot.mode,
                slot.placement, slot.freeze
            };
            for (auto* id : ids)
                resonance.addResetID (id);
        }
        resonance.onReset = [this]
        {
            if (callbacks.clearResonanceCandidates)
                callbacks.clearResonanceCandidates();
        };
    }

    void configureNoteFollower()
    {
        noteFollower.useSourceAsEnable (IDs::NoteFollower::trackingSource);
        noteFollower.addChoice (IDs::NoteFollower::trackingSource, "Source");
        noteFollower.addChoice (IDs::NoteFollower::harmonic, "Harmonic");
        noteFollower.addSlider (IDs::NoteFollower::centsOffset, "Cents");
        noteFollower.addSlider (IDs::NoteFollower::confidenceThreshold,
                                "Confidence", true);
        noteFollower.addSlider (IDs::NoteFollower::smoothing, "Smoothing", true);
        noteFollower.addSlider (IDs::NoteFollower::holdTime, "Hold", true);
        noteFollower.addSlider (IDs::NoteFollower::returnTime, "Return", true);
        noteFollower.addSlider (IDs::NoteFollower::minFreq, "Minimum", true);
        noteFollower.addSlider (IDs::NoteFollower::maxFreq, "Maximum", true);
        noteFollower.addChoice (IDs::NoteFollower::midiPriority,
                                "MIDI Priority", true);

        const std::array<const char*, 10> ids {
            IDs::NoteFollower::trackingSource, IDs::NoteFollower::harmonic,
            IDs::NoteFollower::centsOffset,
            IDs::NoteFollower::confidenceThreshold,
            IDs::NoteFollower::smoothing, IDs::NoteFollower::holdTime,
            IDs::NoteFollower::returnTime, IDs::NoteFollower::minFreq,
            IDs::NoteFollower::maxFreq, IDs::NoteFollower::midiPriority
        };
        for (auto* id : ids)
            noteFollower.addResetID (id);
    }

    void configureMixLockIn()
    {
        mixLockIn.addSlider (IDs::Sidechain::lockInAmount, "Masking Amount");
        mixLockIn.addChoice (IDs::Sidechain::source, "Mode / Source", true);
        auto& preview = mixLockIn.addAction ("Collision Preview", [this] {}, true);
        preview.onClick = [this, &preview]
        {
            if (callbacks.collisionPreviewChanged)
                callbacks.collisionPreviewChanged (preview.getToggleState());
        };
        const std::array<const char*, 5> ids {
            IDs::Sidechain::source, IDs::Sidechain::lockInEnabled,
            IDs::Sidechain::lockInAmount, IDs::Sidechain::carveMaxGain,
            IDs::Sidechain::carveThreshold
        };
        for (auto* id : ids)
            mixLockIn.addResetID (id);
    }

    APVTS& stateFromSection()
    {
        // Every section shares this state; retain it explicitly for attachments.
        return state;
    }

    Callbacks& callbacks;
    APVTS& state;
    ModuleSection tightCut;
    ModuleSection chug;
    ModuleSection resonance;
    ModuleSection noteFollower;
    ModuleSection mixLockIn;
    std::unique_ptr<ButtonAttachment> tightCutAuditionAttachment;
};

IntelligenceDrawer::IntelligenceDrawer (APVTS& state, Callbacks callbacksToUse)
    : apvts (state), callbacks (std::move (callbacksToUse))
{
    collapseButton.onClick = [this] { setExpanded (! expanded); };
    addAndMakeVisible (collapseButton);

    collapsedTitle.setText ("INTELLIGENCE", juce::dontSendNotification);
    collapsedTitle.setJustificationType (juce::Justification::centred);
    collapsedTitle.setColour (juce::Label::textColourId,
                              ShredEQLookAndFeel::secondaryTextColour);
    addChildComponent (collapsedTitle);

    content = std::make_unique<Content> (apvts, callbacks);
    viewport.setViewedComponent (content.get(), false);
    viewport.setScrollBarsShown (true, false);
    viewport.setScrollOnDragMode (juce::Viewport::ScrollOnDragMode::all);
    addAndMakeVisible (viewport);

    setExpanded (true);
    startTimerHz (12);
}

IntelligenceDrawer::~IntelligenceDrawer()
{
    stopTimer();
    viewport.setViewedComponent (nullptr, false);
}

void IntelligenceDrawer::setCallbacks (Callbacks newCallbacks)
{
    callbacks = std::move (newCallbacks);
    timerCallback();
}

void IntelligenceDrawer::setExpanded (bool shouldBeExpanded)
{
    expanded = shouldBeExpanded;
    collapseButton.setButtonText (expanded ? ">" : "<");
    viewport.setVisible (expanded);
    collapsedTitle.setVisible (! expanded);
    resized();
    repaint();
}

int IntelligenceDrawer::getPreferredWidth() const noexcept
{
    return expanded ? expandedWidth : collapsedWidth;
}

void IntelligenceDrawer::paint (juce::Graphics& g)
{
    g.fillAll (ShredEQLookAndFeel::backgroundColour);
    g.setColour (ShredEQLookAndFeel::outlineColour);
    g.drawRect (getLocalBounds(), 1);
}

void IntelligenceDrawer::resized()
{
    auto area = getLocalBounds().reduced (1);
    collapseButton.setBounds (area.removeFromTop (32).reduced (5, 3));

    if (! expanded)
    {
        collapsedTitle.setTransform (
            juce::AffineTransform::rotation (-juce::MathConstants<float>::halfPi,
                                             collapsedTitle.getWidth() * 0.5f,
                                             collapsedTitle.getHeight() * 0.5f));
        collapsedTitle.setBounds (0, 40, getWidth(), juce::jmax (0, getHeight() - 44));
        return;
    }

    collapsedTitle.setTransform ({});
    viewport.setBounds (area.reduced (5, 3));
    content->setSize (juce::jmax (1, viewport.getMaximumVisibleWidth() - 2),
                      content->getHeight());
    content->relayout();
}

void IntelligenceDrawer::timerCallback()
{
    if (content != nullptr)
        content->refreshStatuses();
}

} // namespace ShredEQ::UI
