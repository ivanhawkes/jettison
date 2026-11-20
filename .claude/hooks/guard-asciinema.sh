#!/usr/bin/env bash
#
# Claude Code PreToolUse hook: keeps Claude out of ./asciinema, which the launch script records and
# commits. It refuses a tool call that names the directory in a path or a shell command, and a git
# commit that would take any of its files: staged, or tracked and changed under -a/--all.
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/../.."

deny() {
	jq -n --arg reason "$1" '{hookSpecificOutput: {hookEventName: "PreToolUse",
		permissionDecision: "deny", permissionDecisionReason: $reason}}'
	exit 0
}

input="$(cat)"
tool="$(jq -r '.tool_name // empty' <<<"$input")"
dir='asciinema'

if [ "$tool" = Bash ]; then
	command="$(jq -r '.tool_input.command // empty' <<<"$input")"
	# The directory as a word of its own, so a file such as this one, named for it, is not caught.
	if grep -Eq "(^|[^[:alnum:]_.-])$dir(/|[^[:alnum:]_.-]|\$)" <<<"$command"; then
		deny "./$dir is off limits: do not read, write or commit it."
	fi
	if grep -Eq '(^|[^[:alnum:]_-])git([[:space:]]+-C[[:space:]]+[^[:space:]]+)?[[:space:]]+add[[:space:]].*(-A|--all|[[:space:]]\.([[:space:]]|$))' <<<"$command"; then
		deny "git add -A/--all/. would stage ./$dir: add the files by name."
	fi
	if grep -Eq '(^|[^[:alnum:]_-])git([[:space:]]+-C[[:space:]]+[^[:space:]]+)?[[:space:]]+commit' <<<"$command"; then
		staged="$(git diff --cached --name-only -- "$dir" 2>/dev/null || true)"
		if grep -Eq '[[:space:]](-a|--all|-[[:alnum:]]*a[[:alnum:]]*)([[:space:]]|$)' <<<"$command"; then
			staged+="$(git diff --name-only -- "$dir" 2>/dev/null || true)"
		fi
		[ -z "$staged" ] || deny "This commit would include ./$dir files: unstage them (git restore --staged $dir)."
	fi
	exit 0
fi

paths="$(jq -r '.tool_input | [.file_path, .path, .notebook_path, (if .pattern and ("'"$tool"'" == "Glob") then .pattern else null end)] | map(select(. != null)) | .[]' <<<"$input")"
if grep -Eq "(^|/)$dir(/|$)" <<<"$paths"; then
	deny "./$dir is off limits: do not read, write or search it."
fi
exit 0
