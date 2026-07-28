#include "EQWorkspace.h"

#include "ShredEQLookAndFeel.h"

#include <algorithm>
#include <cmath>

namespace ShredEQ::UI
{
namespace
{
constexpr std::array<float, 10> labelledFrequencies {
    20.0f, 50.0f, 100.0f, 200.0f, 500.0f,
    1000.0f, 2000.0f, 5000.0f, 10000.0f, 20000.0f
};

juce::String frequencyLabel (float frequency)
{
    return frequency >= 1000.0f
             ? juce::String (frequency / 1000.0f, frequency < 10000.0f ? 1 : 0) + "k"
             : juce::String (juce::roundToInt (frequency));
}

bool hasFineModifier (const juce::ModifierKeys& modifiers) noexcept
{
    return modifiers.isShiftDown() || modifiers.isAltDown()
        || modifiers.isCtrlDown() || modifiers.isCommandDown();
}
} // namespace

EQWorkspace::EQWorkspace()
{
    layers.fill (true);
    setOpaque (true);
    setWantsKeyboardFocus (true);

    for (auto& node : nodes)
    {
        addAndMakeVisible (node);
        node.setVisible (false);
    }
}

void EQWorkspace::paint (juce::Graphics& g)
{
    g.fillAll (ShredEQLookAndFeel::backgroundColour);

    if (isLayerVisible (Layer::grid))
        paintGrid (g);
    if (isLayerVisible (Layer::preSpectrum))
        paintSpectrum (g, preSpectrum,
                       ShredEQLookAndFeel::secondaryTextColour.withAlpha (0.28f));
    if (isLayerVisible (Layer::postSpectrum))
        paintSpectrum (g, postSpectrum,
                       ShredEQLookAndFeel::analysisAccentColour.withAlpha (0.32f));
    if (isLayerVisible (Layer::percentileCloud))
        paintPercentileCloud (g);
    if (isLayerVisible (Layer::dynamicRangeMap))
        paintDynamicRangeMap (g);
    if (isLayerVisible (Layer::resonanceCandidates))
        paintResonances (g);
    if (isLayerVisible (Layer::noteGuides))
        paintNotes (g);
    if (isLayerVisible (Layer::sidechainCollisions))
        paintCollisions (g);
    if (isLayerVisible (Layer::compositeResponse))
        paintResponse (g, compositeResponse, ShredEQLookAndFeel::textColour, 2.2f);
    if (isLayerVisible (Layer::stageAResponse))
        paintResponse (g, stageAResponse, ShredEQLookAndFeel::stageAColour, 1.35f);
    if (isLayerVisible (Layer::adaptiveResponse))
        paintResponse (g, adaptiveResponse, ShredEQLookAndFeel::adaptiveColour, 1.35f);
    if (isLayerVisible (Layer::stageBResponse))
        paintResponse (g, stageBResponse, ShredEQLookAndFeel::stageBColour, 1.35f);
    if (isLayerVisible (Layer::phaseResponse))
        paintPhase (g);

    g.setColour (ShredEQLookAndFeel::outlineColour);
    g.drawRect (getPlotBounds(), 1.0f);
}

void EQWorkspace::resized()
{
    updateNodePositions();
}

void EQWorkspace::setFrequencyRange (float minimumHz, float maximumHz)
{
    minimumHz = juce::jmax (1.0f, minimumHz);
    maximumHz = juce::jmax (minimumHz * 1.01f, maximumHz);
    minimumFrequencyHz = minimumHz;
    maximumFrequencyHz = maximumHz;
    updateNodePositions();
    repaint();
}

float EQWorkspace::getMinimumFrequency() const noexcept { return minimumFrequencyHz; }
float EQWorkspace::getMaximumFrequency() const noexcept { return maximumFrequencyHz; }

void EQWorkspace::setGainRangeDb (float absoluteRangeDb)
{
    gainRangeDb = juce::jlimit (1.0f, 96.0f, std::abs (absoluteRangeDb));
    updateNodePositions();
    repaint();
}

float EQWorkspace::getGainRangeDb() const noexcept { return gainRangeDb; }

juce::Rectangle<float> EQWorkspace::getPlotBounds() const noexcept
{
    return getLocalBounds().toFloat().withTrimmedLeft (48.0f)
                                     .withTrimmedRight (12.0f)
                                     .withTrimmedTop (12.0f)
                                     .withTrimmedBottom (25.0f);
}

float EQWorkspace::frequencyToX (float frequencyHz) const noexcept
{
    const auto plot = getPlotBounds();
    const auto safe = juce::jlimit (minimumFrequencyHz, maximumFrequencyHz,
                                    frequencyHz);
    const auto proportion = std::log (safe / minimumFrequencyHz)
                          / std::log (maximumFrequencyHz / minimumFrequencyHz);
    return plot.getX() + static_cast<float> (proportion) * plot.getWidth();
}

float EQWorkspace::xToFrequency (float x) const noexcept
{
    const auto plot = getPlotBounds();
    if (plot.getWidth() <= 0.0f)
        return minimumFrequencyHz;
    const auto proportion = juce::jlimit (0.0f, 1.0f,
                                          (x - plot.getX()) / plot.getWidth());
    return minimumFrequencyHz
         * std::pow (maximumFrequencyHz / minimumFrequencyHz, proportion);
}

float EQWorkspace::gainToY (float gainDb) const noexcept
{
    const auto plot = getPlotBounds();
    return juce::jmap (juce::jlimit (-gainRangeDb, gainRangeDb, gainDb),
                       gainRangeDb, -gainRangeDb,
                       plot.getY(), plot.getBottom());
}

float EQWorkspace::yToGain (float y) const noexcept
{
    const auto plot = getPlotBounds();
    if (plot.getHeight() <= 0.0f)
        return 0.0f;
    return juce::jlimit (-gainRangeDb, gainRangeDb,
                         juce::jmap (y, plot.getY(), plot.getBottom(),
                                     gainRangeDb, -gainRangeDb));
}

void EQWorkspace::setLayerVisible (Layer layer, bool visible)
{
    const auto index = static_cast<std::size_t> (layer);
    if (index >= layers.size())
        return;
    layers[index] = visible;
    if (layer == Layer::bandNodes)
        updateNodePositions();
    repaint();
}

bool EQWorkspace::isLayerVisible (Layer layer) const noexcept
{
    const auto index = static_cast<std::size_t> (layer);
    return index < layers.size() && layers[index];
}

void EQWorkspace::setPreSpectrumView (CurveView value)       { preSpectrum = value; repaint(); }
void EQWorkspace::setPostSpectrumView (CurveView value)      { postSpectrum = value; repaint(); }
void EQWorkspace::setCompositeResponseView (CurveView value) { compositeResponse = value; repaint(); }
void EQWorkspace::setStageAResponseView (CurveView value)    { stageAResponse = value; repaint(); }
void EQWorkspace::setAdaptiveResponseView (CurveView value)  { adaptiveResponse = value; repaint(); }
void EQWorkspace::setStageBResponseView (CurveView value)    { stageBResponse = value; repaint(); }
void EQWorkspace::setPhaseResponseView (CurveView value)     { phaseResponse = value; repaint(); }

void EQWorkspace::clearCurveViews()
{
    preSpectrum = {};
    postSpectrum = {};
    compositeResponse = {};
    stageAResponse = {};
    adaptiveResponse = {};
    stageBResponse = {};
    phaseResponse = {};
    repaint();
}

void EQWorkspace::setPercentileCloudSnapshot (
    const PercentileCloud::Snapshot* snapshot)
{
    percentileSnapshot = snapshot;
    repaint();
}

void EQWorkspace::setResonanceCandidates (
    std::span<const ResonanceCandidate> value)
{
    resonances = value;
    repaint();
}

void EQWorkspace::setNoteGuides (std::span<const NoteGuide> value)
{
    notes = value;
    repaint();
}

void EQWorkspace::setSidechainCollisions (
    std::span<const SidechainCollision> value)
{
    collisions = value;
    repaint();
}

void EQWorkspace::setBandState (int bandIndex, const BandState& band)
{
    if (! juce::isPositiveAndBelow (bandIndex, maximumBands))
        return;

    bands[static_cast<std::size_t> (bandIndex)] = band;
    auto state = band.visual;
    state.bandNumber = bandIndex + 1;
    state.staticGainDb = band.gainDb;
    nodes[static_cast<std::size_t> (bandIndex)].setVisualState (state);
    updateNodePositions();
}

void EQWorkspace::setBandCurrentGainDb (int bandIndex, float currentGainDb)
{
    if (! juce::isPositiveAndBelow (bandIndex, maximumBands))
        return;
    auto& band = bands[static_cast<std::size_t> (bandIndex)];
    band.visual.currentGainDb = currentGainDb;
    nodes[static_cast<std::size_t> (bandIndex)].setCurrentGainDb (currentGainDb);
    updateNodePositions();
}

void EQWorkspace::setBandDynamicActivity (int bandIndex, float activity)
{
    if (! juce::isPositiveAndBelow (bandIndex, maximumBands))
        return;
    auto& band = bands[static_cast<std::size_t> (bandIndex)];
    band.visual.dynamicActivity = juce::jlimit (0.0f, 1.0f, activity);
    nodes[static_cast<std::size_t> (bandIndex)].setDynamicActivity (
        band.visual.dynamicActivity);
}

const EQWorkspace::BandState* EQWorkspace::getBandState (
    int bandIndex) const noexcept
{
    return juce::isPositiveAndBelow (bandIndex, maximumBands)
             ? &bands[static_cast<std::size_t> (bandIndex)] : nullptr;
}

BandNode* EQWorkspace::getBandNode (int bandIndex) noexcept
{
    return juce::isPositiveAndBelow (bandIndex, maximumBands)
             ? &nodes[static_cast<std::size_t> (bandIndex)] : nullptr;
}

const BandNode* EQWorkspace::getBandNode (int bandIndex) const noexcept
{
    return juce::isPositiveAndBelow (bandIndex, maximumBands)
             ? &nodes[static_cast<std::size_t> (bandIndex)] : nullptr;
}

void EQWorkspace::setSelectedBandIndex (int bandIndex)
{
    if (! juce::isPositiveAndBelow (bandIndex, maximumBands)
        || ! bands[static_cast<std::size_t> (bandIndex)].visible)
        bandIndex = -1;

    if (selectedBandIndex == bandIndex)
        return;

    selectedBandIndex = bandIndex;
    for (int index = 0; index < maximumBands; ++index)
    {
        auto state = nodes[static_cast<std::size_t> (index)].getVisualState();
        state.selected = index == selectedBandIndex;
        nodes[static_cast<std::size_t> (index)].setVisualState (state);
    }

    if (onSelectedBandChanged)
        onSelectedBandChanged (selectedBandIndex);
}

int EQWorkspace::getSelectedBandIndex() const noexcept
{
    return selectedBandIndex;
}

EQWorkspace::Hit EQWorkspace::findHit (juce::Point<float> point) const noexcept
{
    for (int index = maximumBands - 1; index >= 0; --index)
    {
        if (! bands[static_cast<std::size_t> (index)].visible
            || ! isLayerVisible (Layer::bandNodes))
            continue;

        const auto& node = nodes[static_cast<std::size_t> (index)];
        Hit hit;
        hit.bandIndex = index;
        const auto selected = node.getVisualState().selected;
        hit.solo = selected && node.isSoloHit (point);
        hit.hearRemoved = selected && node.isHearRemovedHit (point);
        hit.handle = node.hitTestHandle (point);
        if (hit.solo || hit.hearRemoved || hit.handle != BandNode::Handle::none)
            return hit;
    }
    return {};
}

void EQWorkspace::mouseDown (const juce::MouseEvent& event)
{
    grabKeyboardFocus();
    dragHit = findHit (event.position);
    if (dragHit.bandIndex < 0)
        return;

    setSelectedBandIndex (dragHit.bandIndex);
    if (dragHit.solo)
    {
        if (onSoloBand)
            onSoloBand (dragHit.bandIndex);
        return;
    }
    if (dragHit.hearRemoved)
    {
        if (onHearRemoved)
            onHearRemoved (dragHit.bandIndex);
        return;
    }

    const auto& band = bands[static_cast<std::size_t> (dragHit.bandIndex)];
    dragStart = event.position;
    dragStartFrequency = band.frequencyHz;
    dragStartGain = band.gainDb;
    dragStartRange = band.visual.dynamicRangeDb;
    gestureActive = dragHit.handle != BandNode::Handle::none;

    if (gestureActive && onEditGesture)
    {
        if (dragHit.handle == BandNode::Handle::range)
            onEditGesture (dragHit.bandIndex, EditedParameter::dynamicRange, true);
        else
        {
            onEditGesture (dragHit.bandIndex, EditedParameter::frequency, true);
            onEditGesture (dragHit.bandIndex, EditedParameter::gain, true);
        }
    }
}

void EQWorkspace::mouseDrag (const juce::MouseEvent& event)
{
    if (! gestureActive || dragHit.bandIndex < 0)
        return;

    const auto fine = hasFineModifier (event.mods) ? 0.2f : 1.0f;
    const auto delta = (event.position - dragStart) * fine;

    if (dragHit.handle == BandNode::Handle::range)
    {
        const auto dbPerPixel = gainRangeDb * 2.0f
                              / juce::jmax (1.0f, getPlotBounds().getHeight());
        updateBandValue (dragHit.bandIndex, EditedParameter::dynamicRange,
                         juce::jlimit (-gainRangeDb, gainRangeDb,
                                      dragStartRange - delta.y * dbPerPixel));
        return;
    }

    const auto startX = frequencyToX (dragStartFrequency);
    updateBandValue (dragHit.bandIndex, EditedParameter::frequency,
                     xToFrequency (startX + delta.x));

    const auto dbPerPixel = gainRangeDb * 2.0f
                          / juce::jmax (1.0f, getPlotBounds().getHeight());
    updateBandValue (dragHit.bandIndex, EditedParameter::gain,
                     juce::jlimit (-gainRangeDb, gainRangeDb,
                                  dragStartGain - delta.y * dbPerPixel));
}

void EQWorkspace::mouseUp (const juce::MouseEvent&)
{
    if (gestureActive && dragHit.bandIndex >= 0 && onEditGesture)
    {
        if (dragHit.handle == BandNode::Handle::range)
            onEditGesture (dragHit.bandIndex, EditedParameter::dynamicRange, false);
        else
        {
            onEditGesture (dragHit.bandIndex, EditedParameter::frequency, false);
            onEditGesture (dragHit.bandIndex, EditedParameter::gain, false);
        }
    }
    gestureActive = false;
    dragHit = {};
}

void EQWorkspace::mouseDoubleClick (const juce::MouseEvent& event)
{
    if (! getPlotBounds().contains (event.position))
        return;

    const auto hit = findHit (event.position);
    if (hit.bandIndex >= 0)
    {
        if (onRequestNumericalEntry)
            onRequestNumericalEntry (hit.bandIndex);
        return;
    }

    const auto frequency = xToFrequency (event.position.x);
    if (event.mods.isAnyModifierKeyDown())
    {
        if (onRequestFilterTypeSelection)
            onRequestFilterTypeSelection (frequency);
    }
    else if (onCreateBellBand)
    {
        onCreateBellBand (frequency);
    }
}

void EQWorkspace::mouseWheelMove (const juce::MouseEvent& event,
                                  const juce::MouseWheelDetails& wheel)
{
    auto hit = findHit (event.position);
    auto index = hit.bandIndex >= 0 ? hit.bandIndex : selectedBandIndex;
    if (! juce::isPositiveAndBelow (index, maximumBands))
        return;

    const auto fine = hasFineModifier (event.mods) ? 0.2f : 1.0f;
    const auto& band = bands[static_cast<std::size_t> (index)];
    const auto newQ = juce::jlimit (0.05f, 40.0f,
                                    band.q * std::pow (2.0f,
                                        wheel.deltaY * fine));
    if (onEditGesture)
        onEditGesture (index, EditedParameter::q, true);
    updateBandValue (index, EditedParameter::q, newQ);
    if (onEditGesture)
        onEditGesture (index, EditedParameter::q, false);
}

void EQWorkspace::mouseMagnify (const juce::MouseEvent& event,
                                float scaleFactor)
{
    const auto hit = findHit (event.position);
    const auto index = hit.bandIndex >= 0 ? hit.bandIndex : selectedBandIndex;
    if (! juce::isPositiveAndBelow (index, maximumBands)
        || ! std::isfinite (scaleFactor) || scaleFactor <= 0.0f)
        return;

    const auto fine = hasFineModifier (event.mods) ? 0.2f : 1.0f;
    const auto& band = bands[static_cast<std::size_t> (index)];
    const auto newQ = juce::jlimit (
        0.05f, 40.0f, band.q * std::pow (scaleFactor, fine));
    if (onEditGesture)
        onEditGesture (index, EditedParameter::q, true);
    updateBandValue (index, EditedParameter::q, newQ);
    if (onEditGesture)
        onEditGesture (index, EditedParameter::q, false);
}

bool EQWorkspace::keyPressed (const juce::KeyPress& key)
{
    if ((key.getKeyCode() == juce::KeyPress::deleteKey
         || key.getKeyCode() == juce::KeyPress::backspaceKey)
        && selectedBandIndex >= 0)
    {
        if (onDeleteBand)
            onDeleteBand (selectedBandIndex);
        return true;
    }
    return false;
}

void EQWorkspace::updateBandValue (int bandIndex, EditedParameter parameter,
                                   float value)
{
    if (! juce::isPositiveAndBelow (bandIndex, maximumBands))
        return;

    auto& band = bands[static_cast<std::size_t> (bandIndex)];
    switch (parameter)
    {
        case EditedParameter::frequency:   band.frequencyHz = value; break;
        case EditedParameter::gain:
            band.gainDb = value;
            band.visual.staticGainDb = value;
            break;
        case EditedParameter::q:           band.q = value; break;
        case EditedParameter::dynamicRange:
            band.visual.dynamicRangeDb = value;
            break;
    }

    auto state = band.visual;
    state.bandNumber = bandIndex + 1;
    nodes[static_cast<std::size_t> (bandIndex)].setVisualState (state);
    updateNodePositions();
    if (onBandEdited)
        onBandEdited (bandIndex, parameter, value);
}

void EQWorkspace::updateNodePositions()
{
    for (int index = 0; index < maximumBands; ++index)
    {
        auto& node = nodes[static_cast<std::size_t> (index)];
        const auto& band = bands[static_cast<std::size_t> (index)];
        node.setBounds (getLocalBounds());
        node.setVisible (band.visible && isLayerVisible (Layer::bandNodes));
        if (! band.visible)
            continue;

        const auto x = frequencyToX (band.frequencyHz);
        const auto centre = juce::Point<float> (x, gainToY (band.gainDb));
        const auto range = juce::Point<float> (
            x, gainToY (band.gainDb + band.visual.dynamicRangeDb));
        const auto live = juce::Point<float> (
            x, gainToY (band.visual.currentGainDb));
        node.setHandlePositions (centre, range, live);
    }
}

void EQWorkspace::paintGrid (juce::Graphics& g) const
{
    const auto plot = getPlotBounds();
    g.setFont (juce::FontOptions (10.0f));

    for (auto frequency : labelledFrequencies)
    {
        if (frequency < minimumFrequencyHz || frequency > maximumFrequencyHz)
            continue;
        const auto x = frequencyToX (frequency);
        const auto isOneKhz = std::abs (frequency - 1000.0f) < 0.1f;
        g.setColour (ShredEQLookAndFeel::gridColour.withAlpha (
            isOneKhz ? 0.82f : 0.55f));
        g.drawVerticalLine (juce::roundToInt (x), plot.getY(), plot.getBottom());
        g.setColour (ShredEQLookAndFeel::secondaryTextColour);
        g.drawText (frequencyLabel (frequency),
                    juce::Rectangle<float> (x - 18.0f, plot.getBottom() + 3.0f,
                                            36.0f, 15.0f),
                    juce::Justification::centred, false);
    }

    constexpr std::array<float, 9> gainDivisions {
        -24.0f, -18.0f, -12.0f, -6.0f, 0.0f, 6.0f, 12.0f, 18.0f, 24.0f
    };
    for (auto gain : gainDivisions)
    {
        if (std::abs (gain) > gainRangeDb)
            continue;
        const auto y = gainToY (gain);
        const auto isZero = std::abs (gain) < 0.01f;
        g.setColour ((isZero ? ShredEQLookAndFeel::outlineColour
                             : ShredEQLookAndFeel::gridColour)
                         .withAlpha (isZero ? 0.95f : 0.55f));
        g.drawHorizontalLine (juce::roundToInt (y), plot.getX(), plot.getRight());
        g.setColour (ShredEQLookAndFeel::secondaryTextColour);
        g.drawText ((gain > 0.0f ? "+" : "") + juce::String (juce::roundToInt (gain)),
                    juce::Rectangle<float> (4.0f, y - 7.0f, 38.0f, 14.0f),
                    juce::Justification::centredRight, false);
    }
}

float EQWorkspace::spectrumDbToY (float valueDb) const noexcept
{
    const auto plot = getPlotBounds();
    return juce::jmap (juce::jlimit (-120.0f, 0.0f, valueDb),
                       -120.0f, 0.0f, plot.getBottom(), plot.getY());
}

juce::Path EQWorkspace::makePath (
    CurveView view, std::function<float (float)> yMapper) const
{
    juce::Path path;
    if (! view.isValid())
        return path;

    const auto count = view.values.size();
    for (std::size_t index = 0; index < count; ++index)
    {
        const auto frequency = view.frequencyHz.empty()
             ? minimumFrequencyHz * std::pow (
                   maximumFrequencyHz / minimumFrequencyHz,
                   count > 1 ? static_cast<float> (index) / (count - 1) : 0.0f)
             : static_cast<float> (view.frequencyHz[index]);
        const auto point = juce::Point<float> (
            frequencyToX (frequency), yMapper (view.values[index]));
        if (index == 0)
            path.startNewSubPath (point);
        else
            path.lineTo (point);
    }
    return path;
}

void EQWorkspace::paintSpectrum (juce::Graphics& g, CurveView view,
                                 juce::Colour colour) const
{
    if (! view.isValid())
        return;
    g.saveState();
    g.reduceClipRegion (getPlotBounds().toNearestInt());
    g.setColour (colour);
    g.strokePath (makePath (view, [this] (float value)
                            { return spectrumDbToY (value); }),
                  { 1.0f, juce::PathStrokeType::curved });
    g.restoreState();
}

void EQWorkspace::paintResponse (juce::Graphics& g, CurveView view,
                                 juce::Colour colour, float thickness) const
{
    if (! view.isValid())
        return;
    g.saveState();
    g.reduceClipRegion (getPlotBounds().toNearestInt());
    g.setColour (colour);
    g.strokePath (makePath (view, [this] (float value)
                            { return gainToY (value); }),
                  { thickness, juce::PathStrokeType::curved,
                    juce::PathStrokeType::rounded });
    g.restoreState();
}

void EQWorkspace::paintPercentileCloud (juce::Graphics& g) const
{
    if (percentileSnapshot == nullptr || ! percentileSnapshot->valid
        || percentileSnapshot->numBins <= 1)
        return;

    const auto low = percentileSnapshot->q10();
    const auto high = percentileSnapshot->q90();
    const auto count = std::min (low.size(), high.size());
    if (count <= 1)
        return;

    juce::Path cloud;
    for (std::size_t index = 0; index < count; ++index)
    {
        const auto frequency = static_cast<float> (index)
                             * static_cast<float> (percentileSnapshot->sampleRate)
                             / (2.0f * static_cast<float> (count - 1));
        const auto point = juce::Point<float> (frequencyToX (frequency),
                                               spectrumDbToY (high[index]));
        if (index == 0) cloud.startNewSubPath (point); else cloud.lineTo (point);
    }
    for (std::size_t index = count; index-- > 0;)
    {
        const auto frequency = static_cast<float> (index)
                             * static_cast<float> (percentileSnapshot->sampleRate)
                             / (2.0f * static_cast<float> (count - 1));
        cloud.lineTo (frequencyToX (frequency), spectrumDbToY (low[index]));
    }
    cloud.closeSubPath();

    g.saveState();
    g.reduceClipRegion (getPlotBounds().toNearestInt());
    g.setColour (ShredEQLookAndFeel::analysisAccentColour.withAlpha (0.10f));
    g.fillPath (cloud);
    g.restoreState();
}

void EQWorkspace::paintDynamicRangeMap (juce::Graphics& g) const
{
    if (percentileSnapshot == nullptr || ! percentileSnapshot->valid)
        return;
    const auto values = percentileSnapshot->dynamicRange();
    if (values.size() <= 1)
        return;

    g.saveState();
    g.reduceClipRegion (getPlotBounds().toNearestInt());
    for (std::size_t index = 0; index < values.size(); ++index)
    {
        const auto frequency = static_cast<float> (index)
                             * static_cast<float> (percentileSnapshot->sampleRate)
                             / (2.0f * static_cast<float> (values.size() - 1));
        const auto intensity = juce::jlimit (0.0f, 1.0f, values[index] / 36.0f);
        g.setColour (ShredEQLookAndFeel::adaptiveColour.withAlpha (
            0.015f + 0.09f * intensity));
        const auto x = frequencyToX (frequency);
        g.drawVerticalLine (juce::roundToInt (x), getPlotBounds().getY(),
                            getPlotBounds().getBottom());
    }
    g.restoreState();
}

void EQWorkspace::paintResonances (juce::Graphics& g) const
{
    const auto plot = getPlotBounds();
    for (const auto& candidate : resonances)
    {
        const auto x = frequencyToX (candidate.frequencyHz);
        const auto confidence = juce::jlimit (0.0f, 1.0f, candidate.confidence);
        g.setColour (ShredEQLookAndFeel::dangerColour.withAlpha (
            0.15f + confidence * 0.55f));
        g.drawVerticalLine (juce::roundToInt (x), plot.getY(), plot.getBottom());
        g.fillEllipse (x - 3.0f,
                       gainToY (juce::jlimit (-gainRangeDb, gainRangeDb,
                                             candidate.excessDb)) - 3.0f,
                       6.0f, 6.0f);
    }
}

void EQWorkspace::paintNotes (juce::Graphics& g) const
{
    const auto plot = getPlotBounds();
    g.setFont (juce::FontOptions (9.0f));
    for (const auto& note : notes)
    {
        const auto x = frequencyToX (note.frequencyHz);
        const auto alpha = 0.14f + 0.42f
                         * juce::jlimit (0.0f, 1.0f, note.emphasis);
        g.setColour (ShredEQLookAndFeel::analysisAccentColour.withAlpha (alpha));
        g.drawDashedLine ({ x, plot.getY(), x, plot.getBottom() },
                          std::array<float, 2> { 3.0f, 4.0f }.data(), 2, 1.0f);
        g.drawText (note.label, juce::Rectangle<float> (
                        x - 18.0f, plot.getY() + 2.0f, 36.0f, 12.0f),
                    juce::Justification::centred, false);
    }
}

void EQWorkspace::paintCollisions (juce::Graphics& g) const
{
    const auto plot = getPlotBounds();
    for (const auto& collision : collisions)
    {
        const auto x1 = frequencyToX (collision.lowFrequencyHz);
        const auto x2 = frequencyToX (collision.highFrequencyHz);
        const auto intensity = juce::jlimit (0.0f, 1.0f, collision.intensity);
        g.setColour (ShredEQLookAndFeel::dangerColour.withAlpha (
            0.025f + intensity * 0.13f));
        g.fillRect (juce::Rectangle<float>::leftTopRightBottom (
            juce::jmin (x1, x2), plot.getY(), juce::jmax (x1, x2),
            plot.getBottom()));
    }
}

void EQWorkspace::paintPhase (juce::Graphics& g) const
{
    if (! phaseResponse.isValid())
        return;
    const auto plot = getPlotBounds();
    g.saveState();
    g.reduceClipRegion (plot.toNearestInt());
    g.setColour (ShredEQLookAndFeel::analysisAccentColour.withAlpha (0.65f));
    g.strokePath (makePath (phaseResponse, [plot] (float degrees)
                            {
                                return juce::jmap (
                                    juce::jlimit (-180.0f, 180.0f, degrees),
                                    180.0f, -180.0f,
                                    plot.getY(), plot.getBottom());
                            }),
                  { 1.0f, juce::PathStrokeType::curved });
    g.restoreState();
}

} // namespace ShredEQ::UI
