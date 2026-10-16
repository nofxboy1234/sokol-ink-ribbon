import { expect, test, type Page } from "@playwright/test";

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
      _web_grace_mode?: () => number;
      _web_grace_screen_x?: () => number;
      _web_grace_screen_y?: () => number;
    };
  }
}

// From Paused (menu open): tap an action icon, click a cell dx cells to Grace's right,
// then Ok on the pill.
async function orderGrace(page: Page, action: "Walk" | "Run", dx: number) {
  const box = await page.locator("canvas#canvas").boundingBox();
  if (!box) {
    throw new Error("canvas has no box");
  }
  const at = await page.evaluate(() => ({
    x: window.Module?._web_grace_screen_x?.() ?? 0,
    y: window.Module?._web_grace_screen_y?.() ?? 0,
  }));
  const cellPx = await page.evaluate(() => window.Module?._level_cell_px?.() ?? 20);
  // the menu is open whenever she is Paused
  await page.getByRole("button", { name: action, exact: true }).click();
  const ok = page
    .getByRole("toolbar", { name: "Confirm action" })
    .getByRole("button", { name: "Ok", exact: true });
  await expect(ok).toBeDisabled();
  const scale = await page.evaluate(() => window.devicePixelRatio);
  await page.mouse.click(box.x + at.x + (cellPx / scale) * dx, box.y + at.y);
  await ok.click();
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

  await page.getByRole("button", { name: "Pause", exact: true }).click();
  await expect(page.getByRole("button", { name: "Restart" })).toBeVisible();
  await expect(page.getByRole("button", { name: "Load Game" })).toBeVisible();
});

test("walking records a replay", async ({ page }) => {
  await page.goto("/");
  await page.waitForFunction(() => (window.Module?._grid_width?.() ?? 0) > 0, null, {
    timeout: 30_000,
  });

  const before = await page.evaluate(() => window.Module?._replay_length?.() ?? 0);
  // walk ~2 cells to her right, within the room
  await orderGrace(page, "Walk", 2);
  await page.waitForTimeout(1500);
  const after = await page.evaluate(() => window.Module?._replay_length?.() ?? 0);
  expect(after).toBeGreaterThan(before);
  // she is Paused again once she arrives
  expect(await page.evaluate(() => window.Module?._web_grace_mode?.())).toBe(0);
});

test("the world is frozen while Grace is paused", async ({ page }) => {
  await page.goto("/");
  await page.waitForFunction(() => (window.Module?._grid_width?.() ?? 0) > 0, null, {
    timeout: 30_000,
  });
  // the action menu starts open; with nothing done yet the indicator shows Wait
  await expect(page.getByRole("button", { name: "Walk", exact: true })).toBeVisible();
  await expect(page.getByRole("status", { name: "Grace: Paused, last Waiting" })).toBeVisible();
  const t0 = await page.evaluate(() => window.Module?._run_elapsed_ms?.() ?? 0);
  await page.waitForTimeout(600);
  expect(await page.evaluate(() => window.Module?._run_elapsed_ms?.() ?? 0)).toBe(t0);
});

