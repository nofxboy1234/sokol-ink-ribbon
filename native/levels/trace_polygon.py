"""Trace Polygon's Care Center map into level-script tile commands.

    python trace_polygon.py MAP_8192.png MARKERS.json OUT_DIR

Writes floor1.txt, floor2.txt and floor3.txt (tiles, doors, items, rooms) in
the level-script language. Cells are CELL px of the 8192 px map, with the
map's top-left crop at (X0, Y0), so a cell (cx, cy) covers map pixels
(X0 + cx*CELL, Y0 + cy*CELL). Needs numpy, scipy and Pillow.
"""
import json
import sys

import numpy as np
from PIL import Image
from scipy import ndimage as ndi

CELL = 24
X0, Y0 = 1840, 80
COLS, ROWS = 184, 252
# floor bands in cell rows (map y = Y0 + row * CELL); a building block
# belongs to the band its centre falls in
FLOORS = {3: (0, 59), 2: (60, 141), 1: (142, 251)}
LINE_MIN = 3 * CELL
# a cell is in the building when it holds the outline wall's centre line,
# about 4 px in from the outline: the outline wall lands in the cell it is
# drawn in, not one cell inward
INSIDE_FRAC = 0.2

ITEM_MAP = {
    "Antique Coin": "coin",
    "Ink Ribbon": "ink_ribbon",
    "Empty Injector": "injector",
}
TITLE_MAP = {
    "Fuse": "fuse",
    "Lighter": "lighter",
    "Screwdriver": "screwdriver",
    "Cherub Key": "cherub_key",
}


def load(path):
    Image.MAX_IMAGE_PIXELS = None
    im = np.asarray(Image.open(path).convert("RGB")).astype(np.int16)
    return im[Y0:Y0 + ROWS * CELL, X0:X0 + COLS * CELL]


def cells(mask, frac):
    m = mask.reshape(ROWS, CELL, COLS, CELL).mean(axis=(1, 3))
    return m >= frac


def paired(lines, axis):
    """Keep the lines that have a twin a few px away: the map draws walls as
    double lines, floor patterns and furniture edges as single ones."""
    twin = np.zeros_like(lines)
    for d in range(4, 10):
        twin |= np.roll(lines, d, axis=axis) | np.roll(lines, -d, axis=axis)
    return lines & twin


def classify(im):
    lum = im.mean(axis=2)
    r, g, b = im[..., 0], im[..., 1], im[..., 2]
    grey = (np.abs(r - b) < 25) & (np.abs(r - g) < 25)
    inside_px = (lum >= 28.5) & grey
    inside_px = ndi.binary_closing(inside_px, np.ones((9, 9)))
    inside_px = ndi.binary_fill_holes(inside_px)
    inside = cells(inside_px, INSIDE_FRAC)
    lab, n = ndi.label(inside)
    sizes = ndi.sum(inside, lab, range(1, n + 1))
    inside = np.isin(lab, [i + 1 for i, s in enumerate(sizes) if s >= 40])

    line_px = (lum >= 50) & grey
    hl = paired(ndi.binary_opening(line_px, np.ones((1, LINE_MIN))), 0)
    vl = paired(ndi.binary_opening(line_px, np.ones((LINE_MIN, 1))), 1)
    hsum = hl.reshape(ROWS, CELL, COLS, CELL).sum(axis=(1, 3))
    vsum = vl.reshape(ROWS, CELL, COLS, CELL).sum(axis=(1, 3))
    # stair treads: many parallel lines per cell, where a wall has one or two
    stairs = ndi.binary_opening((hsum >= 5 * CELL) | (vsum >= 5 * CELL), np.ones((2, 2))) & inside
    hcell = (hsum >= CELL) & ~stairs
    vcell = (vsum >= CELL) & ~stairs

    cyan = (b > 170) & (g > 150) & (r < 160)
    red = (r > 120) & (r - g > 60) & (r - b > 50)
    return inside, hcell, vcell, cyan, red, stairs, hsum, vsum


