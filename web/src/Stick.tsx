import { useEffect, useRef, useState } from "react";
import { aimDeadZone, aimInput, interactTrigger, readGrace, stickInput } from "./wasmBridge";

// Twin sticks (touch only, see app.css). LEFT stick moves Grace: the first
// half of its throw walks, further runs -- the C side turns the vector
// into a speed. A quick tap on its centre is the interact trigger (pick up
// / climb / use); the old double-tap-to-aim gesture is retired now that
// aiming has its own stick. RIGHT stick aims the bottle throw (only shown
// while a bottle is active): direction sets the aim line, push distance
// sets the throw length, and releasing it throws -- or cancels if the
// release never left the dead zone. She can aim while moving; aiming caps
// her speed to a walk in C.
// Stick travel as a fraction of the stick's rendered size: 52px at the
// full 128px size (see --stick-size in app.css). The CSS shrinks the stick
// on short (landscape) screens, so travel -- and with it the walk/run
// bands and the aim length, which are all fractions of this radius -- is
// derived from the element's actual size, never a fixed pixel count.
const RADIUS_RATIO = 52 / 128;

// Dead zone, as fractions of the travel radius above. A touch only starts
// emitting a direction once it pushes past DEAD_ZONE_EXIT: 0.20 of travel
// is ~10px at the full 128px stick size (~8px shrunk in landscape), in
// line with typical 8-12px touch slop, and well inside the walk band
// (C walks up to half travel), so a deliberate small push still walks.
// A centre tap whose drift never leaves the dead zone therefore emits
// only the zero vector and still counts as a tap on release. Once live, a
// stick stays live until it falls back below DEAD_ZONE_ENTER (0.12, the
// same gate the C side uses): the gap between the two is hysteresis, so
// hovering near the edge does not flicker between dead and moving.
const DEAD_ZONE_EXIT = 0.2;
const DEAD_ZONE_ENTER = 0.12;
const TAP_MAX_MS = 300;

function useStickVector(
  onVector: (x: number, y: number) => void,
  onTap?: () => void,
  onDeadZone?: () => void,
) {
  const baseRef = useRef<HTMLDivElement>(null);
  const [knob, setKnob] = useState({ x: 0, y: 0, active: false });
  const drag = useRef<{
    id: number;
    startAt: number;
    live: boolean; // pushed past the dead-zone exit at least once
    exceeded: boolean; // same latch as live, kept for the tap decision
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
    drag.current = {
      id: event.pointerId,
      startAt: performance.now(),
      live: false,
      exceeded: false,
    };
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
    const quick = performance.now() - drag.current.startAt < TAP_MAX_MS;
    const wasTap = quick && !drag.current.exceeded;
    drag.current = null;
    setKnob({ x: 0, y: 0, active: false });
    onVector(0, 0); // release: stops her (move) / throws or cancels (aim, in C)
    if (wasTap && onTap) onTap();
  };

  return { baseRef, knob, onDown, onMove, onUp };
}

export function Stick() {
  // Dead zone on the move stick is a true zero vector: she does not move.
  const { baseRef, knob, onDown, onMove, onUp } = useStickVector(stickInput, interactTrigger, () =>
    stickInput(0, 0),
  );
  return (
    <div
      ref={baseRef}
      className="stick"
      data-active={knob.active || undefined}
      role="application"
      aria-label="Move stick. Tap centre to interact."
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
  const [bottleActive, setBottleActive] = useState(false);
  useEffect(() => {
    const id = setInterval(() => setBottleActive(readGrace().bottleActive), 250);
    return () => clearInterval(id);
  }, []);
  const { baseRef, knob, onDown, onMove, onUp } = useStickVector(aimInput, undefined, aimDeadZone);
  if (!bottleActive) {
    return null;
  }
  return (
    <div
      ref={baseRef}
      className="stick aim-stick"
      data-active={knob.active || undefined}
      role="application"
      aria-label="Aim stick. Push to aim, release to throw."
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
