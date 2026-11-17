#!/usr/bin/env bash
# Fails, naming each line, if the diff against a revision (HEAD by default) changes anything but
# comments and blank lines: the comment review's commits change no code (docs/roadmap.md).
# Usage: scripts/comment-only.sh [revision] [pathspec...]; Markdown is left out unless named.
set -euo pipefail
base=${1:-HEAD}
shift || true
[[ $# -gt 0 ]] || set -- . ':!*.md'

git diff -U0 "$base" -- "$@" | awk '
/^\+\+\+ / { file = substr($0, 7); next }
/^--- / { next }
/^[+-]/ {
	line = substr($0, 2)
	if (line ~ /^[ \t]*$/) next
	# In C-family files a "#" line is the preprocessor, not a comment.
	slashes = file ~ /\.(h|hpp|c|cpp|rs|glsl|vert|frag|comp)$/
	if (slashes ? line ~ /^[ \t]*\/\// : line ~ /^[ \t]*#/) next
	print file ": " $0
	bad = 1
}
END { exit bad }'
