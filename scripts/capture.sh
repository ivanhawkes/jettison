#!/usr/bin/env bash
#
# Frames of render_test, each written from inside its frame loop (--screenshot) on lavapipe under
# Xvfb, with the clocks moved a fixed step a frame and held while anything loads (--frame-step), so
# a view captured twice on one machine is the same to the byte.
#
#   scripts/capture.sh [options] <out.png> [render_test arguments...]   one frame
#   scripts/capture.sh [options] --references [--update] [name...]      the reference frames
#
# The reference frames are the views source/test/frames/views.txt lists, one a line: a name, then
# render_test's arguments, a | standing for a space within one. Each view is captured and compared
# with source/test/frames/<name>.png, and the run fails if more than one pixel in a thousand
# differs by more than 2% (another machine's lavapipe rounds a little differently), or if the
# validation layer reported anything; the frames and their differences are kept in the build tree.
# --update writes the references instead, and is how a change that is meant to change a frame
# records it. Names after --references take those views alone.
#
# Options:
#  --preset NAME   CMake preset to build and run (default: clang-headless)
#  --no-build      Run the tree as it is
#  --window WxH    The frame's size (default: 960x540)
#  --frames N      Frames drawn with nothing loading before the one captured (default: 30)
#  --icd PATH      Pin an ICD manifest instead of finding lavapipe
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.."
# shellcheck source=lib/headless.sh
source scripts/lib/headless.sh

preset="clang-headless"
build=1
window="960x540"
frames=30
icd=""
references=0
update=0
while [ "$#" -gt 0 ]; do
	case "$1" in
	--preset) preset="$2"; shift 2 ;;
	--no-build) build=0; shift ;;
	--window) window="$2"; shift 2 ;;
	--frames) frames="$2"; shift 2 ;;
	--icd) icd="$2"; shift 2 ;;
	--references) references=1; shift ;;
	--update) update=1; shift ;;
	-h | --help) sed -n '2,23p' "$0"; exit 0 ;;
	*) break ;;
	esac
done
if [ "$references" -eq 0 ] && [ "$#" -lt 1 ]; then
	sed -n '2,23p' "$0" >&2
	exit 2
fi

build_dir="build/$preset"
executable="$build_dir/source/test/render_test"
if [ "$build" -eq 1 ]; then
	cmake --preset "$preset" >/dev/null
	cmake --build --preset "$preset" --target render_test >/dev/null
fi
[ -x "$executable" ] || { echo "capture: no executable at $executable" >&2; exit 1; }
headless_use_lavapipe "$icd" || exit 1
# The screen holds the window whole, though nothing reads the screen: the frame is the swapchain's.
headless_start_xvfb "1920x1080x24" || exit 1

# <out.png> <log> [render_test arguments...]: one frame, its output in the log. The run's user
# directory is its own and empty, so no one's settings or saves are in the frame.
capture() {
	local out="$1" log="$2"
	shift 2
	local user status=0
	user="$(mktemp -d "${TMPDIR:-/tmp}/jettison-capture-user.XXXXXX")"
	env DISPLAY="$HEADLESS_DISPLAY" "$executable" --window "$window" --no-sound --user-dir "$user" \
		--frame-step 0.0166666666666667 --after-frames "$frames" --screenshot "$out" "$@" \
		>"$log" 2>&1 || status=$?
	rm -rf "$user"
	return "$status"
}

# The validation layer's reports in a run's log, less the loader's noise smoke-test.sh describes.
loader_noise='Loader Message: Removing layer VK_LAYER_MESA_.*because it is a duplicate of VK_LAYER_MESA_|Loader Message: terminator_CreateInstance: Received return code -9 from call to vkCreateInstance in ICD [^ ]*/libvulkan_dzn\.so\. Skipping this driver\.'
validation_issues() {
	grep -E 'Validation Error:|^\[vulkan:error|^\[vulkan:warning' "$1" | grep -Ev "$loader_noise" || true
}

if [ "$references" -eq 0 ]; then
	out="$(realpath -m "$1")"
	shift
	log="$(mktemp "${TMPDIR:-/tmp}/jettison-capture.XXXXXX")"
	if ! capture "$out" "$log" "$@"; then
		tail -n 20 "$log" >&2
		echo "capture: render_test failed (log: $log)" >&2
		exit 1
	fi
	issues="$(validation_issues "$log")"
	[ -z "$issues" ] || { echo "$issues" >&2; echo "capture: the validation layer reported the above" >&2; exit 1; }
	echo "capture: $out"
	exit 0
fi

frames_dir="source/test/frames"
out_dir="$build_dir/frames"
mkdir -p "$out_dir"
names=()
declare -A view_args
while read -r name rest; do
	[[ -z "$name" || "$name" == \#* ]] && continue
	view_args[$name]="$rest"
	if [ "$#" -eq 0 ] || [[ " $* " == *" $name "* ]]; then
		names+=("$name")
	fi
done <"$frames_dir/views.txt"
[ "${#names[@]}" -gt 0 ] || { echo "capture: no view of those names in $frames_dir/views.txt" >&2; exit 2; }

# The views at once, on the one display: a run spends most of its start-up transcoding the
# planet's maps on one thread.
pids=()
for name in "${names[@]}"; do
	# The arguments are words, with $PWD standing for the repository and | for a space within one
	# (a console line given to --run).
	read -r -a args <<<"${view_args[$name]//\$PWD/$PWD}"
	args=("${args[@]//|/ }")
	capture "$PWD/$out_dir/$name.png" "$out_dir/$name.log" "${args[@]}" &
	pids+=($!)
done
status=0
for index in "${!names[@]}"; do
	name="${names[$index]}"
	if ! wait "${pids[$index]}"; then
		tail -n 20 "$out_dir/$name.log" >&2
		echo "capture: $name: render_test failed (log: $out_dir/$name.log)" >&2
		status=1
		continue
	fi
	issues="$(validation_issues "$out_dir/$name.log")"
	if [ -n "$issues" ]; then
		echo "$issues" | head -n 10 >&2
		echo "capture: $name: the validation layer reported the above" >&2
		status=1
		continue
	fi
	if [ "$update" -eq 1 ]; then
		cp "$out_dir/$name.png" "$frames_dir/$name.png"
		echo "capture: $name: reference written"
		continue
	fi
	if [ ! -f "$frames_dir/$name.png" ]; then
		echo "capture: $name: no reference (scripts/capture.sh --references --update $name)" >&2
		status=1
		continue
	fi
	read -r width height < <(magick identify -format '%w %h\n' "$out_dir/$name.png")
	allowed=$((width * height / 1000))
	differing="$(magick compare -metric AE -fuzz 2% "$frames_dir/$name.png" "$out_dir/$name.png" \
		"$out_dir/$name-diff.png" 2>&1 >/dev/null || true)"
	differing="${differing%% *}"
	# ImageMagick can count a pixel that differs in part as a fraction of one ("2.14118"): a count
	# of pixels, rounded up, is what is allowed.
	if [[ "$differing" =~ ^[0-9]+\.[0-9]+$ ]]; then
		differing="$(awk -v count="$differing" 'BEGIN { whole = int(count); print (count > whole) ? whole + 1 : whole }')"
	fi
	if ! [[ "$differing" =~ ^[0-9]+$ ]] || [ "$differing" -gt "$allowed" ]; then
		echo "capture: $name: $differing pixels differ from the reference, of $allowed allowed (see $out_dir/$name-diff.png)" >&2
		status=1
	else
		echo "capture: $name: $differing pixels differ, of $allowed allowed"
	fi
done
exit "$status"
