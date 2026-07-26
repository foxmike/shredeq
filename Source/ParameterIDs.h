#pragma once

#include <array>

namespace ShredEQ::ParameterIDs
{
inline constexpr int schemaVersion = 1;

struct Band
{
    const char* enabled;
    const char* stage;
    const char* type;
    const char* frequency;
    const char* gain;
    const char* q;
    const char* slope;
    const char* route;
    const char* dynamicMode;
    const char* dynamicRange;
    const char* threshold;
    const char* ratio;
    const char* attack;
    const char* hold;
    const char* release;
    const char* knee;
    const char* detectorSource;
    const char* detectorFrequency;
    const char* detectorQ;
    const char* trackingSource;
    const char* trackingHarmonic;
    const char* lookahead;
};

inline constexpr std::array<Band, 12> bands {{
    { "band01.enabled", "band01.stage", "band01.type", "band01.frequency", "band01.gain", "band01.q", "band01.slope", "band01.route", "band01.dynamicMode", "band01.dynamicRange", "band01.threshold", "band01.ratio", "band01.attack", "band01.hold", "band01.release", "band01.knee", "band01.detectorSource", "band01.detectorFrequency", "band01.detectorQ", "band01.trackingSource", "band01.trackingHarmonic", "band01.lookahead" },
    { "band02.enabled", "band02.stage", "band02.type", "band02.frequency", "band02.gain", "band02.q", "band02.slope", "band02.route", "band02.dynamicMode", "band02.dynamicRange", "band02.threshold", "band02.ratio", "band02.attack", "band02.hold", "band02.release", "band02.knee", "band02.detectorSource", "band02.detectorFrequency", "band02.detectorQ", "band02.trackingSource", "band02.trackingHarmonic", "band02.lookahead" },
    { "band03.enabled", "band03.stage", "band03.type", "band03.frequency", "band03.gain", "band03.q", "band03.slope", "band03.route", "band03.dynamicMode", "band03.dynamicRange", "band03.threshold", "band03.ratio", "band03.attack", "band03.hold", "band03.release", "band03.knee", "band03.detectorSource", "band03.detectorFrequency", "band03.detectorQ", "band03.trackingSource", "band03.trackingHarmonic", "band03.lookahead" },
    { "band04.enabled", "band04.stage", "band04.type", "band04.frequency", "band04.gain", "band04.q", "band04.slope", "band04.route", "band04.dynamicMode", "band04.dynamicRange", "band04.threshold", "band04.ratio", "band04.attack", "band04.hold", "band04.release", "band04.knee", "band04.detectorSource", "band04.detectorFrequency", "band04.detectorQ", "band04.trackingSource", "band04.trackingHarmonic", "band04.lookahead" },
    { "band05.enabled", "band05.stage", "band05.type", "band05.frequency", "band05.gain", "band05.q", "band05.slope", "band05.route", "band05.dynamicMode", "band05.dynamicRange", "band05.threshold", "band05.ratio", "band05.attack", "band05.hold", "band05.release", "band05.knee", "band05.detectorSource", "band05.detectorFrequency", "band05.detectorQ", "band05.trackingSource", "band05.trackingHarmonic", "band05.lookahead" },
    { "band06.enabled", "band06.stage", "band06.type", "band06.frequency", "band06.gain", "band06.q", "band06.slope", "band06.route", "band06.dynamicMode", "band06.dynamicRange", "band06.threshold", "band06.ratio", "band06.attack", "band06.hold", "band06.release", "band06.knee", "band06.detectorSource", "band06.detectorFrequency", "band06.detectorQ", "band06.trackingSource", "band06.trackingHarmonic", "band06.lookahead" },
    { "band07.enabled", "band07.stage", "band07.type", "band07.frequency", "band07.gain", "band07.q", "band07.slope", "band07.route", "band07.dynamicMode", "band07.dynamicRange", "band07.threshold", "band07.ratio", "band07.attack", "band07.hold", "band07.release", "band07.knee", "band07.detectorSource", "band07.detectorFrequency", "band07.detectorQ", "band07.trackingSource", "band07.trackingHarmonic", "band07.lookahead" },
    { "band08.enabled", "band08.stage", "band08.type", "band08.frequency", "band08.gain", "band08.q", "band08.slope", "band08.route", "band08.dynamicMode", "band08.dynamicRange", "band08.threshold", "band08.ratio", "band08.attack", "band08.hold", "band08.release", "band08.knee", "band08.detectorSource", "band08.detectorFrequency", "band08.detectorQ", "band08.trackingSource", "band08.trackingHarmonic", "band08.lookahead" },
    { "band09.enabled", "band09.stage", "band09.type", "band09.frequency", "band09.gain", "band09.q", "band09.slope", "band09.route", "band09.dynamicMode", "band09.dynamicRange", "band09.threshold", "band09.ratio", "band09.attack", "band09.hold", "band09.release", "band09.knee", "band09.detectorSource", "band09.detectorFrequency", "band09.detectorQ", "band09.trackingSource", "band09.trackingHarmonic", "band09.lookahead" },
    { "band10.enabled", "band10.stage", "band10.type", "band10.frequency", "band10.gain", "band10.q", "band10.slope", "band10.route", "band10.dynamicMode", "band10.dynamicRange", "band10.threshold", "band10.ratio", "band10.attack", "band10.hold", "band10.release", "band10.knee", "band10.detectorSource", "band10.detectorFrequency", "band10.detectorQ", "band10.trackingSource", "band10.trackingHarmonic", "band10.lookahead" },
    { "band11.enabled", "band11.stage", "band11.type", "band11.frequency", "band11.gain", "band11.q", "band11.slope", "band11.route", "band11.dynamicMode", "band11.dynamicRange", "band11.threshold", "band11.ratio", "band11.attack", "band11.hold", "band11.release", "band11.knee", "band11.detectorSource", "band11.detectorFrequency", "band11.detectorQ", "band11.trackingSource", "band11.trackingHarmonic", "band11.lookahead" },
    { "band12.enabled", "band12.stage", "band12.type", "band12.frequency", "band12.gain", "band12.q", "band12.slope", "band12.route", "band12.dynamicMode", "band12.dynamicRange", "band12.threshold", "band12.ratio", "band12.attack", "band12.hold", "band12.release", "band12.knee", "band12.detectorSource", "band12.detectorFrequency", "band12.detectorQ", "band12.trackingSource", "band12.trackingHarmonic", "band12.lookahead" }
}};

inline constexpr auto inputGain = "inputGain";
inline constexpr auto outputGain = "outputGain";
inline constexpr auto outputTrim = "outputTrim";
inline constexpr auto globalBypass = "globalBypass";
inline constexpr auto analyzerEnable = "analyzerEnable";
inline constexpr auto abState = "abState";
inline constexpr auto profileIndex = "profileIndex";
inline constexpr auto tuningIndex = "tuningIndex";
inline constexpr auto lookaheadMode = "lookaheadMode";

namespace TightCut
{
inline constexpr auto enabled = "tightCut_enabled";
inline constexpr auto frequency = "tightCut_frequency";
inline constexpr auto slope = "tightCut_slope";
inline constexpr auto resonance = "tightCut_resonance";
inline constexpr auto punchRatio = "tightCut_punchRatio";
inline constexpr auto punchGain = "tightCut_punchGain";
inline constexpr auto punchQ = "tightCut_punchQ";
inline constexpr auto placement = "tightCut_placement";
inline constexpr auto routing = "tightCut_routing";
inline constexpr auto listenRemoved = "tightCut_listenRemoved";
} // namespace TightCut

namespace Chug
{
inline constexpr auto enabled = "chug_enabled";
inline constexpr auto frequency = "chug_frequency";
inline constexpr auto bandwidth = "chug_bandwidth";
inline constexpr auto maxReduction = "chug_maxReduction";
inline constexpr auto threshold = "chug_threshold";
inline constexpr auto attack = "chug_attack";
inline constexpr auto hold = "chug_hold";
inline constexpr auto release = "chug_release";
inline constexpr auto preservePick = "chug_preservePick";
inline constexpr auto transientSens = "chug_transientSens";
inline constexpr auto dominanceSens = "chug_dominanceSens";
} // namespace Chug

inline constexpr auto multibandEnabled = "mb_enabled";
inline constexpr std::array<const char*, 3> multibandCrossovers {
    "mb_crossover1", "mb_crossover2", "mb_crossover3"
};

struct MultibandZone
{
    const char* gain;
    const char* threshold;
    const char* ratio;
    const char* attack;
    const char* release;
    const char* transientEmphasis;
    const char* sustainEmphasis;
    const char* placement;
    const char* detectorSource;
    const char* autoThreshold;
    const char* mode;
};

inline constexpr std::array<MultibandZone, 4> multibandZones {{
    { "mb_zone1_gain", "mb_zone1_threshold", "mb_zone1_ratio", "mb_zone1_attack", "mb_zone1_release", "mb_zone1_transientEmphasis", "mb_zone1_sustainEmphasis", "mb_zone1_placement", "mb_zone1_detectorSource", "mb_zone1_autoThreshold", "mb_zone1_mode" },
    { "mb_zone2_gain", "mb_zone2_threshold", "mb_zone2_ratio", "mb_zone2_attack", "mb_zone2_release", "mb_zone2_transientEmphasis", "mb_zone2_sustainEmphasis", "mb_zone2_placement", "mb_zone2_detectorSource", "mb_zone2_autoThreshold", "mb_zone2_mode" },
    { "mb_zone3_gain", "mb_zone3_threshold", "mb_zone3_ratio", "mb_zone3_attack", "mb_zone3_release", "mb_zone3_transientEmphasis", "mb_zone3_sustainEmphasis", "mb_zone3_placement", "mb_zone3_detectorSource", "mb_zone3_autoThreshold", "mb_zone3_mode" },
    { "mb_zone4_gain", "mb_zone4_threshold", "mb_zone4_ratio", "mb_zone4_attack", "mb_zone4_release", "mb_zone4_transientEmphasis", "mb_zone4_sustainEmphasis", "mb_zone4_placement", "mb_zone4_detectorSource", "mb_zone4_autoThreshold", "mb_zone4_mode" }
}};

namespace Sidechain
{
inline constexpr auto source = "sc_source";
inline constexpr auto lockInEnabled = "sc_lockInEnabled";
inline constexpr auto lockInAmount = "sc_lockInAmount";
inline constexpr auto carveMaxGain = "sc_carveMaxGain";
inline constexpr auto carveThreshold = "sc_carveThreshold";
} // namespace Sidechain

namespace MidSide
{
inline constexpr auto sideHighPass = "ms_sideHighPass";
inline constexpr auto monoGuardEnabled = "ms_monoGuardEnabled";
inline constexpr auto monoGuardProtection = "ms_monoGuardProtection";
} // namespace MidSide

struct ResonanceSlot
{
    const char* enabled;
    const char* frequency;
    const char* q;
    const char* maxDepth;
    const char* attack;
    const char* release;
    const char* persistence;
    const char* mode;
    const char* placement;
    const char* freeze;
};

inline constexpr std::array<ResonanceSlot, 6> resonanceSlots {{
    { "res1_enabled", "res1_frequency", "res1_q", "res1_maxDepth", "res1_attack", "res1_release", "res1_persistence", "res1_mode", "res1_placement", "res1_freeze" },
    { "res2_enabled", "res2_frequency", "res2_q", "res2_maxDepth", "res2_attack", "res2_release", "res2_persistence", "res2_mode", "res2_placement", "res2_freeze" },
    { "res3_enabled", "res3_frequency", "res3_q", "res3_maxDepth", "res3_attack", "res3_release", "res3_persistence", "res3_mode", "res3_placement", "res3_freeze" },
    { "res4_enabled", "res4_frequency", "res4_q", "res4_maxDepth", "res4_attack", "res4_release", "res4_persistence", "res4_mode", "res4_placement", "res4_freeze" },
    { "res5_enabled", "res5_frequency", "res5_q", "res5_maxDepth", "res5_attack", "res5_release", "res5_persistence", "res5_mode", "res5_placement", "res5_freeze" },
    { "res6_enabled", "res6_frequency", "res6_q", "res6_maxDepth", "res6_attack", "res6_release", "res6_persistence", "res6_mode", "res6_placement", "res6_freeze" }
}};

namespace NoteFollower
{
inline constexpr auto trackingSource = "note_trackingSource";
inline constexpr auto harmonic = "note_harmonic";
inline constexpr auto centsOffset = "note_centsOffset";
inline constexpr auto confidenceThreshold = "note_confidenceThreshold";
inline constexpr auto smoothing = "note_smoothing";
inline constexpr auto holdTime = "note_holdTime";
inline constexpr auto returnTime = "note_returnTime";
inline constexpr auto minFreq = "note_minFreq";
inline constexpr auto maxFreq = "note_maxFreq";
inline constexpr auto midiPriority = "note_midiPriority";
} // namespace NoteFollower
} // namespace ShredEQ::ParameterIDs
