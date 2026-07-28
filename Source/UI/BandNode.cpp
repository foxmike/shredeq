#include "BandNode.h"

#include "ShredEQLookAndFeel.h"

#include <cmath>

namespace ShredEQ::UI
{
namespace
{
constexpr float pi = juce::MathConstants<float>::pi;

juce::Colour stageColour (DSP::BandStage stage) noexcept
{
    return stage == DSP::BandStage::stageA
             ? ShredEQLookAndFeel::stageAColour
             : ShredEQLookAndFeel::stageBColour;
}

bool pointInCircle (juce::Point<float> point, juce::Point<float> centre,
                    float radius) noexcept
{
    return point.getDistanceSquaredFrom (centre) <= radius * radius;
}
} // namespace

BandNode::BandNode()
{
    setInterceptsMouseClicks (false, false);
    setOpaque (false);
}

void BandNode::setVisualState (const VisualState& newState)
{
    state = newState;
    state.bandNumber = juce::jmax (1, state.bandNumber);
    state.dynamicActivity = juce::jlimit (0.0f, 1.0f, state.dynamicActivity);
    repaint();
}

const BandNode::VisualState& BandNode::getVisualState() const noexcept
{
    return state;
}

void BandNode::setBandNumber (int value)                    { state.bandNumber = juce::jmax (1, value); repaint(); }
void BandNode::setFilterType (DSP::BandFilterType value)    { state.filterType = value; repaint(); }
void BandNode::setStage (DSP::BandStage value)              { state.stage = value; repaint(); }
void BandNode::setRouting (DSP::StereoRoutingMode value)    { state.routing = value; repaint(); }
void BandNode::setStaticGainDb (float value)                { state.staticGainDb = value; repaint(); }
void BandNode::setDynamicRangeDb (float value)              { state.dynamicRangeDb = value; repaint(); }
void BandNode::setCurrentGainDb (float value)               { state.currentGainDb = value; repaint(); }
void BandNode::setDynamicActivity (float value)             { state.dynamicActivity = juce::jlimit (0.0f, 1.0f, value); repaint(); }
void BandNode::setDynamicsEnabled (bool value)              { state.dynamicsEnabled = value; repaint(); }
void BandNode::setTracking (bool value)                     { state.tracking = value; repaint(); }
void BandNode::setSidechain (bool value)                    { state.sidechain = value; repaint(); }
void BandNode::setBypassed (bool value)                     { state.bypassed = value; repaint(); }
void BandNode::setSelected (bool value)                     { state.selected = value; repaint(); }

void BandNode::setHandlePositions (juce::Point<float> centre,
                                   juce::Point<float> range,
                                   juce::Point<float> live)
{
    centrePosition = centre;
    rangePosition = range;
    livePosition = live;
    repaint();
}

juce::Point<float> BandNode::getCentreHandlePosition() const noexcept { return centrePosition; }
juce::Point<float> BandNode::getRangeHandlePosition() const noexcept  { return rangePosition; }
juce::Point<float> BandNode::getLivePosition() const noexcept         { return livePosition; }

BandNode::Handle BandNode::hitTestHandle (juce::Point<float> point) const noexcept
{
    if (state.dynamicsEnabled
        && pointInCircle (point, rangePosition, rangeHandleRadius + 3.0f))
        return Handle::range;

    if (pointInCircle (point, centrePosition, centreHandleRadius + 3.0f))
        return Handle::centre;

    return Handle::none;
}

bool BandNode::isCentreHandleHit (juce::Point<float> point) const noexcept
{
    return hitTestHandle (point) == Handle::centre;
}

bool BandNode::isRangeHandleHit (juce::Point<float> point) const noexcept
{
    return hitTestHandle (point) == Handle::range;
}

juce::Rectangle<float> BandNode::getSoloHitArea() const noexcept
{
    return { centrePosition.x - 28.0f, centrePosition.y + 13.0f, 18.0f, 18.0f };
}

juce::Rectangle<float> BandNode::getHearRemovedHitArea() const noexcept
{
    return { centrePosition.x + 10.0f, centrePosition.y + 13.0f, 18.0f, 18.0f };
}

bool BandNode::isSoloHit (juce::Point<float> point) const noexcept
{
    return getSoloHitArea().contains (point);
}

bool BandNode::isHearRemovedHit (juce::Point<float> point) const noexcept
{
    return getHearRemovedHitArea().contains (point);
}

void BandNode::paint (juce::Graphics& g)
{
    const auto base = stageColour (state.stage);
    const auto alpha = state.bypassed ? 0.32f : 1.0f;
    const auto nodeColour = base.withMultipliedAlpha (alpha);

    if (state.dynamicsEnabled)
    {
        g.setColour (nodeColour.withAlpha (0.38f));
        g.drawLine ({ centrePosition, rangePosition }, 2.0f);

        g.setColour (nodeColour.withAlpha (0.52f));
        g.fillEllipse (rangePosition.x - rangeHandleRadius,
                       rangePosition.y - rangeHandleRadius,
                       rangeHandleRadius * 2.0f, rangeHandleRadius * 2.0f);

        const auto liveRadius = 3.5f + state.dynamicActivity * 1.5f;
        g.setColour (ShredEQLookAndFeel::analysisAccentColour.withAlpha (
            (0.55f + 0.45f * state.dynamicActivity) * alpha));
        g.fillEllipse (livePosition.x - liveRadius, livePosition.y - liveRadius,
                       liveRadius * 2.0f, liveRadius * 2.0f);

        juce::Path activityArc;
        const auto arcRadius = centreHandleRadius + 4.0f;
        activityArc.addCentredArc (centrePosition.x, centrePosition.y,
                                   arcRadius, arcRadius, 0.0f, -pi * 0.5f,
                                   -pi * 0.5f + juce::MathConstants<float>::twoPi
                                                       * state.dynamicActivity,
                                   true);
        g.setColour (ShredEQLookAndFeel::analysisAccentColour.withAlpha (alpha));
        g.strokePath (activityArc, { 2.0f, juce::PathStrokeType::curved,
                                    juce::PathStrokeType::rounded });
    }

    if (state.selected)
    {
        g.setColour (ShredEQLookAndFeel::selectionColour.withAlpha (0.24f * alpha));
        g.fillEllipse (centrePosition.x - centreHandleRadius - 5.0f,
                       centrePosition.y - centreHandleRadius - 5.0f,
                       (centreHandleRadius + 5.0f) * 2.0f,
                       (centreHandleRadius + 5.0f) * 2.0f);
        g.setColour (ShredEQLookAndFeel::selectionColour.withAlpha (alpha));
        g.drawEllipse (centrePosition.x - centreHandleRadius - 4.0f,
                       centrePosition.y - centreHandleRadius - 4.0f,
                       (centreHandleRadius + 4.0f) * 2.0f,
                       (centreHandleRadius + 4.0f) * 2.0f, 1.5f);
    }

    g.setColour (ShredEQLookAndFeel::raisedPanelColour.withAlpha (alpha));
    g.fillEllipse (centrePosition.x - centreHandleRadius,
                   centrePosition.y - centreHandleRadius,
                   centreHandleRadius * 2.0f, centreHandleRadius * 2.0f);
    g.setColour (nodeColour);
    g.drawEllipse (centrePosition.x - centreHandleRadius,
                   centrePosition.y - centreHandleRadius,
                   centreHandleRadius * 2.0f, centreHandleRadius * 2.0f, 2.0f);

    g.setColour (ShredEQLookAndFeel::textColour.withAlpha (alpha));
    g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    g.drawText (juce::String (state.bandNumber),
                juce::Rectangle<float> (centrePosition.x - 8.0f,
                                        centrePosition.y - 7.0f, 16.0f, 14.0f),
                juce::Justification::centred, false);

    drawFilterGlyph (g, { centrePosition.x - 25.0f, centrePosition.y - 26.0f,
                          18.0f, 13.0f });

    const auto stageArea = juce::Rectangle<float> (
        centrePosition.x + 8.0f, centrePosition.y - 27.0f, 17.0f, 12.0f);
    g.setColour (nodeColour.withAlpha (0.82f));
    g.fillRoundedRectangle (stageArea, 2.0f);
    g.setColour (ShredEQLookAndFeel::backgroundColour.withAlpha (alpha));
    g.setFont (juce::FontOptions (8.0f, juce::Font::bold));
    g.drawText (state.stage == DSP::BandStage::stageA ? "A" : "B",
                stageArea, juce::Justification::centred, false);

    drawRoutingGlyph (g, { centrePosition.x - 5.0f, centrePosition.y + 17.0f,
                           10.0f, 10.0f });

    g.setFont (juce::FontOptions (8.0f));
    if (state.tracking)
    {
        g.setColour (ShredEQLookAndFeel::analysisAccentColour.withAlpha (alpha));
        g.drawText ("T", juce::Rectangle<float> (
                        centrePosition.x - 20.0f, centrePosition.y + 17.0f,
                        9.0f, 10.0f),
                    juce::Justification::centred, false);
    }
    if (state.sidechain)
    {
        g.setColour (ShredEQLookAndFeel::auditionColour.withAlpha (alpha));
        g.drawText ("SC", juce::Rectangle<float> (
                         centrePosition.x + 10.0f, centrePosition.y + 17.0f,
                         14.0f, 10.0f),
                    juce::Justification::centred, false);
    }

    if (state.selected)
    {
        const auto solo = getSoloHitArea();
        g.setColour (ShredEQLookAndFeel::auditionColour.withAlpha (alpha));
        const auto soloArcArea = solo.reduced (4.0f);
        juce::Path soloArc;
        soloArc.addArc (soloArcArea.getX(), soloArcArea.getY(),
                        soloArcArea.getWidth(), soloArcArea.getHeight(),
                        pi, juce::MathConstants<float>::twoPi, true);
        g.strokePath (soloArc, juce::PathStrokeType (1.5f));
        g.drawLine (solo.getCentreX(), solo.getCentreY(),
                    solo.getCentreX(), solo.getBottom() - 3.0f, 1.5f);

        const auto delta = getHearRemovedHitArea();
        juce::Path triangle;
        triangle.addTriangle (delta.getCentreX(), delta.getY() + 3.0f,
                              delta.getX() + 3.0f, delta.getBottom() - 3.0f,
                              delta.getRight() - 3.0f, delta.getBottom() - 3.0f);
        g.strokePath (triangle, juce::PathStrokeType (1.5f));
    }
}

void BandNode::drawFilterGlyph (juce::Graphics& g,
                                juce::Rectangle<float> area) const
{
    juce::Path path;
    const auto x0 = area.getX();
    const auto x1 = area.getCentreX();
    const auto x2 = area.getRight();
    const auto top = area.getY() + 1.0f;
    const auto mid = area.getCentreY();
    const auto bottom = area.getBottom() - 1.0f;

    switch (state.filterType)
    {
        case DSP::BandFilterType::bell:
            path.startNewSubPath (x0, bottom); path.quadraticTo (x1, top, x2, bottom); break;
        case DSP::BandFilterType::notch:
            path.startNewSubPath (x0, top); path.quadraticTo (x1, bottom, x2, top); break;
        case DSP::BandFilterType::lowShelf:
            path.startNewSubPath (x0, bottom); path.lineTo (x1, bottom);
            path.quadraticTo (x1 + 2.0f, top, x2, top); break;
        case DSP::BandFilterType::highShelf:
            path.startNewSubPath (x0, top); path.lineTo (x1, top);
            path.quadraticTo (x1 + 2.0f, bottom, x2, bottom); break;
        case DSP::BandFilterType::lowPass:
        case DSP::BandFilterType::resonantLowPass:
            path.startNewSubPath (x0, top); path.lineTo (x1, top); path.lineTo (x2, bottom); break;
        case DSP::BandFilterType::highPass:
        case DSP::BandFilterType::resonantHighPass:
            path.startNewSubPath (x0, bottom); path.lineTo (x1, top); path.lineTo (x2, top); break;
        case DSP::BandFilterType::bandPass:
            path.startNewSubPath (x0, bottom); path.lineTo (x1, top); path.lineTo (x2, bottom); break;
        case DSP::BandFilterType::tilt:
            path.startNewSubPath (x0, bottom); path.lineTo (x2, top); break;
        case DSP::BandFilterType::flatDynamic:
            path.startNewSubPath (x0, mid); path.lineTo (x2, mid); break;
    }

    g.setColour (stageColour (state.stage).withMultipliedAlpha (
        state.bypassed ? 0.32f : 0.9f));
    g.strokePath (path, { 1.4f, juce::PathStrokeType::curved,
                         juce::PathStrokeType::rounded });
}

void BandNode::drawRoutingGlyph (juce::Graphics& g,
                                 juce::Rectangle<float> area) const
{
    juce::String text;
    auto colour = ShredEQLookAndFeel::secondaryTextColour;

    switch (state.routing)
    {
        case DSP::StereoRoutingMode::linked:   text = "L"; break;
        case DSP::StereoRoutingMode::unlinked: text = "U"; break;
        case DSP::StereoRoutingMode::left:     text = "L"; colour = ShredEQLookAndFeel::routingMidColour; break;
        case DSP::StereoRoutingMode::right:    text = "R"; colour = ShredEQLookAndFeel::routingSideColour; break;
        case DSP::StereoRoutingMode::mid:      text = "M"; colour = ShredEQLookAndFeel::routingMidColour; break;
        case DSP::StereoRoutingMode::side:     text = "S"; colour = ShredEQLookAndFeel::routingSideColour; break;
    }

    g.setColour (colour.withMultipliedAlpha (state.bypassed ? 0.32f : 1.0f));
    g.setFont (juce::FontOptions (8.0f, juce::Font::bold));
    g.drawText (text, area, juce::Justification::centred, false);
}

} // namespace ShredEQ::UI
