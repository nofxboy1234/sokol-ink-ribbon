import { NavGrid } from "./nav";
import type {
  Decision,
  DecisionGraph,
  DoorId,
  Entity,
  GameState,
  ItemId,
  Level,
  RoomId,
  Vec2,
} from "./types";

export const distance = (a: Vec2, b: Vec2): number => Math.hypot(a.x - b.x, a.z - b.z);

export const roomAt = (level: Level, point: Vec2): RoomId | null => {
  for (const room of level.rooms) {
    if (
      point.x >= room.box.minX &&
      point.x <= room.box.maxX &&
      point.z >= room.box.minZ &&
      point.z <= room.box.maxZ
    ) {
      return room.id;
    }
  }
  return null;
};

export const reachableRooms = (level: Level, state: GameState): Set<RoomId> => {
  const reached = new Set<RoomId>([state.player.roomId]);
  let grew = true;
  while (grew) {
    grew = false;
    for (const door of level.doors) {
      if (!state.doors[door.id]?.open) continue;
      if (reached.has(door.roomA) && !reached.has(door.roomB)) {
        reached.add(door.roomB);
        grew = true;
      }
      if (reached.has(door.roomB) && !reached.has(door.roomA)) {
        reached.add(door.roomA);
        grew = true;
      }
    }
  }
  return reached;
};

const entityReachable = (level: Level, state: GameState, entity: Entity): boolean => {
  const rooms = reachableRooms(level, state);
  if (entity.kind === "door") {
    const door = level.doors.find((candidate) => candidate.id === entity.doorId);
    return Boolean(door && (rooms.has(door.roomA) || rooms.has(door.roomB)));
  }
  return rooms.has(entity.roomId);
};

export function withDiscovery(level: Level, state: GameState): GameState {
  const room = roomAt(level, state.player.pos);
  const roomsSeen = room ? { ...state.roomsSeen, [room]: true as const } : state.roomsSeen;
  const discovered = { ...state.discovered };

  for (const entity of level.entities) {
    if (discovered[entity.id]) continue;
    if (entity.discovery === "room" && roomsSeen[entity.roomId]) {
      discovered[entity.id] = true;
      continue;
    }
    if (entity.discovery === "flag") {
      if (entity.revealFlag && state.flags[entity.revealFlag]) discovered[entity.id] = true;
      continue;
    }
    if (entity.discovery === "clue" && entity.revealFlag && state.flags[entity.revealFlag]) {
      discovered[entity.id] = true;
      continue;
    }
    if (entity.discovery === "proximity" || entity.discovery === "clue") {
      const radius = entity.discoveryRadius ?? 2.4;
      if (distance(state.player.pos, entity.pos) <= radius) discovered[entity.id] = true;
    }
  }

  return { ...state, roomsSeen, discovered };
}

const roomCenter = (level: Level, roomId: RoomId): Vec2 => {
  const room = level.rooms.find((candidate) => candidate.id === roomId);
  if (!room) return { x: 0, z: 0 };
  return { x: (room.box.minX + room.box.maxX) / 2, z: (room.box.minZ + room.box.maxZ) / 2 };
};

const discoveredTravelRooms = (level: Level, state: GameState): Set<RoomId> => {
  const reached = new Set<RoomId>([state.player.roomId]);
  const doorEntity = new Map<DoorId, Entity>();
  for (const entity of level.entities) {
    if (entity.doorId && state.discovered[entity.id]) doorEntity.set(entity.doorId, entity);
  }
  let grew = true;
  while (grew) {
    grew = false;
    for (const door of level.doors) {
      if (!state.doors[door.id]?.open || !doorEntity.has(door.id)) continue;
      if (reached.has(door.roomA) && !reached.has(door.roomB)) {
        reached.add(door.roomB);
        grew = true;
      }
      if (reached.has(door.roomB) && !reached.has(door.roomA)) {
        reached.add(door.roomA);
        grew = true;
      }
    }
  }
  return reached;
};

export type RoomRoute = {
  id: string;
  label: string;
  doorIds: DoorId[];
  waypoints: Vec2[];
  length: number;
};

