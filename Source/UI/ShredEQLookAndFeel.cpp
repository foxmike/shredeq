#include "ShredEQLookAndFeel.h"

#include <cmath>

const juce::Colour ShredEQLookAndFeel::backgroundColour       { 0xff0a0e13 };
const juce::Colour ShredEQLookAndFeel::panelColour            { 0xff11171e };
const juce::Colour ShredEQLookAndFeel::raisedPanelColour      { 0xff18212a };
const juce::Colour ShredEQLookAndFeel::gridColour             { 0xff2a3540 };
const juce::Colour ShredEQLookAndFeel::outlineColour          { 0xff34414d };
const juce::Colour ShredEQLookAndFeel::textColour             { 0xffe7edf2 };
const juce::Colour ShredEQLookAndFeel::secondaryTextColour    { 0xff8996a3 };
const juce::Colour ShredEQLookAndFeel::analysisAccentColour   { 0xff63c7c5 };

const juce::Colour ShredEQLookAndFeel::stageAColour           { 0xff56b4e9 };
const juce::Colour ShredEQLookAndFeel::adaptiveColour         { 0xffe69f00 };
const juce::Colour ShredEQLookAndFeel::stageBColour           { 0xffcc79a7 };

const juce::Colour ShredEQLookAndFeel::selectionColour        { 0xff79d8d0 };
const juce::Colour ShredEQLookAndFeel::bypassColour           { 0xff68737e };
const juce::Colour ShredEQLookAndFeel::dangerColour           { 0xffe76f51 };
const juce::Colour ShredEQLookAndFeel::auditionColour         { 0xfff0c94d };
const juce::Colour ShredEQLookAndFeel::routingMidColour       { 0xff0072b2 };
const juce::Colour ShredEQLookAndFeel::routingSideColour      { 0xffd55e00 };

namespace
{
constexpr float cornerRadius = 3.0f;

juce::Colour disabled (juce::Colour colour, bool isEnabled)
{
    return colour.withMultipliedAlpha (isEnabled ? 1.0f : 0.38f);
}
}

ShredEQLookAndFeel::ShredEQLookAndFeel()
{
    setColourScheme ({
        backgroundColour,
        panelColour,
        panelColour,
        outlineColour,
        textColour,
        analysisAccentColour,
        textColour,
        raisedPanelColour,
        textColour
    });

    setColour (juce::Slider::backgroundColourId, gridColour);
    setColour (juce::Slider::trackColourId, analysisAccentColour);
    setColour (juce::Slider::thumbColourId, textColour);
    setColour (juce::Slider::rotarySliderOutlineColourId, gridColour);
    setColour (juce::Slider::rotarySliderFillColourId, analysisAccentColour);
    setColour (juce::Slider::textBoxTextColourId, textColour);
    setColour (juce::Slider::textBoxBackgroundColourId, panelColour);
    setColour (juce::Slider::textBoxOutlineColourId, outlineColour);

    setColour (juce::ComboBox::backgroundColourId, panelColour);
    setColour (juce::ComboBox::outlineColourId, outlineColour);
    setColour (juce::ComboBox::textColourId, textColour);
    setColour (juce::ComboBox::arrowColourId, secondaryTextColour);
    setColour (juce::ComboBox::buttonColourId, raisedPanelColour);

    setColour (juce::TextButton::buttonColourId, panelColour);
    setColour (juce::TextButton::buttonOnColourId, analysisAccentColour);
    setColour (juce::TextButton::textColourOffId, textColour);
    setColour (juce::TextButton::textColourOnId, backgroundColour);
    setColour (juce::ToggleButton::textColourId, textColour);

    setColour (juce::Label::backgroundColourId, juce::Colours::transparentBlack);
    setColour (juce::Label::textColourId, textColour);
    setColour (juce::Label::outlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Label::textWhenEditingColourId, textColour);

    setColour (juce::PopupMenu::backgroundColourId, panelColour);
    setColour (juce::PopupMenu::textColourId, textColour);
    setColour (juce::PopupMenu::headerTextColourId, secondaryTextColour);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, raisedPanelColour);
    setColour (juce::PopupMenu::highlightedTextColourId, textColour);

    setColour (juce::ScrollBar::backgroundColourId, backgroundColour);
    setColour (juce::ScrollBar::trackColourId, panelColour);
    setColour (juce::ScrollBar::thumbColourId, outlineColour);

    setColour (juce::TooltipWindow::backgroundColourId, raisedPanelColour);
    setColour (juce::TooltipWindow::textColourId, textColour);
    setColour (juce::TooltipWindow::outlineColourId, outlineColour);
}

