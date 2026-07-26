#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

/**
    StateManager handles schema-versioned XML state serialization,
    A/B state slots, factory presets, and user presets.
    
    @note This class is NOT real-time safe (RT-safe). All methods that perform
          file I/O or XML parsing/serialization must be called from the message thread.
*/
class StateManager
{
public:
    static constexpr int schema_version = 1;

    StateManager() = default;
    ~StateManager() = default;

    //==============================================================================
    // A/B Slot Operations
    //==============================================================================
    
    /** Captures the current APVTS state into the active slot (A or B) based on the abState parameter. */
    void captureFrom (const juce::AudioProcessorValueTreeState& apvts);
    
    /** Applies the state stored in the active slot (A or B) back to the APVTS. */
    void applyTo (juce::AudioProcessorValueTreeState& apvts) const;
    
    /** Swaps the states in slots A and B. */
    void swapAB();
    
    /** Copies state from slot A to slot B. */
    void copyAToB();
    
    /** Copies state from slot B to slot A. */
    void copyBToA();

    //==============================================================================
    // Preset Operations
    //==============================================================================
    
    /** Returns a list of available factory preset names found in the Resources/Presets directory. */
    juce::StringArray listFactoryPresets() const;
    
    /** Loads a factory preset by name into the provided APVTS. */
    bool loadFactoryPreset (const juce::String& name, juce::AudioProcessorValueTreeState& apvts) const;

    /** Returns the directory where user presets are saved. */
    juce::File getUserPresetsDirectory() const;

    /** Saves the current state of the APVTS as a user preset. */
    bool saveUserPreset (const juce::String& name, const juce::AudioProcessorValueTreeState& apvts);
    
    /** Loads a user preset by name into the provided APVTS. */
    bool loadUserPreset (const juce::String& name, juce::AudioProcessorValueTreeState& apvts) const;
    
    /** Returns a list of available user preset names. */
    juce::StringArray listUserPresets() const;

    //==============================================================================
    // Helpers
    //==============================================================================
    
    /** Helper to load a state from an XML element, performing safety and version checks. */
    bool loadFromXml (const juce::XmlElement& xml, juce::AudioProcessorValueTreeState& apvts) const;

    /** Helper to get the factory presets directory relative to the executable. */
    juce::File getFactoryPresetsDirectory() const;

private:
    juce::ValueTree slotA;
    juce::ValueTree slotB;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StateManager)
};
