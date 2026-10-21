"""Place Polygon's Care Center pickups, fixtures and door locks (floors 1-3).

    python place_markers.py BASE.json MARKERS.json OUT_DIR

BASE.json is the level built without items.txt and locks.txt. Writes
OUT_DIR/items.txt (every pickup and the fixtures that matter for
progression) and OUT_DIR/locks.txt (what opens each locked door), and
prints what it left out. The rules are the tables below; edit them, not
the generated scripts.
"""
import json
import re
import sys

CELL, X0, Y0 = 24, 1840, 80
FLOORS = {3: (0, 59), 2: (60, 141), 1: (142, 251)}

# Pickups that trace_polygon.py already places (coins, ribbons, herbs...).
TRACED = {"Antique Coin", "Ink Ribbon", "Empty Injector", "Save Point", "Safe", "File", "Map"}
TRACED_TITLES = {"Fuse", "Lighter", "Screwdriver", "Cherub Key"}

# (title regex, item id, default qty). First match wins; the qty in
# brackets on the marker, e.g. "Handgun Ammo (10)", overrides the default.
# Item None: not placed. Shells, the MSBG 500, Chainsaw, grenades,
# Compensator and Connoisseur charm are Leon's (GameSpot weapons list,
# GameRant charms); the Red Jewel comes out of the Unicorn Trinket Box.
ITEMS = [
    (r"Handgun Ammo", "handgun_ammo", 5),
    (r"12\.7x", "magnum_ammo", 1),
    (r"Shotgun Shells", None, 0),
    (r"Infected Blood", "infected_blood", 20),
    (r"Transfusion Bag", "transfusion_bag", 1),
    (r"^Scrap", "scrap", 1),
    (r"Rare Metal", "rare_metal", 1),
    (r"Hip Pouch", "hip_pouch", 1),
    (r"^Lockpick", "lockpick", 1),
    (r"Specimen \(Denatured\)", "specimen_denatured", 1),
    (r"Specimen \(Converged\)", "specimen_converged", 1),
    (r"Specimen \(Reversible\)", "specimen_reversible", 1),
    (r"Connoisseur", None, 0),
    (r"Eye Spy", "charm_eye_spy", 1),
    (r"Stakeout", "charm_stakeout", 1),
    (r"S&S M232", "gun_m232", 1),
    (r"B934", "gun_b934", 1),
    (r"MSBG 500", None, 0),
    (r"Chainsaw", None, 0),
    (r"Hemolytic", "hemolytic_injector", 1),
    (r"Hunting Knife", "hunting_knife", 1),
    (r"Makeshift Knife", "makeshift_knife", 1),
    (r"East Wing Keycard", "east_wing_keycard", 1),
    (r"West Wing Keycard", "west_wing_keycard", 1),
    (r"Wristband \(Level 1\)", "wristband_1", 1),
    (r"Wristband \(Level 2\)", "wristband_2", 1),
    (r"Wristband \(Level 3\)", "wristband_3", 1),
    (r"Pantry Key", "pantry_key", 1),
    (r"Star Quartz", "star_quartz", 1),
    (r"Moon Quartz", "moon_quartz", 1),
    (r"Sun Quartz", "sun_quartz", 1),
    (r"Red Jewel", None, 0),
    (r"Artificial Heart", "artificial_heart", 1),
    (r"Organ Transport Box", "organ_box", 1),
    (r"Severed Hand", "severed_hand", 1),
    (r"Blood Collector", "blood_collector", 1),
    (r"^Wrench", "wrench", 1),
    (r"Override Manual", "override_manual", 1),
    (r"Stabilizer", "stabilizer", 1),
    (r"Steroids", "steroids", 1),
    (r"Corrosive", "corrosive", 1),
    (r"Pencil", "pencil", 1),
    (r"Unicorn Trinket Box", "unicorn_box", 1),
    (r"Leatherbound Notebook", "notebook", 1),
    (r"Med Injector", "injector", 1),
    (r"Hand Grenade", None, 0),
    (r"Compensator", None, 0),
    (r"Mr\. Raccoon", "mr_raccoon", 1),
]

