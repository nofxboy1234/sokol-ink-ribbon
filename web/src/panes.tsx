import { useEffect, useMemo, useState } from "react";
import {
  ITEM_NAMES,
  SAVE_SLOTS,
  craftRecipe,
  gameReset,
  loadGame,
  readFiles,
  readReplay,
  runElapsedMs,
  saveGame,
  saveTimeMs,
  saveUsed,
  setLighter,
} from "./wasmBridge";
import { useElapsed, useGameState } from "./GameState";

export function formatMs(ms: number): string {
  const total = Math.max(0, Math.floor(ms / 1000));
  const minutes = Math.floor(total / 60);
  const seconds = total % 60;
  return `${minutes}:${String(seconds).padStart(2, "0")}`;
}

// Same colours as item_color_rgb in native/src/level.c.
const ITEM_COLORS = [
  "#0008ff",
  "#bbbbbb",
  "#00ff26",
  "#cc0066",
  "#ff00ee",
  "#00cc26",
  "#00ffbb",
  "#f2f200",
  "#e60026",
];

// The map page unmounts on the way to Records, but the wasm game stays
// alive, so this has to live outside RunPanel or each visit posts again.
let goalRunSaved = false;

export function RunPanel() {
  const elapsed = useElapsed();
  const { goalMet } = useGameState();
  const [status, setStatus] = useState("");

  // A run saves itself the moment the goal (the fuse box) is activated.
  useEffect(() => {
    if (!goalMet) {
      goalRunSaved = false;
      return;
    }
    if (goalRunSaved) {
      return;
    }
    goalRunSaved = true;
    const replay = readReplay();
    void (async () => {
      try {
        const response = await fetch("/runs", {
          method: "POST",
          headers: { "content-type": "application/json" },
          body: JSON.stringify({
            levelId: "care-center-01",
            durationMs: Math.round(runElapsedMs()),
            completed: true,
            replay,
          }),
        });
        setStatus(
          response.ok ? "Run saved" : response.status === 401 ? "Sign in to save" : "Save failed",
        );
      } catch {
        setStatus("Save failed");
      }
    })();
  }, [goalMet]);

  return (
    <div className="run-panel">
      <span className="timer">{formatMs(elapsed)}</span>
      {status && <span className="run-status">{status}</span>}
    </div>
  );
}

export function HealthBar() {
  const { health } = useGameState();
  const labels = ["FINE", "CAUTION", "DANGER"];
  const colors = ["var(--green)", "var(--orange)", "var(--red)"];
  return (
    <div className="health" style={{ background: colors[health] ?? colors[0] }}>
      <span className="health-label">{labels[health] ?? labels[0]}</span>
    </div>
  );
}

export function LighterButton() {
  const { hasLighter, lighterOn } = useGameState();
  if (!hasLighter) {
    return null;
  }
  return (
    <button
      type="button"
      className="lighter"
      data-on={lighterOn}
      onClick={() => setLighter(!lighterOn)}
    >
      Lighter {lighterOn ? "on" : "off"}
    </button>
  );
}

export function Inventory() {
  const { inventory } = useGameState();
  return (
    <div className="pane-content">
      <h2>ITEMS</h2>
      <div className="inventory-grid">
        {inventory.map((item, index) => (
          <div
            key={index}
            className="inventory-slot"
            data-filled={item >= 0}
            title={item >= 0 ? ITEM_NAMES[item] : ""}
          >
            {item >= 0 && (
              <span
                className="inventory-item"
                style={{ background: ITEM_COLORS[item] ?? "#ff006a" }}
              />
            )}
            {item >= 0 && <span className="inventory-name">{ITEM_NAMES[item]}</span>}
          </div>
        ))}
      </div>
    </div>
  );
}

const RECIPES = [
  { inputs: ["Green Herb", "Green Herb"], output: "Mixed Herb" },
  { inputs: ["Green Herb", "Empty Bottle"], output: "Herb Bottle" },
  { inputs: ["Fuse", "Screwdriver"], output: "Repaired Fuse" },
];