def wall_pixels(im):
    """Map pixels on a wall line: the long double lines inside, and the
    building's outline wall (just inside the outline)."""
    lum = im.mean(axis=2)
    r, g, b = im[..., 0], im[..., 1], im[..., 2]
    grey = (np.abs(r - b) < 25) & (np.abs(r - g) < 25)
    inside_px = ndi.binary_fill_holes(ndi.binary_closing((lum >= 28.5) & grey, np.ones((9, 9))))
    line_px = (lum >= 50) & grey
    hl = paired(ndi.binary_opening(line_px, np.ones((1, LINE_MIN))), 0)
    vl = paired(ndi.binary_opening(line_px, np.ones((LINE_MIN, 1))), 1)
    outline = inside_px & ~ndi.binary_erosion(inside_px, iterations=8)
    return hl | vl | outline, inside_px


def align_walls(t, wallpx, stairs):
    """Move each straight wall run (one cell thick, at least 2 long) to the
    cell its map line's centre falls in, when that is the next cell over:
    the trace's cell rounding can leave a wall a cell off its line."""
    moved = 0
    for axis in (0, 1):
        tt = t if axis == 0 else t.T
        px = wallpx if axis == 0 else wallpx.T
        st = stairs if axis == 0 else stairs.T
        R, Cc = tt.shape
        for x in range(1, Cc - 1):
            y = 0
            while y < R:
                ok = tt[y, x] == 2 and tt[y, x - 1] != 2 and tt[y, x + 1] != 2 and not st[y, x]
                if not ok:
                    y += 1
                    continue
                y1 = y
                while (y1 + 1 < R and tt[y1 + 1, x] == 2 and tt[y1 + 1, x - 1] != 2 and tt[y1 + 1, x + 1] != 2
                       and not st[y1 + 1, x]):
                    y1 += 1
                y0, y = y, y1 + 1
                if y1 - y0 < 1:
                    continue
                prof = px[y0 * CELL:(y1 + 1) * CELL, (x - 1) * CELL:(x + 2) * CELL].sum(axis=0).astype(float)
                if prof.max() <= 0:
                    continue
                prof = np.convolve(prof, np.ones(9) / 9, mode="same")
                xn = x - 1 + int(np.argmax(prof)) // CELL
                if xn == x:
                    continue
                far = 2 * x - xn
                side = set(int(v) for v in tt[y0:y1 + 1, xn])
                back = set(int(v) for v in tt[y0:y1 + 1, far])
                if len(side) != 1 or len(back) != 1 or 2 in side or 2 in back:
                    continue
                tt[y0:y1 + 1, xn] = 2
                tt[y0:y1 + 1, x] = 1 if 1 in back or 1 in side and 0 not in back else 0
                if 0 in back:
                    tt[y0:y1 + 1, x] = 0 if 0 in side else 1
                for ye in (y0 - 1, y1 + 1):
                    if 0 <= ye < R and tt[ye, xn] == 1 and tt[ye, x] == 2:
                        tt[ye, xn] = 2
                moved += y1 - y0 + 1
    return moved


def thin_walls(t, hsum, vsum, min_run=3):
    """A wall line on a cell boundary marks the cells on both sides, so the
    trace draws it two cells thick and narrows the rooms. Where a straight
    run of wall is two cells thick with floor on both faces, keep the side
    holding more of the line (over the whole run) and give the other side
    back to the floor."""
    rows, cols = t.shape
    thinned = 0
    for axis in (0, 1):
        tt = t if axis == 0 else t.T
        line = vsum if axis == 0 else hsum.T
        R, C = tt.shape
        pair = np.zeros((R, C), dtype=bool)
        for y in range(R):
            for x in range(1, C - 2):
                pair[y, x] = tt[y, x] == 2 and tt[y, x + 1] == 2 and tt[y, x - 1] == 1 and tt[y, x + 2] == 1
        for x in range(C):
            y = 0
            while y < R:
                if not pair[y, x]:
                    y += 1
                    continue
                y1 = y
                while y1 + 1 < R and pair[y1 + 1, x]:
                    y1 += 1
                if y1 - y + 1 >= min_run:
                    k = x if line[y:y1 + 1, x].sum() < line[y:y1 + 1, x + 1].sum() else x + 1
                    tt[y:y1 + 1, k] = 1
                    thinned += y1 - y + 1
                y = y1 + 1
    return thinned


