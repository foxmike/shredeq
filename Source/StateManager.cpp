#include "StateManager.h"
#include "ParameterIDs.h"

#include <algorithm>
#include <cmath>

namespace
{
constexpr auto comparisonStateType = "ShredEQComparisonState";
constexpr auto slotAType = "SlotA";
constexpr auto slotBType = "SlotB";
constexpr auto schemaProperty = "schema_version";
constexpr auto activeSlotProperty = "active_slot";
constexpr double rollingWindowSeconds = 0.4;
constexpr float maximumMatchDb = 24.0f;
constexpr float silenceFloorDb = -80.0f;
constexpr double halfPi = juce::MathConstants<double>::halfPi;

juce::ValueTree findParameterNode (juce::ValueTree state, const juce::String& parameterID)
{
    if (state.hasProperty (parameterID))
        return state;

    if (state.getProperty ("id").toString() == parameterID)
        return state;

    for (int i = 0; i < state.getNumChildren(); ++i)
        if (auto found = findParameterNode (state.getChild (i), parameterID); found.isValid())
            return found;

    return {};
}

void setParametersToDefaults (
    juce::ValueTree state,
    const juce::AudioProcessorValueTreeState& apvts)
{
    if (state.hasProperty ("id"))
    {
        const auto parameterID = state.getProperty ("id").toString();
        if (auto* parameter = dynamic_cast<juce::RangedAudioParameter*> (
                apvts.getParameter (parameterID)))
        {
            state.setProperty ("value",
                               parameter->convertFrom0to1 (parameter->getDefaultValue()),
                               nullptr);
        }
    }

    for (int i = 0; i < state.getNumChildren(); ++i)
        setParametersToDefaults (state.getChild (i), apvts);
}
} // namespace

const juce::ValueTree& StateManager::getSlotState (Slot slot) const noexcept
{
    return slot == Slot::A ? slotA : slotB;
}

juce::ValueTree& StateManager::getSlotState (Slot slot) noexcept
{
    return slot == Slot::A ? slotA : slotB;
}

void StateManager::setSnapshotSlotParameter (juce::ValueTree& state, Slot slot)
{
    const auto parameterID = juce::String (ShredEQ::ParameterIDs::abState);
    const auto slotValue = static_cast<int> (slot);

    if (state.hasProperty (parameterID))
        state.setProperty (parameterID, slotValue, nullptr);

    if (auto node = findParameterNode (state, parameterID); node.isValid() && node != state)
        node.setProperty ("value", slotValue, nullptr);
}

void StateManager::captureSlot (Slot slot,
                                juce::AudioProcessorValueTreeState& apvts)
{
    auto snapshot = apvts.copyState();
    snapshot.setProperty (schemaProperty, schema_version, nullptr);
    setSnapshotSlotParameter (snapshot, slot);
    getSlotState (slot) = std::move (snapshot);
}

void StateManager::captureSlot (juce::AudioProcessorValueTreeState& apvts)
{
    captureSlot (getActiveSlot(), apvts);
}

bool StateManager::applySlot (Slot slot,
                              juce::AudioProcessorValueTreeState& apvts) const
{
    auto snapshot = getSlotState (slot);
    if (! snapshot.isValid())
        return false;

    const auto version = static_cast<int> (snapshot.getProperty (schemaProperty, 1));
    if (version > schema_version || ! snapshot.hasType (apvts.state.getType()))
        return false;

    snapshot = snapshot.createCopy();
    setSnapshotSlotParameter (snapshot, slot);
    apvts.replaceState (snapshot);
    return true;
}

bool StateManager::applySlot (juce::AudioProcessorValueTreeState& apvts) const
{
    return applySlot (getActiveSlot(), apvts);
}

void StateManager::switchActiveSlot (Slot target) noexcept
{
    activeSlot.store (target);
}

bool StateManager::switchActiveSlot (Slot target,
                                     juce::AudioProcessorValueTreeState& apvts,
                                     bool captureOutgoingState)
{
    const auto outgoingSlot = getActiveSlot();
    if (captureOutgoingState && target != outgoingSlot)
        captureSlot (outgoingSlot, apvts);

    activeSlot.store (target);
    return applySlot (target, apvts);
}

