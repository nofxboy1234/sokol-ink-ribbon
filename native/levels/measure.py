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
                out.append((sc["name"], px, py, sc))
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


def line_masks(im):
    """Map line pixels by direction (plus door bars and the outline wall,
    which bound a room on either axis)."""
    inside, hcell, vcell, cyan, red, stairs, hsum, vsum = tp.classify(im)
    lum = im.mean(axis=2)
    r, g, b = im[..., 0], im[..., 1], im[..., 2]
    grey = (np.abs(r - b) < 25) & (np.abs(r - g) < 25)
    line_px = (lum >= 50) & grey
    # a cell long is enough here: short walls (stubs, door jambs) count too
    hl = tp.paired(ndi.binary_opening(line_px, np.ones((1, tp.CELL))), 0)
    vl = tp.paired(ndi.binary_opening(line_px, np.ones((tp.CELL, 1))), 1)
    _, inside_px = tp.wall_pixels(im)
    outline = inside_px & ~ndi.binary_erosion(inside_px, iterations=8)
    doors = ndi.binary_closing(cyan | red, np.ones((19, 19)))
    return hl | outline | doors, vl | outline | doors


def single_masks(im):
    """Single map lines (no twin): stair rails and thin partitions."""
    lum = im.mean(axis=2)
    r, g, b = im[..., 0], im[..., 1], im[..., 2]
    grey = (np.abs(r - b) < 25) & (np.abs(r - g) < 25)
    line_px = (lum >= 50) & grey
    return (ndi.binary_opening(line_px, np.ones((1, tp.CELL))),
            ndi.binary_opening(line_px, np.ones((tp.CELL, 1))))


SNAP = 3  # cells: how far from a level wall its map line may be found
MIN_COVER = 0.3


def snap_edge(mask, axis, pos, a0, a1):
    """The map line nearest a level wall edge. axis 0: a horizontal edge at
    y=pos (px) spanning x a0..a1; axis 1: vertical at x=pos over y a0..a1.
    Returns (centre px, coverage) of the best-covered parallel line within
    SNAP cells, or (None, cover) if no line runs along this wall."""
    C = tp.CELL
    a0, a1 = int(a0 + C), int(a1 - C)  # skip the corners
    if a1 - a0 < C // 2:
        a0, a1 = int(a0 - C * 0.75), int(a1 + C * 0.75)
    lo, hi = int(pos - SNAP * C), int(pos + SNAP * C)
    band = mask[max(lo, 0):hi, a0:a1] if axis == 0 else mask[a0:a1, max(lo, 0):hi].T
    cover = band.mean(axis=1) if band.size else np.zeros(1)
    best = cover.max()
    if best < MIN_COVER:
        return None, float(best)
    # the nearest well-covered line, not merely the best: a neighbouring
    # room's wall a few cells over must not win over this wall's own line
    ok = np.nonzero(cover >= MIN_COVER)[0]
    k = int(ok[np.argmin(np.abs(ok + max(lo, 0) - pos))])
    best = cover[k]
    i, j = k, k
    while i > 0 and cover[i - 1] >= 0.8 * best:
        i -= 1
    while j < len(cover) - 1 and cover[j + 1] >= 0.8 * best:
        j += 1
    return max(lo, 0) + (i + j + 1) / 2, float(best)


def wall_runs(tiles):
    """Straight runs (2+ cells) of wall tiles that face floor on one side:
    (axis, fixed, a0, a1) -- axis 0 a horizontal run in row `fixed` over
    columns a0..a1, axis 1 a vertical run in column `fixed`."""
    T = np.array([list(r) for r in tiles])
    floor = T == "."
    wall = (T != ".") & (T != " ")
    H, W = T.shape
    pf = np.pad(floor, 1)
    up, down, left, right = pf[:-2, 1:-1], pf[2:, 1:-1], pf[1:-1, :-2], pf[1:-1, 2:]
    runs = []
    for axis, face in ((0, wall & (up | down) & ~(left | right) | (wall & (T == "-") & (up | down))),
                       (1, wall & (left | right) & ~(up | down) | (wall & (T == "|") & (left | right)))):
        m = face if axis == 0 else face.T
        for f in range(m.shape[0]):
            row = m[f]
            a = 0
            while a < len(row):
                if not row[a]:
                    a += 1
                    continue
                b = a
                while b + 1 < len(row) and row[b + 1]:
                    b += 1
                if b - a + 1 >= 2:
                    runs.append((axis, f, a, b))
                a = b + 1
    return runs


