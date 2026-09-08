#!/usr/bin/env bash
# Run every built test binary. Unix counterpart of scripts/Windows/RunTests.ps1.
#
# Called by ci.yml's "Run tests" steps, and by hand. NOT by a postbuild: the RunAllTests project used
# to list this script in `postbuildcommands` and that line never executed once — see the note in
# Desert/Tests/premake5.lua where it was removed.
#
# THIS RUNNER GLOBS; THE WINDOWS ONE DOES NOT. It runs whatever executables it finds in $TEST_DIR,
# so a suite that stopped LINKING is silently not run rather than reported. The Windows side reads
# build/TestManifest.txt — the list of suites premake generated projects for — and fails on a name
# with no binary behind it. Closing the gap here means deciding what a manifest entry means on a
# platform where a suite may legitimately not be built, which is a separate change with its own
# verdict; until then this asymmetry is a known hole and not an oversight.
#
# Usage: scripts/MacOS/RunTests.sh <workspace-root> <config>
set -uo pipefail

ROOT="${1:?workspace root required}"
CONFIG="${2:-Debug}"

TEST_DIR="$ROOT/build/Bin/Tests/$CONFIG"
REPORT_DIR="$ROOT/build/TestReports"
mkdir -p "$REPORT_DIR"

echo "===== Starting Tests ====="
ERROR=0

if [ ! -d "$TEST_DIR" ]; then
    echo "[ERROR] test dir not found: $TEST_DIR"
    exit 1
fi

for test_bin in "$TEST_DIR"/*; do
    [ -f "$test_bin" ] && [ -x "$test_bin" ] || continue
    name="$(basename "$test_bin")"
    echo "[TEST] $name"
    if ! "$test_bin" --gtest_output="xml:$REPORT_DIR/$name.xml"; then
        echo "[FAIL] $name"
        ERROR=1
    fi
done

echo "===== Test Results ====="
if [ "$ERROR" -eq 0 ]; then
    echo "ALL TESTS PASSED"
else
    echo "SOME TESTS FAILED"
fi
exit "$ERROR"
