"""Measure every room and corridor of the built level against Polygon's map.

    python measure.py LEVEL.json MAP_8192.png OUT.csv

Every level section (each traced room and corridor) gets a seed cell. From
that seed, rays find the room's walls on the map (wall lines, door bars, the
building outline) and in the level (wall tiles, doors); the median ray over
rows and columns spread across the room gives its rect between wall centre
lines, so one doorway does not move a wall. Writes one CSV row per room: both
rects in cells, each edge's offset, and the edge and size errors. Needs
numpy, scipy and Pillow.
"""
import csv
import json
import sys

import numpy as np
from scipy import ndimage as ndi

import trace_polygon as tp

WALL_HALF_PX = 4  # a ray's first wall pixel to the centre of the double line
SPREAD = 9        # rays per pass, spread across the room


def map_walls(im):
    """Pixels that bound a room on the map: wall lines, door bars, outside."""
    inside, hcell, vcell, cyan, red, stairs, hsum, vsum = tp.classify(im)
    lum = im.mean(axis=2)
    r, g, b = im[..., 0], im[..., 1], im[..., 2]
    grey = (np.abs(r - b) < 25) & (np.abs(r - g) < 25)
    inside_px = ndi.binary_fill_holes(ndi.binary_closing((lum >= 28.5) & grey, np.ones((9, 9))))
    line_px = (lum >= 50) & grey
    hl = tp.paired(ndi.binary_opening(line_px, np.ones((1, tp.LINE_MIN))), 0)
    vl = tp.paired(ndi.binary_opening(line_px, np.ones((tp.LINE_MIN, 1))), 1)
    stair_px = np.kron(stairs, np.ones((tp.CELL, tp.CELL), dtype=bool))
    doors = ndi.binary_closing(cyan | red, np.ones((19, 19)))
    return ((hl | vl) & ~stair_px) | doors | ~inside_px


def level_walls(lv):
    """The level at CELL px per cell, so one ray caster serves both: floor
    cells are open, door edges and every other tile are walls."""
    C = tp.CELL
    floor = np.array([[c == "." for c in row] for row in lv["tiles"]])
    return ~np.kron(floor, np.ones((C, C), dtype=bool)) | door_lines(lv, (floor.shape[0] * C, floor.shape[1] * C))


def ray(walls, x, y, dx, dy, limit):
    h, wd = walls.shape
    for k in range(limit):
        px, py = x + dx * k, y + dy * k
        if not (0 <= px < wd and 0 <= py < h) or walls[py, px]:
            return k
    return limit


def room_rect(walls, sx, sy, half, limit=80 * tp.CELL):
    """The room around seed pixel (sx, sy) between wall centre lines, in
    cells: median ray lengths from rows (then columns) spread across it."""
    x0, x1, y0, y1 = sx - 1, sx + 1, sy - 1, sy + 1
    for _ in range(3):
        ys = [int(v) for v in np.linspace(y0 + 2, y1 - 2, SPREAD)] if y1 - y0 > 6 else [sy]
        ys = [y for y in ys if not walls[y, sx]] or [sy]
        x0 = sx - np.median([ray(walls, sx, y, -1, 0, limit) for y in ys])
        x1 = sx + np.median([ray(walls, sx, y, 1, 0, limit) for y in ys])
        xs = [int(v) for v in np.linspace(x0 + 2, x1 - 2, SPREAD)] if x1 - x0 > 6 else [sx]
        xs = [x for x in xs if not walls[sy, x]] or [sx]
        y0 = sy - np.median([ray(walls, x, sy, 0, -1, limit) for x in xs])
        y1 = sy + np.median([ray(walls, x, sy, 0, 1, limit) for x in xs])
    C = tp.CELL
    return (x0 - half) / C, (y0 - half) / C, (x1 + half) / C, (y1 + half) / C


