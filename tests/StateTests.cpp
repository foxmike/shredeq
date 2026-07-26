// =============================================================================
// ShredEQ - State / A/B / Gain-Match / Profiles / Factory-Preset Unit Tests
// =============================================================================
// Covers SHREDEQ_SPEC.md §14 (gain matching & comparison), §15 (profiles),
// §16 (factory presets) and §21.8 (state save/restore), as scoped by phase K
// task k.5.
//
// What is exercised here:
//   1. StateManager A/B: capture/apply/switch, copyAtoB, resetSlot (empty and
//      default-seeded), and full XML round-trip of both slots + active index
//      with schema_version handling.
//   2. schema_version: current version accepted, future versions refused.
//   3. Gain-matched bypass meter: directional G_match for a known dry/wet
//      relationship, freeze stops further updates, G_match stays bounded.
//   4. Profiles: §15.1 placement coverage (>=7 incl. Pre-Amp/Rhythm/Lead/Bass)
//      and §15.2 factory tunings (E Standard, a Drop, a bass); index-stable
//      access; custom tuning setters keep finite pitches.
//   5. Factory presets: the five phase-K Resources/Presets/*.json files exist,
//      parse without throwing, carry name/category metadata, and keep makeup
//      gains within a sane (±12 dB) window.
//   6. Delta audition: documented below — a unified setDeltaAuditionMode() API
//      is not exposed by k.4 on the processor (only per-module listen/audition
//      flags exist). We assert the testable subset: the processor constructs
//      headless without crashing and all audition-style flags default to Off.
//
// Build wiring note: this translation unit references StateManager and
// ShredEQ::Profiles, whose implementations (Source/StateManager.cpp and
// Source/Profiles.cpp) must be compiled into the unit-test target alongside
// tests/StateTests.cpp. k.5's file scope forbids editing CMakeLists.txt; the
// CMake entry for this file and those two implementation files is expected to
// be provided by the phase-K build wiring. The test itself only calls public
// APIs of StateManager / Profiles / ShredEQAudioProcessor.
// =============================================================================

#include <juce_core/juce_core.h>
#include <juce_audio_processors/juce_audio_processors.h>

#include "Source/ParameterIDs.h"
#include "Source/StateManager.h"
#include "Source/Profiles.h"
#include "Source/PluginProcessor.h"

#include <array>
#include <cmath>
#include <memory>
#include <string_view>

namespace
{

// ---------------------------------------------------------------------------
// Small APVTS helpers (message-thread; no host required).
// ---------------------------------------------------------------------------

void setFloatParam (juce::AudioProcessorValueTreeState& apvts,
                    const char* id, float value)
{
    if (auto* p = dynamic_cast<juce::RangedAudioParameter*> (apvts.getParameter (id)))
        p->setValueNotifyingHost (p->getNormalisableRange().convertTo0to1 (value));
}

float getFloatParam (juce::AudioProcessorValueTreeState& apvts, const char* id)
{
    if (auto* v = apvts.getRawParameterValue (id))
        return v->load();
    return 0.0f;
}

void setBoolParam (juce::AudioProcessorValueTreeState& apvts,
                   const char* id, bool value)
{
    if (auto* p = apvts.getParameter (id))
        p->setValueNotifyingHost (value ? 1.0f : 0.0f);
}

bool getBoolParam (juce::AudioProcessorValueTreeState& apvts, const char* id)
{
    if (auto* v = apvts.getRawParameterValue (id))
        return v->load() > 0.5f;
    return false;
}

// ---------------------------------------------------------------------------
// Factory-preset directory discovery. Deterministic and host-independent:
// we look relative to this source file first, then walk up from the test
// executable, then ask StateManager.
// ---------------------------------------------------------------------------

juce::File findFactoryPresetsDir()
{
    juce::Array<juce::File> candidates;

    // Source-relative: .../tests/StateTests.cpp -> repo root -> Resources/Presets
    candidates.add (juce::File (__FILE__)
                        .getParentDirectory()
                        .getParentDirectory()
                        .getChildFile ("Resources")
                        .getChildFile ("Presets"));

    // Walk up from the test executable (handles in-tree build layouts).
    auto exe = juce::File::getSpecialLocation (juce::File::currentExecutableFile);
    auto dir = exe.getParentDirectory();
    for (int i = 0; i < 6; ++i)
    {
        candidates.add (dir.getChildFile ("Resources").getChildFile ("Presets"));
        dir = dir.getParentDirectory();
    }

    // StateManager's own lookup as a last resort.
    StateManager sm;
    candidates.add (sm.getFactoryPresetsDirectory());

    for (const auto& c : candidates)
        if (c.isDirectory() && c.getChildFile ("PreAmp_TightSixString.json").existsAsFile())
            return c;

    return {};
}

// The five factory-preset files mandated by phase K (§16 categories).
static const char* const kFactoryPresetFiles[] = {
    "PreAmp_TightSixString.json",
    "Rhythm_ModernCleanup.json",
    "Lead_SingingFocus.json",
    "Bass_ClangControl.json",
    "Diag_FindResonances.json",
};

} // namespace