ShredEQLookAndFeel::~ShredEQLookAndFeel() = default;

void ShredEQLookAndFeel::drawRotarySlider (juce::Graphics& g,
                                           int x, int y, int width, int height,
                                           float sliderPosProportional,
                                           float rotaryStartAngle,
                                           float rotaryEndAngle,
                                           juce::Slider& slider)
{
    auto bounds = juce::Rectangle<float> (static_cast<float> (x), static_cast<float> (y),
                                          static_cast<float> (width), static_cast<float> (height))
                      .reduced (4.0f);
    const auto radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;

    if (radius <= 3.0f)
        return;

    const auto centre = bounds.getCentre();
    const auto arcRadius = radius - 3.0f;
    const auto angle = juce::jmap (sliderPosProportional, rotaryStartAngle, rotaryEndAngle);
    const auto enabled = slider.isEnabled();

    juce::Path arc;
    arc.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f,
                       rotaryStartAngle, rotaryEndAngle, true);
    g.setColour (disabled (slider.findColour (juce::Slider::rotarySliderOutlineColourId), enabled));
    g.strokePath (arc, { 3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded });

    juce::Path valueArc;
    valueArc.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f,
                            rotaryStartAngle, angle, true);
    g.setColour (disabled (slider.findColour (juce::Slider::rotarySliderFillColourId), enabled));
    g.strokePath (valueArc, { 3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded });

    const auto bodyRadius = juce::jmax (2.0f, radius - 8.0f);
    g.setColour (disabled (panelColour, enabled));
    g.fillEllipse (centre.x - bodyRadius, centre.y - bodyRadius,
                   bodyRadius * 2.0f, bodyRadius * 2.0f);
    g.setColour (disabled (outlineColour, enabled));
    g.drawEllipse (centre.x - bodyRadius, centre.y - bodyRadius,
                   bodyRadius * 2.0f, bodyRadius * 2.0f, 1.0f);

    const auto pointerLength = bodyRadius * 0.66f;
    const auto pointerStart = centre + juce::Point<float> (std::sin (angle), -std::cos (angle))
                                           * (bodyRadius * 0.22f);
    const auto pointerEnd = centre + juce::Point<float> (std::sin (angle), -std::cos (angle))
                                         * pointerLength;
    g.setColour (disabled (textColour, enabled));
    g.drawLine ({ pointerStart, pointerEnd }, 1.5f);
}

void ShredEQLookAndFeel::drawLinearSlider (juce::Graphics& g,
                                           int x, int y, int width, int height,
                                           float sliderPos, float minSliderPos,
                                           float maxSliderPos,
                                           juce::Slider::SliderStyle style,
                                           juce::Slider& slider)
{
    if (slider.isBar())
    {
        auto bounds = juce::Rectangle<float> (static_cast<float> (x), static_cast<float> (y),
                                              static_cast<float> (width), static_cast<float> (height));
        g.setColour (disabled (slider.findColour (juce::Slider::backgroundColourId),
                               slider.isEnabled()));
        g.fillRoundedRectangle (bounds, 2.0f);

        g.setColour (disabled (slider.findColour (juce::Slider::trackColourId),
                               slider.isEnabled()));
        if (slider.isHorizontal())
            g.fillRoundedRectangle (bounds.withWidth (juce::jmax (0.0f, sliderPos - bounds.getX())), 2.0f);
        else
            g.fillRoundedRectangle (bounds.withTop (sliderPos), 2.0f);
        return;
    }

    const auto horizontal = slider.isHorizontal();
    const auto trackWidth = 3.0f;
    const juce::Point<float> start { horizontal ? static_cast<float> (x)
                                                : static_cast<float> (x) + width * 0.5f,
                                     horizontal ? static_cast<float> (y) + height * 0.5f
                                                : static_cast<float> (y + height) };
    const juce::Point<float> end { horizontal ? static_cast<float> (x + width) : start.x,
                                   horizontal ? start.y : static_cast<float> (y) };

    g.setColour (disabled (slider.findColour (juce::Slider::backgroundColourId),
                           slider.isEnabled()));
    g.drawLine ({ start, end }, trackWidth);

    const auto isTwoValue = style == juce::Slider::TwoValueHorizontal
                         || style == juce::Slider::TwoValueVertical;
    const auto isThreeValue = style == juce::Slider::ThreeValueHorizontal
                           || style == juce::Slider::ThreeValueVertical;
    const auto pointAt = [horizontal, x, y, width, height] (float position)
    {
        return juce::Point<float> { horizontal ? position : static_cast<float> (x) + width * 0.5f,
                                    horizontal ? static_cast<float> (y) + height * 0.5f : position };
    };

    const auto valueStart = (isTwoValue || isThreeValue) ? pointAt (minSliderPos) : start;
    const auto valueEnd = pointAt (isTwoValue || isThreeValue ? maxSliderPos : sliderPos);
    g.setColour (disabled (slider.findColour (juce::Slider::trackColourId),
                           slider.isEnabled()));
    g.drawLine ({ valueStart, valueEnd }, trackWidth);

    const auto drawThumb = [&] (float position, bool primary)
    {
        const auto centre = pointAt (position);
        const auto radius = primary ? 5.5f : 4.5f;
        g.setColour (disabled (primary ? slider.findColour (juce::Slider::thumbColourId)
                                      : analysisAccentColour,
                               slider.isEnabled()));
        g.fillEllipse (centre.x - radius, centre.y - radius, radius * 2.0f, radius * 2.0f);
        g.setColour (disabled (backgroundColour, slider.isEnabled()));
        g.drawEllipse (centre.x - radius, centre.y - radius, radius * 2.0f, radius * 2.0f, 1.0f);
    };

    if (isTwoValue || isThreeValue)
    {
        drawThumb (minSliderPos, false);
        drawThumb (maxSliderPos, false);
    }

    if (! isTwoValue)
        drawThumb (sliderPos, true);
}