void StateManager::copyAtoB()
{
    if (slotA.isValid())
    {
        slotB = slotA.createCopy();
        setSnapshotSlotParameter (slotB, Slot::B);
    }
}

void StateManager::copyBtoA()
{
    if (slotB.isValid())
    {
        slotA = slotB.createCopy();
        setSnapshotSlotParameter (slotA, Slot::A);
    }
}

void StateManager::resetSlot (Slot slot)
{
    getSlotState (slot) = {};
}

juce::ValueTree StateManager::makeDefaultState (
    juce::AudioProcessorValueTreeState& apvts)
{
    auto state = apvts.copyState();
    setParametersToDefaults (state, apvts);

    state.setProperty (schemaProperty, schema_version, nullptr);
    return state;
}

void StateManager::resetSlot (Slot slot,
                              juce::AudioProcessorValueTreeState& apvts)
{
    auto defaults = makeDefaultState (apvts);
    setSnapshotSlotParameter (defaults, slot);
    getSlotState (slot) = std::move (defaults);
}

void StateManager::captureFrom (juce::AudioProcessorValueTreeState& apvts)
{
    auto slot = getActiveSlot();
    if (const auto* parameter = apvts.getRawParameterValue (
            ShredEQ::ParameterIDs::abState))
        slot = parameter->load() >= 0.5f ? Slot::B : Slot::A;

    activeSlot.store (slot);
    captureSlot (slot, apvts);
}

void StateManager::applyTo (juce::AudioProcessorValueTreeState& apvts) const
{
    auto slot = getActiveSlot();
    if (const auto* parameter = apvts.getRawParameterValue (
            ShredEQ::ParameterIDs::abState))
        slot = parameter->load() >= 0.5f ? Slot::B : Slot::A;

    applySlot (slot, apvts);
}

void StateManager::swapAB()
{
    std::swap (slotA, slotB);
    setSnapshotSlotParameter (slotA, Slot::A);
    setSnapshotSlotParameter (slotB, Slot::B);
}

void StateManager::beginCrossfade (double sampleRate, double durationMs) noexcept
{
    const auto safeRate = std::max (1.0, sampleRate);
    const auto safeDuration = juce::jlimit (5.0, 30.0, durationMs);
    crossfadeLengthSamples = std::max (1, juce::roundToInt (safeRate * safeDuration * 0.001));
    crossfadePositionSamples = 0;
    freezeGMatch (true);
}

void StateManager::advanceCrossfade (int numSamples) noexcept
{
    if (numSamples <= 0 || ! isCrossfadeActive())
        return;

    crossfadePositionSamples = std::min (crossfadeLengthSamples,
                                         crossfadePositionSamples + numSamples);
    if (! isCrossfadeActive())
        freezeGMatch (false);
}

float StateManager::getCrossfadeGainA() const noexcept
{
    if (crossfadeLengthSamples <= 0)
        return getActiveSlot() == Slot::A ? 1.0f : 0.0f;

    const auto progress = juce::jlimit (
        0.0, 1.0, static_cast<double> (crossfadePositionSamples)
                      / static_cast<double> (crossfadeLengthSamples));
    const auto targetGain = std::sin (progress * halfPi);
    const auto sourceGain = std::cos (progress * halfPi);
    return static_cast<float> (getActiveSlot() == Slot::A ? targetGain : sourceGain);
}

float StateManager::getCrossfadeGainB() const noexcept
{
    if (crossfadeLengthSamples <= 0)
        return getActiveSlot() == Slot::B ? 1.0f : 0.0f;

    const auto progress = juce::jlimit (
        0.0, 1.0, static_cast<double> (crossfadePositionSamples)
                      / static_cast<double> (crossfadeLengthSamples));
    const auto targetGain = std::sin (progress * halfPi);
    const auto sourceGain = std::cos (progress * halfPi);
    return static_cast<float> (getActiveSlot() == Slot::B ? targetGain : sourceGain);
}

