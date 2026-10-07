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
    // narrows the map pane), so the drawing buffer would keep the startup size
    // and the map would be stretched. Re-measure when the pane resizes.
    //
    // Observe the pane, not the canvas: sokol's own resize path writes inline
    // px width/height onto the canvas, so observing the canvas would feed those
    // writes straight back into another resize and collapse the layout to zero.
    // The pane's box is never written to, so this settles.
    //
    // Only fires on an actual resize, so a steady layout costs nothing.
    //
    // The change is compared with a tolerance rather than for equality: the
    // side pane is exactly four cells wide and the cell size comes from the
    // camera, which fits the map to the map pane, so the two depend on each
    // other. A fractional cell size makes them settle into a sub-pixel
    // oscillation (the pane alternating between e.g. 778 and 778.67px), and
    // each pass would otherwise resize sokol's buffer and start it again.
    // Rounding would make that worse, not better. Ignoring sub-pixel drift
    // breaks the loop and leaves the map at most a pixel off its ideal fit.
    const pane = canvas.parentElement;
    const TOLERANCE_PX = 1;
    let lastWidth = -1;
    let lastHeight = -1;
    const observer = new ResizeObserver((entries) => {
      const { width, height } = entries[entries.length - 1].contentRect;
      const settled =
        lastWidth >= 0 &&
        Math.abs(width - lastWidth) < TOLERANCE_PX &&
        Math.abs(height - lastHeight) < TOLERANCE_PX;
      if (settled) {
        return;
      }
      lastWidth = width;
      lastHeight = height;
      if (width > 0 && height > 0) {
        window.dispatchEvent(new Event("resize"));
      }
    });
    if (pane) {
      observer.observe(pane);
    }

    // One effect, one cleanup, whether or not the wasm was already started.
    // (StrictMode remounts, so the second pass must not re-inject the script.)
    if (!wasmStarted) {
      wasmStarted = true;
      window.Module = createModuleOptions(canvas);

      const script = document.createElement("script");
      script.src = "/wasm/main.js";
      script.async = true;
      document.body.appendChild(script);
    }

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
