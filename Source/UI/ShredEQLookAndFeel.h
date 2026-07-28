#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

/**
    Shared visual language for ShredEQ's measurement and correction interface.

    Response colours are intentionally reserved by processing stage so that
    workspace views can remain consistent without duplicating palette values.
*/
class ShredEQLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    ShredEQLookAndFeel();
    ~ShredEQLookAndFeel() override;

    // Core surfaces and analysis colours.
    static const juce::Colour backgroundColour;
    static const juce::Colour panelColour;
    static const juce::Colour raisedPanelColour;
    static const juce::Colour gridColour;
    static const juce::Colour outlineColour;
    static const juce::Colour textColour;
    static const juce::Colour secondaryTextColour;
    static const juce::Colour analysisAccentColour;

    // High-contrast response curves. Do not reuse these for decoration.
    static const juce::Colour stageAColour;
    static const juce::Colour adaptiveColour;
    static const juce::Colour stageBColour;

    // Interaction and state colours.
    static const juce::Colour selectionColour;
    static const juce::Colour bypassColour;
    static const juce::Colour dangerColour;
    static const juce::Colour auditionColour;

    // Colour-blind-safe routing pair.
    static const juce::Colour routingMidColour;
    static const juce::Colour routingSideColour;

    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height,
                           float sliderPosProportional, float rotaryStartAngle,
                           float rotaryEndAngle, juce::Slider&) override;

    void drawLinearSlider (juce::Graphics&, int x, int y, int width, int height,
                           float sliderPos, float minSliderPos, float maxSliderPos,
                           juce::Slider::SliderStyle, juce::Slider&) override;

    void drawComboBox (juce::Graphics&, int width, int height, bool isButtonDown,
                       int buttonX, int buttonY, int buttonW, int buttonH,
                       juce::ComboBox&) override;

    void drawButtonBackground (juce::Graphics&, juce::Button&,
                               const juce::Colour& background,
                               bool isHighlighted, bool isDown) override;

    void drawToggleButton (juce::Graphics&, juce::ToggleButton&,
                           bool isHighlighted, bool isDown) override;

    void drawLabel (juce::Graphics&, juce::Label&) override;

    void drawScrollbar (juce::Graphics&, juce::ScrollBar&, int x, int y,
                        int width, int height, bool isVertical,
                        int thumbStart, int thumbSize,
                        bool isMouseOver, bool isMouseDown) override;

    void drawPopupMenuBackground (juce::Graphics&, int width, int height) override;

    void drawPopupMenuItem (juce::Graphics&, const juce::Rectangle<int>& area,
                            bool isSeparator, bool isActive, bool isHighlighted,
                            bool isTicked, bool hasSubMenu,
                            const juce::String& text,
                            const juce::String& shortcutKeyText,
                            const juce::Drawable* iconToDraw,
                            const juce::Colour* textColourToUse) override;

    void drawTooltip (juce::Graphics&, const juce::String& text,
                      int width, int height) override;

    juce::Typeface::Ptr getTypefaceForFont (const juce::Font&) override;

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ShredEQLookAndFeel)
};