// =============================================================================
// StateManager A/B + schema + gain-match
// =============================================================================

class StateManagerABTests final : public juce::UnitTest
{
public:
    StateManagerABTests() : juce::UnitTest ("StateManagerABTests", "ShredEQ/State") {}

    void runTest() override
    {
        testCaptureApplySwitch();
        testCopyAtoB();
        testResetSlotEmptyAndDefault();
        testXmlRoundTripBothSlotsAndActiveIndex();
        testSchemaVersionAcceptCurrent();
        testSchemaVersionRefuseFuture();
    }

    void testCaptureApplySwitch()
    {
        beginTest ("A/B capture/apply/switch applies the correct slot");

        ShredEQAudioProcessor processor;
        auto& apvts = processor.getAPVTS();
        StateManager sm;

        // Distinct snapshots: A has outputGain = +6 dB, B has -6 dB.
        setFloatParam (apvts, ShredEQ::ParameterIDs::outputGain, 6.0f);
        sm.captureSlot (StateManager::Slot::A, apvts);

        setFloatParam (apvts, ShredEQ::ParameterIDs::outputGain, -6.0f);
        sm.captureSlot (StateManager::Slot::B, apvts);

        // Switch to B applies B's snapshot.
        expect (sm.switchActiveSlot (StateManager::Slot::B, apvts, false),
                "switchActiveSlot(B) should apply");
        expectEquals (getFloatParam (apvts, ShredEQ::ParameterIDs::outputGain), -6.0f,
                      "After switch to B, outputGain should be -6 dB");
        expect (sm.getActiveSlot() == StateManager::Slot::B, "Active slot should be B");

        // Switch to A applies A's snapshot.
        expect (sm.switchActiveSlot (StateManager::Slot::A, apvts, false),
                "switchActiveSlot(A) should apply");
        expectEquals (getFloatParam (apvts, ShredEQ::ParameterIDs::outputGain), 6.0f,
                      "After switch to A, outputGain should be +6 dB");
        expect (sm.getActiveSlot() == StateManager::Slot::A, "Active slot should be A");
    }

    void testCopyAtoB()
    {
        beginTest ("copyAtoB makes slot B match slot A");

        ShredEQAudioProcessor processor;
        auto& apvts = processor.getAPVTS();
        StateManager sm;

        setFloatParam (apvts, ShredEQ::ParameterIDs::outputGain, 4.5f);
        setFloatParam (apvts, ShredEQ::ParameterIDs::inputGain, -3.0f);
        sm.captureSlot (StateManager::Slot::A, apvts);

        setFloatParam (apvts, ShredEQ::ParameterIDs::outputGain, 0.0f);
        setFloatParam (apvts, ShredEQ::ParameterIDs::inputGain, 0.0f);
        sm.captureSlot (StateManager::Slot::B, apvts);

        sm.copyAtoB();

        // Applying B should now reproduce A's values.
        expect (sm.applySlot (StateManager::Slot::B, apvts),
                "applySlot(B) after copyAtoB should succeed");
        expectEquals (getFloatParam (apvts, ShredEQ::ParameterIDs::outputGain), 4.5f,
                      "B should mirror A's outputGain after copyAtoB");
        expectEquals (getFloatParam (apvts, ShredEQ::ParameterIDs::inputGain), -3.0f,
                      "B should mirror A's inputGain after copyAtoB");

        // copyBtoA symmetry sanity (B currently equals A, so this is a no-op
        // but must not crash and must remain consistent).
        sm.copyBtoA();
        expect (sm.applySlot (StateManager::Slot::A, apvts), "applySlot(A) after copyBtoA");
        expectEquals (getFloatParam (apvts, ShredEQ::ParameterIDs::outputGain), 4.5f,
                      "A should remain at 4.5 dB after symmetric copyBtoA");
    }