# Locked Door markers: title -> door properties. Research: Prima and
# rogue.site walkthroughs, evilresource item pages, PC Gamer's security
# clearance and warped door guides.
LOCKS = {
    "Cherub Relief Door": "key=cherub_key",
    "West Wing Door": "key=west_wing_keycard",
    "East Wing Door": "key=east_wing_keycard",
    "Pantry Door": "key=pantry_key",
    "Security Point Lv. 1": "key=wristband_1",
    "Security Point Lv. 2": "key=wristband_2",
    "Security Point Lv. 3": "key=wristband_3",
    "Door with Indent": "key=red_jewel",
    "Door with Indents": "key=moon_quartz key2=sun_quartz key3=star_quartz",
    # the Sturdy Lock is the Sun puzzle box's padlock; Corrosive eats it
    # (GameRant, IGN)
    "Sturdy Lock": "key=corrosive",
    # Room 202's broken light: the room is open, just dark (bring the Lighter)
    "Broken Light": "lock=none",
    # warped doors open only for Leon's hatchet; blocked, broken and barred
    # doors never open in Grace's chapter
    "Warped Door": "lock=jammed",
    "Blocked Door": "lock=jammed",
    "Broken Door": "lock=jammed",
    "Barred Door": "lock=jammed",
    # plain Locked Doors open from one side; reach.c picks the side
    "Locked Door": "lock=other_side",
}
LOCK_REACH = 4  # cells from the marker to the door it labels

# Puzzle boxes, by the quartz each holds -> the item that opens it. No
# puzzles (Dylan): carrying the item the puzzle needs opens the box. Moon:
# the Pencil (rub the notepad); Star: the Level 3 Wristband (the box in the
# Level 3 ward); Sun: Corrosive (its Sturdy Lock padlock).
PUZZLES = {"moon_quartz": "pencil", "star_quartz": "wristband_3", "sun_quartz": "corrosive"}

# Parlor (F1 rooms 50..66 x 186..200): its prizes sit in coin cabinets at
# the Parlor price (GameSpot, Evil Resource, VGC).
PARLOR = {"hip_pouch": 3, "stabilizer": 4, "steroids": 4, "override_manual": 6}
PARLOR_BOX = (50, 186, 66, 200)


def floor_of(cy):
    for fl, (a, b) in FLOORS.items():
        if a <= cy <= b:
            return fl
    return 0


