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

// Realtime: hold a movement key (WASD) for a while; E latches run.
async function steer(page: Page, key: string, ms: number) {
  await page.keyboard.down(key);
  await page.waitForTimeout(ms);
  await page.keyboard.up(key);
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

test("the map is full-width and the Inventory overlay carries the old side pane", async ({ page }) => {
  await page.goto("/");
  // no right-hand pane any more
  await expect(page.locator(".pane-side")).toHaveCount(0);
  await expect(page.getByRole("button", { name: "ITEMS" })).toHaveCount(0);

  await page.getByRole("button", { name: "Inventory", exact: true }).click();
  const overlay = page.getByRole("dialog", { name: "Inventory" });
  await expect(overlay).toBeVisible();
  await expect(overlay.getByRole("button", { name: "ITEMS" })).toBeVisible();
  await expect(overlay.getByRole("button", { name: "CRAFTING" })).toBeVisible();
  await expect(overlay.getByRole("button", { name: "FILES" })).toBeVisible();
  await expect(overlay.getByRole("link", { name: "Records" })).toBeVisible();
  await overlay.getByRole("button", { name: "Close" }).click();
  await expect(overlay).toHaveCount(0);

  await page.getByRole("button", { name: "Pause", exact: true }).click();
  await expect(page.getByRole("button", { name: "Restart level" })).toBeVisible();
  await expect(page.getByRole("button", { name: "Restart from last save" })).toHaveCount(0);
  await expect(page.getByRole("button", { name: "Load Game" })).toBeVisible();
});

test("walking records a replay", async ({ page }) => {
  await page.goto("/");
  await page.waitForFunction(() => (window.Module?._grid_width?.() ?? 0) > 0, null, {
    timeout: 30_000,
  });

  const before = await page.evaluate(() => window.Module?._replay_length?.() ?? 0);
  await steer(page, "d", 700); // ~2 cells right at walk speed
  const after = await page.evaluate(() => window.Module?._replay_length?.() ?? 0);
  expect(after).toBeGreaterThan(before);
  // released: she waits, and the world stays live
  await expect.poll(() => page.evaluate(() => window.Module?._web_grace_mode?.())).toBe(3);
});

test("the world is live while she waits, and the Pause menu freezes it", async ({ page }) => {
  await page.goto("/");
  await page.waitForFunction(() => (window.Module?._grid_width?.() ?? 0) > 0, null, {
    timeout: 30_000,
  });
  await expect(page.getByRole("status", { name: "Grace: Waiting" })).toBeVisible();
  const t0 = await page.evaluate(() => window.Module?._run_elapsed_ms?.() ?? 0);
  await page.waitForTimeout(600);
  expect(await page.evaluate(() => window.Module?._run_elapsed_ms?.() ?? 0)).toBeGreaterThan(t0);

  await page.getByRole("button", { name: "Pause", exact: true }).click();
  const t1 = await page.evaluate(() => window.Module?._run_elapsed_ms?.() ?? 0);
  await page.waitForTimeout(600);
  expect(await page.evaluate(() => window.Module?._run_elapsed_ms?.() ?? 0)).toBe(t1);
});

test("clicking Grace stops her mid-move; WASD and the E run latch drive her", async ({ page }) => {
  await page.goto("/");
  await page.waitForFunction(() => (window.Module?._grid_width?.() ?? 0) > 0, null, {
    timeout: 30_000,
  });
  const box = await page.locator("canvas#canvas").boundingBox();
  if (!box) throw new Error("canvas has no box");
  const mode = () => page.evaluate(() => window.Module?._web_grace_mode?.());

  await page.keyboard.down("d");
  await expect.poll(mode).toBe(1); // Walking
  await page.keyboard.press("e");
  await expect.poll(mode).toBe(2); // Running (latched)
  await page.keyboard.press("e");
  await page.waitForTimeout(200);
  expect(await mode()).toBe(2); // E while running does not toggle off

  // click on her dot: she stops where she stands, back to Waiting
  const at = await page.evaluate(() => ({
    x: window.Module?._web_grace_screen_x?.() ?? 0,
    y: window.Module?._web_grace_screen_y?.() ?? 0,
  }));
  await page.mouse.click(box.x + at.x, box.y + at.y);
  await page.keyboard.up("d");
  await expect.poll(mode).toBe(3);

  // the latch cleared when she stopped: moving again walks
  await page.keyboard.down("d");
  await expect.poll(mode).toBe(1);
  await page.keyboard.up("d");
  await expect.poll(mode).toBe(3);
});

test("the Pause Menu stops movement", async ({ page }) => {
  await page.goto("/");
  await page.waitForFunction(() => (window.Module?._grid_width?.() ?? 0) > 0, null, {
    timeout: 30_000,
  });

  await page.keyboard.down("d");
  await page.waitForTimeout(300);
  await page.getByRole("button", { name: "Pause", exact: true }).click();

  const t1 = await page.evaluate(() => window.Module?._run_elapsed_ms?.() ?? 0);
  const s1 = await page.evaluate(() => window.Module?._player_total_steps?.() ?? 0);
  await page.waitForTimeout(900);
  const t2 = await page.evaluate(() => window.Module?._run_elapsed_ms?.() ?? 0);
  const s2 = await page.evaluate(() => window.Module?._player_total_steps?.() ?? 0);
  await page.keyboard.up("d");

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
  await page.getByRole("button", { name: "Inventory", exact: true }).click();
  await page.getByRole("dialog", { name: "Inventory" }).getByRole("link", { name: "Records" }).click();
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
  await expect(page.getByRole("button", { name: "Walk", exact: true })).toHaveCount(0);
  expect(await page.evaluate(() => (window as unknown as { __ctx: boolean[] }).__ctx)).toEqual([
    true,
  ]);
  // nothing equipped yet: no bottle
  await expect(page.getByRole("button", { name: "Empty Bottle", exact: true })).toHaveCount(0);

  // equip a bottle from the Inventory overlay
  await page.evaluate(() => window.Module?._web_test_give?.(0));
  await page.getByRole("button", { name: "Inventory", exact: true }).click();
  const slot = page.locator(".inventory-slot", { hasText: "Empty Bottle" });
  await slot.click({ button: "right" });
  await page.locator(".item-menu").getByRole("button", { name: "Equip", exact: true }).click();
  await expect(slot).toHaveAttribute("data-equipped", "true");
  await page.getByRole("dialog", { name: "Inventory" }).getByRole("button", { name: "Close" }).click();

  // the bottle sits East in Quick Equip (still open); picking it makes it active
  const bottle = quick.getByRole("button", { name: "Empty Bottle", exact: true });
  await expect(bottle).toHaveAttribute("data-slot", "East");
  await bottle.click();
  expect(await page.evaluate(() => window.Module?._web_bottle_active?.())).toBe(1);

  // Q enters aim mode; moving the mouse aims, the click throws
  await page.keyboard.press("q");
  await expect.poll(() => wasm(page, "web_test_aiming")).toBe(1);
  const cellPx = await page.evaluate(() => window.Module?._web_cell_css_px?.() ?? 0);
  const at2 = await page.evaluate(() => ({
    x: window.Module?._web_grace_screen_x?.() ?? 0,
    y: window.Module?._web_grace_screen_y?.() ?? 0,
  }));
  await page.mouse.move(box.x + at2.x + cellPx * 3, box.y + at2.y);
  await page.waitForTimeout(150);
  await page.mouse.click(box.x + at2.x + cellPx * 3, box.y + at2.y);
  expect(
    await page.evaluate(() => window.Module?._web_impact_age_ms?.() ?? -1),
  ).toBeGreaterThanOrEqual(0);
  expect(await wasm(page, "web_test_aiming")).toBe(0);
  // the last bottle is spent: its slot stays equipped but greyed out
  await expect(slot).toHaveCount(0);
  expect(await page.evaluate(() => window.Module?._web_bottle_equipped?.())).toBe(1);
  expect(await page.evaluate(() => window.Module?._web_bottle_empty?.())).toBe(1);
  expect(await page.evaluate(() => window.Module?._web_bottle_active?.())).toBe(0);
  // realtime: after the throw she is simply Waiting
  expect(await page.evaluate(() => window.Module?._web_grace_mode?.())).toBe(3);
});

test("a tap on an item next to Grace picks it up; tapping her stops her", async ({
  page,
}) => {
  await page.goto("/");
  await page.waitForFunction(() => (window.Module?._grid_width?.() ?? 0) > 0, null, {
    timeout: 30_000,
  });
  // stand just west of the Green Herb at (95, 8)
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
  const before = await page.evaluate(() => window.Module?._items_collected?.() ?? 0);
  // just inside the herb's cell
  await page.mouse.click(box.x + at.x + at.cell * 0.5 + 3, box.y + at.y);
  await expect
    .poll(() => page.evaluate(() => window.Module?._items_collected?.() ?? 0))
    .toBe(before + 1);
  // the X key is the same interact trigger: face east back at her cell is a
  // no-op, but the mode stays Waiting and nothing breaks
  await page.keyboard.press("x");
  expect(await page.evaluate(() => window.Module?._web_grace_mode?.())).toBe(3);
});

test("she starts Waiting in a live world, and steering west/east works from spawn", async ({
  page,
}) => {
  await page.goto("/");
  await page.waitForFunction(() => (window.Module?._grid_width?.() ?? 0) > 0, null, {
    timeout: 30_000,
  });
  const mode = () => page.evaluate(() => window.Module?._web_grace_mode?.());
  expect(await mode()).toBe(3); // Waiting, not Paused
  const x0 = await page.evaluate(() => window.Module?._player_cell_x?.() ?? 0);
  await steer(page, "a", 400);
  expect(await page.evaluate(() => window.Module?._player_cell_x?.() ?? 0)).toBeLessThanOrEqual(x0);
  await expect.poll(mode).toBe(3);
});

test("right click toggles only Quick Equip; there is no action menu", async ({ page }) => {
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
  expect(await state()).toEqual([0, 0]);
  await right();
  await expect.poll(state).toEqual([0, 1]);
  await right();
  await expect.poll(state).toEqual([0, 0]);
  await left();
  await expect.poll(state).toEqual([0, 0]); // left click on Grace only stops her
  expect(await page.evaluate(() => window.Module?._web_grace_mode?.())).toBe(3);
});

// Calls a wasm export by name (without the leading underscore).
function wasm(page: Page, name: string, ...args: number[]) {
  return page.evaluate(
    ([n, a]) =>
      (window.Module as unknown as Record<string, (...x: number[]) => number>)[`_${n}`](...a),
    [name, args] as const,
  );
}

async function boot(page: Page) {
  await page.goto("/");
  await page.waitForFunction(() => (window.Module?._grid_width?.() ?? 0) > 0, null, {
    timeout: 30_000,
  });
  const box = await page.locator("canvas#canvas").boundingBox();
  if (!box) {
    throw new Error("canvas has no box");
  }
  return box;
}

test("aiming over the enemy snaps the reticle onto it, and the click throws", async ({ page }) => {
  const box = await boot(page);
  await wasm(page, "web_test_bottle_ready");
  // put the enemy 3 cells east of Grace, in the open
  const gx = await wasm(page, "player_cell_x");
  const gy = await wasm(page, "player_cell_y");
  await wasm(page, "web_test_enemy_place", (gx + 3) * 100 + 50, gy * 100 + 80);
  await page.keyboard.press("q");
  await expect.poll(() => wasm(page, "web_test_aiming")).toBe(1);
  // the world is live: track the enemy with the mouse until the reticle snaps
  await expect
    .poll(
      async () => {
        const ex = await wasm(page, "web_enemy_screen_x");
        const ey = await wasm(page, "web_enemy_screen_y");
        await page.mouse.move(box.x + ex + 3, box.y + ey - 3);
        return wasm(page, "web_aim_enemy");
      },
      { timeout: 5000 },
    )
    .toBe(1);
  const ex = await wasm(page, "web_enemy_screen_x");
  const ey = await wasm(page, "web_enemy_screen_y");
  await page.mouse.click(box.x + ex + 3, box.y + ey - 3);
  expect(await wasm(page, "web_impact_age_ms")).toBeGreaterThanOrEqual(0);
  expect(await wasm(page, "web_test_aiming")).toBe(0);
});

test("a Med Injector heals fully and a Green Herb one level", async ({ page }) => {
  await boot(page);
  await wasm(page, "web_test_hurt");
  await wasm(page, "web_test_hurt");
  expect(await wasm(page, "health_value")).toBe(2);
  await wasm(page, "web_test_give", 2);
  await wasm(page, "web_inventory_use", 0);
  expect(await wasm(page, "health_value")).toBe(1);
  await wasm(page, "web_test_hurt");
  await wasm(page, "web_test_give", 5);
  await wasm(page, "web_inventory_use", 0);
  expect(await wasm(page, "health_value")).toBe(0);
  // and she breathes fast for a while after a hit
  await wasm(page, "web_test_hurt");
  expect(await wasm(page, "web_grace_stress_ms")).toBeGreaterThan(4000);
});

test("the Lighter sits North in Quick Equip, starts off and toggles", async ({ page }) => {
  const box = await boot(page);
  await wasm(page, "web_test_give", 7);
  const at = {
    x: await wasm(page, "web_grace_screen_x"),
    y: await wasm(page, "web_grace_screen_y"),
  };
  await page.mouse.click(box.x + at.x, box.y + at.y, { button: "right" });
  const lighter = page
    .getByRole("group", { name: "Quick Equip" })
    .getByRole("button", { name: "Lighter" });
  await expect(lighter).toHaveAttribute("data-slot", "North");
  await expect(lighter).toHaveAttribute("aria-pressed", "false");
  await lighter.click();
  await expect(lighter).toHaveAttribute("aria-pressed", "true");
  expect(await wasm(page, "lighter_value")).toBe(1);
  await lighter.click();
  expect(await wasm(page, "lighter_value")).toBe(0);
  // no lighter control left in the Inventory
  await expect(page.locator(".quick-slot-btn")).toHaveCount(0);
});

test("items in the dark can't be picked up until lit", async ({ page }) => {
  const box = await boot(page);
  // the herb at (79,49) has no light near it
  await wasm(page, "web_test_grace_at", 78, 49);
  await page.waitForTimeout(200);
  expect(await wasm(page, "web_light_at", 79, 49)).toBe(0);
  const cell = await wasm(page, "web_cell_css_px");
  const tap = async () => {
    const x = await wasm(page, "web_grace_screen_x");
    const y = await wasm(page, "web_grace_screen_y");
    await page.mouse.click(box.x + x + cell, box.y + y);
  };
  const before = await wasm(page, "items_collected");
  await tap();
  expect(await wasm(page, "items_collected")).toBe(before);
  await wasm(page, "web_test_give", 7);
  await wasm(page, "lighter_set", 1);
  await page.waitForTimeout(100);
  expect(await wasm(page, "web_light_at", 79, 49)).toBe(1);
  await tap();
  expect(await wasm(page, "items_collected")).toBe(before + 1);
});

test("a hit makes the enemy angry", async ({ page }) => {
  await boot(page);
  expect(await wasm(page, "web_enemy_angry")).toBe(0);
  await wasm(page, "web_test_anger");
  expect(await wasm(page, "web_enemy_angry")).toBe(1);
});

test("a hit leaves Grace Hurt for 5 s, unable to move, then she waits", async ({ page }) => {
  await boot(page);
  await wasm(page, "web_test_enemy_place", 500, 8000); // keep the enemy far away
  await steer(page, "d", 400);
  await wasm(page, "web_test_hurt");
  expect(await wasm(page, "web_grace_mode")).toBe(4);
  await expect(page.getByRole("status", { name: "Grace: Hurt" })).toBeVisible();
  const x0 = await wasm(page, "player_px");
  // WASD does nothing while hurt; she doesn't move
  await page.keyboard.down("d");
  await page.waitForTimeout(600);
  await page.keyboard.up("d");
  expect(await wasm(page, "web_grace_mode")).toBe(4);
  expect(await wasm(page, "player_px")).toBeCloseTo(x0, 1);
  // Hurt over: Waiting (world live), not Paused
  await expect.poll(() => wasm(page, "web_grace_mode"), { timeout: 7000 }).toBe(3);
  expect(await wasm(page, "web_grace_hurt_ms")).toBe(0);
});

test("the X key triggers the interact in front of her (pickup)", async ({ page }) => {
  await boot(page);
  // stand just west of the Green Herb at (95, 8), facing east after a nudge
  await page.evaluate(() => window.Module?._web_test_grace_at?.(94, 8));
  await page.waitForTimeout(200);
  await steer(page, "d", 60); // face east without leaving the cell
  const before = await wasm(page, "items_collected");
  await page.keyboard.press("x");
  await expect.poll(() => wasm(page, "items_collected")).toBe(before + 1);
});

test("an item the Lighter has shown stays visible", async ({ page }) => {
  await boot(page);
  await wasm(page, "web_test_grace_at", 78, 49);
  await wasm(page, "web_test_give", 7);
  expect(await wasm(page, "web_item_visible_at", 79, 49)).toBe(0);
  await wasm(page, "lighter_set", 1);
  expect(await wasm(page, "web_item_visible_at", 79, 49)).toBe(1);
  await wasm(page, "lighter_set", 0);
  expect(await wasm(page, "web_light_at", 79, 49)).toBe(0);
  expect(await wasm(page, "web_item_visible_at", 79, 49)).toBe(1);
});

test("a typewriter save keeps the enemy where it was", async ({ page }) => {
  await boot(page);
  await wasm(page, "web_test_give", 3);
  await wasm(page, "web_test_enemy_place", 9050, 1230);
  expect(await wasm(page, "save_game", 0)).toBe(1);
  await wasm(page, "web_test_enemy_place", 2050, 4050);
  expect(await wasm(page, "load_game", 0)).toBe(1);
  expect(await wasm(page, "web_enemy_px")).toBeCloseTo(90.5, 3);
  expect(await wasm(page, "web_enemy_py")).toBeCloseTo(12.3, 3);
});

test("she can only throw standing still: moving blocks the aim, stopping allows it", async ({
  page,
}) => {
  await boot(page);
  expect(await wasm(page, "web_test_bottle_ready")).toBe(1);
  // moving: Q does nothing, the direct throw refuses
  await page.keyboard.down("d");
  await page.waitForTimeout(300);
  await page.keyboard.press("q");
  expect(await wasm(page, "web_test_aiming")).toBe(0);
  expect(await wasm(page, "web_throw_allowed")).toBe(0);
  await page.keyboard.up("d");
  await expect.poll(() => wasm(page, "web_grace_mode")).toBe(3);
  // stopped: the throw works and she stays Waiting
  let thrown = 0;
  for (const [dx, dy] of [[0, 2], [0, -2], [-2, 0], [2, 0], [0, 3], [0, -3]]) {
    const cx = await wasm(page, "player_cell_x");
    const cy = await wasm(page, "player_cell_y");
    if (await wasm(page, "web_throw_in_reach", cx + dx, cy + dy)) {
      thrown = await wasm(page, "web_test_throw_at", cx + dx, cy + dy);
      if (thrown) break;
    }
  }
  expect(thrown).toBe(1);
  expect(await wasm(page, "web_grace_mode")).toBe(3);
});

test("running blocks throwing until she stops and the latch clears", async ({ page }) => {
  await boot(page);
  expect(await wasm(page, "web_test_bottle_ready")).toBe(1);
  await page.keyboard.down("d");
  await page.keyboard.press("e");
  await expect.poll(() => wasm(page, "web_grace_mode")).toBe(2);
  expect(await wasm(page, "web_throw_allowed")).toBe(0);
  await page.keyboard.press("q");
  expect(await wasm(page, "web_test_aiming")).toBe(0);
  await page.keyboard.up("d");
  await expect.poll(() => wasm(page, "web_grace_mode")).toBe(3);
  expect(await wasm(page, "web_throw_allowed")).toBe(1);
});

test("items lit by a switch light stay visible after it is switched off", async ({ page }) => {
  await boot(page);
  await page.evaluate(() => window.Module?._web_test_grace_at?.(90, 58));
  await page.waitForTimeout(200);
  expect(await wasm(page, "web_item_visible_at", 88, 63)).toBe(1);
  expect(await wasm(page, "web_test_interact", 91, 58)).toBe(1);
  await page.waitForTimeout(200);
  expect(await wasm(page, "web_light_at", 88, 63)).toBe(0);
  expect(await wasm(page, "web_item_visible_at", 88, 63)).toBe(1);
});

test("a hit in Danger kills Grace and stops the timer", async ({ page }) => {
  await boot(page);
  await page.evaluate(() => window.Module?._web_test_hurt?.());
  await page.waitForTimeout(100);
  await page.evaluate(() => window.Module?._web_test_hurt?.());
  await page.waitForTimeout(100);
  await page.evaluate(() => window.Module?._web_test_hurt?.());
  await expect(page.getByText("Grace died")).toBeVisible();
  const t0 = await wasm(page, "run_elapsed_ms");
  await page.waitForTimeout(600);
  expect(await wasm(page, "run_elapsed_ms")).toBe(t0);
  expect(await wasm(page, "web_grace_dead")).toBe(1);
});

test("the fuse chain: gate stays shut without the fuse, opens with it, goal past it", async ({ page }) => {
  await boot(page);
  await page.evaluate(() => window.Module?._web_test_grace_at?.(8, 69));
  await page.waitForTimeout(200);
  expect(await wasm(page, "web_test_interact", 7, 69)).toBe(1);
  expect(await wasm(page, "web_goal_met")).toBe(0);
  await page.evaluate(() => window.Module?._web_test_give?.(6));
  expect(await wasm(page, "web_test_interact", 7, 69)).toBe(1);
  expect(await wasm(page, "web_goal_met")).toBe(1);
  await page.evaluate(() => window.Module?._web_test_grace_at?.(5, 74));
  await expect.poll(() => wasm(page, "web_level_complete")).toBe(1);
});

test("climbing: X climbs on after the delay, and X climbs off again", async ({ page }) => {
  await boot(page);
  // obstacle at (68,53): make it climbable with the test hook, stand west.
  await page.evaluate(() => window.Module?._web_test_grace_at?.(67, 53));
  await page.waitForTimeout(200);
  expect(await wasm(page, "web_test_set_climbable", 68, 53)).toBe(1);
  await steer(page, "d", 60); // face east, stay in her cell
  await page.waitForTimeout(250);
  await page.keyboard.press("x");
  await expect.poll(() => wasm(page, "web_on_top"), { timeout: 3000 }).toBe(1);
  // climb off with X again (facing decides the landing cell)
  await page.keyboard.press("x");
  await expect.poll(() => wasm(page, "web_on_top"), { timeout: 3000 }).toBe(0);
});
