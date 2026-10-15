import { useEffect, useLayoutEffect, useRef, useState } from "react";
import {
  GraceAction,
  GraceMode,
  GraceSetup,
  graceAction,
  readGrace,
  type GraceView,
} from "./wasmBridge";

const same = (a: GraceView, b: GraceView) =>
  a.mode === b.mode &&
  a.setup === b.setup &&
  a.ready === b.ready &&
  Math.round(a.x) === Math.round(b.x) &&
  Math.round(a.y) === Math.round(b.y) &&
  a.cell === b.cell;

const ACTIONS = [
  { label: "Walk", setup: GraceSetup.Walk, action: GraceAction.Walk },
  { label: "Run", setup: GraceSetup.Run, action: GraceAction.Run },
  { label: "Wait", setup: GraceSetup.Wait, action: GraceAction.Wait },
];

const EDGE = 6;

// Grace's action menu, shown above her only while she is Paused. Pick an
// action (the others lock), make its choices, then Ok or Cancel.
export function GraceActions() {
  const [grace, setGrace] = useState<GraceView>(readGrace);
  const menuRef = useRef<HTMLDivElement>(null);
  const [shift, setShift] = useState({ x: 0, y: 0 });

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

  const paused = grace.mode === GraceMode.Paused;
  const left = grace.x;
  const top = grace.y - Math.max(grace.cell * 0.6, 12);

  // Keep the menu inside the map pane near its edges.
  useLayoutEffect(() => {
    const menu = menuRef.current;
    const pane = menu?.offsetParent as HTMLElement | null;
    if (!menu || !pane) {
      return;
    }
    const w = menu.offsetWidth;
    const h = menu.offsetHeight;
    const minX = EDGE + w / 2;
    const maxX = pane.clientWidth - EDGE - w / 2;
    const x = Math.min(Math.max(left, minX), Math.max(minX, maxX)) - left;
    const y = top - h < EDGE ? EDGE + h - top : 0;
    setShift((prev) => (prev.x === x && prev.y === y ? prev : { x, y }));
  }, [left, top, paused, grace.setup]);

  if (!paused) {
    return null;
  }
  const locked = grace.setup !== GraceSetup.None;
  const hint =
    grace.setup === GraceSetup.Walk || grace.setup === GraceSetup.Run
      ? grace.ready
        ? "Ok to go"
        : "Pick a cell"
      : grace.setup === GraceSetup.Wait
        ? "Ok to wait"
        : null;

  return (
    <div
      ref={menuRef}
      className="grace-menu"
      role="toolbar"
      aria-label="Grace actions"
      style={{ left: left + shift.x, top: top + shift.y }}
    >
      <div className="grace-menu-row">
        {ACTIONS.map((entry) => (
          <button
            key={entry.label}
            type="button"
            aria-pressed={grace.setup === entry.setup}
            disabled={locked && grace.setup !== entry.setup}
            onClick={() => graceAction(entry.action)}
          >
            {entry.label}
          </button>
        ))}
      </div>
      <div className="grace-menu-row">
        <button type="button" disabled={!grace.ready} onClick={() => graceAction(GraceAction.Ok)}>
          Ok
        </button>
        <button type="button" disabled={!locked} onClick={() => graceAction(GraceAction.Cancel)}>
          Cancel
        </button>
      </div>
      {hint && <div className="grace-menu-hint">{hint}</div>}
    </div>
  );
}
