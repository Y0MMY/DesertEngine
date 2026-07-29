#!/usr/bin/env bash
# Diff-based clang-format gate: only the lines CHANGED vs the base must satisfy .clang-format.
# Deliberately NOT a full-tree check — a large share of the codebase predates the config, so a
# whole-repo --Werror would fail on legacy noise forever. The rule this enforces instead:
# code you TOUCH becomes clean; the tree converges over time.
#
# Usage: scripts/CI/CheckFormat.sh [<base-ref-or-sha>]
#   CI passes the PR base sha (pull_request) or the pre-push sha (push);
#   locally, run it bare to check your work against origin/dev.
set -euo pipefail
cd "$(dirname "$0")/../.."

BASE_INPUT="${1:-origin/dev}"

# Resolve a usable base: the given ref, else HEAD~1 (first pushes send an all-zero 'before' sha).
if git rev-parse --verify -q "$BASE_INPUT^{commit}" >/dev/null 2>&1; then
    BASE=$(git merge-base HEAD "$BASE_INPUT" 2>/dev/null || echo "$BASE_INPUT")
else
    BASE=$(git rev-parse HEAD~1 2>/dev/null || git rev-parse HEAD)
fi

OUT=$(git clang-format --diff "$BASE" -- '*.cpp' '*.hpp' 2>&1 || true)

if [ -z "$OUT" ] || echo "$OUT" | grep -qE "(no modified files to format|did not modify any files)"; then
    echo "clang-format: changed lines are clean (vs $BASE)"
    exit 0
fi

echo "$OUT"
echo ""
echo "clang-format violations in the lines you changed."
echo "Fix locally with:  git clang-format $BASE   (then review + commit the touch-ups)"
exit 1
