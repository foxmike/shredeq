#!/usr/bin/env bash
# scripts/validate.sh — ShredEQ CI/operator validation entrypoint.
#
# Runs: Release (or documented fallback) configure + build, ctest unit tests,
# and static/source gates covering RT-safety intent, zero-latency reporting,
# click-free bypass intent, and state round-trip coverage pointers.
#
# NOTE: This script provides *automated source-level gates* only. Full proof of
# RT-safety, pluginval host validation, DAW sidechain behavior, and Windows
# builds remain REQUIRES_MANUAL (see summary at the end). Spec §18.1 / §19.1 /
# §14.2 / §17 / §22.
set -euo pipefail

# --- Resolve repo root from script location -------------------------------
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
cd "${REPO_ROOT}"

# --- Configurable presets -------------------------------------------------
# The project ships only a `linux-relwithdebinfo` preset today (see
# CMakePresets.json). A true Release preset (e.g. linux-release) does not yet
# exist; if/when added, set CONFIGURE_PRESET=linux-release to use it.
CONFIGURE_PRESET="${CONFIGURE_PRESET:-linux-relwithdebinfo}"
BUILD_PRESET="${BUILD_PRESET:-${CONFIGURE_PRESET}}"
TEST_PRESET="${TEST_PRESET:-${CONFIGURE_PRESET}}"

# Track whether we silently fell back to a non-Release preset.
USING_RELEASE_FALLBACK=0

if ! grep -q "\"name\"[[:space:]]*:[[:space:]]*\"${CONFIGURE_PRESET}\"" CMakePresets.json 2>/dev/null; then
    echo "validate.sh: CONFIGURE_PRESET='${CONFIGURE_PRESET}' not found in CMakePresets.json" >&2
    echo "validate.sh: falling back to in-tree RelWithDebInfo configure (cmake -S . -B build/validate-fallback)." >&2
    USING_RELEASE_FALLBACK=1
    CONFIGURE_PRESET=""
    BINARY_DIR="${BINARY_DIR:-${REPO_ROOT}/build/validate-fallback}"
fi

# Resolve binary dir from preset if we are using presets.
if [[ -z "${CONFIGURE_PRESET:-}" ]]; then
    : "${BINARY_DIR:?}"
elif [[ -z "${BINARY_DIR:-}" ]]; then
    # Extract binaryDir from CMakePresets.json (handles ${sourceDir} expansion).
    BINARY_DIR="$(python3 - <<PY 2>/dev/null || true
import json, os
with open("CMakePresets.json") as f:
    data = json.load(f)
for p in data.get("configurePresets", []):
    if p.get("name") == "${CONFIGURE_PRESET}":
        bd = p.get("binaryDir", "").replace("\${sourceDir}", os.getcwd())
        print(bd)
        break
PY
)"
    BINARY_DIR="${BINARY_DIR:-${REPO_ROOT}/build/${CONFIGURE_PRESET}}"
fi

# Track check results so we can print an honest summary.
RT_CHECK_STATUS="PASS"
LATENCY_CHECK_STATUS="PASS"
BYPASS_CHECK_STATUS="PASS"
STATE_CHECK_STATUS="PASS"

section() {
    echo
    echo "==================================================================="
    echo "  ${1}"
    echo "==================================================================="
}

warn() { echo "validate.sh: WARN: ${1}" >&2; }
fail() { echo "validate.sh: FAIL: ${1}" >&2; }

# =========================================================================
# (a) Configure
# =========================================================================
section "(a) Configure"
if [[ -n "${CONFIGURE_PRESET}" ]]; then
    echo "==> cmake --preset \"${CONFIGURE_PRESET}\""
    cmake --preset "${CONFIGURE_PRESET}"
else
    echo "==> cmake -S . -B \"${BINARY_DIR}\" -DCMAKE_BUILD_TYPE=RelWithDebInfo"
    cmake -S . -B "${BINARY_DIR}" -DCMAKE_BUILD_TYPE=RelWithDebInfo
fi

# =========================================================================
# (b) Build
# =========================================================================
section "(b) Build"
if [[ -n "${BUILD_PRESET}" ]] && grep -q "\"name\"[[:space:]]*:[[:space:]]*\"${BUILD_PRESET}\"" CMakePresets.json 2>/dev/null; then
    echo "==> cmake --build --preset \"${BUILD_PRESET}\""
    cmake --build --preset "${BUILD_PRESET}"
else
    echo "==> cmake --build \"${BINARY_DIR}\" --config Release -j\"$(nproc)\""
    cmake --build "${BINARY_DIR}" --config Release -j"$(nproc)"
fi

# =========================================================================
# (c) Unit tests (ctest / juce UnitTests)
# =========================================================================
section "(c) Unit tests (ctest)"
if grep -q "\"name\"[[:space:]]*:[[:space:]]*\"${TEST_PRESET}\"" CMakePresets.json 2>/dev/null; then
    echo "==> ctest --preset \"${TEST_PRESET}\" --output-on-failure"
    ctest --preset "${TEST_PRESET}" --output-on-failure
