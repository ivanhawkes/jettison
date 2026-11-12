#!/usr/bin/env bash
#
# The gate: everything CI runs, in one command, so the worktrunk pre-merge hook and the
# GitHub workflow check exactly the same thing a developer can reproduce locally.
#
# Usage: scripts/ci.sh [--skip-smoke] [--skip-release] [--skip-tidy] [--werror] [--asan]
#                      [--llvmpipe] [--preset NAME] [--log FILE]
#
#  --skip-smoke     Format + compile only (use when no display/driver is available)
#  --skip-release   Skip the Release build and its tests (CI runs them; hooks usually do not need
#                   them)
#  --skip-tidy       Skip clang-tidy (it needs a configured Debug tree for its database)
#  --werror         Configure with JETTISON_WERROR=ON, so warnings fail the build
#  --asan           Build clang-asan and run the smoke test (on lavapipe, whatever else is
#                   asked), and the simulation's suites, under the sanitizers
#  --llvmpipe       Force software Vulkan (lavapipe) for the smoke test
#  --preset NAME    Debug preset (default: clang-debug)
#  --log FILE       Write the full output to FILE and print only the stages, the summary and, if a
#                   stage fails, its errors and last lines
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.."

# The suites are separate processes, each writing only under a temporary directory named for its
# process, so they run side by side. CTEST_PARALLEL_LEVEL bounds them, as CI's runner does.
jobs="${CTEST_PARALLEL_LEVEL:-$(nproc)}"

preset="clang-debug"
headless_preset="${JETTISON_CI_HEADLESS_PRESET:-clang-headless}"
smoke=1
release=1
tidy=1
asan=0
software=()
werror=()
log=""

while [ "$#" -gt 0 ]; do
	case "$1" in
	--skip-smoke) smoke=0; shift ;;
	--skip-release) release=0; shift ;;
	--skip-tidy) tidy=0; shift ;;
	--werror) werror=(-DJETTISON_WERROR=ON); shift ;;
	--asan) asan=1; shift ;;
	--llvmpipe) software=(--llvmpipe); shift ;;
	--preset) preset="$2"; shift 2 ;;
	--log) log="$2"; shift 2 ;;
	-h | --help) sed -n '2,18p' "$0"; exit 0 ;;
	*) echo "ci: unknown argument: $1" >&2; exit 2 ;;
	esac
done

# Each stage is timed, and the run ends with one line per stage whether it passed or not, so a
# reader (or an agent) can see what ran without reading the build's output. With --log that output
# goes to the file alone, and the terminal gets the stages, the summary and a failed stage's errors.
exec 3>&1
if [ -n "$log" ]; then
	mkdir -p "$(dirname "$log")"
	exec >"$log" 2>&1
fi
summary=()
current=""
started=$SECONDS
stage_start=$SECONDS
stage_line=0

say() {
	echo "$@"
	[ -n "$log" ] && echo "$@" >&3
	return 0
}

stage() {
	end_stage
	current="$1"
	stage_start=$SECONDS
	[ -n "$log" ] && stage_line=$(wc -l <"$log")
	say "==> $1"
}

skip() {
	end_stage
	summary+=("$(printf 'skip        %s' "$1")")
	say "==> $1: skipped"
}

end_stage() {
	if [ -n "$current" ]; then
		summary+=("$(printf 'ok   %5ds  %s' $((SECONDS - stage_start)) "$current")")
	fi
	current=""
}

