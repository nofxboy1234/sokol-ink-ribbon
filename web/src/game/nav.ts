import type { DoorId, Level, Vec2 } from "./types";

const CELL = 0.25;
const CLEARANCE = 0.46;
const EPS = 0.0001;

const distanceToSegment = (p: Vec2, a: Vec2, b: Vec2): number => {
  const dx = b.x - a.x;
  const dz = b.z - a.z;
  const lengthSq = dx * dx + dz * dz;
  const t = lengthSq < EPS ? 0 : Math.max(0, Math.min(1, ((p.x - a.x) * dx + (p.z - a.z) * dz) / lengthSq));
  return Math.hypot(p.x - (a.x + t * dx), p.z - (a.z + t * dz));
};

const inBox = (p: Vec2, minX: number, maxX: number, minZ: number, maxZ: number): boolean =>
  p.x >= minX && p.x <= maxX && p.z >= minZ && p.z <= maxZ;

export class NavGrid {
  readonly cols: number;
  readonly rows: number;
  readonly originX: number;
  readonly originZ: number;
  private readonly walkable: Uint8Array;

  constructor(level: Level, doorsOpen: Record<DoorId, boolean>) {
    let minX = Infinity;
    let maxX = -Infinity;
    let minZ = Infinity;
    let maxZ = -Infinity;
    for (const room of level.rooms) {
      minX = Math.min(minX, room.box.minX);
      maxX = Math.max(maxX, room.box.maxX);
      minZ = Math.min(minZ, room.box.minZ);
      maxZ = Math.max(maxZ, room.box.maxZ);
    }
    this.originX = minX - 1;
    this.originZ = minZ - 1;
    this.cols = Math.ceil((maxX - minX + 2) / CELL);
    this.rows = Math.ceil((maxZ - minZ + 2) / CELL);
    this.walkable = new Uint8Array(this.cols * this.rows);

    for (let cz = 0; cz < this.rows; cz++) {
      for (let cx = 0; cx < this.cols; cx++) {
        const point = this.cellPoint(cx, cz);
        this.walkable[cz * this.cols + cx] = this.computeWalkable(level, doorsOpen, point) ? 1 : 0;
      }
    }
  }

  private cellPoint(cx: number, cz: number): Vec2 {
    return { x: this.originX + (cx + 0.5) * CELL, z: this.originZ + (cz + 0.5) * CELL };
  }

  private computeWalkable(level: Level, doorsOpen: Record<DoorId, boolean>, point: Vec2): boolean {
    const inside = level.rooms.some((room) => inBox(point, room.box.minX, room.box.maxX, room.box.minZ, room.box.maxZ));
    if (!inside) return false;

    for (const wall of level.walls) {
      if (distanceToSegment(point, wall.a, wall.b) < CLEARANCE) return false;
    }

    for (const obstacle of level.obstacles) {
      if (
        inBox(
          point,
          obstacle.minX - CLEARANCE,
          obstacle.maxX + CLEARANCE,
          obstacle.minZ - CLEARANCE,
          obstacle.maxZ + CLEARANCE,
        )
      ) {
        return false;
      }
    }

    for (const door of level.doors) {
      if (doorsOpen[door.id]) continue;
      const half = door.width / 2;
      const a = { x: door.pos.x - half, z: door.pos.z };
      const b = { x: door.pos.x + half, z: door.pos.z };
      if (distanceToSegment(point, a, b) < CLEARANCE) return false;
    }

    return true;
  }

  isWalkable(point: Vec2): boolean {
    const cx = Math.floor((point.x - this.originX) / CELL);
    const cz = Math.floor((point.z - this.originZ) / CELL);
    if (cx < 0 || cz < 0 || cx >= this.cols || cz >= this.rows) return false;
    return this.walkable[cz * this.cols + cx] === 1;
  }

  nearestWalkable(point: Vec2, maxRadius = 24): Vec2 | null {
    if (this.isWalkable(point)) return point;
    const cx = Math.floor((point.x - this.originX) / CELL);
    const cz = Math.floor((point.z - this.originZ) / CELL);
    for (let radius = 1; radius <= maxRadius; radius++) {
      for (let dz = -radius; dz <= radius; dz++) {
        for (let dx = -radius; dx <= radius; dx++) {
          if (Math.max(Math.abs(dx), Math.abs(dz)) !== radius) continue;
          const nx = cx + dx;
          const nz = cz + dz;
          if (nx < 0 || nz < 0 || nx >= this.cols || nz >= this.rows) continue;
          if (this.walkable[nz * this.cols + nx]) return this.cellPoint(nx, nz);
        }
      }
    }
    return null;
  }

