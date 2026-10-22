import { useEffect, useRef, useState } from "react";
import { createShapeId, Tldraw, toRichText, type Editor, type TLShape, type TLShapeId } from "tldraw";
import "tldraw/tldraw.css";
import { cloneLevel, defaultLevel } from "../game/levelStorage";
import type { Entity, Level, Vec2 } from "../game/types";
import { roomAt } from "../game/decisions";
import { tldrawAssets } from "../tldrawAssets";

type Props = {
  level: Level;
  onApply: (level: Level) => void;
  onCancel: () => void;
};

type GameMeta = {
  game?: { role: "player" | "entity"; id?: string };
};

const SCALE = 44;

const pagePoint = (point: Vec2): { x: number; y: number } => ({ x: point.x * SCALE, y: point.z * SCALE });

const clamp = (value: number, min: number, max: number): number => Math.max(min, Math.min(max, value));

const clampIntoRooms = (level: Level, point: Vec2): Vec2 => {
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
  if (entity.station === "typewriter") return "grey";
  return "blue";
};

const shortLabel = (name: string): string =>
  name
    .replace("ink-ribbon", "ribbon")
    .replace("green herb", "herb")
    .replace("brass key", "key")
    .replace("type-writer", "type")
    .replace("study door", "door")
    .replace("daybook", "book");

