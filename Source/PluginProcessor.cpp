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

float ShredEQAudioProcessor::getParamValue (const char* parameterID) const noexcept
{
    if (const auto* value = apvts.getRawParameterValue (parameterID))
        return value->load (std::memory_order_relaxed);

    jassertfalse;
    return 0.0f;
}

void ShredEQAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    parametricEQ.prepare (sampleRate, samplesPerBlock);
    midSideRouter.prepare (sampleRate, samplesPerBlock);
    tightCut.prepare (sampleRate, samplesPerBlock);
    chugStabilizer.prepare (sampleRate, samplesPerBlock);
    multibandEngine.prepare (sampleRate, samplesPerBlock);

    spectralAnalysisEngine.prepare (sampleRate, samplesPerBlock, 2048, 512);
    resonanceDetector.prepare (sampleRate, 2048);
    resonanceSuppressor.prepare (sampleRate);
    pitchDetector.prepare (sampleRate, samplesPerBlock);

    analysisSnapshot.prepare (spectralAnalysisEngine.getNumBins());
    controlSampleCounter = 0;

    constexpr double gainSmoothingSeconds = 0.02;
    inputGain.reset (sampleRate, gainSmoothingSeconds);
    outputGain.reset (sampleRate, gainSmoothingSeconds);
    outputTrim.reset (sampleRate, gainSmoothingSeconds);
    inputGain.setCurrentAndTargetValue (
        juce::Decibels::decibelsToGain (getParamValue (ShredEQ::ParameterIDs::inputGain)));
    outputGain.setCurrentAndTargetValue (
        juce::Decibels::decibelsToGain (getParamValue (ShredEQ::ParameterIDs::outputGain)));
    outputTrim.setCurrentAndTargetValue (
        juce::Decibels::decibelsToGain (getParamValue (ShredEQ::ParameterIDs::outputTrim)));
}

