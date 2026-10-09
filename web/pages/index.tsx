import { useEffect, useState } from "react";
import { Link } from "@void/react";
import "../src/app.css";
import { AuthPanel } from "../src/AuthPanel";
import { GameStateProvider } from "../src/GameState";
import { MapCanvas } from "../src/MapCanvas";
import {
  Crafting,
  Files,
  HealthBar,
  Inventory,
  LighterButton,
  PauseMenu,
  RunPanel,
} from "../src/panes";
import { setPaused as setGamePaused } from "../src/wasmBridge";

type Tab = "items" | "crafting" | "files";

const TABS: { id: Tab; label: string }[] = [
  { id: "items", label: "ITEMS" },
  { id: "crafting", label: "CRAFTING" },
  { id: "files", label: "FILES" },
];

export default function HomePage() {
  const [tab, setTab] = useState<Tab>("items");
  const [paused, setPaused] = useState(false);

  useEffect(() => {
    const onKey = (event: KeyboardEvent) => {
      if (event.key === "Escape") {
        setPaused((value) => !value);
      }
    };
    window.addEventListener("keydown", onKey);
    return () => window.removeEventListener("keydown", onKey);
  }, []);

  useEffect(() => {
    setGamePaused(paused);
  }, [paused]);

  return (
    <>
      <meta name="viewport" content="width=device-width, initial-scale=1.0" />
      <GameStateProvider>
        <main className="shell">
          <section className="pane pane-map">
            <MapCanvas />
            <button type="button" className="pause-button" onClick={() => setPaused(true)}>
              Pause
            </button>
          </section>
          <section className="pane pane-side">
            <header className="side-header">
              <HealthBar />
              <LighterButton />
              <Link className="records-link" href="/records">
                Records
              </Link>
              <AuthPanel />
            </header>
            <RunPanel />
            <nav className="tabs">
              {TABS.map((entry) => (
                <button
                  key={entry.id}
                  type="button"
                  className="tab"
                  data-active={tab === entry.id}
                  onClick={() => setTab(entry.id)}
                >
                  {entry.label}
                </button>
              ))}
            </nav>
            {tab === "items" && <Inventory />}
            {tab === "crafting" && <Crafting />}
            {tab === "files" && <Files />}
          </section>
        </main>
        <PauseMenu open={paused} onClose={() => setPaused(false)} />
      </GameStateProvider>
    </>
  );
}