def doors(mask, locked):
    # close first so a hatched barricade (red and white stripes) is one bar
    lab, n = ndi.label(ndi.binary_closing(mask, np.ones((19, 19))))
    out = []
    for sl in ndi.find_objects(lab):
        h = sl[0].stop - sl[0].start
        w = sl[1].stop - sl[1].start
        if max(w, h) < 12:
            continue
        # a door is a bar; a square blob is a map marker (item/objective icon)
        if max(w, h) < 1.8 * min(w, h):
            continue
        cy = (sl[0].start + sl[0].stop) / 2 / CELL
        cx = (sl[1].start + sl[1].stop) / 2 / CELL
        if w >= h:
            span = max(1, round(w / CELL))
            out.append(("h", int(round(cx - span / 2)), int(cy), span, locked))
        else:
            span = max(1, round(h / CELL))
            out.append(("v", int(cx), int(round(cy - span / 2)), span, locked))
    return out


def tiles(inside, hcell, vcell, stairs):
    solid = ~inside
    pad = np.pad(inside, 1)
    edge = inside & ~(pad[:-2, 1:-1] & pad[2:, 1:-1] & pad[1:-1, :-2] & pad[1:-1, 2:]
                      & pad[:-2, :-2] & pad[:-2, 2:] & pad[2:, :-2] & pad[2:, 2:])
    wall = inside & (edge | hcell | vcell) & ~(stairs & ~edge)
    t = np.where(wall, 2, np.where(inside, 1, 0))
    return t, solid


# Room labels as printed on the map, in 1024 px overview coordinates.
LABELS = {
    3: {"Lounge": (316, 120), "Director's Office": (357, 122), "Meeting Room": (353, 142),
        "Elevator": (402, 96), "Attic Storage": (421, 157), "Attic": (570, 155),
        "Medical Supply Room": (625, 90)},
    2: {"Bar & Lounge": (390, 237), "Kitchenette": (345, 278), "Gallery": (434, 276), "Office": (348, 318),
        "Records Room": (380, 320), "Chairman's Office": (430, 333), "Central Hall": (505, 329),
        "Room 201": (562, 249), "Room 202": (599, 253), "Room 203": (709, 216),
        "Nurses' Station": (707, 307), "Conference Room": (620, 353), "Lead Researcher": (686, 392)},
    1: {"Cold Storage": (268, 473), "Filing Room": (396, 491), "Dining Room": (434, 495), "Restroom": (353, 511),
        "Pantry": (337, 563), "Kitchen": (377, 577), "Parlor": (417, 587), "Courtyard": (504, 484),
        "Central Hall": (505, 594), "East Wing Lobby": (565, 598), "Guard Office": (472, 627),
        "Medication Room": (436, 648), "Custodian's Office": (366, 637), "Garage": (307, 720),
        "Treatment Room": (595, 517), "Examination Room": (596, 545), "Blood Lab": (656, 524),
        "Isolation Ward": (740, 513), "Security Manager": (766, 546), "Waiting Room": (615, 600),
        "Closet": (626, 629), "Elevator": (593, 642), "Rehabilitation Ward": (667, 690)},
}


