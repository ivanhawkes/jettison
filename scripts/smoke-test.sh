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
#  --no-build      Skip configure/build; the tree must already contain source/test/render_test
#  --llvmpipe      Pin the software Vulkan driver (lavapipe) for deterministic, GPU-free runs
#  --werror        Configure the build with warnings treated as errors
#  --icd PATH      Pin an explicit ICD manifest instead of lavapipe
#  --xvfb          Force running under a virtual X server (default: only when DISPLAY is unset)
#  --no-xvfb       Run on the current DISPLAY / compositor session
#  --screen GEOM   Xvfb screen geometry, WxHxD (default: 1280x720x24; keep it small for
#                  software rendering, which is fill-rate bound)
#  --packed        Run from the assets packed into one file (assets.pack, as a shipped build has
#                  them), with the test's data mod (source/test/mods/smoke) in the run's user mods
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.."
# shellcheck source=lib/headless.sh
source scripts/lib/headless.sh

preset="clang-headless"
build_dir=""
build=1
icd=""
software=0
werror=()
xvfb_mode="auto"
screen="1280x720x24"
packed=0

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
	--werror)
		werror=(-DJETTISON_WERROR=ON)
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
	--packed)
		packed=1
		shift
		;;
	-h | --help)
		sed -n '2,22p' "$0"
		exit 0
		;;
	*)
		echo "smoke-test: unknown argument: $1" >&2
		exit 2
		;;
	esac
done

[ -n "$build_dir" ] || build_dir="build/$preset"
executable="$build_dir/source/test/render_test"

if [ "$build" -eq 1 ]; then
	cmake --preset "$preset" ${werror[@]+"${werror[@]}"} >/dev/null
	# The render test alone, with the shaders and assets it stages: nothing else in this tree is run
	# here, and the Debug tree builds and tests the rest. In the sanitised tree the rest was a third
	# of the build, 18 s of 68 after a change to a header most of the engine includes.
	cmake --build --preset "$preset" --target render_test >/dev/null
	if [ "$packed" -eq 1 ]; then
		cmake --build --preset "$preset" --target jettison_assets_pack render_test_smoke_mod >/dev/null
	fi
fi

# The packed run: every asset from the pack, and for each scene a user directory of its own holding
# the mod, which the shell mounts between the two and logs.
if [ "$packed" -eq 1 ]; then
	pack="$build_dir/assets.pack"
	mod="$build_dir/source/test/mods/smoke.pack"
	for file in "$pack" "$mod"; do
		[ -f "$file" ] || { echo "smoke-test: no pack at $file (build jettison_assets_pack and render_test_smoke_mod)" >&2; exit 1; }
	done
fi

if [ ! -x "$executable" ]; then
	echo "smoke-test: no executable at $executable (GLFW's X11 backend needs the $preset preset)" >&2
	exit 1
fi

# Software rendering is optional: on a machine with a GPU the same test is a real device run.
if [ -n "$icd" ] || [ "$software" -eq 1 ]; then
	headless_use_lavapipe "$icd" || exit 1
fi

# The debug build requests VK_LAYER_KHRONOS_validation, so fail early with a readable
# message rather than a Vulkan exception from deep inside device creation.
if [ -n "${VK_LAYER_PATH:-}" ] && [ ! -f "$VK_LAYER_PATH/VkLayer_khronos_validation.json" ]; then
	echo "smoke-test: validation layer manifest missing in VK_LAYER_PATH=$VK_LAYER_PATH" >&2
	exit 1
fi

