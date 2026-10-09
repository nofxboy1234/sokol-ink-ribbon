import { useEffect, useRef } from "react";
import { createModuleOptions } from "./wasmBridge";

let wasmStarted = false;

export function MapCanvas() {
  const canvasRef = useRef<HTMLCanvasElement>(null);

  useEffect(() => {
    const canvas = canvasRef.current;
    if (!canvas) {
      return;
    }

    // sokol measures the canvas CSS box at startup and on window resize, so
    // re-measure when the pane changes size. Observe the pane, not the canvas:
    // sokol writes inline px sizes onto the canvas, which would feed back.
    const pane = canvas.parentElement;
    const observer = new ResizeObserver(() => {
      window.dispatchEvent(new Event("resize"));
    });
    if (pane) {
      observer.observe(pane);
    }

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
      className="map-canvas"
      onContextMenu={(event) => event.preventDefault()}
    />
  );
}
