import { useEffect, useRef, useState } from "react";
import {
  aimDeadZone,
  aimInput,
  interactTrigger,
  quickSelect,
  quickToggle,
  radialSlot,
  readGrace,
  setRadialDirection,
  stickInput,
} from "./wasmBridge";
import {
  dispatchMenuAccept,
  dispatchMenuShortcut,
  dispatchMenuStep,
  menuCapturing,
} from "./menuNav";

// Twin sticks (touch only, see app.css). LEFT stick moves Grace: the first
// half of its throw walks, further runs -- the C side turns the vector
// into a speed. RIGHT stick is always shown: it aims the bottle throw
// while a bottle is active (greyed, aim unavailable, otherwise), its
// centre tap is the interact trigger (pick up / climb / use -- the Space
// key on desktop), and holding its centre opens Quick Equip, after which
// pointing it at a radial slot and releasing equips that slot. She can
// aim while moving; aiming caps her speed to a walk in C.
// Stick travel as a fraction of the stick's rendered size: 52px at the
// full 128px size (see --stick-size in app.css). The CSS shrinks the stick
// on short (landscape) screens, so travel -- and with it the walk/run
// bands and the aim length, which are all fractions of this radius -- is
// derived from the element's actual size, never a fixed pixel count.
const RADIUS_RATIO = 52 / 128;

// Dead zone, as fractions of the travel radius above. A touch only starts
// emitting a direction once it pushes past DEAD_ZONE_EXIT: 0.34 of travel
// is ~18px at the full 128px stick size and ~12-14px shrunk in landscape
// (90-98px sticks) -- a fat finger's centre is hard to place precisely,
// so the earlier 0.20 zone still read resting drift as a direction. The
// walk band is everything from here to half travel (C walks to 0.5), so
// 0.34-0.5 remains for a deliberate small push to walk, and an aim push
// past 0.34 already reaches ~2.7 cells of throw. A centre tap whose drift
// never leaves the dead zone therefore emits only the zero vector and
// still counts as a tap on release. Once live, a stick stays live until
// it falls back below DEAD_ZONE_ENTER (0.24): the gap between the two is
// hysteresis, so hovering near the edge does not flicker.
const DEAD_ZONE_EXIT = 0.34;
const DEAD_ZONE_ENTER = 0.24;
// Tap vs hold on the right stick's centre: a release under TAP_MAX_MS is
// a tap (interact); still touching at HOLD_MS is a hold (Quick Equip) and
// the tap is suppressed. Releases in between do neither -- a deliberate
// ambiguity guard so the two inputs can never both fire.
const TAP_MAX_MS = 300;
const HOLD_MS = 450;