const createEntityShape = (
  editor: Editor,
  entity: Entity,
  shapeId: TLShapeId,
): void => {
  const point = pagePoint(entity.pos);
  editor.createShape({
    id: shapeId,
    type: "geo",
    x: point.x - 24,
    y: point.y - 24,
    meta: { game: { role: "entity", id: entity.id } } satisfies GameMeta,
    isLocked: entity.kind === "door",
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

const seedShapes = (editor: Editor, level: Level): void => {
  editor.run(() => {
    for (const room of level.rooms) {
      editor.createShape({
        id: createShapeId(`room-${room.id}`),
        type: "geo",
        x: room.box.minX * SCALE,
        y: room.box.minZ * SCALE,
        isLocked: true,
        props: {
          geo: "rectangle",
          w: (room.box.maxX - room.box.minX) * SCALE,
          h: (room.box.maxZ - room.box.minZ) * SCALE,
          color: "grey",
          fill: "none",
          size: "s",
          richText: toRichText(""),
        },
      });
      const labelPoint = pagePoint({ x: room.box.minX + 0.4, z: room.box.minZ + 0.4 });
      editor.createShape({
        id: createShapeId(`room-label-${room.id}`),
        type: "text",
        x: labelPoint.x,
        y: labelPoint.y,
        isLocked: true,
        props: {
          richText: toRichText(room.name.toUpperCase()),
          color: "grey",
          size: "s",
          font: "draw",
          textAlign: "start",
          w: 220,
          scale: 0.5,
          autoSize: false,
        },
      });
    }

    level.walls.forEach((wall, index) => {
      const a = pagePoint(wall.a);
      const b = pagePoint(wall.b);
      editor.createShape({
        id: createShapeId(`wall-${index}`),
        type: "arrow",
        x: a.x,
        y: a.y,
        isLocked: true,
        props: {
          start: { x: 0, y: 0 },
          end: { x: b.x - a.x, y: b.y - a.y },
          color: "grey",
          size: "s",
          arrowheadStart: "none",
          arrowheadEnd: "none",
        },
      });
    });

    for (const door of level.doors) {
      const half = door.width / 2;
      const a = pagePoint({ x: door.pos.x - half, z: door.pos.z });
      const b = pagePoint({ x: door.pos.x + half, z: door.pos.z });
      editor.createShape({
        id: createShapeId(`door-${door.id}`),
        type: "arrow",
        x: a.x,
        y: a.y,
        isLocked: true,
        props: {
          start: { x: 0, y: 0 },
          end: { x: b.x - a.x, y: b.y - a.y },
          color: "black",
          size: "m",
          arrowheadStart: "none",
          arrowheadEnd: "none",
        },
      });
    }

    for (const entity of level.entities) {
      createEntityShape(editor, entity, createShapeId(`game-entity-${entity.id}`));
    }

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
  });

  const min = { x: Infinity, y: Infinity };
  const max = { x: -Infinity, y: -Infinity };
  for (const room of level.rooms) {
    min.x = Math.min(min.x, room.box.minX * SCALE);
    min.y = Math.min(min.y, room.box.minZ * SCALE);
    max.x = Math.max(max.x, room.box.maxX * SCALE);
    max.y = Math.max(max.y, room.box.maxZ * SCALE);
  }
  editor.zoomToBounds({ x: min.x - 60, y: min.y - 60, w: max.x - min.x + 120, h: max.y - min.y + 120 }, { inset: 40 });
};

const shapeCenter = (editor: Editor, shape: TLShape): Vec2 => {
  const bounds = editor.getShapePageBounds(shape);
  if (bounds) return { x: (bounds.x + bounds.w / 2) / SCALE, z: (bounds.y + bounds.h / 2) / SCALE };
  return { x: shape.x / SCALE, z: shape.y / SCALE };
};

export function LevelEditor({ level, onApply, onCancel }: Props) {
  const [editor, setEditor] = useState<Editor | null>(null);
  const draft = useRef<Level>(cloneLevel(level));
  const seeding = useRef(true);
  const timer = useRef(0);
  const [count, setCount] = useState(level.entities.length);

  const sync = (): void => {
    const mounted = editor;
    if (!mounted) return;
    const next = draft.current;
    const shapes = mounted.getCurrentPageShapes();
    const entityIds = new Set<string>();
    const playerShape = shapes.find((shape) => (shape.meta as GameMeta).game?.role === "player");

    if (playerShape) {
      const point = clampIntoRooms(next, shapeCenter(mounted, playerShape));
      next.start = { pos: point, roomId: roomAt(next, point) ?? next.start.roomId };
    }

    for (const shape of shapes) {
      const meta = (shape.meta as GameMeta).game;
      if (!meta || meta.role !== "entity" || !meta.id) continue;
      const entity = next.entities.find((candidate) => candidate.id === meta.id);
      if (!entity) continue;
      entityIds.add(entity.id);
      const point = clampIntoRooms(next, shapeCenter(mounted, shape));
      const standOffset = { x: entity.standAt.x - entity.pos.x, z: entity.standAt.z - entity.pos.z };
      entity.pos = point;
      entity.standAt = clampIntoRooms(next, { x: point.x + standOffset.x, z: point.z + standOffset.z });
      entity.roomId = roomAt(next, point) ?? entity.roomId;
      if (entity.doorId) {
        const door = next.doors.find((candidate) => candidate.id === entity.doorId);
        if (door) {
          const hingeOffset = { x: door.hinge.x - door.pos.x, z: door.hinge.z - door.pos.z };
          const throughAOffset = { x: door.throughA.x - door.pos.x, z: door.throughA.z - door.pos.z };
          const throughBOffset = { x: door.throughB.x - door.pos.x, z: door.throughB.z - door.pos.z };
          door.pos = point;
          door.hinge = { x: point.x + hingeOffset.x, z: point.z + hingeOffset.z };
          door.throughA = { x: point.x + throughAOffset.x, z: point.z + throughAOffset.z };
          door.throughB = { x: point.x + throughBOffset.x, z: point.z + throughBOffset.z };
        }
      }
    }

    next.entities = next.entities.filter((entity) => entityIds.has(entity.id));
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

  const addEntity = (kind: string): void => {
    if (!editor) return;
    const center = editor.screenToPage({ x: window.innerWidth / 2, y: window.innerHeight / 2 });
    const point = clampIntoRooms(draft.current, { x: center.x / SCALE, z: center.y / SCALE });
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
        itemId,
      };
    } else {
      const station = kind === "typewriter" ? "typewriter" : "daybook";
      entity = {
        id,
        name: station === "typewriter" ? "type-writer" : "daybook",
        kind: "station",
        roomId: roomAt(draft.current, point) ?? draft.current.rooms[0].id,
        pos: point,
        standAt: clampIntoRooms(draft.current, { x: point.x, z: point.z - 1.5 }),
        discovery: "room",
        station,
      };
    }
    draft.current.entities.push(entity);
    const shapeId = createShapeId(`game-entity-${entity.id}`);
    createEntityShape(editor, entity, shapeId);
    editor.select(shapeId);
    setCount(draft.current.entities.length);
  };

  const reset = (): void => {
    if (!editor) return;
    seeding.current = true;
    editor.deleteShapes(editor.getCurrentPageShapes().map((shape) => shape.id));
    draft.current = cloneLevel(defaultLevel);
    seedShapes(editor, draft.current);
    seeding.current = false;
    setCount(draft.current.entities.length);
  };

  const apply = (): void => {
    sync();
    window.clearTimeout(timer.current);
    onApply(draft.current);
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
        <p className="view-hint">Drag objects to place them. Room geometry stays fixed in this version.</p>
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
        <span className="editor-tool-label">Add object</span>
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
        <span className="editor-count">{count} objects</span>
      </div>
    </div>
  );
}