export function routeOptions(level: Level, state: GameState, from: Vec2, to: Vec2): RoomRoute[] {
  const fromRoom = roomAt(level, from);
  const toRoom = roomAt(level, to);
  if (!fromRoom || !toRoom || fromRoom === toRoom) return [];

  const knownDoors = new Set(
    level.entities
      .filter((entity) => entity.doorId && state.discovered[entity.id])
      .map((entity) => entity.doorId),
  );
  const openDoors = level.doors.filter(
    (door) =>
      state.doors[door.id]?.open &&
      knownDoors.has(door.id) &&
      level.rooms.some((room) => room.id === door.roomA) &&
      level.rooms.some((room) => room.id === door.roomB),
  );
  const byRoom = new Map<RoomId, typeof openDoors>();
  for (const door of openDoors) {
    for (const roomId of [door.roomA, door.roomB]) {
      const list = byRoom.get(roomId) ?? [];
      list.push(door);
      byRoom.set(roomId, list);
    }
  }

  const paths: typeof openDoors[] = [];
  const visit = (room: RoomId, usedDoors: Set<DoorId>, doors: typeof openDoors): void => {
    if (doors.length > 5) return;
    if (room === toRoom) {
      paths.push([...doors]);
      return;
    }
    for (const door of byRoom.get(room) ?? []) {
      if (usedDoors.has(door.id)) continue;
      const nextRoom = door.roomA === room ? door.roomB : door.roomA;
      usedDoors.add(door.id);
      doors.push(door);
      visit(nextRoom, usedDoors, doors);
      doors.pop();
      usedDoors.delete(door.id);
    }
  };
  visit(fromRoom, new Set(), []);

  const nav = new NavGrid(
    level,
    Object.fromEntries(level.doors.map((door) => [door.id, state.doors[door.id]?.open ?? false])),
  );
  const routes: RoomRoute[] = [];
  const seen = new Set<string>();

  for (const path of paths) {
    const id = path.map((door) => door.id).join("|");
    if (seen.has(id)) continue;
    let currentRoom = fromRoom;
    const waypoints: Vec2[] = [];
    for (const door of path) {
      if (door.roomA === currentRoom) {
        waypoints.push(door.throughB);
        currentRoom = door.roomB;
      } else {
        waypoints.push(door.throughA);
        currentRoom = door.roomA;
      }
    }
    if (currentRoom !== toRoom) continue;

    let cursor = from;
    let length = 0;
    let valid = true;
    for (const point of [...waypoints, to]) {
      const leg = nav.findPath(cursor, point);
      if (!leg) {
        valid = false;
        break;
      }
      for (let index = 1; index < leg.length; index++) {
        length += Math.hypot(leg[index].x - leg[index - 1].x, leg[index].z - leg[index - 1].z);
      }
      cursor = point;
    }
    if (!valid) continue;

    seen.add(id);
    const names = path.map((door) => door.name);
    routes.push({
      id,
      label: `via the ${names.join(" then the ")}`,
      doorIds: path.map((door) => door.id),
      waypoints: waypoints.map((point) => ({ ...point })),
      length,
    });
  }

  return routes.sort((a, b) => a.length - b.length);
}

const hasItem = (state: GameState, itemId: ItemId): boolean => state.inventory.includes(itemId);

