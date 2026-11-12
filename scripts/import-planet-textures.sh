#!/usr/bin/env bash
# Imports a planet's source maps (TGA, from the artist's pack) as KTX 2 textures under
# assets/textures/planets/<name>/, named <name>-<map>.ktx2, with KTX-Software's ktx tool and
# ImageMagick from devenv:
#
#   scripts/import-planet-textures.sh <source-dir> <name>
#
# Six 4096-square maps took six minutes on 2026-11-12, most of it UASTC's rate-distortion pass.
#
# The pack names its maps by suffix: _C colour, _N normal, _H height, _R roughness, and
# _Detail_C and _Detail_N for the detail layer that tiles over them. A map's file is the one whose
# name ends in its suffix before .tga; a base map's suffix is the detail map's tail, so it skips
# _Detail_.
#
# Every map is Basis Universal UASTC with rate-distortion optimisation and zstd, mipmapped by the
# tool, which the engine transcodes to BC7 (or RGBA8 on a device without it). Colour is sRGB; every
# other map is data and is assigned a linear transfer function, so the tool takes the bytes as they
# are rather than converting them from sRGB. The normal maps are authored with green pointing down
# the image (DirectX's convention: integrating them that way reproduces the height map, and the
# detail colour at a correlation of 0.997); green is inverted here so they point up, as glTF's do
# and as the engine's shaders read them. Greyscale maps are kept to one channel.
set -euo pipefail

if [[ $# -ne 2 ]]; then
	echo "usage: $0 <source-dir> <name>" >&2
	exit 2
fi
source_dir=$1
name=$2
if [[ ! $name =~ ^[a-z0-9]+(-[a-z0-9]+)*$ ]]; then
	echo "the name '$name' is not kebab-case" >&2
	exit 2
fi

repo=$(cd "$(dirname "$0")/.." && pwd)
out="$repo/assets/textures/planets/$name"
mkdir -p "$out"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

find_map() {
	local matches=()
	local file
	for file in "$source_dir"/*"$1".tga; do
		[[ -f $file ]] || continue
		[[ $1 != _Detail_* && $file == *_Detail_* ]] && continue
		matches+=("$file")
	done
	if [[ ${#matches[@]} -ne 1 ]]; then
		echo "expected one file ending in $1.tga in $source_dir" >&2
		exit 1
	fi
	echo "${matches[0]}"
}

# <suffix> <map> <kind>: kind is colour, data (greyscale), or normal.
import_map() {
	local source
	source=$(find_map "$1")
	local png="$work/$2.png"
	local target="$out/$name-$2.ktx2"
	local format tf
	case $3 in
		colour)
			magick "$source" -depth 8 -define png:color-type=2 "$png"
			format=R8G8B8_SRGB tf=srgb ;;
		data)
			magick "$source" -colorspace Gray -depth 8 -define png:color-type=0 "$png"
			format=R8_UNORM tf=linear ;;
		normal)
			magick "$source" -channel G -negate +channel -depth 8 -define png:color-type=2 "$png"
			format=R8G8B8_UNORM tf=linear ;;
	esac
	ktx create --format "$format" --assign-tf "$tf" --encode uastc --uastc-quality 2 \
		--uastc-rdo --zstd 19 --generate-mipmap "$png" "$target"
	echo "$target: $(du -h "$target" | cut -f1)"
}

import_map _C color colour
import_map _N normal normal
import_map _H height data
import_map _R roughness data
import_map _Detail_C detail-color data
import_map _Detail_N detail-normal normal
