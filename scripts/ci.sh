#!/usr/bin/env bash
#
# The gate: everything CI runs, in one command, so the worktrunk pre-merge hook and the
# GitHub workflow check exactly the same thing a developer can reproduce locally.
#
# Usage: scripts/ci.sh [--skip-smoke] [--skip-release] [--skip-tidy] [--werror] [--asan]
#                      [--llvmpipe] [--preset NAME]
#
#  --skip-smoke     Format + compile only (use when no display/driver is available)
#  --skip-release   Skip the Release build (CI runs it; hooks usually do not need it)
#  --skip-tidy       Skip clang-tidy (it needs a configured Debug tree for its database)
#  --werror         Configure with JETTISON_WERROR=ON, so warnings fail the build
#  --asan           Build clang-asan and run the smoke test under the sanitizers
#  --llvmpipe       Force software Vulkan (lavapipe) for the smoke test
#  --preset NAME    Debug preset (default: clang-debug)
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.."

preset="clang-debug"
headless_preset="${JETTISON_CI_HEADLESS_PRESET:-clang-headless}"
smoke=1
release=1
tidy=1
asan=0
software=()
werror=()

while [ "$#" -gt 0 ]; do
	case "$1" in
	--skip-smoke) smoke=0; shift ;;
	--skip-release) release=0; shift ;;
	--skip-tidy) tidy=0; shift ;;
	--werror) werror=(-DJETTISON_WERROR=ON); shift ;;
	--asan) asan=1; shift ;;
	--llvmpipe) software=(--llvmpipe); shift ;;
	--preset) preset="$2"; shift 2 ;;
	-h | --help) sed -n '2,15p' "$0"; exit 0 ;;
	*) echo "ci: unknown argument: $1" >&2; exit 2 ;;
	esac
done

echo "==> format gate (.clang-format, clang-format $(clang-format --version | awk '{print $NF}'))"
./scripts/format-check.sh

echo "==> configure + build: $preset"
cmake --preset "$preset" ${werror[@]+"${werror[@]}"}
cmake --build --preset "$preset"
"build/$preset/source/test/smoke_sequence_test"
"build/$preset/source/test/dependency_probe"

if [ "$tidy" -eq 1 ]; then
	echo "==> clang-tidy (.clang-tidy, $preset database)"
	./scripts/tidy.sh --summary --build-dir "build/$preset"
else
	echo "==> clang-tidy: skipped (--skip-tidy)"
fi

if [ "$release" -eq 1 ]; then
	echo "==> configure + build: clang-release"
	cmake --preset clang-release ${werror[@]+"${werror[@]}"}
	cmake --build --preset clang-release
fi

if [ "$smoke" -eq 1 ]; then
	echo "==> smoke test: $headless_preset (X11 GLFW build, virtual display)"
	./scripts/smoke-test.sh --preset "$headless_preset" --build --xvfb ${software[@]+"${software[@]}"} ${werror[@]+--werror}
else
	echo "==> smoke test: skipped (--skip-smoke)"
fi

if [ "$asan" -eq 1 ]; then
	echo "==> smoke test: clang-asan (address + undefined sanitizers)"
	./scripts/smoke-test.sh --preset clang-asan --build --xvfb ${software[@]+"${software[@]}"} ${werror[@]+--werror}
fi

echo "==> gate passed"
