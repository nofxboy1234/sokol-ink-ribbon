import { useEffect, useRef } from "react";
import { createModuleOptions } from "./wasmBridge";

let wasmStarted = false;
// The canvas is created once and re-parented on navigation: a webgl canvas
// keeps its context when moved, so the wasm keeps rendering after returning
// from another page (e.g. Records).
let canvasEl: HTMLCanvasElement | null = null;

export function MapCanvas() {
  const hostRef = useRef<HTMLDivElement>(null);

  useEffect(() => {
    const host = hostRef.current;
    if (!host) {
      return;
    }
    if (!canvasEl) {
      canvasEl = document.createElement("canvas");
      canvasEl.id = "canvas";
      canvasEl.className = "map-canvas";
      canvasEl.addEventListener("contextmenu", (event) => event.preventDefault());
    }
    host.appendChild(canvasEl);

    // sokol measures the canvas CSS box at startup and on window resize, so
    // re-measure when the pane changes size. Observe the host, not the canvas:
    // sokol writes inline px sizes onto the canvas, which would feed back.
    const observer = new ResizeObserver(() => {
      window.dispatchEvent(new Event("resize"));
    });
    observer.observe(host);

    if (!wasmStarted) {
      wasmStarted = true;
      window.Module = createModuleOptions(canvasEl);
      const script = document.createElement("script");
      script.src = "/wasm/main.js";
      script.async = true;
      document.body.appendChild(script);
    } else {
      // re-attached after navigating back; nudge sokol to re-measure
      window.dispatchEvent(new Event("resize"));
    }

    return () => observer.disconnect();
  }, []);

  return <div ref={hostRef} className="map-canvas-host" />;
}