# GLFW loads its window-system libraries at runtime, so an X11 tree needs libX11 findable
# when it runs, not only when it builds. A Nix shell puts package libraries nowhere unless
# the shell names them (devenv.nix does, in the enterShell list), and all GLFW reports is
# "X11: Failed to load Xlib", which says nothing about the shell that needs fixing.
if grep -q 'JETTISON_GLFW_X11:BOOL=ON' "$build_dir/CMakeCache.txt" 2>/dev/null; then
	x11=""
	IFS=':' read -r -a dirs <<< "${LD_LIBRARY_PATH:-}"
	for dir in ${dirs[@]+"${dirs[@]}"}; do
		[ -e "$dir/libX11.so.6" ] && { x11="$dir/libX11.so.6"; break; }
	done
	if [ -z "$x11" ] && [ -x "$(command -v ldconfig)" ]; then
		ldconfig -p 2>/dev/null | grep -q 'libX11\.so\.6' && x11="$(ldconfig -p | grep -m1 'libX11\.so\.6' | awk '{print $NF}')"
	fi
	if [ -z "$x11" ]; then
		echo "smoke-test: this tree builds GLFW's X11 backend, and libX11.so.6 is not findable at run time" >&2
		echo "smoke-test: GLFW dlopens X11, so LD_LIBRARY_PATH has to carry it (devenv.nix names it in enterShell)" >&2
		exit 1
	fi
	echo "smoke-test: X11 runtime $x11"
fi

# The app's own stream is the record of what the layer noticed: the validation layer prints
# "Validation Error: ..." on its own, and DeviceContext's messenger prints the same events
# with a [vulkan:...] prefix. Keep the console output, keep the log, judge the run on the log.
status=0
run_app() {
	if grep -q '^CMAKE_BUILD_TYPE:STRING=Debug$' "$build_dir/CMakeCache.txt"; then
		empty_layers="$(mktemp -d "${TMPDIR:-/tmp}/jettison-empty-layers.XXXXXX")"
		startup_status=0
		startup_output="$(env VK_LAYER_PATH="$empty_layers" "$@" 2>&1)" || startup_status=$?
		rmdir "$empty_layers"
		if [ "$startup_status" -ne 1 ] || [[ "$startup_output" != *"validation layers requested, but not available"* ]]; then
			echo "smoke-test: failed startup did not exit cleanly (exit $startup_status)" >&2
			printf '%s\n' "$startup_output" >&2
			status=1
			return
		fi
		echo "smoke-test: missing validation layer exits cleanly"
		empty_assets="$(mktemp -d "${TMPDIR:-/tmp}/jettison-empty-assets.XXXXXX")"
		startup_status=0
		startup_output="$("$@" --assets "$empty_assets" --scene "$PWD/assets/scene/main.yaml" 2>&1)" || startup_status=$?
		rmdir "$empty_assets"
		if [ "$startup_status" -ne 1 ] || [[ "$startup_output" != *"failed to open file"* ]] ||
			[[ "$startup_output" == *"[vulkan:error"* ]] || [[ "$startup_output" == *"Validation Error:"* ]]; then
			echo "smoke-test: missing shader did not exit cleanly (exit $startup_status)" >&2
			printf '%s\n' "$startup_output" >&2
			status=1
			return
		fi
		echo "smoke-test: missing shader exits without validation errors"
	fi
	# Two scenes, one log: the sample scene (glTF models, a skinned clip, the point light), named, and
	# the default, the large-world scene (a station over a planet at real scale, placed by the star
	# system, under an orbit camera and the sun). Each walks the whole sequence; the run fails if
	# either does. Each jumps once: the sample scene, of no system, to the galaxy's first (the
	# large-world scene), and the large-world scene through Halcyon's gate to Vesper, built from its
	# system alone.
	app_log="$(mktemp "${TMPDIR:-/tmp}/jettison-smoke-app.XXXXXX")"
	status=0
	for scene in "$PWD/assets/scene/main.yaml" ""; do
		scene_args=()
		[ -n "$scene" ] && scene_args=(--scene "$scene")
		echo "smoke-test: scene ${scene:-default}"
		packed_args=()
		if [ "$packed" -eq 1 ]; then
			packed_user="$(mktemp -d "${TMPDIR:-/tmp}/jettison-smoke-user.XXXXXX")"
			mkdir -p "$packed_user/data/mods"
			cp "$mod" "$packed_user/data/mods/"
			packed_args=(--assets "$PWD/$pack" --user-dir "$packed_user")
		fi
		if ! "$@" ${packed_args[@]+"${packed_args[@]}"} ${scene_args[@]+"${scene_args[@]}"} 2>&1 | tee -a "$app_log"; then
			scene_status=${PIPESTATUS[0]}
			[ "$scene_status" -ne 0 ] || scene_status=1
			status=$scene_status
		fi
		if [ "$packed" -eq 1 ]; then rm -rf "$packed_user"; fi
	done
}

