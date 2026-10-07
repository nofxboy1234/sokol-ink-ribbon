import "../src/app.css";
import { Inventory } from "../src/Inventory";
import { WasmCanvas } from "../src/WasmCanvas";

export default function HomePage() {
  return (
    <main className="stage">
      <WasmCanvas />
      <Inventory />
    </main>
  );
}
