import { useEffect, useRef, useState } from "react";
import { createShapeId, Tldraw, toRichText, type Editor, type TLShape } from "tldraw";
import "tldraw/tldraw.css";
import { cloneLevel, defaultLevel } from "../game/levelStorage";
import { roomAt } from "../game/decisions";
import type { DoorDef, Entity, Level, Room, Vec2, Wall } from "../game/types";
import { tldrawAssets } from "../tldrawAssets";

type Props = {
  level: Level;
  onApply: (level: Level) => void;
  onCancel: () => void;
};

type GameRole = "player" | "entity" | "room" | "wall" | "door";

type GameMeta = {
  game?: { role: GameRole; id?: string };
};

const SCALE = 44;
const SNAP = 0.1;

const pagePoint = (point: Vec2): { x: number; y: number } => ({ x: point.x * SCALE, y: point.z * SCALE });
const snap = (value: number): number => Math.round(value / SNAP) * SNAP;
const clamp = (value: number, min: number, max: number): number => Math.max(min, Math.min(max, value));
const normalOf = (dir: Vec2): Vec2 => ({ x: -dir.z, z: dir.x });
const add = (point: Vec2, other: Vec2, scale = 1): Vec2 => ({ x: point.x + other.x * scale, z: point.z + other.z * scale });

const clampIntoRooms = (level: Level, point: Vec2): Vec2 => {
  if (level.rooms.length === 0) return point;
  const current = level.rooms.find(
    (room) =>
      point.x >= room.box.minX &&
      point.x <= room.box.maxX &&
      point.z >= room.box.minZ &&
      point.z <= room.box.maxZ,
  );
  const room =
    current ??
    level.rooms.reduce((best, candidate) => {
      const center = { x: (candidate.box.minX + candidate.box.maxX) / 2, z: (candidate.box.minZ + candidate.box.maxZ) / 2 };
      const distance = Math.hypot(point.x - center.x, point.z - center.z);
      return distance < best.distance ? { room: candidate, distance } : best;
    }, { room: level.rooms[0], distance: Infinity }).room;
  return {
    x: clamp(point.x, room.box.minX + 0.7, room.box.maxX - 0.7),
    z: clamp(point.z, room.box.minZ + 0.7, room.box.maxZ - 0.7),
  };
};

const styleFor = (entity: Entity): "light-violet" | "light-green" | "yellow" | "grey" | "blue" => {
  if (entity.itemId === "ink-ribbon") return "light-violet";
  if (entity.itemId === "green-herb") return "light-green";
  if (entity.itemId === "brass-key") return "yellow";
  if (entity.station === "typewriter" || entity.station === "cabinet") return "grey";
  return "blue";
};

const shortLabel = (name: string): string =>
  name
    .replace("ink-ribbon", "ribbon")
    .replace("green herb", "herb")
    .replace("brass key", "key")
    .replace("type-writer", "type")
    .replace("study cabinet", "drawer")
    .replace("study door", "door")
    .replace("daybook", "book");

const metaFor = (shape: TLShape): GameMeta["game"] | undefined => (shape.meta as GameMeta).game;

const shapeCenter = (editor: Editor, shape: TLShape): Vec2 => {
  const bounds = editor.getShapePageBounds(shape);
  if (bounds) return { x: (bounds.x + bounds.w / 2) / SCALE, z: (bounds.y + bounds.h / 2) / SCALE };
  return { x: shape.x / SCALE, z: shape.y / SCALE };
};

const arrowEnds = (shape: TLShape): { start: Vec2; end: Vec2 } => {
  const props = shape.props as { start: { x: number; y: number }; end: { x: number; y: number } };
  return {
    start: { x: (shape.x + props.start.x) / SCALE, z: (shape.y + props.start.y) / SCALE },
    end: { x: (shape.x + props.end.x) / SCALE, z: (shape.y + props.end.y) / SCALE },
  };
};