    void testResetSlotEmptyAndDefault()
    {
        beginTest ("resetSlot clears/defaults a slot without crash");

        ShredEQAudioProcessor processor;
        auto& apvts = processor.getAPVTS();
        StateManager sm;

        // Capture something into A, then clear it.
        setFloatParam (apvts, ShredEQ::ParameterIDs::outputGain, 9.0f);
        sm.captureSlot (StateManager::Slot::A, apvts);
        sm.resetSlot (StateManager::Slot::A);

        // Applying an empty slot is a safe no-op (returns false).
        expect (! sm.applySlot (StateManager::Slot::A, apvts),
                "applySlot on an empty slot should return false (no-op)");

        // The current APVTS state must be untouched (still 9 dB from capture;
        // applySlot did not replace anything).
        expectEquals (getFloatParam (apvts, ShredEQ::ParameterIDs::outputGain), 9.0f,
                      "Empty applySlot must not mutate APVTS");

        // resetSlot(slot, apvts) seeds defaults and must apply cleanly.
        sm.resetSlot (StateManager::Slot::B, apvts);
        expect (sm.applySlot (StateManager::Slot::B, apvts),
                "applySlot on a default-seeded slot should succeed");
        expectEquals (getFloatParam (apvts, ShredEQ::ParameterIDs::outputGain), 0.0f,
                      "Default-seeded slot should restore outputGain to 0 dB");
    }

    void testXmlRoundTripBothSlotsAndActiveIndex()
    {
        beginTest ("XML round-trip restores both slots and the active index");

        ShredEQAudioProcessor proc1;
        auto& apvts1 = proc1.getAPVTS();
        StateManager sm1;

        // A = +6 dB, B = -6 dB, active = B.
        setFloatParam (apvts1, ShredEQ::ParameterIDs::outputGain, 6.0f);
        sm1.captureSlot (StateManager::Slot::A, apvts1);
        setFloatParam (apvts1, ShredEQ::ParameterIDs::outputGain, -6.0f);
        sm1.captureSlot (StateManager::Slot::B, apvts1);
        sm1.switchActiveSlot (StateManager::Slot::B, apvts1, false);

        auto xml = sm1.createComparisonXml();
        expect (xml != nullptr, "createComparisonXml should produce XML");
        expect (xml->hasTagName ("ShredEQComparisonState"),
                "Comparison XML root tag should be ShredEQComparisonState");

        // Restore into a fresh processor.
        ShredEQAudioProcessor proc2;
        auto& apvts2 = proc2.getAPVTS();
        StateManager sm2;

        expect (sm2.restoreComparisonXml (*xml, apvts2),
                "restoreComparisonXml should accept the round-tripped state");
        expect (sm2.getActiveSlot() == StateManager::Slot::B,
                "Restored active slot should be B");
        expectEquals (getFloatParam (apvts2, ShredEQ::ParameterIDs::outputGain), -6.0f,
                      "Restored active B should apply -6 dB");

        // The non-active slot must also be present and switchable.
        sm2.switchActiveSlot (StateManager::Slot::A);
        expect (sm2.applySlot (apvts2), "applySlot(A) after restore should succeed");
        expectEquals (getFloatParam (apvts2, ShredEQ::ParameterIDs::outputGain), 6.0f,
                      "Restored slot A should apply +6 dB");

        sm2.switchActiveSlot (StateManager::Slot::B);
        expect (sm2.applySlot (apvts2), "applySlot(B) after restore should succeed");
        expectEquals (getFloatParam (apvts2, ShredEQ::ParameterIDs::outputGain), -6.0f,
                      "Restored slot B should re-apply -6 dB");
    }

    void testSchemaVersionAcceptCurrent()
    {
        beginTest ("schema_version: current version accepted");

        ShredEQAudioProcessor proc;
        auto& apvts = proc.getAPVTS();
        StateManager sm;

        setFloatParam (apvts, ShredEQ::ParameterIDs::outputGain, 2.0f);
        sm.captureSlot (StateManager::Slot::A, apvts);

        auto xml = sm.createComparisonXml();
        expect (xml != nullptr, "createComparisonXml should produce XML");

        // The embedded schema_version should match the current code's version.
        const auto rootVersion = xml->getIntAttribute ("schema_version", -1);
        expectEquals (rootVersion, StateManager::schema_version,
                      "Comparison XML should carry current schema_version");

        ShredEQAudioProcessor proc2;
        auto& apvts2 = proc2.getAPVTS();
        StateManager sm2;
        expect (sm2.restoreComparisonXml (*xml, apvts2),
                "restoreComparisonXml should accept current-version state");

        // Also exercise loadFromXml with a current-version single-APVTS preset.
        juce::XmlElement preset ("Preset");
        auto* state = preset.createNewChildElement ("ShredEQState");
        state->setAttribute ("schema_version", StateManager::schema_version);
        state->setAttribute (ShredEQ::ParameterIDs::outputGain, "1.5");

        ShredEQAudioProcessor proc3;
        auto& apvts3 = proc3.getAPVTS();
        StateManager sm3;
        expect (sm3.loadFromXml (preset, apvts3),
                "loadFromXml should accept current schema_version");
    }