test("a double click on the map pauses Grace mid-move", async ({ page }) => {
  await page.goto("/");
  await page.waitForFunction(() => (window.Module?._grid_width?.() ?? 0) > 0, null, {
    timeout: 30_000,
  });

  await orderGrace(page, "Walk", 6);
  await page.waitForTimeout(300);
  const box = await page.locator("canvas#canvas").boundingBox();
  if (!box) {
    throw new Error("canvas has no box");
  }
  const x = box.x + box.width / 2 + 60;
  const y = box.y + box.height / 2 + 60;
  await page.mouse.click(x, y);
  await page.waitForTimeout(400);
  // a single click does nothing
  expect(await page.evaluate(() => window.Module?._web_grace_mode?.())).toBe(1);
  await expect(page.getByRole("status", { name: "Grace: Walking" })).toBeVisible();
  await page.mouse.dblclick(x, y);
  await expect.poll(() => page.evaluate(() => window.Module?._web_grace_mode?.())).toBe(0);
  // paused by double-click: the menu opens again, the indicator keeps her
  // last state and the cyan route to her target stays drawn
  await expect(page.getByRole("button", { name: "Walk", exact: true })).toBeVisible();
  await expect(page.getByRole("status", { name: "Grace: Paused, last Walking" })).toBeVisible();
  expect(await page.evaluate(() => window.Module?._web_grace_pending?.())).toBe(1);
  expect(await page.evaluate(() => window.Module?._web_route_length?.() ?? 0)).toBeGreaterThan(1);

  const t1 = await page.evaluate(() => window.Module?._run_elapsed_ms?.() ?? 0);
  const s1 = await page.evaluate(() => window.Module?._player_total_steps?.() ?? 0);
  await page.waitForTimeout(900);
  const t2 = await page.evaluate(() => window.Module?._run_elapsed_ms?.() ?? 0);
  const s2 = await page.evaluate(() => window.Module?._player_total_steps?.() ?? 0);

  expect(await page.evaluate(() => window.Module?._web_grace_mode?.())).toBe(0);
  expect(t1).toBeGreaterThan(0);
  expect(t2).toBe(t1);
  expect(s2).toBe(s1);
});

test("the Pause Menu stops movement", async ({ page }) => {
  await page.goto("/");
  await page.waitForFunction(() => (window.Module?._grid_width?.() ?? 0) > 0, null, {
    timeout: 30_000,
  });

  await orderGrace(page, "Walk", 6);
  await page.waitForTimeout(300);
  await page.getByRole("button", { name: "Pause", exact: true }).click();

  const t1 = await page.evaluate(() => window.Module?._run_elapsed_ms?.() ?? 0);
  const s1 = await page.evaluate(() => window.Module?._player_total_steps?.() ?? 0);
  await page.waitForTimeout(900);
  const t2 = await page.evaluate(() => window.Module?._run_elapsed_ms?.() ?? 0);
  const s2 = await page.evaluate(() => window.Module?._player_total_steps?.() ?? 0);

  expect(t2 - t1).toBeLessThan(50);
  expect(s2).toBe(s1);
});