function useStickVector(
  onVector: (x: number, y: number) => void,
  onTap?: () => void,
  onDeadZone?: () => void,
  onHold?: () => void,
) {
  const baseRef = useRef<HTMLDivElement>(null);
  const [knob, setKnob] = useState({ x: 0, y: 0, active: false });
  const drag = useRef<{
    id: number;
    startAt: number;
    live: boolean; // pushed past the dead-zone exit at least once
    exceeded: boolean; // same latch as live, kept for the tap decision
    holdFired: boolean;
    holdTimer: ReturnType<typeof setTimeout> | null;
  } | null>(null);

  const vector = (event: React.PointerEvent) => {
    const base = baseRef.current;
    if (!base) return { x: 0, y: 0, px: 0, py: 0 };
    const rect = base.getBoundingClientRect();
    const radius = Math.max(1, rect.width * RADIUS_RATIO);
    let dx = event.clientX - (rect.left + rect.width / 2);
    let dy = event.clientY - (rect.top + rect.height / 2);
    const len = Math.hypot(dx, dy);
    if (len > radius) {
      dx = (dx / len) * radius;
      dy = (dy / len) * radius;
    }
    return { x: dx / radius, y: dy / radius, px: dx, py: dy };
  };

  const onDown = (event: React.PointerEvent) => {
    (event.target as HTMLElement).setPointerCapture(event.pointerId);
    const current: NonNullable<typeof drag.current> = {
      id: event.pointerId,
      startAt: performance.now(),
      live: false,
      exceeded: false,
      holdFired: false,
      holdTimer: null,
    };
    drag.current = current;
    if (onHold) {
      current.holdTimer = setTimeout(() => {
        if (drag.current === current && !current.exceeded) {
          current.holdFired = true;
          onHold();
        }
      }, HOLD_MS);
    }
    const v = vector(event);
    setKnob({ x: v.px, y: v.py, active: true });
  };

  const onMove = (event: React.PointerEvent) => {
    if (!drag.current || event.pointerId !== drag.current.id) return;
    const v = vector(event);
    setKnob({ x: v.px, y: v.py, active: true });
    const mag = Math.hypot(v.x, v.y);
    if (!drag.current.live) {
      if (mag > DEAD_ZONE_EXIT) {
        drag.current.live = true;
        drag.current.exceeded = true;
        if (drag.current.holdTimer) {
          clearTimeout(drag.current.holdTimer);
          drag.current.holdTimer = null;
        }
        onVector(v.x, v.y);
      } else if (mag <= DEAD_ZONE_ENTER) {
        // Held inside the dead zone: emit only the zero/dead signal, so a
        // jittery centre touch can neither move Grace nor start an aim.
        onDeadZone?.();
      }
      // Between ENTER and EXIT while not live: hysteresis gap, say nothing.
      return;
    }
    if (mag < DEAD_ZONE_ENTER) {
      // Fell back into the dead zone after being live: signal dead (for
      // the aim stick this is what makes the eventual release cancel
      // instead of throw), but keep `exceeded` latched -- no tap.
      drag.current.live = false;
      onDeadZone?.();
      return;
    }
    onVector(v.x, v.y);
  };

  const onUp = (event: React.PointerEvent) => {
    if (!drag.current || event.pointerId !== drag.current.id) return;
    if (drag.current.holdTimer) {
      clearTimeout(drag.current.holdTimer);
    }
    const quick = performance.now() - drag.current.startAt < TAP_MAX_MS;
    const wasTap = quick && !drag.current.exceeded && !drag.current.holdFired;
    drag.current = null;
    setKnob({ x: 0, y: 0, active: false });
    onVector(0, 0); // release: stops her (move) / throws, cancels, or equips (right, in C/React)
    if (wasTap && onTap) onTap();
  };

  return { baseRef, knob, onDown, onMove, onUp };
}

export function Stick() {
  // Move stick only: its centre tap is NOT interact any more (that moved
  // to the right stick). Dead zone here is a true zero vector. While a
  // menu is open the stick is captured by it: a flick (out past the dead
  // zone and back) steps the menu selection once, and no movement ever
  // reaches Grace behind the menu.
  const flickArmed = useRef(true);
  const onMoveVector = (x: number, y: number) => {
    if (!menuCapturing()) {
      stickInput(x, y);
      return;
    }
    if (x === 0 && y === 0) {
      return;
    }
    if (!flickArmed.current) {
      return;
    }
    flickArmed.current = false;
    if (Math.abs(x) > Math.abs(y)) {
      dispatchMenuStep({ dx: Math.sign(x), dy: 0 });
    } else {
      dispatchMenuStep({ dx: 0, dy: Math.sign(y) });
    }
  };
  const onMoveDeadZone = () => {
    flickArmed.current = true;
    if (!menuCapturing()) {
      stickInput(0, 0);
    }
  };
  // Menu shortcuts on this stick's centre (which otherwise does
  // nothing): HOLDING it toggles the Pause menu -- the mirror of
  // holding the right stick's centre for Quick Equip -- and a
  // DOUBLE-TAP toggles the Inventory. Both stay inside the dead zone,
  // so they can never move Grace, aim, interact or equip.
  const lastTapAt = useRef<number | null>(null);
  const onMoveTap = () => {
    const now = performance.now();
    if (lastTapAt.current !== null && now - lastTapAt.current < 400) {
      lastTapAt.current = null;
      dispatchMenuShortcut("inventory");
    } else {
      lastTapAt.current = now;
    }
  };
  const onMoveHold = () => {
    dispatchMenuShortcut("pause");
  };
  const { baseRef, knob, onDown, onMove, onUp } = useStickVector(
    onMoveVector,
    onMoveTap,
    onMoveDeadZone,
    onMoveHold,
  );
  return (
    <div
      ref={baseRef}
      className="stick"
      data-active={knob.active || undefined}
      role="application"
      aria-label="Move stick. Hold centre for the Pause menu, double-tap centre for the Inventory."
      onPointerDown={onDown}
      onPointerMove={onMove}
      onPointerUp={onUp}
      onPointerCancel={onUp}
      onContextMenu={(event) => event.preventDefault()}
    >
      <div
        className="stick-knob"
        style={{ transform: `translate(calc(-50% + ${knob.x}px), calc(-50% + ${knob.y}px))` }}
      />
    </div>
  );
}