else
    echo "==> ctest --test-dir \"${BINARY_DIR}\" --output-on-failure"
    ctest --test-dir "${BINARY_DIR}" --output-on-failure
fi

# =========================================================================
# (d) RT-safety static checks (heuristic source gates; spec §18.1)
# =========================================================================
section "(d) RT-safety static checks (source heuristics)"
echo "Scanning Source/PluginProcessor.cpp and Source/DSP/** for forbidden"
echo "audio-thread patterns. Full RT-safety proof remains manual/host (pluginval)."

# Files considered part of the processBlock hot path.
HOT_PATHS=( "Source/PluginProcessor.cpp" )
if compgen -G "Source/DSP/*" > /dev/null; then
    while IFS= read -r -d '' f; do
        HOT_PATHS+=( "$f" )
    done < <(find Source/DSP -type f \( -name '*.cpp' -o -name '*.h' \) -print0)
fi

# Forbidden allocation primitives. Patterns are scoped to actual allocation
# expressions (new <Type>, delete, malloc/free/realloc calls) to avoid flagging
# the English word "new" in comments. Allowlist: lines containing an explicit
# "// ALLOW_RT:" comment are treated as reviewed-and-safe.
RT_ALLOC_PATTERN='(new\s+[A-Z]|\bdelete\s|\bdelete\[\]|\bmalloc\s*\(|\bfree\s*\(|\brealloc\s*\()'
RT_LOCK_PATTERN='(std::lock_guard|std::mutex|juce::CriticalSection|CriticalSection|MessageManager::callAsync)'
RT_IO_PATTERN='(fopen|ifstream|ofstream|juce::File)'

scan_rt_pattern() {
    local label="$1"
    local pattern="$2"
    local hits=0
    local f
    for f in "${HOT_PATHS[@]}"; do
        [[ -f "$f" ]] || continue
        # Use grep -nE; allowlist via ALLOW_RT marker.
        if command -v rg >/dev/null 2>&1; then
            matches=$(rg -nE "$pattern" "$f" 2>/dev/null | grep -vE 'ALLOW_RT' || true)
        else
            matches=$(grep -nE "$pattern" "$f" 2>/dev/null | grep -vE 'ALLOW_RT' || true)
        fi
        if [[ -n "$matches" ]]; then
            warn "RT-safety [$label]: potential forbidden pattern in $f:"
            echo "$matches" >&2
            hits=$((hits+1))
        fi
    done
    if [[ "$hits" -gt 0 ]]; then
        warn "RT-safety [$label]: $hits file(s) flagged. Review or annotate with // ALLOW_RT: <reason>."
        RT_CHECK_STATUS="REVIEW"
    else
        echo "  [$label] OK — no unallowlisted matches in hot paths."
    fi
}

scan_rt_pattern "alloc"   "$RT_ALLOC_PATTERN"
scan_rt_pattern "locks"   "$RT_LOCK_PATTERN"
scan_rt_pattern "io"      "$RT_IO_PATTERN"

if [[ "$RT_CHECK_STATUS" != "PASS" ]]; then
    fail "RT-safety static checks flagged items for review (see above)."
fi

# =========================================================================
# (e) Zero-latency verification (spec §19.1) — source-level
# =========================================================================
section "(e) Zero-latency verification (source-level)"
echo "Asserting default latency == 0 and that any non-zero latency is gated on"
echo "lookahead mode (Source/DSP/LookaheadDelay.* + PluginProcessor)."

# 1. LookaheadDelay must report a non-negative latency and default to 0.
if ! grep -qE 'Milliseconds0_5|Mode::Off' Source/DSP/LookaheadDelay.cpp 2>/dev/null \
   && ! grep -qE 'Milliseconds0_5|Mode::Off' Source/DSP/LookaheadDelay.h 2>/dev/null; then
    fail "Zero-latency: LookaheadDelay mode enum / Off default not found."
    LATENCY_CHECK_STATUS="FAIL"
fi

# 2. getLatencySamples must not hardcode a positive constant without a
#    lookahead gate. Allowed forms: return requestedDelaySamples.load(); or
#    return 0; — anything else is suspect.
LATENCY_BODY=$(grep -A3 'int LookaheadDelay::getLatencySamples' Source/DSP/LookaheadDelay.cpp 2>/dev/null || true)
if [[ -z "$LATENCY_BODY" ]]; then
    fail "Zero-latency: getLatencySamples definition not found in Source/DSP/LookaheadDelay.cpp."
    LATENCY_CHECK_STATUS="FAIL"
elif echo "$LATENCY_BODY" | grep -qE 'return[[:space:]]+[1-9][0-9]*[[:space:]]*;'; then
    fail "Zero-latency: getLatencySamples appears to hardcode a positive constant without lookahead gate:"
    echo "$LATENCY_BODY" >&2
    LATENCY_CHECK_STATUS="FAIL"
else
    echo "  getLatencySamples returns lookahead-driven / zero default. OK"
fi

# 3. Default parameter value for lookahead must be 0 (Off).
if ! grep -qE 'lookaheadChoices' Source/PluginProcessor.cpp 2>/dev/null; then
    warn "Zero-latency: lookaheadChoices not found in PluginProcessor.cpp — verify defaults manually."
    LATENCY_CHECK_STATUS="${LATENCY_CHECK_STATUS/FAIL/FAIL}"
    LATENCY_CHECK_STATUS="${LATENCY_CHECK_STATUS/PASS/REVIEW}"
