#include "Profiles.h"
#include "ParameterIDs.h"

#include <algorithm>
#include <cstring>

namespace ShredEQ::Profiles
{

//==============================================================================
// Factory placement profiles (§15.1)
//
// Order is stable and matches PlacementId. Index is consumed by the
// profileIndex parameter. Do not reorder existing entries.
//==============================================================================

namespace
{

constexpr AnalyzerWeighting makeWeighting (float low, float lowMid, float highMid,
                                           float high, float res, float fizz,
                                           float transient, float mask)
{
    return { low, lowMid, highMid, high, res, fizz, transient, mask };
}

constexpr RangeGuide makeRange (float fMin, float fMax,
                                float gMin, float gMax,
                                float qMin, float qMax)
{
    return { fMin, fMax, gMin, gMax, qMin, qMax };
}

// Default band seeds are intentionally sparse and conservative. They only
// establish a sensible starting layout; everything is user-editable and
// nothing here is continuously enforced.
DefaultSeed makeSeedGuitarDIPreAmp()
{
    DefaultSeed s;
    s.tightCutEnabled   = true;
    s.tightCutFreqHz    = 80.0f;
    s.tightCutEmphasis  = true;
    s.chugEmphasis      = false;
    s.bands[0] = { 0, true, 0, 0, 80.0f,  0.0f, 0.7f, 0, 0 };
    s.bands[1] = { 1, true, 0, 0, 250.0f, 0.0f, 1.0f, 0, 0 };
    s.activeBandCount   = 2;
    return s;
}

DefaultSeed makeSeedPostAmpCab()
{
    DefaultSeed s;
    s.tightCutEnabled   = false;
    s.chugEmphasis      = false;
    s.bands[0] = { 0, true, 1, 0, 4500.0f, -3.0f, 2.0f, 0, 0 };
    s.bands[1] = { 1, true, 1, 0, 480.0f,  -1.5f, 1.5f, 0, 0 };
    s.activeBandCount   = 2;
    return s;
}

DefaultSeed makeSeedRhythmGuitar()
{
    DefaultSeed s;
    s.tightCutEnabled   = true;
    s.tightCutFreqHz    = 90.0f;
    s.chugEnabled       = true;
    s.chugFreqHz        = 120.0f;
    s.chugEmphasis      = true;
    s.bands[0] = { 0, true, 0, 0, 250.0f, -2.0f, 1.0f, 0, 0 };
    s.bands[1] = { 1, true, 1, 0, 4000.0f, -2.0f, 1.5f, 0, 0 };
    s.activeBandCount   = 2;
    return s;
}

DefaultSeed makeSeedLeadGuitar()
{
    DefaultSeed s;
    s.tightCutEnabled    = false;
    s.noteFollowerEnabled = true;
    s.bands[0] = { 0, true, 1, 0, 1500.0f, 1.5f, 1.2f, 0, 0 };
    s.bands[1] = { 1, true, 1, 0, 6500.0f, -2.0f, 1.5f, 0, 0 };
    s.activeBandCount    = 2;
    return s;
}

DefaultSeed makeSeedStereoGuitarBus()
{
    DefaultSeed s;
    s.tightCutEnabled   = false;
    s.multibandEnabled  = false;
    s.midSideMonoGuard  = true;
    s.bands[0] = { 0, true, 1, 0, 200.0f,  0.0f, 0.9f, 4 /*side*/, 0 };
    s.bands[1] = { 1, true, 1, 0, 3000.0f, 0.0f, 1.2f, 4 /*side*/, 0 };
    s.activeBandCount   = 2;
    return s;
}

DefaultSeed makeSeedActiveBass()
{
    DefaultSeed s;
    s.tightCutEnabled   = true;
    s.tightCutFreqHz    = 40.0f;
    s.chugEnabled       = false;
    s.bands[0] = { 0, true, 0, 0, 60.0f,  0.0f, 0.9f, 0, 0 };
    s.bands[1] = { 1, true, 1, 0, 800.0f, -2.0f, 1.5f, 0, 0 };
    s.activeBandCount   = 2;
    return s;
}

DefaultSeed makeSeedDistortedBass()
{
    DefaultSeed s;
    s.tightCutEnabled   = true;
    s.tightCutFreqHz    = 35.0f;
    s.bands[0] = { 0, true, 0, 0, 50.0f,  0.0f, 0.8f, 0, 0 };
    s.bands[1] = { 1, true, 1, 0, 5000.0f, -3.0f, 1.5f, 0, 0 };
    s.activeBandCount   = 2;
    return s;
}

const PlacementProfile& placementAt (int index)
{
    static const std::array<PlacementProfile, kNumPlacementProfiles> table = {{
        // 0 — Guitar DI / Pre-Amp
        { PlacementId::GuitarDIPreAmp,
          "Guitar DI / Pre-Amp", "Pre-Amp",
          makeWeighting (1.0f, 1.2f, 1.3f, 1.0f, 0.7f, 0.7f, 1.0f, 0.5f),
          makeRange (25.0f, 20000.0f, -24.0f, 24.0f, 0.10f, 36.0f),
          makeSeedGuitarDIPreAmp() },
        // 1 — Post Amp / Cab
        { PlacementId::PostAmpCab,
          "Post Amp / Cab", "Post-Cab",
          makeWeighting (0.8f, 1.1f, 1.4f, 1.3f, 1.5f, 1.5f, 0.8f, 0.7f),
          makeRange (15.0f, 20000.0f, -24.0f, 24.0f, 0.10f, 36.0f),
          makeSeedPostAmpCab() },
        // 2 — Rhythm Guitar
        { PlacementId::RhythmGuitar,
          "Rhythm Guitar", "Rhythm",
          makeWeighting (1.4f, 1.3f, 1.2f, 1.0f, 1.0f, 1.0f, 1.5f, 0.9f),
          makeRange (20.0f, 20000.0f, -24.0f, 18.0f, 0.10f, 36.0f),
          makeSeedRhythmGuitar() },
        // 3 — Lead Guitar
        { PlacementId::LeadGuitar,
          "Lead Guitar", "Lead",
          makeWeighting (0.9f, 1.0f, 1.4f, 1.4f, 1.0f, 1.3f, 0.8f, 0.7f),
          makeRange (20.0f, 20000.0f, -24.0f, 24.0f, 0.10f, 36.0f),
          makeSeedLeadGuitar() },
        // 4 — Stereo Guitar Bus
        { PlacementId::StereoGuitarBus,
          "Stereo Guitar Bus", "Bus",
          makeWeighting (1.2f, 1.1f, 1.2f, 1.1f, 1.0f, 1.0f, 0.9f, 1.2f),
          makeRange (15.0f, 20000.0f, -24.0f, 18.0f, 0.10f, 36.0f),
          makeSeedStereoGuitarBus() },
        // 5 — Active Bass
        { PlacementId::ActiveBass,
          "Active Bass", "Bass",
          makeWeighting (1.5f, 1.2f, 1.0f, 0.9f, 0.9f, 0.8f, 1.2f, 1.2f),
          makeRange (20.0f, 16000.0f, -24.0f, 18.0f, 0.10f, 36.0f),
          makeSeedActiveBass() },
        // 6 — Distorted Bass
        { PlacementId::DistortedBass,
          "Distorted Bass", "Bass",
          makeWeighting (1.5f, 1.2f, 1.3f, 1.2f, 1.0f, 1.4f, 1.0f, 1.0f),
          makeRange (20.0f, 16000.0f, -24.0f, 18.0f, 0.10f, 36.0f),
          makeSeedDistortedBass() },
    }};

    // Bounds-safe: caller already validated, but keep a defensive fallback.
    const int i = std::clamp (index, 0, kNumPlacementProfiles - 1);
    return table[static_cast<size_t> (i)];
}

//==============================================================================
// Factory tuning profiles (§15.2)
//
// openStringMidi uses MIDI note numbers. Unused slots are -1.
// Resolved frequency = concertPitchHz * 2^((midi - 69) / 12).
//==============================================================================

constexpr int kCustomTuningIndex = 17; // last factory entry is the Custom slot

const TuningProfile& tuningAt (int index)
{
    static const std::array<TuningProfile, 18> table = {{
        // 0 — E Standard
        { "E Standard", 6, { 40, 45, 50, 55, 59, 64, -1, -1 }, 0, 24, 440.0f, 0, 0 },
        // 1 — Eb Standard
        { "Eb Standard", 6, { 39, 44, 49, 54, 58, 63, -1, -1 }, 0, 24, 440.0f, 0, 0 },
        // 2 — D Standard
        { "D Standard", 6, { 38, 43, 48, 53, 57, 62, -1, -1 }, 0, 24, 440.0f, 0, 0 },
        // 3 — C# Standard
        { "C# Standard", 6, { 37, 42, 47, 52, 56, 61, -1, -1 }, 0, 24, 440.0f, 0, 0 },
        // 4 — C Standard
        { "C Standard", 6, { 36, 41, 46, 51, 55, 60, -1, -1 }, 0, 24, 440.0f, 0, 0 },
        // 5 — B Standard (baritone)
        { "B Standard", 6, { 35, 40, 45, 50, 54, 59, -1, -1 }, 0, 24, 440.0f, 0, 0 },
        // 6 — Drop D
        { "Drop D", 6, { 38, 45, 50, 55, 59, 64, -1, -1 }, 0, 24, 440.0f, 0, 0 },
        // 7 — Drop C#
        { "Drop C#", 6, { 37, 44, 49, 54, 58, 63, -1, -1 }, 0, 24, 440.0f, 0, 0 },
        // 8 — Drop C
        { "Drop C", 6, { 36, 43, 48, 53, 57, 62, -1, -1 }, 0, 24, 440.0f, 0, 0 },
        // 9 — Drop B
        { "Drop B", 6, { 35, 42, 47, 52, 56, 61, -1, -1 }, 0, 24, 440.0f, 0, 0 },
        // 10 — Drop A
        { "Drop A", 6, { 33, 40, 45, 50, 54, 59, -1, -1 }, 0, 24, 440.0f, 0, 0 },
        // 11 — 7-string Standard
        { "7-string Standard", 7, { 35, 40, 45, 50, 54, 59, 64, -1 }, 0, 24, 440.0f, 0, 0 },
        // 12 — 7-string Drop A
        { "7-string Drop A", 7, { 33, 40, 45, 50, 54, 59, 64, -1 }, 0, 24, 440.0f, 0, 0 },
        // 13 — 8-string Standard
        { "8-string Standard", 8, { 30, 35, 40, 45, 50, 54, 59, 64 }, 0, 24, 440.0f, 0, 0 },
        // 14 — 8-string Drop E
        { "8-string Drop E", 8, { 28, 35, 40, 45, 50, 54, 59, 64 }, 0, 24, 440.0f, 0, 0 },
        // 15 — 4-string Bass
        { "4-string Bass", 4, { 28, 33, 38, 43, -1, -1, -1, -1 }, 0, 24, 440.0f, 0, 0 },
        // 16 — 5-string Bass
        { "5-string Bass", 5, { 23, 28, 33, 38, 43, -1, -1, -1 }, 0, 24, 440.0f, 0, 0 },
        // 17 — Custom (user-defined; actual data lives in a CustomTuning record)
        { "Custom", 0, { -1, -1, -1, -1, -1, -1, -1, -1 }, 0, 24, 440.0f, 0, 0 },
    }};

    const int i = std::clamp (index, 0, static_cast<int> (table.size()) - 1);
    return table[static_cast<size_t> (i)];
}

}  // namespace

//==============================================================================
// CustomTuning
//==============================================================================

CustomTuning::CustomTuning()
{
    openStringMidi.fill (-1);
    openStringMidi[0] = 40; openStringMidi[1] = 45; openStringMidi[2] = 50;
    openStringMidi[3] = 55; openStringMidi[4] = 59; openStringMidi[5] = 64;
}

void CustomTuning::setName (const juce::String& newName)            { name = newName; }
void CustomTuning::setStringCount (int count)
{
    stringCount = std::clamp (count, 0, kMaxStrings);
}
void CustomTuning::setOpenStringMidi (int stringIndex, int midiNote)
{
    if (stringIndex < 0 || stringIndex >= kMaxStrings) return;
    openStringMidi[static_cast<size_t> (stringIndex)] = midiNote;
}
void CustomTuning::setFretRange (int minFret, int maxFret)
{
    if (minFret > maxFret) std::swap (minFret, maxFret);
    fretRangeMin = std::max (0, minFret);
    fretRangeMax = std::max (fretRangeMin, std::min (36, maxFret));
}
void CustomTuning::setConcertPitchHz (float hz)
{
    concertPitchHz = (hz > 1.0f && hz < 1000.0f) ? hz : 440.0f;
}
void CustomTuning::setCapoSemitones (int semis)        { capoSemitones = std::clamp (semis, 0, 24); }
void CustomTuning::setTranspositionSemitones (int semis){ transpositionSemitones = std::clamp (semis, -24, 24); }

int CustomTuning::getOpenStringMidi (int i) const
{
    if (i < 0 || i >= kMaxStrings) return -1;
    return openStringMidi[static_cast<size_t> (i)];
}

TuningProfile CustomTuning::toTuningProfile() const
{
    TuningProfile t {};
    // name is a juce::String; copy to a stable C string via a static buffer?
    // No — return a copy with a heap-allocated char array is awkward. Instead,
    // callers that need the name should call getName(). We set displayName to
    // a literal placeholder; the catalog never stores CustomTuning as a
    // factory entry.
    t.name = "Custom";
    t.stringCount  = stringCount;
    t.openStringMidi = openStringMidi;
    t.fretRangeMin = fretRangeMin;
    t.fretRangeMax = fretRangeMax;
    t.concertPitchHz     = concertPitchHz;
    t.capoSemitones      = capoSemitones;
    t.transpositionSemitones = transpositionSemitones;
    return t;
}

//==============================================================================
// ProfileCatalog
//==============================================================================

int ProfileCatalog::getNumTuningProfiles() noexcept
{
    return 18;
}

const PlacementProfile* ProfileCatalog::getPlacement (int index) noexcept
{
    if (index < 0 || index >= kNumPlacementProfiles) return nullptr;
    return &placementAt (index);
}

const PlacementProfile* ProfileCatalog::getPlacement (PlacementId id) noexcept
{
    return getPlacement (static_cast<int> (id));
}

const TuningProfile* ProfileCatalog::getTuning (int index) noexcept
{
    if (index < 0 || index >= getNumTuningProfiles()) return nullptr;
    return &tuningAt (index);
}

const PlacementProfile* ProfileCatalog::findPlacementByName (std::string_view name) noexcept
{
    for (int i = 0; i < kNumPlacementProfiles; ++i)
    {
        const auto& p = placementAt (i);
        if (name == p.displayName) return &p;
    }
    return nullptr;
}

const TuningProfile* ProfileCatalog::findTuningByName (std::string_view name) noexcept
{
    const int n = getNumTuningProfiles();
    for (int i = 0; i < n; ++i)
    {
        const auto& t = tuningAt (i);
        if (name == t.name) return &t;
    }
    return nullptr;
}

int ProfileCatalog::listPlacementProfiles (const PlacementProfile** outArray, int capacity) noexcept
{
    if (outArray == nullptr || capacity <= 0) return 0;
    const int n = std::min (capacity, kNumPlacementProfiles);
    for (int i = 0; i < n; ++i)
        outArray[i] = &placementAt (i);
    return n;
}

int ProfileCatalog::listTuningProfiles (const TuningProfile** outArray, int capacity) noexcept
{
    if (outArray == nullptr || capacity <= 0) return 0;
    const int total = getNumTuningProfiles();
    const int n = std::min (capacity, total);
    for (int i = 0; i < n; ++i)
        outArray[i] = &tuningAt (i);
    return n;
}

const PlacementProfile& defaultPlacementProfile() noexcept { return placementAt (0); }
const TuningProfile&    defaultTuningProfile()    noexcept { return tuningAt (0); }

//==============================================================================
// applyPlacementDefaults — explicit, message-thread only
//==============================================================================

namespace
{

void setParam (juce::AudioProcessorValueTreeState& apvts,
               const char* id, float value)
{
    if (auto* p = apvts.getParameter (id))
    {
        const auto range = p->getNormalisableRange();
        p->setValueNotifyingHost (range.convertTo0to1 (value));
    }
}

void setBoolParam (juce::AudioProcessorValueTreeState& apvts,
                  const char* id, bool value)
{
    if (auto* p = apvts.getParameter (id))
        p->setValueNotifyingHost (value ? 1.0f : 0.0f);
}

}  // namespace

void applyPlacementDefaults (juce::AudioProcessorValueTreeState& apvts,
                             PlacementId placementId)
{
    const int idx = static_cast<int> (placementId);
    if (idx < 0 || idx >= kNumPlacementProfiles) return;

    const auto& profile = placementAt (idx);
    const auto& seed = profile.defaultSeed;

    // Module enables — explicit only. No hidden continuous modulation.
    setBoolParam (apvts, ParameterIDs::TightCut::enabled, seed.tightCutEnabled);
    if (seed.tightCutEnabled)
        setParam (apvts, ParameterIDs::TightCut::frequency, seed.tightCutFreqHz);

    setBoolParam (apvts, ParameterIDs::Chug::enabled, seed.chugEnabled);
    if (seed.chugEnabled)
        setParam (apvts, ParameterIDs::Chug::frequency, seed.chugFreqHz);

    setBoolParam (apvts, ParameterIDs::multibandEnabled, seed.multibandEnabled);
    setBoolParam (apvts, ParameterIDs::MidSide::monoGuardEnabled, seed.midSideMonoGuard);

    // Note follower is enabled via band trackingSource; we only seed its
    // helper params when the profile wants it active. The actual band
    // selection is left to the user / preset.
    if (seed.noteFollowerEnabled)
        setParam (apvts, ParameterIDs::NoteFollower::confidenceThreshold, 0.5f);

    // Band seeds — sparse, conservative, fully editable.
    for (int i = 0; i < seed.activeBandCount && i < 12; ++i)
    {
        const auto& b = seed.bands[static_cast<size_t> (i)];
        if (b.index < 0 || b.index >= 12) continue;

        const auto& bid = ParameterIDs::bands[static_cast<size_t> (b.index)];
        setBoolParam (apvts, bid.enabled,      b.enabled);
        setParam   (apvts, bid.stage,          static_cast<float> (b.stage));
        setParam   (apvts, bid.type,           static_cast<float> (b.type));
        setParam   (apvts, bid.frequency,      b.frequencyHz);
        setParam   (apvts, bid.gain,           b.gainDb);
        setParam   (apvts, bid.q,              b.q);
        setParam   (apvts, bid.route,          static_cast<float> (b.route));
        setParam   (apvts, bid.dynamicMode,    static_cast<float> (b.dynamicMode));
    }
}

//==============================================================================
// Serialization helpers
//==============================================================================

namespace Serialization
{

void writePlacementState (juce::ValueTree& vt, int placementIndex)
{
    if (placementIndex < 0 || placementIndex >= kNumPlacementProfiles)
        placementIndex = 0;

    vt.setProperty (kPlacementId, placementIndex, nullptr);
}

void writeTuningState (juce::ValueTree& vt,
                       int tuningIndex,
                       const CustomTuning* custom)
{
    if (tuningIndex < 0 || tuningIndex >= ProfileCatalog::getNumTuningProfiles())
        tuningIndex = 0;

    vt.setProperty (kTuningId, tuningIndex, nullptr);
    vt.removeProperty (kIsCustomTuning, nullptr);
    vt.removeProperty (kCustomName, nullptr);
    vt.removeProperty (kStringCount, nullptr);
    vt.removeProperty (kOpenStrings, nullptr);
    vt.removeProperty (kFretMin, nullptr);
    vt.removeProperty (kFretMax, nullptr);
    vt.removeProperty (kConcertPitch, nullptr);
    vt.removeProperty (kCapo, nullptr);
    vt.removeProperty (kTransposition, nullptr);

    if (custom != nullptr && tuningIndex == kCustomTuningIndex)
    {
        vt.setProperty (kIsCustomTuning, true, nullptr);
        vt.setProperty (kCustomName, custom->getName(), nullptr);
        vt.setProperty (kStringCount, custom->getStringCount(), nullptr);

        juce::StringArray notes;
        for (int i = 0; i < kMaxStrings; ++i)
            notes.add (juce::String (custom->getOpenStringMidi (i)));
        vt.setProperty (kOpenStrings, notes.joinIntoString (","), nullptr);

        vt.setProperty (kFretMin, custom->getFretRangeMin(), nullptr);
        vt.setProperty (kFretMax, custom->getFretRangeMax(), nullptr);
        vt.setProperty (kConcertPitch, custom->getConcertPitchHz(), nullptr);
        vt.setProperty (kCapo, custom->getCapoSemitones(), nullptr);
        vt.setProperty (kTransposition, custom->getTranspositionSemitones(), nullptr);
    }
}

int readPlacementIndex (const juce::ValueTree& vt)
{
    const int idx = vt.getProperty (kPlacementId, -1);
    if (idx < 0 || idx >= kNumPlacementProfiles) return -1;
    return idx;
}

int readTuningIndex (const juce::ValueTree& vt)
{
    const int idx = vt.getProperty (kTuningId, -1);
    if (idx < 0 || idx >= ProfileCatalog::getNumTuningProfiles()) return -1;
    return idx;
}

bool readCustomTuning (const juce::ValueTree& vt, CustomTuning& out)
{
    if (! vt.hasProperty (kIsCustomTuning)) return false;
    if (! static_cast<bool> (vt.getProperty (kIsCustomTuning, false))) return false;

    out.setName (vt.getProperty (kCustomName, "Custom").toString());

    const int sc = vt.getProperty (kStringCount, 6);
    out.setStringCount (sc);

    const juce::String notesStr = vt.getProperty (kOpenStrings, "").toString();
    const auto tokens = juce::StringArray::fromTokens (notesStr, ",", "");
    for (int i = 0; i < kMaxStrings && i < tokens.size(); ++i)
        out.setOpenStringMidi (i, tokens[i].getIntValue());

    const int fMin = vt.getProperty (kFretMin, 0);
    const int fMax = vt.getProperty (kFretMax, 24);
    out.setFretRange (fMin, fMax);

    out.setConcertPitchHz (static_cast<float> (vt.getProperty (kConcertPitch, 440.0)));
    out.setCapoSemitones (static_cast<int> (vt.getProperty (kCapo, 0)));
    out.setTranspositionSemitones (static_cast<int> (vt.getProperty (kTransposition, 0)));
    return true;
}

std::unique_ptr<juce::XmlElement> toXml (int placementIndex,
                                         int tuningIndex,
                                         const CustomTuning* custom)
{
    juce::ValueTree vt ("ShredEQProfiles");
    writePlacementState (vt, placementIndex);
    writeTuningState (vt, tuningIndex, custom);
    return vt.createXml();
}

bool fromXml (const juce::XmlElement& xml,
              int& placementIndex,
              int& tuningIndex,
              CustomTuning& customOut,
              bool& hasCustomOut)
{
    if (xml.getTagName() != "ShredEQProfiles") return false;

    const juce::ValueTree vt (juce::ValueTree::fromXml (xml));
    placementIndex = readPlacementIndex (vt);
    tuningIndex    = readTuningIndex (vt);
    hasCustomOut   = readCustomTuning (vt, customOut);

    if (placementIndex < 0) placementIndex = 0;
    if (tuningIndex    < 0) tuningIndex    = 0;
    return true;
}

}  // namespace Serialization

}  // namespace ShredEQ::Profiles
