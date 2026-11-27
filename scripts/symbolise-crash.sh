#!/usr/bin/env bash
#
# Names the frames of a crash report written where there was no symboliser, a player's machine, against
# the build that wrote it (Platform::InstallCrashHandler). The report's frames are "#NN address
# module+offset"; each is given to addr2line with the module's debug file found by the build ID the
# report lists for it, as <directory>/.build-id/<first two digits>/<the rest>.debug, the layout a
# stripped release's kept debug files have; else the module found in the directory by its file name;
# else the module at the path the report has.
#
#   scripts/symbolise-crash.sh <report> [directory of the build's executables, libraries or debug files]
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
		id=$(grep -F "  $module build-id " "$report" | head -n 1 | sed 's/.* build-id //')
		if [ -n "$id" ] && [ -f "$build/.build-id/${id:0:2}/${id:2}.debug" ]; then
			module=$build/.build-id/${id:0:2}/${id:2}.debug
		else
			found=$(find "$build" -name "$(basename "$module")" -type f -print -quit)
			[ -n "$found" ] && module=$found
		fi
	fi
	if [ ! -f "$module" ]; then
		echo "$frame  $location: no such module here"
		continue
	fi
	printf '%s  ' "$frame"
	addr2line -Cfip -e "$module" "$(printf '0x%x' "$offset")" 2>/dev/null
done
