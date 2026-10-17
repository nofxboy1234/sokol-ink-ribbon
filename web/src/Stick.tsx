import { useRef, useState } from "react";
import { aimToggle, interactTrigger, stickInput } from "./wasmBridge";

// The analogue stick at the bottom centre of the map (touch only, see
// app.css). Push it to move: the first half of its throw walks, further
// runs -- the C side turns the vector into a speed. A quick tap on the
// centre is the interact trigger (pick up / climb / use); a double-tap
// toggles throw aiming, and releasing the stick while aiming throws.
const RADIUS = 52; // px of stick travel

export function Stick() {
  const baseRef = useRef<HTMLDivElement>(null);
  const [knob, setKnob] = useState({ x: 0, y: 0, active: false });
  const drag = useRef<{ id: number; startAt: number; moved: boolean } | null>(null);
  const taps = useRef<{ at: number; timer: number | null }>({ at: 0, timer: null });

  const vector = (event: React.PointerEvent) => {
    const base = baseRef.current;
    if (!base) return { x: 0, y: 0 };
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
    setKnob({ x: v.px ?? 0, y: v.py ?? 0, active: true });
  };

  const onMove = (event: React.PointerEvent) => {
    if (!drag.current || event.pointerId !== drag.current.id) return;
    const v = vector(event);
    if (Math.hypot(v.x, v.y) > 0.12) drag.current.moved = true;
    setKnob({ x: v.px ?? 0, y: v.py ?? 0, active: true });
    stickInput(v.x, v.y);
  };

  const onUp = (event: React.PointerEvent) => {
    if (!drag.current || event.pointerId !== drag.current.id) return;
    const quick = performance.now() - drag.current.startAt < 300;
    const wasTap = quick && !drag.current.moved;
    drag.current = null;
    setKnob({ x: 0, y: 0, active: false });
    stickInput(0, 0); // release: stops her; while aiming, this throws/cancels in C
    if (!wasTap) return;
    // A centre tap is interact; a second tap inside the window aims instead.
    const now = performance.now();
    if (now - taps.current.at < 350) {
      if (taps.current.timer) window.clearTimeout(taps.current.timer);
      taps.current = { at: 0, timer: null };
      aimToggle();
    } else {
      taps.current.at = now;
      taps.current.timer = window.setTimeout(() => {
        taps.current = { at: 0, timer: null };
        interactTrigger();
      }, 340);
    }
  };

  return (
    <div
      ref={baseRef}
      className="stick"
      data-active={knob.active || undefined}
      role="application"
      aria-label="Move stick. Tap centre to interact, double-tap to aim."
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
