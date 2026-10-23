import { useEffect, useRef, useState } from "react";
import { createShapeId, Tldraw, toRichText, type Editor, type TLShapeId } from "tldraw";
import "tldraw/tldraw.css";
import { layoutGraph2D } from "../game/layout";
import type { DecisionGraph } from "../game/types";
import { tldrawAssets } from "../tldrawAssets";

export type DecisionGraph2DProps = {
  graph: DecisionGraph;
  selectedIds: string[];
  onSelect: (nodeId: string) => void;
};

type HitPoint = { x: number; y: number; radius: number };

export function DecisionGraph2D({ graph, selectedIds, onSelect }: DecisionGraph2DProps) {
  const [editor, setEditor] = useState<Editor | null>(null);
  const [hits, setHits] = useState<Record<string, HitPoint>>({});
  const shapeIds = useRef<Record<string, TLShapeId>>({});
  const labelIds = useRef<TLShapeId[]>([]);

  useEffect(() => {
    if (!editor) return;
    const { positions, nodeRadius } = layoutGraph2D(graph);

    const nextIds: Record<string, TLShapeId> = {};
    const nextLabels: TLShapeId[] = [];

    const previous = Object.values(shapeIds.current);
    try {
      editor.updateInstanceState({ isReadonly: false });
      if (previous.length > 0) editor.deleteShapes(previous);
      if (labelIds.current.length > 0) editor.deleteShapes(labelIds.current);
      editor.run(() => {
        for (const edge of graph.edges) {
          const from = positions[edge.from];
          const to = positions[edge.to];
          if (!from || !to) continue;
          editor.createShape({
            id: createShapeId(`edge-${edge.from}-${edge.to}`),
            type: "arrow",
            x: from.x,
            y: from.y,
            props: {
              start: { x: 0, y: 0 },
              end: { x: to.x - from.x, y: to.y - from.y },
              color: "grey",
              size: "s",
              arrowheadStart: "none",
              arrowheadEnd: "none",
            },
          });
        }

        for (const node of graph.nodes) {
          const position = positions[node.id];
          if (!position) continue;
          const labelId = createShapeId(`label-${node.id}`);
          nextLabels.push(labelId);
          editor.createShape({
            id: labelId,
            type: "text",
            x: position.x - 60.5,
            y: position.y - nodeRadius - 34,
            props: {
              richText: toRichText(node.label),
              color: node.kind === "player" ? "blue" : "black",
              size: "s",
              font: "draw",
              textAlign: "middle",
              w: 220,
              scale: 0.55,
              autoSize: false,
            },
          });

          const id = createShapeId(`node-${node.id}`);
          nextIds[node.id] = id;
          editor.createShape({
            id,
            type: "geo",
            x: position.x - nodeRadius,
            y: position.y - nodeRadius,
            props: {
              geo: "ellipse",
              w: nodeRadius * 2,
              h: nodeRadius * 2,
              color: node.kind === "player" ? "light-blue" : node.kind === "inventory" ? "grey" : "blue",
              fill: node.kind === "player" ? "fill" : "none",
              size: "s",
              richText: toRichText(""),
            },
          });
        }

        editor.bringToFront(Object.values(nextIds));
      });
    } finally {
      editor.updateInstanceState({ isReadonly: true });
    }

    shapeIds.current = nextIds;
    labelIds.current = nextLabels;

    const points = Object.values(positions);
    if (points.length > 0) {
      const minX = Math.min(...points.map((point) => point.x)) - 175;
      const maxX = Math.max(...points.map((point) => point.x)) + 175;
      const minY = Math.min(...points.map((point) => point.y)) - 120;
      const maxY = Math.max(...points.map((point) => point.y)) + 150;
      const frame = requestAnimationFrame(() => {
        editor.zoomToBounds({ x: minX, y: minY, w: maxX - minX, h: maxY - minY }, { inset: 30 });
      });
      return () => cancelAnimationFrame(frame);
    }
  }, [editor, graph]);

  useEffect(() => {
    if (!editor) return;
    const routeIds = new Set<string>(selectedIds);
    const selectedId = selectedIds[selectedIds.length - 1] ?? null;
    try {
      editor.updateInstanceState({ isReadonly: false });
      for (const node of graph.nodes) {
        const id = shapeIds.current[node.id];
        if (!id) continue;
        const selected = node.id === selectedId;
        const inRoute = routeIds.has(node.id);
        editor.updateShape({
          id,
          type: "geo",
          props: {
            fill: inRoute || node.kind === "player" ? "fill" : "none",
            size: selected ? "m" : "s",
            color: inRoute || node.kind === "player" ? "light-blue" : node.kind === "inventory" ? "grey" : "blue",
          },
        });
      }
    } finally {
      editor.updateInstanceState({ isReadonly: true });
    }
  }, [editor, graph, selectedIds]);

  useEffect(() => {
    if (!editor) return;
    let frame = 0;
    let last = 0;
    const tick = (time: number): void => {
      frame = requestAnimationFrame(tick);
      if (time - last < 30) return;
      last = time;
      const { positions, nodeRadius } = layoutGraph2D(graph);
      const zoom = editor.getZoomLevel();
      const next: Record<string, HitPoint> = {};
      for (const node of graph.nodes) {
        const position = positions[node.id];
        if (!position) continue;
        const screen = editor.pageToScreen(position);
        const radius = Math.max(28, nodeRadius * zoom);
        next[node.id] = { x: screen.x, y: screen.y, radius };
      }
      setHits(next);
    };
    frame = requestAnimationFrame(tick);
    return () => cancelAnimationFrame(frame);
  }, [editor, graph]);

  return (
    <div className="graph2d-host">
      <Tldraw hideUi assetUrls={tldrawAssets} onMount={setEditor} />
      <div className="graph2d-hits">
        {graph.nodes.map((node) => {
          const hit = hits[node.id];
          if (!hit) return null;
          return (
            <button
              key={node.id}
              type="button"
              className={`decision-hit ${node.id === selectedIds[selectedIds.length - 1] ? "selected" : ""} ${
                selectedIds.includes(node.id) ? "in-route" : ""
              } ${node.kind === "player" ? "player" : ""}`}
              aria-label={`Decision: ${node.label}`}
              aria-pressed={selectedIds.includes(node.id)}
              data-decision-id={node.id}
              style={{
                left: hit.x,
                top: hit.y,
                width: hit.radius * 2,
                height: hit.radius * 2,
              }}
              onClick={() => {
                if (node.kind !== "player") onSelect(node.id);
              }}
            />
          );
        })}
      </div>
    </div>
  );
}
