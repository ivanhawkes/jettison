#!/usr/bin/env bash
#
# A Claude Code session in this repository, recorded with asciinema to ~/asciinema/claude-NNNN.cast,
# numbered one past the highest recording already there (claude-0000.cast first).
#
#   scripts/record-claude.sh [claude arguments...]
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.."

directory="$HOME/asciinema"
mkdir -p "$directory"

next=0
shopt -s nullglob
for recording in "$directory"/claude-[0-9][0-9][0-9][0-9]*.cast; do
    number="${recording##*/claude-}"
    number="${number%.cast}"
    [[ $number =~ ^[0-9]+$ ]] || continue
    ((10#$number >= next)) && next=$((10#$number + 1))
done

# asciinema runs the command through sh -c, so each argument goes in quoted.
printf -v command '%q ' claude "$@"
# asciinema refuses to overwrite a file, so two sessions started at once cannot share one.
exec asciinema rec --command "$command" "$(printf '%s/claude-%04d.cast' "$directory" "$next")"