const createRoomShape = (editor: Editor, room: Room): void => {
  editor.createShape({
    id: createShapeId(`room-${room.id}`),
    type: "geo",
    x: room.box.minX * SCALE,
    y: room.box.minZ * SCALE,
    meta: { game: { role: "room", id: room.id } } satisfies GameMeta,
    props: {
      geo: "rectangle",
      w: (room.box.maxX - room.box.minX) * SCALE,
      h: (room.box.maxZ - room.box.minZ) * SCALE,
      color: "grey",
      fill: "none",
      richText: toRichText(room.name),
      size: "s",
      font: "draw",
      align: "start",
      verticalAlign: "start",
      scale: 0.5,
    },
  });
};

const createWallShape = (editor: Editor, wall: Wall): void => {
  const a = pagePoint(wall.a);
  const b = pagePoint(wall.b);
  editor.createShape({
    id: createShapeId(`wall-${wall.id}`),
    type: "arrow",
    x: a.x,
    y: a.y,
    meta: { game: { role: "wall", id: wall.id } } satisfies GameMeta,
    props: {
      start: { x: 0, y: 0 },
      end: { x: b.x - a.x, y: b.y - a.y },
      color: "grey",
      size: "m",
      arrowheadStart: "none",
      arrowheadEnd: "none",
    },
  });
};

const createDoorShape = (editor: Editor, door: DoorDef): void => {
  const half = door.width / 2;
  const start = add(door.pos, door.dir, -half);
  const end = add(door.pos, door.dir, half);
  const a = pagePoint(start);
  const b = pagePoint(end);
  editor.createShape({
    id: createShapeId(`door-${door.id}`),
    type: "arrow",
    x: a.x,
    y: a.y,
    meta: { game: { role: "door", id: door.id } } satisfies GameMeta,
    props: {
      start: { x: 0, y: 0 },
      end: { x: b.x - a.x, y: b.y - a.y },
      color: "black",
      labelColor: "black",
      size: "s",
      font: "draw",
      richText: toRichText(door.name),
      labelPosition: 0.5,
      arrowheadStart: "none",
      arrowheadEnd: "none",
    },
  });
};

const createEntityShape = (editor: Editor, entity: Entity): void => {
  const point = pagePoint(entity.pos);
  editor.createShape({
    id: createShapeId(`game-entity-${entity.id}`),
    type: "geo",
    x: point.x - 24,
    y: point.y - 24,
    meta: { game: { role: "entity", id: entity.id } } satisfies GameMeta,
    props: {
      geo: "ellipse",
      w: 48,
      h: 48,
      color: styleFor(entity),
      fill: "fill",
      richText: toRichText(shortLabel(entity.name)),
      size: "s",
      font: "draw",
      align: "middle",
      verticalAlign: "middle",
      scale: 0.52,
    },
  });
};

const createPlayerShape = (editor: Editor, level: Level): void => {
  const playerPoint = pagePoint(level.start.pos);
  editor.createShape({
    id: createShapeId("game-player"),
    type: "geo",
    x: playerPoint.x - 26,
    y: playerPoint.y - 26,
    meta: { game: { role: "player" } } satisfies GameMeta,
    props: {
      geo: "ellipse",
      w: 52,
      h: 52,
      color: "light-blue",
      fill: "fill",
      richText: toRichText("player"),
      size: "s",
      font: "draw",
      align: "middle",
      verticalAlign: "middle",
      scale: 0.45,
    },
  });
};

