#pragma once

#include <array>
#include <cstdint>
#include <string_view>

#include <juce_audio_processors/juce_audio_processors.h>

/**
    ShredEQ profile system (spec §15).

    Profiles change defaults, analyzer weighting, labels, and recommended
    ranges. They never impose hidden audio processing. This module exposes
    only data and message-thread helpers:

      - factory placement profiles (§15.1)
      - factory + custom tuning profiles (§15.2)
      - index-stable catalog access for profileIndex / tuningIndex parameters
      - optional applyPlacementDefaults() that writes documented default
        seeds into an APVTS when explicitly invoked
      - ValueTree/XML serialization helpers for host state (StateManager)

    No function in this module performs audio-thread work, allocates on the
    audio thread, or applies continuous hidden modulation. Catalog tables are
    constructed once on the message thread (or referenced as static data).
*/
namespace ShredEQ::Profiles
{

//==============================================================================
// Placement profiles (§15.1)
//==============================================================================

/** Stable 0-based placement profile identifiers. Order must remain contiguous
    and stable for the profileIndex parameter. Append new entries only. */
enum class PlacementId : int
{
    GuitarDIPreAmp      = 0,
    PostAmpCab          = 1,
    RhythmGuitar        = 2,
    LeadGuitar          = 3,
    StereoGuitarBus     = 4,
    ActiveBass          = 5,
    DistortedBass       = 6,

    numFactoryProfiles  = 7
};

/** Maximum number of factory placement profiles. Kept as a constant so the
    catalog can size fixed storage without heap at construction time. */
inline constexpr int kNumPlacementProfiles = static_cast<int> (PlacementId::numFactoryProfiles);

/**
    Analyzer weighting hints. Data only — they are consumed by the analyzer /
    diagnostic layers to bias label selection and weighting. They do not
    perform processing and are not applied continuously to the EQ response.
*/
struct AnalyzerWeighting
{
    float lowWeight       = 1.0f;   // < 140 Hz importance
    float lowMidWeight    = 1.0f;   // 140 Hz – 900 Hz
    float highMidWeight   = 1.0f;   // 900 Hz – 4.5 kHz
    float highWeight      = 1.0f;   // > 4.5 kHz
    float resonanceWeight = 1.0f;   // persistence scan emphasis
    float fizzWeight      = 1.0f;   // anti-fizz emphasis
    float transientWeight = 1.0f;   // chug / palm-mute emphasis
    float maskingWeight   = 1.0f;   // sidechain mask emphasis
};

/**
    Recommended parameter range guidance. These are display/UI hints (min/max
    guides, default centers). They are NOT hard clamps — the underlying DSP
    ranges defined in ParameterIDs / spec §3.3 remain authoritative. Profiles
    must never narrow real ranges in a way that hides processing from the user.
*/
struct RangeGuide
{
    float freqMinHz     = 15.0f;
    float freqMaxHz     = 20000.0f;
    float gainMinDb     = -24.0f;
    float gainMaxDb     = 24.0f;
    float qMin          = 0.10f;
    float qMax          = 36.0f;
};

/**
    Structured default-seed hints for a placement profile. These describe what
    a fresh instance looks like when a profile is explicitly applied via
    applyPlacementDefaults(). They are not continuously enforced.

    Band seeds are sparse: only the bands a profile cares about are seeded.
    The catalog produces a ValueTree form for StateManager / APVTS seeding.
*/
struct BandSeed
{
    int    index       = 0;     // 0-based band index, 0..11
    bool   enabled     = false;
    int    stage       = 0;     // 0 = Stage A, 1 = Stage B
    int    type        = 0;     // filter type enum value (processor-defined)
    float  frequencyHz = 1000.0f;
    float  gainDb      = 0.0f;
    float  q           = 1.0f;
    int    route       = 0;     // 0 = stereo linked
    int    dynamicMode = 0;     // 0 = static
};

struct DefaultSeed
{
    bool                       tightCutEnabled    = false;
    float                      tightCutFreqHz     = 80.0f;
    bool                       chugEnabled        = false;
    float                      chugFreqHz         = 120.0f;
    bool                       noteFollowerEnabled = false;
    bool                       multibandEnabled   = false;
    bool                       midSideMonoGuard   = false;
    bool                       tightCutEmphasis   = false;  // emphasis flag for label/UI
    bool                       chugEmphasis       = false;
    std::array<BandSeed, 12>   bands {};
    int                        activeBandCount    = 0;
};

/** A complete factory placement profile record (data only). */
struct PlacementProfile
{
    PlacementId         id;
    const char*         displayName;
    const char*         categoryLabel;
    AnalyzerWeighting   weighting;
    RangeGuide          rangeGuide;
    DefaultSeed         defaultSeed;
};

//==============================================================================
// Tuning profiles (§15.2)
//==============================================================================

/** Maximum supported string count. Open-string array is fixed-size to avoid
    heap allocation in catalog records. */
inline constexpr int kMaxStrings = 8;

/**
    A tuning record. openStringMidi holds MIDI note numbers for each string
    when useHz is false; otherwise openStringHz holds absolute frequencies.
    Factory tunings use MIDI note numbers for portability across concert
    pitch; the resolved frequency is concertPitchHz * 2^((midi-69)/12).
*/
struct TuningProfile
{
    const char*                 name;
    int                         stringCount;
    std::array<int, kMaxStrings> openStringMidi;  // unused slots = -1
    int                         fretRangeMin;
    int                         fretRangeMax;
    float                       concertPitchHz;
    int                         capoSemitones;
    int                         transpositionSemitones;
};

/**
    Mutable custom tuning. Mirrors TuningProfile but with setters so the user
    can specify string count, open-note pitches, fret range, concert pitch,
    and capo/transposition (spec §15.2). Owned on the message thread.
*/
class CustomTuning
{
public:
    CustomTuning();

