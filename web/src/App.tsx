import { useEffect, useMemo, useRef, useState, useSyncExternalStore } from "react";
import { GameStore } from "./game/store";
import { deriveDecisionGraph, routeOptions, type RoomRoute } from "./game/decisions";
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

function RouteBar({
  route,
  options,
  chosenRouteId,
  onChooseRoute,
  onRemove,
  onClear,
  onConfirm,
  onCancel,
}: {
  route: Decision[];
  options: RoomRoute[];
  chosenRouteId: string | null;
  onChooseRoute: (id: string) => void;
  onRemove: (index: number) => void;
  onClear: () => void;
  onConfirm: () => void;
  onCancel: () => void;
}) {
  const last = route[route.length - 1] ?? null;
  return (
    <div className="confirm-bar route-bar">
      <div className="confirm-copy">
        <span className="confirm-kicker">{route.length > 0 ? `route · ${route.length} step${route.length === 1 ? "" : "s"}` : "choose a node"}</span>
        <strong>{last ? last.label : "What can you do from here?"}</strong>
        {route.length > 0 ? (
          <div className="route-chips">
            {route.map((entry, index) => (
              <button key={`${entry.id}-${index}`} className="route-chip" onClick={() => onRemove(index)}>
                <span>{index + 1}</span>
                {entry.label}
              </button>
            ))}
          </div>
        ) : (
          <span className="route-hint">Click move nodes to add waypoints, then choose an action.</span>
        )}
        {options.length > 1 ? (
          <div className="route-options">
            <span className="route-options-label">Routes to target</span>
            {options.map((option) => (
              <button
                key={option.id}
                className={`route-option ${option.id === chosenRouteId ? "selected" : ""}`}
                onClick={() => onChooseRoute(option.id)}
              >
                {option.label}
              </button>
            ))}
          </div>
        ) : null}
      </div>
      {route.length > 0 ? (
        <button className="ghost-button" onClick={onClear}>
          Clear
        </button>
      ) : null}
      <button className="primary-button" onClick={onConfirm} disabled={route.length === 0}>
        Confirm <kbd>Enter</kbd>
      </button>
      <button className="ghost-button" onClick={onCancel}>
        Back <kbd>Esc</kbd>
      </button>
    </div>
  );
}

