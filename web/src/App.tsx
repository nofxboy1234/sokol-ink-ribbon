import { useEffect, useMemo, useRef, useState, useSyncExternalStore } from "react";
import { GameStore } from "./game/store";
import { deriveDecisionGraph } from "./game/decisions";
import { cloneLevel, defaultLevel, loadStoredLevel, saveLevel } from "./game/levelStorage";
import type { Decision, DecisionGraph, GameState, Level, Vec2 } from "./game/types";
import { ThreeScene } from "./scene/ThreeScene";
import { MapView } from "./views/MapView";
import { DecisionGraph2D } from "./views/DecisionGraph2D";
import { DecisionGraph3D } from "./views/DecisionGraph3D";
import { LevelEditor } from "./views/LevelEditor";

type View = "none" | "graph2d" | "graph3d" | "map" | "inventory" | "editor";

declare global {
  interface Window {
    __ink?: {
      snapshot: () => GameState;
      graph: () => DecisionGraph;
      player: () => Vec2;
      open: (view: View) => void;
      select: (id: string | null) => void;
      confirm: () => void;
      close: () => void;
      reset: () => void;
      level: () => Level;
    };
    __ink3d?: { point: (nodeId: string) => { x: number; y: number } | null };
  }
}

const KEY_LABELS: { key: string; label: string; view: View }[] = [
  { key: "G", label: "Decisions", view: "graph2d" },
  { key: "T", label: "3D graph", view: "graph3d" },
  { key: "M", label: "Map", view: "map" },
  { key: "I", label: "Items", view: "inventory" },
  { key: "E", label: "Editor", view: "editor" },
];

const itemColor: Record<string, string> = {
  "ink-ribbon": "#e085f4",
  "green-herb": "#4cb05e",
  "brass-key": "#d9a441",
};

function ConfirmBar({
  node,
  route,
  onConfirm,
  onCancel,
}: {
  node: Decision | null;
  route: Decision[];
  onConfirm: () => void;
  onCancel: () => void;
}) {
  return (
    <div className="confirm-bar">
      <div className="confirm-copy">
        <span className="confirm-kicker">{node ? "decision" : "choose a node"}</span>
        <strong>{node ? node.label : "What can you do from here?"}</strong>
        {route.length > 1 ? <span className="route-line">{route.map((entry) => entry.label).join(" → ")}</span> : null}
      </div>
      <button className="primary-button" onClick={onConfirm} disabled={!node}>
        Confirm <kbd>Enter</kbd>
      </button>
      <button className="ghost-button" onClick={onCancel}>
        Back <kbd>Esc</kbd>
      </button>
    </div>
  );
}

function ViewHeader({
  kicker,
  title,
  onClose,
  closeKey,
  children,
}: {
  kicker: string;
  title: string;
  onClose: () => void;
  closeKey: string;
  children?: React.ReactNode;
}) {
  return (
    <header className="view-topbar">
      <div>
        <p className="view-kicker">{kicker}</p>
        <h2 className="view-title">{title}</h2>
      </div>
      {children}
      <button className="ghost-button" onClick={onClose}>
        Close <kbd>{closeKey}</kbd>
      </button>
    </header>
  );
}

function InventoryOverlay({ state, level, onClose }: { state: GameState; level: Level; onClose: () => void }) {
  return (
    <div className="view-screen inventory-view">
      <ViewHeader kicker="What you carry" title="Inventory" onClose={onClose} closeKey="I" />
      <div className="inventory-grid">
        {state.inventory.length === 0 ? (
          <div className="inventory-empty">
            <span className="empty-ring" />
            <p>Nothing yet.</p>
            <small>The room is holding out on you.</small>
          </div>
        ) : (
          state.inventory.map((itemId) => {
            const item = level.items[itemId];
            return (
              <article className="inventory-card" key={itemId}>
                <span className="item-orb" style={{ background: itemColor[itemId] ?? "#4ba1f1" }} />
                <div>
                  <h3>{item.name}</h3>
                  <p>{item.description}</p>
                </div>
              </article>
            );
          })
        )}
        <aside className="inventory-notes">
          <p className="view-kicker">Notes</p>
          <ul>
            <li className={state.flags.typedClue ? "done" : ""}>A type-writer waits in the Atelier.</li>
            <li className={state.flags.readDaybook ? "done" : ""}>The study keeps what the house forgets.</li>
          </ul>
        </aside>
      </div>
    </div>
  );
}

