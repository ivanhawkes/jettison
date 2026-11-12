#!/usr/bin/env python3
# Rebuilds the sample hull textures in assets/textures/hulls/hull-panels: painted plating (its
# colour) and a mask of lit windows (its emissive), both 256x256 and tileable. Writes the PNGs to the
# current directory; the KTX 2 files are encoded from them with KTX-Software's ktx tool (nixpkgs
# ktx-tools, the version the engine reads, in devenv):
#
#   python3 scripts/make-sample-textures.py
#   ktx create --format R8G8B8_SRGB --encode uastc --uastc-quality 2 --zstd 19 \
#       --generate-mipmap hull-panels-color.png \
#       assets/textures/hulls/hull-panels/hull-panels-color.ktx2
#   ktx create --format R8G8B8_SRGB --encode basis-lz --clevel 2 --qlevel 192 \
#       --generate-mipmap hull-panels-emissive.png \
#       assets/textures/hulls/hull-panels/hull-panels-emissive.ktx2
#
# The two encodings are Basis Universal's two modes, UASTC and ETC1S, so the sample assets take
# both through the transcoder. The seed is fixed: the same script makes the same images.
import random, struct, zlib

SIZE = 256
random.seed(7)

def png(path, pixels):
    raw = b''.join(b'\0' + bytes(c for px in row for c in px) for row in pixels)
    def chunk(kind, data):
        body = kind + data
        return struct.pack('>I', len(data)) + body + struct.pack('>I', zlib.crc32(body) & 0xffffffff)
    with open(path, 'wb') as f:
        f.write(b'\x89PNG\r\n\x1a\n')
        f.write(chunk(b'IHDR', struct.pack('>IIBBBBB', SIZE, SIZE, 8, 2, 0, 0, 0)))
        f.write(chunk(b'IDAT', zlib.compress(raw, 9)))
        f.write(chunk(b'IEND', b''))

# Panels: rows 32 high, panels 64 wide, every other row offset by half a panel, as plating is.
ROW, WIDE = 32, 64
def panel_of(x, y):
    row = y // ROW
    shift = (WIDE // 2) * (row % 2)
    return row, ((x + shift) % SIZE) // WIDE, (x + shift) % WIDE, y % ROW

tints = {}
for row in range(SIZE // ROW):
    for col in range(SIZE // WIDE):
        tints[(row, col)] = random.uniform(-14, 14)
# A few panels are a darker service colour.
for key in random.sample(sorted(tints), 5):
    tints[key] -= 38

hull, lights = [], []
for y in range(SIZE):
    hull_row, light_row = [], []
    for x in range(SIZE):
        row, col, px, py = panel_of(x, y)
        base = 162 + tints[(row, col)] + random.uniform(-4, 4)
        r, g, b = base, base + 2, base + 7
        if px < 2 or py < 2:
            # The seam between plates.
            r, g, b = 62, 64, 70
        elif (px in (5, WIDE - 6)) and (py in (5, ROW - 6)):
            # Rivets at a plate's corners.
            r, g, b = r + 40, g + 40, b + 40
        elif 2 <= py < 4 or 2 <= px < 4:
            # A highlight along the plate's leading edges.
            r, g, b = r + 12, g + 12, b + 12
        hull_row.append(tuple(max(0, min(255, int(c))) for c in (r, g, b)))

        lit = (0, 0, 0)
        # Windows in every other row: a strip of small ports along the middle of the plate.
        if row % 2 == 1 and 13 <= py < 19 and 8 <= px < WIDE - 8 and (px - 8) % 12 < 7:
            warmth = 0.85 + 0.15 * ((col * 7 + row * 3) % 5) / 4
            if (row * 13 + col * 5 + (px - 8) // 12) % 6 != 0:
                lit = (255, int(214 * warmth), int(150 * warmth))
        light_row.append(lit)
    hull.append(hull_row)
    lights.append(light_row)

png('hull-panels-color.png', hull)
png('hull-panels-emissive.png', lights)
