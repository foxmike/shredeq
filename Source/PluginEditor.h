#pragma once

#ifndef JucePlugin_Name
#define JucePlugin_Name "ShredEQ"
#endif

#include "PluginProcessor.h"

//==============================================================================
/**
    A minimal skeletal PluginEditor for ShredEQ.
*/
class ShredEQAudioProcessorEditor  : public juce::AudioProcessorEditor
{
public:
    ShredEQAudioProcessorEditor (ShredEQAudioProcessor&);
    ~ShredEQAudioProcessorEditor() override;

    //==============================================================================
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    ShredEQAudioProcessor& audioProcessor;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ShredEQAudioProcessorEditor)
};
