# Sourced by the scripts that run render_test without a GPU or a display: smoke-test.sh and
# capture.sh. Lavapipe found and pinned, and a virtual X server started on a free display and
# stopped as the script exits.

# Pins lavapipe as the only Vulkan driver, or the manifest given: VK_DRIVER_FILES and
# VK_ICD_FILENAMES. Returns 1, having said why, when there is none.
headless_use_lavapipe() {
	local icd="${1:-}"
	if [ -n "$icd" ]; then
		[ -f "$icd" ] || { echo "$0: ICD manifest not found: $icd" >&2; return 1; }
		export VK_DRIVER_FILES="$icd" VK_ICD_FILENAMES="$icd"
		return 0
	fi
	local candidates=(
		"${JETTISON_LAVAPIPE_ICD:-}"
		"${VK_DRIVER_FILES:-}"
		"/etc/vulkan/icd.d/lvp_icd.x86_64.json"
		"/usr/share/vulkan/icd.d/lvp_icd.x86_64.json"
		"/run/current-system/sw/etc/vulkan/icd.d/lvp_icd.x86_64.json"
	)
	local path
	for path in ${XDG_DATA_DIRS:-/usr/local/share:/usr/share}; do
		candidates+=("$path/vulkan/icd.d/lvp_icd.x86_64.json")
	done
	for path in /nix/store/*-mesa-drivers-*/share/vulkan/icd.d/lvp_icd.x86_64.json \
		/nix/store/*-mesa-*/share/vulkan/icd.d/lvp_icd.x86_64.json; do
		[ -f "$path" ] && candidates+=("$path")
	done
	for path in "${candidates[@]}"; do
		if [ -f "$path" ]; then
			export VK_DRIVER_FILES="$path" VK_ICD_FILENAMES="$path"
			echo "$(basename "$0"): software driver $path"
			return 0
		fi
	done
	echo "$0: no lavapipe ICD found; pass --icd <lvp_icd.x86_64.json> (NixOS: pkgs.mesa)" >&2
	return 1
}

# Starts Xvfb with the screen given (WxHxD) on the first free display from 99, sets
# HEADLESS_DISPLAY to it, and stops it as the script exits. Returns 1, having said why, if it
# cannot.
headless_start_xvfb() {
	local screen="$1"
	local xvfb
	xvfb="$(command -v Xvfb || command -v /run/current-system/sw/bin/Xvfb || true)"
	if [ -z "$xvfb" ]; then
		echo "$0: no Xvfb on PATH (NixOS: xorgserver; Debian: xvfb)" >&2
		return 1
	fi
	local display_number lock
	for display_number in 99 100 101 102 103 104 105 106 107; do
		lock="/tmp/.X11-unix/X$display_number"
		[ ! -e "$lock" ] && [ ! -e "/tmp/.X${display_number}-lock" ] && break
	done
	if [ -e "$lock" ] || [ -e "/tmp/.X${display_number}-lock" ]; then
		echo "$0: no free X display in 99..107" >&2
		return 1
	fi
	HEADLESS_XVFB_LOCK="/tmp/.X${display_number}-lock"
	echo "$(basename "$0"): starting Xvfb on :$display_number ($screen)"
	"$xvfb" ":$display_number" -screen 0 "$screen" -nolisten tcp >/dev/null 2>&1 &
	HEADLESS_XVFB_PID=$!
	trap headless_stop_xvfb EXIT INT TERM
	local _
	for _ in $(seq 1 50); do
		[ -e "$lock" ] && break
		sleep 0.2
	done
	[ -e "$lock" ] || { echo "$0: Xvfb failed to start on :$display_number" >&2; return 1; }
	HEADLESS_DISPLAY=":$display_number"
}

headless_stop_xvfb() {
	set +e
	if [ -n "${HEADLESS_XVFB_PID:-}" ]; then
		kill "$HEADLESS_XVFB_PID" 2>/dev/null
		wait "$HEADLESS_XVFB_PID" 2>/dev/null
		HEADLESS_XVFB_PID=""
	fi
	[ -n "${HEADLESS_XVFB_LOCK:-}" ] && rm -f "$HEADLESS_XVFB_LOCK"
	return 0
}