export function Crafting() {
  const { inventory } = useGameState();
  return (
    <div className="pane-content">
      <h2>CRAFTING</h2>
      <ul className="recipe-list">
        {RECIPES.map((recipe, index) => (
          <li key={recipe.output}>
            <button
              type="button"
              className="recipe"
              disabled={inventory.every((item) => item < 0)}
              onClick={() => craftRecipe(index)}
            >
              <span className="recipe-dot" style={{ background: "#ff006a" }} />
              <span>{recipe.inputs[0]}</span>
              <span className="recipe-op">+</span>
              <span className="recipe-ring" />
              <span>{recipe.inputs[1]}</span>
              <span className="recipe-op">→</span>
              <span className="recipe-dot" style={{ background: "#ff006a" }} />
              <span className="recipe-output">{recipe.output}</span>
            </button>
          </li>
        ))}
      </ul>
    </div>
  );
}

export function Files() {
  const { filesFound } = useGameState();
  const files = useMemo(() => readFiles(), [filesFound]);
  return (
    <div className="pane-content">
      <h2>FILES</h2>
      <ul className="file-list">
        {files.length === 0 && <li className="file file-empty">NO DATA</li>}
        {files.map((file) => (
          <li key={file.name} className="file">
            <span className="file-name">{file.name}</span>
            <span className="file-code">= {file.code}</span>
          </li>
        ))}
      </ul>
    </div>
  );
}

export function PauseMenu({
  open,
  onClose,
  onLoad,
}: {
  open: boolean;
  onClose: () => void;
  onLoad: () => void;
}) {
  if (!open) {
    return null;
  }
  return (
    <div className="pause-overlay" onClick={onClose}>
      <div className="pause-menu" onClick={(event) => event.stopPropagation()}>
        <button
          type="button"
          className="pause-item pause-selected"
          onClick={() => {
            gameReset();
            onClose();
          }}
        >
          Restart
        </button>
        <button type="button" className="pause-item" onClick={onLoad}>
          Load Game
        </button>
        <button type="button" className="pause-item" onClick={onClose}>
          Resume
        </button>
      </div>
    </div>
  );
}

// Save/Load menu in the same style as the pause menu (ref/map.kra "Load menu").
export function SaveLoadMenu({ open, onClose }: { open: boolean; onClose: () => void }) {
  if (!open) {
    return null;
  }
  // Remount fresh each time the menu opens. saveUsed/saveTimeMs read mutable
  // wasm state that React can't observe, so the React Compiler would otherwise
  // memoize the reads and show stale slot data after a save.
  return <SaveLoadSlots onClose={onClose} />;
}

type SaveSlotInfo = { index: number; used: boolean; time: number };

function SaveLoadSlots({ onClose }: { onClose: () => void }) {
  const slots = useMemo<SaveSlotInfo[]>(
    () =>
      Array.from({ length: SAVE_SLOTS }, (_, index) => ({
        index,
        used: saveUsed(index),
        time: saveTimeMs(index),
      })),
    [],
  );
  return (
    <div className="pause-overlay" onClick={onClose}>
      <div className="save-menu" onClick={(event) => event.stopPropagation()}>
        <div className="save-title">Save / Load</div>
        {slots.map((slot) => (
          <div key={slot.index} className="save-row">
            <span className="save-slot">Slot {slot.index + 1}</span>
            <span className="save-data">{slot.used ? formatMs(slot.time) : "NO DATA"}</span>
            <button
              type="button"
              className="pause-item save-btn"
              onClick={() => {
                saveGame(slot.index);
                onClose();
              }}
            >
              Save
            </button>
            <button
              type="button"
              className="pause-item save-btn"
              disabled={!slot.used}
              onClick={() => {
                loadGame(slot.index);
                onClose();
              }}
            >
              Load
            </button>
          </div>
        ))}
        <button type="button" className="pause-item" onClick={onClose}>
          Close
        </button>
      </div>
    </div>
  );
}
