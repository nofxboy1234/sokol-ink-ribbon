import {
  useEffect,
  useMemo,
  useRef,
  useState,
  type MouseEvent,
  type PointerEvent,
} from "react";
import {
  SAVE_SLOTS,
  clearInventorySlot,
  craftRecipe,
  gameReset,
  itemAppearance,
  itemCount,
  loadGame,
  moveInventorySlot,
  readFiles,
  readRecipes,
  readReplay,
  recipeReady,
  runElapsedMs,
  saveGame,
  saveTimeMs,
  saveUsed,
  setLighter,
  type ItemAppearance,
} from "./wasmBridge";
import { useElapsed, useGameState } from "./GameState";

export function formatMs(ms: number): string {
  const total = Math.max(0, Math.floor(ms / 1000));
  const minutes = Math.floor(total / 60);
  const seconds = total % 60;
  return `${minutes}:${String(seconds).padStart(2, "0")}`;
}

// The map page unmounts on the way to Records, but the wasm game stays
// alive, so this has to live outside RunPanel or each visit posts again.
let goalRunSaved = false;

export function RunPanel() {
  const elapsed = useElapsed();
  const { goalMet } = useGameState();
  const [status, setStatus] = useState("");
  const count = itemCount();
  if (count > 0 && (ITEM_TEXT.length !== count || ITEM_ACTIONS.length !== count)) {
    throw new Error(
      `item copy length text ${ITEM_TEXT.length} actions ${ITEM_ACTIONS.length} catalog ${count}`,
    );
  }

  // A run saves itself the moment the goal (the fuse box) is activated.
  useEffect(() => {
    if (!goalMet) {
      goalRunSaved = false;
      return;
    }
    if (goalRunSaved) {
      return;
    }
    goalRunSaved = true;
    const replay = readReplay();
    void (async () => {
      try {
        const response = await fetch("/runs", {
          method: "POST",
          headers: { "content-type": "application/json" },
          body: JSON.stringify({
            levelId: "care-center-01",
            durationMs: Math.round(runElapsedMs()),
            completed: true,
            replay,
          }),
        });
        setStatus(
          response.ok ? "Run saved" : response.status === 401 ? "Sign in to save" : "Save failed",
        );
      } catch {
        setStatus("Save failed");
      }
    })();
  }, [goalMet]);

  return (
    <div className="run-panel">
      <span className="timer">{formatMs(elapsed)}</span>
      {status && <span className="run-status">{status}</span>}
    </div>
  );
}

const HEALTH_LABELS = ["FINE", "CAUTION", "DANGER"];
const HEALTH_COLORS = ["var(--green)", "var(--orange)", "var(--red)"];

const ITEM_TEXT = [
  "A clear glass bottle. It could hold a mixed herb.",
  "A worn coin stamped with a crest. It has no spendable value here.",
  "A fresh green herb. It can be ground, or used to steady Grace.",
  "A typewriter ribbon. Saving at a typewriter spends one.",
  "A small flat-head screwdriver. It pries panels and tightens fittings.",
  "A prepared dose in a glass syringe. It is meant for an emergency.",
  "A glass cartridge fuse. The basement fuse box will take it.",
  "A pocket lighter. It throws a small circle of light when it is lit.",
  "A key topped with a small cherub. It opens a matching lock.",
  "This is a combination of 2 Green Herbs, and will restore a medium amount of health when used.",
];

type ItemAction = "Move" | "Use" | "Discard";

const ITEM_ACTIONS: ItemAction[][] = [
  ["Move", "Discard"],
  ["Move", "Discard"],
  ["Move", "Use", "Discard"],
  ["Move", "Discard"],
  ["Move"],
  ["Move", "Use", "Discard"],
  ["Move"],
  [],
  ["Move"],
  ["Move", "Discard"],
];

const STEROID_CHARGES = 0;
const STABILIZER_CHARGES = 0;

