#include "StateManager.h"
#include "ParameterIDs.h"

void StateManager::captureFrom (const juce::AudioProcessorValueTreeState& apvts)
{
    int activeSlot = 0;
    if (auto* paramVal = apvts.getRawParameterValue (ShredEQ::ParameterIDs::abState))
        activeSlot = juce::roundToInt (paramVal->load());

    auto stateCopy = apvts.copyState();
    stateCopy.setProperty ("schema_version", schema_version, nullptr);

    if (activeSlot == 0)
        slotA = stateCopy;
    else
        slotB = stateCopy;
}

void StateManager::applyTo (juce::AudioProcessorValueTreeState& apvts) const
{
    int activeSlot = 0;
    if (auto* paramVal = apvts.getRawParameterValue (ShredEQ::ParameterIDs::abState))
        activeSlot = juce::roundToInt (paramVal->load());

    const auto& sourceState = (activeSlot == 0) ? slotA : slotB;
    if (sourceState.isValid())
    {
        int version = sourceState.getProperty ("schema_version", 1);
        if (version <= schema_version)
        {
            apvts.replaceState (sourceState);
        }
    }
}

void StateManager::swapAB()
{
    std::swap (slotA, slotB);
}

void StateManager::copyAToB()
{
    if (slotA.isValid())
        slotB = slotA.createCopy();
}

void StateManager::copyBToA()
{
    if (slotB.isValid())
        slotA = slotB.createCopy();
}

juce::StringArray StateManager::listFactoryPresets() const
{
    juce::StringArray list;
    auto dir = getFactoryPresetsDirectory();
    if (dir.isDirectory())
    {
        for (const auto& entry : juce::RangedDirectoryIterator (dir, false, "*.xml", juce::File::findFiles))
        {
            list.add (entry.getFile().getFileNameWithoutExtension());
        }
    }
    return list;
}

bool StateManager::loadFactoryPreset (const juce::String& name, juce::AudioProcessorValueTreeState& apvts) const
{
    auto dir = getFactoryPresetsDirectory();
    auto file = dir.getChildFile (name + ".xml");
    if (! file.existsAsFile())
        return false;

    auto xml = juce::XmlDocument::parse (file);
    if (xml == nullptr)
        return false;

    return loadFromXml (*xml, apvts);
}

juce::File StateManager::getUserPresetsDirectory() const
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
        .getChildFile ("nSpace Systems")
        .getChildFile ("ShredEQ")
        .getChildFile ("Presets");
}

bool StateManager::saveUserPreset (const juce::String& name, const juce::AudioProcessorValueTreeState& apvts)
{
    auto dir = getUserPresetsDirectory();
    if (! dir.exists())
    {
        if (! dir.createDirectory().wasOk())
            return false;
    }

    auto file = dir.getChildFile (name + ".xml");
    
    auto state = apvts.copyState();
    state.setProperty ("schema_version", schema_version, nullptr);

    if (auto xml = state.createXml())
    {
        return xml->writeTo (file);
    }
    return false;
}

bool StateManager::loadUserPreset (const juce::String& name, juce::AudioProcessorValueTreeState& apvts) const
{
    auto dir = getUserPresetsDirectory();
    auto file = dir.getChildFile (name + ".xml");
    if (! file.existsAsFile())
        return false;

    auto xml = juce::XmlDocument::parse (file);
    if (xml == nullptr)
        return false;

    return loadFromXml (*xml, apvts);
}

juce::StringArray StateManager::listUserPresets() const
{
    juce::StringArray list;
    auto dir = getUserPresetsDirectory();
    if (dir.isDirectory())
    {
        for (const auto& entry : juce::RangedDirectoryIterator (dir, false, "*.xml", juce::File::findFiles))
        {
            list.add (entry.getFile().getFileNameWithoutExtension());
        }
    }
    return list;
}

bool StateManager::loadFromXml (const juce::XmlElement& xml, juce::AudioProcessorValueTreeState& apvts) const
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

    if (! (stateXml->hasTagName (apvts.state.getType()) || stateXml->hasTagName ("ShredEQState")))
        return false;

    int version = stateXml->getIntAttribute ("schema_version", stateXml->getIntAttribute ("schemaVersion", 1));
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
    auto exe = juce::File::getSpecialLocation (juce::File::currentExecutableFile);
    
    auto dir = exe.getParentDirectory();
    for (int i = 0; i < 5; ++i)
    {
        auto presetsDir = dir.getChildFile ("Resources").getChildFile ("Presets");
        if (presetsDir.isDirectory())
            return presetsDir;
        
        auto resourcesDir = dir.getChildFile ("Resources");
        if (resourcesDir.isDirectory())
            return presetsDir;
        
        dir = dir.getParentDirectory();
    }
    
    return exe.getParentDirectory().getChildFile ("Resources").getChildFile ("Presets");
}
