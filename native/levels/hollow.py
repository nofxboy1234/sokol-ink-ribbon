"""Hollow out the inside of thick walls.

    python3 hollow.py LEVEL.json > scripts/hollow.txt

The tracer fills a map shaft (two thin wall lines with a dark gap between,
1.5-2.5 cells across) with solid wall, which reads as a 3+ cell thick wall
in game. A wall cell with no floor in its 8 neighbours is never a wall face:
it is that dark gap, so it becomes void. Cells under or beside objects
(stairs, doors) are kept."""
import json
import sys

lv = json.load(open(sys.argv[1]))
t = lv["tiles"]
H, W = len(t), len(t[0])
keep = set()
for o in lv["objects"]:
    w, h = o.get("w", 1), o.get("h", 1)
    if o["kind"] == "door":
        w, h = (o["span"], 1) if o["horizontal"] else (1, o["span"])
    for y in range(o["y"] - 2, o["y"] + h + 2):
        for x in range(o["x"] - 2, o["x"] + w + 2):
            keep.add((x, y))
n = 0
for y in range(H):
    x = 0
    while x < W:
        def hollow(x):
            if t[y][x] in " ." or (x, y) in keep:
                return False
            return all(t[yy][xx] != "." for yy in range(max(y - 1, 0), min(y + 2, H))
                       for xx in range(max(x - 1, 0), min(x + 2, W)))
        if hollow(x):
            s = x
            while x + 1 < W and hollow(x + 1):
                x += 1
            print(f"void {s} {y} {x} {y}")
            n += 1
        x += 1
print(f"# {n} runs hollowed", file=sys.stderr)