function ItemGlyph({ appearance }: { appearance: ItemAppearance }) {
  const { color, shape } = appearance;
  if (shape === 1) {
    return <span className="glyph glyph-square" style={{ background: color }} />;
  }
  if (shape === 2) {
    return <span className="glyph glyph-ring" style={{ borderColor: color }} />;
  }
  if (shape === 3) {
    return (
      <span className="glyph glyph-ink" style={{ background: color }}>
        <span className="glyph-hole" />
      </span>
    );
  }
  return <span className="glyph glyph-circle" style={{ background: color }} />;
}

function ChargePips({
  label,
  icon,
  filled,
  fill,
}: {
  label: string;
  icon: string;
  filled: number;
  fill: string;
}) {
  return (
    <div className="pip-group" title={label}>
      <span className="pip" style={{ background: icon }} />
      {Array.from({ length: 4 }, (_, index) => (
        <span
          key={index}
          className="pip"
          style={{ background: index < filled ? fill : undefined }}
        />
      ))}
    </div>
  );
}

export function Inventory() {
  const { inventory, health, hasLighter, lighterOn } = useGameState();
  const [selected, setSelected] = useState(-1);
  const [movingFrom, setMovingFrom] = useState(-1);
  const [menu, setMenu] = useState<{ index: number; x: number; y: number } | null>(null);
  const menuRef = useRef<HTMLDivElement>(null);
  // Touch has no right click, so a long press on a slot opens the menu.
  const press = useRef<{ timer: number; x: number; y: number; fired: boolean } | null>(null);

  useEffect(() => {
    if (!menu) {
      return;
    }
    const onDown = (event: globalThis.PointerEvent) => {
      if (menuRef.current?.contains(event.target as Node)) {
        return;
      }
      setMenu(null);
    };
    window.addEventListener("pointerdown", onDown);
    return () => window.removeEventListener("pointerdown", onDown);
  }, [menu]);

  useEffect(() => () => window.clearTimeout(press.current?.timer), []);

  useEffect(() => {
    if (!menu && movingFrom < 0) {
      return;
    }
    const onKey = (event: KeyboardEvent) => {
      if (event.key !== "Escape") {
        return;
      }
      event.stopImmediatePropagation();
      setMenu(null);
      setMovingFrom(-1);
    };
    window.addEventListener("keydown", onKey, true);
    return () => window.removeEventListener("keydown", onKey, true);
  }, [menu, movingFrom]);

  const selectedItem = selected >= 0 ? inventory[selected] : -1;
  const selectedLook = selectedItem >= 0 ? itemAppearance(selectedItem) : null;
  const menuItem = menu ? inventory[menu.index] : -1;
  const menuActions = menuItem >= 0 ? (ITEM_ACTIONS[menuItem] ?? []) : [];

  const onSlotClick = (index: number) => {
    if (press.current?.fired) {
      press.current = null;
      return;
    }
    if (movingFrom >= 0) {
      if (movingFrom !== index) {
        moveInventorySlot(movingFrom, index);
        setSelected(index);
      }
      setMovingFrom(-1);
      return;
    }
    setSelected((inventory[index] ?? -1) >= 0 ? index : -1);
  };

  const openMenu = (index: number, x: number, y: number) => {
    if (movingFrom >= 0) {
      setMovingFrom(-1);
      return;
    }
    const item = inventory[index] ?? -1;
    if (item < 0 || (ITEM_ACTIONS[item] ?? []).length === 0) {
      setMenu(null);
      return;
    }
    setSelected(index);
    setMenu({
      index,
      x: Math.min(x, window.innerWidth - 120),
      y: Math.min(y, window.innerHeight - 110),
    });
  };

  const onSlotMenu = (event: MouseEvent, index: number) => {
    event.preventDefault();
    if (press.current) {
      // Android also reports a long press as contextmenu; open it only once.
      window.clearTimeout(press.current.timer);
      if (press.current.fired) {
        return;
      }
      press.current.fired = true;
    }
    openMenu(index, event.clientX, event.clientY);
  };

  const onSlotPointerDown = (event: PointerEvent, index: number) => {
    window.clearTimeout(press.current?.timer);
    press.current = null;
    if (event.pointerType === "mouse") {
      return;
    }
    const x = event.clientX;
    const y = event.clientY;
    const timer = window.setTimeout(() => {
      if (press.current) {
        press.current.fired = true;
      }
      openMenu(index, x, y);
    }, 500);
    press.current = { timer, x, y, fired: false };
  };

  const onSlotPointerMove = (event: PointerEvent) => {
    const p = press.current;
    if (!p || p.fired) {
      return;
    }
    if (Math.hypot(event.clientX - p.x, event.clientY - p.y) > 10) {
      window.clearTimeout(p.timer);
      press.current = null;
    }
  };

  const onSlotPointerEnd = () => {
    const p = press.current;
    if (!p) {
      return;
    }
    window.clearTimeout(p.timer);
    if (!p.fired) {
      press.current = null;
    }
  };

  const runAction = (action: ItemAction) => {
    if (!menu) {
      return;
    }
    const index = menu.index;
    setMenu(null);
    if (action === "Move") {
      setMovingFrom(index);
      return;
    }
    if (action === "Discard") {
      clearInventorySlot(index);
      if (selected === index) {
        setSelected(-1);
      }
    }
  };

  return (
    <div className="pane-content">
      <h2>ITEMS</h2>
      <div className="inventory-grid">
        {inventory.map((item, index) => {
          const look = item >= 0 ? itemAppearance(item) : null;
          return (
            <button
              key={index}
              type="button"
              className="inventory-slot"
              data-filled={item >= 0}
              data-selected={selected === index}
              data-moving={movingFrom === index}
              title={look ? look.label : ""}
              onClick={() => onSlotClick(index)}
              onContextMenu={(event) => onSlotMenu(event, index)}
              onPointerDown={(event) => onSlotPointerDown(event, index)}
              onPointerMove={onSlotPointerMove}
              onPointerUp={onSlotPointerEnd}
              onPointerCancel={onSlotPointerEnd}
            >
              {look && <ItemGlyph appearance={look} />}
              {look && <span className="inventory-name">{look.label}</span>}
            </button>
          );
        })}
      </div>
      {movingFrom >= 0 && <p className="move-hint">Choose a slot</p>}
      <div className="item-detail">
        {selectedLook && (
          <>
            <h3>{selectedLook.label}</h3>
            <p>{ITEM_TEXT[selectedItem]}</p>
          </>
        )}
      </div>
      <div className="equip-row">
        <div className="gun-slot" title="No gun equipped" />
        <div className="quick-slots">
          <div className="quick-slot" />
          {hasLighter ? (
            <button
              type="button"
              className="quick-slot quick-slot-btn"
              data-on={lighterOn}
              title={lighterOn ? "Lighter on" : "Lighter"}
              onClick={() => setLighter(!lighterOn)}
            >
              <span className="lighter-swatch" />
            </button>
          ) : (
            <div className="quick-slot" />
          )}
        </div>
        <div className="status-stack">
          <div className="pip-row">
            <ChargePips
              label="Steroids Injector. Increases health."
              icon="#f2ff00"
              filled={STEROID_CHARGES}
              fill="#f2ff00"
            />
            <ChargePips
              label="Stabilizer Injector. Increases gun damage and handling."
              icon="#66d5f4"
              filled={STABILIZER_CHARGES}
              fill="#66d5f4"
            />
          </div>
          <div
            className="status-color"
            style={{ background: HEALTH_COLORS[health] ?? HEALTH_COLORS[0] }}
          />
          <div className="status-label">{HEALTH_LABELS[health] ?? HEALTH_LABELS[0]}</div>
        </div>
      </div>
      {movingFrom >= 0 && (
        <button type="button" className="move-catch" onClick={() => setMovingFrom(-1)} />
      )}
      {menu && menuActions.length > 0 && (
        <div ref={menuRef} className="item-menu" style={{ left: menu.x, top: menu.y }}>
          {menuActions.map((action) => (
            <button key={action} type="button" onClick={() => runAction(action)}>
              {action}
            </button>
          ))}
        </div>
      )}
    </div>
  );
}