finish() {
	local status=$?
	if [ "$status" -eq 0 ]; then
		end_stage
	elif [ -n "$current" ]; then
		summary+=("$(printf 'FAIL %5ds  %s' $((SECONDS - stage_start)) "$current")")
	fi
	# The failed stage's lines are read before anything is appended to the log they come from.
	local errors="" last=""
	if [ "$status" -ne 0 ] && [ -n "$log" ] && [ -n "$current" ]; then
		errors="$(tail -n +"$((stage_line + 1))" "$log" |
			grep -E '[Ee]rror:|ERROR|FAILED|[Ff]ailed|\*\*\*|Assertion|\[vulkan:error\]' | head -n 40 || true)"
		last="$(tail -n +"$((stage_line + 1))" "$log" | grep -vE '^\[[0-9]+/[0-9]+\] ' | tail -n 15 || true)"
	fi
	{
		if [ -n "$last" ]; then
			echo "--- errors in '$current' (full output: $log)"
			[ -n "$errors" ] && echo "$errors"
			echo "--- last lines"
			echo "$last"
		fi
		echo "--- gate summary ($((SECONDS - started)) s)"
		printf '%s\n' "${summary[@]}"
		if [ "$status" -eq 0 ]; then echo "gate passed"; else echo "gate FAILED (exit $status)"; fi
	} | if [ -n "$log" ]; then tee -a "$log" >&3; else cat; fi
}
trap finish EXIT

stage "format gate (.clang-format, clang-format $(clang-format --version | awk '{print $NF}'))"
./scripts/format-check.sh

stage "configure + build: $preset"
cmake --preset "$preset" ${werror[@]+"${werror[@]}"}
cmake --build --preset "$preset"
stage "unit tests: $preset (CTest)"
ctest --test-dir "build/$preset" --output-on-failure --no-tests=error -j "$jobs"

if [ "$tidy" -eq 1 ]; then
	stage "clang-tidy (.clang-tidy, $preset database)"
	./scripts/tidy.sh --summary --build-dir "build/$preset"
else
	skip "clang-tidy (--skip-tidy)"
fi

if [ "$release" -eq 1 ]; then
	stage "configure + build: clang-release"
	cmake --preset clang-release ${werror[@]+"${werror[@]}"}
	cmake --build --preset clang-release
	# The simulation's state hashes are pinned in the suites, and have to be the same in every
	# build: this is where Release is held to the Debug build's values.
	stage "unit tests: clang-release (CTest)"
	ctest --test-dir build/clang-release --output-on-failure --no-tests=error -j "$jobs"
else
	skip "clang-release build and tests (--skip-release)"
fi

if [ "$smoke" -eq 1 ]; then
	stage "smoke test: $headless_preset (X11 GLFW build, virtual display)"
	./scripts/smoke-test.sh --preset "$headless_preset" --build --xvfb ${software[@]+"${software[@]}"} ${werror[@]+--werror}
	# The frames phase 10 is judged by, against the references kept beside views.txt; always on
	# lavapipe, which the references were taken on (scripts/capture.sh).
	stage "reference frames: $headless_preset (lavapipe, virtual display)"
	./scripts/capture.sh --preset "$headless_preset" --no-build --references
else
	skip "smoke test (--skip-smoke)"
fi

if [ "$asan" -eq 1 ]; then
	# Always on lavapipe: the sanitizers are for this project's code, and a proprietary driver is
	# neither instrumented nor willing. NVIDIA's (615.71) fails vkCreateInstance under ASan, which the
	# loader reports, and LeakSanitizer then reports what it allocated before failing.
	stage "smoke test: clang-asan (address + undefined sanitizers, lavapipe)"
	./scripts/smoke-test.sh --preset clang-asan --build --xvfb --llvmpipe ${werror[@]+--werror}
	# The suites whose state hashes are pinned, held to the same values under the sanitizers, and
	# the spatial index steering reads.
	stage "unit tests: clang-asan (sim, spatial, physics, ai and dock)"
	cmake --build --preset clang-asan --target jettison_sim_tests jettison_spatial_tests \
		jettison_physics_tests jettison_ai_tests jettison_dock_tests
	ctest --test-dir build/clang-asan -R '^(sim|spatial|physics|ai|dock)$' --output-on-failure \
		--no-tests=error -j "$jobs"
fi