bool StateManager::isCrossfadeActive() const noexcept
{
    return crossfadeLengthSamples > 0
        && crossfadePositionSamples < crossfadeLengthSamples;
}

void StateManager::prepare (double sampleRate)
{
    meterSampleRate = std::max (1.0, sampleRate);
    const auto capacity = static_cast<size_t> (
        std::max (1, juce::roundToInt (meterSampleRate * rollingWindowSeconds)));
    drySquaredWindow.assign (capacity, 0.0);
    wetSquaredWindow.assign (capacity, 0.0);
    reset();
}

void StateManager::reset() noexcept
{
    std::fill (drySquaredWindow.begin(), drySquaredWindow.end(), 0.0);
    std::fill (wetSquaredWindow.begin(), wetSquaredWindow.end(), 0.0);
    rollingWriteIndex = 0;
    rollingSamples = 0;
    rollingDrySum = 0.0;
    rollingWetSum = 0.0;
    matchGainDb.store (0.0f);
    gainMatchFrozen.store (false);
}

void StateManager::setGainMatchEnabled (bool shouldBeEnabled) noexcept
{
    gainMatchEnabled.store (shouldBeEnabled);
}

void StateManager::appendSquaredLevels (double drySquared,
                                        double wetSquared,
                                        int numSamples) noexcept
{
    if (drySquaredWindow.empty() || numSamples <= 0)
        return;

    const auto capacity = drySquaredWindow.size();
    for (int sample = 0; sample < numSamples; ++sample)
    {
        if (rollingSamples == capacity)
        {
            rollingDrySum -= drySquaredWindow[rollingWriteIndex];
            rollingWetSum -= wetSquaredWindow[rollingWriteIndex];
        }
        else
        {
            ++rollingSamples;
        }

        drySquaredWindow[rollingWriteIndex] = drySquared;
        wetSquaredWindow[rollingWriteIndex] = wetSquared;
        rollingDrySum += drySquared;
        rollingWetSum += wetSquared;
        rollingWriteIndex = (rollingWriteIndex + 1) % capacity;
    }
}

void StateManager::updateMatchGain (int numSamples) noexcept
{
    if (rollingSamples == 0 || gainMatchFrozen.load() || ! gainMatchEnabled.load())
        return;

    const auto dryRms = std::sqrt (std::max (0.0, rollingDrySum / rollingSamples));
    const auto wetRms = std::sqrt (std::max (0.0, rollingWetSum / rollingSamples));
    const auto dryDb = juce::Decibels::gainToDecibels (dryRms, -160.0);
    const auto wetDb = juce::Decibels::gainToDecibels (wetRms, -160.0);

    if (dryDb <= silenceFloorDb || wetDb <= silenceFloorDb)
        return;

    const auto targetDb = juce::jlimit (-maximumMatchDb,
                                        maximumMatchDb,
                                        static_cast<float> (dryDb - wetDb));
    const auto currentDb = matchGainDb.load();
    const auto movingFurtherFromUnity = std::abs (targetDb) > std::abs (currentDb);
    const auto timeSeconds = movingFurtherFromUnity ? 1.5 : 3.0;
    const auto alpha = 1.0 - std::exp (-static_cast<double> (std::max (1, numSamples))
                                      / (meterSampleRate * timeSeconds));
    const auto smoothed = currentDb
        + static_cast<float> (alpha) * (targetDb - currentDb);
    matchGainDb.store (juce::jlimit (-maximumMatchDb, maximumMatchDb, smoothed));
}

void StateManager::processMatchLevels (float dryRms,
                                       float wetRms,
                                       int numSamples) noexcept
{
    if (numSamples <= 0 || drySquaredWindow.empty())
        return;

    const auto safeDry = std::isfinite (dryRms) ? std::max (0.0f, dryRms) : 0.0f;
    const auto safeWet = std::isfinite (wetRms) ? std::max (0.0f, wetRms) : 0.0f;
    appendSquaredLevels (static_cast<double> (safeDry) * safeDry,
                         static_cast<double> (safeWet) * safeWet,
                         numSamples);
    updateMatchGain (numSamples);
}

