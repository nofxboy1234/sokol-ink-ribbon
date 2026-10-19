import { useEffect, useRef, useState } from "react";
import { Link } from "@void/react";
import "../src/app.css";
import { AuthPanel } from "../src/AuthPanel";
import { GameStateProvider } from "../src/GameState";
import { GraceActions } from "../src/GraceActions";
import { MapCanvas } from "../src/MapCanvas";
import { AimStick, Stick } from "../src/Stick";
import { CodeKeypad, Crafting, Files, GameOver, Inventory, LevelComplete, PauseMenu, RunPanel, SaveLoadMenu } from "../src/panes";
import { setPaused as setGamePaused, takeSaveMenu } from "../src/wasmBridge";
import { onMenuShortcut, useMenuNav } from "../src/menuNav";
import { useGameState } from "../src/GameState";

type Tab = "items" | "crafting" | "files";

const TABS: { id: Tab; label: string }[] = [
  { id: "items", label: "ITEMS" },
  { id: "crafting", label: "CRAFTING" },
  { id: "files", label: "FILES" },
];

function DeadMenuCloser({ onDead }: { onDead: () => void }) {
  const { dead } = useGameState();
  useEffect(() => { if (dead) onDead(); }, [dead, onDead]);
  return null;
}

// What used to be the right-hand pane, now an overlay in the pause-menu
// style: header carries Records + sign-in, then the ITEMS/CRAFTING/FILES
// tabs. Opening it freezes the world (see setGamePaused below).
function InventoryOverlay({ open, onClose }: { open: boolean; onClose: () => void }) {
  const [tab, setTab] = useState<Tab>("items");
  const navRef = useRef<HTMLDivElement>(null);
  // Space/Enter on an inventory cell opens its context menu (Inventory
  // registers the handler); accepts elsewhere keep the default click.
  const acceptRef = useRef<((el: HTMLElement) => boolean) | null>(null);
  useMenuNav(navRef, open, undefined, (el) => acceptRef.current?.(el) ?? false);
  if (!open) {
    return null;
  }
  return (
    <div className="pause-overlay" onClick={onClose}>
      <div ref={navRef} className="inventory-panel" role="dialog" aria-label="Inventory" onClick={(event) => event.stopPropagation()}>
        <header className="inventory-header">
          <Link className="records-link" data-menu-item href="/records">
            Records
          </Link>
          <AuthPanel />
          <button type="button" className="pause-item inventory-close" onClick={onClose}>
            Close
          </button>
        </header>
        <nav className="tabs">
          {TABS.map((entry) => (
            <button
              key={entry.id}
              type="button"
              className="tab"
              data-menu-item
              data-active={tab === entry.id}
              onClick={() => setTab(entry.id)}
            >
              {entry.label}
            </button>
          ))}
        </nav>
        {tab === "items" && <Inventory acceptRef={acceptRef} />}
        {tab === "crafting" && <Crafting />}
        {tab === "files" && <Files />}
      </div>
    </div>
  );
}

export default function HomePage() {
  const [paused, setPaused] = useState(false);
  const [saveMenu, setSaveMenu] = useState(false);
  const [inventoryOpen, setInventoryOpen] = useState(false);

  // Opening menus without a pointer: P toggles Pause, I toggles the
  // Inventory (on a phone: hold the move stick's centre for Pause,
  // double-tap it for Inventory -- see Stick.tsx, dispatched through
  // the same toggles). Esc closes the topmost menu, else toggles
  // Pause. Save/Load sits on top of these: while it is open only Esc
  // (and its own navigation) applies. Typing in auth fields is not a
  // shortcut. No conflicts: WASD move, E runs, Q aims, Space interacts,
  // Z equips, arrows/Enter navigate inside menus.
  const togglePause = () => {
    if (saveMenu || inventoryOpen) {
      return;
    }
    setPaused((value) => !value);
  };
  const toggleInventory = () => {
    if (saveMenu) {
      return;
    }
    if (inventoryOpen) {
      setInventoryOpen(false);
      return;
    }
    setPaused(false); // switch, never stack, so menu capture stays single
    setInventoryOpen(true);
  };

  useEffect(() => onMenuShortcut((shortcut) => {
    if (shortcut === "pause") {
      togglePause();
    } else {
      toggleInventory();
    }
  }));

  useEffect(() => {
    const onKey = (event: KeyboardEvent) => {
      const target = event.target as HTMLElement | null;
      if (
        target &&
        (target.tagName === "INPUT" || target.tagName === "TEXTAREA" || target.tagName === "SELECT")
      ) {
        return;
      }
      if (event.key === "Escape") {
        // Escape closes the topmost menu first, then toggles the pause menu.
        if (saveMenu) {
          setSaveMenu(false);
          return;
        }
        if (inventoryOpen) {
          setInventoryOpen(false);
          return;
        }
        setPaused((value) => !value);
        return;
      }
      if (saveMenu) {
        return;
      }
      const key = event.key.toLowerCase();
      if (key === "p") {
        togglePause();
      } else if (key === "i") {
        toggleInventory();
      }
    };
    window.addEventListener("keydown", onKey);
    return () => window.removeEventListener("keydown", onKey);
  });

  // A typewriter click in the canvas opens the save/load menu.
  useEffect(() => {
    const id = setInterval(() => {
      if (takeSaveMenu()) {
        setSaveMenu(true);
      }
    }, 150);
    return () => clearInterval(id);
  }, []);

  useEffect(() => {
    setGamePaused(paused || saveMenu || inventoryOpen);
  }, [paused, saveMenu, inventoryOpen]);

  return (
    <>
      <meta name="viewport" content="width=device-width, initial-scale=1.0" />
      <GameStateProvider>
        <main className="shell">
          <section className="pane pane-map">
            <MapCanvas />
            <GraceActions />
            <Stick />
            <AimStick />
            <div className="timer-chip">
              <RunPanel />
            </div>
            <div className="map-actions">
              <button type="button" className="pause-button" onClick={() => setInventoryOpen(true)}>
                Inventory <span aria-hidden="true" className="shortcut-hint">(I)</span>
              </button>
              <button type="button" className="pause-button" onClick={() => setPaused(true)}>
                Pause <span aria-hidden="true" className="shortcut-hint">(P)</span>
              </button>
            </div>
          </section>
        </main>
        <DeadMenuCloser onDead={() => { setPaused(false); setSaveMenu(false); setInventoryOpen(false); }} />
        <InventoryOverlay open={inventoryOpen} onClose={() => setInventoryOpen(false)} />
        <PauseMenu
          open={paused}
          onClose={() => setPaused(false)}
          onLoad={() => {
            setPaused(false);
            setSaveMenu(true);
          }}
        />
        <GameOver onLoad={() => setSaveMenu(true)} />
        <LevelComplete onLoad={() => setSaveMenu(true)} />
        <CodeKeypad />
        <SaveLoadMenu open={saveMenu} onClose={() => setSaveMenu(false)} />
      </GameStateProvider>
    </>
  );
}
