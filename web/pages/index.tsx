import { useRef } from "react";
import "../src/app.css";
import { Inventory } from "../src/Inventory";
import { WasmCanvas } from "../src/WasmCanvas";

export default function HomePage() {
  // The pane is rendered here rather than inside Inventory so it occupies its
  // reserved width from the very first paint, before any script runs. It is
  // also what Inventory measures to size the grid.
  const sideRef = useRef<HTMLElement>(null);

  return (
    <>
      {/* Without this, phones lay the page out at a fixed ~980px and the
          max-width breakpoint below never matches. React hoists meta into
          <head>. */}
      <meta name="viewport" content="width=device-width, initial-scale=1.0" />
      <main className="shell">
        <section className="pane pane-map">
          <WasmCanvas />
        </section>
        <section className="pane pane-side" ref={sideRef}>
          <Inventory paneRef={sideRef} />
        </section>
      </main>
    </>
  );
}
