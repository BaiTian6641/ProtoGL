#!/usr/bin/env bash
# ProtoGL syntax/AST compile check — portable C++17 gate.
#
# Compiles every check TU with the NATIVE desktop g++ in -fsyntax-only mode.
# No ESP toolchain and no ESP-IDF headers are involved: ARDUINO_ARCH_ESP32 is
# intentionally NOT defined, so this verifies that the library headers (minus
# the guarded ESP32 transport paths) are clean portable C++17, matching the
# library.json build flags (-std=gnu++17).
#
# No code is linked or executed — each TU is checked with -fsyntax-only.
# (For the runnable wire-format round-trip smoke test, see run_roundtrip.sh.)
#
# Usage (from the repo root or anywhere else):
#   ./tests/syntax_check/run_check.sh
#
# Exit code: 0 if every TU passes, 1 otherwise.

set -u

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"

CXX="${CXX:-g++}"
CXXFLAGS="-std=gnu++17 -fsyntax-only -Wall -Wextra -I$REPO_ROOT/src"

TUS="check.cpp check_shaders.cpp"
FAILURES=0

echo "ProtoGL syntax check (native portable C++17)"
echo "  compiler: $CXX"
echo "  flags:    $CXXFLAGS"
echo

if ! command -v "$CXX" >/dev/null 2>&1; then
    echo "ERROR: $CXX not found (set CXX to override)"
    exit 1
fi

for tu in $TUS; do
    out=$("$CXX" $CXXFLAGS "$SCRIPT_DIR/$tu" 2>&1)
    if [ $? -eq 0 ]; then
        echo "[g++] PASS $tu"
    else
        echo "[g++] FAIL $tu"
        FAILURES=$((FAILURES + 1))
    fi
    if [ -n "$out" ]; then
        echo "$out" | sed 's/^/    /'
    fi
done

echo
if [ "$FAILURES" -eq 0 ]; then
    echo "RESULT: PASS (all TUs)"
    exit 0
else
    echo "RESULT: FAIL ($FAILURES failing check(s))"
    exit 1
fi
