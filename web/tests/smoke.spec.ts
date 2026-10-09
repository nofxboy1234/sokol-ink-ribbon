import { expect, test } from "@playwright/test";

declare global {
  interface Window {
    Module?: {
      _grid_width?: () => number;
      _grid_height?: () => number;
      _player_cell_x?: () => number;
      _player_cell_y?: () => number;
      _web_inventory_count?: () => number;
      _lighter_value?: () => number;
      _replay_length?: () => number;
    };
  }
}

test("the map app boots and renders the level", async ({ page }) => {
  await page.goto("/");

  const canvas = page.locator("canvas#canvas");
  await expect(canvas).toBeVisible();

  await page.waitForFunction(() => (window.Module?._grid_width?.() ?? 0) > 0, null, {
    timeout: 30_000,
  });

  const grid = await page.evaluate(() => ({
    cols: window.Module?._grid_width?.() ?? 0,
    rows: window.Module?._grid_height?.() ?? 0,
    cellX: window.Module?._player_cell_x?.() ?? -1,
    cellY: window.Module?._player_cell_y?.() ?? -1,
    replay: window.Module?._replay_length?.() ?? 0,
  }));

  expect(grid.cols).toBe(64);
  expect(grid.rows).toBe(56);
  expect(grid.cellX).toBe(5);
  expect(grid.cellY).toBe(13);
  expect(grid.replay).toBeGreaterThan(0);
});

test("the side panes and pause menu are present", async ({ page }) => {
  await page.goto("/");
  await expect(page.getByRole("button", { name: "ITEMS" })).toBeVisible();
  await expect(page.getByRole("button", { name: "CRAFTING" })).toBeVisible();
  await expect(page.getByRole("button", { name: "FILES" })).toBeVisible();

  await page.getByRole("button", { name: "Pause" }).click();
  await expect(page.getByRole("button", { name: "Restart" })).toBeVisible();
  await expect(page.getByRole("button", { name: "Load Game" })).toBeVisible();
});

test("walking records a replay", async ({ page }) => {
  await page.goto("/");
  await page.waitForFunction(() => (window.Module?._grid_width?.() ?? 0) > 0, null, {
    timeout: 30_000,
  });

  const before = await page.evaluate(() => window.Module?._replay_length?.() ?? 0);
  const canvas = page.locator("canvas#canvas");
  const box = await canvas.boundingBox();
  if (!box) {
    throw new Error("canvas has no box");
  }
  // click to the right of Grace's start cell to walk a few cells
  await page.mouse.click(box.x + box.width * 0.62, box.y + box.height * 0.5);
  await page.waitForTimeout(1500);
  const after = await page.evaluate(() => window.Module?._replay_length?.() ?? 0);
  expect(after).toBeGreaterThan(before);
});