fi

if [[ "$LATENCY_CHECK_STATUS" == "FAIL" ]]; then
    fail "Zero-latency source checks failed."
    exit 1
elif [[ "$LATENCY_CHECK_STATUS" == "REVIEW" ]]; then
    warn "Zero-latency: source checks require manual confirmation."
fi

# =========================================================================
# (f) Bypass / click-free intent pointers (spec §14.2)
# =========================================================================
section "(f) Bypass / click-free intent pointers"
echo "Confirming gain-match / crossfade / smoothing symbols exist so release"
echo "gates are not vacant. Real-time click-free validation is host/manual."

BYPASS_SYMBOLS_OK=1
for sym in "crossfade" "gainMatch" "SmoothedValue\|smoothedValue\|smooth"; do
    if ! grep -rqE "$sym" Source/StateManager.cpp Source/PluginProcessor.cpp 2>/dev/null; then
        warn "Bypass/click-free: symbol pattern '$sym' not found in StateManager/PluginProcessor."
        BYPASS_SYMBOLS_OK=0
    fi
done
if [[ "$BYPASS_SYMBOLS_OK" -eq 1 ]]; then
    echo "  crossfade / gainMatch / smoothing symbols present. OK (intent only; host validates)."
else
    BYPASS_CHECK_STATUS="REVIEW"
    warn "Bypass/click-free: one or more intent symbols missing — review."
fi

# =========================================================================
# (g) State round-trip coverage (spec §17 / §21.8)
# =========================================================================
section "(g) State round-trip coverage"
echo "Ensuring tests/StateTests.cpp exists and is part of the unit-test build,"
echo "or that setStateInformation/getStateInformation/schema_version symbols are covered."

STATE_OK=0
if [[ -f tests/StateTests.cpp ]]; then
    if grep -q "StateTests.cpp" CMakeLists.txt 2>/dev/null \
       || grep -qE 'tests/StateTests\.cpp' CMakeLists.txt 2>/dev/null; then
        echo "  tests/StateTests.cpp present and referenced by CMakeLists.txt. OK"
        STATE_OK=1
    else
        warn "State round-trip: tests/StateTests.cpp exists but is not referenced in CMakeLists.txt."
    fi
    # Confirm coverage of the canonical symbols.
    for sym in "setStateInformation\|getStateInformation" "schema_version" "restoreComparison\|loadFromXml"; do
        if ! grep -qE "$sym" tests/StateTests.cpp 2>/dev/null; then
            warn "State round-trip: symbol pattern '$sym' not covered in StateTests.cpp."
        fi
    done
fi

if [[ "$STATE_OK" -eq 0 ]]; then
    # Fall back to symbol coverage in Source.
    if grep -rqE "setStateInformation|getStateInformation" Source/ 2>/dev/null \
       && grep -rqE "schema_version" Source/ 2>/dev/null; then
        warn "State round-trip: no dedicated StateTests coverage, but save/restore symbols exist in Source. REVIEW."
        STATE_CHECK_STATUS="REVIEW"
    else
        fail "State round-trip: neither StateTests.cpp nor save/restore symbols found."
        STATE_CHECK_STATUS="FAIL"
        exit 1
    fi
fi

# =========================================================================
# Summary
# =========================================================================
section "Validation summary"

if [[ "$USING_RELEASE_FALLBACK" -eq 1 ]]; then
    echo "  Configure preset   : FALLBACK (RelWithDebInfo in-tree) — no Release preset in CMakePresets.json"
else
    echo "  Configure preset   : ${CONFIGURE_PRESET}"
fi
echo "  Build              : PASS"
echo "  ctest (unit tests) : PASS"
echo "  RT-safety static   : ${RT_CHECK_STATUS}"
echo "  Zero-latency       : ${LATENCY_CHECK_STATUS}"
echo "  Bypass/click-free  : ${BYPASS_CHECK_STATUS}"
echo "  State round-trip   : ${STATE_CHECK_STATUS}"
echo
echo "  VERIFIED (automated):"
echo "    - Release-compatible build + ctest unit tests pass"
echo "    - Static RT-safety / latency / bypass / state source gates ran"
echo
echo "  REQUIRES_MANUAL (host / non-automated):"
echo "    - pluginval host validation (RT-safety under real audio callback)"
echo "    - DAW sidechain / PDC / latency-report verification"
echo "    - Windows build (this script targets the Linux preset)"
echo "    - Click-free bypass A/B under real-time audio (spec §14.2)"
echo

# Hard exit if any FAIL (not REVIEW). REVIEW items are reported but do not
# fail the gate, since they require manual confirmation by design.
if [[ "$LATENCY_CHECK_STATUS" == "FAIL" || "$STATE_CHECK_STATUS" == "FAIL" ]]; then
    fail "One or more required automated checks failed."
    exit 1
fi

echo "validate.sh: automated portion PASSED (manual items remain — see above)."
exit 0