    void testSchemaVersionRefuseFuture()
    {
        beginTest ("schema_version: future unsupported version refused");

        // Comparison state with a too-new schema_version must be refused.
        juce::ValueTree root ("ShredEQComparisonState");
        root.setProperty ("schema_version", StateManager::schema_version + 99, nullptr);
        root.setProperty ("active_slot", 0, nullptr);
        root.addChild (juce::ValueTree ("SlotA"), -1, nullptr);
        root.addChild (juce::ValueTree ("SlotB"), -1, nullptr);

        ShredEQAudioProcessor proc;
        auto& apvts = proc.getAPVTS();
        StateManager sm;
        expect (! sm.restoreComparisonState (root, apvts),
                "restoreComparisonState should refuse a future schema_version");

        // XML form of the same.
        auto xml = root.createXml();
        expect (xml != nullptr);
        expect (! sm.restoreComparisonXml (*xml, apvts),
                "restoreComparisonXml should refuse a future schema_version");

        // loadFromXml with a too-new single-APVTS preset must also be refused.
        juce::XmlElement preset ("Preset");
        auto* state = preset.createNewChildElement ("ShredEQState");
        state->setAttribute ("schema_version", StateManager::schema_version + 99);
        expect (! sm.loadFromXml (preset, apvts),
                "loadFromXml should refuse a future schema_version");
    }
};

// =============================================================================
// StateManager gain-matched bypass meter
// =============================================================================

class GainMatchMeterTests final : public juce::UnitTest
{
public:
    GainMatchMeterTests() : juce::UnitTest ("GainMatchMeterTests", "ShredEQ/State") {}

    void runTest() override
    {
        testDirectionalGMatchWetQuieter();
        testDirectionalGMatchWetLouder();
        testFreezeStopsUpdates();
        testBoundedTrim();
    }

    void feed (StateManager& sm, float dryRms, float wetRms, int blocks, int samplesPerBlock)
    {
        for (int i = 0; i < blocks; ++i)
            sm.processMatchLevels (dryRms, wetRms, samplesPerBlock);
    }

    void testDirectionalGMatchWetQuieter()
    {
        beginTest ("G_match moves positive when wet is quieter than dry");

        StateManager sm;
        sm.prepare (48000.0);
        sm.setGainMatchEnabled (true);
        sm.freezeGMatch (false);

        // dry = 1.0 (0 dB), wet = 0.5 (~ -6.02 dB). G_match = -(wet-dry)dB =
        // dryDb - wetDb = +6.02 dB (boost wet to match dry).
        feed (sm, 1.0f, 0.5f, 20, 48000);

        const auto g = sm.getMatchGainDb();
        expect (g > 0.0f, "G_match should be positive when wet is quieter than dry");
        expectWithinAbsoluteError (g, 6.0206f, 0.25f,
                                    juce::String ("G_match should approach +6 dB; got ")
                                        + juce::String (g, 4));
    }

    void testDirectionalGMatchWetLouder()
    {
        beginTest ("G_match moves negative when wet is louder than dry");

        StateManager sm;
        sm.prepare (48000.0);
        sm.setGainMatchEnabled (true);
        sm.freezeGMatch (false);

        // dry = 0.5 (-6 dB), wet = 1.0 (0 dB) -> G_match = -6 dB.
        feed (sm, 0.5f, 1.0f, 20, 48000);

        const auto g = sm.getMatchGainDb();
        expect (g < 0.0f, "G_match should be negative when wet is louder than dry");
        expectWithinAbsoluteError (g, -6.0206f, 0.25f,
                                    juce::String ("G_match should approach -6 dB; got ")
                                        + juce::String (g, 4));
    }

    void testFreezeStopsUpdates()
    {
        beginTest ("Freeze stops further G_match updates");

        StateManager sm;
        sm.prepare (48000.0);
        sm.setGainMatchEnabled (true);
        sm.freezeGMatch (false);

        // Establish +6 dB trim.
        feed (sm, 1.0f, 0.5f, 20, 48000);
        const auto beforeFreeze = sm.getMatchGainDb();
        expectWithinAbsoluteError (beforeFreeze, 6.0206f, 0.25f);

        // Freeze, then feed a relationship that would otherwise drive G_match
        // back toward 0 dB (wet == dry). It must not move.
        sm.freezeGMatch (true);
        feed (sm, 1.0f, 1.0f, 20, 48000);
        const auto frozen = sm.getMatchGainDb();
        expectWithinAbsoluteError (frozen, beforeFreeze, 1.0e-3f,
                                    "Frozen G_match must not change while frozen");
        expect (sm.isFrozen(), "isFrozen() should report true after freeze(true)");

        // Unfreezing resumes updates toward the new target.
        sm.freezeGMatch (false);
        feed (sm, 1.0f, 1.0f, 20, 48000);
        const auto thawed = sm.getMatchGainDb();
        expect (std::abs (thawed) < std::abs (beforeFreeze),
                "After unfreeze, G_match should move back toward 0 dB");
        expect (! sm.isFrozen(), "isFrozen() should report false after freeze(false)");
    }

