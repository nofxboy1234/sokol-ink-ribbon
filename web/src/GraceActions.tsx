import { useEffect, useLayoutEffect, useRef, useState, type ReactNode } from "react";
import {
  GraceMode,
  quickSelect,
  radialSlot,
  readGrace,
  readRadialDirection,
  graceDead,
  type GraceView,
} from "./wasmBridge";
import { useGameState } from "./GameState";

const same = (a: GraceView, b: GraceView) =>
  (Object.keys(a) as (keyof GraceView)[]).every((key) =>
    key === "x" || key === "y" ? Math.round(a[key]) === Math.round(b[key]) : a[key] === b[key],
  );

// Hurt: a jagged crack, she can't move for a moment.
function HurtIcon() {
  return (
    <svg viewBox="0 0 24 24" aria-hidden="true">
      <path
        d="M4 12h4l2-5 3 10 2-5h5"
        fill="none"
        stroke="currentColor"
        strokeWidth="2.4"
        strokeLinejoin="round"
        strokeLinecap="round"
      />
    </svg>
  );
}

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

// The Empty Bottle, drawn like its blue dot on the map.
function BottleIcon() {
  return <span className="grace-bottle" aria-hidden="true" />;
}

// The Lighter: a yellow flame swatch, lit or not.
function LighterIcon() {
  return <span className="grace-lighter" aria-hidden="true" />;
}

// What the status circle shows for each grace_mode_t.
const STATUS = [
  { label: "Paused", Icon: PauseIcon },
  { label: "Walking", Icon: WalkIcon },
  { label: "Running", Icon: RunIcon },
  { label: "Waiting", Icon: WaitIcon },
  { label: "Hurt", Icon: HurtIcon },
];

const EDGE = 4;

// The Quick Equip circles sit on the compass points around her dot: N, E, S, W.
const COMPASS = [-90, 0, 90, 180];
const SLOT_NAMES = ["North", "East", "South", "West"];
// Tried in order when a compass point is taken or off the map.
const TURNS = [
  0, 22.5, -22.5, 45, -45, 67.5, -67.5, 90, -90, 112.5, -112.5, 135, -135, 157.5, -157.5, 180,
];
// The status circle sits top-left under the timer chip (app.css
// .grace-status); its centre is reserved so equip circles avoid it.
const STATUS_CENTRE = { x: 10 + 20, y: 46 + 20 };

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
  const placed: { x: number; y: number }[] = [STATUS_CENTRE];
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

// Drawn sizes in cells, from native/src/render.c: Grace's dot radius and
// the larger of an item's glyph and its pickup marker.
const GRACE_DOT = 0.51;
const ITEM_MARK = 0.36;

// Distance from Grace's centre to each circle's centre. The neighbouring
// item spans 1 - ITEM_MARK .. 1 + ITEM_MARK cells; the gap between her dot
// and it is (1 - ITEM_MARK - GRACE_DOT) cells, and the circle's inner edge
// sits the same gap beyond the item's far edge.
export function menuRadius(cell: number, size: number): number {
  const gap = 1 - ITEM_MARK - GRACE_DOT;
  return (1 + ITEM_MARK + gap) * cell + size / 2;
}

type Slot = {
  label: string;
  icon: ReactNode;
  onClick?: () => void;
  disabled?: boolean;
  pressed?: boolean;
  // equipped, but none left (greyed out)
  spent?: boolean;
  title?: string;
};