function Hud({
  state,
  level,
  pendingLabel,
  onOpen,
}: {
  state: GameState;
  level: Level;
  pendingLabel: string | null;
  onOpen: (view: View) => void;
}) {
  const room = level.rooms.find((candidate) => candidate.id === state.player.roomId);
  return (
    <>
      <div className="hud hud-top-left">
        <div className="brand">
          <span className="brand-orb" />
          <div>
            <h1>Ink Ribbon</h1>
            <p>{room?.name ?? "The Atelier"}</p>
          </div>
        </div>
      </div>

      <div className="hud hud-top-right">
        {KEY_LABELS.map((entry) => (
          <button className="hud-button" key={entry.key} onClick={() => onOpen(entry.view)}>
            {entry.label} <kbd>{entry.key}</kbd>
          </button>
        ))}
      </div>

      <div className="hud hud-bottom-left">
        <div className="hint-card">
          <p className="hint-title">No walking. Only deciding.</p>
          <p>
            Pick a node in a decision graph, then confirm. The capsule does the rest.
          </p>
        </div>
      </div>

      <div className="hud hud-bottom-right">
        <div className="carry-strip">
          <span className="carry-label">Carrying</span>
          {state.inventory.length === 0 ? (
            <span className="carry-empty">empty</span>
          ) : (
            state.inventory.map((itemId) => (
              <span className="carry-item" key={itemId}>
                <i style={{ background: itemColor[itemId] ?? "#4ba1f1" }} />
                {level.items[itemId]?.name ?? itemId}
              </span>
            ))
          )}
        </div>
      </div>

      {pendingLabel ? (
        <div className="walk-banner">
          <span className="walk-dot" />
          walking to {pendingLabel.replace(/^move to /, "")}
        </div>
      ) : null}
    </>
  );
}

