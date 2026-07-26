#include "PluginProcessor.h"

#include "ParameterIDs.h"

#if __has_include("PluginEditor.h")
#include "PluginEditor.h"
#define SHREDEQ_HAS_CUSTOM_EDITOR 1
#else
#define SHREDEQ_HAS_CUSTOM_EDITOR 0
#endif

#include <array>
#include <memory>
#include <utility>

namespace
{
constexpr auto stateTreeType = "ShredEQState";
constexpr auto schemaVersionProperty = "schema_version";

juce::NormalisableRange<float> frequencyRange (float minimum,
                                                float maximum,
                                                float centre)
{
    juce::NormalisableRange<float> range (minimum, maximum);
    range.setSkewForCentre (centre);
    return range;
}
} // namespace

ShredEQAudioProcessor::ShredEQAudioProcessor()
    : juce::AudioProcessor (BusesProperties()
                                .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                .withInput ("Sidechain", juce::AudioChannelSet::stereo(), false)
                                .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, stateTreeType, createParameterLayout())
{
}

juce::AudioProcessorValueTreeState::ParameterLayout
ShredEQAudioProcessor::createParameterLayout()
{
    namespace IDs = ShredEQ::ParameterIDs;

    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    const auto parameterID = [] (const char* id)
    {
        return juce::ParameterID { id, IDs::schemaVersion };
    };

    const auto addBool = [&] (const char* id,
                              const juce::String& name,
                              bool defaultValue)
    {
        layout.add (std::make_unique<juce::AudioParameterBool> (
            parameterID (id), name, defaultValue));
    };

    const auto addFloat = [&] (const char* id,
                               const juce::String& name,
                               juce::NormalisableRange<float> range,
                               float defaultValue,
                               const juce::String& unit = {})
    {
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            parameterID (id), name, range, defaultValue, unit));
    };

    const auto addChoice = [&] (const char* id,
                                const juce::String& name,
                                juce::StringArray choices,
                                int defaultIndex)
    {
        layout.add (std::make_unique<juce::AudioParameterChoice> (
            parameterID (id), name, std::move (choices), defaultIndex));
    };

    const juce::StringArray stages { "Stage A", "Stage B" };
    const juce::StringArray filterTypes {
        "Bell", "Notch", "Low Shelf", "High Shelf", "Low Pass", "High Pass",
        "Band Pass", "Tilt", "Resonant High Pass", "Resonant Low Pass",
        "Flat Dynamic"
    };
    const juce::StringArray slopes {
        "6 dB/oct", "12 dB/oct", "18 dB/oct", "24 dB/oct", "36 dB/oct",
        "48 dB/oct"
    };
    const juce::StringArray placements {
        "Stereo Linked", "Stereo Unlinked", "Left", "Right", "Mid", "Side"
    };
    const juce::StringArray dynamicModes {
        "Static", "Downward", "Upward", "Compression", "Expansion",
        "Transient Only", "Sustain Only", "External Sidechain"
    };
    const juce::StringArray detectorSources {
        "Full-band Internal", "Band-limited Internal", "Left", "Right", "Mid",
        "Side", "External Sidechain", "Input Minus Output", "Transient",
        "Sustain"
    };
    const juce::StringArray trackingSources { "Off", "Audio", "MIDI" };
    const juce::StringArray harmonics {
        "1", "2", "3", "4", "5", "6", "7", "8"
    };
    const juce::StringArray lookaheadChoices {
        "Off", "0.5 ms", "1 ms", "2 ms", "5 ms"
    };

    constexpr std::array<float, 12> defaultBandFrequencies {
        60.0f, 90.0f, 140.0f, 220.0f, 350.0f, 550.0f,
        850.0f, 1300.0f, 2000.0f, 3200.0f, 5200.0f, 8500.0f
    };

    for (std::size_t index = 0; index < IDs::bands.size(); ++index)
    {
        const auto& band = IDs::bands[index];
        const auto prefix = "Band " + juce::String (static_cast<int> (index + 1)) + " ";

        addBool (band.enabled, prefix + "Enabled", false);
        addChoice (band.stage, prefix + "Stage", stages, 0);
        addChoice (band.type, prefix + "Type", filterTypes, 0);
        addFloat (band.frequency, prefix + "Frequency",
                  frequencyRange (20.0f, 20000.0f, 1000.0f),
                  defaultBandFrequencies[index], "Hz");
        addFloat (band.gain, prefix + "Gain", { -24.0f, 24.0f, 0.01f }, 0.0f, "dB");
        addFloat (band.q, prefix + "Q", frequencyRange (0.1f, 36.0f, 1.0f), 0.707f);
        addChoice (band.slope, prefix + "Slope", slopes, 1);
        addChoice (band.route, prefix + "Route", placements, 0);
        addChoice (band.dynamicMode, prefix + "Dynamic Mode", dynamicModes, 0);
        addFloat (band.dynamicRange, prefix + "Dynamic Range",
                  { -24.0f, 18.0f, 0.01f }, 0.0f, "dB");
        addFloat (band.threshold, prefix + "Threshold",
                  { -90.0f, 0.0f, 0.01f }, -24.0f, "dBFS");
        addFloat (band.ratio, prefix + "Ratio", { 1.0f, 10.0f, 0.01f }, 2.0f, ":1");
        addFloat (band.attack, prefix + "Attack",
                  frequencyRange (0.1f, 300.0f, 10.0f), 10.0f, "ms");
        addFloat (band.hold, prefix + "Hold", { 0.0f, 500.0f, 0.1f }, 0.0f, "ms");
        addFloat (band.release, prefix + "Release",
                  frequencyRange (5.0f, 3000.0f, 150.0f), 150.0f, "ms");
        addFloat (band.knee, prefix + "Knee", { 0.0f, 18.0f, 0.01f }, 6.0f, "dB");
        addChoice (band.detectorSource, prefix + "Detector Source", detectorSources, 1);
        addFloat (band.detectorFrequency, prefix + "Detector Frequency",
                  frequencyRange (20.0f, 20000.0f, 1000.0f),
                  defaultBandFrequencies[index], "Hz");
        addFloat (band.detectorQ, prefix + "Detector Q",
                  frequencyRange (0.15f, 12.0f, 1.0f), 1.0f);
        addChoice (band.trackingSource, prefix + "Tracking Source", trackingSources, 0);
        addChoice (band.trackingHarmonic, prefix + "Tracking Harmonic", harmonics, 0);
        addChoice (band.lookahead, prefix + "Lookahead", lookaheadChoices, 0);
    }

    addFloat (IDs::inputGain, "Input Gain", { -24.0f, 24.0f, 0.01f }, 0.0f, "dB");
    addFloat (IDs::outputGain, "Output Gain", { -24.0f, 24.0f, 0.01f }, 0.0f, "dB");
    addFloat (IDs::outputTrim, "Output Trim", { -12.0f, 12.0f, 0.01f }, 0.0f, "dB");
    addBool (IDs::globalBypass, "Global Bypass", false);
    addBool (IDs::analyzerEnable, "Analyzer Enable", true);
    addChoice (IDs::abState, "A/B State", { "A", "B" }, 0);
    addChoice (IDs::profileIndex, "Profile", {
        "Guitar DI / Pre-Amp", "Post Amp / Cab", "Rhythm Guitar", "Lead Guitar",
        "Stereo Guitar Bus", "Active Bass", "Distorted Bass"
    }, 0);
    addChoice (IDs::tuningIndex, "Tuning", {
        "E Standard", "Eb Standard", "D Standard", "C# Standard", "C Standard",
        "B Standard", "Drop D", "Drop C#", "Drop C", "Drop B", "Drop A",
        "7-String Standard", "7-String Drop A", "8-String Standard",
        "8-String Drop E", "4-String Bass", "5-String Bass", "Custom"
    }, 0);
    addChoice (IDs::lookaheadMode, "Global Lookahead", lookaheadChoices, 0);

    addBool (IDs::TightCut::enabled, "Tight Cut Enabled", false);
    addFloat (IDs::TightCut::frequency, "Tight Cut Frequency",
              frequencyRange (25.0f, 220.0f, 80.0f), 80.0f, "Hz");
    addChoice (IDs::TightCut::slope, "Tight Cut Slope",
               { "12 dB/oct", "18 dB/oct", "24 dB/oct", "36 dB/oct" }, 2);
    addFloat (IDs::TightCut::resonance, "Tight Cut Resonance",
              { 0.5f, 2.5f, 0.001f }, 0.707f);
    addFloat (IDs::TightCut::punchRatio, "Tight Cut Punch Ratio",
              { 0.75f, 1.5f, 0.001f }, 1.0f);
    addFloat (IDs::TightCut::punchGain, "Tight Cut Punch Gain",
              { 0.0f, 8.0f, 0.01f }, 0.0f, "dB");
    addFloat (IDs::TightCut::punchQ, "Tight Cut Punch Q",
              { 0.35f, 3.0f, 0.001f }, 0.707f);
    addChoice (IDs::TightCut::placement, "Tight Cut Placement",
               { "Pre Stage A", "Post Stage A" }, 0);
    addChoice (IDs::TightCut::routing, "Tight Cut Routing",
               { "Stereo", "Mid", "Side" }, 0);
    addBool (IDs::TightCut::listenRemoved, "Tight Cut Listen Removed", false);

    addBool (IDs::Chug::enabled, "Chug Stabilizer Enabled", false);
    addFloat (IDs::Chug::frequency, "Chug Target Frequency",
              frequencyRange (60.0f, 320.0f, 140.0f), 140.0f, "Hz");
    addFloat (IDs::Chug::bandwidth, "Chug Bandwidth",
              { 0.3f, 2.5f, 0.001f }, 1.0f, "oct");
    addFloat (IDs::Chug::maxReduction, "Chug Maximum Reduction",
              { 0.0f, 18.0f, 0.01f }, 6.0f, "dB");
    addFloat (IDs::Chug::threshold, "Chug Threshold",
              { -90.0f, 0.0f, 0.01f }, -24.0f, "dBFS");
    addFloat (IDs::Chug::attack, "Chug Attack",
              frequencyRange (0.2f, 20.0f, 3.0f), 3.0f, "ms");
    addFloat (IDs::Chug::hold, "Chug Hold", { 0.0f, 80.0f, 0.1f }, 20.0f, "ms");
    addFloat (IDs::Chug::release, "Chug Release",
              frequencyRange (15.0f, 350.0f, 100.0f), 100.0f, "ms");
    addFloat (IDs::Chug::preservePick, "Chug Preserve Pick",
              { 0.0f, 100.0f, 0.1f }, 50.0f, "%");
    addFloat (IDs::Chug::transientSens, "Chug Transient Sensitivity",
              { 0.0f, 100.0f, 0.1f }, 50.0f, "%");
    addFloat (IDs::Chug::dominanceSens, "Chug Dominance Sensitivity",
              { 0.0f, 100.0f, 0.1f }, 50.0f, "%");

    addBool (IDs::multibandEnabled, "Multiband Enabled", false);
    addFloat (IDs::multibandCrossovers[0], "Multiband Crossover 1",
              frequencyRange (40.0f, 500.0f, 140.0f), 140.0f, "Hz");
    addFloat (IDs::multibandCrossovers[1], "Multiband Crossover 2",
              frequencyRange (150.0f, 3000.0f, 900.0f), 900.0f, "Hz");
    addFloat (IDs::multibandCrossovers[2], "Multiband Crossover 3",
              frequencyRange (1000.0f, 12000.0f, 4500.0f), 4500.0f, "Hz");

    for (std::size_t index = 0; index < IDs::multibandZones.size(); ++index)
    {
        const auto& zone = IDs::multibandZones[index];
        const auto prefix = "Multiband Zone " + juce::String (static_cast<int> (index + 1)) + " ";
        addFloat (zone.gain, prefix + "Gain", { -24.0f, 24.0f, 0.01f }, 0.0f, "dB");
        addFloat (zone.threshold, prefix + "Threshold",
                  { -90.0f, 0.0f, 0.01f }, -24.0f, "dBFS");
        addFloat (zone.ratio, prefix + "Ratio", { 1.0f, 10.0f, 0.01f }, 2.0f, ":1");
        addFloat (zone.attack, prefix + "Attack",
                  frequencyRange (0.1f, 300.0f, 10.0f), 10.0f, "ms");
        addFloat (zone.release, prefix + "Release",
                  frequencyRange (5.0f, 3000.0f, 150.0f), 150.0f, "ms");
        addFloat (zone.transientEmphasis, prefix + "Transient Emphasis",
                  { -100.0f, 100.0f, 0.1f }, 0.0f, "%");
        addFloat (zone.sustainEmphasis, prefix + "Sustain Emphasis",
                  { -100.0f, 100.0f, 0.1f }, 0.0f, "%");
        addChoice (zone.placement, prefix + "Placement", placements, 0);
        addChoice (zone.detectorSource, prefix + "Detector Source", detectorSources, 1);
        addBool (zone.autoThreshold, prefix + "Auto Threshold", true);
        addChoice (zone.mode, prefix + "Mode", { "Downward", "Upward" }, 0);
    }

    addChoice (IDs::Sidechain::source, "Sidechain Source",
               { "External", "Main Input", "Mid", "Side" }, 0);
    addBool (IDs::Sidechain::lockInEnabled, "Mix Lock-In Enabled", false);
    addFloat (IDs::Sidechain::lockInAmount, "Mix Lock-In Amount",
              { 0.0f, 100.0f, 0.1f }, 50.0f, "%");
    addFloat (IDs::Sidechain::carveMaxGain, "Sidechain Carve Maximum",
              { 0.0f, 18.0f, 0.01f }, 6.0f, "dB");
    addFloat (IDs::Sidechain::carveThreshold, "Sidechain Carve Threshold",
              { 0.0f, 1.0f, 0.001f }, 0.5f);

    addFloat (IDs::MidSide::sideHighPass, "Side High-Pass",
              frequencyRange (20.0f, 500.0f, 150.0f), 120.0f, "Hz");
    addBool (IDs::MidSide::monoGuardEnabled, "Mono Guard Enabled", false);
    addFloat (IDs::MidSide::monoGuardProtection, "Mono Guard Protection",
              { 0.0f, 100.0f, 0.1f }, 50.0f, "%");

    for (std::size_t index = 0; index < IDs::resonanceSlots.size(); ++index)
    {
        const auto& slot = IDs::resonanceSlots[index];
        const auto prefix = "Resonance " + juce::String (static_cast<int> (index + 1)) + " ";
        addBool (slot.enabled, prefix + "Enabled", false);
        addFloat (slot.frequency, prefix + "Frequency",
                  frequencyRange (150.0f, 16000.0f, 3000.0f), 3000.0f, "Hz");
        addFloat (slot.q, prefix + "Q", frequencyRange (3.0f, 40.0f, 10.0f), 10.0f);
        addFloat (slot.maxDepth, prefix + "Maximum Depth",
                  { 0.0f, 18.0f, 0.01f }, 6.0f, "dB");
        addFloat (slot.attack, prefix + "Attack",
                  frequencyRange (0.5f, 100.0f, 10.0f), 10.0f, "ms");
        addFloat (slot.release, prefix + "Release",
                  frequencyRange (20.0f, 2000.0f, 200.0f), 200.0f, "ms");
        addFloat (slot.persistence, prefix + "Persistence",
                  { 0.0f, 100.0f, 0.1f }, 50.0f, "%");
        addChoice (slot.mode, prefix + "Mode", { "Static", "Dynamic" }, 1);
        addChoice (slot.placement, prefix + "Placement", placements, 0);
        addBool (slot.freeze, prefix + "Freeze", false);
    }

    addChoice (IDs::NoteFollower::trackingSource, "Note Tracking Source",
               { "Off", "Audio Monophonic", "MIDI" }, 0);
    addChoice (IDs::NoteFollower::harmonic, "Note Harmonic", harmonics, 0);
    addFloat (IDs::NoteFollower::centsOffset, "Note Cents Offset",
              { -1200.0f, 1200.0f, 1.0f }, 0.0f, "cents");
    addFloat (IDs::NoteFollower::confidenceThreshold, "Note Confidence Threshold",
              { 0.0f, 100.0f, 0.1f }, 70.0f, "%");
    addFloat (IDs::NoteFollower::smoothing, "Note Smoothing",
              { 0.0f, 1000.0f, 0.1f }, 80.0f, "ms");
    addFloat (IDs::NoteFollower::holdTime, "Note Hold Time",
              { 0.0f, 2000.0f, 0.1f }, 150.0f, "ms");
    addFloat (IDs::NoteFollower::returnTime, "Note Return Time",
              { 0.0f, 5000.0f, 0.1f }, 500.0f, "ms");
    addFloat (IDs::NoteFollower::minFreq, "Note Minimum Frequency",
              frequencyRange (20.0f, 1000.0f, 80.0f), 40.0f, "Hz");
    addFloat (IDs::NoteFollower::maxFreq, "Note Maximum Frequency",
              frequencyRange (200.0f, 20000.0f, 2000.0f), 4000.0f, "Hz");
    addChoice (IDs::NoteFollower::midiPriority, "MIDI Note Priority",
               { "Lowest", "Highest", "Last", "Most Recent Held" }, 2);

    return layout;
}

void ShredEQAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    tightCut.prepare (sampleRate, samplesPerBlock);
    chugStabilizer.prepare (sampleRate, samplesPerBlock);
    multibandEngine.prepare (sampleRate, samplesPerBlock);

    spectralAnalysisEngine.prepare (sampleRate, samplesPerBlock, 2048, 512);
    resonanceDetector.prepare (sampleRate, 2048);
    resonanceSuppressor.prepare (sampleRate);
    pitchDetector.prepare (sampleRate, samplesPerBlock);

    analysisSnapshot.prepare (spectralAnalysisEngine.getNumBins());
    controlSampleCounter = 0;
}

void ShredEQAudioProcessor::releaseResources()
{
    tightCut.reset();
    chugStabilizer.reset();
    multibandEngine.reset();

    spectralAnalysisEngine.setEnabled (false);
    resonanceDetector.setEnabled (false);
    resonanceSuppressor.reset();
    pitchDetector.reset();
}

void ShredEQAudioProcessor::reset()
{
    tightCut.reset();
    chugStabilizer.reset();
    multibandEngine.reset();

    spectralAnalysisEngine.reset();
    resonanceDetector.reset();
    resonanceSuppressor.reset();
    pitchDetector.reset();
    controlSampleCounter = 0;
}

bool ShredEQAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto mainInput = layouts.getMainInputChannelSet();
    const auto mainOutput = layouts.getMainOutputChannelSet();

    if (mainInput != mainOutput
        || (mainInput != juce::AudioChannelSet::mono()
            && mainInput != juce::AudioChannelSet::stereo()))
        return false;

    const auto sidechain = layouts.getChannelSet (true, 1);

    if (sidechain.isDisabled())
        return true;

    return mainInput == juce::AudioChannelSet::stereo()
        && (sidechain == juce::AudioChannelSet::mono()
            || sidechain == juce::AudioChannelSet::stereo());
}

void ShredEQAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer,
                                          juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    juce::ignoreUnused (midiMessages);

    const auto totalInputChannels = getTotalNumInputChannels();
    const auto totalOutputChannels = getTotalNumOutputChannels();

    for (int channel = totalInputChannels; channel < totalOutputChannels; ++channel)
        buffer.clear (channel, 0, buffer.getNumSamples());

    // Helper lambdas to safely read parameters from APVTS
    auto getFloatParam = [this] (const char* id) -> float
    {
        if (auto* param = apvts.getRawParameterValue (id))
            return param->load();
        return 0.0f;
    };
    auto getBoolParam = [this] (const char* id) -> bool
    {
        if (auto* param = apvts.getRawParameterValue (id))
            return param->load() > 0.5f;
        return false;
    };

    const auto numSamples = buffer.getNumSamples();
    auto* const* channels = buffer.getArrayOfWritePointers();
    const auto numChannels = buffer.getNumChannels();

    // Global bypass check
    if (getBoolParam (ShredEQ::ParameterIDs::globalBypass))
    {
        tightCut.reset();
        chugStabilizer.reset();
        multibandEngine.reset();
        resonanceSuppressor.reset();
        return;
    }

    // Push input audio to engine and pitch detector
    if (getBoolParam (ShredEQ::ParameterIDs::analyzerEnable))
    {
        spectralAnalysisEngine.setEnabled (true);
        (void) spectralAnalysisEngine.pushAudio (channels, numChannels, numSamples);
    }
    else
    {
        spectralAnalysisEngine.setEnabled (false);
    }

    if (numChannels > 0)
    {
        (void) pitchDetector.pushBlock (channels[0], numSamples);
    }

    // Control rate updates
    controlSampleCounter += numSamples;
    if (controlSampleCounter >= 256)
    {
        controlSampleCounter = 0;
        (void) pitchDetector.processControlRate();

        if (spectralAnalysisEngine.isEnabled())
        {
            if (spectralAnalysisEngine.readLatestSnapshot (analysisSnapshot))
            {
                (void) resonanceDetector.processControlRate (analysisSnapshot, pitchDetector.getLatestResult());
            }
        }
    }

    if (resonanceSuppressor.isScanning())
    {
        resonanceSuppressor.updateScan (numSamples, resonanceDetector);
    }

    // 1. Update TightCut parameters
    tightCut.setEnabled (getBoolParam (ShredEQ::ParameterIDs::TightCut::enabled));
    tightCut.setFrequency (getFloatParam (ShredEQ::ParameterIDs::TightCut::frequency));
    
    const auto tcSlopeChoice = static_cast<int> (std::round (getFloatParam (ShredEQ::ParameterIDs::TightCut::slope)));
    int slopeDb = 24;
    if (tcSlopeChoice == 0) slopeDb = 12;
    else if (tcSlopeChoice == 1) slopeDb = 18;
    else if (tcSlopeChoice == 2) slopeDb = 24;
    else if (tcSlopeChoice == 3) slopeDb = 36;
    tightCut.setSlope (slopeDb);

    tightCut.setResonance (getFloatParam (ShredEQ::ParameterIDs::TightCut::resonance));
    tightCut.setPunchRatio (getFloatParam (ShredEQ::ParameterIDs::TightCut::punchRatio));
    tightCut.setPunchGainDb (getFloatParam (ShredEQ::ParameterIDs::TightCut::punchGain));
    tightCut.setPunchQ (getFloatParam (ShredEQ::ParameterIDs::TightCut::punchQ));
    
    const auto tcPlacementChoice = static_cast<int> (std::round (getFloatParam (ShredEQ::ParameterIDs::TightCut::placement)));
    tightCut.setPlacement (static_cast<ShredEQ::DSP::TightCut::Placement> (tcPlacementChoice));
    
    const auto tcRoutingChoice = static_cast<int> (std::round (getFloatParam (ShredEQ::ParameterIDs::TightCut::routing)));
    tightCut.setRouting (static_cast<ShredEQ::DSP::TightCut::Routing> (tcRoutingChoice));
    
    tightCut.setListenRemoved (getBoolParam (ShredEQ::ParameterIDs::TightCut::listenRemoved));

    // 2. Update ChugStabilizer parameters
    chugStabilizer.setEnabled (getBoolParam (ShredEQ::ParameterIDs::Chug::enabled));
    chugStabilizer.setTargetFrequency (getFloatParam (ShredEQ::ParameterIDs::Chug::frequency));
    chugStabilizer.setBandwidthOctaves (getFloatParam (ShredEQ::ParameterIDs::Chug::bandwidth));
    chugStabilizer.setMaximumReductionDb (getFloatParam (ShredEQ::ParameterIDs::Chug::maxReduction));
    chugStabilizer.setThresholdDb (getFloatParam (ShredEQ::ParameterIDs::Chug::threshold));
    chugStabilizer.setAutomaticThreshold (false);
    chugStabilizer.setAttackMs (getFloatParam (ShredEQ::ParameterIDs::Chug::attack));
    chugStabilizer.setHoldMs (getFloatParam (ShredEQ::ParameterIDs::Chug::hold));
    chugStabilizer.setReleaseMs (getFloatParam (ShredEQ::ParameterIDs::Chug::release));
    chugStabilizer.setPreservePick (getFloatParam (ShredEQ::ParameterIDs::Chug::preservePick) > 0.0f);
    chugStabilizer.setTransientSensitivity (getFloatParam (ShredEQ::ParameterIDs::Chug::transientSens) / 100.0);
    chugStabilizer.setDominanceSensitivity (getFloatParam (ShredEQ::ParameterIDs::Chug::dominanceSens) / 100.0);

    // 3. Update MultibandEngine parameters
    multibandEngine.setEnabled (getBoolParam (ShredEQ::ParameterIDs::multibandEnabled));
    multibandEngine.setCrossoverHz (0, getFloatParam (ShredEQ::ParameterIDs::multibandCrossovers[0]));
    multibandEngine.setCrossoverHz (1, getFloatParam (ShredEQ::ParameterIDs::multibandCrossovers[1]));
    multibandEngine.setCrossoverHz (2, getFloatParam (ShredEQ::ParameterIDs::multibandCrossovers[2]));

    for (std::size_t index = 0; index < 4; ++index)
    {
        const auto& zone = ShredEQ::ParameterIDs::multibandZones[index];
        multibandEngine.setZoneGainDb (index, getFloatParam (zone.gain));
        multibandEngine.setZoneThresholdDb (index, getFloatParam (zone.threshold));
        multibandEngine.setZoneRatio (index, getFloatParam (zone.ratio));
        multibandEngine.setZoneAttackMs (index, getFloatParam (zone.attack));
        multibandEngine.setZoneReleaseMs (index, getFloatParam (zone.release));
        multibandEngine.setZoneTransientEmphasis (index, getFloatParam (zone.transientEmphasis));
        multibandEngine.setZoneSustainEmphasis (index, getFloatParam (zone.sustainEmphasis));
        
        const auto placementChoice = static_cast<int> (std::round (getFloatParam (zone.placement)));
        multibandEngine.setZonePlacement (index, static_cast<ShredEQ::DSP::MultibandZone::Placement> (placementChoice));
        
        const auto detChoice = static_cast<int> (std::round (getFloatParam (zone.detectorSource)));
        multibandEngine.setZoneDetectorSource (index, static_cast<ShredEQ::DSP::DetectorSource> (detChoice));
        
        multibandEngine.setZoneAutoThreshold (index, getBoolParam (zone.autoThreshold));
        
        const auto modeChoice = static_cast<int> (std::round (getFloatParam (zone.mode)));
        multibandEngine.setZoneUpward (index, modeChoice == 1);
    }

    // Update ResonanceSuppressor parameters
    for (int i = 0; i < 6; ++i)
    {
        const auto& slotIDs = ShredEQ::ParameterIDs::resonanceSlots[static_cast<std::size_t> (i)];
        auto& slot = resonanceSuppressor.getSlot (i);

        slot.setEnabled (getBoolParam (slotIDs.enabled));
        slot.setFrequency (getFloatParam (slotIDs.frequency));
        slot.setQ (getFloatParam (slotIDs.q));
        slot.setMaxDepthDb (getFloatParam (slotIDs.maxDepth));
        slot.setAttackMs (getFloatParam (slotIDs.attack));
        slot.setReleaseMs (getFloatParam (slotIDs.release));
        slot.setPersistenceRequirement (getFloatParam (slotIDs.persistence));

        const auto modeChoice = static_cast<int> (std::round (getFloatParam (slotIDs.mode)));
        slot.setIsStatic (modeChoice == 0);

        const auto placementChoice = static_cast<int> (std::round (getFloatParam (slotIDs.placement)));
        slot.setPlacement (static_cast<ShredEQ::DSP::StereoRoutingMode> (placementChoice));

        slot.setFreeze (getBoolParam (slotIDs.freeze));

        slot.updateDepthTarget (resonanceDetector);
    }

    // 4. Extract sidechain buffer if the bus is active
    const juce::AudioBuffer<float>* sidechainBufferPtr = nullptr;
    juce::AudioBuffer<float> sidechainBuffer;
    if (auto* bus = getBus (true, 1))
    {
        if (bus->isEnabled())
        {
            sidechainBuffer = getBusBuffer (buffer, true, 1);
            sidechainBufferPtr = &sidechainBuffer;
        }
    }

    // 5. Process DSP Chain
    const auto placement = static_cast<ShredEQ::DSP::TightCut::Placement> (tcPlacementChoice);

    // Pre Stage A processing
    if (placement == ShredEQ::DSP::TightCut::Placement::preStageA)
    {
        tightCut.process (channels, numChannels, numSamples);
    }

    // Chug Stabilizer processing
    chugStabilizer.process (channels, numChannels, numSamples);

    // Stage A / EQ processing would happen here

    // Multiband Engine processing
    multibandEngine.process (buffer, sidechainBufferPtr);

    // Resonance Suppressor processing
    // Documenting insertion point choice: Chosen to run as a late correction stage after
    // the main Tone EQ / Multiband processing to correct room or cabinet resonances.
    resonanceSuppressor.process (buffer);

    // Post Stage A processing
    if (placement == ShredEQ::DSP::TightCut::Placement::postStageA)
    {
        tightCut.process (channels, numChannels, numSamples);
    }
}