def run_offsets(tiles, hmask, vmask, hsingle=None, vsingle=None):
    """Each wall run's offset (cells) from the centre of the map line along
    it: the wall cell's centre minus the line's. None when the map has no
    line there (a wall the map doesn't draw)."""
    C = tp.CELL
    out = []
    for axis, f, a, b in wall_runs(tiles):
        mask = hmask if axis == 0 else vmask
        pos = (f + 0.5) * C
        c, cv = snap_edge(mask, axis, pos, a * C, (b + 1) * C)
        kind = "double"
        if hsingle is not None and (c is None or abs(pos - c) > C / 2):
            # no wall line within half a cell: a single line (stair rail,
            # thin partition) may be what this wall stands for
            s_mask = hsingle if axis == 0 else vsingle
            c2, cv2 = snap_edge(s_mask, axis, pos, a * C, (b + 1) * C)
            if c2 is not None and (c is None or abs(pos - c2) < abs(pos - c)):
                c, cv, kind = c2, cv2, "single"
        out.append(dict(axis="h" if axis == 0 else "v", at=f, start=a, end=b,
                        offset=None if c is None else round(f + 0.5 - c / C, 2), cover=round(cv, 2), line=kind))
    return out


def emit_align(lv, runs, path):
    """Levelscript that moves every wall run more than half a cell off its
    map line one cell toward it (the cell rounding left it on the wrong
    side). Only safe moves: one-cell-thick runs whose cells on both sides
    are uniform, never closing a corridor, never next to a door gap."""
    T = [list(r) for r in lv["tiles"]]
    H, W = len(T), len(T[0])
    gaps, jambs = set(), set()
    for o in lv["objects"]:
        if o["kind"] == "door":
            for i in range(o["span"]):
                gaps.add((o["x"] + i, o["y"]) if o["horizontal"] else (o["x"], o["y"] + i))
            # the wall cells either side of a door: opening one leaves a gap beside it
            n = o["span"]
            jambs.update([(o["x"] - 1, o["y"]), (o["x"] + n, o["y"])] if o["horizontal"]
                         else [(o["x"], o["y"] - 1), (o["x"], o["y"] + n)])
    stair_cells = set()
    for o in lv["objects"]:
        if o["kind"] == "stairs":
            for dy in range(-1, o.get("h", 1) + 1):
                for dx in range(-1, o.get("w", 1) + 1):
                    stair_cells.add((o["x"] + dx, o["y"] + dy))
    at = lambda x, y: T[y][x] if 0 <= x < W and 0 <= y < H else " "
    lines, moved, skipped = [], 0, []
    for r in runs:
        off = r["offset"]
        if off is None or abs(off) <= 0.5 or abs(off) > 1.5:
            continue
        step = -1 if off > 0 else 1
        f, a, b = r["at"], r["start"], r["end"]
        cell = (lambda k, d: (k, f + d)) if r["axis"] == "h" else (lambda k, d: (f + d, k))
        ks = range(a, b + 1)
        inner = range(a + 1, b) if b - a >= 2 else ks
        tgt = {at(*cell(k, step)) for k in inner}
        beyond = {at(*cell(k, 2 * step)) for k in inner}
        far = {at(*cell(k, -step)) for k in inner}
        why = None
        thick = bool(tgt) and tgt <= set("-|abcd")  # a double wall: keep the half on the line
        if len(far) != 1 or far & set("-|abcd") or not thick and (len(tgt) != 1 or tgt & set("-|abcd")):
            why = "sides not uniform"
        elif not thick and tgt == {"."} and beyond != {"."}:
            why = "would close a corridor"
        elif any(cell(k, 0) in gaps for k in range(a - 1, b + 2)) or any(
                cell(k, d) in gaps for k in range(a, b + 1) for d in (step, 2 * step)):
            why = "a door sits in this wall or where it would go (move it by hand)"
        elif any(cell(k, 0) in jambs for k in ks):
            why = "a door's jamb (moving it would open a gap beside the door)"
        elif any(cell(k, d) in stair_cells for k in ks for d in (0, step)):
            why = "on a staircase (its walls are set by hand in fixes.txt)"
        if why:
            skipped.append(f"# skipped {r['axis']} {f} {a}-{b} offset {off}: {why}")
            continue
        # the run's end cells move too unless a cross wall carries on there
        span = [k for k in ks if k in inner or (at(*cell(k, step)) in tgt and at(*cell(k, -step)) in far)]
        if thick:
            span = [k for k in ks if k in inner or at(*cell(k, -step)) in far]
        newf = "floor" if far == {"."} else "void"
        secs = lv["sections"]
        in_sec = lambda x, y: any(sc["x"] <= x < sc["x"] + sc["w"] and sc["y"] <= y < sc["y"] + sc["h"] for sc in secs)
        if newf == "floor":  # a cell turned to floor must not open onto the void
            # (the target cell becomes the wall, so it may be void now)
            span = [k for k in span if all(at(*n) != " " for n in (
                cell(k - 1, 0), cell(k + 1, 0), cell(k, -step)) + (() if not thick and False else (cell(k, step),))
                if not (n == cell(k, step) and not thick))]
        if not span:
            skipped.append(f"# skipped {r['axis']} {f} {a}-{b} offset {off}: would open onto the void")
            continue
        lines.append(f"# {r['axis']} wall {f} {a}-{b} is {off:+.2f} cells off its map line: {'thin the double wall to its line' if thick else 'move it one cell'}"
                     f" {'up' if r['axis'] == 'h' and step < 0 else 'down' if r['axis'] == 'h' else 'left' if step < 0 else 'right'}")
        for k in span:
            c0, c1 = cell(k, 0), cell(k, step)
            lines.append(f"{newf} {c0[0]} {c0[1]} {c0[0]} {c0[1]}")
            if newf == "floor" and not in_sec(*c0):
                lines.append(f"cover {c0[0]} {c0[1]}")
            if not thick:
                lines.append(f"tile {'h' if r['axis'] == 'h' else 'v'} {c1[0]} {c1[1]}")
        moved += 1
    with open(path, "w") as fh:
        fh.write("# Generated by measure.py --emit-align: wall runs moved onto their map line.\n")
        fh.write("# Regenerate: build without it, measure, build again (see build.sh --align).\n")
        fh.write("\n".join(lines + skipped) + "\n")
    return moved, len(skipped)