export function App() {
  const [startingLevel] = useState<Level>(() => loadStoredLevel() ?? cloneLevel(defaultLevel));
  const storeRef = useRef<GameStore | null>(null);
  if (!storeRef.current) storeRef.current = new GameStore(startingLevel);
  const store = storeRef.current;
  const level = store.level;

  const state = useSyncExternalStore(store.subscribe, store.getSnapshot);
  const tracker = useRef<Vec2>({ ...state.player.pos });
  const [view, setView] = useState<View>("none");
  const [selectedId, setSelectedId] = useState<string | null>(null);

  const graph = useMemo(() => deriveDecisionGraph(level, state), [level, state]);
  const selected = graph.nodes.find((node) => node.id === selectedId && node.kind !== "player") ?? null;
  const route = useMemo(() => {
    const chain: Decision[] = [];
    let current = selected;
    while (current && current.kind !== "player") {
      chain.unshift(current);
      current = graph.nodes.find((node) => node.id === current?.parentId) ?? null;
    }
    return chain;
  }, [graph, selected]);

  const levelRef = useRef(level);
  useEffect(() => {
    if (levelRef.current === level) return;
    levelRef.current = level;
    tracker.current = { ...state.player.pos };
  }, [level, state.player.pos]);

  const close = (): void => {
    setView("none");
    setSelectedId(null);
  };

  const open = (next: View): void => {
    if (state.pending && next !== "map" && next !== "inventory") return;
    store.clearMessage();
    setSelectedId(null);
    setView(next);
  };

  const applyLevel = (next: Level): void => {
    store.loadLevel(next);
    saveLevel(next);
    setSelectedId(null);
    setView("none");
    store.say("Level saved. The room was rebuilt from tldraw.", "good");
  };

  const confirm = (): void => {
    if (!selected) return;
    const result = store.choose(selected);
    if (result === "inventory") {
      setSelectedId(null);
      setView("inventory");
      return;
    }
    if (result === "started") {
      setSelectedId(null);
      setView("none");
    }
  };

  const confirmRef = useRef(confirm);
  confirmRef.current = confirm;

  useEffect(() => {
    if (selectedId && !graph.nodes.some((node) => node.id === selectedId)) setSelectedId(null);
  }, [graph, selectedId]);

  useEffect(() => {
    const message = state.message;
    if (!message) return;
    const timer = window.setTimeout(() => store.clearMessage(message.id), 5600);
    return () => window.clearTimeout(timer);
  }, [state.message, store]);

  useEffect(() => {
    const onKey = (event: KeyboardEvent): void => {
      if (event.repeat || event.metaKey || event.ctrlKey || event.altKey) return;
      const target = event.target as HTMLElement | null;
      if (target && (target.tagName === "INPUT" || target.tagName === "TEXTAREA" || target.isContentEditable)) return;

      const key = event.key.toLowerCase();
      if (view === "editor" && key !== "e" && key !== "escape") return;
      if (key === "escape") {
        if (view !== "none") {
          event.preventDefault();
          event.stopPropagation();
          close();
        }
        return;
      }
      if (key === "enter" && (view === "graph2d" || view === "graph3d")) {
        event.preventDefault();
        event.stopPropagation();
        confirmRef.current();
        return;
      }
      const entry = KEY_LABELS.find((candidate) => candidate.key.toLowerCase() === key);
      if (!entry) return;
      event.preventDefault();
      event.stopPropagation();
      if (view === entry.view) close();
      else open(entry.view);
    };
    window.addEventListener("keydown", onKey, true);
    return () => window.removeEventListener("keydown", onKey, true);
  }, [view, state.pending]);

  useEffect(() => {
    store.say("You cannot walk. Decide, then confirm. G opens the graph, E edits the level.", "info");
  }, [store]);

  window.__ink = {
    snapshot: () => store.getSnapshot(),
    graph: () => store.graph(),
    player: () => tracker.current,
    open,
    select: setSelectedId,
    confirm: () => confirmRef.current(),
    close,
    reset: () => store.reset(),
    level: () => store.level,
  };

  const pendingLabel = state.pending?.label ?? null;

  return (
    <div className="app">
      <ThreeScene store={store} tracker={tracker} />
      <Hud state={state} level={level} pendingLabel={pendingLabel} onOpen={open} />

      {state.message ? (
        <div className={`toast toast-${state.message.tone}`} key={state.message.id}>
          {state.message.text}
        </div>
      ) : null}

      {view === "graph2d" ? (
        <div className="view-screen graph-view">
          <DecisionGraph2D graph={graph} selectedId={selectedId} onSelect={setSelectedId} />
          <ViewHeader
            kicker="2D · tldraw"
            title="Decisions"
            onClose={close}
            closeKey="G"
          >
            <p className="view-hint">Click a node, then confirm the route.</p>
          </ViewHeader>
          <ConfirmBar node={selected} route={route} onConfirm={confirm} onCancel={close} />
        </div>
      ) : null}

      {view === "graph3d" ? (
        <div className="view-screen graph-view graph-view-3d">
          <DecisionGraph3D graph={graph} selectedId={selectedId} onSelect={setSelectedId} />
          <ViewHeader
            kicker="3D · three.js"
            title="Decisions"
            onClose={close}
            closeKey="T"
          >
            <p className="view-hint">Drag to orbit. Click a sphere. Confirm the route.</p>
          </ViewHeader>
          <ConfirmBar node={selected} route={route} onConfirm={confirm} onCancel={close} />
        </div>
      ) : null}

      {view === "map" ? <MapView store={store} tracker={tracker} onClose={close} /> : null}

      {view === "inventory" ? <InventoryOverlay state={state} level={level} onClose={close} /> : null}

      {view === "editor" ? (
        <LevelEditor level={level} onApply={applyLevel} onCancel={close} />
      ) : null}
    </div>
  );
}