def to_cell(x1024, y1024):
    return int((x1024 * 8 - X0) // CELL), int((y1024 * 8 - Y0) // CELL)


def regions(names, floor_of, fl, door_list):
    """Floor cells split into rooms: walls and door edges separate them."""
    walk = (names == "floor") & (floor_of == fl)
    cut_h = np.zeros(walk.shape, bool)  # edge above cell
    cut_v = np.zeros(walk.shape, bool)  # edge left of cell
    for o, x, y, span, _ in door_list:
        for k in range(span):
            if o == "h" and 0 <= y < ROWS and 0 <= x + k < COLS:
                cut_h[y, x + k] = True
            if o == "v" and 0 <= y + k < ROWS and 0 <= x < COLS:
                cut_v[y + k, x] = True
    lab = np.zeros(walk.shape, int)
    n = 0
    for sy, sx in zip(*np.nonzero(walk)):
        if lab[sy, sx]:
            continue
        n += 1
        stack = [(sy, sx)]
        lab[sy, sx] = n
        while stack:
            y, x = stack.pop()
            for dy, dx, blocked in ((0, 1, x + 1 < COLS and cut_v[y, x + 1]), (0, -1, cut_v[y, x]),
                                    (1, 0, y + 1 < ROWS and cut_h[y + 1, x]), (-1, 0, cut_h[y, x])):
                ny, nx = y + dy, x + dx
                if 0 <= ny < ROWS and 0 <= nx < COLS and walk[ny, nx] and not lab[ny, nx] and not blocked:
                    lab[ny, nx] = n
                    stack.append((ny, nx))
    return lab, n


def snap_to_floor(names, floor_of, fl, x, y):
    for r in range(5):
        best = None
        for yy in range(y - r, y + r + 1):
            for xx in range(x - r, x + r + 1):
                if 0 <= yy < ROWS and 0 <= xx < COLS and names[yy, xx] == "floor" and floor_of[yy, xx] == fl:
                    d = (yy - y) ** 2 + (xx - x) ** 2
                    if best is None or d < best[0]:
                        best = (d, xx, yy)
        if best:
            return best[1], best[2]
    return x, y


def snap_door(names, o, x, y, span):
    """Move a door bar onto the doorway gap it marks: a run of floor cells in
    a wall line, walled at both ends, with floor on both faces."""
    def wall(xx, yy):
        return 0 <= yy < ROWS and 0 <= xx < COLS and names[yy, xx] not in ("", "floor")

    def flo(xx, yy):
        return 0 <= yy < ROWS and 0 <= xx < COLS and names[yy, xx] == "floor"

    best = None
    for dy in range(-2, 3):
        for dx in range(-2, 3):
            gx, gy = x + dx, y + dy
            if o == "h":
                if not (flo(gx, gy) and wall(gx - 1, gy)):
                    continue
                g = 0
                while flo(gx + g, gy) and g <= span + 1:
                    g += 1
                ok = wall(gx + g, gy) and all(flo(gx + k, gy - 1) and flo(gx + k, gy + 1) for k in range(g))
            else:
                if not (flo(gx, gy) and wall(gx, gy - 1)):
                    continue
                g = 0
                while flo(gx, gy + g) and g <= span + 1:
                    g += 1
                ok = wall(gx, gy + g) and all(flo(gx - 1, gy + k) and flo(gx + 1, gy + k) for k in range(g))
            if ok and g <= span + 1:
                d = dx * dx + dy * dy
                if best is None or d < best[0]:
                    best = (d, gx, gy, g)
    return best[1:] if best else None


def carve_door(names, o, x, y, span, reach=5):
    """No gap to snap to: the map's wall here is too short to trace. Build it
    across the opening, from the bar out to the walls on either side."""
    dx, dy = (1, 0) if o == "h" else (0, 1)
    cells = [(x + dx * k, y + dy * k) for k in range(span)]
    if any(not (0 <= cx < COLS and 0 <= cy < ROWS) or names[cy, cx] != "floor" for cx, cy in cells):
        return None
    ends = []
    for sign, (sx, sy) in ((-1, cells[0]), (1, cells[-1])):
        run = []
        cx, cy = sx + sign * dx, sy + sign * dy
        while 0 <= cx < COLS and 0 <= cy < ROWS and names[cy, cx] == "floor" and len(run) < reach:
            run.append((cx, cy))
            cx, cy = cx + sign * dx, cy + sign * dy
        if not (0 <= cx < COLS and 0 <= cy < ROWS) or names[cy, cx] in ("", "floor"):
            return None
        ends += run
    for cx, cy in ends:
        names[cy, cx] = "h" if o == "h" else "v"
    return x, y, span


def punch_door(names, o, x, y, span, depth=2):
    """The bar lies on wall cells (a thick wall, often under a barricade):
    open a doorway through the wall, walled along both sides."""
    dx, dy = (1, 0) if o == "h" else (0, 1)
    px, py = dy, dx  # across the wall
    cells = [(x + dx * k, y + dy * k) for k in range(span)]

    def wall(cx, cy):
        return 0 <= cx < COLS and 0 <= cy < ROWS and names[cy, cx] not in ("", "floor")

    if not all(wall(cx, cy) for cx, cy in cells):
        return None
    opened = list(cells)
    for sign in (-1, 1):
        for cx, cy in cells:
            k = 1
            while wall(cx + sign * px * k, cy + sign * py * k) and k <= depth:
                opened.append((cx + sign * px * k, cy + sign * py * k))
                k += 1
            nx, ny = cx + sign * px * k, cy + sign * py * k
            if not (0 <= nx < COLS and 0 <= ny < ROWS) or names[ny, nx] != "floor":
                return None
    for cx, cy in opened:
        names[cy, cx] = "floor"
    for cx, cy in opened:
        for ex, ey in ((cx - dx, cy - dy), (cx + dx, cy + dy)):
            if (ex, ey) not in opened and 0 <= ex < COLS and 0 <= ey < ROWS and names[ey, ex] in ("", "floor"):
                names[ey, ex] = "h" if o == "h" else "v"
    return x, y, span


def connect_rooms(names, floor_of, fl):
    """Open a one-cell gap wherever a single wall cell is all that separates
    two walkable pockets, until each block of the floor is one walkable
    space. The tracer reads some floor-pattern and railing lines as walls;
    this undoes the ones that would seal rooms the map leaves open."""
    opened = []
    while True:
        lab, n = regions(names, floor_of, fl, [])
        if n <= 1:
            return opened
        sizes = np.bincount(lab.ravel())
        best = None
        for y, x in zip(*np.nonzero((names != "floor") & (names != "") & (floor_of == fl))):
            for (ax, ay), (bx, by) in (((x - 1, y), (x + 1, y)), ((x, y - 1), (x, y + 1))):
                if not (0 <= ax < COLS and 0 <= bx < COLS and 0 <= ay < ROWS and 0 <= by < ROWS):
                    continue
                a, b = lab[ay, ax], lab[by, bx]
                if a and b and a != b:
                    score = -min(sizes[a], sizes[b])
                    if best is None or score < best[0]:
                        best = (score, x, y)
        if best is None:
            return opened
        names[best[2], best[1]] = "floor"
        opened.append((int(best[1]), int(best[2])))


def fit_door(names, o, x, y, g, span):
    """Grow a doorway toward the bar's length, one wall cell at a time while
    the cell has floor on both faces and the wall goes on past it; then
    check every slab cell joins two floor cells. Returns the final span,
    or 0 if the doorway does not join two floors."""
    dx, dy = (1, 0) if o == "h" else (0, 1)
    fx, fy = dy, dx

    def flo(cx, cy):
        return 0 <= cx < COLS and 0 <= cy < ROWS and names[cy, cx] == "floor"

    def wall(cx, cy):
        return 0 <= cx < COLS and 0 <= cy < ROWS and names[cy, cx] not in ("", "floor")

    while g < span:
        ex, ey = x + dx * g, y + dy * g
        if wall(ex, ey) and wall(ex + dx, ey + dy) and flo(ex - fx, ey - fy) and flo(ex + fx, ey + fy):
            names[ey, ex] = "floor"
            g += 1
        elif wall(x - dx, y - dy) and wall(x - 2 * dx, y - 2 * dy) and flo(x - dx - fx, y - dy - fy) \
                and flo(x - dx + fx, y - dy + fy):
            x, y = x - dx, y - dy
            names[y, x] = "floor"
            g += 1
        else:
            break
    for k in range(g):
        cx, cy = x + dx * k, y + dy * k
        if not (flo(cx, cy) and flo(cx - fx, cy - fy)):
            return None
    return x, y, g


# Every normal (cyan) door is as wide as Room 202's door: 2 cells. The red
# barricades keep the width the map draws.
DOOR_SPAN = 2


def widen_passage(names, o, x, y, reach=24):
    """A one-cell doorway in a one-cell passage: widen the whole passage by a
    cell on one side (the side with wall to spare), so the door fits."""
    dx, dy = (1, 0) if o == "h" else (0, 1)  # along the slab
    px, py = dy, dx                          # along the passage

    def at(cx, cy):
        return names[cy, cx] if 0 <= cx < COLS and 0 <= cy < ROWS else ""

    for side in (1, -1):
        sx, sy = x + dx * side, y + dy * side
        if at(sx, sy) in ("", "floor"):
            continue
        carve = []
        ok = True
        for sign in (1, -1):
            k = 0 if sign == 1 else 1
            while k <= reach:
                cx, cy = x + px * sign * k, y + py * sign * k
                if at(cx, cy) != "floor":
                    break
                nx, ny = cx + dx * side, cy + dy * side
                if at(nx, ny) == "floor":
                    break
                if at(nx, ny) == "" or at(nx + dx * side, ny + dy * side) == "floor":
                    ok = False
                    break
                carve.append((nx, ny))
                k += 1
            if k > reach:
                ok = False
        if not ok or not carve:
            continue
        for cx, cy in carve:
            names[cy, cx] = "floor"
            bx, by = cx + dx * side, cy + dy * side
            if at(bx, by) == "":
                names[by, bx] = "v" if o == "h" else "h"
        return (min(x, sx), min(y, sy), DOOR_SPAN)
    return None


def size_door(names, o, x, y, g):
    """Make a normal doorway DOOR_SPAN cells wide: grow it with fit_door, or
    wall off its ends, keeping it centred on the gap."""
    if g < DOOR_SPAN:
        fitted = fit_door(names, o, x, y, g, DOOR_SPAN)
        if fitted and fitted[2] >= DOOR_SPAN:
            return fitted
        return widen_passage(names, o, x, y) or (x, y, g)
    dx, dy = (1, 0) if o == "h" else (0, 1)
    lead = (g - DOOR_SPAN) // 2
    for k in list(range(lead)) + list(range(lead + DOOR_SPAN, g)):
        names[y + dy * k, x + dx * k] = o
    return x + dx * lead, y + dy * lead, DOOR_SPAN


def floor_map(inside):
    lab, n = ndi.label(inside, np.ones((3, 3)))
    rows = ndi.center_of_mass(inside, lab, range(1, n + 1))
    out = np.zeros(inside.shape, dtype=np.int8)
    for i, (cy, _) in enumerate(rows):
        fl = next(f for f, (r0, r1) in FLOORS.items() if r0 <= cy <= r1)
        out[lab == i + 1] = fl
    return out


def floor_at(floor_of, x, y):
    """The floor under (x, y), or of the nearest building cell within 3."""
    for r in range(4):
        ys, xs = np.mgrid[y - r:y + r + 1, x - r:x + r + 1]
        ok = (ys >= 0) & (ys < ROWS) & (xs >= 0) & (xs < COLS)
        vals = floor_of[ys[ok], xs[ok]]
        vals = vals[vals > 0]
        if len(vals):
            return int(np.bincount(vals).argmax())
    return 0


def glyph(t, x, y):
    def w(xx, yy):
        return 0 <= yy < t.shape[0] and 0 <= xx < t.shape[1] and t[yy, xx] == 2
    l, r, u, d = w(x - 1, y), w(x + 1, y), w(x, y - 1), w(x, y + 1)
    if r and d and not l and not u:
        return "nw"
    if l and d and not r and not u:
        return "ne"
    if r and u and not l and not d:
        return "sw"
    if l and u and not r and not d:
        return "se"
    if (u or d) and not (l or r):
        return "v"
    return "h"


def runs(row):
    x = 0
    while x < len(row):
        k = row[x]
        x1 = x
        while x1 + 1 < len(row) and row[x1 + 1] == k:
            x1 += 1
        yield x, x1, k
        x = x1 + 1


def main():
    out_dir = sys.argv[3]
    im = load(sys.argv[1])
    markers = json.load(open(sys.argv[2]))["markers"]
    inside, hcell, vcell, cyan, red, stairs, hsum, vsum = classify(im)
    t, _ = tiles(inside, hcell, vcell, stairs)
    thinned = thin_walls(t, hsum, vsum)
    wallpx, _ = wall_pixels(im)
    aligned = align_walls(t, wallpx, stairs)
    del wallpx
    lab, n = ndi.label(stairs)
    flights = [dict(x=int(sl[1].start), y=int(sl[0].start), w=int(sl[1].stop - sl[1].start),
                    h=int(sl[0].stop - sl[0].start)) for sl in ndi.find_objects(lab)]
    json.dump(flights, open(f"{out_dir}/stair_flights.json", "w"), indent=1)
    names = np.full(t.shape, "", dtype=object)
    for y in range(ROWS):
        for x in range(COLS):
            if t[y, x] == 1:
                names[y, x] = "floor"
            elif t[y, x] == 2:
                names[y, x] = glyph(t, x, y)
    ds = list(dict.fromkeys(doors(cyan, False) + doors(red, True)))
    floor_of = floor_map(inside)
    skipped = {}
    unplaced = []
    doors_on = {fl: [] for fl in FLOORS}
    for o, x, y, span, locked in ds:
        fl = floor_at(floor_of, x, y)
        if not fl:
            continue
        snapped = (snap_door(names, o, x, y, span) or carve_door(names, o, x, y, span)
                   or punch_door(names, o, x, y, span))
        if snapped:
            snapped = fit_door(names, o, snapped[0], snapped[1], snapped[2], span)
        if snapped and not locked:
            snapped = size_door(names, o, *snapped)
        if not snapped:
            unplaced.append(f"floor {fl}: {'red' if locked else 'cyan'} door bar near cell {x},{y}")
            continue
        d = (o, snapped[0], snapped[1], snapped[2], locked)
        if d not in doors_on[fl]:
            doors_on[fl].append(d)
    # walls the door fitting added outside the traced building join its floor
    for y, x in zip(*np.nonzero((names != "") & (floor_of == 0))):
        floor_of[y, x] = floor_at(floor_of, x, y)
    repairs = {fl: connect_rooms(names, floor_of, fl) for fl in FLOORS}
    for fl in FLOORS:
        fl_doors = doors_on[fl]
        lines = [f"# Floor {fl}: traced from Polygon's Care Center map by trace_polygon.py",
                 f"# cells {CELL}px of the 8192px map, crop origin ({X0}, {Y0})"]
        for y in range(ROWS):
            row = [k if floor_of[y, x] == fl else "" for x, k in enumerate(names[y])]
            for x0, x1, k in runs(row):
                if k:
                    lines.append(f"tile {k} {x0} {y} {x1} {y}" if x1 > x0 else f"tile {k} {x0} {y}")
        for o, x, y, span, locked in fl_doors:
            lines.append(f"door {x} {y} {o} {span}" + (" name=barrier  # red on the map" if locked else ""))
        lab, n = regions(names, floor_of, fl, fl_doors)
        labelled = {}
        for label, (lx, ly) in LABELS[fl].items():
            cx, cy = snap_to_floor(names, floor_of, fl, *to_cell(lx, ly))
            if lab[cy, cx]:
                labelled.setdefault(lab[cy, cx], label)
        for i in range(1, n + 1):
            ys, xs = np.nonzero(lab == i)
            name = labelled.get(i, "Corridor" if len(ys) > 40 else "Alcove")
            lines.append(f"section {xs.min()} {ys.min()} {xs.max() - xs.min() + 1} {ys.max() - ys.min() + 1} F{fl} {name}")
            if i in labelled:
                # a ceiling lamp over the floor cell nearest the room's middle
                k = int(np.argmin((ys - ys.mean()) ** 2 + (xs - xs.mean()) ** 2))
                radius = max(4, min(8, int(np.sqrt(len(ys)) / 2)))
                lines.append(f"obj light {xs[k]} {ys[k]} radius={radius} state=1 name=light")
        for m in markers:
            cx = int((m["x"] * 8192 - X0) // CELL)
            cy = int((m["y"] * 8192 - Y0) // CELL)
            if floor_at(floor_of, cx, cy) != fl:
                continue
            cx, cy = snap_to_floor(names, floor_of, fl, cx, cy)
            title = m["title"]
            item = TITLE_MAP.get(title) or ITEM_MAP.get(m["layer"])
            if not item and m["layer"] == "Restorative" and "Herb" in title:
                item = "herb"
            if not item and m["layer"] == "Throwable" and "Bottle" in title:
                item = "bottle"
            nm = title.replace(" ", "_")[:31]
            if item:
                lines.append(f"item {cx} {cy} {item} name={nm}")
            elif m["layer"] == "File" or m["layer"] == "Map":
                lines.append(f"obj file {cx} {cy} name={nm}")
            elif m["layer"] == "Save Point":
                lines.append(f"obj typewriter {cx} {cy} name={nm}")
            elif m["layer"] == "Safe":
                lines.append(f"obj safe {cx} {cy} name={nm}")
            else:
                skipped.setdefault(fl, {}).setdefault(f"{m['layer']}: {title}", 0)
                skipped[fl][f"{m['layer']}: {title}"] += 1
        open(f"{out_dir}/floor{fl}.txt", "w").write("\n".join(lines) + "\n")
    json.dump(dict(markers=skipped, door_bars_without_a_doorway=unplaced,
                   double_wall_cells_thinned=thinned, wall_cells_aligned_to_map=aligned,
                   wall_cells_opened_to_join_pockets={fl: len(v) for fl, v in repairs.items()}),
              open(f"{out_dir}/skipped.json", "w"), indent=1, sort_keys=True)
    np.save(f"{out_dir}/tiles.npy", t)


if __name__ == "__main__":
    main()