test("a phone draws map cells about as large as a desktop window resized to the same box", async ({
  browser,
}) => {
  const desktop = await browser.newContext({
    viewport: { width: 1280, height: 800 },
    deviceScaleFactor: 1,
  });
  const desktopPage = await desktop.newPage();
  await desktopPage.goto("/");
  await desktopPage.waitForFunction(() => (window.Module?._grid_width?.() ?? 0) > 0, null, {
    timeout: 30_000,
  });
  const opened = await desktopPage.evaluate(() => window.Module?._level_cell_px?.() ?? 0);
  expect(opened).toBe(17);

  await desktopPage.setViewportSize({ width: 390, height: 844 });
  await desktopPage.waitForFunction(() => {
    const canvas = document.querySelector("canvas#canvas");
    return canvas instanceof HTMLCanvasElement && canvas.width === 372;
  });
  const resized = await desktopPage.evaluate(() => ({
    cell: window.Module?._level_cell_px?.() ?? 0,
    width: document.querySelector("canvas#canvas")?.width ?? 0,
  }));
  expect(resized).toEqual({ cell: 17, width: 372 });
  await desktop.close();

  const phone = await browser.newContext({
    viewport: { width: 390, height: 844 },
    deviceScaleFactor: 3,
    isMobile: true,
    hasTouch: true,
  });
  const phonePage = await phone.newPage();
  await phonePage.goto("/");
  await phonePage.waitForFunction(() => (window.Module?._grid_width?.() ?? 0) > 0, null, {
    timeout: 30_000,
  });
  const phoneMap = await phonePage.evaluate(() => ({
    cell: window.Module?._level_cell_px?.() ?? 0,
    width: document.querySelector("canvas#canvas")?.width ?? 0,
  }));
  expect(phoneMap).toEqual({ cell: 16, width: 372 });
  await phone.close();
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

test("right click on Grace opens Quick Equip and an active bottle can be thrown", async ({
  page,
}) => {
  await page.addInitScript(() => {
    const seen: boolean[] = [];
    (window as unknown as { __ctx: boolean[] }).__ctx = seen;
    document.addEventListener("contextmenu", (event) => seen.push(event.defaultPrevented));
  });
  await page.goto("/");
  await page.waitForFunction(() => (window.Module?._grid_width?.() ?? 0) > 0, null, {
    timeout: 30_000,
  });
  const box = await page.locator("canvas#canvas").boundingBox();
  if (!box) {
    throw new Error("canvas has no box");
  }
  const at = await page.evaluate(() => ({
    x: window.Module?._web_grace_screen_x?.() ?? 0,
    y: window.Module?._web_grace_screen_y?.() ?? 0,
  }));
  const quick = page.getByRole("group", { name: "Quick Equip" });

  await page.mouse.click(box.x + at.x, box.y + at.y, { button: "right" });
  await expect(quick).toBeVisible();
  await expect(page.getByRole("button", { name: "Walk", exact: true })).toBeHidden();
  expect(await page.evaluate(() => (window as unknown as { __ctx: boolean[] }).__ctx)).toEqual([
    true,
  ]);
  // nothing equipped yet: no bottle
  await expect(page.getByRole("button", { name: "Empty Bottle", exact: true })).toHaveCount(0);

  // equip a bottle from the Inventory
  await page.evaluate(() => window.Module?._web_test_give?.(0));
  const slot = page.locator(".inventory-slot", { hasText: "Empty Bottle" });
  await slot.click({ button: "right" });
  await page.locator(".item-menu").getByRole("button", { name: "Equip", exact: true }).click();
  await expect(slot).toHaveAttribute("data-equipped", "true");

  // the bottle sits East in Quick Equip (still open); picking it makes it active
  const bottle = quick.getByRole("button", { name: "Empty Bottle", exact: true });
  await expect(bottle).toHaveAttribute("data-slot", "East");
  await bottle.click();
  expect(await page.evaluate(() => window.Module?._web_bottle_active?.())).toBe(1);

  // Throw sits West; aim at a cell in reach, confirm on the pill
  const throwButton = page.getByRole("button", { name: "Throw", exact: true });
  await expect(throwButton).toHaveAttribute("data-slot", "West");
  await throwButton.click();
  const target = await page.evaluate(() => {
    const m = window.Module;
    const px = m?._player_cell_x?.() ?? 0;
    const py = m?._player_cell_y?.() ?? 0;
    for (let d = 5; d >= 2; d--) {
      for (const [dx, dy] of [
        [d, 0],
        [-d, 0],
        [0, d],
        [0, -d],
      ]) {
        if (m?._web_throw_in_reach?.(px + dx, py + dy)) {
          return { dx, dy };
        }
      }
    }
    return null;
  });
  expect(target).not.toBeNull();
  const cellPx = await page.evaluate(() => window.Module?._web_cell_css_px?.() ?? 0);
  await page.mouse.click(
    box.x + at.x + (target?.dx ?? 0) * cellPx,
    box.y + at.y + (target?.dy ?? 0) * cellPx,
  );
  const ok = page
    .getByRole("toolbar", { name: "Confirm action" })
    .getByRole("button", { name: "Ok", exact: true });
  await expect(ok).toBeEnabled();
  await ok.click();
  expect(
    await page.evaluate(() => window.Module?._web_impact_age_ms?.() ?? -1),
  ).toBeGreaterThanOrEqual(0);
  // the bottle is spent (and unequipped), and she stays Paused
  await expect(slot).toHaveCount(0);
  expect(await page.evaluate(() => window.Module?._web_bottle_equipped?.())).toBe(0);
  expect(await page.evaluate(() => window.Module?._web_grace_mode?.())).toBe(0);
});

test("a tap on an item next to Grace picks it up instead of toggling her menu", async ({
  page,
}) => {
  await page.goto("/");
  await page.waitForFunction(() => (window.Module?._grid_width?.() ?? 0) > 0, null, {
    timeout: 30_000,
  });
  // stand just west of the Green Herb at (95, 8), menu open
  await page.evaluate(() => window.Module?._web_test_grace_at?.(94, 8));
  await page.waitForTimeout(250);
  const box = await page.locator("canvas#canvas").boundingBox();
  if (!box) {
    throw new Error("canvas has no box");
  }
  const at = await page.evaluate(() => ({
    x: window.Module?._web_grace_screen_x?.() ?? 0,
    y: window.Module?._web_grace_screen_y?.() ?? 0,
    cell: window.Module?._web_cell_css_px?.() ?? 0,
  }));
  expect(await page.evaluate(() => window.Module?._web_grace_menu_open?.())).toBe(1);
  const before = await page.evaluate(() => window.Module?._items_collected?.() ?? 0);
  // just inside the herb's cell, within the old generous dot radius
  await page.mouse.click(box.x + at.x + at.cell * 0.5 + 3, box.y + at.y);
  await expect
    .poll(() => page.evaluate(() => window.Module?._items_collected?.() ?? 0))
    .toBe(before + 1);
  // a tap on her own cell still toggles the menu
  await page.mouse.click(box.x + at.x, box.y + at.y);
  await expect.poll(() => page.evaluate(() => window.Module?._web_grace_menu_open?.())).toBe(1);
  await page.mouse.click(box.x + at.x, box.y + at.y);
  await expect.poll(() => page.evaluate(() => window.Module?._web_grace_menu_open?.())).toBe(0);
});

test("a double click while Paused resumes the paused walk, and does nothing at start", async ({
  page,
}) => {
  await page.goto("/");
  await page.waitForFunction(() => (window.Module?._grid_width?.() ?? 0) > 0, null, {
    timeout: 30_000,
  });
  const box = await page.locator("canvas#canvas").boundingBox();
  if (!box) {
    throw new Error("canvas has no box");
  }
  const x = box.x + box.width / 2 + 60;
  const y = box.y + box.height / 2 + 60;
  const mode = () => page.evaluate(() => window.Module?._web_grace_mode?.());
  await orderGrace(page, "Walk", 6);
  await page.waitForTimeout(300);
  await page.mouse.dblclick(x, y);
  await expect.poll(mode).toBe(0);
  // stays Paused until asked
  await page.waitForTimeout(600);
  expect(await mode()).toBe(0);
  await page.mouse.dblclick(x, y);
  await expect.poll(mode).toBe(1);

  // nothing to resume at the start
  await page.reload();
  await page.waitForFunction(() => (window.Module?._grid_width?.() ?? 0) > 0, null, {
    timeout: 30_000,
  });
  await page.mouse.dblclick(x, y);
  await page.waitForTimeout(400);
  expect(await mode()).toBe(0);
  expect(await page.evaluate(() => window.Module?._web_grace_resumable?.())).toBe(0);
});

test("left click toggles only the action menu, right click only Quick Equip", async ({ page }) => {
  await page.goto("/");
  await page.waitForFunction(() => (window.Module?._grid_width?.() ?? 0) > 0, null, {
    timeout: 30_000,
  });
  const box = await page.locator("canvas#canvas").boundingBox();
  if (!box) {
    throw new Error("canvas has no box");
  }
  const at = await page.evaluate(() => ({
    x: window.Module?._web_grace_screen_x?.() ?? 0,
    y: window.Module?._web_grace_screen_y?.() ?? 0,
  }));
  const state = () =>
    page.evaluate(() => [
      window.Module?._web_grace_menu_open?.(),
      window.Module?._web_quick_open?.(),
    ]);
  const left = () => page.mouse.click(box.x + at.x, box.y + at.y);
  const right = () => page.mouse.click(box.x + at.x, box.y + at.y, { button: "right" });
  expect(await state()).toEqual([1, 0]);
  await right();
  await expect.poll(state).toEqual([0, 1]);
  await right();
  await expect.poll(state).toEqual([0, 0]);
  await left();
  await expect.poll(state).toEqual([1, 0]);
  await left();
  await expect.poll(state).toEqual([0, 0]);
  await right();
  await expect.poll(state).toEqual([0, 1]);
  await left();
  await expect.poll(state).toEqual([1, 0]);
});
