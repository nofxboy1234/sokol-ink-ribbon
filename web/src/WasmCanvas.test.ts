import { describe, expect, it } from "vitest";
import { createModuleOptions } from "./WasmCanvas";

describe("createModuleOptions", () => {
  it("attaches the canvas and logging hooks", () => {
    const canvas = {} as HTMLCanvasElement;
    const options = createModuleOptions(canvas);

    expect(options.canvas).toBe(canvas);
    expect(typeof options.print).toBe("function");
    expect(typeof options.printErr).toBe("function");
  });
});