const seedShapes = (editor: Editor, level: Level): void => {
  editor.run(() => {
    for (const room of level.rooms) createRoomShape(editor, room);
    for (const wall of level.walls) createWallShape(editor, wall);
    for (const door of level.doors) createDoorShape(editor, door);
    for (const entity of level.entities) {
      if (entity.kind !== "door") createEntityShape(editor, entity);
    }
    createPlayerShape(editor, level);
  });

  const min = { x: Infinity, y: Infinity };
  const max = { x: -Infinity, y: -Infinity };
  for (const room of level.rooms) {
    min.x = Math.min(min.x, room.box.minX * SCALE);
    min.y = Math.min(min.y, room.box.minZ * SCALE);
    max.x = Math.max(max.x, room.box.maxX * SCALE);
    max.y = Math.max(max.y, room.box.maxZ * SCALE);
  }
  if (Number.isFinite(min.x)) {
    editor.zoomToBounds({ x: min.x - 60, y: min.y - 60, w: max.x - min.x + 120, h: max.y - min.y + 120 }, { inset: 40 });
  }
};

const sanitize = (level: Level): Level | null => {
  const next = cloneLevel(level);
  next.rooms = next.rooms.filter((room) => room.box.maxX - room.box.minX >= 2 && room.box.maxZ - room.box.minZ >= 2);
  if (next.rooms.length === 0) return null;

  const roomIds = new Set(next.rooms.map((room) => room.id));
  next.doors = next.doors.filter((door) => roomIds.has(door.roomA) && roomIds.has(door.roomB));
  const doorIds = new Set(next.doors.map((door) => door.id));
  next.entities = next.entities.filter((entity) => entity.kind !== "door" || (entity.doorId && doorIds.has(entity.doorId)));

  for (const entity of next.entities) {
    if (entity.doorId) {
      const door = next.doors.find((candidate) => candidate.id === entity.doorId);
      if (door) {
        entity.pos = door.pos;
        entity.standAt = door.throughA;
        entity.roomId = door.roomA;
        continue;
      }
    }
    entity.pos = clampIntoRooms(next, entity.pos);
    entity.standAt = clampIntoRooms(next, entity.standAt);
    entity.roomId = roomAt(next, entity.pos) ?? next.rooms[0].id;
  }

  next.start.pos = clampIntoRooms(next, next.start.pos);
  next.start.roomId = roomAt(next, next.start.pos) ?? next.rooms[0].id;
  next.walls = next.walls.filter(
    (wall) => Number.isFinite(wall.a.x) && Number.isFinite(wall.a.z) && Number.isFinite(wall.b.x) && Number.isFinite(wall.b.z),
  );
  return next;
};

