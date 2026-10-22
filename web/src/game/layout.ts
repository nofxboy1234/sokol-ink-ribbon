import type { DecisionGraph, Vec2 } from "./types";

export type Point2 = { x: number; y: number };
export type Point3 = { x: number; y: number; z: number };

const WORLD_SCALE = 64;
const CHILD_GAP = 112;

const normalize = (x: number, y: number): Point2 => {
  const length = Math.hypot(x, y);
  return length < 0.0001 ? { x: 0, y: -1 } : { x: x / length, y: y / length };
};

export type Layout2D = {
  positions: Record<string, Point2>;
  nodeRadius: number;
  scale: number;
};

export function layoutGraph2D(graph: DecisionGraph): Layout2D {
  const positions: Record<string, Point2> = {};
  const player = graph.nodes.find((node) => node.id === graph.rootId);
  const playerWorld: Vec2 = player?.target ?? { x: 0, z: 0 };
  const nodeRadius = 18;

  for (const node of graph.nodes) {
    const parent = graph.nodes.find((candidate) => candidate.id === node.parentId);

    if (node.kind === "player") {
      positions[node.id] = { x: node.anchor.x * WORLD_SCALE, y: node.anchor.z * WORLD_SCALE };
      continue;
    }

    if (node.id === "inventory") {
      positions[node.id] = {
        x: playerWorld.x * WORLD_SCALE,
        y: playerWorld.z * WORLD_SCALE + 150,
      };
      continue;
    }

    if (node.kind === "use" && node.anchor.x === playerWorld.x && node.anchor.z === playerWorld.z) {
      positions[node.id] = {
        x: playerWorld.x * WORLD_SCALE,
        y: playerWorld.z * WORLD_SCALE - 150,
      };
      continue;
    }

    if (node.kind === "take" || node.kind === "use" || node.kind === "read" || node.kind === "open" || node.kind === "unlock") {
      const parentPosition = parent ? positions[parent.id] : undefined;
      if (parentPosition) {
        const direction = normalize(parentPosition.x - playerWorld.x * WORLD_SCALE, parentPosition.y - playerWorld.z * WORLD_SCALE);
        positions[node.id] = {
          x: parentPosition.x + direction.x * CHILD_GAP,
          y: parentPosition.y + direction.y * CHILD_GAP,
        };
        continue;
      }
    }

    positions[node.id] = { x: node.anchor.x * WORLD_SCALE, y: node.anchor.z * WORLD_SCALE };
  }

  return { positions, nodeRadius, scale: WORLD_SCALE };
}

const hash = (value: string): number => {
  let result = 2166136261;
  for (let i = 0; i < value.length; i++) {
    result ^= value.charCodeAt(i);
    result = Math.imul(result, 16777619);
  }
  return result >>> 0;
};

export type Layout3D = {
  positions: Record<string, Point3>;
  radius: number;
};

export function layoutGraph3D(graph: DecisionGraph): Layout3D {
  const positions: Record<string, Point3> = {};
  const root = graph.nodes.find((node) => node.id === graph.rootId);
  const roots = graph.nodes.filter((node) => node.parentId === graph.rootId);
  const rootCount = Math.max(1, roots.length);
  const golden = Math.PI * (3 - Math.sqrt(5));

  for (const node of graph.nodes) {
    if (node.id === graph.rootId) {
      positions[node.id] = { x: 0, y: 0, z: 0 };
      continue;
    }

    if (node.parentId === graph.rootId) {
      const index = roots.findIndex((candidate) => candidate.id === node.id);
      const jitter = (hash(node.id) % 1000) / 1000;
      const yUnit = 1 - (2 * (index + 0.5)) / rootCount;
      const radiusAtY = Math.sqrt(Math.max(0, 1 - yUnit * yUnit));
      const theta = golden * (index + 0.35) + jitter * 0.4;
      const radius = 2.25 + jitter * 0.35;
      positions[node.id] = {
        x: Math.cos(theta) * radiusAtY * radius,
        y: yUnit * radius * 0.85,
        z: Math.sin(theta) * radiusAtY * radius,
      };
      continue;
    }

    const parentPosition = positions[node.parentId];
    if (!parentPosition) {
      positions[node.id] = { x: 0, y: 0, z: 0 };
      continue;
    }
    const direction = normalize3(parentPosition.x, parentPosition.y + 0.2, parentPosition.z);
    const spread = (hash(node.id) % 100) / 100 - 0.5;
    positions[node.id] = {
      x: parentPosition.x + direction.x * 1.25 + spread * 0.9,
      y: parentPosition.y + direction.y * 1.25 + 0.45 + spread * 0.5,
      z: parentPosition.z + direction.z * 1.25 - spread * 0.8,
    };
  }

  const ids = Object.keys(positions).filter((id) => id !== graph.rootId);
  for (let iteration = 0; iteration < 110; iteration++) {
    for (let i = 0; i < ids.length; i++) {
      for (let j = i + 1; j < ids.length; j++) {
        const a = positions[ids[i]];
        const b = positions[ids[j]];
        let dx = a.x - b.x;
        let dy = a.y - b.y;
        let dz = a.z - b.z;
        let length = Math.hypot(dx, dy, dz);
        if (length < 0.0001) {
          dx = 0.01;
          dy = 0.01;
          dz = 0.01;
          length = 0.02;
        }
        const minimum = 1.15;
        if (length < minimum) {
          const push = (minimum - length) * 0.5;
          a.x += (dx / length) * push;
          a.y += (dy / length) * push;
          a.z += (dz / length) * push;
          b.x -= (dx / length) * push;
          b.y -= (dy / length) * push;
          b.z -= (dz / length) * push;
        }
      }
    }
  }

  const radius = Math.max(2.2, ...ids.map((id) => Math.hypot(positions[id].x, positions[id].y, positions[id].z) + 0.6));
  return { positions, radius };
}

const normalize3 = (x: number, y: number, z: number): Point3 => {
  const length = Math.hypot(x, y, z);
  return length < 0.0001 ? { x: 0, y: 1, z: 0 } : { x: x / length, y: y / length, z: z / length };
};