void ShredEQLookAndFeel::drawComboBox (juce::Graphics& g,
                                       int width, int height, bool isButtonDown,
                                       int buttonX, int buttonY, int buttonW, int buttonH,
                                       juce::ComboBox& box)
{
    const auto bounds = juce::Rectangle<float> (0.5f, 0.5f,
                                                static_cast<float> (width - 1),
                                                static_cast<float> (height - 1));
    g.setColour (disabled (box.findColour (juce::ComboBox::backgroundColourId), box.isEnabled()));
    g.fillRoundedRectangle (bounds, cornerRadius);
    g.setColour (disabled (isButtonDown ? selectionColour : box.findColour (juce::ComboBox::outlineColourId),
                           box.isEnabled()));
    g.drawRoundedRectangle (bounds, cornerRadius, 1.0f);

    auto arrowArea = juce::Rectangle<int> (buttonX, buttonY, buttonW, buttonH)
                         .toFloat()
                         .reduced (juce::jmax (3.0f, buttonW * 0.28f),
                                   juce::jmax (3.0f, buttonH * 0.35f));
    juce::Path arrow;
    arrow.startNewSubPath (arrowArea.getTopLeft());
    arrow.lineTo (arrowArea.getCentreX(), arrowArea.getBottom());
    arrow.lineTo (arrowArea.getTopRight());
    g.setColour (disabled (box.findColour (juce::ComboBox::arrowColourId), box.isEnabled()));
    g.strokePath (arrow, { 1.5f, juce::PathStrokeType::curved,
                          juce::PathStrokeType::rounded });
}

void ShredEQLookAndFeel::drawButtonBackground (juce::Graphics& g,
                                               juce::Button& button,
                                               const juce::Colour& background,
                                               bool isHighlighted, bool isDown)
{
    auto fill = background;

    if (button.getToggleState())
        fill = analysisAccentColour.withMultipliedBrightness (0.72f);
    else if (isDown)
        fill = selectionColour.withAlpha (0.32f);
    else if (isHighlighted)
        fill = raisedPanelColour.brighter (0.08f);

    fill = disabled (fill, button.isEnabled());
    const auto bounds = button.getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (fill);
    g.fillRoundedRectangle (bounds, cornerRadius);
    g.setColour (disabled (button.hasKeyboardFocus (true) ? selectionColour : outlineColour,
                           button.isEnabled()));
    g.drawRoundedRectangle (bounds, cornerRadius, button.hasKeyboardFocus (true) ? 1.5f : 1.0f);
}