    void testBoundedTrim()
    {
        beginTest ("G_match remains bounded by the maximum trim");

        StateManager sm;
        sm.prepare (48000.0);
        sm.setGainMatchEnabled (true);
        sm.freezeGMatch (false);

        // Wet is ~60 dB quieter than dry: target would be +60 dB but must clamp.
        // wetDb = 20*log10(0.001) = -60 dB, which is above the -80 dB silence
        // floor, so updateMatchGain actually engages.
        feed (sm, 1.0f, 0.001f, 30, 48000);
        const auto gPos = sm.getMatchGainDb();
        expect (gPos <= 24.0f + 1.0e-3f,
                juce::String ("Positive G_match must be bounded <= +24 dB; got ")
                    + juce::String (gPos, 4));

        // Wet ~60 dB louder: target -60 dB, must clamp to -24 dB.
        sm.reset();
        sm.setGainMatchEnabled (true);
        feed (sm, 0.001f, 1.0f, 30, 48000);
        const auto gNeg = sm.getMatchGainDb();
        expect (gNeg >= -24.0f - 1.0e-3f,
                juce::String ("Negative G_match must be bounded >= -24 dB; got ")
                    + juce::String (gNeg, 4));
    }
};

// =============================================================================
// Profiles catalogs (§15.1 placement, §15.2 tuning, custom tuning)
// =============================================================================

class ProfilesCatalogTests final : public juce::UnitTest
{
public:
    ProfilesCatalogTests() : juce::UnitTest ("ProfilesCatalogTests", "ShredEQ/State") {}

    void runTest() override
    {
        testPlacementCoverage();
        testTuningCoverage();
        testIndexStableAccess();
        testCustomTuningFinitePitches();
    }

    void testPlacementCoverage()
    {
        beginTest ("Placement profiles cover §15.1 (>=7 incl. Pre-Amp/Rhythm/Lead/Bass)");

        using namespace ShredEQ::Profiles;

        expectEquals (ProfileCatalog::getNumPlacementProfiles(), 7,
                      "Factory placement profile count should be >= 7 (stable)");

        std::array<const PlacementProfile*, 16> out {};
        const int n = ProfileCatalog::listPlacementProfiles (out.data(),
                                                              static_cast<int> (out.size()));
        expectEquals (n, 7, "listPlacementProfiles should return all 7 entries");

        // Collect display names.
        juce::StringArray names;
        for (int i = 0; i < n; ++i)
        {
            const auto* entry = out[static_cast<std::size_t> (i)];
            expect (entry != nullptr, "Placement pointer must not be null");
            names.add (entry->displayName);
        }

        // §15.1 categories: Pre-Amp, Rhythm, Lead, Bass must each appear.
        expect (containsSubstring (names, "Pre-Amp"),
                "Placement names must include a Pre-Amp entry");
        expect (containsSubstring (names, "Rhythm"),
                "Placement names must include a Rhythm entry");
        expect (containsSubstring (names, "Lead"),
                "Placement names must include a Lead entry");
        expect (containsSubstring (names, "Bass"),
                "Placement names must include a Bass entry");
    }

    void testTuningCoverage()
    {
        beginTest ("Tuning profiles cover §15.2 (E Standard + a Drop + a bass tuning)");

        using namespace ShredEQ::Profiles;

        expectEquals (ProfileCatalog::getNumTuningProfiles(), 18,
                      "Factory tuning profile count should be 18 (incl. Custom)");

        std::array<const TuningProfile*, 32> out {};
        const int n = ProfileCatalog::listTuningProfiles (out.data(),
                                                           static_cast<int> (out.size()));
        expectEquals (n, 18, "listTuningProfiles should return all 18 entries");

        juce::StringArray names;
        for (int i = 0; i < n; ++i)
        {
            const auto* entry = out[static_cast<std::size_t> (i)];
            expect (entry != nullptr, "Tuning pointer must not be null");
            names.add (entry->name);
        }

        expect (names.contains ("E Standard"),
                "Factory tunings must include E Standard");
        expect (containsSubstring (names, "Drop"),
                "Factory tunings must include at least one Drop tuning");
        expect (containsSubstring (names, "Bass"),
                "Factory tunings must include at least one bass tuning");
    }

