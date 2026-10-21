import { useEffect, useMemo, useRef, useState, type MouseEvent, type PointerEvent } from "react";
import {
  ITEM_BOTTLE,
  SAVE_SLOTS,
  applyInventoryItem,
  clearInventorySlot,
  craftRecipe,
  equipItem,
  gameReset,
  itemActionBits,
  itemAppearance,
  itemText,
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
  pendingSafe,
  itemboxStore,
  itemboxTake,
  readItembox,
  takeItembox,
  setPaused,
  pendingCodeDoor,
  trySafeCode,
  tryDoorCode,
  type ItemAppearance,
} from "./wasmBridge";
import { useElapsed, useGameState } from "./GameState";
import { useMenuNav } from "./menuNav";

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
  const { levelComplete } = useGameState();
  const [status, setStatus] = useState("");
  // A run saves itself when she walks past the powered gate to the goal.
  useEffect(() => {
    if (!levelComplete) {
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
  }, [levelComplete]);

  return (
    <div className="run-panel">
      <span className="timer">{formatMs(elapsed)}</span>
      {status && <span className="run-status">{status}</span>}
    </div>
  );
}

const HEALTH_LABELS = ["FINE", "CAUTION", "DANGER"];
const HEALTH_COLORS = ["var(--green)", "var(--orange)", "var(--red)"];

type ItemAction = "Move" | "Use" | "Equip" | "Unequip" | "Discard";

