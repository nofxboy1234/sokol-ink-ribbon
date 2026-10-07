import { useEffect, useRef } from "react";
import type { EmscriptenModule } from "./wasmBridge";

export type { EmscriptenModule };

export function createModuleOptions(canvas: HTMLCanvasElement): EmscriptenModule {
  return {
    canvas,
    print: (...args: unknown[]) => console.log("[stdout]:", ...args),
    printErr: (...args: unknown[]) => console.error("[stderr]:", ...args),
  };
}

let wasmStarted = false;

export function WasmCanvas() {
  const canvasRef = useRef<HTMLCanvasElement>(null);

  useEffect(() => {
    const canvas = canvasRef.current;
    if (!canvas || wasmStarted) {
      return;
    }
    wasmStarted = true;

    window.Module = createModuleOptions(canvas);

    const script = document.createElement("script");
    script.src = "/wasm/main.js";
    script.async = true;
    document.body.appendChild(script);
  }, []);

  return (
    <canvas
      ref={canvasRef}
      id="canvas"
      className="wasm-canvas"
      onContextMenu={(event) => event.preventDefault()}
    />
  );
}
