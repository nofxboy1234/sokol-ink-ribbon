import { useState } from "react";
import { ITEM_NAMES } from "./wasmBridge";
import { useElapsed, useGameState } from "./GameState";

export function formatMs(ms: number): string {
  const total = Math.max(0, Math.floor(ms / 1000));
  const minutes = Math.floor(total / 60);
  const seconds = total % 60;
  return `${minutes}:${String(seconds).padStart(2, "0")}`;
}

export function RunPanel() {
  const elapsed = useElapsed();
  const { steps, revealed, itemsCollected } = useGameState();
  const [status, setStatus] = useState("");

  const save = async () => {
    const replay = [{ steps, revealed, itemsCollected, t: Math.round(elapsed) }];
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
  const labels = ["Fine", "Caution", "Danger"];
  const colors = ["#3fbf5f", "#e0a020", "#d33b3b"];
  return (
    <div className="health" data-state={health}>
      <span className="health-dot" style={{ background: colors[health] ?? colors[0] }} />
      <span className="health-label">{labels[health] ?? labels[0]}</span>
    </div>
  );
}

export function Inventory() {
  const { inventory } = useGameState();
  return (
    <div className="pane-content">
      <h2>Items</h2>
      <div className="inventory-grid">
        {inventory.map((item, index) => (
          <div key={index} className="inventory-slot" title={item >= 0 ? ITEM_NAMES[item] : ""}>
            {item >= 0 && <span className="inventory-item" data-item={item} />}
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
  return (
    <div className="pane-content">
      <h2>Crafting</h2>
      <ul className="recipe-list">
        {RECIPES.map((recipe) => (
          <li key={recipe.output} className="recipe">
            <span className="recipe-inputs">{recipe.inputs.join(" + ")}</span>
            <span className="recipe-arrow">→</span>
            <span className="recipe-output">{recipe.output}</span>
          </li>
        ))}
      </ul>
    </div>
  );
}

const FILES = [
  { name: "Fuse box note", code: 4721 },
  { name: "Lobby safe", code: 1938 },
];

export function Files() {
  return (
    <div className="pane-content">
      <h2>Files</h2>
      <ul className="file-list">
        {FILES.map((file) => (
          <li key={file.name} className="file">
            <span className="file-name">{file.name}</span>
            <span className="file-code">{file.code}</span>
          </li>
        ))}
      </ul>
    </div>
  );
}
