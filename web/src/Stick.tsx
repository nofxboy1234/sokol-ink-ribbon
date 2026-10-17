import { useEffect, useRef, useState } from "react";
import { aimInput, interactTrigger, readGrace, stickInput } from "./wasmBridge";

// Twin sticks (touch only, see app.css). LEFT stick moves Grace: the first
// half of its throw walks, further runs -- the C side turns the vector
// into a speed. A quick tap on its centre is the interact trigger (pick up
// / climb / use); the old double-tap-to-aim gesture is retired now that
// aiming has its own stick. RIGHT stick aims the bottle throw (only shown
// while a bottle is active): direction sets the aim line, push distance
// sets the throw length, and releasing it throws -- or cancels if the
// release never left the dead zone. She can aim while moving; aiming caps
// her speed to a walk in C.
const RADIUS = 52; // px of stick travel

function useStickVector(onVector: (x: number, y: number) => void, onTap?: () => void) {
  const baseRef = useRef<HTMLDivElement>(null);
  const [knob, setKnob] = useState({ x: 0, y: 0, active: false });
  const drag = useRef<{ id: number; startAt: number; moved: boolean } | null>(null);

  const vector = (event: React.PointerEvent) => {
    const base = baseRef.current;
    if (!base) return { x: 0, y: 0, px: 0, py: 0 };
    const rect = base.getBoundingClientRect();
    let dx = event.clientX - (rect.left + rect.width / 2);
    let dy = event.clientY - (rect.top + rect.height / 2);
    const len = Math.hypot(dx, dy);
    if (len > RADIUS) {
      dx = (dx / len) * RADIUS;
      dy = (dy / len) * RADIUS;
    }
    return { x: dx / RADIUS, y: dy / RADIUS, px: dx, py: dy };
  };

  const onDown = (event: React.PointerEvent) => {
    (event.target as HTMLElement).setPointerCapture(event.pointerId);
    drag.current = { id: event.pointerId, startAt: performance.now(), moved: false };
    const v = vector(event);
    setKnob({ x: v.px, y: v.py, active: true });
  };

  const onMove = (event: React.PointerEvent) => {
    if (!drag.current || event.pointerId !== drag.current.id) return;
    const v = vector(event);
    if (Math.hypot(v.x, v.y) > 0.12) drag.current.moved = true;
    setKnob({ x: v.px, y: v.py, active: true });
    onVector(v.x, v.y);
  };

  const onUp = (event: React.PointerEvent) => {
    if (!drag.current || event.pointerId !== drag.current.id) return;
    const quick = performance.now() - drag.current.startAt < 300;
    const wasTap = quick && !drag.current.moved;
    drag.current = null;
    setKnob({ x: 0, y: 0, active: false });
    onVector(0, 0); // release: stops her (move) / throws or cancels (aim, in C)
    if (wasTap && onTap) onTap();
  };

  return { baseRef, knob, onDown, onMove, onUp };
}

export function Stick() {
  const { baseRef, knob, onDown, onMove, onUp } = useStickVector(stickInput, interactTrigger);
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
  const { baseRef, knob, onDown, onMove, onUp } = useStickVector(aimInput);
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
