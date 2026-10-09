import { useMemo, useState } from "react";
import {
  ITEM_NAMES,
  craftRecipe,
  gameReset,
  readFiles,
  readReplay,
  setLighter,
} from "./wasmBridge";
import { useElapsed, useGameState } from "./GameState";

export function formatMs(ms: number): string {
  const total = Math.max(0, Math.floor(ms / 1000));
  const minutes = Math.floor(total / 60);
  const seconds = total % 60;
  return `${minutes}:${String(seconds).padStart(2, "0")}`;
}

const ITEM_COLORS = [
  "#00ddff",
  "#f2ff00",
  "#00ff26",
  "#1e1e1e",
  "#9d9d9d",
  "#ff002a",
  "#f2ff00",
  "#ff006a",
  "#961eff",
];

export function RunPanel() {
  const elapsed = useElapsed();
  const [status, setStatus] = useState("");

  const save = async () => {
    const replay = readReplay();
    try {
      const response = await fetch("/runs", {
        method: "POST",
        headers: { "content-type": "application/json" },
        body: JSON.stringify({
          levelId: "care-center-01",
          durationMs: Math.round(elapsed),
          completed: false,
          replay,
        }),
      });
      setStatus(
        response.ok ? "Saved" : response.status === 401 ? "Sign in to save" : "Save failed",
      );
    } catch {
      setStatus("Save failed");
    }
  };

  return (
    <div className="run-panel">
      <span className="timer">{formatMs(elapsed)}</span>
      <button type="button" onClick={() => void save()}>
        Save run
      </button>
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

export function PauseMenu({ open, onClose }: { open: boolean; onClose: () => void }) {
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
        <button
          type="button"
          className="pause-item"
          onClick={() => {
            gameReset();
            onClose();
          }}
        >
          Load Game
        </button>
        <button type="button" className="pause-item" onClick={onClose}>
          Resume
        </button>
      </div>
    </div>
  );
}
