#!/usr/bin/env bash
# ProtoGL CRC-16 table-equivalence test — runnable regression gate.
#
# Compiles, links and EXECUTES check_crc16.cpp with the native desktop g++:
# proves the table-driven PglCRC16 (F-02) is bit-identical to the original
# bit-by-bit algorithm (all 256 byte values, chained updates over a 1 KB
# pseudo-random buffer, and the "123456789" = 0x29B1 check vector).
#
# Run after any change to PglCRC16.h, together with run_check.sh and
# run_roundtrip.sh.
#
# Usage (from the repo root or anywhere else):
#   ./tests/syntax_check/run_crc16.sh
#
# Exit code: 0 if the equivalence checks pass, 1 otherwise.

set -u

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"

CXX="${CXX:-g++}"
CXXFLAGS="-std=gnu++17 -Wall -Wextra -O2 -I$REPO_ROOT/src"
BIN="$(mktemp /tmp/protogl_crc16.XXXXXX)"
trap 'rm -f "$BIN"' EXIT

echo "ProtoGL CRC-16 table-equivalence test"
echo "  compiler: $CXX"
echo "  flags:    $CXXFLAGS"
echo

if ! command -v "$CXX" >/dev/null 2>&1; then
    echo "ERROR: $CXX not found (set CXX to override)"
    exit 1
fi

out=$("$CXX" $CXXFLAGS "$SCRIPT_DIR/check_crc16.cpp" -o "$BIN" 2>&1)
if [ $? -ne 0 ]; then
    echo "[g++] FAIL build check_crc16.cpp"
    if [ -n "$out" ]; then
        echo "$out" | sed 's/^/    /'
    fi
    echo
    echo "RESULT: FAIL (build)"
    exit 1
fi
echo "[g++] PASS build check_crc16.cpp"
if [ -n "$out" ]; then
    echo "$out" | sed 's/^/    /'
fi
echo

"$BIN"
rc=$?

exit $rc