def measure(lv, im):
    """Each room's level rect (median rays between its walls) against the map:
    every one of its four walls is snapped to the map line running along it,
    so openings, L-shapes and rings on the map can't stretch the result."""
    lwalls = level_walls(lv)
    mwalls = close_doorways(lv, map_walls(im))
    hmask, vmask = line_masks(im)
    C = tp.CELL
    runs = run_offsets(lv["tiles"], hmask, vmask, *single_masks(im))
    run_cells = {}
    for i, r in enumerate(runs):
        for k in range(r["start"], r["end"] + 1):
            run_cells.setdefault((k, r["at"]) if r["axis"] == "h" else (r["at"], k), []).append(i)
    # door gaps are floor cells in a wall line: not part of either room
    gapmask = np.zeros_like(lwalls)
    for o in lv["objects"]:
        if o["kind"] == "door":
            for i in range(o["span"]):
                gx, gy = (o["x"] + i, o["y"]) if o["horizontal"] else (o["x"], o["y"] + i)
                gapmask[gy * C:(gy + 1) * C, gx * C:(gx + 1) * C] = True
    rows = []
    for name, px, py, sc in seeds(lv, mwalls):
        # the level room: the seed's open region inside its reveal section
        X0, Y0 = sc["x"] * C, sc["y"] * C
        win = ~(lwalls | gapmask)[Y0:Y0 + sc["h"] * C, X0:X0 + sc["w"] * C]
        lab, _ = ndi.label(win)
        reg = lab == lab[py - Y0, px - X0]
        ys, xs = np.nonzero(reg)
        h2 = C // 2
        L = [X0 + xs.min() - h2, Y0 + ys.min() - h2, X0 + xs.max() + 1 + h2, Y0 + ys.max() + 1 + h2]
        lr = [v / C for v in L]
        # each bbox wall's extent: where the region actually touches it
        ext = []
        top, bot = ys == ys.min(), ys == ys.max()
        lef, rig = xs == xs.min(), xs == xs.max()
        ext = [(Y0 + ys[lef].min(), Y0 + ys[lef].max() + 1), (X0 + xs[top].min(), X0 + xs[top].max() + 1),
               (Y0 + ys[rig].min(), Y0 + ys[rig].max() + 1), (X0 + xs[bot].min(), X0 + xs[bot].max() + 1)]
        m, cov = [], []
        wall_at = [(X0 + xs.min()) // C - 1, (Y0 + ys.min()) // C - 1, (X0 + xs.max()) // C + 1, (Y0 + ys.max()) // C + 1]
        for i, (mask, axis) in enumerate(((vmask, 1), (hmask, 0), (vmask, 1), (hmask, 0))):
            a0, a1 = ext[i]
            # the edge's own wall runs say where its map line is
            on = [r for r in runs if r["axis"] == ("v" if axis == 1 else "h") and r["at"] == wall_at[i]
                  and r["offset"] is not None and r["start"] * C < a1 and (r["end"] + 1) * C > a0]
            if on:
                m.append(L[i] / C - float(np.mean([r["offset"] for r in on])))
                cov.append(round(float(np.mean([r["cover"] for r in on])), 2))
                continue
            c, cv = snap_edge(mask, axis, L[i], a0 - C, a1 + C)
            m.append(L[i] / C if c is None else c / C)
            cov.append(round(cv, 2) if c is not None else 0.0)
        unmatched = sum(1 for c in cov if c == 0.0)
        # the walls around this room: runs touching its floor cells
        cells = set()
        for cy in range(sc["y"], sc["y"] + sc["h"]):
            for cx in range(sc["x"], sc["x"] + sc["w"]):
                yy, xx = cy * C + C // 2 - Y0, cx * C + C // 2 - X0
                if 0 <= yy < reg.shape[0] and 0 <= xx < reg.shape[1] and reg[yy, xx]:
                    cells.add((cx, cy))
        mine = set()
        for cx, cy in cells:
            for n in ((cx + 1, cy), (cx - 1, cy), (cx, cy + 1), (cx, cy - 1)):
                mine.update(run_cells.get(n, ()))
        offs = [runs[i]["offset"] for i in mine if runs[i]["offset"] is not None]
        worst = max(mine, key=lambda i: -1 if runs[i]["offset"] is None else abs(runs[i]["offset"]), default=None)
        d = [lr[i] - m[i] for i in range(4)]
        rows.append(dict(floor=int(name[1]), name=name[3:], seed_x=px // tp.CELL, seed_y=py // tp.CELL,
                         map_x=round(m[0], 2), map_y=round(m[1], 2), map_w=round(m[2] - m[0], 2), map_h=round(m[3] - m[1], 2),
                         level_x=round(lr[0], 2), level_y=round(lr[1], 2), level_w=round(lr[2] - lr[0], 2),
                         level_h=round(lr[3] - lr[1], 2),
                         dx0=round(d[0], 2), dy0=round(d[1], 2), dx1=round(d[2], 2), dy1=round(d[3], 2),
                         edge_err=round(max(abs(v) for v in d), 2),
                         size_err=round(max(abs(d[2] - d[0]), abs(d[3] - d[1])), 2),
                         walls_without_map_line=unmatched,
                         wall_runs=len(mine), wall_err=round(max((abs(o) for o in offs), default=0.0), 2),
                         wall_err_mean=round(float(np.mean([abs(o) for o in offs])) if offs else 0.0, 2),
                         runs_without_map_line=sum(1 for i in mine if runs[i]["offset"] is None),
                         worst_run="" if worst is None else "{axis} {at} {start}-{end} {offset}".format(**runs[worst]),
                         cover_l=cov[0], cover_t=cov[1], cover_r=cov[2], cover_b=cov[3]))
    rows.sort(key=lambda r: (-r["floor"], r["name"], r["seed_y"], r["seed_x"]))
    return rows, runs


def summary(rows):
    errs = [r["size_err"] for r in rows]
    edges = [r["edge_err"] for r in rows]
    off = sum(1 for e in errs if e > 0.5)
    return (f"{len(rows)} rooms and corridors; size error max {max(errs):.2f} mean {np.mean(errs):.2f};"
            f" edge error max {max(edges):.2f} mean {np.mean(edges):.2f}; {off} off by more than 0.5 cell;"
            f" {sum(r['walls_without_map_line'] for r in rows)} level walls with no map line;"
            f" wall offset max {max(r['wall_err'] for r in rows):.2f} mean {np.mean([r['wall_err_mean'] for r in rows]):.2f},"
            f" {sum(1 for r in rows if r['wall_err'] > 0.5)} rooms with a wall more than 0.5 cell off its map line")


def main():
    lv = json.load(open(sys.argv[1]))
    rows, runs = measure(lv, tp.load(sys.argv[2]))
    with open(sys.argv[3].replace(".csv", "_walls.csv"), "w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=list(runs[0].keys()))
        w.writeheader()
        w.writerows(runs)
    with open(sys.argv[3], "w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=list(rows[0].keys()))
        w.writeheader()
        w.writerows(rows)
    print(summary(rows))
    if "--emit-align" in sys.argv:
        out = sys.argv[sys.argv.index("--emit-align") + 1]
        print("align: %d runs moved, %d skipped" % emit_align(lv, runs, out))


if __name__ == "__main__":
    main()
