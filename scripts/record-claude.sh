#!/usr/bin/env bash
#
# A Claude Code session in this repository, recorded with asciinema to ~/asciinema/jettison-NNNN.cast,
# numbered one past the highest recording there or already shipped to asciinema/ here
# (jettison-0000.cast first). The session is named jettison-NNNN after its recording, so the two can be
# matched later. Recordings stay outside the repository while you work; you move them into asciinema/
# here and commit them in batches.
#
#   scripts/record-claude.sh [claude arguments...]
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.."

directory="$HOME/asciinema"
mkdir -p "$directory"

next=0
shopt -s nullglob
for recording in "$directory"/jettison-[0-9][0-9][0-9][0-9]*.cast \
    asciinema/jettison-[0-9][0-9][0-9][0-9]*.cast; do
    number="${recording##*/jettison-}"
    number="${number%.cast}"
    [[ $number =~ ^[0-9]+$ ]] || continue
    ((10#$number >= next)) && next=$((10#$number + 1))
done
printf -v name 'jettison-%04d' "$next"

# asciinema runs the command through sh -c, so each argument goes in quoted.
printf -v command '%q ' claude -n "$name" "$@"
recording="$directory/$name.cast"
# asciinema refuses to overwrite a file, so two sessions started at once cannot share one.
exec asciinema rec --command "$command" "$recording"