void ShredEQLookAndFeel::drawToggleButton (juce::Graphics& g,
                                           juce::ToggleButton& button,
                                           bool isHighlighted, bool isDown)
{
    const auto bounds = button.getLocalBounds().toFloat();
    const auto boxSize = juce::jmin (16.0f, bounds.getHeight() - 4.0f);
    const auto box = juce::Rectangle<float> (bounds.getX() + 2.0f,
                                             bounds.getCentreY() - boxSize * 0.5f,
                                             boxSize, boxSize);
    const auto enabled = button.isEnabled();

    g.setColour (disabled ((isHighlighted || isDown) ? raisedPanelColour : panelColour, enabled));
    g.fillRoundedRectangle (box, 2.0f);
    g.setColour (disabled (button.hasKeyboardFocus (true) ? selectionColour : outlineColour, enabled));
    g.drawRoundedRectangle (box, 2.0f, 1.0f);

    if (button.getToggleState())
    {
        auto tick = getTickShape (boxSize * 0.7f);
        g.setColour (disabled (analysisAccentColour, enabled));
        g.fillPath (tick, tick.getTransformToScaleToFit (box.reduced (3.0f), true));
    }

    g.setColour (disabled (button.findColour (juce::ToggleButton::textColourId), enabled));
    g.setFont (juce::FontOptions { juce::jmin (15.0f, bounds.getHeight() * 0.48f) });
    g.drawFittedText (button.getButtonText(),
                      button.getLocalBounds().withTrimmedLeft (juce::roundToInt (box.getRight() + 7.0f)),
                      juce::Justification::centredLeft, 1);
}

void ShredEQLookAndFeel::drawLabel (juce::Graphics& g, juce::Label& label)
{
    if (label.isBeingEdited())
        return;

    const auto background = label.findColour (juce::Label::backgroundColourId);
    if (! background.isTransparent())
    {
        g.setColour (background);
        g.fillRoundedRectangle (label.getLocalBounds().toFloat(), 2.0f);
    }

    g.setColour (disabled (label.findColour (juce::Label::textColourId), label.isEnabled()));
    g.setFont (getLabelFont (label));
    const auto border = getLabelBorderSize (label);
    g.drawFittedText (label.getText(), border.subtractedFrom (label.getLocalBounds()),
                      label.getJustificationType(),
                      juce::jmax (1, static_cast<int> (label.getHeight() / getLabelFont (label).getHeight())),
                      label.getMinimumHorizontalScale());

    if (label.isEnabled() && label.isEditable() && label.isMouseOver (true))
    {
        g.setColour (outlineColour);
        g.drawRoundedRectangle (label.getLocalBounds().toFloat().reduced (0.5f), 2.0f, 1.0f);
    }
}

void ShredEQLookAndFeel::drawScrollbar (juce::Graphics& g,
                                        juce::ScrollBar& scrollbar,
                                        int x, int y, int width, int height,
                                        bool isVertical, int thumbStart, int thumbSize,
                                        bool isMouseOver, bool isMouseDown)
{
    const auto fullBounds = juce::Rectangle<float> (static_cast<float> (x), static_cast<float> (y),
                                                    static_cast<float> (width), static_cast<float> (height));
    g.setColour (scrollbar.findColour (juce::ScrollBar::trackColourId));
    g.fillRoundedRectangle (fullBounds.reduced (2.0f), 2.0f);

    auto thumb = isVertical
                   ? juce::Rectangle<float> (static_cast<float> (x + 2), static_cast<float> (thumbStart),
                                             static_cast<float> (width - 4), static_cast<float> (thumbSize))
                   : juce::Rectangle<float> (static_cast<float> (thumbStart), static_cast<float> (y + 2),
                                             static_cast<float> (thumbSize), static_cast<float> (height - 4));
    auto thumbColour = scrollbar.findColour (juce::ScrollBar::thumbColourId);
    if (isMouseDown)
        thumbColour = selectionColour;
    else if (isMouseOver)
        thumbColour = thumbColour.brighter (0.15f);

    g.setColour (thumbColour);
    g.fillRoundedRectangle (thumb, juce::jmin (3.0f, thumb.getWidth() * 0.5f,
                                              thumb.getHeight() * 0.5f));
}

void ShredEQLookAndFeel::drawPopupMenuBackground (juce::Graphics& g, int width, int height)
{
    const auto bounds = juce::Rectangle<float> (0.0f, 0.0f,
                                                static_cast<float> (width),
                                                static_cast<float> (height));
    g.setColour (findColour (juce::PopupMenu::backgroundColourId));
    g.fillRect (bounds);
    g.setColour (outlineColour);
    g.drawRect (bounds.reduced (0.5f), 1.0f);
}

