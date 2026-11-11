#!/usr/bin/env bash
#
# Claude Code PostToolUse hook: formats a C++ file Claude has just written or edited, so the format
# gate (scripts/format-check.sh) never fails on Claude's work. It reads the tool call as JSON on
# stdin and formats only the project's own sources: build trees and fetched dependencies are left
# alone. Without clang-format 20 or newer on the PATH (outside the devenv shell) it does nothing,
# since an older formatter rejects .clang-format.
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/../.."

file="$(jq -r '.tool_input.file_path // empty')"
[ -n "$file" ] && [ -f "$file" ] || exit 0

root="$(pwd -P)"
file="$(realpath "$file")"
case "$file" in
"$root"/build/* | "$root"/.devenv/*) exit 0 ;;
"$root"/*.cpp | "$root"/*.h | "$root"/*.hpp) ;;
*) exit 0 ;;
esac

command -v clang-format >/dev/null || exit 0
version="$(clang-format --version | sed -E 's/.* ([0-9]+)\.[0-9]+.*/\1/')"
[ "${version:-0}" -ge 20 ] || exit 0

clang-format -i -- "$file"
