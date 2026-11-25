#!/usr/bin/env bash
#
# Runs a command holding the machine's lock for heavy work, so the builds, suites and gates of
# different worktrees (the lanes: docs/build-and-gates.md, "Lanes") take turns rather than share the
# cores and the memory: two full-width builds side by side thrash when the local AI is resident. One
# lock serves every lane of Jettison and of Manifest; scripts/ci.sh takes it itself.
#
# Usage: scripts/heavy.sh <command> [arguments...]
#
# The command inherits the lock's descriptor, so a process it leaves running (an Xvfb a killed run
# left) holds the lock until that process ends.
set -euo pipefail

[ "$#" -gt 0 ] || { echo "usage: scripts/heavy.sh <command> [arguments...]" >&2; exit 2; }
# Already held by the run that started this one: a second open of the file would wait on itself.
[ -z "${JETTISON_HEAVY_HELD:-}" ] || exec "$@"

lock="${JETTISON_HEAVY_LOCK:-${XDG_CACHE_HOME:-$HOME/.cache}/jettison/heavy.lock}"
mkdir -p "$(dirname "$lock")"
exec 9>>"$lock"
if ! flock -n 9; then
	echo "heavy: waiting for $lock, held by another worktree's build or gate" >&2
	flock 9
fi
export JETTISON_HEAVY_HELD=1
exec "$@"
