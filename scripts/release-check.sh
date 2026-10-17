#!/usr/bin/env bash
# Release gate: every stage must pass before a release is tagged. A failing test, a sanitizer report, a fuzz input
# that crashes or runs out of time or memory, or a failing conformance procedure exits nonzero.
#
#   scripts/release-check.sh [stage...]       stages: unit fuzz conformance python (default: all)
#
# Environment:
#   OUT            where builds, logs and reports go (default: build-release-check)
#   FUZZ_SECONDS   how long each fuzzer runs after replaying its corpus (default: 300)
#   JOBS           parallel build jobs (default: nproc)
#   MVN            the Maven command (default: mvn)
#
# Needs: cmake, clang (fuzzers), OpenSSL headers, JDK 17+ and mvn (conformance), python3 (python).
set -euo pipefail

ROOT=$(cd "$(dirname "$0")/.." && pwd)
OUT=$(mkdir -p "${OUT:-$ROOT/build-release-check}" && cd "${OUT:-$ROOT/build-release-check}" && pwd)
FUZZ_SECONDS=${FUZZ_SECONDS:-300}
JOBS=${JOBS:-$(nproc)}
MVN=${MVN:-mvn}
STAGES=("$@")
[ ${#STAGES[@]} -eq 0 ] && STAGES=(unit fuzz conformance python)

SANITIZERS="-fsanitize=address,undefined -fno-sanitize-recover=undefined -fno-omit-frame-pointer"
export ASAN_OPTIONS=${ASAN_OPTIONS:-detect_leaks=0:abort_on_error=1}
export UBSAN_OPTIONS=${UBSAN_OPTIONS:-print_stacktrace=1:halt_on_error=1}

log() { echo "==> $*"; }
fail() { echo "FAILED: $*" >&2; exit 1; }

# ---- unit: unit, ASIO and integration tests under ASan and UBSan ----
stage_unit() {
    local b=$OUT/unit
    log "unit: building with ASan/UBSan in $b"
    cmake -S "$ROOT" -B "$b" -DCMAKE_BUILD_TYPE=Debug -DDNP3_TESTS=ON -DDNP3_TLS=ON \
        -DCMAKE_CXX_FLAGS="-g $SANITIZERS" -DCMAKE_EXE_LINKER_FLAGS="$SANITIZERS" \
        -DCMAKE_SHARED_LINKER_FLAGS="$SANITIZERS" > "$b.cmake.log" 2>&1 || fail "unit: configure (see $b.cmake.log)"
    cmake --build "$b" --parallel "$JOBS" > "$b.build.log" 2>&1 || fail "unit: build (see $b.build.log)"
    log "unit: running"
    "$b/cpp/tests/unit/unittests" || fail "unit: unittests"
    (cd "$b/cpp/tests/asiotests" && ./asiotests) || fail "unit: asiotests"
    "$b/cpp/tests/integration/integrationtests" || fail "unit: integrationtests"
}

# ---- fuzz: replay the committed corpora, then fuzz each target for FUZZ_SECONDS ----
stage_fuzz() {
    local b=$OUT/fuzz
    log "fuzz: building with clang, libFuzzer, ASan/UBSan in $b"
    CC=clang CXX=clang++ cmake -S "$ROOT" -B "$b" -DCMAKE_BUILD_TYPE=Debug -DDNP3_FUZZING=ON -DDNP3_LIBFUZZER=ON \
        -DDNP3_TLS=OFF -DDNP3_STATIC_LIBS=ON \
        -DCMAKE_CXX_FLAGS="-g -O1 $SANITIZERS -fsanitize=fuzzer-no-link" -DCMAKE_EXE_LINKER_FLAGS="$SANITIZERS" \
        > "$b.cmake.log" 2>&1 || fail "fuzz: configure (see $b.cmake.log)"
    local targets=(fuzzdecoder fuzzdecoder_logging fuzzoutstation fuzzmaster fuzzoutstation_session fuzzmaster_session
        fuzzroundtrip fuzzroundtrip_events fuzzroundtrip_commands fuzzroundtrip_eventbuffer)
    cmake --build "$b" --parallel "$JOBS" --target "${targets[@]}" > "$b.build.log" 2>&1 \
        || fail "fuzz: build (see $b.build.log)"

    for t in "${targets[@]}"; do
        # the session fuzzers have seeds of their own; the others take the link frames of corpus/
        local seeds=$ROOT/cpp/tests/fuzz/seeds/$t
        [ -d "$seeds" ] || seeds=$ROOT/cpp/tests/fuzz/corpus
        local corpus=$OUT/fuzz-corpus/$t artifacts=$OUT/fuzz-artifacts/$t
        mkdir -p "$corpus" "$artifacts"
        log "fuzz: $t: replaying $seeds"
        "$b/cpp/tests/fuzz/$t" -runs=0 -timeout=10 -rss_limit_mb=2048 -artifact_prefix="$artifacts/" "$seeds" \
            > "$OUT/fuzz-$t-replay.log" 2>&1 || fail "fuzz: $t replay (see $OUT/fuzz-$t-replay.log, $artifacts)"
        log "fuzz: $t: fuzzing for ${FUZZ_SECONDS}s"
        "$b/cpp/tests/fuzz/$t" -max_total_time="$FUZZ_SECONDS" -timeout=10 -rss_limit_mb=2048 -max_len=4096 \
            -print_final_stats=1 -artifact_prefix="$artifacts/" "$corpus" "$seeds" \
            > "$OUT/fuzz-$t.log" 2>&1 || fail "fuzz: $t found a crash (see $OUT/fuzz-$t.log, $artifacts)"
    done
}

# ---- conformance: dnp3c, every revision and subset level the outstation is tested at ----
run_dnp3c() {
    local rev=$1 config=$2 tag=$3 absent=${4:-}
    log "conformance: $rev $tag"
    (cd "$ROOT/conformance" && YADNP3_ABSENT=$absent \
        YADNP3_OUTSTATION=$OUT/conformance-build/cpp/tests/conformance/conformance-outstation \
        "$MVN" -q -B compile scala:run -DlauncherId=conformance -Dconformance.revision="$rev" \
        -Dconformance.config="$config" -Dconformance.run=all) > "$OUT/conformance-$rev-$tag.log" 2>&1 \
        || fail "conformance: $rev $tag (see $OUT/conformance-$rev-$tag.log and conformance/results)"
    grep -E 'Passed procedure' -c "$OUT/conformance-$rev-$tag.log" | sed "s/^/    passed: /"
    grep -A20 'passed with warnings' "$OUT/conformance-$rev-$tag.log" | grep -E '^\s+[0-9]' || true
}

stage_conformance() {
    local b=$OUT/conformance-build
    log "conformance: building the outstation in $b"
    cmake -S "$ROOT" -B "$b" -DCMAKE_BUILD_TYPE=Release -DDNP3_TESTS=ON > "$b.cmake.log" 2>&1 \
        || fail "conformance: configure (see $b.cmake.log)"
    cmake --build "$b" --parallel "$JOBS" --target conformance-outstation > "$b.build.log" 2>&1 \
        || fail "conformance: build (see $b.build.log)"

    local ini=$ROOT/conformance/device.ini
    # level 1 has no frozen counters; level 3 needs the packed double-bit variation (g3v1), which the library lacks
    sed -e 's/^outstation.subset.level = 2/outstation.subset.level = 1/' \
        -e 's/^supports.frozenCounter = true/supports.frozenCounter = false/' \
        -e 's/^supports.disable.frozenCounter = true/supports.disable.frozenCounter = false/' \
        -e 's/^supports.frozenCounterChangeEvents = true/supports.frozenCounterChangeEvents = false/' \
        "$ini" > "$OUT/device-l1.ini"
    sed -e 's/^outstation.subset.level = 2/outstation.subset.level = 3/' "$ini" > "$OUT/device-l3.ini"
    echo "test.exclude.3.1r1/8.24.2b = no packed double-bit input variation (g3v1)" >> "$OUT/device-l3.ini"

    run_dnp3c 3.1r1 "$ini" L2
    run_dnp3c 3.1r1 "$OUT/device-l1.ini" L1 fcnt
    run_dnp3c 3.1r1 "$OUT/device-l3.ini" L3
    run_dnp3c 2.7 "$ini" L2
}

# ---- python: build the bindings and run their tests ----
stage_python() {
    local venv=$OUT/venv
    log "python: building the bindings in $venv"
    python3 -m venv "$venv"
    "$venv/bin/pip" install -q pytest > "$OUT/python-install.log" 2>&1
    "$venv/bin/pip" install -q "$ROOT/python" >> "$OUT/python-install.log" 2>&1 \
        || fail "python: build (see $OUT/python-install.log)"
    (cd "$OUT" && "$venv/bin/python" -m pytest -q "$ROOT/python/tests") || fail "python: tests"
}

for s in "${STAGES[@]}"; do
    case $s in
    unit) stage_unit ;;
    fuzz) stage_fuzz ;;
    conformance) stage_conformance ;;
    python) stage_python ;;
    *) fail "unknown stage: $s" ;;
    esac
done
log "release check passed: ${STAGES[*]}"
