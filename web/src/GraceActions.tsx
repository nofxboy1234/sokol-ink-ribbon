import { useEffect, useState } from "react";
import { GraceAction, GraceMode, graceAction, readGrace, type GraceView } from "./wasmBridge";

const same = (a: GraceView, b: GraceView) =>
  a.mode === b.mode &&
  a.canPause === b.canPause &&
  Math.round(a.x) === Math.round(b.x) &&
  Math.round(a.y) === Math.round(b.y) &&
  a.cell === b.cell;

// Grace's action menu and Pause action, drawn over the map above her dot.
export function GraceActions() {
  const [grace, setGrace] = useState<GraceView>(readGrace);

  useEffect(() => {
    let frame = 0;
    const poll = () => {
      const next = readGrace();
      setGrace((prev) => (same(prev, next) ? prev : next));
      frame = requestAnimationFrame(poll);
    };
    frame = requestAnimationFrame(poll);
    return () => cancelAnimationFrame(frame);
  }, []);

  const above = { left: grace.x, top: grace.y - Math.max(grace.cell * 0.6, 12) };

  return (
    <>
      {grace.mode === GraceMode.Menu && (
        <div className="grace-menu" role="menu" aria-label="Grace actions" style={above}>
          <button type="button" role="menuitem" onClick={() => graceAction(GraceAction.Walk)}>
            Walk
          </button>
          <button type="button" role="menuitem" onClick={() => graceAction(GraceAction.Run)}>
            Run
          </button>
          <button type="button" role="menuitem" onClick={() => graceAction(GraceAction.Wait)}>
            Wait
          </button>
        </div>
      )}
      {grace.canPause && (
        <button
          type="button"
          className="grace-pause"
          aria-label="Pause Grace"
          style={above}
          onClick={() => graceAction(GraceAction.Pause)}
        >
          {grace.mode === GraceMode.Waiting ? "Waiting… Pause" : "Pause"}
        </button>
      )}
      {grace.mode === GraceMode.Target && (
        <div className="grace-hint">
          <span>Pick a cell</span>
          <button type="button" onClick={() => graceAction(GraceAction.Cancel)}>
            Cancel
          </button>
        </div>
      )}
    </>
  );
}
