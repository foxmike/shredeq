#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "DSP/TightCut.h"
#include "DSP/ChugStabilizer.h"
#include "DSP/MultibandEngine.h"
#include "Analysis/SpectralAnalysisEngine.h"
#include "DSP/ResonanceDetector.h"
#include "DSP/ResonanceSuppressor.h"
#include "DSP/PitchDetector.h"

class ShredEQAudioProcessor : public juce::AudioProcessor
{
public:
    ShredEQAudioProcessor();
    ~ShredEQAudioProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void reset() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>& buffer,
                       juce::MidiBuffer& midiMessages) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    const juce::String getName() const override;
    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int index, const juce::String& newName) override;

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState& getAPVTS() noexcept { return apvts; }
    const juce::AudioProcessorValueTreeState& getAPVTS() const noexcept { return apvts; }

    // Scan Workflow APIs
    void startResonanceScan (double durationSeconds) noexcept;
    void stopResonanceScan() noexcept;
    bool isResonanceScanning() const noexcept;
    double getResonanceScanProgress() const noexcept;
    
    ShredEQ::DSP::ResonanceSuppressor& getResonanceSuppressor() noexcept { return resonanceSuppressor; }
    const ShredEQ::DSP::ResonanceSuppressor& getResonanceSuppressor() const noexcept { return resonanceSuppressor; }

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    juce::AudioProcessorValueTreeState apvts;

    ShredEQ::DSP::TightCut tightCut;
    ShredEQ::DSP::ChugStabilizer chugStabilizer;
    ShredEQ::DSP::MultibandEngine multibandEngine;

    // Phase G additions
    ShredEQ::Analysis::SpectralAnalysisEngine spectralAnalysisEngine;
    ShredEQ::DSP::ResonanceDetector resonanceDetector;
    ShredEQ::DSP::ResonanceSuppressor resonanceSuppressor;
    ShredEQ::DSP::PitchDetector pitchDetector;

    ShredEQ::Analysis::SpectralAnalysisEngine::Snapshot analysisSnapshot;
    int controlSampleCounter = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ShredEQAudioProcessor)
};

