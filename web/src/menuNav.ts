import { useEffect, useRef, type RefObject } from "react";

// Unified menu navigation for every React menu (Pause, Save/Load, Game
// Over, Level Complete, Inventory overlay, code keypad).
//
// Desktop: Up/Down move the selection (Left/Right too, stepping by the
// row width in grids), Enter/Space accepts, and the selected item gets
// data-menu-selected="true" (green in app.css) so it is visible without
// a mouse. Mouse/touch clicks keep working as before -- selection is a
// highlight layered on top, not a focus steal (no .focus() calls, so a
// real keyboard Enter can't double-fire a button).
//
// Mobile: while any menu is mounted it "captures" the sticks (see
// menuCapturing below). The move stick then flicks through the menu --
// one step per excursion past the dead zone, re-armed on return, so
// holding it cannot machine-gun -- and a right-stick centre tap accepts
// instead of interacting. The stick components check menuCapturing()
// and route here instead of sending movement/aim to the game, which
// also guarantees Grace cannot move behind an open menu.

let captureCount = 0;
export function menuCapturing(): boolean {
  return captureCount > 0;
}

export type MenuStep = { dx: number; dy: number };

export function dispatchMenuStep(step: MenuStep): void {
  window.dispatchEvent(new CustomEvent<MenuStep>("menu-step", { detail: step }));
}
export function dispatchMenuAccept(): void {
  window.dispatchEvent(new CustomEvent("menu-accept"));
}

// Opening menus without a pointer: the app shell listens for these and
// runs the same toggles as the P / I keys. The move stick dispatches
// them (hold its centre = pause, double-tap its centre = inventory).
export type MenuShortcut = "pause" | "inventory";
export function dispatchMenuShortcut(shortcut: MenuShortcut): void {
  window.dispatchEvent(new CustomEvent<MenuShortcut>("menu-shortcut", { detail: shortcut }));
}
export function onMenuShortcut(handler: (shortcut: MenuShortcut) => void): () => void {
  const listener = (event: Event) => handler((event as CustomEvent<MenuShortcut>).detail);
  window.addEventListener("menu-shortcut", listener);
  return () => window.removeEventListener("menu-shortcut", listener);
}

const STEP_EVENT = "menu-step";
const ACCEPT_EVENT = "menu-accept";

function menuItems(container: HTMLElement): HTMLElement[] {
  return Array.from(container.querySelectorAll<HTMLElement>("[data-menu-item]")).filter(
    (el) => el.offsetParent !== null && !(el as HTMLButtonElement).disabled,
  );
}

function columnsOf(items: HTMLElement[]): number {
  if (items.length === 0) {
    return 1;
  }
  const top = items[0].offsetTop;
  return Math.max(
    1,
    items.filter((el) => el.offsetTop === top).length,
  );
}

export function useMenuNav(
  containerRef: RefObject<HTMLElement | null>,
  active: boolean,
  onEscape?: () => void,
): void {
  const selectedRef = useRef(0);
  const escapeRef = useRef(onEscape);
  escapeRef.current = onEscape;

  useEffect(() => {
    if (!active) {
      return;
    }
    const container = containerRef.current;
    if (!container) {
      return;
    }
    captureCount++;
    selectedRef.current = 0;

    const paint = () => {
      const items = menuItems(container);
      if (selectedRef.current >= items.length) {
        selectedRef.current = Math.max(0, items.length - 1);
      }
      items.forEach((el, i) => {
        if (i === selectedRef.current) {
          if (el.getAttribute("data-menu-selected") !== "true") {
            el.setAttribute("data-menu-selected", "true");
          }
        } else if (el.hasAttribute("data-menu-selected")) {
          el.removeAttribute("data-menu-selected");
        }
      });
      return items;
    };
    paint();
    // Menus change shape (slots fill, tabs swap content); keep the paint
    // and the clamp fresh whenever the DOM inside does.
    const observer = new MutationObserver(() => paint());
    observer.observe(container, { childList: true, subtree: true });

    const move = (dx: number, dy: number) => {
      const items = paint();
      if (items.length === 0) {
        return;
      }
      const cols = columnsOf(items);
      let delta = 0;
      if (dy !== 0) {
        delta = dy * cols;
      } else if (dx !== 0) {
        delta = dx;
      }
      if (delta === 0) {
        return;
      }
      selectedRef.current =
        (selectedRef.current + delta + items.length) % items.length;
      paint();
    };

    const accept = () => {
      const items = paint();
      items[selectedRef.current]?.click();
    };

    const onKey = (event: KeyboardEvent) => {
      // Typing in a form field (auth, code display) is not navigation.
      const target = event.target as HTMLElement | null;
      if (
        target &&
        (target.tagName === "INPUT" || target.tagName === "TEXTAREA" || target.tagName === "SELECT")
      ) {
        return;
      }
      switch (event.key) {
        case "ArrowDown":
          event.preventDefault();
          event.stopPropagation();
          move(0, 1);
          break;
        case "ArrowUp":
          event.preventDefault();
          event.stopPropagation();
          move(0, -1);
          break;
        case "ArrowRight":
          event.preventDefault();
          event.stopPropagation();
          move(1, 0);
          break;
        case "ArrowLeft":
          event.preventDefault();
          event.stopPropagation();
          move(-1, 0);
          break;
        case "Enter":
        case " ":
          event.preventDefault();
          event.stopPropagation();
          accept();
          break;
        case "Escape":
          if (escapeRef.current) {
            event.preventDefault();
            escapeRef.current();
          }
          break;
        default:
          break;
      }
    };
    const onStep = (event: Event) => {
      const step = (event as CustomEvent<MenuStep>).detail;
      move(step.dx, step.dy);
    };
    const onAccept = () => accept();

    window.addEventListener("keydown", onKey, true);
    window.addEventListener(STEP_EVENT, onStep);
    window.addEventListener(ACCEPT_EVENT, onAccept);
    return () => {
      captureCount--;
      observer.disconnect();
      window.removeEventListener("keydown", onKey, true);
      window.removeEventListener(STEP_EVENT, onStep);
      window.removeEventListener(ACCEPT_EVENT, onAccept);
      container
        .querySelectorAll("[data-menu-selected]")
        .forEach((el) => el.removeAttribute("data-menu-selected"));
    };
  }, [active, containerRef]);
}