if [ "$xvfb_mode" = off ] || { [ "$xvfb_mode" = auto ] && [ -n "${DISPLAY:-}" ]; }; then
	echo "smoke-test: running on DISPLAY=${DISPLAY:-unset} (native session)"
	run_app "$executable" --smoke-test
else
	headless_start_xvfb "$screen" || exit 1
	run_app env DISPLAY="$HEADLESS_DISPLAY" "$executable" --smoke-test
fi

# A run that renders, resizes and exits cleanly is not a run the layer is happy about. Two of the
# loader's messages are environment, not this project: duplicate Mesa layer manifests (the shell's
# Mesa against /run/opengl-driver), and Mesa's D3D12 driver (dzn), which exists for WSL and declines
# to start anywhere else, skipped whenever every driver is loaded rather than one pinned.
loader_noise='Loader Message: Removing layer VK_LAYER_MESA_.*because it is a duplicate of VK_LAYER_MESA_|Loader Message: terminator_CreateInstance: Received return code -9 from call to vkCreateInstance in ICD [^ ]*/libvulkan_dzn\.so\. Skipping this driver\.'
issues=0
if [ -n "${app_log:-}" ] && [ -f "${app_log:-}" ]; then
	issues=$(grep -E 'Validation Error:|^\[vulkan:error|^\[vulkan:warning' "$app_log" | grep -Evc "$loader_noise" || true)
	if [ "${issues:-0}" -gt 0 ]; then
		echo "smoke-test: FAIL (the validation layer reported $issues issue(s) in a run that exited $status)" >&2
		grep -E 'Validation Error:|^\[vulkan:error|^\[vulkan:warning' "$app_log" | grep -Ev "$loader_noise" | head -20 >&2
		echo "smoke-test: full output in $app_log" >&2
		status=1
	elif [ -n "${VK_SMOKE_VERBOSE:-}" ]; then
		echo "smoke-test: validation reported nothing (log: $app_log)"
	fi
fi

# A packed run proves what it read: the base a pack, the mod mounted over it, the mod's patches
# taken and its script loaded from it, each said by the shell as it starts, once a scene; and that the
# script was called from an event, a trade filled, which it says it heard, and never trapped.
if [ "$packed" -eq 1 ] && [ -n "${app_log:-}" ]; then
	for said in "base: the pack " "mod:smoke: the pack " "defs/goods/beer: patched by 'mod:smoke'" \
		"defs/decisions/trade: patched by 'mod:smoke'" "Script 'smoke': scripts/smoke.wasm loaded"; do
		count=$(grep -cF "$said" "$app_log" || true)
		if [ "${count:-0}" -ne 2 ]; then
			echo "smoke-test: FAIL (the packed run said '$said' $count times, not once a scene)" >&2
			status=1
		fi
	done
	heard=$(grep -cF "smoke: heard trade" "$app_log" || true)
	if [ "${heard:-0}" -lt 1 ]; then
		echo "smoke-test: FAIL (the mod's script was not called from a trade's event)" >&2
		status=1
	fi
	if grep -E "Script 'smoke': .*(trapped|time limit|left out)" "$app_log" >&2; then
		echo "smoke-test: FAIL (the mod's script trapped, or did not load)" >&2
		status=1
	fi
	if [ "$status" -eq 0 ]; then
		echo "smoke-test: the assets were read from $pack, the mod in the user's mods loaded, and its script heard $heard trade(s)"
	fi
fi

if [ "$status" -eq 0 ]; then
	echo "smoke-test: PASS (two scenes; window resizes, minimize/restore, a scene reload, a ship flown and a jump handled, validation enabled)"
else
	echo "smoke-test: FAIL (exit $status)" >&2
fi
exit "$status"
