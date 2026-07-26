#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
ShredEQAudioProcessorEditor::ShredEQAudioProcessorEditor (ShredEQAudioProcessor& p)
    : AudioProcessorEditor (&p), audioProcessor (p)
{
    setSize (960, 600);
}

ShredEQAudioProcessorEditor::~ShredEQAudioProcessorEditor()
{
}

//==============================================================================
void ShredEQAudioProcessorEditor::paint (juce::Graphics& g)
{
    // Dark premium background gradient
    juce::ColourGradient grad (
        juce::Colour (0xff0f1219), 0.0f, 0.0f,
        juce::Colour (0xff080a0f), 0.0f, static_cast<float> (getHeight()),
        false
    );
    g.setGradientFill (grad);
    g.fillAll();

    // Draw header panel
    g.setColour (juce::Colour (0xff00e5ff));
    g.setFont (juce::FontOptions (18.0f, juce::Font::bold));
    g.drawText ("SHREDEQ", 20, 15, 150, 24, juce::Justification::left);

    g.setColour (juce::Colour (0xff8f9ba8));
    g.setFont (juce::FontOptions (11.0f, juce::Font::plain));
    g.drawText ("PARAMETRIC & DYNAMIC EQUALIZER", 115, 20, 300, 16, juce::Justification::left);
}

void ShredEQAudioProcessorEditor::resized()
{
}
