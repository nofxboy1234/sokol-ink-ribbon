import { expect, test } from "@playwright/test";

declare global {
  interface Window {
    Module?: {
      _grid_width?: () => number;
      _grid_height?: () => number;
      _player_cell_x?: () => number;
      _player_cell_y?: () => number;
      _web_inventory_count?: () => number;
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
  }));

  expect(grid.cols).toBe(64);
  expect(grid.rows).toBe(48);
  expect(grid.cellX).toBe(6);
  expect(grid.cellY).toBe(10);
});

test("the side panes are present", async ({ page }) => {
  await page.goto("/");
  await expect(page.getByRole("button", { name: "ITEMS" })).toBeVisible();
  await expect(page.getByRole("button", { name: "CRAFTING" })).toBeVisible();
  await expect(page.getByRole("button", { name: "FILES" })).toBeVisible();
});
