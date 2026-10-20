"""Play a level JSON's key chain on paper: is every step reachable?

    python check_level.py LEVEL.json [REACH.png]

Walks from the start through floor tiles, unlocked doors and stairs. A
locked door opens once the item it links to is held, an openable gives up
what sits on it once its linked item is held, and the fusebox breaks every
door named "gate" once the fuse is held. Prints what was reached, in order,
and exits 1 if the goal is never reached or Grace can step off the tiles.
"""
import json
import sys

lv = json.load(open(sys.argv[1]))
tiles = lv["tiles"]
rows, cols = lv["rows"], lv["cols"]
objs = lv["objects"]
by_id = {o["id"]: o for o in objs}


def floor(x, y):
    return 0 <= y < rows and 0 <= x < len(tiles[y]) and tiles[y][x] == "."


def door_edges(o):
    for k in range(o.get("span", 1)):
        if o.get("horizontal"):
            yield ("h", o["x"] + k, o["y"])
        else:
            yield ("v", o["x"], o["y"] + k)


def covers(o, x, y):
    return o["x"] <= x < o["x"] + o.get("w", 1) and o["y"] <= y < o["y"] + o.get("h", 1)


bad = [f"{o['kind']} {o.get('name', '')} at {o['x']},{o['y']}" for o in objs
       if o["kind"] in ("start", "stairs", "item", "goal", "fusebox") and not floor(o["x"], o["y"])]
if not floor(*lv["start"]):
    bad.append(f"start cell {lv['start']}")
for b in bad:
    print(f"NOT ON FLOOR: {b}")

held, log = set(), []
broken = set()


def blocked_edges():
    out = set()
    for o in objs:
        if o["kind"] != "door" or o["id"] in broken:
            continue
        key = by_id.get(o.get("link", 0))
        if o.get("state") == "locked" and not (key and key["id"] in held):
            out.update(door_edges(o))
    return out


def flood():
    block = blocked_edges()
    sx, sy = lv["start"]
    seen = {(sx, sy)}
    stack = [(sx, sy)]
    leak = None
    while stack:
        x, y = stack.pop()
        for nx, ny, edge in ((x + 1, y, ("v", x + 1, y)), (x - 1, y, ("v", x, y)),
                             (x, y + 1, ("h", x, y + 1)), (x, y - 1, ("h", x, y))):
            if edge in block or (nx, ny) in seen:
                continue
            if not (0 <= nx < cols and 0 <= ny < rows):
                leak = (x, y)
                continue
            if floor(nx, ny):
                seen.add((nx, ny))
                stack.append((nx, ny))
        for s in objs:
            if s["kind"] == "stairs" and covers(s, x, y) and s.get("link") in by_id:
                d = by_id[s["link"]]
                cell = (d["x"] + d.get("w", 1) // 2, d["y"] + d.get("h", 1) // 2)
                if cell not in seen:
                    seen.add(cell)
                    stack.append(cell)
    return seen, leak


goal_open = False
while True:
    seen, leak = flood()
    progress = False
    for o in objs:
        if (o["x"], o["y"]) not in seen:
            continue
        if o["kind"] == "item" and o["id"] not in held:
            box = next((b for b in objs if b["kind"] == "obstacle" and b.get("openable") and covers(b, o["x"], o["y"])), None)
            if box and box.get("link") and box["link"] not in held:
                continue
            held.add(o["id"])
            log.append(f"take {o['item']} at {o['x']},{o['y']}")
            progress = True
        if o["kind"] == "fusebox" and not goal_open and any(by_id[h]["item"] == "fuse" for h in held):
            goal_open = True
            for d in objs:
                if d["kind"] == "door" and d.get("name") == "gate":
                    broken.add(d["id"])
            log.append(f"fuse into fusebox at {o['x']},{o['y']}: gate open")
            progress = True
    if not progress:
        break

goal = next((o for o in objs if o["kind"] == "goal"), None)
reached = goal is not None and goal_open and (goal["x"], goal["y"]) in seen
for line in log:
    print(line)
print(f"walkable cells reached: {len(seen)}")
unreached = [o for o in objs if o["kind"] == "item" and o["id"] not in held]
print(f"items reached {len(held)}, not reached {len(unreached)}")
for o in unreached:
    print(f"  not reached: {o['item']} at {o['x']},{o['y']}")
if leak:
    print(f"LEAK: Grace can walk off the map at {leak}")
print("GOAL REACHED" if reached else "GOAL NOT REACHED")
if len(sys.argv) > 2:
    from PIL import Image
    img = Image.new("RGB", (cols, rows))
    px = img.load()
    for y in range(rows):
        for x in range(cols):
            c = tiles[y][x] if x < len(tiles[y]) else " "
            px[x, y] = (0, 200, 0) if (x, y) in seen else (230, 230, 230) if c == "." else (60, 60, 60) if c != " " else (0, 0, 0)
    for o in objs:
        if o["kind"] == "door":
            for _, x, y in door_edges(o):
                px[x, y] = (255, 0, 40) if o.get("state") == "locked" else (0, 220, 255)
    img.resize((cols * 4, rows * 4), Image.NEAREST).save(sys.argv[2])
sys.exit(0 if reached and not leak and not bad else 1)