  hasLineOfSight(a: Vec2, b: Vec2): boolean {
    const length = Math.hypot(b.x - a.x, b.z - a.z);
    const steps = Math.max(2, Math.ceil(length / 0.1));
    for (let i = 0; i <= steps; i++) {
      const t = i / steps;
      const point = { x: a.x + (b.x - a.x) * t, z: a.z + (b.z - a.z) * t };
      if (!this.isWalkable(point)) return false;
    }
    return true;
  }

  findPath(from: Vec2, to: Vec2): Vec2[] | null {
    const start = this.nearestWalkable(from);
    const goal = this.nearestWalkable(to);
    if (!start || !goal) return null;

    const sx = Math.floor((start.x - this.originX) / CELL);
    const sz = Math.floor((start.z - this.originZ) / CELL);
    const gx = Math.floor((goal.x - this.originX) / CELL);
    const gz = Math.floor((goal.z - this.originZ) / CELL);
    const startIndex = sz * this.cols + sx;
    const goalIndex = gz * this.cols + gx;
    if (startIndex === goalIndex) return [goal];

    const total = this.cols * this.rows;
    const gScore = new Float32Array(total).fill(Infinity);
    const fScore = new Float32Array(total).fill(Infinity);
    const cameFrom = new Int32Array(total).fill(-1);
    const closed = new Uint8Array(total);
    const open: number[] = [startIndex];
    gScore[startIndex] = 0;
    fScore[startIndex] = octile(sx, sz, gx, gz);

    while (open.length > 0) {
      let best = 0;
      for (let i = 1; i < open.length; i++) {
        if (fScore[open[i]] < fScore[open[best]]) best = i;
      }
      const current = open.splice(best, 1)[0];
      if (current === goalIndex) {
        return this.reconstruct(cameFrom, current, start, goal);
      }
      closed[current] = 1;
      const cx = current % this.cols;
      const cz = Math.floor(current / this.cols);

      for (let dz = -1; dz <= 1; dz++) {
        for (let dx = -1; dx <= 1; dx++) {
          if (dx === 0 && dz === 0) continue;
          const nx = cx + dx;
          const nz = cz + dz;
          if (nx < 0 || nz < 0 || nx >= this.cols || nz >= this.rows) continue;
          const next = nz * this.cols + nx;
          if (!this.walkable[next] || closed[next]) continue;
          if (dx !== 0 && dz !== 0) {
            if (!this.walkable[cz * this.cols + nx] || !this.walkable[nz * this.cols + cx]) continue;
          }
          const step = dx !== 0 && dz !== 0 ? Math.SQRT2 : 1;
          const tentative = gScore[current] + step;
          if (tentative >= gScore[next]) continue;
          cameFrom[next] = current;
          gScore[next] = tentative;
          fScore[next] = tentative + octile(nx, nz, gx, gz);
          if (!open.includes(next)) open.push(next);
        }
      }
    }
    return null;
  }

  private reconstruct(cameFrom: Int32Array, current: number, start: Vec2, goal: Vec2): Vec2[] {
    const cells: number[] = [];
    let cursor = current;
    while (cursor !== -1) {
      cells.push(cursor);
      cursor = cameFrom[cursor];
    }
    cells.reverse();
    const points = cells.map((index) => this.cellPoint(index % this.cols, Math.floor(index / this.cols)));
    if (this.isWalkable(start)) points[0] = start;
    if (this.isWalkable(goal)) points[points.length - 1] = goal;

    const smoothed: Vec2[] = [];
    let anchor = 0;
    while (anchor < points.length - 1) {
      let furthest = anchor + 1;
      for (let i = points.length - 1; i > anchor; i--) {
        if (this.hasLineOfSight(points[anchor], points[i])) {
          furthest = i;
          break;
        }
      }
      smoothed.push(points[anchor]);
      anchor = furthest;
    }
    smoothed.push(points[points.length - 1]);
    return smoothed;
  }
}

const octile = (ax: number, az: number, bx: number, bz: number): number => {
  const dx = Math.abs(ax - bx);
  const dz = Math.abs(az - bz);
  return Math.max(dx, dz) + (Math.SQRT2 - 1) * Math.min(dx, dz);
};
