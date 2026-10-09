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
      _level_cell_px?: () => number;
      _level_revealed?: () => number;
      _player_total_steps?: () => number;
      _run_elapsed_ms?: () => number;
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
    revealed: window.Module?._level_revealed?.() ?? 0,
  }));

  expect(grid.cols).toBe(99);
  expect(grid.rows).toBe(86);
  expect(grid.cellX).toBe(89);
  expect(grid.cellY).toBe(4);
  expect(grid.replay).toBeGreaterThan(0);
  // the whole starting section is revealed as one large chunk
  expect(grid.revealed).toBeGreaterThan(100);
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
  const cellPx = await page.evaluate(() => window.Module?._level_cell_px?.() ?? 20);
  const canvas = page.locator("canvas#canvas");
  const box = await canvas.boundingBox();
  if (!box) {
    throw new Error("canvas has no box");
  }
  // Grace is centred; click ~2.5 cells to her right to walk within the room
  await page.mouse.click(box.x + box.width / 2 + cellPx * 2.5, box.y + box.height / 2);
  await page.waitForTimeout(1500);
  const after = await page.evaluate(() => window.Module?._replay_length?.() ?? 0);
  expect(after).toBeGreaterThan(before);
});

test("pause stops movement", async ({ page }) => {
  await page.goto("/");
  await page.waitForFunction(() => (window.Module?._grid_width?.() ?? 0) > 0, null, {
    timeout: 30_000,
  });

  const cellPx = await page.evaluate(() => window.Module?._level_cell_px?.() ?? 20);
  const box = await page.locator("canvas#canvas").boundingBox();
  if (!box) {
    throw new Error("canvas has no box");
  }

  // start a long walk, then pause mid-move
  await page.mouse.click(box.x + box.width / 2 + cellPx * 6, box.y + box.height / 2);
  await page.waitForTimeout(300);
  await page.getByRole("button", { name: "Pause" }).click();

  const t1 = await page.evaluate(() => window.Module?._run_elapsed_ms?.() ?? 0);
  const s1 = await page.evaluate(() => window.Module?._player_total_steps?.() ?? 0);
  await page.waitForTimeout(900);
  const t2 = await page.evaluate(() => window.Module?._run_elapsed_ms?.() ?? 0);
  const s2 = await page.evaluate(() => window.Module?._player_total_steps?.() ?? 0);

  expect(t2 - t1).toBeLessThan(50);
  expect(s2).toBe(s1);
});

test("no save button and records navigates client-side", async ({ page }) => {
  await page.goto("/");
  await expect(page.getByRole("button", { name: "Save run" })).toHaveCount(0);

  await page.evaluate(() => {
    (window as unknown as { __marker?: number }).__marker = 1;
  });
  await page.getByRole("link", { name: "Records" }).click();
  await expect(page.getByRole("heading", { name: "Records" })).toBeVisible();
  const marker = await page.evaluate(() => (window as unknown as { __marker?: number }).__marker);
  expect(marker).toBe(1);
});