    void setName (const juce::String& newName);
    void setStringCount (int count);
    /** Sets MIDI note number for string index (0-based). Out-of-range ignored. */
    void setOpenStringMidi (int stringIndex, int midiNote);
    void setFretRange (int minFret, int maxFret);
    void setConcertPitchHz (float hz);
    void setCapoSemitones (int semis);
    void setTranspositionSemitones (int semis);

    juce::String getName()               const { return name; }
    int          getStringCount()        const { return stringCount; }
    int          getOpenStringMidi (int i) const;
    int          getFretRangeMin()       const { return fretRangeMin; }
    int          getFretRangeMax()       const { return fretRangeMax; }
    float        getConcertPitchHz()     const { return concertPitchHz; }
    int          getCapoSemitones()      const { return capoSemitones; }
    int          getTranspositionSemitones() const { return transpositionSemitones; }

    /** Fills a TuningProfile view (without copying strings). */
    TuningProfile toTuningProfile() const;

private:
    juce::String                       name;
    int                                stringCount { 6 };
    std::array<int, kMaxStrings>       openStringMidi {};
    int                                fretRangeMin { 0 };
    int                                fretRangeMax { 24 };
    float                              concertPitchHz { 440.0f };
    int                                capoSemitones { 0 };
    int                                transpositionSemitones { 0 };
};

//==============================================================================
// Catalog access (index-stable, message thread)
//==============================================================================

class ProfileCatalog
{
public:
    ProfileCatalog() = default;
    ~ProfileCatalog() = default;

    /** Returns the number of factory placement profiles (stable). */
    static int getNumPlacementProfiles() noexcept { return kNumPlacementProfiles; }

    /** Returns the number of factory tuning profiles (stable). */
    static int getNumTuningProfiles() noexcept;

    /** Stable 0-based index access. Out-of-range returns nullptr. */
    static const PlacementProfile* getPlacement (int index) noexcept;
    static const PlacementProfile* getPlacement (PlacementId id) noexcept;
    static const TuningProfile*    getTuning (int index) noexcept;

    /** Find by display name (case-sensitive). Returns nullptr if not found. */
    static const PlacementProfile* findPlacementByName (std::string_view name) noexcept;
    static const TuningProfile*    findTuningByName (std::string_view name) noexcept;

    /** Fills the caller's array with pointers to all factory placement
        profiles in stable index order. Returns the count. */
    static int listPlacementProfiles (const PlacementProfile** outArray, int capacity) noexcept;

    /** Fills the caller's array with pointers to all factory tuning profiles
        in stable index order. Returns the count. */
    static int listTuningProfiles (const TuningProfile** outArray, int capacity) noexcept;
};

/** Convenience: returns the factory default placement profile (GuitarDIPreAmp). */
const PlacementProfile& defaultPlacementProfile() noexcept;

/** Convenience: returns the factory default tuning profile (E Standard). */
const TuningProfile& defaultTuningProfile() noexcept;

//==============================================================================
// applyPlacementDefaults — explicit default seeding (message thread only)
//==============================================================================

/**
    Writes the documented default seeds for the given placement profile into
    the APVTS. This is an explicit, user-invoked operation — it never runs
    continuously and never applies hidden modulation. Only parameters listed
    in the profile's DefaultSeed are touched; everything else is left alone.

    Call on the message thread only. APVTS writes are not real-time safe.
*/
void applyPlacementDefaults (juce::AudioProcessorValueTreeState& apvts,
                             PlacementId placementId);

//==============================================================================
// Serialization helpers (message thread; for StateManager / host state)
//==============================================================================

namespace Serialization
{
    // Property keys used inside ValueTree/XML state.
    inline constexpr const char* kPlacementId   = "placementId";
    inline constexpr const char* kTuningId      = "tuningId";
    inline constexpr const char* kIsCustomTuning = "isCustomTuning";
    inline constexpr const char* kCustomName    = "customTuningName";
    inline constexpr const char* kStringCount   = "stringCount";
    inline constexpr const char* kOpenStrings   = "openStringMidi";
    inline constexpr const char* kFretMin       = "fretRangeMin";
    inline constexpr const char* kFretMax       = "fretRangeMax";
    inline constexpr const char* kConcertPitch  = "concertPitchHz";
    inline constexpr const char* kCapo          = "capoSemitones";
    inline constexpr const char* kTransposition = "transpositionSemitones";

    /** Writes placement id + (optional) custom tuning fields to a ValueTree. */
    void writePlacementState (juce::ValueTree& vt, int placementIndex);

    /** Writes tuning index and, when custom, the full custom tuning record. */
    void writeTuningState (juce::ValueTree& vt,
                           int tuningIndex,
                           const CustomTuning* custom);

    /** Reads placement index from a ValueTree. Returns -1 if absent/invalid. */
    int readPlacementIndex (const juce::ValueTree& vt);

    /** Reads tuning index from a ValueTree. Returns -1 if absent/invalid. */
    int readTuningIndex (const juce::ValueTree& vt);

    /** Returns true and fills `out` when the ValueTree carries a custom tuning. */
    bool readCustomTuning (const juce::ValueTree& vt, CustomTuning& out);

    /** Convenience XML round-trip (StateManager may use these). */
    std::unique_ptr<juce::XmlElement> toXml (int placementIndex,
                                             int tuningIndex,
                                             const CustomTuning* custom);
    bool fromXml (const juce::XmlElement& xml,
                  int& placementIndex,
                  int& tuningIndex,
                  CustomTuning& customOut,
                  bool& hasCustomOut);
}  // namespace Serialization

}  // namespace ShredEQ::Profiles