export function LevelEditor({ level, onApply, onCancel }: Props) {
  const [editor, setEditor] = useState<Editor | null>(null);
  const [error, setError] = useState<string | null>(null);
  const draft = useRef<Level>(cloneLevel(level));
  const seeding = useRef(true);
  const timer = useRef(0);
  const [count, setCount] = useState(level.entities.length);

  const sync = (): void => {
    const mounted = editor;
    if (!mounted) return;
    const next = draft.current;
    const shapes = mounted.getCurrentPageShapes();
    const roomShapes = shapes.filter((shape) => metaFor(shape)?.role === "room");
    const wallShapes = shapes.filter((shape) => metaFor(shape)?.role === "wall");
    const doorShapes = shapes.filter((shape) => metaFor(shape)?.role === "door");
    const entityShapes = shapes.filter((shape) => metaFor(shape)?.role === "entity");
    const playerShape = shapes.find((shape) => metaFor(shape)?.role === "player");

    const presentRooms = new Set<string>();
    for (const shape of roomShapes) {
      const meta = metaFor(shape);
      const room = meta?.id ? next.rooms.find((candidate) => candidate.id === meta.id) : undefined;
      const bounds = mounted.getShapePageBounds(shape);
      if (!room || !bounds) continue;
      const minX = snap(bounds.x / SCALE);
      const minZ = snap(bounds.y / SCALE);
      const maxX = snap((bounds.x + bounds.w) / SCALE);
      const maxZ = snap((bounds.y + bounds.h) / SCALE);
      if (maxX - minX < 2 || maxZ - minZ < 2) {
        presentRooms.add(room.id);
        continue;
      }
      room.box = { minX, maxX, minZ, maxZ };
      presentRooms.add(room.id);
    }
    if (presentRooms.size > 0) next.rooms = next.rooms.filter((room) => presentRooms.has(room.id));
    if (next.rooms.length === 0) return;

    const presentWalls = new Set<string>();
    for (const shape of wallShapes) {
      const meta = metaFor(shape);
      if (!meta?.id) continue;
      const ends = arrowEnds(shape);
      const existing = next.walls.find((candidate) => candidate.id === meta.id);
      if (existing) {
        existing.a = ends.start;
        existing.b = ends.end;
      } else {
        next.walls.push({ id: meta.id, a: ends.start, b: ends.end });
      }
      presentWalls.add(meta.id);
    }
    next.walls = next.walls.filter((wall) => presentWalls.has(wall.id));

    const presentDoors = new Set<string>();
    for (const shape of doorShapes) {
      const meta = metaFor(shape);
      if (!meta?.id) continue;
      const door = next.doors.find((candidate) => candidate.id === meta.id);
      if (!door) continue;
      const ends = arrowEnds(shape);
      const dx = ends.end.x - ends.start.x;
      const dz = ends.end.z - ends.start.z;
      const width = Math.hypot(dx, dz);
      if (width < 0.8) continue;
      const dir = { x: dx / width, z: dz / width };
      const pos = { x: (ends.start.x + ends.end.x) / 2, z: (ends.start.z + ends.end.z) / 2 };
      const normal = normalOf(dir);
      const throughA = add(pos, normal, 1.7);
      const throughB = add(pos, normal, -1.7);
      const roomA = roomAt(next, throughA);
      const roomB = roomAt(next, throughB);
      door.pos = pos;
      door.hinge = ends.start;
      door.width = width;
      door.dir = dir;
      door.throughA = throughA;
      door.throughB = throughB;
      if (roomA && roomB && roomA !== roomB) {
        door.roomA = roomA;
        door.roomB = roomB;
      }
      const entity = next.entities.find((candidate) => candidate.doorId === door.id);
      if (entity) {
        entity.pos = pos;
        entity.standAt = throughA;
        entity.roomId = door.roomA;
      }
      presentDoors.add(door.id);
    }
    const removedDoors = next.doors.filter((door) => !presentDoors.has(door.id));
    if (removedDoors.length > 0) {
      const removedIds = new Set(removedDoors.map((door) => door.id));
      next.doors = next.doors.filter((door) => presentDoors.has(door.id));
      next.entities = next.entities.filter((entity) => !(entity.doorId && removedIds.has(entity.doorId)));
    }

    const presentEntities = new Set<string>();
    for (const shape of entityShapes) {
      const meta = metaFor(shape);
      if (!meta?.id) continue;
      const entity = next.entities.find((candidate) => candidate.id === meta.id);
      if (!entity) continue;
      const point = clampIntoRooms(next, shapeCenter(mounted, shape));
      const standOffset = { x: entity.standAt.x - entity.pos.x, z: entity.standAt.z - entity.pos.z };
      entity.pos = point;
      entity.standAt = clampIntoRooms(next, { x: point.x + standOffset.x, z: point.z + standOffset.z });
      entity.roomId = roomAt(next, point) ?? entity.roomId;
      presentEntities.add(entity.id);
    }
    next.entities = next.entities.filter((entity) => entity.kind === "door" || presentEntities.has(entity.id));

    if (playerShape) {
      const point = clampIntoRooms(next, shapeCenter(mounted, playerShape));
      next.start = { pos: point, roomId: roomAt(next, point) ?? next.rooms[0].id };
    }

    setCount(next.entities.length);
  };

  const scheduleSync = (): void => {
    window.clearTimeout(timer.current);
    timer.current = window.setTimeout(sync, 70);
  };

  useEffect(() => {
    if (!editor) return;
    editor.updateInstanceState({ isReadonly: false });
    seeding.current = true;
    seedShapes(editor, draft.current);
    seeding.current = false;
    const unsubscribe = editor.store.listen(
      () => {
        if (seeding.current) return;
        scheduleSync();
      },
      { source: "user", scope: "document" },
    );
    return () => {
      window.clearTimeout(timer.current);
      unsubscribe();
    };
  }, [editor]);

  const viewportPoint = (): Vec2 => {
    if (!editor) return { x: 6, z: 6 };
    const center = editor.screenToPage({ x: window.innerWidth / 2, y: window.innerHeight / 2 });
    return { x: snap(center.x / SCALE), z: snap(center.y / SCALE) };
  };

  const addRoom = (): void => {
    if (!editor) return;
    const center = viewportPoint();
    const room: Room = {
      id: `room-${Date.now().toString(36)}`,
      name: `Room ${draft.current.rooms.length + 1}`,
      box: { minX: center.x - 3.5, maxX: center.x + 3.5, minZ: center.z - 3, maxZ: center.z + 3 },
    };
    draft.current.rooms.push(room);
    createRoomShape(editor, room);
    editor.select(createShapeId(`room-${room.id}`));
    setError(null);
  };

  const addWall = (): void => {
    if (!editor) return;
    const center = viewportPoint();
    const wall: Wall = {
      id: `wall-${Date.now().toString(36)}`,
      a: { x: center.x - 1.5, z: center.z },
      b: { x: center.x + 1.5, z: center.z },
    };
    draft.current.walls.push(wall);
    createWallShape(editor, wall);
    editor.select(createShapeId(`wall-${wall.id}`));
  };

  const addDoor = (): void => {
    if (!editor) return;
    if (draft.current.rooms.length === 0) {
      setError("Add a room before adding a door.");
      return;
    }
    const center = viewportPoint();
    const dir: Vec2 = { x: 1, z: 0 };
    const normal = normalOf(dir);
    const throughA = add(center, normal, 1.7);
    const throughB = add(center, normal, -1.7);
    const roomA = roomAt(draft.current, throughA) ?? draft.current.rooms[0].id;
    const roomB = roomAt(draft.current, throughB) ?? draft.current.rooms[0].id;
    const door: DoorDef = {
      id: `door-${Date.now().toString(36)}`,
      name: "door",
      roomA,
      roomB,
      pos: center,
      hinge: add(center, dir, -1.1),
      width: 2.2,
      dir,
      locked: false,
      throughA,
      throughB,
    };
    const entity: Entity = {
      id: `door-entity-${Date.now().toString(36)}`,
      name: "door",
      kind: "door",
      roomId: roomA,
      pos: center,
      standAt: throughA,
      discovery: "proximity",
      discoveryRadius: 3.4,
      doorId: door.id,
    };
    draft.current.doors.push(door);
    draft.current.entities.push(entity);
    createDoorShape(editor, door);
    editor.select(createShapeId(`door-${door.id}`));
  };

  const addEntity = (kind: string): void => {
    if (!editor) return;
    if (draft.current.rooms.length === 0) {
      setError("Add a room before adding objects.");
      return;
    }
    const point = clampIntoRooms(draft.current, viewportPoint());
    const id = `${kind}-${Date.now().toString(36)}-${draft.current.entities.length}`;
    let entity: Entity;
    if (kind === "ink-ribbon" || kind === "green-herb" || kind === "brass-key") {
      const itemId = kind;
      const name = kind === "green-herb" ? "green herb" : kind === "brass-key" ? "brass key" : "ink-ribbon";
      entity = {
        id,
        name,
        kind: "item",
        roomId: roomAt(draft.current, point) ?? draft.current.rooms[0].id,
        pos: point,
        standAt: clampIntoRooms(draft.current, { x: point.x, z: point.z - 1.4 }),
        discovery: kind === "brass-key" ? "clue" : "room",
        discoveryRadius: kind === "brass-key" ? 2.4 : undefined,
        revealFlag: kind === "brass-key" ? "typedClue" : undefined,
        itemId,
      };
    } else {
      const station = kind === "typewriter" ? "typewriter" : kind === "cabinet" ? "cabinet" : "daybook";
      const name = station === "typewriter" ? "type-writer" : station === "cabinet" ? "study cabinet" : "daybook";
      entity = {
        id,
        name,
        kind: "station",
        roomId: roomAt(draft.current, point) ?? draft.current.rooms[0].id,
        pos: point,
        standAt: clampIntoRooms(draft.current, { x: point.x, z: point.z - 1.5 }),
        discovery: station === "cabinet" ? "flag" : "room",
        revealFlag: station === "cabinet" ? "readDaybook" : undefined,
        station,
        footprint: station === "typewriter" ? { w: 1.7, d: 1.05 } : station === "cabinet" ? { w: 1.1, d: 0.7 } : { w: 1.05, d: 0.75 },
      };
    }
    draft.current.entities.push(entity);
    createEntityShape(editor, entity);
    editor.select(createShapeId(`game-entity-${entity.id}`));
    setCount(draft.current.entities.length);
    setError(null);
  };

  const reset = (): void => {
    if (!editor) return;
    seeding.current = true;
    editor.run(() => {
      editor.deleteShapes(editor.getCurrentPageShapes().map((shape) => shape.id));
    });
    draft.current = cloneLevel(defaultLevel);
    seedShapes(editor, draft.current);
    seeding.current = false;
    setCount(draft.current.entities.length);
    setError(null);
  };

  const apply = (): void => {
    sync();
    window.clearTimeout(timer.current);
    const sanitized = sanitize(draft.current);
    if (!sanitized) {
      setError("Keep at least one room at least 2 by 2 metres.");
      return;
    }
    onApply(sanitized);
  };

  return (
    <div className="view-screen editor-view">
      <div className="tldraw-host">
        <Tldraw hideUi assetUrls={tldrawAssets} onMount={setEditor} />
      </div>
      <header className="view-topbar">
        <div>
          <p className="view-kicker">2D · tldraw → three.js</p>
          <h2 className="view-title">Level editor</h2>
        </div>
        <p className="view-hint">Drag rooms, walls, doors and objects. Wall and door handles reshape them.</p>
        <div className="editor-actions">
          <button className="ghost-button" onClick={reset}>
            Reset
          </button>
          <button className="primary-button" onClick={apply}>
            Apply &amp; play
          </button>
          <button className="ghost-button" onClick={onCancel}>
            Cancel <kbd>Esc</kbd>
          </button>
        </div>
      </header>

      <div className="editor-tools">
        <span className="editor-tool-label">Geometry</span>
        <button className="tool-button" onClick={addRoom}>
          Room
        </button>
        <button className="tool-button" onClick={addWall}>
          Wall
        </button>
        <button className="tool-button" onClick={addDoor}>
          Door
        </button>
        <span className="editor-tool-divider" />
        <span className="editor-tool-label">Objects</span>
        <button className="tool-button" onClick={() => addEntity("ink-ribbon")}>
          <i style={{ background: "#e085f4" }} /> Ink ribbon
        </button>
        <button className="tool-button" onClick={() => addEntity("green-herb")}>
          <i style={{ background: "#4cb05e" }} /> Green herb
        </button>
        <button className="tool-button" onClick={() => addEntity("brass-key")}>
          <i style={{ background: "#d9a441" }} /> Brass key
        </button>
        <button className="tool-button" onClick={() => addEntity("typewriter")}>
          <i style={{ background: "#9da0af" }} /> Type-writer
        </button>
        <button className="tool-button" onClick={() => addEntity("daybook")}>
          <i style={{ background: "#4ba1f1" }} /> Daybook
        </button>
        <button className="tool-button" onClick={() => addEntity("cabinet")}>
          <i style={{ background: "#9da0af" }} /> Cabinet
        </button>
        <span className="editor-count">{count} objects</span>
        {error ? <span className="editor-error">{error}</span> : null}
      </div>
    </div>
  );
}
