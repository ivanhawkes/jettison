#!/usr/bin/env bash
#
# Names the frames of a crash report written where there was no symboliser, a player's machine, against
# the build that wrote it (Platform::InstallCrashHandler). The report's frames are "#NN address
# module+offset"; each is given to addr2line with the module found in the directory given, by its
# file name, or at the path the report has.
#
#   scripts/symbolise-crash.sh <report> [directory of the build's executables and libraries]
#
# A return address is the instruction after its call, so the address named is the one before it, as
# the handler names frames: the line of the call, not of what follows it. Frame 0 is the instruction
# the signal stopped, named as it is.

set -euo pipefail

if [ $# -lt 1 ] || [ $# -gt 2 ]; then
	echo "Usage: $0 <report> [build directory]" >&2
	exit 2
fi
report=$1
build=${2:-}
command -v addr2line >/dev/null || { echo "$0: addr2line is not on PATH" >&2; exit 1; }

grep -E '^  #[0-9]+ 0x[0-9a-f]+  .+\+0x[0-9a-f]+$' "$report" | while read -r frame _ location; do
	module=${location%+0x*}
	offset=$((16#${location##*+0x}))
	[ "$frame" = "#00" ] || offset=$((offset - 1))
	if [ -n "$build" ]; then
		found=$(find "$build" -name "$(basename "$module")" -type f -print -quit)
		[ -n "$found" ] && module=$found
	fi
	if [ ! -f "$module" ]; then
		echo "$frame  $location: no such module here"
		continue
	fi
	printf '%s  ' "$frame"
	addr2line -Cfip -e "$module" "$(printf '0x%x' "$offset")" 2>/dev/null
done
