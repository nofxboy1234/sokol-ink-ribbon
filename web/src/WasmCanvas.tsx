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
    if (!canvas) {
      return;
    }

    // sokol measures the canvas CSS box at startup and again only on window
    // resize, but the shell settles after that (the inventory pane appears and
    // narrows the map pane), so the drawing buffer would stay at the startup
    // size and the map would be stretched. Re-measure when the pane resizes.
    //
    // Observe the pane, not the canvas: sokol's own resize path writes inline
    // px width/height onto the canvas, so observing the canvas would feed those
    // writes straight back into another resize and collapse the layout to
    // zero. The pane's box is never written to, so this settles.
    const pane = canvas.parentElement;
    let lastWidth = -1;
    let lastHeight = -1;
    const observer = new ResizeObserver(() => {
      const { width, height } = pane!.getBoundingClientRect();
      const w = Math.round(width);
      const h = Math.round(height);
      if (w === lastWidth && h === lastHeight) {
        return;
      }
      lastWidth = w;
      lastHeight = h;
      if (w > 0 && h > 0) {
        window.dispatchEvent(new Event("resize"));
      }
    });
    if (pane) {
      observer.observe(pane);
    }

    if (wasmStarted) {
      return () => observer.disconnect();
    }
    wasmStarted = true;

    window.Module = createModuleOptions(canvas);

    const script = document.createElement("script");
    script.src = "/wasm/main.js";
    script.async = true;
    document.body.appendChild(script);

    return () => observer.disconnect();
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
