export type Vec2 = { x: number; z: number };
export type RoomId = string;
export type EntityId = string;
export type ItemId = string;
export type DoorId = string;

export type DiscoveryMode = "room" | "proximity" | "clue" | "flag";
export type EntityKind = "item" | "station" | "door";

export type ItemDef = {
  id: ItemId;
  name: string;
  description: string;
  color: number;
};

export type Entity = {
  id: EntityId;
  name: string;
  kind: EntityKind;
  roomId: RoomId;
  pos: Vec2;
  standAt: Vec2;
  discovery: DiscoveryMode;
  discoveryRadius?: number;
  itemId?: ItemId;
  station?: "typewriter" | "daybook" | "cabinet";
  doorId?: DoorId;
  revealFlag?: "typedClue" | "readDaybook";
  footprint?: { w: number; d: number };
};

export type DoorDef = {
  id: DoorId;
  name: string;
  roomA: RoomId;
  roomB: RoomId;
  pos: Vec2;
  hinge: Vec2;
  width: number;
  dir: Vec2;
  locked: boolean;
  initiallyOpen?: boolean;
  keyId?: ItemId;
  throughA: Vec2;
  throughB: Vec2;
};

export type Box = { minX: number; maxX: number; minZ: number; maxZ: number };
export type Room = { id: RoomId; name: string; box: Box };
export type Wall = { id: string; a: Vec2; b: Vec2 };

export type Level = {
  rooms: Room[];
  walls: Wall[];
  obstacles: Box[];
  entities: Entity[];
  doors: DoorDef[];
  items: Record<ItemId, ItemDef>;
  start: { pos: Vec2; roomId: RoomId };
  palette: {
    ink: string;
    paper: string;
    blue: string;
    pink: string;
    green: string;
    nodeBlue: string;
    nodeLightBlue: string;
    nodeGrey: string;
  };
};

export type Interaction =
  | { type: "none" }
  | { type: "move"; entityId?: EntityId }
  | { type: "take"; entityId: EntityId; itemId: ItemId }
  | { type: "use-item"; itemId: ItemId }
  | { type: "use-station"; entityId: EntityId; itemId: ItemId }
  | { type: "read"; entityId: EntityId }
  | { type: "unlock"; doorId: DoorId }
  | { type: "open"; doorId: DoorId }
  | { type: "open-cabinet"; entityId: EntityId }
  | { type: "inventory" };

export type PendingAction = {
  nodeId: string;
  label: string;
  targets: Vec2[];
  interaction: Interaction;
};

export type GameMessage = {
  id: number;
  text: string;
  tone: "info" | "good" | "warn";
};

export type GameState = {
  player: { pos: Vec2; roomId: RoomId };
  status: "playing" | "won";
  inventory: ItemId[];
  collected: Record<EntityId, true>;
  used: Record<EntityId, true>;
  doors: Record<DoorId, { open: boolean; locked: boolean }>;
  discovered: Record<EntityId, true>;
  roomsSeen: Record<RoomId, true>;
  flags: { typedClue: boolean; readDaybook: boolean };
  pending: PendingAction | null;
  message: GameMessage | null;
};

export type DecisionKind =
  | "player"
  | "move"
  | "take"
  | "use"
  | "read"
  | "open"
  | "unlock"
  | "inventory";

export type Decision = {
  id: string;
  label: string;
  kind: DecisionKind;
  anchor: Vec2;
  target: Vec2;
  interaction: Interaction;
  parentId: string;
};

export type DecisionEdge = { from: string; to: string };
export type DecisionGraph = { nodes: Decision[]; edges: DecisionEdge[]; rootId: string };
