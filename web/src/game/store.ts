import { deriveDecisionGraph, roomAt, withDiscovery } from "./decisions";
import type {
  Decision,
  DecisionGraph,
  GameMessage,
  GameState,
  Interaction,
  Level,
  PendingAction,
} from "./types";

const initialState = (level: Level): GameState => {
  const base: GameState = {
    player: { pos: { ...level.start.pos }, roomId: level.start.roomId },
    inventory: [],
    collected: {},
    used: {},
    doors: Object.fromEntries(
      level.doors.map((door) => [door.id, { open: false, locked: door.locked }]),
    ),
    discovered: {},
    roomsSeen: { [level.start.roomId]: true },
    flags: { typedClue: false, readDaybook: false },
    pending: null,
    message: null,
  };
  return withDiscovery(level, base);
};

type Applied = { state: GameState; text?: string; tone?: GameMessage["tone"] };

const applyInteraction = (level: Level, state: GameState, interaction: Interaction): Applied => {
  switch (interaction.type) {
    case "none":
      return { state };

    case "move": {
      const entity = level.entities.find((candidate) => candidate.id === interaction.entityId);
      if (entity?.kind === "door" && entity.doorId) {
        const door = level.doors.find((candidate) => candidate.id === entity.doorId);
        const doorState = door ? state.doors[door.id] : undefined;
        if (door && doorState?.locked && !state.inventory.includes(door.keyId ?? "")) {
          return { state, text: "The study door is locked. A brass key would fit.", tone: "warn" };
        }
      }
      return { state };
    }

    case "take": {
      if (state.collected[interaction.entityId]) return { state };
      const item = level.items[interaction.itemId];
      return {
        state: {
          ...state,
          inventory: state.inventory.includes(interaction.itemId)
            ? state.inventory
            : [...state.inventory, interaction.itemId],
          collected: { ...state.collected, [interaction.entityId]: true },
        },
        text: `${item?.name ?? interaction.itemId} is in your inventory.`,
        tone: "good",
      };
    }

    case "use-item": {
      if (!state.inventory.includes(interaction.itemId)) return { state };
      const entity = level.entities.find((candidate) => candidate.itemId === interaction.itemId);
      return {
        state: {
          ...state,
          inventory: state.inventory.filter((itemId) => itemId !== interaction.itemId),
          used: entity ? { ...state.used, [entity.id]: true } : state.used,
        },
        text:
          interaction.itemId === "green-herb"
            ? "The green herb clears your head. The room settles."
            : "The item is spent.",
        tone: "info",
      };
    }

    case "use-station": {
      const entity = level.entities.find((candidate) => candidate.id === interaction.entityId);
      if (!entity || !state.inventory.includes(interaction.itemId)) return { state };
      return {
        state: {
          ...state,
          inventory: state.inventory.filter((itemId) => itemId !== interaction.itemId),
          used: { ...state.used, [entity.id]: true },
          flags: { ...state.flags, typedClue: true },
        },
        text: "One line in blue ink. The ribbon runs dry. A brass key glints by the study door.",
        tone: "good",
      };
    }

    case "read": {
      return {
        state: {
          ...state,
          used: { ...state.used, [interaction.entityId]: true },
          flags: { ...state.flags, readDaybook: true },
        },
        text: "The daybook ends: “Leave the ribbon. Take the key. The study keeps what the house forgets.”",
        tone: "info",
      };
    }

    case "unlock": {
      const door = level.doors.find((candidate) => candidate.id === interaction.doorId);
      if (!door || !state.inventory.includes(door.keyId ?? "")) {
        return { state, text: "That lock needs a key.", tone: "warn" };
      }
      return {
        state: {
          ...state,
          doors: { ...state.doors, [door.id]: { open: true, locked: false } },
        },
        text: "The brass key turns. The study is open.",
        tone: "good",
      };
    }

    case "open": {
      const doorState = state.doors[interaction.doorId];
      if (!doorState || doorState.locked) return { state };
      return {
        state: { ...state, doors: { ...state.doors, [interaction.doorId]: { ...doorState, open: true } } },
        text: "The study door swings open.",
        tone: "info",
      };
    }

    case "inventory":
      return { state };
  }
};

export class GameStore {
  private state: GameState;
  private listeners = new Set<() => void>();
  private messageSeq = 0;

  constructor(public level: Level) {
    this.state = initialState(level);
  }

  getSnapshot = (): GameState => this.state;

  subscribe = (listener: () => void): (() => void) => {
    this.listeners.add(listener);
    return () => this.listeners.delete(listener);
  };

  private emit(): void {
    for (const listener of this.listeners) listener();
  }

  graph(): DecisionGraph {
    return deriveDecisionGraph(this.level, this.state);
  }

  choose(decision: Decision): "inventory" | "started" | "ignored" {
    if (decision.interaction.type === "inventory") return "inventory";
    if (decision.interaction.type === "none" || this.state.pending) return "ignored";
    const pending: PendingAction = {
      nodeId: decision.id,
      label: decision.label,
      target: { ...decision.target },
      interaction: decision.interaction,
    };
    this.state = { ...this.state, pending };
    this.emit();
    return "started";
  }

  finishPending(): void {
    const pending = this.state.pending;
    if (!pending) return;

    const room = roomAt(this.level, pending.target) ?? this.state.player.roomId;
    let next: GameState = {
      ...this.state,
      player: { pos: { ...pending.target }, roomId: room },
      pending: null,
    };

    const applied = applyInteraction(this.level, next, pending.interaction);
    next = withDiscovery(this.level, applied.state);

    if (applied.text) {
      next = { ...next, message: { id: ++this.messageSeq, text: applied.text, tone: applied.tone ?? "info" } };
    }
    this.state = next;
    this.emit();
  }

  failPending(text: string): void {
    if (!this.state.pending) return;
    this.state = {
      ...this.state,
      pending: null,
      message: { id: ++this.messageSeq, text, tone: "warn" },
    };
    this.emit();
  }

  say(text: string, tone: GameMessage["tone"] = "info"): void {
    this.state = { ...this.state, message: { id: ++this.messageSeq, text, tone } };
    this.emit();
  }

  clearMessage(id?: number): void {
    if (!this.state.message) return;
    if (id !== undefined && this.state.message.id !== id) return;
    this.state = { ...this.state, message: null };
    this.emit();
  }

  loadLevel(level: Level): void {
    this.level = level;
    this.state = initialState(level);
    this.emit();
  }

  reset(): void {
    this.state = initialState(this.level);
    this.emit();
  }
}
