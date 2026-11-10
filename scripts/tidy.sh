#!/usr/bin/env bash
# tidy.sh - run clang-tidy over the project's own sources from a compilation database.
#
# Checks come from .clang-tidy, curated for this tree. The path filter keeps the
# FetchContent dependencies out of the run, and HeaderFilterRegex in the config keeps
# their headers out of the diagnostics.
#
# Usage: scripts/tidy.sh [--build-dir DIR] [--summary] [--fix]
#
#  --build-dir DIR  tree with compile_commands.json (default: build/clang-debug)
#  --summary        print the summary line only, not the diagnostics
#  --fix            apply safe fixes in place; start from a clean tree to review them
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.."

build_dir="build/clang-debug"
summary=0
fix=0

while [ $# -gt 0 ]; do
	case "$1" in
		--build-dir)
			[ $# -ge 2 ] || { echo "tidy: --build-dir needs an argument" >&2; exit 2; }
			build_dir="$2"
			shift 2
			;;
		--summary)
			summary=1
			shift
			;;
		--fix)
			fix=1
			shift
			;;
		-h | --help)
			sed -n '2,12p' "${BASH_SOURCE[0]}"
			exit 0
			;;
		*)
			echo "tidy: unknown argument $1 (see --help)" >&2
			exit 2
			;;
	esac
done

if [ ! -f "$build_dir/compile_commands.json" ]; then
	echo "tidy: $build_dir has no compile_commands.json; run 'cmake --preset clang-debug'" >&2
	exit 1
fi

if [ ! -x "$(command -v run-clang-tidy)" ]; then
	echo "tidy: run-clang-tidy not on PATH; run 'devenv' (clang-tools) first" >&2
	exit 1
fi

args=(-p "$build_dir" -quiet -j "${JETTISON_TIDY_JOBS:-2}")
[ "$fix" -eq 1 ] && args+=(-fix)

# Only our sources: the dependency tree is third-party code, not ours to lint.
# run-clang-tidy exits non-zero when it emits diagnostics, so take its output and decide
# here what a failure means.
set +e
output="$(run-clang-tidy "${args[@]}" 'source/(renderer|flotsam|jetsam|test)/.*\.(cpp|h)$' 2>&1)"
status=$?
set -e

if [ "$status" -ne 0 ]; then
	printf '%s\n' "$output" >&2
	echo "tidy: run-clang-tidy failed (exit $status)" >&2
	exit "$status"
fi

diagnostics="$(printf '%s\n' "$output" | grep -E '^[^ ].*: warning:|^[^ ].*: error:' | grep -v 'clang-tidy' || true)"
count=0
[ -n "$diagnostics" ] && count="$(printf '%s\n' "$diagnostics" | wc -l | tr -d ' ')"

if [ "$summary" -eq 0 ]; then
	printf '%s\n' "$output"
fi

if [ "$count" -eq 0 ]; then
	echo "tidy: clean (.clang-tidy over $build_dir)"
	exit 0
fi

printf '%s\n' "$diagnostics" | sed -E 's/^[^ ]*: (warning|error): //' |
	grep -oE '\[[a-z0-9.-]+\]$' | sort | uniq -c | sort -rn | sed 's/^/tidy: /'
echo "tidy: $count diagnostics; see above (checks in .clang-tidy)"
exit 1