// Actions come from the C item catalog (web_item_actions): 1 Move, 2 Use,
// 4 Equip, 8 Discard. Equip flips to Unequip on the equipped item.
function actionsFor(item: number, equippedItem: number): ItemAction[] {
  const bits = itemActionBits(item);
  const out: ItemAction[] = [];
  if (bits & 1) out.push("Move");
  if (bits & 4) out.push(item === equippedItem ? "Unequip" : "Equip");
  if (bits & 2) out.push("Use");
  if (bits & 8) out.push("Discard");
  return out;
}

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
  if (shape === 4) {
    return <span className="glyph glyph-triangle" style={{ background: color }} />;
  }
  if (shape === 5) {
    return <span className="glyph glyph-diamond" style={{ background: color }} />;
  }
  if (shape === 6) {
    return <span className="glyph glyph-plus" style={{ background: color }} />;
  }
  if (shape === 7) {
    return <span className="glyph glyph-frame" style={{ borderColor: color }} />;
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

export function Inventory({
  acceptRef,
}: {
  // Menu navigation (arrows / stick) accept hook owned by the overlay:
  // Space/Enter (or a right-stick centre tap) on a cell opens its context
  // menu here instead of merely selecting the cell. Pointer behaviour is
  // unchanged: click selects, right-click / long-press opens the menu.
  acceptRef?: { current: ((el: HTMLElement) => boolean) | null };
} = {}) {
  const { inventory, health, bottleEquipped, bottleEmpty, equippedItem, keyItems, gear } =
    useGameState();
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
  const menuActions = menuItem >= 0 ? actionsFor(menuItem, equippedItem) : [];

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
    if (item < 0 || actionsFor(item, equippedItem).length === 0) {
      setMenu(null);
      return;
    }
    setSelected(index);
    setMenu({
      index,
      x: Math.min(x, window.innerWidth - 120),
      y: Math.min(y, window.innerHeight - 140),
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

  useEffect(() => {
    if (!acceptRef) {
      return;
    }
    acceptRef.current = (el) => {
      // While the context menu is open, or while a Move is waiting for a
      // target cell, accept keeps its default meaning (click the element).
      if (menu || movingFrom >= 0) {
        return false;
      }
      const raw = el.getAttribute("data-slot-index");
      if (raw === null) {
        return false;
      }
      const index = Number(raw);
      if ((inventory[index] ?? -1) < 0) {
        return false; // empty cell: nothing to open, default select behaviour
      }
      const rect = el.getBoundingClientRect();
      openMenu(index, rect.left, rect.bottom);
      return true;
    };
    return () => {
      acceptRef.current = null;
    };
  });

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
    if (action === "Use") {
      if (applyInventoryItem(index) && selected === index) {
        setSelected(-1);
      }
      return;
    }
    if (action === "Equip" || action === "Unequip") {
      equipItem(inventory[index], action === "Equip");
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
              data-menu-item
              data-slot-index={index}
              data-filled={item >= 0}
              data-selected={selected === index}
              data-moving={movingFrom === index}
              data-equipped={item >= 0 && item === equippedItem}
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
            <p>{itemText(selectedItem)}</p>
          </>
        )}
      </div>
      <div className="equip-row">
        {equippedItem >= 0 && equippedItem !== ITEM_BOTTLE ? (
          <div
            className="gun-slot gun-slot-item"
            data-equipped-item={equippedItem}
            title={`${itemAppearance(equippedItem).label} · equipped`}
          >
            <ItemGlyph appearance={itemAppearance(equippedItem)} />
          </div>
        ) : (
          <div className="gun-slot" title="No weapon equipped" />
        )}
        <div className="quick-slots">
          {bottleEquipped ? (
            <div
              className="quick-slot quick-slot-item"
              data-empty={bottleEmpty}
              title={
                bottleEmpty ? "Empty Bottle · equipped (none left)" : "Empty Bottle · equipped"
              }
            >
              <ItemGlyph appearance={itemAppearance(ITEM_BOTTLE)} />
            </div>
          ) : (
            <div className="quick-slot" />
          )}
        </div>
        <div className="status-stack">
          <div className="pip-row">
            <ChargePips
              label="Steroids Injector. Increases health."
              icon="#f2ff00"
              filled={gear.steroids}
              fill="#f2ff00"
            />
            <ChargePips
              label="Stabilizer Injector. Increases gun damage and handling."
              icon="#66d5f4"
              filled={gear.stabilizer}
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
      <p className="gear-line" data-testid="gear-line">
        Health {gear.hp}/{gear.hpMax}
        {gear.loaded >= 0 ? ` · Loaded ${gear.loaded}/${gear.mag}${gear.reloading ? " (reloading)" : ""}` : ""} · Handgun{" "}
        {gear.handgunAmmo} · 12.7mm {gear.magnumAmmo} · Blood{" "}
        {gear.bloodCap > 0 ? `${gear.blood}/${gear.bloodCap}` : "—"} · Scrap {gear.scrap} · Coins {gear.coins}
      </p>
      {keyItems.length > 0 && (
        <div className="key-ring" data-testid="key-ring">
          {keyItems.map((item) => {
            const look = itemAppearance(item);
            return (
              <span key={item} className="key-ring-item" title={`${look.label}: ${itemText(item)}`}>
                <ItemGlyph appearance={look} />
                <span>{look.label}</span>
              </span>
            );
          })}
        </div>
      )}
      {movingFrom >= 0 && (
        <button type="button" className="move-catch" onClick={() => setMovingFrom(-1)} />
      )}
      {menu && menuActions.length > 0 && (
        <div ref={menuRef} className="item-menu" data-menu-layer style={{ left: menu.x, top: menu.y }}>
          {menuActions.map((action) => (
            <button key={action} type="button" data-menu-item onClick={() => runAction(action)}>
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
    if (
      current.kind === "idle" ||
      current.index !== index ||
      current.pointerId !== event.pointerId
    ) {
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
        {recipes.map((recipe) => {
          const index = recipe.index;
          const ready = recipeReady(index);
          return (
            <li key={recipe.index}>
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
            {file.code > 0 && <span className="file-code">= {file.code}</span>}
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
  const { lastSaveSlot } = useGameState();
  const navRef = useRef<HTMLDivElement>(null);
  useMenuNav(navRef, open);
  if (!open) {
    return null;
  }
  return (
    <div className="pause-overlay" onClick={onClose}>
      <div ref={navRef} className="pause-menu" onClick={(event) => event.stopPropagation()}>
        {lastSaveSlot >= 0 && (
          <button
            type="button"
            className="pause-item" data-menu-item
            onClick={() => {
              loadGame(lastSaveSlot);
              onClose();
            }}
          >
            Restart from last save
          </button>
        )}
        <button
          type="button"
          className="pause-item" data-menu-item
          onClick={() => {
            gameReset();
            onClose();
          }}
        >
          Restart level
        </button>
        <button type="button" className="pause-item" data-menu-item onClick={onLoad}>
          Load Game
        </button>
        <button type="button" className="pause-item" data-menu-item onClick={onClose}>
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
  const navRef = useRef<HTMLDivElement>(null);
  useMenuNav(navRef, true);
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
      <div ref={navRef} className="save-menu" onClick={(event) => event.stopPropagation()}>
        <div className="save-title">Save / Load</div>
        {slots.map((slot) => (
          <div key={slot.index} className="save-row">
            <span className="save-slot">Slot {slot.index + 1}</span>
            <span className="save-data">{slot.used ? formatMs(slot.time) : "NO DATA"}</span>
            <button
              type="button"
              className="pause-item save-btn" data-menu-item
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
              className="pause-item save-btn" data-menu-item
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
        <button type="button" className="pause-item" data-menu-item onClick={onClose}>
          Close
        </button>
      </div>
    </div>
  );
}

// The Item Box (RE's storage trunk): opens when Grace uses one in the
// level. Tap a bag item to store it, a box item to take it back.
export function ItemBox() {
  const { inventory, capacity, dead } = useGameState();
  const navRef = useRef<HTMLDivElement>(null);
  const [open, setOpen] = useState(false);
  const [, setTick] = useState(0);
  useMenuNav(navRef, open && !dead, () => setOpen(false));
  useEffect(() => {
    const id = setInterval(() => {
      if (takeItembox()) setOpen(true);
    }, 150);
    return () => clearInterval(id);
  }, []);
  useEffect(() => {
    if (!open) return;
    setPaused(true);  // the world waits while she sorts the box
    return () => setPaused(false);
  }, [open]);
  if (!open || dead) return null;
  const box = readItembox();
  const refresh = () => setTick((t) => t + 1);
  return (
    <div className="pause-overlay">
      <div ref={navRef} className="item-box" role="dialog" aria-label="Item Box">
        <div className="save-title">Item Box</div>
        <div className="item-box-cols">
          <div>
            <div className="item-box-head">Bag</div>
            <div className="item-box-list" data-testid="itembox-bag">
              {inventory.slice(0, capacity).map((item, slot) =>
                item >= 0 ? (
                  <button
                    key={slot}
                    type="button"
                    className="pause-item item-box-entry"
                    data-menu-item
                    data-item={item}
                    title={`Store ${itemAppearance(item).label}`}
                    onClick={() => { itemboxStore(slot); refresh(); }}
                  >
                    <ItemGlyph appearance={itemAppearance(item)} />
                    <span>{itemAppearance(item).label}</span>
                  </button>
                ) : null,
              )}
            </div>
          </div>
          <div>
            <div className="item-box-head">Box ({box.length})</div>
            <div className="item-box-list" data-testid="itembox-box">
              {box.map((item, index) => (
                <button
                  key={`${index}-${item}`}
                  type="button"
                  className="pause-item item-box-entry"
                  data-menu-item
                  data-item={item}
                  title={`Take ${itemAppearance(item).label}`}
                  onClick={() => { itemboxTake(index); refresh(); }}
                >
                  <ItemGlyph appearance={itemAppearance(item)} />
                  <span>{itemAppearance(item).label}</span>
                </button>
              ))}
            </div>
          </div>
        </div>
        <button type="button" className="pause-item" data-menu-item onClick={() => setOpen(false)}>Close</button>
      </div>
    </div>
  );
}

// Touch-friendly code entry for safes and combination-lock doors (next level).
export function CodeKeypad() {
  const { dead } = useGameState();
  const navRef = useRef<HTMLDivElement>(null);
  const [target, setTarget] = useState<"safe" | "door" | null>(null);
  const [digits, setDigits] = useState("");
  const [msg, setMsg] = useState("");
  useMenuNav(navRef, !!target && !dead, () => setTarget(null));
  useEffect(() => {
    const id = setInterval(() => {
      if (pendingSafe() >= 0) setTarget("safe");
      else if (pendingCodeDoor() >= 0) setTarget("door");
    }, 150);
    return () => clearInterval(id);
  }, []);
  if (!target || dead) return null;
  const submit = () => {
    const ok = target === "safe" ? trySafeCode(Number(digits)) : tryDoorCode(Number(digits));
    if (ok) { setTarget(null); setDigits(""); setMsg(""); }
    else { setMsg("Wrong code"); setDigits(""); }
  };
  return (
    <div className="pause-overlay">
      <div ref={navRef} className="keypad" role="dialog" aria-label="Enter code">
        <div className="keypad-title">{target === "safe" ? "Safe" : "Combination lock"}</div>
        <div className="keypad-display">{digits || "----"}</div>
        <div className="keypad-grid">
          {[1,2,3,4,5,6,7,8,9,0].map((d) => (
            <button key={d} type="button" className="keypad-key" data-menu-item onClick={() => setDigits((v) => (v + d).slice(0, 8))}>{d}</button>
          ))}
          <button type="button" className="keypad-key" data-menu-item onClick={() => setDigits("")}>Clear</button>
          <button type="button" className="keypad-key" data-menu-item onClick={submit}>Open</button>
        </div>
        {msg && <div className="keypad-msg">{msg}</div>}
        <button type="button" className="pause-item" data-menu-item onClick={() => setTarget(null)}>Cancel</button>
      </div>
    </div>
  );
}

export function GameOver({ onLoad }: { onLoad: () => void }) {
  const { dead, lastSaveSlot } = useGameState();
  const navRef = useRef<HTMLDivElement>(null);
  const [away, setAway] = useState(false);
  useMenuNav(navRef, dead && !away);
  useEffect(() => { if (!dead) setAway(false); }, [dead]);
  if (!dead || away) return null;
  return (
    <div className="pause-overlay game-over">
      <div ref={navRef} className="pause-menu">
        <div className="save-title">Grace died</div>
        {lastSaveSlot >= 0 && (
          <button type="button" className="pause-item" data-menu-item onClick={() => loadGame(lastSaveSlot)}>Restart from last save</button>
        )}
        <button type="button" className="pause-item" data-menu-item onClick={() => { setAway(true); onLoad(); }}>Load</button>
        <button type="button" className="pause-item" data-menu-item onClick={() => gameReset()}>Restart level</button>
      </div>
    </div>
  );
}

export function LevelComplete({ onLoad }: { onLoad: () => void }) {
  const { levelComplete, lastSaveSlot } = useGameState();
  const navRef = useRef<HTMLDivElement>(null);
  const elapsed = useElapsed();
  const [away, setAway] = useState(false);
  useMenuNav(navRef, levelComplete && !away);
  useEffect(() => { if (!levelComplete) setAway(false); }, [levelComplete]);
  if (!levelComplete || away) return null;
  return (
    <div className="level-complete" role="status">
      <div ref={navRef} className="pause-menu">
        <div className="save-title">Level complete</div>
        <div className="level-time">Time: {formatMs(elapsed)}</div>
        {lastSaveSlot >= 0 && (
          <button type="button" className="pause-item" data-menu-item onClick={() => loadGame(lastSaveSlot)}>Restart from last save</button>
        )}
        <button type="button" className="pause-item" data-menu-item onClick={() => { setAway(true); onLoad(); }}>Load a save</button>
        <button type="button" className="pause-item" data-menu-item onClick={() => gameReset()}>Restart level</button>
        <button type="button" className="pause-item" data-menu-item onClick={() => setAway(true)}>Close</button>
      </div>
    </div>
  );
}