void ShredEQAudioProcessor::releaseResources()
{
    parametricEQ.reset();
    midSideRouter.reset();
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
    parametricEQ.reset();
    midSideRouter.reset();
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

    auto mainBuffer = getBusBuffer (buffer, false, 0);
    const auto numSamples = mainBuffer.getNumSamples();
    auto* const* channels = mainBuffer.getArrayOfWritePointers();
    const auto numChannels = mainBuffer.getNumChannels();

    const auto getBoolParam = [this] (const char* id) noexcept
    {
        return getParamValue (id) > 0.5f;
    };
    const auto getChoiceParam = [this] (const char* id) noexcept
    {
        return juce::roundToInt (getParamValue (id));
    };

    // Global bypass check
    if (getBoolParam (ShredEQ::ParameterIDs::globalBypass))
    {
        parametricEQ.reset();
        midSideRouter.reset();
        tightCut.reset();
        chugStabilizer.reset();
        multibandEngine.reset();
        resonanceSuppressor.reset();
        return;
    }

    inputGain.setTargetValue (juce::Decibels::decibelsToGain (
        getParamValue (ShredEQ::ParameterIDs::inputGain)));
    for (int sample = 0; sample < numSamples; ++sample)
    {
        const auto gain = inputGain.getNextValue();
        for (int channel = 0; channel < numChannels; ++channel)
            channels[channel][sample] *= gain;
    }

    // Feed the analysis layer from the conditioned main input.
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

    const auto tightCutEnabled = getBoolParam (ShredEQ::ParameterIDs::TightCut::enabled);
    const auto tightCutPlacement = static_cast<ShredEQ::DSP::TightCut::Placement> (
        getChoiceParam (ShredEQ::ParameterIDs::TightCut::placement));
    const auto tightCutRouting = static_cast<ShredEQ::DSP::TightCut::Routing> (
        getChoiceParam (ShredEQ::ParameterIDs::TightCut::routing));
    constexpr std::array<int, 4> tightCutSlopes { 12, 18, 24, 36 };
    const auto tightCutSlopeIndex = static_cast<std::size_t> (juce::jlimit (
        0, 3, getChoiceParam (ShredEQ::ParameterIDs::TightCut::slope)));

    tightCut.setEnabled (tightCutEnabled);
    tightCut.setFrequency (getParamValue (ShredEQ::ParameterIDs::TightCut::frequency));
    tightCut.setSlope (tightCutSlopes[tightCutSlopeIndex]);
    tightCut.setResonance (getParamValue (ShredEQ::ParameterIDs::TightCut::resonance));
    tightCut.setPunchRatio (getParamValue (ShredEQ::ParameterIDs::TightCut::punchRatio));
    tightCut.setPunchGainDb (getParamValue (ShredEQ::ParameterIDs::TightCut::punchGain));
    tightCut.setPunchQ (getParamValue (ShredEQ::ParameterIDs::TightCut::punchQ));
    tightCut.setPlacement (tightCutPlacement);
    tightCut.setRouting (tightCutRouting);
    tightCut.setListenRemoved (getBoolParam (ShredEQ::ParameterIDs::TightCut::listenRemoved));

    const auto chugEnabled = getBoolParam (ShredEQ::ParameterIDs::Chug::enabled);
    chugStabilizer.setEnabled (chugEnabled);
    chugStabilizer.setTargetFrequency (getParamValue (ShredEQ::ParameterIDs::Chug::frequency));
    chugStabilizer.setBandwidthOctaves (getParamValue (ShredEQ::ParameterIDs::Chug::bandwidth));
    chugStabilizer.setMaximumReductionDb (getParamValue (ShredEQ::ParameterIDs::Chug::maxReduction));
    chugStabilizer.setThresholdDb (getParamValue (ShredEQ::ParameterIDs::Chug::threshold));
    chugStabilizer.setAutomaticThreshold (false);
    chugStabilizer.setAttackMs (getParamValue (ShredEQ::ParameterIDs::Chug::attack));
    chugStabilizer.setHoldMs (getParamValue (ShredEQ::ParameterIDs::Chug::hold));
    chugStabilizer.setReleaseMs (getParamValue (ShredEQ::ParameterIDs::Chug::release));
    chugStabilizer.setPreservePick (
        getParamValue (ShredEQ::ParameterIDs::Chug::preservePick) > 0.0f);
    chugStabilizer.setTransientSensitivity (
        getParamValue (ShredEQ::ParameterIDs::Chug::transientSens) / 100.0);
    chugStabilizer.setDominanceSensitivity (
        getParamValue (ShredEQ::ParameterIDs::Chug::dominanceSens) / 100.0);

    constexpr std::array<int, 6> bandSlopes { 6, 12, 18, 24, 36, 48 };
    constexpr std::array<double, 5> lookaheadMilliseconds { 0.0, 0.5, 1.0, 2.0, 5.0 };

    for (std::size_t index = 0; index < ShredEQ::ParameterIDs::bands.size(); ++index)
    {
        const auto& ids = ShredEQ::ParameterIDs::bands[index];
        auto& band = parametricEQ[index];
        const auto dynamicMode = getChoiceParam (ids.dynamicMode);
        const auto slopeIndex = static_cast<std::size_t> (
            juce::jlimit (0, 5, getChoiceParam (ids.slope)));
        const auto lookaheadIndex = static_cast<std::size_t> (
            juce::jlimit (0, 4, getChoiceParam (ids.lookahead)));
        const auto trackingSource = getChoiceParam (ids.trackingSource);

        band.setEnabled (getBoolParam (ids.enabled));
        band.setStage (static_cast<ShredEQ::DSP::BandStage> (getChoiceParam (ids.stage)));
        band.setFilterType (
            static_cast<ShredEQ::DSP::BandFilterType> (getChoiceParam (ids.type)));
        band.setFrequency (getParamValue (ids.frequency));
        band.setGainDb (getParamValue (ids.gain));
        band.setQ (getParamValue (ids.q));
        band.setSlope (bandSlopes[slopeIndex]);
        band.setRoutingMode (
            static_cast<ShredEQ::DSP::StereoRoutingMode> (getChoiceParam (ids.route)));
        band.setDynamicMode (dynamicMode);
        band.setDynamicsEnabled (dynamicMode != 0);
        band.setDynamicRangeDb (getParamValue (ids.dynamicRange));
        band.setThresholdDb (getParamValue (ids.threshold));
        band.setRatio (getParamValue (ids.ratio));
        band.setAttackMs (getParamValue (ids.attack));
        band.setHoldMs (getParamValue (ids.hold));
        band.setReleaseMs (getParamValue (ids.release));
        band.setKneeDb (getParamValue (ids.knee));
        band.setDetectorSource (getChoiceParam (ids.detectorSource));
        band.setDetectorFrequency (getParamValue (ids.detectorFrequency));
        band.setDetectorQ (getParamValue (ids.detectorQ));
        band.setTrackingSource (trackingSource);
        band.setTrackingHarmonic (getChoiceParam (ids.trackingHarmonic) + 1);
        band.setNoteFollowerEnabled (trackingSource != 0);
        band.setLookaheadMs (lookaheadMilliseconds[lookaheadIndex]);
    }

    const auto multibandEnabled = getBoolParam (ShredEQ::ParameterIDs::multibandEnabled);
    multibandEngine.setEnabled (multibandEnabled);
    multibandEngine.setCrossoverHz (
        0, getParamValue (ShredEQ::ParameterIDs::multibandCrossovers[0]));
    multibandEngine.setCrossoverHz (
        1, getParamValue (ShredEQ::ParameterIDs::multibandCrossovers[1]));
    multibandEngine.setCrossoverHz (
        2, getParamValue (ShredEQ::ParameterIDs::multibandCrossovers[2]));

    for (std::size_t index = 0; index < 4; ++index)
    {
        const auto& zone = ShredEQ::ParameterIDs::multibandZones[index];
        multibandEngine.setZoneGainDb (index, getParamValue (zone.gain));
        multibandEngine.setZoneThresholdDb (index, getParamValue (zone.threshold));
        multibandEngine.setZoneRatio (index, getParamValue (zone.ratio));
        multibandEngine.setZoneAttackMs (index, getParamValue (zone.attack));
        multibandEngine.setZoneReleaseMs (index, getParamValue (zone.release));
        multibandEngine.setZoneTransientEmphasis (
            index, getParamValue (zone.transientEmphasis));
        multibandEngine.setZoneSustainEmphasis (
            index, getParamValue (zone.sustainEmphasis));
        multibandEngine.setZonePlacement (
            index, static_cast<ShredEQ::DSP::MultibandZone::Placement> (
                getChoiceParam (zone.placement)));
        multibandEngine.setZoneDetectorSource (
            index, static_cast<ShredEQ::DSP::DetectorSource> (
                getChoiceParam (zone.detectorSource)));
        multibandEngine.setZoneAutoThreshold (index, getBoolParam (zone.autoThreshold));
        multibandEngine.setZoneUpward (index, getChoiceParam (zone.mode) == 1);
    }

    bool anyResonanceSlotEnabled = false;
    for (int i = 0; i < ShredEQ::DSP::ResonanceSuppressor::numSlots; ++i)
    {
        const auto& slotIDs = ShredEQ::ParameterIDs::resonanceSlots[static_cast<std::size_t> (i)];
        auto& slot = resonanceSuppressor.getSlot (i);
        const auto slotEnabled = getBoolParam (slotIDs.enabled);

        anyResonanceSlotEnabled = anyResonanceSlotEnabled || slotEnabled;
        slot.setEnabled (slotEnabled);
        slot.setFrequency (getParamValue (slotIDs.frequency));
        slot.setQ (getParamValue (slotIDs.q));
        slot.setMaxDepthDb (getParamValue (slotIDs.maxDepth));
        slot.setAttackMs (getParamValue (slotIDs.attack));
        slot.setReleaseMs (getParamValue (slotIDs.release));
        slot.setPersistenceRequirement (getParamValue (slotIDs.persistence));
        slot.setIsStatic (getChoiceParam (slotIDs.mode) == 0);
        slot.setPlacement (static_cast<ShredEQ::DSP::StereoRoutingMode> (
            getChoiceParam (slotIDs.placement)));
        slot.setFreeze (getBoolParam (slotIDs.freeze));
        if (slotEnabled)
            slot.updateDepthTarget (resonanceDetector);
    }

    auto sidechainBuffer = getBusBuffer (buffer, true, 1);
    const auto* sidechainBus = getBus (true, 1);
    const auto sidechainEnabled = sidechainBus != nullptr && sidechainBus->isEnabled();
    const auto* sidechainBufferPtr = sidechainEnabled ? &sidechainBuffer : nullptr;
    const float* const* sidechainChannels = sidechainEnabled
        ? sidechainBuffer.getArrayOfReadPointers() : nullptr;
    const auto numSidechainChannels = sidechainEnabled ? sidechainBuffer.getNumChannels() : 0;

    const auto processTightCut = [&]() noexcept
    {
        if (!tightCutEnabled)
            return;

        if (numChannels == 2 && tightCutRouting != ShredEQ::DSP::TightCut::Routing::stereo)
        {
            midSideRouter.setEnabled (true);
            midSideRouter.processEncode (channels, numChannels, numSamples);
            tightCut.process (channels, numChannels, numSamples, true);
            midSideRouter.processDecode (channels, numChannels, numSamples);
            midSideRouter.setEnabled (false);
        }
        else
        {
            tightCut.process (channels, numChannels, numSamples, false);
        }
    };

    const auto processParametricStage = [&] (ShredEQ::DSP::BandStage stage) noexcept
    {
        bool hasMidSideBand = false;

        for (auto& band : parametricEQ.getBands())
        {
            if (!band.isEnabled() || band.getStage() != stage)
                continue;

            const auto routing = band.getRoutingMode();
            if (routing == ShredEQ::DSP::StereoRoutingMode::mid
                || routing == ShredEQ::DSP::StereoRoutingMode::side)
            {
                hasMidSideBand = true;
            }
            else
            {
                band.process (channels, numChannels, numSamples, false,
                              sidechainChannels, numSidechainChannels);
            }
        }

        if (numChannels != 2 || !hasMidSideBand)
            return;

        midSideRouter.setEnabled (true);
        midSideRouter.processEncode (channels, numChannels, numSamples);
        for (auto& band : parametricEQ.getBands())
        {
            const auto routing = band.getRoutingMode();
            if (band.isEnabled() && band.getStage() == stage
                && (routing == ShredEQ::DSP::StereoRoutingMode::mid
                    || routing == ShredEQ::DSP::StereoRoutingMode::side))
            {
                band.process (channels, numChannels, numSamples, true,
                              sidechainChannels, numSidechainChannels);
            }
        }
        midSideRouter.processDecode (channels, numChannels, numSamples);
        midSideRouter.setEnabled (false);
    };

    if (tightCutPlacement == ShredEQ::DSP::TightCut::Placement::preStageA)
        processTightCut();

    if (chugEnabled)
        chugStabilizer.process (channels, numChannels, numSamples);

    processParametricStage (ShredEQ::DSP::BandStage::stageA);

    if (tightCutPlacement == ShredEQ::DSP::TightCut::Placement::postStageA)
        processTightCut();

    if (anyResonanceSlotEnabled || resonanceSuppressor.getAuditionCardIndex() >= 0)
        resonanceSuppressor.process (channels, numChannels, numSamples);

    if (multibandEnabled)
        multibandEngine.process (mainBuffer, sidechainBufferPtr);

    processParametricStage (ShredEQ::DSP::BandStage::stageB);

    outputGain.setTargetValue (juce::Decibels::decibelsToGain (
        getParamValue (ShredEQ::ParameterIDs::outputGain)));
    outputTrim.setTargetValue (juce::Decibels::decibelsToGain (
        getParamValue (ShredEQ::ParameterIDs::outputTrim)));
    for (int sample = 0; sample < numSamples; ++sample)
    {
        const auto gain = outputGain.getNextValue() * outputTrim.getNextValue();
        for (int channel = 0; channel < numChannels; ++channel)
            channels[channel][sample] *= gain;
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
