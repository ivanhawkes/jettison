#!/usr/bin/env bash
#
# Headless smoke test: renders the scene through Vulkan, exercises two window resizes and
# shuts down with validation layers enabled, on a virtual X display and a software driver.
#
# The project builds GLFW Wayland-only by default, so a headless run needs the
# 'clang-headless' preset, which turns on GLFW's X11 backend, and a Vulkan implementation
# that does not need a physical GPU: Mesa's lavapipe (llvmpipe-backed) covers that.
#
# Usage: scripts/smoke-test.sh [--preset NAME] [--build-dir DIR] [--no-build]
#                              [--llvmpipe] [--icd PATH] [--xvfb] [--no-xvfb]
#
#  --preset NAME   CMake preset to build (default: clang-headless)
#  --build-dir DIR Build tree (default: build/<preset>)
#  --build         Configure and build the preset before running (the default)
#  --no-build      Skip configure/build; the tree must already contain source/test/test
#  --llvmpipe      Pin the software Vulkan driver (lavapipe) for deterministic, GPU-free runs
#  --icd PATH      Pin an explicit ICD manifest instead of lavapipe
#  --xvfb          Force running under a virtual X server (default: only when DISPLAY is unset)
#  --no-xvfb       Run on the current DISPLAY / compositor session
#  --screen GEOM   Xvfb screen geometry, WxHxD (default: 1280x720x24; keep it small for
#                  software rendering, which is fill-rate bound)
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.."

preset="clang-headless"
build_dir=""
build=1
icd=""
software=0
xvfb_mode="auto"
screen="1280x720x24"

while [ "$#" -gt 0 ]; do
	case "$1" in
	--preset)
		preset="$2"
		shift 2
		;;
	--build-dir)
		build_dir="$2"
		shift 2
		;;
	--build)
		build=1
		shift
		;;
	--no-build)
		build=0
		shift
		;;
	--llvmpipe)
		software=1
		shift
		;;
	--icd)
		icd="$2"
		software=1
		shift 2
		;;
	--xvfb)
		xvfb_mode=force
		shift
		;;
	--no-xvfb)
		xvfb_mode=off
		shift
		;;
	--screen)
		screen="$2"
		shift 2
		;;
	-h | --help)
		sed -n '2,20p' "$0"
		exit 0
		;;
	*)
		echo "smoke-test: unknown argument: $1" >&2
		exit 2
		;;
	esac
done

[ -n "$build_dir" ] || build_dir="build/$preset"
executable="$build_dir/source/test/test"

if [ "$build" -eq 1 ]; then
	cmake --preset "$preset" >/dev/null
	cmake --build --preset "$preset" >/dev/null
fi

if [ ! -x "$executable" ]; then
	echo "smoke-test: no executable at $executable (GLFW's X11 backend needs the $preset preset)" >&2
	exit 1
fi

# Software rendering is optional: on a machine with a GPU the same test is a real device run.
if [ -n "$icd" ]; then
	[ -f "$icd" ] || { echo "smoke-test: ICD manifest not found: $icd" >&2; exit 1; }
	export VK_DRIVER_FILES="$icd" VK_ICD_FILENAMES="$icd"
elif [ "$software" -eq 1 ]; then
	candidates=(
		"${VK_DRIVER_FILES:-}"
		"/etc/vulkan/icd.d/lvp_icd.x86_64.json"
		"/usr/share/vulkan/icd.d/lvp_icd.x86_64.json"
		"/run/current-system/sw/etc/vulkan/icd.d/lvp_icd.x86_64.json"
	)
	for path in ${XDG_DATA_DIRS:-/usr/local/share:/usr/share}; do
		candidates+=("$path/vulkan/icd.d/lvp_icd.x86_64.json")
	done
	for path in /nix/store/*-mesa-drivers-*/share/vulkan/icd.d/lvp_icd.x86_64.json \
		/nix/store/*-mesa-*/share/vulkan/icd.d/lvp_icd.x86_64.json; do
		[ -f "$path" ] && candidates+=("$path")
	done
	found=""
	for path in "${candidates[@]}"; do
		[ -f "$path" ] && { found="$path"; break; }
	done
	if [ -z "$found" ]; then
		echo "smoke-test: no lavapipe ICD found; pass --icd <lvp_icd.x86_64.json> (NixOS: pkgs.mesa)" >&2
		exit 1
	fi
	export VK_DRIVER_FILES="$found" VK_ICD_FILENAMES="$found"
	echo "smoke-test: software driver $found"
fi

# The debug build requests VK_LAYER_KHRONOS_validation, so fail early with a readable
# message rather than a Vulkan exception from deep inside device creation.
if [ -n "${VK_LAYER_PATH:-}" ] && [ ! -f "$VK_LAYER_PATH/VkLayer_khronos_validation.json" ]; then
	echo "smoke-test: validation layer manifest missing in VK_LAYER_PATH=$VK_LAYER_PATH" >&2
	exit 1
fi

status=0
cleanup() {
	set +e
	if [ -n "${XVFB_PID:-}" ]; then
		kill "$XVFB_PID" 2>/dev/null
		wait "$XVFB_PID" 2>/dev/null
	fi
	[ -n "${XVFB_LOCK:-}" ] && rm -f "$XVFB_LOCK"
	return 0
}
trap cleanup EXIT INT TERM

if [ "$xvfb_mode" = off ] || { [ "$xvfb_mode" = auto ] && [ -n "${DISPLAY:-}" ]; }; then
	echo "smoke-test: running on DISPLAY=${DISPLAY:-unset} (native session)"
	"$executable" --smoke-test
	status=$?
else
	if [ -x "$(command -v xvfb-run)" ]; then
		echo "smoke-test: running under xvfb-run (software display, $screen)"
		xvfb-run -a -s "-screen 0 $screen -nolisten tcp" "$executable" --smoke-test
		status=$?
	else
		xvfb="$(command -v Xvfb || command -v /run/current-system/sw/bin/Xvfb || true)"
		if [ -z "$xvfb" ]; then
			echo "smoke-test: no xvfb-run and no Xvfb on PATH (NixOS: xorg.xorgserver; Debian: xvfb)" >&2
			exit 1
		fi
		for display_number in 99 100 101 102 103; do
			lock="/tmp/.X11-unix/X$display_number"
			[ ! -e "$lock" ] && [ ! -e "/tmp/.X${display_number}-lock" ] && break
		done
		if [ -e "$lock" ] || [ -e "/tmp/.X${display_number}-lock" ]; then
			echo "smoke-test: no free X display in 99..103" >&2
			exit 1
		fi
		XVFB_LOCK="/tmp/.X${display_number}-lock"
		echo "smoke-test: starting Xvfb on :$display_number ($screen)"
		"$xvfb" ":$display_number" -screen 0 "$screen" -nolisten tcp >/dev/null 2>&1 &
		XVFB_PID=$!
		for _ in $(seq 1 50); do
			[ -e "$lock" ] && break
			sleep 0.2
		done
		[ -e "$lock" ] || { echo "smoke-test: Xvfb failed to start on :$display_number" >&2; exit 1; }
		DISPLAY=":$display_number" "$executable" --smoke-test
		status=$?
	fi
fi

if [ "$status" -eq 0 ]; then
	echo "smoke-test: PASS (both resizes handled, validation enabled)"
else
	echo "smoke-test: FAIL (exit $status)" >&2
fi
exit "$status"
