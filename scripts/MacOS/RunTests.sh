#!/usr/bin/env bash
# Run every built test binary (unix counterpart of the generated run_tests.bat).
# Called by the RunAllTests utility project; can also be run by hand.
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
