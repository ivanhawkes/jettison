#!/usr/bin/env bash
#
# A Claude Code session in this repository, recorded with asciinema to asciinema/jettison-NNNN.cast here,
# numbered one past the highest recording already there (jettison-0000.cast first). The session is
# named jettison-NNNN after its recording, so the two can be matched later. The recordings are
# committed, a permanent record of how the project is developed: when the session ends its recording
# is committed whole, on its own, whatever else is staged; pushing is left to you.
#
#   scripts/record-claude.sh [claude arguments...]
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.."

directory="$PWD/asciinema"
mkdir -p "$directory"

next=0
shopt -s nullglob
for recording in "$directory"/jettison-[0-9][0-9][0-9][0-9]*.cast; do
    number="${recording##*/jettison-}"
    number="${number%.cast}"
    [[ $number =~ ^[0-9]+$ ]] || continue
    ((10#$number >= next)) && next=$((10#$number + 1))
done
printf -v name 'jettison-%04d' "$next"

# asciinema runs the command through sh -c, so each argument goes in quoted.
printf -v command '%q ' claude -n "$name" "$@"
recording="asciinema/$name.cast"
# asciinema refuses to overwrite a file, so two sessions started at once cannot share one.
status=0
asciinema rec --command "$command" "$recording" || status=$?

if [[ -f $recording ]]; then
    git add -- "$recording"
    # --only commits this path alone, leaving anything else staged as it was.
    git commit --quiet --only --message "Recording: $name" -- "$recording" ||
        echo "record-claude.sh: $recording is not committed" >&2
fi
exit "$status"