// Grace's remaining circle UI: the status circle (top-left) and the
// Quick Equip circles around her dot (opened by Z on desktop or holding
// the right stick's centre -- her dot itself takes no input). The old
// radial action menu is gone -- movement is WASD / the analogue stick
// now, and throwing has its own aim mode.
export function GraceActions() {
  const { dead } = useGameState();
  const [grace, setGrace] = useState<GraceView>(readGrace);
  const hostRef = useRef<HTMLDivElement>(null);
  const [pane, setPane] = useState({ w: 0, h: 0, size: 40 });
  const [highlight, setHighlight] = useState(-1);
  const mouseRef = useRef<{ x: number; y: number } | null>(null);

  useEffect(() => {
    const onMouse = (event: MouseEvent) => {
      mouseRef.current = { x: event.clientX, y: event.clientY };
    };
    window.addEventListener("mousemove", onMouse);
    return () => window.removeEventListener("mousemove", onMouse);
  }, []);

  useEffect(() => {
    let frame = 0;
    const poll = () => {
      const next = readGrace();
      setGrace((prev) => (same(prev, next) ? prev : next));
      // Radial selection: while Quick Equip is open, whichever slot the
      // pointer direction indicates is highlighted -- the right stick's
      // live direction on touch, the mouse's direction from Grace on
      // desktop. Clicking (desktop) or releasing the stick (touch)
      // confirms it; the centre/dead zone highlights nothing.
      let idx = -1;
      if (next.live && next.quickOpen) {
        const dir = readRadialDirection();
        if (Math.hypot(dir.x, dir.y) > 0.2) {
          idx = radialSlot(dir.x, dir.y);
        } else if (mouseRef.current && hostRef.current?.parentElement) {
          const rect = hostRef.current.parentElement.getBoundingClientRect();
          const dx = mouseRef.current.x - rect.left - next.x;
          const dy = mouseRef.current.y - rect.top - next.y;
          if (Math.hypot(dx, dy) > 36) {
            idx = radialSlot(dx, dy);
          }
        }
      }
      setHighlight((prev) => (prev === idx ? prev : idx));
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
  const equipping = grace.live && grace.quickOpen;

  // An item right next to her sits with equal gaps on both sides: her dot
  // edge, gap, item, gap, the circle's edge (see menuRadius).
  const radius = menuRadius(grace.cell, pane.size);
  const spots = placeSlots(grace.x, grace.y, pane.w, pane.h, radius, pane.size);

  // Quick Equip: the Lighter sits north once picked up (off until picked here),
  // the bottle east once equipped. With every bottle used up the bottle stays
  // in its slot, greyed out, until she finds another.
  const quickSlots: Slot[] = SLOT_NAMES.map((name, i): Slot => {
    if (i === 0 && grace.hasLighter) {
      return {
        label: "Lighter",
        icon: <LighterIcon />,
        pressed: grace.lighterOn,
        title: grace.lighterOn
          ? "Lighter · on (pick to put it out)"
          : "Lighter · off (pick to light it)",
        onClick: () => quickSelect(0),
      };
    }
    if (i === 1 && grace.bottleEquipped) {
      return grace.bottleEmpty
        ? {
            label: "Empty Bottle",
            icon: <BottleIcon />,
            spent: true,
            disabled: true,
            title: "Empty Bottle · none left",
          }
        : {
            label: "Empty Bottle",
            icon: <BottleIcon />,
            pressed: grace.bottleActive,
            title: grace.bottleActive ? "Empty Bottle · active" : "Empty Bottle · make active",
            onClick: () => quickSelect(1),
          };
    }
    return { label: `${name} slot empty`, icon: null, disabled: true };
  });

  const slots = equipping ? quickSlots : [];
  if (dead || graceDead()) return null;  // game over: no circles underneath
  const status = STATUS[grace.mode] ?? STATUS[3];
  const statusLabel = grace.aiming
    ? "Grace: Aiming"
    : paused
      ? `Grace: Paused, last ${STATUS[grace.last]?.label ?? "Waiting"}`
      : `Grace: ${status.label}`;

  return (
    <div ref={hostRef} className="grace-actions">
      {grace.live && (
        <div
          className="grace-status"
          role="status"
          aria-live="polite"
          aria-label={statusLabel}
          data-paused={paused}
          data-aiming={grace.aiming || undefined}
          data-hurt={grace.mode === GraceMode.Hurt}
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
          aria-label="Quick Equip"
          data-kind="quick"
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
                data-spent={slot.spent || undefined}
                data-slot={SLOT_NAMES[i]}
                data-highlighted={highlight === i || undefined}
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
    </div>
  );
}