const HOLD_MS = 1000;

type Hold =
  | { kind: "idle" }
  | { kind: "filling"; index: number; pointerId: number; startedAt: number }
  | { kind: "committed"; index: number; pointerId: number };

type HoldBind = {
  onPointerDown: (event: PointerEvent<HTMLButtonElement>) => void;
  onPointerUp: (event: PointerEvent<HTMLButtonElement>) => void;
  onPointerCancel: (event: PointerEvent<HTMLButtonElement>) => void;
  onContextMenu: (event: MouseEvent<HTMLButtonElement>) => void;
};

function useCraftHold(onCommit: (index: number) => boolean): {
  bind: (index: number, ready: boolean) => HoldBind;
  fill: (index: number) => number;
} {
  const onCommitRef = useRef(onCommit);
  onCommitRef.current = onCommit;
  const [hold, setHold] = useState<Hold>({ kind: "idle" });
  const holdRef = useRef(hold);
  holdRef.current = hold;
  const [fillRatio, setFillRatio] = useState(0);
  const frameRef = useRef(0);

  useEffect(() => {
    if (hold.kind !== "filling") {
      return;
    }
    const startedAt = hold.startedAt;
    const index = hold.index;
    const pointerId = hold.pointerId;
    const tick = (now: number) => {
      const current = holdRef.current;
      if (current.kind !== "filling" || current.startedAt !== startedAt) {
        return;
      }
      const elapsed = now - startedAt;
      if (elapsed >= HOLD_MS) {
        holdRef.current = { kind: "committed", index, pointerId };
        const ok = onCommitRef.current(index);
        if (!ok) {
          holdRef.current = { kind: "idle" };
          setFillRatio(0);
          setHold({ kind: "idle" });
          return;
        }
        setFillRatio(1);
        setHold(holdRef.current);
        return;
      }
      setFillRatio(Math.min(1, Math.max(0, elapsed / HOLD_MS)));
      frameRef.current = requestAnimationFrame(tick);
    };
    frameRef.current = requestAnimationFrame(tick);
    return () => cancelAnimationFrame(frameRef.current);
  }, [hold]);

  const endHold = (event: PointerEvent<HTMLButtonElement>, index: number) => {
    const current = holdRef.current;
    if (current.kind === "idle" || current.index !== index || current.pointerId !== event.pointerId) {
      return;
    }
    cancelAnimationFrame(frameRef.current);
    holdRef.current = { kind: "idle" };
    setFillRatio(0);
    setHold({ kind: "idle" });
  };

  return {
    bind: (index, ready) => ({
      onPointerDown: (event) => {
        if (!ready || holdRef.current.kind !== "idle") {
          return;
        }
        if (event.pointerType === "mouse" && event.button !== 0) {
          return;
        }
        event.preventDefault();
        event.currentTarget.setPointerCapture(event.pointerId);
        const next: Hold = {
          kind: "filling",
          index,
          pointerId: event.pointerId,
          startedAt: event.timeStamp,
        };
        holdRef.current = next;
        setFillRatio(0);
        setHold(next);
      },
      onPointerUp: (event) => endHold(event, index),
      onPointerCancel: (event) => endHold(event, index),
      onContextMenu: (event) => {
        event.preventDefault();
      },
    }),
    fill: (index) => {
      if (hold.kind === "idle" || hold.index !== index) {
        return 0;
      }
      if (hold.kind === "committed") {
        return 1;
      }
      return fillRatio;
    },
  };
}

