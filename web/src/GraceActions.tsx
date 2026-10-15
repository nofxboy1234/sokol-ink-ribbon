import { useEffect, useLayoutEffect, useRef, useState, type ReactNode } from "react";
import {
  GraceAction,
  GraceMode,
  GraceSetup,
  ThrowStance,
  graceAction,
  quickSelect,
  readGrace,
  type GraceView,
} from "./wasmBridge";

const same = (a: GraceView, b: GraceView) =>
  (Object.keys(a) as (keyof GraceView)[]).every((key) =>
    key === "x" || key === "y" ? Math.round(a[key]) === Math.round(b[key]) : a[key] === b[key],
  );

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

function PauseIcon() {
  return (
    <svg viewBox="0 0 24 24" aria-hidden="true">
      <path d="M9 6v12M15 6v12" />
    </svg>
  );
}

function ThrowIcon() {
  return (
    <svg viewBox="0 0 24 24" aria-hidden="true">
      <path d="M5 19L17 7M11 7h6v6" />
      <circle cx="6" cy="6" r="1.5" />
    </svg>
  );
}

// The Empty Bottle, drawn like its blue dot on the map.
function BottleIcon() {
  return <span className="grace-bottle" aria-hidden="true" />;
}

// What the status circle shows for each grace_mode_t.
const STATUS = [
  { label: "Paused", Icon: PauseIcon },
  { label: "Walking", Icon: WalkIcon },
  { label: "Running", Icon: RunIcon },
  { label: "Waiting", Icon: WaitIcon },
];

const EDGE = 4;

// Both circle menus sit on the compass points around her dot: N, E, S, W.
const COMPASS = [-90, 0, 90, 180];
const SLOT_NAMES = ["North", "East", "South", "West"];
// Tried in order when a compass point is taken or off the map.
const TURNS = [
  0, 22.5, -22.5, 45, -45, 67.5, -67.5, 90, -90, 112.5, -112.5, 135, -135, 157.5, -157.5, 180,
];
// Centre of the status circle from the pane's bottom-right (app.css .grace-status).
const STATUS_INSET = 10 + 20;

// Where each slot goes: its compass point, or, if that would leave the map
// or sit on a slot already placed, the nearest free point turning away from
// it in 22.5° steps (further out if needed).
function placeSlots(
  gx: number,
  gy: number,
  w: number,
  h: number,
  radius: number,
  size: number,
): { x: number; y: number }[] {
  const half = size / 2;
  const fits = (x: number, y: number) =>
    x - half >= EDGE && x + half <= w - EDGE && y - half >= EDGE && y + half <= h - EDGE;
  // the status circle in the bottom-right corner is taken too
  const placed: { x: number; y: number }[] = [{ x: w - STATUS_INSET, y: h - STATUS_INSET }];
  const clear = (x: number, y: number) =>
    placed.every((p) => Math.hypot(p.x - x, p.y - y) >= size * 1.05);
  const slots: { x: number; y: number }[] = [];
  for (const base of COMPASS) {
    let spot: { x: number; y: number } | null = null;
    for (const turn of TURNS) {
      for (const r of [radius, radius * 1.6, radius * 2.2]) {
        const a = ((base + turn) * Math.PI) / 180;
        const x = gx + Math.cos(a) * r;
        const y = gy + Math.sin(a) * r;
        if (fits(x, y) && clear(x, y)) {
          spot = { x, y };
          break;
        }
      }
      if (spot) {
        break;
      }
    }
    if (!spot) {
      const a = (base * Math.PI) / 180;
      spot = {
        x: Math.min(Math.max(gx + Math.cos(a) * radius, EDGE + half), w - EDGE - half),
        y: Math.min(Math.max(gy + Math.sin(a) * radius, EDGE + half), h - EDGE - half),
      };
    }
    placed.push(spot);
    slots.push(spot);
  }
  return slots;
}

type Slot = {
  label: string;
  icon: ReactNode;
  onClick?: () => void;
  disabled?: boolean;
  pressed?: boolean;
  title?: string;
};

