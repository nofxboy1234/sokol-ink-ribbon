import { level as defaultLevel } from "./level";
import type { Level } from "./types";

const STORAGE_KEY = "ink-ribbon-level-v1";

export const cloneLevel = (level: Level): Level => structuredClone(level);

const finite = (value: unknown): value is number => typeof value === "number" && Number.isFinite(value);

export const isLevel = (value: unknown): value is Level => {
  if (!value || typeof value !== "object") return false;
  const candidate = value as Partial<Level>;
  if (!Array.isArray(candidate.rooms) || candidate.rooms.length === 0) return false;
  if (!Array.isArray(candidate.walls) || !Array.isArray(candidate.entities)) return false;
  if (!Array.isArray(candidate.doors) || !candidate.items || !candidate.start) return false;
  if (!candidate.palette || typeof candidate.palette.paper !== "string") return false;
  if (!finite(candidate.start.pos?.x) || !finite(candidate.start.pos?.z)) return false;
  const items = candidate.items as Record<string, { id?: string }>;
  return candidate.entities.every(
    (entity) =>
      entity &&
      typeof entity.id === "string" &&
      typeof entity.name === "string" &&
      finite(entity.pos?.x) &&
      finite(entity.pos?.z) &&
      finite(entity.standAt?.x) &&
      finite(entity.standAt?.z) &&
      (entity.kind !== "item" || Boolean(entity.itemId && items[entity.itemId])),
  );
};

export function loadStoredLevel(): Level | null {
  try {
    const raw = window.localStorage.getItem(STORAGE_KEY);
    if (!raw) return null;
    const parsed: unknown = JSON.parse(raw);
    return isLevel(parsed) ? parsed : null;
  } catch {
    return null;
  }
}

export function saveLevel(level: Level): void {
  try {
    window.localStorage.setItem(STORAGE_KEY, JSON.stringify(level));
  } catch {
    return;
  }
}

export function clearStoredLevel(): void {
  try {
    window.localStorage.removeItem(STORAGE_KEY);
  } catch {
    return;
  }
}

export { defaultLevel };