export function Crafting() {
  const { revision } = useGameState();
  const recipes = useMemo(() => readRecipes(), [revision]);
  const hold = useCraftHold((index) => craftRecipe(index));

  return (
    <div className="pane-content">
      <h2>CRAFTING</h2>
      <ul className="recipe-list">
        {recipes.map((recipe, index) => {
          const ready = recipeReady(index);
          return (
            <li key={index}>
              <button
                type="button"
                className="recipe"
                aria-disabled={!ready}
                {...hold.bind(index, ready)}
              >
                <span
                  className="recipe-fill"
                  style={{
                    transform: `scaleX(${hold.fill(index)})`,
                    visibility: ready && hold.fill(index) > 0 ? "visible" : "hidden",
                  }}
                />
                {recipe.inputs.map((part, partIndex) => (
                  <span key={part.item} className="recipe-part">
                    {partIndex > 0 && <span className="recipe-op">+</span>}
                    <span>
                      {part.label} × {part.qty}
                    </span>
                  </span>
                ))}
                <span className="recipe-op">→</span>
                <span className="recipe-output">
                  {recipe.output.label} × {recipe.output.qty}
                </span>
              </button>
            </li>
          );
        })}
      </ul>
    </div>
  );
}

export function Files() {
  const { filesFound } = useGameState();
  const files = useMemo(() => readFiles(), [filesFound]);
  return (
    <div className="pane-content">
      <h2>FILES</h2>
      <ul className="file-list">
        {files.length === 0 && <li className="file file-empty">NO DATA</li>}
        {files.map((file) => (
          <li key={file.name} className="file">
            <span className="file-name">{file.name}</span>
            <span className="file-code">= {file.code}</span>
          </li>
        ))}
      </ul>
    </div>
  );
}

