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
- **Level editor** is a tldraw canvas that edits the game objects used to
  generate the 3D room. Dragging an object, adding an object, then choosing
  Apply and play rebuilds the three.js scene from the edited level.

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

Click a node to select it. The selected node turns player blue and the parent
chain is shown as the route in the confirm bar. Click Confirm, or press Enter,
to commit. The view closes, the capsule walks its path using A* pathing, and the
graph updates when you open it again.

The level editor loads the current level as tldraw game objects. Drag the player
or item dots, add objects from the bottom toolbar, then choose Apply and play.
The level is saved in localStorage and the 3D scene is rebuilt from the edited
level. Room and wall geometry stays fixed in this version.

## The decision model

`web/src/game/decisions.ts` derives the graph from state. There is no separate
hand-authored node list.

- An entity appears only when it is discovered and its room is reachable through
  doors that are already open.
- Items add `move to X` and `take X` decisions.
- A held ink-ribbon adds type-writer decisions. A held green herb adds `use green
  herb`.
- A discovered door adds `move to door` plus `unlock door` when the key is held.
  An open door adds `enter the study` until the room has been seen.
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
clicks nodes, confirms, waits for the capsule to arrive, checks inventory and
graph changes, drags an object in the tldraw level editor, applies the level, and
checks the new position survives a reload.

## Layout

```
web/src/game/       level data, navigation, discovery, graph derivation, store
web/src/scene/      three.js room, capsule, items, camera, path walking
web/src/views/      2D tldraw graph, 3D three.js graph, tldraw map, level editor
web/src/App.tsx     view switching, keyboard input, HUD, confirm flow
```
