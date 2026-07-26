#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>
#include <memory>
#include <vector>

/**
    StateManager owns schema-versioned A/B snapshots and the temporary
    gain-matched comparison trim.

    ValueTree copying, XML conversion, parameter replacement, and preset I/O
    are message-thread operations and are not real-time safe. The gain-meter
    process methods and crossfade coefficient methods are allocation-free after
    prepare() and may be called by the audio processor.
*/
class StateManager
{
public:
    static constexpr int schema_version = 1;

    enum class Slot
    {
        A = 0,
        B = 1
    };

    StateManager() = default;
    ~StateManager() = default;

    //==============================================================================
    // A/B Slot Operations (message thread; RT-unsafe)
    //==============================================================================

    /** Captures a complete APVTS snapshot in the selected slot. */
    void captureSlot (Slot slot, juce::AudioProcessorValueTreeState& apvts);

    /** Captures a complete APVTS snapshot in the currently active slot. */
    void captureSlot (juce::AudioProcessorValueTreeState& apvts);

    /** Applies the selected slot. Returns false when that slot is empty or invalid. */
    bool applySlot (Slot slot, juce::AudioProcessorValueTreeState& apvts) const;

    /** Applies the currently active slot. */
    bool applySlot (juce::AudioProcessorValueTreeState& apvts) const;

    /** Marks a slot active. Applying/mixing the new state remains processor-owned. */
    void switchActiveSlot (Slot target) noexcept;

    /**
        Captures the outgoing state, marks target active, and applies its snapshot.
        The processor should call beginCrossfade() before mixing old/new audio.
    */
    bool switchActiveSlot (Slot target,
                           juce::AudioProcessorValueTreeState& apvts,
                           bool captureOutgoingState = true);

    Slot getActiveSlot() const noexcept { return activeSlot.load(); }

    void copyAtoB();
    void copyBtoA();

    /** Clears a slot. Applying an empty slot is a safe no-op. */
    void resetSlot (Slot slot);

    /** Replaces a slot with an APVTS snapshot whose parameters are at defaults. */
    void resetSlot (Slot slot, juce::AudioProcessorValueTreeState& apvts);

    /** Legacy phase-A wrappers. */
    void captureFrom (juce::AudioProcessorValueTreeState& apvts);
    void applyTo (juce::AudioProcessorValueTreeState& apvts) const;
    void swapAB();
    void copyAToB() { copyAtoB(); }
    void copyBToA() { copyBtoA(); }

    //==============================================================================
    // Click-safe comparison ramp (audio-thread safe after prepare)
    //==============================================================================

    /**
        Starts an equal-power A/B ramp. Duration is always clamped to 5--30 ms.
        This computes gains only; the processor owns old/new signal rendering.
    */
    void beginCrossfade (double sampleRate, double durationMs = 15.0) noexcept;
    void advanceCrossfade (int numSamples = 1) noexcept;
    float getCrossfadeGainA() const noexcept;
    float getCrossfadeGainB() const noexcept;
    bool isCrossfadeActive() const noexcept;

    //==============================================================================
    // Gain-matched bypass meter
    //==============================================================================

    /**
        Allocates a fixed rolling RMS window. Call on the message thread before
        processMatchLevels(); no meter call allocates after this returns.
    */
    void prepare (double sampleRate);
    void reset() noexcept;

    void setGainMatchEnabled (bool shouldBeEnabled) noexcept;

    /**
        Pushes block RMS values. numSamples provides time weighting, so differently
        sized process blocks contribute the correct amount to the rolling window.
    */
    void processMatchLevels (float dryRms, float wetRms, int numSamples) noexcept;
    void pushProcessLevels (float dryRms, float wetRms, int numSamples) noexcept
    {
        processMatchLevels (dryRms, wetRms, numSamples);
    }

    /** Pushes mono sample streams and computes their RMS without allocation. */
    void processMatchSamples (const float* dry, const float* wet, int numSamples) noexcept;

    /** Freezes/unfreezes the temporary trim during A/B or bypass transitions. */
    void freezeGMatch (bool shouldFreeze) noexcept;

    float getMatchGainDb() const noexcept { return matchGainDb.load(); }
    bool isFrozen() const noexcept { return gainMatchFrozen.load(); }
    bool isEnabled() const noexcept { return gainMatchEnabled.load(); }

    /**
        Explicitly adds the temporary match trim to outputGain. This is the only
        gain-match method that mutates APVTS/preset state and is RT-unsafe.
    */
    bool commitMatchTrim (juce::AudioProcessorValueTreeState& apvts);

    //==============================================================================
    // Complete A/B host state (message thread; RT-unsafe)
    //==============================================================================

    /** Returns an aggregate ValueTree containing both slots and active-slot index. */
    juce::ValueTree createComparisonState() const;

    /** Restores both slots and active index; rejects unsupported future schemas. */
    bool restoreComparisonState (const juce::ValueTree& state,
                                 juce::AudioProcessorValueTreeState& apvts);

    std::unique_ptr<juce::XmlElement> createComparisonXml() const;
    bool restoreComparisonXml (const juce::XmlElement& xml,
                               juce::AudioProcessorValueTreeState& apvts);

    //==============================================================================
    // Preset Operations
    //==============================================================================

    juce::StringArray listFactoryPresets() const;
    bool loadFactoryPreset (const juce::String& name,
                            juce::AudioProcessorValueTreeState& apvts) const;
    juce::File getUserPresetsDirectory() const;
    bool saveUserPreset (const juce::String& name,
                         juce::AudioProcessorValueTreeState& apvts);
    bool loadUserPreset (const juce::String& name,
                         juce::AudioProcessorValueTreeState& apvts);
    juce::StringArray listUserPresets() const;

    /** Loads a conventional single-APVTS preset, with version checks. */
    bool loadFromXml (const juce::XmlElement& xml,
                      juce::AudioProcessorValueTreeState& apvts) const;
    juce::File getFactoryPresetsDirectory() const;

private:
    static juce::ValueTree makeDefaultState (
        juce::AudioProcessorValueTreeState& apvts);
    static void setSnapshotSlotParameter (juce::ValueTree& state, Slot slot);
    const juce::ValueTree& getSlotState (Slot slot) const noexcept;
    juce::ValueTree& getSlotState (Slot slot) noexcept;
    void appendSquaredLevels (double drySquared,
                              double wetSquared,
                              int numSamples) noexcept;
    void updateMatchGain (int numSamples) noexcept;

    juce::ValueTree slotA;
    juce::ValueTree slotB;
    std::atomic<Slot> activeSlot { Slot::A };

    std::vector<double> drySquaredWindow;
    std::vector<double> wetSquaredWindow;
    size_t rollingWriteIndex = 0;
    size_t rollingSamples = 0;
    double rollingDrySum = 0.0;
    double rollingWetSum = 0.0;
    double meterSampleRate = 0.0;
    std::atomic<float> matchGainDb { 0.0f };
    std::atomic<bool> gainMatchFrozen { false };
    std::atomic<bool> gainMatchEnabled { true };

    int crossfadeLengthSamples = 0;
    int crossfadePositionSamples = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StateManager)
};