    void testIndexStableAccess()
    {
        beginTest ("Profile catalog index access is stable and bounds-safe");

        using namespace ShredEQ::Profiles;

        // Index 0 placement == default placement profile (Guitar DI / Pre-Amp).
        const auto* p0 = ProfileCatalog::getPlacement (0);
        expect (p0 != nullptr, "getPlacement(0) must not be null");
        expect (juce::String (p0->displayName) == "Guitar DI / Pre-Amp",
                "Index 0 placement should be the default (Guitar DI / Pre-Amp)");
        expect (ProfileCatalog::getPlacement (0) == ProfileCatalog::getPlacement (PlacementId::GuitarDIPreAmp),
                "Enum and int access must agree");

        // Out-of-range returns nullptr (no UB).
        expect (ProfileCatalog::getPlacement (-1) == nullptr, "getPlacement(-1) must be null");
        expect (ProfileCatalog::getPlacement (99) == nullptr, "getPlacement(99) must be null");

        // Index 0 tuning == E Standard.
        const auto* t0 = ProfileCatalog::getTuning (0);
        expect (t0 != nullptr, "getTuning(0) must not be null");
        expect (juce::String (t0->name) == "E Standard",
                "Index 0 tuning should be E Standard");
        expect (ProfileCatalog::getTuning (-1) == nullptr, "getTuning(-1) must be null");
        expect (ProfileCatalog::getTuning (99) == nullptr, "getTuning(99) must be null");

        // Re-fetching the same index returns a stable pointer.
        expect (ProfileCatalog::getPlacement (2) == ProfileCatalog::getPlacement (2),
                "Repeated getPlacement must return a stable pointer");
        expect (ProfileCatalog::getTuning (6) == ProfileCatalog::getTuning (6),
                "Repeated getTuning must return a stable pointer");
    }

    void testCustomTuningFinitePitches()
    {
        beginTest ("Custom tuning setters keep finite, sane pitches");

        using namespace ShredEQ::Profiles;

        CustomTuning ct;
        ct.setName ("Test Custom");
        ct.setStringCount (7);
        // 7-string standard-ish open notes (MIDI), all within 0..127.
        const int notes[7] = { 35, 40, 45, 50, 54, 59, 64 };
        for (int i = 0; i < 7; ++i)
            ct.setOpenStringMidi (i, notes[i]);
        ct.setFretRange (0, 24);
        ct.setConcertPitchHz (432.0f);
        ct.setCapoSemitones (2);
        ct.setTranspositionSemitones (-1);

        const auto view = ct.toTuningProfile();
        expectEquals (view.stringCount, 7, "Custom string count should round-trip");
        expect (std::isfinite (static_cast<double> (view.concertPitchHz))
                    && view.concertPitchHz > 0.0f,
                "Custom concert pitch must be finite and positive");
        expectWithinAbsoluteError (view.concertPitchHz, 432.0f, 1.0e-3f,
                                    "Concert pitch should round-trip 432 Hz");

        for (int i = 0; i < 7; ++i)
        {
            const int m = view.openStringMidi[static_cast<size_t> (i)];
            expect (m >= 0 && m <= 127,
                    juce::String ("Custom open-string MIDI ") + juce::String (i)
                        + " must be finite and in 0..127; got " + juce::String (m));
            expectEquals (m, notes[i], "Open-string MIDI should round-trip");
        }

        // Out-of-range setters are ignored (no UB, no infinite pitches).
        ct.setOpenStringMidi (-1, 60);
        ct.setOpenStringMidi (99, 60);
        ct.setConcertPitchHz (-1.0f);  // rejected -> falls back to 440
        expectWithinAbsoluteError (ct.getConcertPitchHz(), 440.0f, 1.0e-3f,
                                    "Invalid concert pitch must fall back to 440 Hz, not infinity");

        const auto view2 = ct.toTuningProfile();
        for (std::size_t i = 0; i < view2.openStringMidi.size(); ++i)
        {
            const int m = view2.openStringMidi[i];
            expect (m == -1 || (m >= 0 && m <= 127),
                    "Custom open-string slots must be -1 or a finite MIDI note");
        }
    }

    static bool containsSubstring (const juce::StringArray& haystack, std::string_view needle)
    {
        const juce::String needleStr { std::string (needle) };
        for (const auto& s : haystack)
            if (s.contains (needleStr))
                return true;
        return false;
    }
};

// =============================================================================
// Factory presets (§16) — five phase-K JSON files
// =============================================================================

