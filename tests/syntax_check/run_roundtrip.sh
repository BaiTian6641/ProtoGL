#!/usr/bin/env bash
# ProtoGL wire-format round-trip smoke test — runnable regression gate.
#
# Compiles, links and EXECUTES check_roundtrip.cpp with the native desktop
# g++: it encodes a frame with PglEncoder and parses it back with PglParser,
# verifying opcode sequence, payload fields and CRC accept/reject behavior.
#
# This complements run_check.sh (which is -fsyntax-only). Run both after any
# change to the wire format (PglTypes.h / PglOpcodes.h / PglCRC16.h /
# PglEncoder.h / PglParser.h) — e.g. the planned V8 protocol work.
#
# Usage (from the repo root or anywhere else):
#   ./tests/syntax_check/run_roundtrip.sh
#
# Exit code: 0 if the round-trip passes, 1 otherwise.

set -u

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"

CXX="${CXX:-g++}"
CXXFLAGS="-std=gnu++17 -Wall -Wextra -O2 -I$REPO_ROOT/src"
BIN="$(mktemp /tmp/protogl_roundtrip.XXXXXX)"
trap 'rm -f "$BIN"' EXIT

echo "ProtoGL wire-format round-trip test"
echo "  compiler: $CXX"
echo "  flags:    $CXXFLAGS"
echo

if ! command -v "$CXX" >/dev/null 2>&1; then
    echo "ERROR: $CXX not found (set CXX to override)"
    exit 1
fi

out=$("$CXX" $CXXFLAGS "$SCRIPT_DIR/check_roundtrip.cpp" -o "$BIN" 2>&1)
if [ $? -ne 0 ]; then
    echo "[g++] FAIL build check_roundtrip.cpp"
    if [ -n "$out" ]; then
        echo "$out" | sed 's/^/    /'
    fi
    echo
    echo "RESULT: FAIL (build)"
    exit 1
fi
echo "[g++] PASS build check_roundtrip.cpp"
if [ -n "$out" ]; then
    echo "$out" | sed 's/^/    /'
fi
echo

"$BIN"
rc=$?

exit $rc
