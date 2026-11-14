#!/usr/bin/env bash
#
# Cross-review: Codex reviews the current branch adversarially against the project's documents,
# and the author answers each finding until it approves or the rounds run out, then the user decides.
#
# Usage: scripts/cross-review.sh --brief FILE [--base REF] [--gate-log FILE] [--model NAME]
#                                [--no-post]
#        scripts/cross-review.sh --reply FILE [--gate-log FILE] [--model NAME] [--no-post]
#        scripts/cross-review.sh --new ...
#
#  --brief FILE     Round 1: what the change is for, the roadmap row, what was measured and what
#                   the author believes holds. The reviewer attacks these claims.
#  --reply FILE     A later round: the author's answer to each finding of the last round (fixed,
#                   with what proves it, or disputed, with the evidence).
#  --base REF       What the change is measured against (default: main); kept for later rounds
#  --gate-log FILE  A gate log for the reviewer to read
#  --model NAME     The Codex model (default: Codex's own configuration)
#  --effort LEVEL   The reviewer's reasoning effort (default: high; Codex's own default is none)
#  --max-rounds N   Refuse a round past N (default: 2); past it the user settles what is open
#  --no-post        Do not post the summary to Discord
#  --new            Discard this branch's earlier rounds and start again
#
# Rounds are kept in build/reviews/<branch>/: brief.md, round-N.md (the review), round-N.log (the
# reviewer's transcript), reply-N.md (the author's answer to round N) and scratch/, the reviewer's
# reproductions. If JETTISON_REVIEW_WEBHOOK is set, or ~/.config/jettison/review-webhook holds a
# Discord webhook URL, each round's verdict and finding titles are posted there. Exit status: 0 approved, 1 changes asked for, 2 usage, 3 the
# review failed or changed the tree and was discarded.
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.."
root="$(pwd)"

brief=""
reply=""
base=""
gate_log=""
model=()
effort=high
max_rounds=2
post=1
fresh=0

while [ "$#" -gt 0 ]; do
	case "$1" in
	--brief) brief="$2"; shift 2 ;;
	--reply) reply="$2"; shift 2 ;;
	--base) base="$2"; shift 2 ;;
	--gate-log) gate_log="$2"; shift 2 ;;
	--model) model=(--model "$2"); shift 2 ;;
	--effort) effort="$2"; shift 2 ;;
	--max-rounds) max_rounds="$2"; shift 2 ;;
	--no-post) post=0; shift ;;
	--new) fresh=1; shift ;;
	-h | --help) sed -n '2,29p' "$0"; exit 0 ;;
	*) echo "cross-review: unknown argument: $1" >&2; exit 2 ;;
	esac
done

branch="$(git rev-parse --abbrev-ref HEAD)"
dir="build/reviews/${branch//\//-}"
[ "$fresh" = 1 ] && rm -rf "$dir"
mkdir -p "$dir/scratch"

last=0
while [ -f "$dir/round-$((last + 1)).md" ]; do last=$((last + 1)); done
round=$((last + 1))

if [ "$round" = 1 ]; then
	[ -n "$brief" ] || { echo "cross-review: round 1 needs --brief FILE" >&2; exit 2; }
	[ -z "$reply" ] || { echo "cross-review: no earlier round to reply to (use --brief)" >&2; exit 2; }
	cp "$brief" "$dir/brief.md"
	git rev-parse --verify "${base:-main}^{commit}" >"$dir/base"
else
	[ -n "$reply" ] || { echo "cross-review: round $round needs --reply FILE to round $last" >&2; exit 2; }
	if [ "$round" -gt "$max_rounds" ]; then
		echo "cross-review: $last rounds done; what is still open goes to the user" >&2
		exit 2
	fi
	cp "$reply" "$dir/reply-$last.md"
	[ -z "$base" ] || git rev-parse --verify "$base^{commit}" >"$dir/base"
fi
base="$(cat "$dir/base")"
fork="$(git merge-base "$base" HEAD)"