def seeds(lv, mwalls):
    """One seed per level section, at the floor cell nearest its middle that
    is open on the map too."""
    floor = np.array([[c == "." for c in row] for row in lv["tiles"]])
    C = tp.CELL
    out = []
    for sc in lv["sections"]:
        ys, xs = np.nonzero(floor[sc["y"]:sc["y"] + sc["h"], sc["x"]:sc["x"] + sc["w"]])
        if not len(xs):
            continue
        xs, ys = xs + sc["x"], ys + sc["y"]
        for k in np.argsort((xs - xs.mean()) ** 2 + (ys - ys.mean()) ** 2):
            px, py = int(xs[k] * C + C // 2), int(ys[k] * C + C // 2)
            if not mwalls[py, px]:
                out.append((sc["name"], px, py))
                break
    return out


def door_lines(lv, shape):
    """The level's doors as px lines. They sit on the map's door bars, so they
    split the map's rooms where its bars do (a bar alone leaves gaps a ray
    slips through beside it)."""
    C = tp.CELL
    w = np.zeros(shape, dtype=bool)
    for o in lv["objects"]:
        if o["kind"] != "door":
            continue
        x, y = o["x"] * C, o["y"] * C
        if o["horizontal"]:
            w[max(0, y - 1):y + 1, x:x + o["span"] * C] = True
        else:
            w[y:y + o["span"] * C, max(0, x - 1):x + 1] = True
    return w


def close_doorways(lv, mwalls, reach=2 * tp.CELL):
    """The map draws a door bar shorter than its doorway: extend each level
    door's line along its axis, both ways, up to the map's walls, so the
    doorway separates the rooms the way the door does in the level."""
    C = tp.CELL
    w = mwalls.copy()
    h, wd = w.shape
    for o in lv["objects"]:
        if o["kind"] != "door":
            continue
        x, y, n = o["x"] * C, o["y"] * C, o["span"] * C
        if o["horizontal"]:
            a = x
            while a > 0 and x - a < reach and not mwalls[y, a - 1]:
                a -= 1
            b = x + n
            while b < wd and b - (x + n) < reach and not mwalls[y, b]:
                b += 1
            w[max(0, y - 1):y + 1, a:b] = True
        else:
            a = y
            while a > 0 and y - a < reach and not mwalls[a - 1, x]:
                a -= 1
            b = y + n
            while b < h and b - (y + n) < reach and not mwalls[b, x]:
                b += 1
            w[a:b, max(0, x - 1):x + 1] = True
    return w


def measure(lv, mwalls):
    lwalls = level_walls(lv)
    mwalls = close_doorways(lv, mwalls)
    rows = []
    for name, px, py in seeds(lv, mwalls):
        m = room_rect(mwalls, px, py, WALL_HALF_PX)
        lr = room_rect(lwalls, px, py, tp.CELL // 2)
        d = [lr[i] - m[i] for i in range(4)]
        rows.append(dict(floor=int(name[1]), name=name[3:], seed_x=px // tp.CELL, seed_y=py // tp.CELL,
                         map_x=round(m[0], 2), map_y=round(m[1], 2), map_w=round(m[2] - m[0], 2), map_h=round(m[3] - m[1], 2),
                         level_x=round(lr[0], 2), level_y=round(lr[1], 2), level_w=round(lr[2] - lr[0], 2),
                         level_h=round(lr[3] - lr[1], 2),
                         dx0=round(d[0], 2), dy0=round(d[1], 2), dx1=round(d[2], 2), dy1=round(d[3], 2),
                         edge_err=round(max(abs(v) for v in d), 2),
                         size_err=round(max(abs(d[2] - d[0]), abs(d[3] - d[1])), 2)))
    rows.sort(key=lambda r: (-r["floor"], r["name"], r["seed_y"], r["seed_x"]))
    return rows


def summary(rows):
    errs = [r["size_err"] for r in rows]
    edges = [r["edge_err"] for r in rows]
    off = sum(1 for e in errs if e > 0.5)
    return (f"{len(rows)} rooms and corridors; size error max {max(errs):.2f} mean {np.mean(errs):.2f};"
            f" edge error max {max(edges):.2f} mean {np.mean(edges):.2f}; {off} off by more than 0.5 cell")


def main():
    lv = json.load(open(sys.argv[1]))
    rows = measure(lv, map_walls(tp.load(sys.argv[2])))
    with open(sys.argv[3], "w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=list(rows[0].keys()))
        w.writeheader()
        w.writerows(rows)
    print(summary(rows))


if __name__ == "__main__":
    main()