// Grace's circle menus. While Paused her action menu (Walk, Run, Wait and,
// with the bottle active, Throw) sits on the compass points around her dot;
// a right click or long press on her dot swaps it for Quick Equip. Once an
// action is chosen: an Ok/Cancel pill at the bottom of the map, so the level
// stays clear for picking a cell.
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
  const choosing = grace.live && paused && grace.setup === GraceSetup.None;
  const picking = choosing && grace.menuOpen;
  const equipping = choosing && grace.quickOpen;
  const setupName =
    grace.setup === GraceSetup.Walk
      ? "Walk"
      : grace.setup === GraceSetup.Run
        ? "Run"
        : grace.setup === GraceSetup.Wait
          ? "Wait"
          : grace.setup === GraceSetup.Throw
            ? "Throw"
            : "";
  const hint =
    grace.setup === GraceSetup.Wait
      ? "Ok to wait"
      : grace.setup === GraceSetup.Throw
        ? grace.ready
          ? `Tap again or Ok · ${grace.hitChance}% vs enemy`
          : "Pick a cell in range"
        : grace.ready
          ? "Tap again or Ok"
          : "Pick a cell";

  const radius = Math.max(grace.cell * 0.9, pane.size * 0.95) + pane.size / 2;
  const spots = placeSlots(grace.x, grace.y, pane.w, pane.h, radius, pane.size);

  // Action menu: Walk north, Run east, Wait south, Throw west (only with the
  // bottle active; otherwise an empty circle).
  const throwSlot: Slot = grace.bottleActive
    ? {
        label: "Throw",
        icon: <ThrowIcon />,
        disabled: !grace.throwAllowed,
        title:
          grace.stance === ThrowStance.Run
            ? "Throw · not while running"
            : `Throw the Empty Bottle · ${grace.hitChance}% to hit the enemy`,
        onClick: () => graceAction(GraceAction.Throw),
      }
    : { label: "Empty", icon: null, disabled: true };
  const actionSlots: Slot[] = [
    { label: "Walk", icon: <WalkIcon />, onClick: () => graceAction(GraceAction.Walk) },
    { label: "Run", icon: <RunIcon />, onClick: () => graceAction(GraceAction.Run) },
    { label: "Wait", icon: <WaitIcon />, onClick: () => graceAction(GraceAction.Wait) },
    throwSlot,
  ];
  // Quick Equip: the bottle sits east once equipped; the rest are empty.
  const quickSlots: Slot[] = SLOT_NAMES.map((name, i) =>
    i === 1 && grace.bottleEquipped
      ? {
          label: "Empty Bottle",
          icon: <BottleIcon />,
          pressed: grace.bottleActive,
          title: grace.bottleActive ? "Empty Bottle · active" : "Empty Bottle · make active",
          onClick: () => quickSelect(1),
        }
      : { label: `${name} slot empty`, icon: null, disabled: true },
  );

  const slots = picking ? actionSlots : equipping ? quickSlots : [];
  const status = STATUS[paused ? grace.last : grace.mode] ?? STATUS[3];
  const statusLabel = paused ? `Grace: Paused, last ${status.label}` : `Grace: ${status.label}`;

  return (
    <div ref={hostRef} className="grace-actions">
      {grace.live && (
        <div
          className="grace-status"
          role="status"
          aria-live="polite"
          aria-label={statusLabel}
          data-paused={paused}
        >
          <status.Icon />
          {paused && (
            <span className="grace-status-badge" aria-hidden="true">
              <PauseIcon />
            </span>
          )}
        </div>
      )}
      {slots.length > 0 && (
        <div
          className="grace-ring"
          role="group"
          aria-label={equipping ? "Quick Equip" : "Actions"}
          data-kind={equipping ? "quick" : "actions"}
        >
          {slots.map((slot, i) => {
            const spot = spots[i] ?? { x: grace.x, y: grace.y };
            const empty = slot.icon === null;
            return (
              <button
                key={`${slot.label}-${i}`}
                type="button"
                className="grace-icon"
                data-empty={empty}
                data-slot={SLOT_NAMES[i]}
                aria-label={slot.label}
                aria-pressed={slot.pressed}
                aria-hidden={empty || undefined}
                tabIndex={empty ? -1 : undefined}
                title={slot.title ?? (empty ? "" : slot.label)}
                disabled={slot.disabled}
                style={{ left: spot.x, top: spot.y, width: pane.size, height: pane.size }}
                onClick={slot.onClick}
                onContextMenu={(event) => event.preventDefault()}
              >
                {slot.icon}
              </button>
            );
          })}
        </div>
      )}
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