# The tree as it stands, uncommitted work included, as a commit object that changes nothing, so
# a later round can show the reviewer what the author changed since this one.
snapshot() { local s; s="$(git stash create)"; echo "${s:-$(git rev-parse HEAD)}"; }
tree_state() { { git diff HEAD; git ls-files -o --exclude-standard; } | sha256sum; }
snap="$(snapshot)"
echo "$snap" >"$dir/round-$round.snap"

prompt="$dir/round-$round.prompt"
{
	cat scripts/cross-review-prompt.md
	echo
	echo "# This review: round $round of at most $max_rounds"
	echo
	echo "Branch \`$branch\`, measured against \`${base:0:12}\` (merge base \`${fork:0:12}\`). The change,"
	echo "uncommitted work included, is \`git diff $fork\`; its commits are"
	echo "\`git log --oneline $fork..HEAD\`. Files touched:"
	echo
	echo '```'
	git diff --stat "$fork" | tail -n 60
	echo '```'
	echo
	echo "Your scratch directory is \`$dir/scratch\`; it lasts until the review starts again."
	echo
	echo "## The author's brief"
	echo
	cat "$dir/brief.md"
	if [ -n "$gate_log" ]; then
		echo
		echo "## The gate"
		echo
		echo "The author's gate log is \`$gate_log\`."
	fi
	if [ "$round" -gt 1 ]; then
		prev="$(cat "$dir/round-$last.snap")"
		echo
		echo "## The last round"
		echo
		echo "Your review of round $last is \`$dir/round-$last.md\`. What the author changed since"
		echo "is \`git diff $prev $snap\`. The author's reply follows. For each earlier finding say"
		echo "whether it is resolved, still open (and why the reply does not settle it) or withdrawn"
		echo "(and why you were wrong). Then look for anything new, in the changes since above all."
		echo
		cat "$dir/reply-$last.md"
	fi
} >"$prompt"

out="$dir/round-$round.md"
before="$(tree_state)"
echo "cross-review: round $round on $branch against ${base:0:12}, transcript in $dir/round-$round.log" >&2
status=0
codex exec -C "$root" -s workspace-write --color never "${model[@]}" \
	-c "model_reasoning_effort=\"$effort\"" -o "$out" - \
	<"$prompt" >"$dir/round-$round.log" 2>&1 || status=$?

discard() {
	echo "cross-review: $1; the review is kept as $out.discarded" >&2
	[ -f "$out" ] && mv "$out" "$out.discarded"
	rm -f "$dir/round-$round.snap"
	[ "$round" = 1 ] || mv "$dir/reply-$last.md" "$dir/reply-$last.md.unsent"
	exit 3
}
if [ "$(tree_state)" != "$before" ]; then
	git status --short >&2
	discard "the reviewer changed the tree (above); look before keeping any of it"
fi
[ "$status" = 0 ] && [ -s "$out" ] || discard "codex failed (status $status)"

verdict="$(sed -n '/^## Verdict/,$p' "$out" | grep -oE '\b(approve|changes)\b' | head -n 1 || true)"
findings="$(grep -E '^### F[0-9]+' "$out" | sed 's/^### //' || true)"
echo "verdict: ${verdict:-unreadable}"
[ -z "$findings" ] || echo "$findings"
echo "review: $out"

webhook="${JETTISON_REVIEW_WEBHOOK:-}"
[ -n "$webhook" ] || webhook="$(cat ~/.config/jettison/review-webhook 2>/dev/null || true)"
if [ "$post" = 1 ] && [ -n "$webhook" ]; then
	# Discord refuses a message over 2000 characters.
	text="$(printf '**%s**, round %s: %s\n%s' "$branch" "$round" "${verdict:-unreadable}" "$findings" |
		head -c 1900)"
	jq -n --arg content "$text" '{content: $content}' |
		curl -fsS -H 'Content-Type: application/json' -d @- "$webhook" >/dev/null ||
		echo "cross-review: posting to Discord failed" >&2
fi

[ "$verdict" = approve ]
