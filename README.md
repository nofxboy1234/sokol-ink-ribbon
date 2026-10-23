# Ink Ribbon

A small browser game where you never steer the player. You choose. The decision
graph lists what the character can do from where they stand: move, take, use,
unlock, read. Confirm a node and the capsule walks there. When it arrives the
graph rearranges around the new possibilities.

The game has five views over one 3D room.

- **Game view** is three.js. White room, ink-black wall lines, a blue player
  capsule, the pink ink-ribbon sphere, and the green herb sphere.
- **2D decisions** is a full-screen tldraw canvas. Nodes are circles, edges are
  drawn lines, labels use the tldraw draw font.
- **3D decisions** is a full-screen three.js cloud of spheres. Drag to orbit,
  click a sphere, confirm with Enter.
- **Map** is a tldraw drawing of the rooms with a live blue player dot.
- **Level editor** is a tldraw canvas that edits the whole level: rooms,
  walls, doors, the player start, and the game objects. Move and resize a room,
  drag wall or door handles, place objects, then choose Apply and play. The
  three.js scene, map, and navigation grid are rebuilt from the edited level.

## Controls

| Key | Action |
| --- | --- |
| `G` | Open or close the 2D decision graph |
| `T` | Open or close the 3D decision graph |
| `M` | Open or close the map |
| `I` | Open or close the inventory |
| `E` | Open the tldraw level editor |
| `Enter` | Confirm the selected decision |
| `Esc` | Close the current view |

Click a node to start a route. Clicking another move node appends it as a
waypoint. Clicking an action node appends it to the current waypoint chain. The
selected node turns player blue, the route is shown as removable chips, and
Confirm closes the view and walks every waypoint with A* pathing before running
the final action.

When a target can be reached through more than one open door, the confirm bar
offers each route, for example `via the study door` and `via the arch`. Choosing
one inserts its door waypoints into the walk. The scene draws small rings at the
intermediate waypoints.

The level editor starts from the current level. Drag rooms to move them, resize
them with the selection handles, drag wall and door endpoints, or add new rooms,
walls, and doors from the bottom toolbar. The level is saved in localStorage and
the scene, map, and navigation grid are rebuilt from the edited level.

## The decision model

`web/src/game/decisions.ts` derives the graph from state. There is no separate
hand-authored node list.

- An entity appears only when it is discovered and its room is reachable through
  doors that are already open.
- Items add `move to X` and `take X` decisions.
- A held ink-ribbon adds type-writer decisions. A held green herb adds `use green
  herb`.
- A discovered door adds `move to door` plus `unlock door` when the key is held.
- Discovered travel through open doors adds `enter the study` for unseen rooms
  and `move to the atelier` for rooms already seen. These room targets can have
  several door routes.
- Reading the daybook reveals the study cabinet. Opening it with the daybook
  passcode wins the game.
- The player node anchors to the player's world position, and decision nodes
  anchor to their objects, so the 2D layout is a small floor plan of intent.

The starting room offers the ink-ribbon tree and the green herb tree. The locked
study door and the brass key stay out of the graph until the player gets close to
them, or until the type-writer reveals the key.

## Run

```sh
cd web
npm install
npm run dev
```

Open http://localhost:4173.

```sh
npm run typecheck
npm test
```

The Playwright suite drives the real app: it opens the graph with the keyboard,
chains move nodes into a multi-waypoint route, picks between two door routes,
confirms, waits for the capsule, checks inventory and graph changes, reads the
daybook to reveal the cabinet, opens it to reach the goal, and edits rooms, walls,
and objects in tldraw before checking the new geometry survives a reload.

## Layout

```
web/src/game/       level data, navigation, discovery, graph derivation, store
web/src/scene/      three.js room, capsule, items, camera, path walking
web/src/views/      2D tldraw graph, 3D three.js graph, tldraw map, level editor
web/src/App.tsx     view switching, keyboard input, HUD, confirm flow
```
