#!/usr/bin/env bash
# ProtoGL runtime shader-compiler gate — compiles AND runs
# check_shader_compile.cpp with native g++ (unlike run_check.sh, which is
# -fsyntax-only). Covers the PGLSL → PSB register allocator: all 8 stock
# shaders must compile within the r8–r27 user bank, and a deliberately
# over-deep expression must still fail with "register allocation overflow".
#
# Usage (from the repo root or anywhere else):
#   ./tests/syntax_check/run_shader_compile.sh
#
# Exit code: 0 = build + all runtime checks pass, 1 otherwise.

set -u

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"

CXX="${CXX:-g++}"
CXXFLAGS="-std=gnu++17 -Wall -Wextra -I$REPO_ROOT/src"
BIN="${TMPDIR:-/tmp}/protogl_check_shader_compile"

echo "ProtoGL shader compile runtime check"
echo "  compiler: $CXX"
echo "  flags:    $CXXFLAGS"
echo

if ! command -v "$CXX" >/dev/null 2>&1; then
    echo "ERROR: $CXX not found (set CXX to override)"
    exit 1
fi

if ! "$CXX" $CXXFLAGS "$SCRIPT_DIR/check_shader_compile.cpp" -o "$BIN"; then
    echo "RESULT: FAIL (build error)"
    exit 1
fi

"$BIN" "$REPO_ROOT"
rc=$?

echo
if [ $rc -eq 0 ]; then
    echo "RESULT: PASS"
else
    echo "RESULT: FAIL"
fi
exit $rc