function GoalOverlay({
  state,
  onContinue,
  onRestart,
}: {
  state: GameState;
  onContinue: () => void;
  onRestart: () => void;
}) {
  const taken = Object.keys(state.collected).length;
  return (
    <div className="goal-overlay">
      <div className="goal-card">
        <p className="view-kicker">The study</p>
        <h2>The Drawer Opens</h2>
        <p className="goal-lead">
          You read the daybook, turned the dial to 1-9-2-6, and opened what the house forgot.
        </p>
        <div className="goal-stats">
          <span><strong>{taken}</strong> objects taken</span>
          <span><strong>{state.flags.typedClue ? "yes" : "no"}</strong> ribbon spent</span>
          <span><strong>{state.flags.readDaybook ? "yes" : "no"}</strong> clue read</span>
        </div>
        <div className="goal-actions">
          <button className="primary-button" onClick={onContinue}>
            Keep exploring
          </button>
          <button className="ghost-button" onClick={onRestart}>
            Start again
          </button>
        </div>
      </div>
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
            {state.flags.readDaybook ? <li className="code-note">The drawer answers to 1-9-2-6.</li> : null}
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
          walking to {(pendingLabel.split(" → ").pop() ?? pendingLabel).replace(/^move to /, "")}
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
  const [routeIds, setRouteIds] = useState<string[]>([]);
  const [chosenRouteId, setChosenRouteId] = useState<string | null>(null);
  const [goalDismissed, setGoalDismissed] = useState(false);

  const graph = useMemo(() => deriveDecisionGraph(level, state), [level, state]);
  const route = useMemo(
    () => routeIds.map((id) => graph.nodes.find((node) => node.id === id)).filter((node): node is Decision => Boolean(node)),
    [graph, routeIds],
  );
  const finalTarget = route.length > 0 ? route[route.length - 1].target : null;
  const options = useMemo(
    () => (finalTarget ? routeOptions(level, state, state.player.pos, finalTarget) : []),
    [level, state, finalTarget],
  );

  useEffect(() => {
    if (options.length === 0) {
      setChosenRouteId(null);
      return;
    }
    setChosenRouteId((current) => (current && options.some((option) => option.id === current) ? current : options[0].id));
  }, [options]);

  const levelRef = useRef(level);
  useEffect(() => {
    if (levelRef.current === level) return;
    levelRef.current = level;
    tracker.current = { ...state.player.pos };
  }, [level, state.player.pos]);

  const close = (): void => {
    setView("none");
    setRouteIds([]);
    setChosenRouteId(null);
  };

  const open = (next: View): void => {
    if (state.pending && next !== "map" && next !== "inventory") return;
    store.clearMessage();
    setRouteIds([]);
    setChosenRouteId(null);
    setView(next);
  };

  const applyLevel = (next: Level): void => {
    store.loadLevel(next);
    saveLevel(next);
    setRouteIds([]);
    setView("none");
    store.say("Level saved. The room was rebuilt from tldraw.", "good");
  };

  const ancestorIds = (node: Decision): string[] => {
    const ids: string[] = [];
    let current: Decision | undefined = node;
    while (current && current.kind !== "player") {
      ids.unshift(current.id);
      current = current.parentId ? graph.nodes.find((candidate) => candidate.id === current?.parentId) : undefined;
    }
    return ids;
  };

  const toggleNode = (id: string): void => {
    const node = graph.nodes.find((candidate) => candidate.id === id);
    if (!node || node.kind === "player") return;
    setRouteIds((previous) => {
      const existing = previous.indexOf(id);
      if (existing >= 0) return previous.slice(0, existing);
      if (previous.length === 0) return ancestorIds(node);
      const last = graph.nodes.find((candidate) => candidate.id === previous[previous.length - 1]);
      if (node.kind === "move" && last?.kind === "move") return [...previous, id];
      if (node.kind !== "move" && last?.kind === "move") {
        if (node.parentId === last.id || node.parentId === "player") return [...previous, id];
      }
      return ancestorIds(node);
    });
  };

  const removeRouteStep = (index: number): void => {
    setRouteIds((previous) => previous.slice(0, index));
  };

  const confirm = (): void => {
    if (route.length === 0) return;
    const selectedRoute = options.find((option) => option.id === chosenRouteId) ?? null;
    const result = store.chooseRoute(route, selectedRoute?.waypoints ?? [], selectedRoute?.label);
    if (result === "inventory") {
      setRouteIds([]);
      setView("inventory");
      return;
    }
    if (result === "started") {
      setRouteIds([]);
      setChosenRouteId(null);
      setView("none");
    }
  };

  const confirmRef = useRef(confirm);
  confirmRef.current = confirm;

  useEffect(() => {
    setRouteIds((previous) => {
      const next = previous.filter((id) => graph.nodes.some((node) => node.id === id));
      return next.length === previous.length ? previous : next;
    });
  }, [graph]);

  useEffect(() => {
    if (state.status !== "won") setGoalDismissed(false);
  }, [state.status]);

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
      if (state.status === "won" && !goalDismissed) {
        if (key === "escape" || key === "enter") {
          event.preventDefault();
          event.stopPropagation();
          setGoalDismissed(true);
        }
        return;
      }
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
  }, [view, state.pending, state.status, goalDismissed]);

  useEffect(() => {
    store.say("You cannot walk. Decide, then confirm. G opens the graph, E edits the level.", "info");
  }, [store]);

  window.__ink = {
    snapshot: () => store.getSnapshot(),
    graph: () => store.graph(),
    player: () => tracker.current,
    open,
    select: (id) => setRouteIds(id ? [id] : []),
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
          <DecisionGraph2D graph={graph} selectedIds={routeIds} onSelect={toggleNode} />
          <ViewHeader
            kicker="2D · tldraw"
            title="Decisions"
            onClose={close}
            closeKey="G"
          >
            <p className="view-hint">Click a node, then confirm the route.</p>
          </ViewHeader>
          <RouteBar
            route={route}
            options={options}
            chosenRouteId={chosenRouteId}
            onChooseRoute={setChosenRouteId}
            onRemove={removeRouteStep}
            onClear={() => setRouteIds([])}
            onConfirm={confirm}
            onCancel={close}
          />
        </div>
      ) : null}

      {view === "graph3d" ? (
        <div className="view-screen graph-view graph-view-3d">
          <DecisionGraph3D graph={graph} selectedIds={routeIds} onSelect={toggleNode} />
          <ViewHeader
            kicker="3D · three.js"
            title="Decisions"
            onClose={close}
            closeKey="T"
          >
            <p className="view-hint">Drag to orbit. Click a sphere. Confirm the route.</p>
          </ViewHeader>
          <RouteBar
            route={route}
            options={options}
            chosenRouteId={chosenRouteId}
            onChooseRoute={setChosenRouteId}
            onRemove={removeRouteStep}
            onClear={() => setRouteIds([])}
            onConfirm={confirm}
            onCancel={close}
          />
        </div>
      ) : null}

      {view === "map" ? <MapView store={store} tracker={tracker} onClose={close} /> : null}

      {view === "inventory" ? <InventoryOverlay state={state} level={level} onClose={close} /> : null}

      {view === "editor" ? (
        <LevelEditor level={level} onApply={applyLevel} onCancel={close} />
      ) : null}

      {state.status === "won" && !goalDismissed ? (
        <GoalOverlay
          state={state}
          onContinue={() => setGoalDismissed(true)}
          onRestart={() => {
            store.reset();
            setGoalDismissed(true);
            setView("none");
            setRouteIds([]);
          }}
        />
      ) : null}
    </div>
  );
}