export function PauseMenu({
  open,
  onClose,
  onLoad,
}: {
  open: boolean;
  onClose: () => void;
  onLoad: () => void;
}) {
  if (!open) {
    return null;
  }
  return (
    <div className="pause-overlay" onClick={onClose}>
      <div className="pause-menu" onClick={(event) => event.stopPropagation()}>
        <button
          type="button"
          className="pause-item pause-selected"
          onClick={() => {
            gameReset();
            onClose();
          }}
        >
          Restart
        </button>
        <button type="button" className="pause-item" onClick={onLoad}>
          Load Game
        </button>
        <button type="button" className="pause-item" onClick={onClose}>
          Resume
        </button>
      </div>
    </div>
  );
}

// Save/Load menu in the same style as the pause menu (ref/map.kra "Load menu").
export function SaveLoadMenu({ open, onClose }: { open: boolean; onClose: () => void }) {
  if (!open) {
    return null;
  }
  // Remount fresh each time the menu opens. saveUsed/saveTimeMs read mutable
  // wasm state that React can't observe, so the React Compiler would otherwise
  // memoize the reads and show stale slot data after a save.
  return <SaveLoadSlots onClose={onClose} />;
}

type SaveSlotInfo = { index: number; used: boolean; time: number };

const INK_RIBBON = 3;

function SaveLoadSlots({ onClose }: { onClose: () => void }) {
  const { inventory } = useGameState();
  const canSave = inventory.includes(INK_RIBBON);
  const slots = useMemo<SaveSlotInfo[]>(
    () =>
      Array.from({ length: SAVE_SLOTS }, (_, index) => ({
        index,
        used: saveUsed(index),
        time: saveTimeMs(index),
      })),
    [],
  );
  return (
    <div className="pause-overlay" onClick={onClose}>
      <div className="save-menu" onClick={(event) => event.stopPropagation()}>
        <div className="save-title">Save / Load</div>
        {slots.map((slot) => (
          <div key={slot.index} className="save-row">
            <span className="save-slot">Slot {slot.index + 1}</span>
            <span className="save-data">{slot.used ? formatMs(slot.time) : "NO DATA"}</span>
            <button
              type="button"
              className="pause-item save-btn"
              disabled={!canSave}
              title={canSave ? "Uses one ink ribbon" : "Needs an ink ribbon"}
              onClick={() => {
                saveGame(slot.index);
                onClose();
              }}
            >
              Save
            </button>
            <button
              type="button"
              className="pause-item save-btn"
              disabled={!slot.used}
              onClick={() => {
                loadGame(slot.index);
                onClose();
              }}
            >
              Load
            </button>
          </div>
        ))}
        <button type="button" className="pause-item" onClick={onClose}>
          Close
        </button>
      </div>
    </div>
  );
}