export function deriveDecisionGraph(level: Level, state: GameState): DecisionGraph {
  const nodes: Decision[] = [
    {
      id: "player",
      label: "you",
      kind: "player",
      anchor: state.player.pos,
      target: state.player.pos,
      interaction: { type: "none" },
      parentId: "",
    },
  ];
  const edges: DecisionGraph["edges"] = [];

  const add = (node: Decision): void => {
    nodes.push(node);
    edges.push({ from: node.parentId, to: node.id });
  };

  const at = (entity: Entity): boolean => distance(state.player.pos, entity.standAt) < 0.4;

  for (const entity of level.entities) {
    if (entity.kind !== "item" || !entity.itemId) continue;
    if (!state.discovered[entity.id] || !entityReachable(level, state, entity)) continue;
    if (state.collected[entity.id]) continue;

    const moveId = `move:${entity.id}`;
    const parentId = at(entity) ? "player" : moveId;
    if (!at(entity)) {
      add({
        id: moveId,
        label: `move to ${entity.name}`,
        kind: "move",
        anchor: entity.pos,
        target: entity.standAt,
        interaction: { type: "move", entityId: entity.id },
        parentId: "player",
      });
    }
    add({
      id: `take:${entity.id}`,
      label: `take ${entity.name}`,
      kind: "take",
      anchor: entity.pos,
      target: entity.standAt,
      interaction: { type: "take", entityId: entity.id, itemId: entity.itemId },
      parentId,
    });
  }

  for (const entity of level.entities) {
    if (entity.kind !== "station") continue;
    if (!state.discovered[entity.id] || !entityReachable(level, state, entity)) continue;

    if (entity.station === "typewriter") {
      if (!hasItem(state, "ink-ribbon") || state.used[entity.id]) continue;
      const moveId = `move:${entity.id}`;
      const parentId = at(entity) ? "player" : moveId;
      if (!at(entity)) {
        add({
          id: moveId,
          label: `move to ${entity.name}`,
          kind: "move",
          anchor: entity.pos,
          target: entity.standAt,
          interaction: { type: "move", entityId: entity.id },
          parentId: "player",
        });
      }
      add({
        id: `use:${entity.id}`,
        label: `use ${entity.name}`,
        kind: "use",
        anchor: entity.pos,
        target: entity.standAt,
        interaction: { type: "use-station", entityId: entity.id, itemId: "ink-ribbon" },
        parentId,
      });
    }

    if (entity.station === "daybook") {
      if (state.flags.readDaybook) continue;
      const moveId = `move:${entity.id}`;
      const parentId = at(entity) ? "player" : moveId;
      if (!at(entity)) {
        add({
          id: moveId,
          label: `move to ${entity.name}`,
          kind: "move",
          anchor: entity.pos,
          target: entity.standAt,
          interaction: { type: "move", entityId: entity.id },
          parentId: "player",
        });
      }
      add({
        id: `read:${entity.id}`,
        label: `read ${entity.name}`,
        kind: "read",
        anchor: entity.pos,
        target: entity.standAt,
        interaction: { type: "read", entityId: entity.id },
        parentId,
      });
    }

    if (entity.station === "cabinet") {
      if (!state.flags.readDaybook || state.used[entity.id]) continue;
      const moveId = `move:${entity.id}`;
      const parentId = at(entity) ? "player" : moveId;
      if (!at(entity)) {
        add({
          id: moveId,
          label: `move to ${entity.name}`,
          kind: "move",
          anchor: entity.pos,
          target: entity.standAt,
          interaction: { type: "move", entityId: entity.id },
          parentId: "player",
        });
      }
      add({
        id: `open-cabinet:${entity.id}`,
        label: "open the drawer",
        kind: "use",
        anchor: entity.pos,
        target: entity.standAt,
        interaction: { type: "open-cabinet", entityId: entity.id },
        parentId,
      });
    }
  }

  for (const entity of level.entities) {
    if (entity.kind !== "door" || !entity.doorId) continue;
    if (!state.discovered[entity.id] || !entityReachable(level, state, entity)) continue;
    const door = level.doors.find((candidate) => candidate.id === entity.doorId);
    if (!door) continue;

    if (state.doors[door.id]?.open) continue;

    const moveId = `move:${entity.id}`;
    const parentId = at(entity) ? "player" : moveId;
    if (!at(entity)) {
      add({
        id: moveId,
        label: `move to ${entity.name}`,
        kind: "move",
        anchor: entity.pos,
        target: entity.standAt,
        interaction: { type: "move", entityId: entity.id },
        parentId: "player",
      });
    }

    if (state.doors[door.id]?.locked) {
      if (door.keyId && hasItem(state, door.keyId)) {
        add({
          id: `unlock:${entity.id}`,
          label: "unlock door",
          kind: "unlock",
          anchor: entity.pos,
          target: entity.standAt,
          interaction: { type: "unlock", doorId: door.id },
          parentId,
        });
      }
    } else {
      add({
        id: `open:${entity.id}`,
        label: "open door",
        kind: "open",
        anchor: entity.pos,
        target: entity.standAt,
        interaction: { type: "open", doorId: door.id },
        parentId,
      });
    }
  }

  const travelRooms = discoveredTravelRooms(level, state);
  for (const room of level.rooms) {
    if (room.id === state.player.roomId || !travelRooms.has(room.id)) continue;
    const center = roomCenter(level, room.id);
    const seen = Boolean(state.roomsSeen[room.id]);
    add({
      id: `${seen ? "room" : "enter"}:${room.id}`,
      label: `${seen ? "move to" : "enter"} ${room.name.toLowerCase()}`,
      kind: "move",
      anchor: center,
      target: center,
      interaction: { type: "move" },
      parentId: "player",
    });
  }

  for (const itemId of state.inventory) {
    if (itemId !== "green-herb") continue;
    const entity = level.entities.find((candidate) => candidate.itemId === itemId);
    if (!entity || state.used[entity.id]) continue;
    add({
      id: `use-item:${itemId}`,
      label: "use green herb",
      kind: "use",
      anchor: state.player.pos,
      target: state.player.pos,
      interaction: { type: "use-item", itemId },
      parentId: "player",
    });
  }

  add({
    id: "inventory",
    label: "open inventory",
    kind: "inventory",
    anchor: state.player.pos,
    target: state.player.pos,
    interaction: { type: "inventory" },
    parentId: "player",
  });

  return { nodes, edges, rootId: "player" };
}