void StateManager::processMatchSamples (const float* dry,
                                        const float* wet,
                                        int numSamples) noexcept
{
    if (dry == nullptr || wet == nullptr || numSamples <= 0)
        return;

    double drySum = 0.0;
    double wetSum = 0.0;
    for (int i = 0; i < numSamples; ++i)
    {
        const auto drySample = std::isfinite (dry[i]) ? dry[i] : 0.0f;
        const auto wetSample = std::isfinite (wet[i]) ? wet[i] : 0.0f;
        drySum += static_cast<double> (drySample) * drySample;
        wetSum += static_cast<double> (wetSample) * wetSample;
    }

    processMatchLevels (static_cast<float> (std::sqrt (drySum / numSamples)),
                        static_cast<float> (std::sqrt (wetSum / numSamples)),
                        numSamples);
}

void StateManager::freezeGMatch (bool shouldFreeze) noexcept
{
    gainMatchFrozen.store (shouldFreeze);
}

bool StateManager::commitMatchTrim (juce::AudioProcessorValueTreeState& apvts)
{
    auto* parameter = dynamic_cast<juce::RangedAudioParameter*> (
        apvts.getParameter (ShredEQ::ParameterIDs::outputGain));
    if (parameter == nullptr)
        return false;

    const auto currentDb = parameter->convertFrom0to1 (parameter->getValue());
    const auto committedDb = currentDb + matchGainDb.load();
    const auto normalized = parameter->convertTo0to1 (committedDb);
    parameter->beginChangeGesture();
    parameter->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, normalized));
    parameter->endChangeGesture();
    matchGainDb.store (0.0f);
    return true;
}

juce::ValueTree StateManager::createComparisonState() const
{
    juce::ValueTree root (comparisonStateType);
    root.setProperty (schemaProperty, schema_version, nullptr);
    root.setProperty (activeSlotProperty, static_cast<int> (getActiveSlot()), nullptr);

    juce::ValueTree aWrapper (slotAType);
    juce::ValueTree bWrapper (slotBType);
    if (slotA.isValid())
        aWrapper.addChild (slotA.createCopy(), -1, nullptr);
    if (slotB.isValid())
        bWrapper.addChild (slotB.createCopy(), -1, nullptr);
    root.addChild (aWrapper, -1, nullptr);
    root.addChild (bWrapper, -1, nullptr);
    return root;
}

bool StateManager::restoreComparisonState (
    const juce::ValueTree& state,
    juce::AudioProcessorValueTreeState& apvts)
{
    if (! state.hasType (comparisonStateType))
        return false;

    const auto version = static_cast<int> (state.getProperty (schemaProperty, 1));
    if (version > schema_version)
        return false;

    const auto aWrapper = state.getChildWithName (slotAType);
    const auto bWrapper = state.getChildWithName (slotBType);
    auto restoredA = aWrapper.getNumChildren() > 0 ? aWrapper.getChild (0).createCopy()
                                                  : juce::ValueTree {};
    auto restoredB = bWrapper.getNumChildren() > 0 ? bWrapper.getChild (0).createCopy()
                                                  : juce::ValueTree {};
    const auto restoredActive =
        static_cast<int> (state.getProperty (activeSlotProperty, 0)) == 1
            ? Slot::B : Slot::A;
    const auto& restoredCurrent = restoredActive == Slot::A ? restoredA : restoredB;
    const auto hasRestoredCurrent = restoredCurrent.isValid();
    const auto isValidSnapshot = [&apvts] (const juce::ValueTree& snapshot)
    {
        return ! snapshot.isValid()
            || (snapshot.hasType (apvts.state.getType())
                && static_cast<int> (snapshot.getProperty (schemaProperty, 1))
                       <= schema_version);
    };

    if (! isValidSnapshot (restoredA) || ! isValidSnapshot (restoredB))
        return false;

    slotA = std::move (restoredA);
    slotB = std::move (restoredB);
    activeSlot.store (restoredActive);
    return ! hasRestoredCurrent || applySlot (restoredActive, apvts);
}

std::unique_ptr<juce::XmlElement> StateManager::createComparisonXml() const
{
    return createComparisonState().createXml();
}

