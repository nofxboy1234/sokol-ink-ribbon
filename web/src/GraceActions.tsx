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
  a.live === b.live &&
  a.menuOpen === b.menuOpen &&
  a.mode === b.mode &&
  a.setup === b.setup &&
  a.ready === b.ready &&
  Math.round(a.x) === Math.round(b.x) &&
  Math.round(a.y) === Math.round(b.y) &&
  a.cell === b.cell;

function WalkIcon() {
  return (
    <svg viewBox="0 0 24 24" aria-hidden="true">
      <path d="M9 5l7 7-7 7" />
    </svg>
  );
}

function RunIcon() {
  return (
    <svg viewBox="0 0 24 24" aria-hidden="true">
      <path d="M5 5l7 7-7 7M12 5l7 7-7 7" />
    </svg>
  );
}

function WaitIcon() {
  return (
    <svg viewBox="0 0 24 24" aria-hidden="true">
      <circle cx="12" cy="12" r="8" />
      <path d="M12 7v5l3 2" />
    </svg>
  );
}

const ACTIONS = [
  { label: "Walk", action: GraceAction.Walk, Icon: WalkIcon },
  { label: "Run", action: GraceAction.Run, Icon: RunIcon },
  { label: "Wait", action: GraceAction.Wait, Icon: WaitIcon },
];

const EDGE = 4;
const SPREAD = 60; // degrees between icons

// Which way the arc opens: up by default, away from any edge Grace is near.
function arcCentre(x: number, y: number, w: number, h: number, reach: number): number {
  if (y - reach < EDGE) {
    return 90; // down
  }
  if (x - reach < EDGE) {
    return 0; // right
  }
  if (x + reach > w - EDGE) {
    return 180; // left
  }
  return -90; // up
}

// Grace's actions. While Paused, tapping her dot toggles three icons in an
// arc around her. Once an action is chosen: an Ok/Cancel pill at the bottom of
// the map, so the level stays clear for picking a cell.
export function GraceActions() {
  const [grace, setGrace] = useState<GraceView>(readGrace);
  const hostRef = useRef<HTMLDivElement>(null);
  const [pane, setPane] = useState({ w: 0, h: 0, size: 40 });

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

  useLayoutEffect(() => {
    const host = hostRef.current?.parentElement;
    if (!host) {
      return;
    }
    const measure = () => {
      const size = window.matchMedia("(pointer: coarse)").matches ? 44 : 40;
      setPane((prev) =>
        prev.w === host.clientWidth && prev.h === host.clientHeight && prev.size === size
          ? prev
          : { w: host.clientWidth, h: host.clientHeight, size },
      );
    };
    measure();
    const observer = new ResizeObserver(measure);
    observer.observe(host);
    return () => observer.disconnect();
  }, []);

  const paused = grace.mode === GraceMode.Paused;
  // Nothing shows until the map is up: before that the bridge reports
  // Grace at (0, 0) and the icons would bunch up in the top-left corner.
  const picking = grace.live && paused && grace.menuOpen && grace.setup === GraceSetup.None;
  const setupName =
    grace.setup === GraceSetup.Walk
      ? "Walk"
      : grace.setup === GraceSetup.Run
        ? "Run"
        : grace.setup === GraceSetup.Wait
          ? "Wait"
          : "";
  const hint =
    grace.setup === GraceSetup.Wait
      ? "Ok to wait"
      : grace.ready
        ? "Tap the cell again or Ok"
        : "Pick a cell";

  const radius = Math.max(grace.cell * 0.9, pane.size * 0.95) + pane.size / 2;
  const reach = radius + pane.size / 2;
  const centre = arcCentre(grace.x, grace.y, pane.w, pane.h, reach);

  return (
    <div ref={hostRef} className="grace-actions">
      {picking &&
        ACTIONS.map((entry, i) => {
          // keep Walk on the left when the arc opens downwards
          const step = centre === 90 ? 1 - i : i - 1;
          const angle = ((centre + step * SPREAD) * Math.PI) / 180;
          const x = grace.x + Math.cos(angle) * radius;
          const y = grace.y + Math.sin(angle) * radius;
          const half = pane.size / 2;
          return (
            <button
              key={entry.label}
              type="button"
              className="grace-icon"
              aria-label={entry.label}
              title={entry.label}
              style={{
                left: Math.min(Math.max(x, EDGE + half), pane.w - EDGE - half),
                top: Math.min(Math.max(y, EDGE + half), pane.h - EDGE - half),
                width: pane.size,
                height: pane.size,
              }}
              onClick={() => graceAction(entry.action)}
            >
              <entry.Icon />
            </button>
          );
        })}
      {grace.live && paused && grace.setup !== GraceSetup.None && (
        <div className="grace-pill" role="toolbar" aria-label="Confirm action">
          <span className="grace-pill-label">
            <strong>{setupName}</strong> · {hint}
          </span>
          <button type="button" disabled={!grace.ready} onClick={() => graceAction(GraceAction.Ok)}>
            Ok
          </button>
          <button type="button" onClick={() => graceAction(GraceAction.Cancel)}>
            Cancel
          </button>
        </div>
      )}
    </div>
  );
}