class FactoryPresetTests final : public juce::UnitTest
{
public:
    FactoryPresetTests() : juce::UnitTest ("FactoryPresetTests", "ShredEQ/State") {}

    void runTest() override
    {
        beginTest ("Factory preset files exist and parse with metadata + sane makeup");

        const auto presetsDir = findFactoryPresetsDir();
        expect (presetsDir.isDirectory(),
                "Could not locate Resources/Presets directory containing the five phase-K files");

        if (! presetsDir.isDirectory())
        {
            logMessage ("  [skipped] Resources/Presets not resolvable in this build layout; "
                        "ensure the directory is reachable from the test executable.");
            return;
        }

        for (const char* fileName : kFactoryPresetFiles)
        {
            const auto file = presetsDir.getChildFile (fileName);
            expect (file.existsAsFile(),
                    juce::String ("Factory preset file should exist: ") + fileName);

            if (! file.existsAsFile())
                continue;

            // Parse must not throw / must produce a valid object.
            const auto parsed = juce::JSON::parse (file);
            expect (parsed.isObject(),
                    juce::String ("Factory preset should parse to a JSON object: ") + fileName);

            if (! parsed.isObject())
                continue;

            const auto* obj = parsed.getDynamicObject();
            expect (obj != nullptr, "Parsed JSON should expose a DynamicObject");

            // schema_version must be present and current.
            const auto schema = obj->getProperty ("schema_version");
            expect (schema.isInt() || schema.isInt64(),
                    juce::String ("schema_version should be an int in ") + fileName);
            expectEquals (static_cast<int> (schema), 1,
                          juce::String ("schema_version should == 1 in ") + fileName);

            // name + category metadata present and non-empty.
            const auto name = obj->getProperty ("name").toString();
            const auto category = obj->getProperty ("category").toString();
            expect (name.isNotEmpty(),
                    juce::String ("Factory preset must carry a non-empty name: ") + fileName);
            expect (category.isNotEmpty(),
                    juce::String ("Factory preset must carry a non-empty category: ") + fileName);

            // parameters block with makeup gains in a sane window.
            const auto paramsVar = obj->getProperty ("parameters");
            expect (paramsVar.isObject(),
                    juce::String ("Factory preset must carry a parameters object: ") + fileName);
            if (auto* params = paramsVar.getDynamicObject())
            {
                const auto outputGain = params->getProperty (ShredEQ::ParameterIDs::outputGain);
                const auto outputTrim = params->getProperty (ShredEQ::ParameterIDs::outputTrim);
                const auto inputGain = params->getProperty (ShredEQ::ParameterIDs::inputGain);

                expectMakeup (fileName, "outputGain", outputGain);
                expectMakeup (fileName, "outputTrim", outputTrim);
                expectMakeup (fileName, "inputGain", inputGain);

                // Per-band gains stay within the parametric ±24 dB ceiling.
                for (std::size_t b = 0; b < ShredEQ::ParameterIDs::bands.size(); ++b)
                {
                    const auto gainVar = params->getProperty (ShredEQ::ParameterIDs::bands[b].gain);
                    if (! gainVar.isVoid())
                        expectGain (fileName, gainVar, 24.0f);
                }
            }

            // placement/tuning index metadata present and in-range when carried.
            const auto profileIndex = obj->getProperty ("profileIndex");
            if (! profileIndex.isVoid())
            {
                expect (static_cast<int> (profileIndex) >= 0
                            && static_cast<int> (profileIndex) < ShredEQ::Profiles::ProfileCatalog::getNumPlacementProfiles(),
                        juce::String ("profileIndex in range for ") + fileName);
            }
            const auto tuningIndex = obj->getProperty ("tuningIndex");
            if (! tuningIndex.isVoid())
            {
                expect (static_cast<int> (tuningIndex) >= 0
                            && static_cast<int> (tuningIndex) < ShredEQ::Profiles::ProfileCatalog::getNumTuningProfiles(),
                        juce::String ("tuningIndex in range for ") + fileName);
            }
        }
    }

    void expectMakeup (const char* fileName, const char* paramName, const juce::var& value)
    {
        if (value.isVoid())
            return; // optional; some presets may omit a field
        expect (value.isDouble() || value.isInt() || value.isInt64(),
                juce::String ("Makeup param should be numeric: ") + paramName + " in " + fileName);
        const auto dB = static_cast<float> (value);
        expect (std::isfinite (static_cast<double> (dB)),
                juce::String ("Makeup param must be finite: ") + paramName + " in " + fileName);
        expect (std::abs (dB) <= 12.0f,
                juce::String ("Makeup ") + paramName + " in " + fileName
                    + " should not exceed ±12 dB; got " + juce::String (dB, 4));
    }

