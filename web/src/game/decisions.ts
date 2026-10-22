import type {
  Decision,
  DecisionGraph,
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
    if (entity.discovery === "clue" && state.flags.typedClue) {
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
  }

  for (const entity of level.entities) {
    if (entity.kind !== "door" || !entity.doorId) continue;
    if (!state.discovered[entity.id] || !entityReachable(level, state, entity)) continue;
    const door = level.doors.find((candidate) => candidate.id === entity.doorId);
    if (!door) continue;

    if (state.doors[door.id]?.open) {
      const otherRoom = door.roomA === state.player.roomId ? door.roomB : door.roomA;
      if (state.roomsSeen[otherRoom]) continue;
      const room = level.rooms.find((candidate) => candidate.id === otherRoom);
      if (!room) continue;
      const target = otherRoom === door.roomB ? door.throughB : door.throughA;
      add({
        id: `enter:${door.id}`,
        label: `enter ${room.name.toLowerCase()}`,
        kind: "move",
        anchor: door.pos,
        target,
        interaction: { type: "move", entityId: entity.id },
        parentId: "player",
      });
      continue;
    }

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
