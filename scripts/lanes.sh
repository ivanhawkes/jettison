#!/usr/bin/env bash
#
# Every lane of Jettison and Manifest (docs/build-and-gates.md, "Lanes"): its branch, how far it is
# from feature-base, the files its work changes against where it left feature-base, committed or
# not, and its note (build/lane.md). A free lane is detached at feature-base with nothing changed;
# the files are what a new task's plan checks for clashes.
#
# Usage: scripts/lanes.sh
set -euo pipefail

projects="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
for repo in jettison manifest; do
	for lane in "" .lane-b .lane-c; do
		dir="$projects/$repo$lane"
		[ -d "$dir" ] || continue
		on="$(git -C "$dir" symbolic-ref --short -q HEAD ||
			echo "detached at $(git -C "$dir" rev-parse --short HEAD)")"
		ahead="$(git -C "$dir" rev-list --count feature-base..HEAD)"
		behind="$(git -C "$dir" rev-list --count HEAD..feature-base)"
		echo "== $repo${lane:- (primary)}: $on, $ahead ahead of feature-base, $behind behind"
		[ ! -f "$dir/build/lane.md" ] || grep -v '^#' "$dir/build/lane.md" | sed '/^$/d; s/^/   note: /'
		fork="$(git -C "$dir" merge-base HEAD feature-base)"
		{ git -C "$dir" diff --name-only "$fork"; git -C "$dir" ls-files -o --exclude-standard; } |
			sort -u | sed 's/^/   /'
	done
done
