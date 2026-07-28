#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <functional>
#include <memory>

namespace ShredEQ::UI
{

/**
    Collapsible, scrollable editor for the guitar-specific processing modules.

    DSP workflow operations and live status are deliberately supplied as
    callbacks. This keeps the drawer usable with APVTS alone and avoids making
    the UI own, or duplicate, detector state.
*/
class IntelligenceDrawer final : public juce::Component,
                                 private juce::Timer
{
public:
    struct Callbacks
    {
        std::function<void()> startResonanceScan;
        std::function<void()> applyResonanceCandidate;
        std::function<void()> clearResonanceCandidates;
        std::function<void (bool)> auditionResonanceCandidate;
        std::function<void (bool)> collisionPreviewChanged;

        std::function<juce::String()> tightCutStatus;
        std::function<juce::String()> chugStatus;
        std::function<juce::String()> resonanceStatus;
        std::function<juce::String()> noteFollowerStatus;
        std::function<juce::String()> mixLockInStatus;
    };

    explicit IntelligenceDrawer (juce::AudioProcessorValueTreeState&,
                                 Callbacks callbacks = {});
    ~IntelligenceDrawer() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    void setExpanded (bool);
    [[nodiscard]] bool isExpanded() const noexcept { return expanded; }
    [[nodiscard]] int getPreferredWidth() const noexcept;

    void setCallbacks (Callbacks);

    static constexpr int expandedWidth = 330;
    static constexpr int collapsedWidth = 38;

private:
    class Content;

    void timerCallback() override;

    juce::AudioProcessorValueTreeState& apvts;
    Callbacks callbacks;
    bool expanded = true;

    juce::TextButton collapseButton { ">" };
    juce::Label collapsedTitle;
    juce::Viewport viewport;
    std::unique_ptr<Content> content;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (IntelligenceDrawer)
};

} // namespace ShredEQ::UI