bool ShredEQAudioProcessor::hasEditor() const
{
    return true;
}

juce::AudioProcessorEditor* ShredEQAudioProcessor::createEditor()
{
#if SHREDEQ_HAS_CUSTOM_EDITOR
    return new ShredEQAudioProcessorEditor (*this);
#else
    return new juce::GenericAudioProcessorEditor (*this);
#endif
}

const juce::String ShredEQAudioProcessor::getName() const
{
    return JucePlugin_Name;
}

bool ShredEQAudioProcessor::acceptsMidi() const
{
    return true;
}

bool ShredEQAudioProcessor::producesMidi() const
{
    return false;
}

bool ShredEQAudioProcessor::isMidiEffect() const
{
    return false;
}

double ShredEQAudioProcessor::getTailLengthSeconds() const
{
    return 0.0;
}

int ShredEQAudioProcessor::getNumPrograms()
{
    return 1;
}

int ShredEQAudioProcessor::getCurrentProgram()
{
    return 0;
}

void ShredEQAudioProcessor::setCurrentProgram (int index)
{
    juce::ignoreUnused (index);
}

const juce::String ShredEQAudioProcessor::getProgramName (int index)
{
    juce::ignoreUnused (index);
    return {};
}

void ShredEQAudioProcessor::changeProgramName (int index,
                                               const juce::String& newName)
{
    juce::ignoreUnused (index, newName);
}

void ShredEQAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty (schemaVersionProperty, ShredEQ::ParameterIDs::schemaVersion, nullptr);

    if (const auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void ShredEQAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    const auto xml = getXmlFromBinary (data, sizeInBytes);

    if (xml == nullptr)
        return;

    auto state = juce::ValueTree::fromXml (*xml);

    if (state.isValid() && state.hasType (apvts.state.getType()))
    {
        if (! state.hasProperty (schemaVersionProperty))
            state.setProperty (schemaVersionProperty, 1, nullptr);

        apvts.replaceState (state);
    }
}

void ShredEQAudioProcessor::startResonanceScan (double durationSeconds) noexcept
{
    resonanceSuppressor.startScan (durationSeconds, getSampleRate(), resonanceDetector);
}

void ShredEQAudioProcessor::stopResonanceScan() noexcept
{
    resonanceSuppressor.endScan (resonanceDetector);
}

bool ShredEQAudioProcessor::isResonanceScanning() const noexcept
{
    return resonanceSuppressor.isScanning();
}

double ShredEQAudioProcessor::getResonanceScanProgress() const noexcept
{
    return resonanceSuppressor.getScanProgress();
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new ShredEQAudioProcessor();
}
