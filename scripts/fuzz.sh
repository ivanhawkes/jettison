#!/usr/bin/env bash
#
# Fuzzes one target of the clang-fuzz tree for a while (docs/build-and-gates.md, "Fuzzing"), holding
# the heavy lock: configures the tree if there is none, builds the target, and runs libFuzzer over
# the target's working corpus, build/clang-fuzz/fuzz/<target>/corpus, which keeps every input that
# reached new code for the next run, and its seeds. An input that crashes, leaks or runs past the
# timeout is written beside the corpus as crash-<sha1>, leak-<sha1> or timeout-<sha1>; the target
# given that file alone runs it again.
#
# Usage: scripts/fuzz.sh <target> [seconds]    the target for that long (default 60)
#        scripts/fuzz.sh --list                 the targets
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.."

targets=(gltf pack ktx2 image save net collision clip strings script audio font rml rcss)
usage() { echo "usage: scripts/fuzz.sh <target> [seconds] | --list" >&2; exit 2; }
[ "$#" -ge 1 ] || usage
if [ "$1" = "--list" ]; then
	printf '%s\n' "${targets[@]}"
	exit 0
fi
target="$1"
seconds="${2:-60}"
[[ " ${targets[*]} " == *" $target "* ]] || { echo "fuzz: no target '$target' (--list)" >&2; exit 2; }
[[ "$seconds" =~ ^[0-9]+$ ]] || usage

# One gate or build at a time across every worktree (scripts/heavy.sh).
[ -n "${JETTISON_HEAVY_HELD:-}" ] || exec scripts/heavy.sh scripts/fuzz.sh "$@"

tree=build/clang-fuzz
[ -f "$tree/CMakeCache.txt" ] || cmake --preset clang-fuzz
cmake --build --preset clang-fuzz --target "jettison_fuzz_$target"

work="$tree/fuzz/$target"
mkdir -p "$work/corpus"
# Ten seconds an input is far past what any seed takes; a slower one is a hang, reported as a timeout.
options=(-max_total_time="$seconds" -timeout=10 -rss_limit_mb=2048 -print_final_stats=1
	-artifact_prefix="$work/")
dictionary="source/test/fuzz/dictionaries/$target.dict"
[ -f "$dictionary" ] && options+=(-dict="$dictionary")
"$tree/source/test/fuzz/jettison_fuzz_$target" "${options[@]}" "$work/corpus" \
	"$tree/source/test/fuzz/seeds/$target"