bool StateManager::restoreComparisonXml (
    const juce::XmlElement& xml,
    juce::AudioProcessorValueTreeState& apvts)
{
    return restoreComparisonState (juce::ValueTree::fromXml (xml), apvts);
}

juce::StringArray StateManager::listFactoryPresets() const
{
    juce::StringArray list;
    auto dir = getFactoryPresetsDirectory();
    if (dir.isDirectory())
        for (const auto& entry : juce::RangedDirectoryIterator (
                 dir, false, "*.xml", juce::File::findFiles))
            list.add (entry.getFile().getFileNameWithoutExtension());
    return list;
}

bool StateManager::loadFactoryPreset (
    const juce::String& name,
    juce::AudioProcessorValueTreeState& apvts) const
{
    const auto file = getFactoryPresetsDirectory().getChildFile (name + ".xml");
    if (! file.existsAsFile())
        return false;

    const auto xml = juce::XmlDocument::parse (file);
    return xml != nullptr && loadFromXml (*xml, apvts);
}

juce::File StateManager::getUserPresetsDirectory() const
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
        .getChildFile ("nSpace Systems")
        .getChildFile ("ShredEQ")
        .getChildFile ("Presets");
}

bool StateManager::saveUserPreset (
    const juce::String& name,
    juce::AudioProcessorValueTreeState& apvts)
{
    auto dir = getUserPresetsDirectory();
    if (! dir.exists() && ! dir.createDirectory().wasOk())
        return false;

    captureSlot (getActiveSlot(), apvts);
    const auto xml = createComparisonXml();
    return xml != nullptr && xml->writeTo (dir.getChildFile (name + ".xml"));
}

bool StateManager::loadUserPreset (
    const juce::String& name,
    juce::AudioProcessorValueTreeState& apvts)
{
    const auto file = getUserPresetsDirectory().getChildFile (name + ".xml");
    if (! file.existsAsFile())
        return false;

    const auto xml = juce::XmlDocument::parse (file);
    if (xml == nullptr)
        return false;

    if (xml->hasTagName (comparisonStateType))
        return restoreComparisonXml (*xml, apvts);

    return loadFromXml (*xml, apvts);
}

juce::StringArray StateManager::listUserPresets() const
{
    juce::StringArray list;
    const auto dir = getUserPresetsDirectory();
    if (dir.isDirectory())
        for (const auto& entry : juce::RangedDirectoryIterator (
                 dir, false, "*.xml", juce::File::findFiles))
            list.add (entry.getFile().getFileNameWithoutExtension());
    return list;
}

bool StateManager::loadFromXml (
    const juce::XmlElement& xml,
    juce::AudioProcessorValueTreeState& apvts) const
{
    const juce::XmlElement* stateXml = &xml;

    if (xml.hasTagName ("Preset"))
    {
        stateXml = xml.getChildByName (apvts.state.getType().toString());
        if (stateXml == nullptr)
            stateXml = xml.getChildByName ("ShredEQState");
        if (stateXml == nullptr)
            return false;
    }

    if (! (stateXml->hasTagName (apvts.state.getType())
           || stateXml->hasTagName ("ShredEQState")))
        return false;

    const auto version = stateXml->getIntAttribute (
        schemaProperty, stateXml->getIntAttribute ("schemaVersion", 1));
    if (version > schema_version)
        return false;

    auto state = juce::ValueTree::fromXml (*stateXml);
    if (! state.isValid())
        return false;

    apvts.replaceState (state);
    return true;
}

juce::File StateManager::getFactoryPresetsDirectory() const
{
    const auto exe = juce::File::getSpecialLocation (juce::File::currentExecutableFile);
    auto dir = exe.getParentDirectory();
    for (int i = 0; i < 5; ++i)
    {
        const auto presetsDir = dir.getChildFile ("Resources").getChildFile ("Presets");
        if (presetsDir.isDirectory()
            || dir.getChildFile ("Resources").isDirectory())
            return presetsDir;
        dir = dir.getParentDirectory();
    }

    return exe.getParentDirectory().getChildFile ("Resources").getChildFile ("Presets");
}
