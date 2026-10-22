import { useEffect, useRef, useState } from "react";
import { createShapeId, Tldraw, toRichText, type Editor } from "tldraw";
import "tldraw/tldraw.css";
import { tldrawAssets } from "../tldrawAssets";
import type { GameStore } from "../game/store";
import type { ItemId, Vec2 } from "../game/types";

type Props = {
  store: GameStore;
  tracker: { current: Vec2 };
  onClose: () => void;
};

const SCALE = 42;
const OFFSET_X = 120;
const OFFSET_Z = 140;

const mapX = (x: number): number => OFFSET_X + x * SCALE;
const mapZ = (z: number): number => OFFSET_Z + z * SCALE;

const dotColor = (itemId: ItemId | undefined): "light-blue" | "light-violet" | "light-green" | "grey" => {
  if (itemId === "ink-ribbon") return "light-violet";
  if (itemId === "green-herb") return "light-green";
  return "grey";
};

export function MapView({ store, tracker, onClose }: Props) {
  const [editor, setEditor] = useState<Editor | null>(null);
  const playerDotId = useRef(createShapeId("map-player"));

  useEffect(() => {
    if (!editor) return;
    editor.updateInstanceState({ isReadonly: false });

    const state = store.getSnapshot();
    const walls = store.level.walls;
    const entities = store.level.entities;

    editor.run(() => {
      walls.forEach((wall, index) => {
        editor.createShape({
          id: createShapeId(`map-wall-${index}`),
          type: "arrow",
          x: mapX(wall.a.x),
          y: mapZ(wall.a.z),
          props: {
            start: { x: 0, y: 0 },
            end: { x: (wall.b.x - wall.a.x) * SCALE, y: (wall.b.z - wall.a.z) * SCALE },
            color: "grey",
            size: "s",
            arrowheadStart: "none",
            arrowheadEnd: "none",
          },
        });
      });

      for (const door of store.level.doors) {
        if (state.doors[door.id]?.open) continue;
        const half = door.width / 2;
        editor.createShape({
          id: createShapeId(`map-door-${door.id}`),
          type: "arrow",
          x: mapX(door.pos.x - half),
          y: mapZ(door.pos.z),
          props: {
            start: { x: 0, y: 0 },
            end: { x: door.width * SCALE, y: 0 },
            color: "black",
            size: "s",
            arrowheadStart: "none",
            arrowheadEnd: "none",
          },
        });
      }

      for (const entity of entities) {
        if (entity.kind !== "item") continue;
        if (state.collected[entity.id]) continue;
        if (entity.itemId === "brass-key" && !state.discovered[entity.id]) continue;
        if (entity.station === "daybook" && !state.discovered[entity.id]) continue;
        const size = entity.kind === "item" ? 11 : 9;
        editor.createShape({
          id: createShapeId(`map-entity-${entity.id}`),
          type: "geo",
          x: mapX(entity.pos.x) - size / 2,
          y: mapZ(entity.pos.z) - size / 2,
          props: {
            geo: "ellipse",
            w: size,
            h: size,
            color: dotColor(entity.itemId),
            fill: "fill",
            richText: toRichText(""),
          },
        });
      }

      editor.createShape({
        id: playerDotId.current,
        type: "geo",
        x: mapX(state.player.pos.x) - 8,
        y: mapZ(state.player.pos.z) - 8,
        props: {
          geo: "ellipse",
          w: 16,
          h: 16,
          color: "light-blue",
          fill: "fill",
          richText: toRichText(""),
        },
      });

      editor.bringToFront([playerDotId.current]);
      editor.zoomToBounds(
        { x: OFFSET_X - 40, y: OFFSET_Z - 7 * SCALE - 40, w: 18 * SCALE + 80, h: 18 * SCALE + 80 },
        { inset: 40 },
      );
    });
    editor.updateInstanceState({ isReadonly: true });

    const interval = window.setInterval(() => {
      const point = tracker.current;
      if (!point) return;
      editor.updateInstanceState({ isReadonly: false });
      editor.updateShape({
        id: playerDotId.current,
        type: "geo",
        x: mapX(point.x) - 8,
        y: mapZ(point.z) - 8,
      });
      editor.updateInstanceState({ isReadonly: true });
    }, 40);

    return () => window.clearInterval(interval);
  }, [editor, store, tracker]);

  useEffect(() => {
    const onKey = (event: KeyboardEvent): void => {
      if (event.key === "Escape" || event.key.toLowerCase() === "m") {
        event.preventDefault();
        onClose();
      }
    };
    window.addEventListener("keydown", onKey);
    return () => window.removeEventListener("keydown", onKey);
  }, [onClose]);

  return (
    <div className="view-screen map-view">
      <div className="view-topbar">
        <div>
          <p className="view-kicker">The house</p>
          <h2 className="view-title">Map</h2>
        </div>
        <div className="view-legend">
          <span>
            <i style={{ background: "#4ba1f1" }} /> you
          </span>
          <span>
            <i style={{ background: "#e085f4" }} /> ink-ribbon
          </span>
          <span>
            <i style={{ background: "#4cb05e" }} /> green herb
          </span>
        </div>
        <button className="ghost-button" onClick={onClose}>
          Close <kbd>M</kbd>
        </button>
      </div>
      <div className="tldraw-host">
        <Tldraw hideUi assetUrls={tldrawAssets} onMount={setEditor} />
      </div>
    </div>
  );
}
