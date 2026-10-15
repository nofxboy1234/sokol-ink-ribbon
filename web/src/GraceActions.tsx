import { useEffect, useState } from "react";
import {
  GraceAction,
  GraceMode,
  GraceSetup,
  graceAction,
  readGrace,
  type GraceView,
} from "./wasmBridge";

const same = (a: GraceView, b: GraceView) =>
  a.mode === b.mode && a.running === b.running && a.setup === b.setup && a.ready === b.ready;

// The action bar along the bottom of the map: pick an action, make its
// choices, then Ok to commit it or Cancel to keep doing what she was doing.
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

  const current =
    grace.mode === GraceMode.Waiting
      ? GraceSetup.Wait
      : grace.mode === GraceMode.Moving
        ? grace.running
          ? GraceSetup.Run
          : GraceSetup.Walk
        : GraceSetup.None;
  const pending = grace.setup !== GraceSetup.None;
  const actions = [
    { label: "Walk", setup: GraceSetup.Walk, action: GraceAction.Walk },
    { label: "Run", setup: GraceSetup.Run, action: GraceAction.Run },
    { label: "Wait", setup: GraceSetup.Wait, action: GraceAction.Wait },
  ];
  const hint =
    grace.setup === GraceSetup.Walk || grace.setup === GraceSetup.Run
      ? grace.ready
        ? "Ok to go"
        : "Pick a cell"
      : grace.setup === GraceSetup.Wait
        ? "Ok to wait"
        : null;

  return (
    <div className="action-bar" role="toolbar" aria-label="Grace actions">
      {actions.map((entry) => (
        <button
          key={entry.label}
          type="button"
          className="action-btn"
          aria-pressed={grace.setup === entry.setup}
          data-current={current === entry.setup}
          onClick={() => graceAction(entry.action)}
        >
          {entry.label}
        </button>
      ))}
      <span className="action-gap" />
      <button
        type="button"
        className="action-btn action-ok"
        disabled={!grace.ready}
        onClick={() => graceAction(GraceAction.Ok)}
      >
        Ok
      </button>
      <button
        type="button"
        className="action-btn"
        disabled={!pending}
        onClick={() => graceAction(GraceAction.Cancel)}
      >
        Cancel
      </button>
      {hint && <span className="action-hint">{hint}</span>}
    </div>
  );
}