export function AimStick() {
  const [state, setState] = useState({ bottleActive: false, quickOpen: false });
  // Mirrors for the touch callbacks, which must not read stale state
  // mid-drag (the menu can open from this very touch's hold).
  const quickOpenRef = useRef(false);
  const equipDir = useRef({ x: 0, y: 0 });
  const equipExceeded = useRef(false);

  useEffect(() => {
    const sync = () => {
      const grace = readGrace();
      quickOpenRef.current = grace.quickOpen;
      setState((prev) =>
        prev.bottleActive === (grace.bottleActive || grace.weaponActive) &&
        prev.quickOpen === grace.quickOpen
          ? prev
          : { bottleActive: grace.bottleActive || grace.weaponActive, quickOpen: grace.quickOpen },
      );
    };
    sync();
    const id = setInterval(sync, 150);
    return () => clearInterval(id);
  }, []);

  const closeEquip = () => {
    quickToggle();
    quickOpenRef.current = false;
    equipDir.current = { x: 0, y: 0 };
    equipExceeded.current = false;
    setRadialDirection(0, 0);
  };

  // While Quick Equip is open the right stick points at radial slots and
  // aiming is suspended: no aim vector ever reaches C, so no throw can
  // fire. Releasing beyond the dead zone equips the pointed slot (an
  // empty slot keeps the menu open); releasing back in the dead zone
  // after pointing cancels and closes; a plain centre tap closes too.
  const onAimVector = (x: number, y: number) => {
    if (menuCapturing()) {
      return; // a menu owns the sticks: no aiming behind it
    }
    if (!quickOpenRef.current) {
      aimInput(x, y);
      return;
    }
    if (x === 0 && y === 0) {
      // release
      const slot = radialSlot(equipDir.current.x, equipDir.current.y);
      const wasPointing = equipExceeded.current;
      equipDir.current = { x: 0, y: 0 };
      equipExceeded.current = false;
      setRadialDirection(0, 0);
      if (slot >= 0) {
        quickSelect(slot);
        quickOpenRef.current = readGrace().quickOpen; // bottle closes it, Lighter stays open
      } else if (wasPointing) {
        closeEquip(); // pointed, then pulled back to centre: cancel
      }
      return;
    }
    equipExceeded.current = true;
    equipDir.current = { x, y };
    setRadialDirection(x, y);
  };

  const onAimDeadZone = () => {
    if (menuCapturing()) {
      return;
    }
    if (quickOpenRef.current) {
      equipDir.current = { x: 0, y: 0 };
      setRadialDirection(0, 0);
      return;
    }
    aimDeadZone();
  };

  const onAimTap = () => {
    if (menuCapturing()) {
      dispatchMenuAccept(); // right-stick centre tap accepts in a menu
      return;
    }
    if (quickOpenRef.current) {
      closeEquip(); // a centre tap while the menu is open cancels it
      return;
    }
    interactTrigger();
  };

  const onAimHold = () => {
    if (menuCapturing()) {
      return; // no Quick Equip from inside another menu
    }
    if (quickOpenRef.current) {
      return;
    }
    if (quickToggle()) {
      quickOpenRef.current = true;
    }
  };

  const { baseRef, knob, onDown, onMove, onUp } = useStickVector(
    onAimVector,
    onAimTap,
    onAimDeadZone,
    onAimHold,
  );
  return (
    <div
      ref={baseRef}
      className="stick aim-stick"
      data-active={knob.active || undefined}
      data-inactive={!state.bottleActive || undefined}
      data-equip-open={state.quickOpen || undefined}
      role="application"
      aria-label={
        state.bottleActive
          ? "Aim stick. Push to aim, release to throw or fire. Tap centre to interact, hold centre for Quick Equip."
          : "Interact stick. Tap centre to interact, hold centre for Quick Equip. No bottle or weapon active, so aiming is unavailable."
      }
      onPointerDown={onDown}
      onPointerMove={onMove}
      onPointerUp={onUp}
      onPointerCancel={onUp}
      onContextMenu={(event) => event.preventDefault()}
    >
      <div
        className="stick-knob"
        style={{ transform: `translate(calc(-50% + ${knob.x}px), calc(-50% + ${knob.y}px))` }}
      />
    </div>
  );
}