void ShredEQLookAndFeel::drawPopupMenuItem (juce::Graphics& g,
                                            const juce::Rectangle<int>& area,
                                            bool isSeparator, bool isActive,
                                            bool isHighlighted, bool isTicked,
                                            bool hasSubMenu,
                                            const juce::String& text,
                                            const juce::String& shortcutKeyText,
                                            const juce::Drawable* iconToDraw,
                                            const juce::Colour* textColourToUse)
{
    if (isSeparator)
    {
        g.setColour (gridColour);
        g.fillRect (area.reduced (8, 0).withHeight (1).withCentre (area.getCentre()));
        return;
    }

    auto itemBounds = area.reduced (2, 1);
    if (isHighlighted && isActive)
    {
        g.setColour (findColour (juce::PopupMenu::highlightedBackgroundColourId));
        g.fillRoundedRectangle (itemBounds.toFloat(), 2.0f);
        g.setColour (analysisAccentColour);
        g.fillRect (itemBounds.removeFromLeft (2));
    }

    auto colour = textColourToUse != nullptr
                    ? *textColourToUse
                    : findColour (isHighlighted ? juce::PopupMenu::highlightedTextColourId
                                                : juce::PopupMenu::textColourId);
    g.setColour (colour.withMultipliedAlpha (isActive ? 1.0f : 0.38f));

    auto content = area.reduced (8, 0);
    const auto iconWidth = juce::jmin (20, content.getHeight() - 4);
    auto iconArea = content.removeFromLeft (iconWidth).toFloat().reduced (2.0f);

    if (iconToDraw != nullptr)
    {
        iconToDraw->drawWithin (g, iconArea, juce::RectanglePlacement::centred
                                             | juce::RectanglePlacement::onlyReduceInSize,
                                isActive ? 1.0f : 0.38f);
    }
    else if (isTicked)
    {
        auto tick = getTickShape (static_cast<float> (iconWidth));
        g.fillPath (tick, tick.getTransformToScaleToFit (iconArea, true));
    }

    content.removeFromLeft (6);
    if (hasSubMenu)
    {
        auto arrowArea = content.removeFromRight (12).toFloat().reduced (2.0f, 5.0f);
        juce::Path arrow;
        arrow.startNewSubPath (arrowArea.getTopLeft());
        arrow.lineTo (arrowArea.getRight(), arrowArea.getCentreY());
        arrow.lineTo (arrowArea.getBottomLeft());
        g.strokePath (arrow, { 1.3f, juce::PathStrokeType::curved,
                              juce::PathStrokeType::rounded });
    }

    auto shortcutArea = content.removeFromRight (
        shortcutKeyText.isEmpty() ? 0 : juce::jmin (110, content.getWidth() / 2));
    const auto font = getPopupMenuFont().withHeight (
        juce::jmin (getPopupMenuFont().getHeight(), area.getHeight() * 0.58f));
    g.setFont (font);
    g.drawFittedText (text, content, juce::Justification::centredLeft, 1);

    if (shortcutKeyText.isNotEmpty())
    {
        g.setColour (secondaryTextColour.withMultipliedAlpha (isActive ? 1.0f : 0.38f));
        g.setFont (font.withHeight (font.getHeight() * 0.88f));
        g.drawFittedText (shortcutKeyText, shortcutArea,
                          juce::Justification::centredRight, 1);
    }
}

void ShredEQLookAndFeel::drawTooltip (juce::Graphics& g,
                                      const juce::String& text,
                                      int width, int height)
{
    const auto bounds = juce::Rectangle<float> (0.5f, 0.5f,
                                                static_cast<float> (width - 1),
                                                static_cast<float> (height - 1));
    g.setColour (findColour (juce::TooltipWindow::backgroundColourId));
    g.fillRoundedRectangle (bounds, cornerRadius);
    g.setColour (findColour (juce::TooltipWindow::outlineColourId));
    g.drawRoundedRectangle (bounds, cornerRadius, 1.0f);
    g.setColour (findColour (juce::TooltipWindow::textColourId));
    g.setFont (juce::FontOptions { 13.0f });
    g.drawFittedText (text, juce::Rectangle<int> (width, height).reduced (8, 4),
                      juce::Justification::centredLeft, 3);
}

juce::Typeface::Ptr ShredEQLookAndFeel::getTypefaceForFont (const juce::Font& font)
{
    // No suite font asset is required. This preserves JUCE's platform fallback
    // and also honours a default typeface installed later by the host.
    return juce::LookAndFeel_V4::getTypefaceForFont (font);
}
