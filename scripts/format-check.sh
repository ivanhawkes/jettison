#!/usr/bin/env bash
#
# Format gate: fails if clang-format would rewrite any tracked C++ file.
# Shared by the worktrunk pre-commit hook and CI so both check the same rules.
#
# Requires clang-format 20 or newer: .clang-format sets BinPackParameters to a string,
# which older formatters reject outright. The devenv shell ships LLVM 21.
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.."

# Files slated for deletion are not formatted, to keep a sweep's diff clean. Nothing is
# excluded right now; ImGuiMain.cpp, the last one, is deleted.
EXCLUDES=()

if [ ! -x "$(command -v clang-format)" ]; then
	echo "format-check: clang-format not on PATH; run 'devenv' (LLVM 21) first" >&2
	exit 1
fi

version="$(clang-format --version | sed -E 's/.* ([0-9]+)\.[0-9]+.*/\1/')"
if [ "${version:-0}" -lt 20 ]; then
	echo "format-check: clang-format >= 20 required (.clang-format uses string BinPackParameters), found $version" >&2
	exit 1
fi

files=()
while IFS= read -r -d '' candidate; do
	skip=0
	for excluded in "${EXCLUDES[@]}"; do
		[ "$candidate" = "$excluded" ] && skip=1
	done
	[ "$skip" -eq 0 ] && files+=("$candidate")
done < <(git ls-files -z '*.cpp' '*.h' '*.hpp')

if [ "${#files[@]}" -eq 0 ]; then
	echo "format-check: no tracked sources found" >&2
	exit 1
fi

clang-format --dry-run -Werror -- "${files[@]}"
echo "format-check: ${#files[@]} files match .clang-format"