    void expectGain (const char* fileName, const juce::var& value, float ceiling)
    {
        if (value.isVoid())
            return;
        const auto dB = static_cast<float> (value);
        expect (std::isfinite (static_cast<double> (dB)),
                juce::String ("Band gain must be finite in ") + fileName);
        expect (std::abs (dB) <= ceiling,
                juce::String ("Band gain in ") + fileName + " should stay within ±"
                    + juce::String (ceiling, 1) + " dB; got " + juce::String (dB, 4));
    }
};

// =============================================================================
// Delta audition — testable subset (§14.3, §21.8)
// =============================================================================
// A unified setDeltaAuditionMode() API is not exposed by phase-K's processor.
// §14.3 describes audition as a set of per-module listen/removed-audition flags
// (Tight Cut listen-removed, Resonance Suppressor removed-audition, etc.),
// and k.4 did not aggregate them behind a single Offline mode enum on
// ShredEQAudioProcessor. Per task instructions ("call only public testable
// APIs", "Skip heavy audio graph asserts if headless construct is incomplete —
// document"), we assert the testable subset:
//   * The processor constructs headless without crashing.
//   * Every audition-style flag defaults to Off (safe default).
// Mode cycling through a single API is therefore not asserted here; it is
// documented as a remaining integration gap rather than forced green by
// rewriting production code.
// =============================================================================

class DeltaAuditionTests final : public juce::UnitTest
{
public:
    DeltaAuditionTests() : juce::UnitTest ("DeltaAuditionTests", "ShredEQ/State") {}

    void runTest() override
    {
        beginTest ("Processor constructs headless; audition flags default Off");

        ShredEQAudioProcessor processor;
        auto& apvts = processor.getAPVTS();

        // Construction succeeded and the processor reports a sane identity.
        expect (processor.getName().isNotEmpty(),
                "Processor should construct headless and report a name");

        // "Off is safe default": every audition-style flag must start disabled.
        expect (! getBoolParam (apvts, ShredEQ::ParameterIDs::TightCut::listenRemoved),
                "Tight Cut listen-removed must default to Off");
        expect (! getBoolParam (apvts, ShredEQ::ParameterIDs::globalBypass),
                "Global bypass must default to Off (no audition/removed path by default)");
        expect (! getBoolParam (apvts, ShredEQ::ParameterIDs::TightCut::enabled),
                "Tight Cut enabled defaults Off (audition path inactive)");

        // Resonance slots: removed-audition is an internal runtime flag (not a
        // host parameter), and defaults to false. We verify the host-facing
        // slot freeze/enabled flags default to Off so no audition/removed path
        // is active at construction.
        for (std::size_t i = 0; i < ShredEQ::ParameterIDs::resonanceSlots.size(); ++i)
        {
            const auto& slot = ShredEQ::ParameterIDs::resonanceSlots[i];
            expect (! getBoolParam (apvts, slot.enabled),
                    juce::String ("Resonance slot ") + juce::String (static_cast<int> (i + 1))
                        + " enabled must default Off");
            expect (! getBoolParam (apvts, slot.freeze),
                    juce::String ("Resonance slot ") + juce::String (static_cast<int> (i + 1))
                        + " freeze must default Off");
        }

        logMessage ("  [note] setDeltaAuditionMode() is not exposed by k.4; per-module "
                    "audition flags are exercised instead (§14.3). Mode-cycling through a "
                    "single unified API is documented as a remaining integration gap.");

        // Cycling the available per-module listen flag must not crash: flip
        // Tight Cut listen-removed on and back off through the public APVTS
        // surface. This is the largest "mode switch" the public API exposes
        // without a full host/audio graph.
        expectDoesNotThrow (setBoolParam (apvts, ShredEQ::ParameterIDs::TightCut::listenRemoved, true));
        expect (getBoolParam (apvts, ShredEQ::ParameterIDs::TightCut::listenRemoved),
                "Tight Cut listen-removed should accept On");
        expectDoesNotThrow (setBoolParam (apvts, ShredEQ::ParameterIDs::TightCut::listenRemoved, false));
        expect (! getBoolParam (apvts, ShredEQ::ParameterIDs::TightCut::listenRemoved),
                "Tight Cut listen-removed should accept Off again");
    }
};

// =============================================================================
// Static registration — discovered by juce::UnitTestRunner::runAllTests()
// =============================================================================
static StateManagerABTests     stateManagerABTestsInstance;
static GainMatchMeterTests     gainMatchMeterTestsInstance;
static ProfilesCatalogTests    profilesCatalogTestsInstance;
static FactoryPresetTests       factoryPresetTestsInstance;
static DeltaAuditionTests       deltaAuditionTestsInstance;
