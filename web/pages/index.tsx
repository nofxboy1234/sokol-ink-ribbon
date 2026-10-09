import { useState } from "react";
import "../src/app.css";
import { AuthPanel } from "../src/AuthPanel";
import { GameStateProvider } from "../src/GameState";
import { MapCanvas } from "../src/MapCanvas";
import { Crafting, Files, HealthBar, Inventory, RunPanel } from "../src/panes";

type Tab = "items" | "crafting" | "files";

const TABS: { id: Tab; label: string }[] = [
  { id: "items", label: "ITEMS" },
  { id: "crafting", label: "CRAFTING" },
  { id: "files", label: "FILES" },
];

export default function HomePage() {
  const [tab, setTab] = useState<Tab>("items");

  return (
    <>
      <meta name="viewport" content="width=device-width, initial-scale=1.0" />
      <GameStateProvider>
        <main className="shell">
          <section className="pane pane-map">
            <MapCanvas />
          </section>
          <section className="pane pane-side">
            <header className="side-header">
              <HealthBar />
              <a className="records-link" href="/records">
                Records
              </a>
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
      </GameStateProvider>
    </>
  );
}
