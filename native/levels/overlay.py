"""Draw a level JSON over the Polygon map crop it was traced from.

    python overlay.py LEVEL.json MAP_8192.png OUT_PREFIX

Writes OUT_PREFIX_floorN.png (map at 50% with the level's tiles, doors and
items on top) and prints, per floor, how many map-interior cells the level
covers and how many level cells fall outside the map interior.
"""
import json
import sys

import numpy as np
from PIL import Image, ImageDraw

sys.path.insert(0, __file__.rsplit("/", 1)[0])
from trace_polygon import COLS, ROWS, classify, floor_map, load  # noqa: E402

S = 12  # output px per cell

lv = json.load(open(sys.argv[1]))
im = load(sys.argv[2])
inside = classify(im)[0]
floor_of = floor_map(inside)
BANDS = {fl: (int(ys.min()), int(ys.max())) for fl in (1, 2, 3) for ys in [np.nonzero(floor_of == fl)[0]]}
tiles = np.array([[c for c in row.ljust(COLS)[:COLS]] for row in lv["tiles"]])
built = tiles != " "
base = Image.fromarray(im.astype(np.uint8)).resize((COLS * S, ROWS * S))
for fl, (r0, r1) in sorted(BANDS.items()):
    img = base.crop((0, r0 * S, COLS * S, (r1 + 1) * S)).convert("RGBA")
    over = Image.new("RGBA", img.size)
    d = ImageDraw.Draw(over)
    for y in range(r0, r1 + 1):
        for x in range(COLS):
            c = tiles[y, x]
            box = [x * S, (y - r0) * S, x * S + S - 1, (y - r0) * S + S - 1]
            if c == ".":
                d.rectangle(box, fill=(255, 255, 255, 60))
            elif c != " ":
                d.rectangle(box, fill=(255, 200, 0, 150))
    for o in lv["objects"]:
        if not (r0 <= o["y"] <= r1):
            continue
        x, y = o["x"] * S, (o["y"] - r0) * S
        if o["kind"] == "door":
            span = o.get("span", 1) * S
            col = (255, 0, 40, 255) if o.get("state") == "locked" else (30, 255, 250, 255)
            box = [x, y - 3, x + span, y + 3] if o.get("horizontal") else [x - 3, y, x + 3, y + span]
            d.rectangle(box, fill=col)
        elif o["kind"] == "stairs":
            d.rectangle([x, y, x + o["w"] * S, y + o["h"] * S], outline=(0, 255, 0, 255), width=3)
        else:
            d.ellipse([x + 2, y + 2, x + S - 2, y + S - 2], fill=(255, 0, 255, 255))
    Image.alpha_composite(img, over).convert("RGB").save(f"{sys.argv[3]}_floor{fl}.png")
    want, got = floor_of == fl, built & (floor_of == fl)
    print(f"floor {fl}: map interior {want.sum()} cells, level covers {(want & got).sum()} "
          f"({100 * (want & got).sum() / max(1, want.sum()):.1f}%), level cells outside the map {(built & ~inside).sum() if fl == 1 else 0}")
