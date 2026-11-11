#!/usr/bin/env bash
# tidy.sh - run clang-tidy over the project's own sources from a compilation database.
#
# Checks come from .clang-tidy, curated for this tree. The path filter keeps the
# FetchContent dependencies out of the run, and HeaderFilterRegex in the config keeps
# their headers out of the diagnostics.
#
# Checking runs scripts/tidy.py: every core (JETTISON_TIDY_JOBS to change it), and only the units
# whose sources, headers, compile command or checks changed since they last came out clean, from a
# cache outside the tree (~/.cache/jettison/tidy, or JETTISON_TIDY_CACHE).
#
# Usage: scripts/tidy.sh [--build-dir DIR] [--summary] [--all] [--fix]
#
#  --build-dir DIR  tree with compile_commands.json (default: build/clang-debug), built, since the
#                   cache reads the headers each unit includes from its dependency record
#  --summary        print the summary lines only, not the diagnostics
#  --all            check every unit, cached as clean or not
#  --fix            apply safe fixes in place, over every unit; start from a clean tree to review them
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.."

build_dir="build/clang-debug"
summary=0
fix=0
summary_flag=()
all_flag=()

while [ $# -gt 0 ]; do
	case "$1" in
		--build-dir)
			[ $# -ge 2 ] || { echo "tidy: --build-dir needs an argument" >&2; exit 2; }
			build_dir="$2"
			shift 2
			;;
		--summary)
			summary=1
			summary_flag=(--summary)
			shift
			;;
		--all)
			all_flag=(--all)
			shift
			;;
		--fix)
			fix=1
			shift
			;;
		-h | --help)
			sed -n '2,18p' "${BASH_SOURCE[0]}"
			exit 0
			;;
		*)
			echo "tidy: unknown argument $1 (see --help)" >&2
			exit 2
			;;
	esac
done

if [ "$fix" -eq 0 ]; then
	# Checking: every core, and only the units that changed since they were last clean (tidy.py).
	exec python3 scripts/tidy.py --build-dir "$build_dir" ${summary_flag[@]+"${summary_flag[@]}"} ${all_flag[@]+"${all_flag[@]}"}
fi

if [ ! -f "$build_dir/compile_commands.json" ]; then
	echo "tidy: $build_dir has no compile_commands.json; run 'cmake --preset clang-debug'" >&2
	exit 1
fi

if [ ! -x "$(command -v run-clang-tidy)" ]; then
	echo "tidy: run-clang-tidy not on PATH; run 'devenv' (clang-tools) first" >&2
	exit 1
fi

# Fixing goes through run-clang-tidy, which collects every unit's fixes and applies them once, so two
# units fixing one header do not write over each other. Every unit is checked: a fix is a decision
# about the whole tree.
set +e
run-clang-tidy -p "$build_dir" -quiet -j "${JETTISON_TIDY_JOBS:-$(nproc)}" -fix 'source/(engine|tools|samples|test)/.*\.(cpp|h)$'
status=$?
set -e
exit "$status"