def main(base_path, markers_path, out_dir):
    lv = json.load(open(base_path))
    cols, rows = lv["cols"], lv["rows"]
    tiles = lv["tiles"]
    floor = lambda x, y: 0 <= x < cols and 0 <= y < rows and tiles[y][x] == "."
    doors = [o for o in lv["objects"] if o["kind"] == "door"]
    taken = {(o["x"], o["y"]) for o in lv["objects"] if o["kind"] != "door"}
    stairs = [o for o in lv["objects"] if o["kind"] == "stairs"]
    in_stairs = lambda x, y: any(s["x"] <= x < s["x"] + s["w"] and s["y"] <= y < s["y"] + s["h"] for s in stairs)
    door_cells = set()
    for d in doors:
        for k in range(d["span"]):
            x, y = (d["x"] + k, d["y"]) if d["horizontal"] else (d["x"], d["y"] + k)
            door_cells |= {(x, y), (x, y - 1)} if d["horizontal"] else {(x, y), (x - 1, y)}

    def snap(x, y, fl, free=True):
        for r in range(8):
            best = None
            for yy in range(y - r, y + r + 1):
                for xx in range(x - r, x + r + 1):
                    if not floor(xx, yy) or floor_of(yy) != fl or in_stairs(xx, yy):
                        continue
                    if free and ((xx, yy) in taken or (xx, yy) in door_cells):
                        continue
                    d = (xx - x) ** 2 + (yy - y) ** 2
                    if best is None or d < best[0]:
                        best = (d, xx, yy)
            if best:
                return best[1], best[2]
        return None

    def wall_cell(x, y):
        """A fixture cell against a wall whose floor neighbours stay joined,
        so the solid fixture never cuts a corridor."""
        def ok(cx, cy):
            if not floor(cx, cy) or (cx, cy) in taken or (cx, cy) in door_cells or in_stairs(cx, cy):
                return False
            ring = [(cx + dx, cy + dy) for dx, dy in ((-1, -1), (0, -1), (1, -1), (1, 0), (1, 1), (0, 1), (-1, 1), (-1, 0))]
            f = [floor(*c) for c in ring]
            if all(f[1::2]):
                return False  # no wall beside it
            # floor runs around the ring: one run means removing it keeps them joined
            runs = sum(1 for i in range(8) if f[i] and not f[i - 1])
            return runs <= 1
        for r in range(6):
            for yy in range(y - r, y + r + 1):
                for xx in range(x - r, x + r + 1):
                    if max(abs(xx - x), abs(yy - y)) == r and floor_of(yy) == floor_of(y) and ok(xx, yy):
                        return xx, yy
        return None

    def solid(x, y):
        return 0 <= x < cols and 0 <= y < rows and tiles[y][x] not in (".", " ")

    def punch(cx, cy, fl):
        best = None
        for y in range(cy - LOCK_REACH, cy + LOCK_REACH + 1):
            for x in range(cx - LOCK_REACH, cx + LOCK_REACH + 1):
                if floor_of(y) != fl:
                    continue
                # the wall carries on past both ends (jambs), so she can't
                # walk round the door
                for o, cells, a, b in (
                        ("h", [(x - 1, y), (x, y), (x + 1, y), (x + 2, y)], [(x, y - 1), (x + 1, y - 1)], [(x, y + 1), (x + 1, y + 1)]),
                        ("v", [(x, y - 1), (x, y), (x, y + 1), (x, y + 2)], [(x - 1, y), (x - 1, y + 1)], [(x + 1, y), (x + 1, y + 1)])):
                    if all(solid(*c) for c in cells) and all(floor(*c) for c in a + b) \
                            and not any(c in door_cells for c in a + b):
                        d = (x - cx) ** 2 + (y - cy) ** 2
                        if best is None or d < best[0]:
                            best = (d, o, x, y)
        return best[1:] if best else None

    markers = json.load(open(markers_path))["markers"]
    items, fixtures, locks, left = [], [], [], {}
    placed = []  # (item, x, y) for puzzles and drawers
    parlor = []  # (x, y, item): Parlor prizes, sold from coin cabinets
    boxes = []  # Item Box cells
    for m in markers:
        cx = int((m["x"] * 8192 - X0) // CELL)
        cy = int((m["y"] * 8192 - Y0) // CELL)
        fl = floor_of(cy)
        layer, title = m["layer"], m["title"]
        if not fl or layer in TRACED or title in TRACED_TITLES or (layer == "Restorative" and "Herb" in title) \
                or (layer == "Throwable" and "Bottle" in title):
            continue
        if layer == "Locked Door":
            props = LOCKS.get(title)
            if props is None:
                left.setdefault(f"F{fl} door {title}", 0)
                left[f"F{fl} door {title}"] += 1
                continue
            locks.append((fl, cx, cy, title, props))
            continue
        hit = next(((it, q) for rx, it, q in ITEMS if re.search(rx, title)), None)
        if hit and hit[0] is None:
            k = f"skipped (Leon's, or Grace gets it another way): {title}"
            left[k] = left.get(k, 0) + 1
            continue
        if hit and hit[0] in PARLOR and fl == 1 and PARLOR_BOX[0] <= cx <= PARLOR_BOX[2] and PARLOR_BOX[1] <= cy <= PARLOR_BOX[3]:
            parlor.append((cx, cy, hit[0]))
            continue
        if hit:
            it, qty = hit
            n = re.search(r"\((\d+)\)", title)
            if n and it not in ("transfusion_bag",):
                qty = int(n.group(1))
            pos = snap(cx, cy, fl)
            if not pos:
                left.setdefault(f"F{fl} {title} (no floor)", 0)
                continue
            taken.add(pos)
            nm = re.sub(r"[^A-Za-z0-9.()]+", "_", title)[:31]
            q = f" qty={qty}" if qty > 1 else ""
            items.append(f"item {pos[0]} {pos[1]} {it}{q} name={nm}  # F{fl} {layer}")
            placed.append((it, pos[0], pos[1], fl))
            continue
        fixtures.append((fl, cx, cy, layer, title))

    out_fix = []
    for fl, cx, cy, layer, title in fixtures:
        if layer == "Laser Microscope":
            pos = wall_cell(*snap(cx, cy, fl))
            if pos:
                taken.add(pos)
                out_fix.append(f"obj microscope {pos[0]} {pos[1]} name=Laser_Microscope  # F{fl}")
                continue
        elif layer == "Warped Closet":
            pos = wall_cell(*snap(cx, cy, fl))
            if pos:
                taken.add(pos)
                out_fix.append(f"obj obstacle {pos[0]} {pos[1]} openable=1 lock=jammed name=Warped_Closet  # F{fl}")
                continue
        elif layer == "Simple Lock":
            # the lock guards the nearest pickup within 2 cells, if any
            near = [(abs(x - cx) + abs(y - cy), it, x, y) for it, x, y, f in placed if f == fl and abs(x - cx) <= 2 and abs(y - cy) <= 2]
            pos = wall_cell(*snap(cx, cy, fl))
            if pos:
                taken.add(pos)
                note = f" guards {min(near)[1]}" if near else ""
                out_fix.append(f"obj obstacle {pos[0]} {pos[1]} openable=1 key=lockpick name={title.replace(' ', '_')}  # F{fl}{note}")
                continue
        elif layer == "Puzzle Box":
            near = sorted((abs(x - cx) + abs(y - cy), it, x, y) for it, x, y, f in placed if f == fl and it in PUZZLES)
            if near and near[0][0] <= 12:
                _, it, x, y = near[0]
                out_fix.append(f"obj safe {x} {y} key={PUZZLES[it]} name=Puzzle_Box  # F{fl} holds {it}")
                continue
        elif layer == "Item Box":
            pos = wall_cell(*snap(cx, cy, fl))
            if pos:
                taken.add(pos)
                boxes.append(pos)
                out_fix.append(f"obj itembox {pos[0]} {pos[1]} name=Item_Box  # F{fl} Polygon marker")
                continue
        key = f"F{fl} {layer}: {title}"
        left[key] = left.get(key, 0) + 1

    # the Parlor prizes: a coin cabinet each, against the wall by the marker
    for cx, cy, it in parlor:
        pos = wall_cell(*snap(cx, cy, 1))
        if not pos:
            left[f"F1 Parlor {it} (no wall)"] = left.get(f"F1 Parlor {it} (no wall)", 0) + 1
            continue
        taken.add(pos)
        out_fix.append(f"obj cabinet {pos[0]} {pos[1]} item={it} qty=1 code={PARLOR[it]} name=Coin_Cabinet  # F1 Parlor")

    # an Item Box beside each typewriter the map has no Item Box near (RE's
    # safe-room storage trunks)
    for t in [o for o in lv["objects"] if o["kind"] == "typewriter"]:
        if any(abs(bx - t["x"]) + abs(by - t["y"]) <= 10 for bx, by in boxes):
            continue
        pos = wall_cell(t["x"], t["y"])
        if pos:
            taken.add(pos)
            out_fix.append(f"obj itembox {pos[0]} {pos[1]} name=Item_Box  # F{floor_of(t['y'])} by the typewriter at {t['x']},{t['y']}")

    out_locks = []
    used = set()
    punched = set()
    for fl, cx, cy, title, props in locks:
        best = None
        for i, d in enumerate(doors):
            # doors links.txt already scripted (the cherub door, the gate the
            # fuse box powers) keep their own rules
            if i in used or floor_of(d["y"]) != fl or d.get("name", "door") not in ("door", "barrier"):
                continue
            span = d["span"]
            mx = d["x"] + (span / 2 if d["horizontal"] else 0)
            my = d["y"] + (0 if d["horizontal"] else span / 2)
            dist = max(abs(mx - cx), abs(my - cy))
            if dist <= LOCK_REACH and (best is None or dist < best[0]):
                best = (dist, i, d)
        scripted = [d for d in doors if floor_of(d["y"]) == fl and d.get("name", "door") not in ("door", "barrier")
                    and max(abs(d["x"] - cx), abs(d["y"] - cy)) <= LOCK_REACH]
        if not best and scripted:
            key = f"F{fl} {title} at {cx},{cy}: kept the scripted '{scripted[0]['name']}' door"
            left[key] = left.get(key, 0) + 1
            continue
        if not best:
            # the map draws a lock where our trace left a wall: punch a
            # span-2 doorway through the nearest one-cell wall that has floor
            # on both faces
            hole = punch(cx, cy, fl)
            if hole in punched:
                continue  # two markers on one doorway
            if hole:
                punched.add(hole)
                o, x, y = hole
                nm = re.sub(r"[^A-Za-z0-9.]+", "_", title)[:31]
                out_locks.append(f"door {x} {y} {o} 2 {props} name={nm}  # F{fl} marker {cx},{cy}, no doorway traced")
                continue
            key = f"F{fl} {title} (no door or wall within {LOCK_REACH} cells of {cx},{cy})"
            left[key] = left.get(key, 0) + 1
            continue
        used.add(best[1])
        d = best[2]
        # barrier doors keep their name: it is what lets them keep the
        # width the map draws
        nm = "" if d.get("name") == "barrier" else " name=" + re.sub(r"[^A-Za-z0-9.]+", "_", title)[:31]
        out_locks.append(f"set {d['x']} {d['y']} {props}{nm}  # F{fl} marker {cx},{cy}")

    head = "# Generated by place_markers.py from Polygon's Care Center markers. Do not edit.\n"
    open(f"{out_dir}/items.txt", "w").write(head + "\n".join(items + out_fix) + "\n")
    open(f"{out_dir}/locks.txt", "w").write(head + "\n".join(out_locks) + "\n")
    print(f"items {len(items)}, fixtures {len(out_fix)}, locks {len(out_locks)}")
    for k, v in sorted(left.items()):
        print(f"left out: {k} x{v}")


if __name__ == "__main__":
    main(*sys.argv[1:4])
