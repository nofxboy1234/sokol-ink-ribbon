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
    // resize, so without this the drawing buffer would keep the startup size
    // and the map would be stretched. Re-measure when the pane resizes.
    //
    // Observe the pane, not the canvas: sokol's own resize path writes inline
    // px width/height onto the canvas, so observing the canvas would feed those
    // writes straight back into another resize and collapse the layout to zero.
    // The pane's box is never written to, so this settles.
    //
    // The tolerance ignores sub-pixel drift, which the browser can report when
    // a fractional layout rounds differently between frames; it avoids
    // reallocating the buffer for nothing. It is not what fixes the load-time
    // reflow - that came from the inventory grid sizing the pane it sits in,
    // which is now prevented by reserving the pane width in CSS.
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
