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

test("clicking Grace does nothing mid-move; WASD and the E run latch drive her", async ({ page }) => {
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

  // click on her dot mid-run: her dot takes no input, she keeps running
  const at = await page.evaluate(() => ({
    x: window.Module?._web_grace_screen_x?.() ?? 0,
    y: window.Module?._web_grace_screen_y?.() ?? 0,
  }));
  await page.mouse.click(box.x + at.x, box.y + at.y);
  await page.waitForTimeout(250);
  expect(await mode()).toBe(2); // still Running: the click did nothing
  await page.keyboard.up("d");
  await expect.poll(mode).toBe(3);

  // moving again walks (the run latch cleared when she stopped)
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

test("Z opens Quick Equip (right-clicking Grace does not) and an active bottle can be thrown", async ({
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

  // right-clicking her dot does nothing: no Quick Equip
  await page.mouse.click(box.x + at.x, box.y + at.y, { button: "right" });
  await page.waitForTimeout(250);
  await expect(quick).toHaveCount(0);
  // Z is the desktop equip key
  await page.keyboard.press("z");
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

test("clicking an item, the X marker or Grace does nothing; X key still picks the item up", async ({
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
  // clicking just inside the herb's cell (where its X prompt shows): nothing
  await page.mouse.click(box.x + at.x + at.cell * 0.5 + 3, box.y + at.y);
  await page.waitForTimeout(250);
  expect(await page.evaluate(() => window.Module?._items_collected?.() ?? 0)).toBe(before);
  // clicking Grace herself: also nothing
  await page.mouse.click(box.x + at.x, box.y + at.y);
  await page.waitForTimeout(250);
  expect(await page.evaluate(() => window.Module?._items_collected?.() ?? 0)).toBe(before);
  expect(await page.evaluate(() => window.Module?._web_grace_mode?.())).toBe(3);
  // the X key is the interact path, and it works
  await page.keyboard.press("x");
  await expect
    .poll(() => page.evaluate(() => window.Module?._items_collected?.() ?? 0))
    .toBe(before + 1);
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

test("Grace's dot takes no input; Z toggles Quick Equip and there is no action menu", async ({
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
  const at = await page.evaluate(() => ({
    x: window.Module?._web_grace_screen_x?.() ?? 0,
    y: window.Module?._web_grace_screen_y?.() ?? 0,
  }));
  const state = () =>
    page.evaluate(() => [
      window.Module?._web_grace_menu_open?.(),
      window.Module?._web_quick_open?.(),
    ]);
  expect(await state()).toEqual([0, 0]);
  // right click, left click and a long press on her dot: all nothing
  await page.mouse.click(box.x + at.x, box.y + at.y, { button: "right" });
  await expect.poll(state).toEqual([0, 0]);
  await page.mouse.click(box.x + at.x, box.y + at.y);
  await expect.poll(state).toEqual([0, 0]);
  await page.mouse.move(box.x + at.x, box.y + at.y);
  await page.mouse.down();
  await page.waitForTimeout(800); // a long press used to open Quick Equip
  await page.mouse.up();
  await expect.poll(state).toEqual([0, 0]);
  expect(await page.evaluate(() => window.Module?._web_grace_mode?.())).toBe(3);
  // Z toggles Quick Equip instead
  await page.keyboard.press("z");
  await expect.poll(state).toEqual([0, 1]);
  await page.keyboard.press("z");
  await expect.poll(state).toEqual([0, 0]);
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
  await boot(page);
  await wasm(page, "web_test_give", 7);
  await page.keyboard.press("z");
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
  await boot(page);
  // the herb at (79,49) has no light near it
  await wasm(page, "web_test_grace_at", 78, 49);
  await page.waitForTimeout(200);
  expect(await wasm(page, "web_light_at", 79, 49)).toBe(0);
  // interact is the X key now (map clicks on the item do nothing)
  const tap = async () => {
    await page.keyboard.press("x");
    await page.waitForTimeout(120);
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

test("throwing on the move: Q aims while walking and the click throws (hitscan)", async ({
  page,
}) => {
  const box = await boot(page);
  expect(await wasm(page, "web_test_bottle_ready")).toBe(1);
  // walking east, Q enters aim without stopping her
  await page.keyboard.down("d");
  await expect.poll(() => wasm(page, "web_grace_mode")).toBe(1);
  await page.keyboard.press("q");
  expect(await wasm(page, "web_test_aiming")).toBe(1);
  await page.waitForTimeout(300);
  expect(await wasm(page, "web_grace_mode")).toBe(1); // still walking, capped
  // aim east with the mouse and click: the bottle goes, aim exits
  const gx = await wasm(page, "web_grace_screen_x");
  const gy = await wasm(page, "web_grace_screen_y");
  const cell = await wasm(page, "web_cell_css_px");
  await page.mouse.move(box.x + gx + cell * 3, box.y + gy);
  await page.waitForTimeout(120);
  await page.mouse.click(box.x + gx + cell * 3, box.y + gy);
  await page.keyboard.up("d");
  expect(await wasm(page, "web_impact_age_ms")).toBeGreaterThanOrEqual(0);
  expect(await wasm(page, "web_test_aiming")).toBe(0);
});

test("hitscan: a clear ray hits the enemy, a door in between blocks it", async ({ page }) => {
  await boot(page);
  // Grace north of the closed door at (69,49); enemy first on her side...
  await wasm(page, "web_test_grace_at", 70, 48);
  await page.waitForTimeout(150);
  await wasm(page, "web_test_enemy_place", 7050, 4750);
  expect(await wasm(page, "web_test_hitscan", 0, -100, 300)).toBe(1);
  // ...then south of the door: the ray stops at the door
  await wasm(page, "web_test_enemy_place", 7050, 5050);
  expect(await wasm(page, "web_test_hitscan", 0, 100, 300)).toBe(2);
});

test("aiming caps a run to a walk; un-aiming restores the run latch", async ({ page }) => {
  await boot(page);
  expect(await wasm(page, "web_test_bottle_ready")).toBe(1);
  await page.keyboard.down("d");
  await page.keyboard.press("e");
  await expect.poll(() => wasm(page, "web_grace_mode")).toBe(2); // running
  await page.keyboard.press("q");
  await expect.poll(() => wasm(page, "web_grace_mode")).toBe(1); // capped to walk while aiming
  expect(await wasm(page, "web_throw_allowed")).toBe(1);
  await page.keyboard.press("q"); // aim off
  await expect.poll(() => wasm(page, "web_grace_mode")).toBe(2); // latch still there
  await page.keyboard.up("d");
  await expect.poll(() => wasm(page, "web_grace_mode")).toBe(3);
});

type Box = { x: number; y: number; width: number; height: number };

function boxesOverlap(a: Box, b: Box) {
  return a.x < b.x + b.width && a.x + a.width > b.x && a.y < b.y + b.height && a.y + a.height > b.y;
}

async function phonePage(
  browser: import("@playwright/test").Browser,
  viewport: { width: number; height: number },
) {
  const context = await browser.newContext({
    viewport,
    deviceScaleFactor: 3,
    isMobile: true,
    hasTouch: true,
  });
  const page = await context.newPage();
  await page.goto("/");
  await page.waitForFunction(() => (window.Module?._grid_width?.() ?? 0) > 0, null, {
    timeout: 30_000,
  });
  return { context, page };
}

async function touchSession(page: Page, context: import("@playwright/test").BrowserContext) {
  const cdp = await context.newCDPSession(page);
  return (type: string, points: { x: number; y: number; id: number }[]) =>
    cdp.send("Input.dispatchTouchEvent", { type, touchPoints: points });
}

// Both sticks visible, inside the map, clear of each other and of the
// top-of-map furniture (Grace status, timer chip, Inventory/Pause), and
// not eating the map: height is asserted as a share of the map height.
async function expectStickLayout(page: Page, maxMapFraction: number) {
  await page.evaluate(() => (window as any).Module?._web_test_bottle_ready?.());
  const move = await page.locator(".stick:not(.aim-stick)").boundingBox();
  const aim = await page.locator(".aim-stick").boundingBox();
  const map = await page.locator(".pane-map").boundingBox();
  const status = await page.locator(".grace-status").boundingBox();
  const timer = await page.locator(".timer-chip").boundingBox();
  const actions = await page.locator(".map-actions").boundingBox();
  expect(move).not.toBeNull();
  expect(aim).not.toBeNull();
  expect(map).not.toBeNull();
  const m = move as Box;
  const a = aim as Box;
  const mp = map as Box;
  expect(m.x).toBeLessThan(a.x); // move left, aim right
  expect(boxesOverlap(m, a)).toBe(false);
  for (const stick of [m, a]) {
    expect(stick.height).toBeGreaterThanOrEqual(80); // still a usable touch target
    expect(stick.height / mp.height).toBeLessThanOrEqual(maxMapFraction);
    expect(stick.x).toBeGreaterThanOrEqual(mp.x - 1);
    expect(stick.y).toBeGreaterThanOrEqual(mp.y - 1);
    expect(stick.x + stick.width).toBeLessThanOrEqual(mp.x + mp.width + 1);
    expect(stick.y + stick.height).toBeLessThanOrEqual(mp.y + mp.height + 1);
    for (const top of [status, timer, actions]) {
      expect(top).not.toBeNull();
      expect(boxesOverlap(stick, top as Box)).toBe(false);
    }
  }
  return { move: m, aim: a, map: mp };
}

async function twinStickThrow(page: Page, context: import("@playwright/test").BrowserContext) {
  const { move, aim } = await expectStickLayout(page, 0.35);
  const mc = { x: move.x + move.width / 2, y: move.y + move.height / 2 };
  const ac = { x: aim.x + aim.width / 2, y: aim.y + aim.height / 2 };
  const touch = await touchSession(page, context);
  const push = Math.round(move.width * 0.38);
  await touch("touchStart", [
    { ...mc, id: 1 },
    { ...ac, id: 2 },
  ]);
  await touch("touchMove", [
    { x: mc.x + push, y: mc.y, id: 1 },
    { x: ac.x + push, y: ac.y, id: 2 },
  ]);
  await page.waitForTimeout(600);
  expect(await page.evaluate(() => (window as any).Module?._web_grace_mode?.())).toBe(1);
  expect(await page.evaluate(() => (window as any).Module?._web_test_aiming?.())).toBe(1);
  // release only the aim stick: she throws, still moving on the left stick
  await touch("touchEnd", [{ x: ac.x + push, y: ac.y, id: 2 }]);
  await expect
    .poll(() => page.evaluate(() => (window as any).Module?._web_impact_age_ms?.() ?? -1))
    .toBeGreaterThanOrEqual(0);
  await touch("touchEnd", [{ x: mc.x + push, y: mc.y, id: 1 }]);
}

test("phone twin sticks: left moves, right aims, releasing the aim throws while moving", async ({
  browser,
}) => {
  const { context, page } = await phonePage(browser, { width: 390, height: 844 });
  // portrait keeps the full-size stick: 128px, a small share of the tall map
  const { move } = await expectStickLayout(page, 0.2);
  expect(move.height).toBe(132); // 128px content + 2px border each side
  await twinStickThrow(page, context);
  await context.close();
});

test("phone twin sticks in landscape: shrunk sticks still move, aim and throw", async ({
  browser,
}) => {
  const { context, page } = await phonePage(browser, { width: 844, height: 390 });
  // landscape: the stick shrinks off the short dimension instead of
  // covering ~40% of the map height
  const { move } = await expectStickLayout(page, 0.32);
  expect(move.height).toBeLessThan(132);
  expect(move.height).toBeGreaterThanOrEqual(84);
  await twinStickThrow(page, context);
  await context.close();
});

test("phone sticks on a short landscape screen (360px high) stay proportionate", async ({
  browser,
}) => {
  const { context, page } = await phonePage(browser, { width: 780, height: 360 });
  const { move } = await expectStickLayout(page, 0.32);
  expect(move.height).toBeGreaterThanOrEqual(84);
  await context.close();
});

test("phone stick centre tap: left tap does not interact, right tap with jitter does", async ({
  browser,
}) => {
  const { context, page } = await phonePage(browser, { width: 390, height: 844 });
  // stand just west of the Green Herb at (95, 8)
  await page.evaluate(() => (window as any).Module?._web_test_grace_at?.(94, 8));
  await page.waitForTimeout(250);
  const touch = await touchSession(page, context);
  const centreOf = async (selector: string) => {
    const box = await page.locator(selector).boundingBox();
    expect(box).not.toBeNull();
    return { x: (box?.x ?? 0) + (box?.width ?? 0) / 2, y: (box?.y ?? 0) + (box?.height ?? 0) / 2 };
  };
  const before = await page.evaluate(() => (window as any).Module?._items_collected?.() ?? 0);
  const px0 = await page.evaluate(() => (window as any).Module?._player_px?.() ?? 0);
  // LEFT stick centre tap (with a few px of jitter): no interact any more
  const left = await centreOf(".stick:not(.aim-stick)");
  await touch("touchStart", [{ ...left, id: 1 }]);
  await touch("touchMove", [{ x: left.x + 4, y: left.y - 2, id: 1 }]);
  await touch("touchEnd", [{ x: left.x + 4, y: left.y - 2, id: 1 }]);
  await page.waitForTimeout(300);
  expect(await page.evaluate(() => (window as any).Module?._items_collected?.() ?? 0)).toBe(before);
  // RIGHT stick centre tap with jitter: interact fires, no movement emitted
  const right = await centreOf(".aim-stick");
  await touch("touchStart", [{ ...right, id: 1 }]);
  await touch("touchMove", [{ x: right.x + 8, y: right.y - 4, id: 1 }]);
  await touch("touchMove", [{ x: right.x + 14, y: right.y + 5, id: 1 }]);
  await touch("touchEnd", [{ x: right.x + 14, y: right.y + 5, id: 1 }]);
  await expect
    .poll(() => page.evaluate(() => (window as any).Module?._items_collected?.() ?? 0))
    .toBe(before + 1); // the tap fired interact and picked the herb up
  await page.waitForTimeout(300);
  expect(await page.evaluate(() => (window as any).Module?._player_px?.() ?? 0)).toBeCloseTo(px0, 1);
  expect(await page.evaluate(() => (window as any).Module?._web_grace_mode?.())).toBe(3);
  await context.close();
});

test("phone stick: drift up to the dead zone does not move her, a push beyond it walks", async ({
  browser,
}) => {
  const { context, page } = await phonePage(browser, { width: 390, height: 844 });
  const stick = await page.locator(".stick:not(.aim-stick)").boundingBox();
  expect(stick).not.toBeNull();
  const c = {
    x: (stick?.x ?? 0) + (stick?.width ?? 0) / 2,
    y: (stick?.y ?? 0) + (stick?.height ?? 0) / 2,
  };
  const px0 = await page.evaluate(() => (window as any).Module?._player_px?.() ?? 0);
  const touch = await touchSession(page, context);
  await touch("touchStart", [{ ...c, id: 1 }]);
  await touch("touchMove", [{ x: c.x + 16, y: c.y, id: 1 }]); // inside the ~18px dead zone
  await page.waitForTimeout(250); // under the 450ms hold-centre Pause gesture
  expect(await page.evaluate(() => (window as any).Module?._web_grace_mode?.())).toBe(3);
  expect(await page.evaluate(() => (window as any).Module?._player_px?.() ?? 0)).toBeCloseTo(px0, 1);
  // a deliberate push just past the dead zone (still in the walk band)
  await touch("touchMove", [{ x: c.x + 24, y: c.y, id: 1 }]);
  await expect
    .poll(() => page.evaluate(() => (window as any).Module?._web_grace_mode?.()))
    .toBe(1); // Walking
  await touch("touchEnd", [{ x: c.x + 24, y: c.y, id: 1 }]);
  await context.close();
});

test("phone aim stick: centre jitter and a dead-zone release cancel instead of throwing", async ({
  browser,
}) => {
  const { context, page } = await phonePage(browser, { width: 390, height: 844 });
  await page.evaluate(() => (window as any).Module?._web_test_bottle_ready?.());
  const aim = await page.locator(".aim-stick").boundingBox();
  expect(aim).not.toBeNull();
  const c = { x: (aim?.x ?? 0) + (aim?.width ?? 0) / 2, y: (aim?.y ?? 0) + (aim?.height ?? 0) / 2 };
  const touch = await touchSession(page, context);
  // centre touch with jitter only: never aims, release throws nothing
  await touch("touchStart", [{ ...c, id: 1 }]);
  await touch("touchMove", [{ x: c.x + 4, y: c.y + 3, id: 1 }]);
  await touch("touchEnd", [{ x: c.x + 4, y: c.y + 3, id: 1 }]);
  await page.waitForTimeout(200);
  expect(await page.evaluate(() => (window as any).Module?._web_test_aiming?.())).toBe(0);
  expect(await page.evaluate(() => (window as any).Module?._web_impact_age_ms?.() ?? -1)).toBe(-1);
  // push out to aim, pull back into the dead zone, then release: cancels
  await touch("touchStart", [{ ...c, id: 1 }]);
  await touch("touchMove", [{ x: c.x + 32, y: c.y, id: 1 }]);
  await expect
    .poll(() => page.evaluate(() => (window as any).Module?._web_test_aiming?.()))
    .toBe(1);
  await touch("touchMove", [{ x: c.x + 2, y: c.y, id: 1 }]);
  await touch("touchEnd", [{ x: c.x + 2, y: c.y, id: 1 }]);
  await page.waitForTimeout(200);
  expect(await page.evaluate(() => (window as any).Module?._web_test_aiming?.())).toBe(0);
  expect(await page.evaluate(() => (window as any).Module?._web_impact_age_ms?.() ?? -1)).toBe(-1);
  await context.close();
});

test("phone aim stick: drift up to the new dead zone neither aims nor throws", async ({
  browser,
}) => {
  const { context, page } = await phonePage(browser, { width: 844, height: 390 });
  await page.evaluate(() => (window as any).Module?._web_test_bottle_ready?.());
  const aim = await page.locator(".aim-stick").boundingBox();
  expect(aim).not.toBeNull();
  const c = { x: (aim?.x ?? 0) + (aim?.width ?? 0) / 2, y: (aim?.y ?? 0) + (aim?.height ?? 0) / 2 };
  const touch = await touchSession(page, context);
  // on the shrunk landscape stick the dead zone is ~13px: drift 11px
  await touch("touchStart", [{ ...c, id: 1 }]);
  await touch("touchMove", [{ x: c.x + 11, y: c.y - 3, id: 1 }]);
  await page.waitForTimeout(250); // under the 450ms hold-to-equip threshold
  expect(await page.evaluate(() => (window as any).Module?._web_test_aiming?.())).toBe(0);
  await touch("touchEnd", [{ x: c.x + 11, y: c.y - 3, id: 1 }]);
  await page.waitForTimeout(200);
  expect(await page.evaluate(() => (window as any).Module?._web_impact_age_ms?.() ?? -1)).toBe(-1);
  // beyond the dead zone it aims, and releasing throws (existing behaviour)
  await touch("touchStart", [{ ...c, id: 1 }]);
  await touch("touchMove", [{ x: c.x + 28, y: c.y, id: 1 }]);
  await expect
    .poll(() => page.evaluate(() => (window as any).Module?._web_test_aiming?.()))
    .toBe(1);
  await touch("touchEnd", [{ x: c.x + 28, y: c.y, id: 1 }]);
  await expect
    .poll(() => page.evaluate(() => (window as any).Module?._web_impact_age_ms?.() ?? -1))
    .toBeGreaterThanOrEqual(0);
  await context.close();
});

test("phone right stick is always shown: greyed with no bottle, no aim or throw from it", async ({
  browser,
}) => {
  const { context, page } = await phonePage(browser, { width: 390, height: 844 });
  const aimStick = page.locator(".aim-stick");
  await expect(aimStick).toBeVisible();
  await expect(aimStick).toHaveAttribute("data-inactive", "true");
  const box = await aimStick.boundingBox();
  expect(box).not.toBeNull();
  const c = { x: (box?.x ?? 0) + (box?.width ?? 0) / 2, y: (box?.y ?? 0) + (box?.height ?? 0) / 2 };
  const touch = await touchSession(page, context);
  await touch("touchStart", [{ ...c, id: 1 }]);
  await touch("touchMove", [{ x: c.x + 34, y: c.y, id: 1 }]);
  await page.waitForTimeout(300);
  expect(await page.evaluate(() => (window as any).Module?._web_test_aiming?.())).toBe(0);
  await touch("touchEnd", [{ x: c.x + 34, y: c.y, id: 1 }]);
  await page.waitForTimeout(200);
  expect(await page.evaluate(() => (window as any).Module?._web_impact_age_ms?.() ?? -1)).toBe(-1);
  // and once a bottle is active the same stick is live, not greyed
  await page.evaluate(() => (window as any).Module?._web_test_bottle_ready?.());
  await expect(aimStick).not.toHaveAttribute("data-inactive", "true");
  await context.close();
});

test("phone right stick: tap interacts, hold opens Quick Equip without interacting", async ({
  browser,
}) => {
  const { context, page } = await phonePage(browser, { width: 390, height: 844 });
  await page.evaluate(() => (window as any).Module?._web_test_grace_at?.(94, 8)); // herb to the east
  await page.evaluate(() => (window as any).Module?._web_test_give?.(7)); // a Lighter slot exists
  await page.waitForTimeout(250);
  const box = await page.locator(".aim-stick").boundingBox();
  expect(box).not.toBeNull();
  const c = { x: (box?.x ?? 0) + (box?.width ?? 0) / 2, y: (box?.y ?? 0) + (box?.height ?? 0) / 2 };
  const touch = await touchSession(page, context);
  const items = () => page.evaluate(() => (window as any).Module?._items_collected?.() ?? 0);
  const quick = () => page.evaluate(() => (window as any).Module?._web_quick_open?.() ?? 0);
  const before = await items();
  // a quick tap does not open the menu (it interacts: covered by the tap test)
  await touch("touchStart", [{ ...c, id: 1 }]);
  await touch("touchEnd", [{ ...c, id: 1 }]);
  await page.waitForTimeout(200);
  expect(await quick()).toBe(0);
  expect(await items()).toBe(before + 1); // that tap was the interact
  // a hold (>=450ms) opens Quick Equip and must NOT also interact
  await touch("touchStart", [{ ...c, id: 1 }]);
  await expect.poll(quick, { timeout: 3000 }).toBe(1);
  await touch("touchEnd", [{ ...c, id: 1 }]);
  await page.waitForTimeout(200);
  expect(await items()).toBe(before + 1); // no second interact from the hold/release
  expect(await quick()).toBe(1); // menu stays open after the opening hold
  // a centre tap while open cancels/closes it, again without interacting
  await touch("touchStart", [{ ...c, id: 1 }]);
  await touch("touchEnd", [{ ...c, id: 1 }]);
  await expect.poll(quick).toBe(0);
  expect(await items()).toBe(before + 1);
  await context.close();
});

test("phone Quick Equip radial: hold, point the right stick north, release equips the Lighter", async ({
  browser,
}) => {
  const { context, page } = await phonePage(browser, { width: 390, height: 844 });
  await page.evaluate(() => (window as any).Module?._web_test_bottle_ready?.());
  await page.evaluate(() => (window as any).Module?._web_test_give?.(7));
  const box = await page.locator(".aim-stick").boundingBox();
  expect(box).not.toBeNull();
  const c = { x: (box?.x ?? 0) + (box?.width ?? 0) / 2, y: (box?.y ?? 0) + (box?.height ?? 0) / 2 };
  const touch = await touchSession(page, context);
  await touch("touchStart", [{ ...c, id: 1 }]);
  await expect
    .poll(() => page.evaluate(() => (window as any).Module?._web_quick_open?.() ?? 0), { timeout: 3000 })
    .toBe(1);
  // same touch, now point north: the Lighter slot highlights, no aiming
  await touch("touchMove", [{ x: c.x, y: c.y - 36, id: 1 }]);
  const lighter = page
    .getByRole("group", { name: "Quick Equip" })
    .getByRole("button", { name: "Lighter" });
  await expect(lighter).toHaveAttribute("data-highlighted", "true");
  expect(await page.evaluate(() => (window as any).Module?._web_test_aiming?.())).toBe(0);
  // release confirms the pointed slot: Lighter toggles on, nothing throws
  await touch("touchEnd", [{ x: c.x, y: c.y - 36, id: 1 }]);
  await expect
    .poll(() => page.evaluate(() => (window as any).Module?._lighter_value?.() ?? 0))
    .toBe(1);
  expect(await page.evaluate(() => (window as any).Module?._web_impact_age_ms?.() ?? -1)).toBe(-1);
  expect(await page.evaluate(() => (window as any).Module?._web_bottle_active?.() ?? 0)).toBe(1);
  await context.close();
});

test("desktop Z opens Quick Equip and the mouse direction selects radially", async ({ page }) => {
  await boot(page);
  await page.evaluate(() => window.Module?._web_test_give?.(7));
  await page.keyboard.press("z");
  await expect
    .poll(() => page.evaluate(() => (window as any).Module?._web_quick_open?.() ?? 0))
    .toBe(1);
  const lighter = page
    .getByRole("group", { name: "Quick Equip" })
    .getByRole("button", { name: "Lighter" });
  await expect(lighter).toBeVisible();
  // point the mouse at the north slot: it highlights before any click
  const slotBox = await lighter.boundingBox();
  expect(slotBox).not.toBeNull();
  await page.mouse.move(
    (slotBox?.x ?? 0) + (slotBox?.width ?? 0) / 2,
    (slotBox?.y ?? 0) + (slotBox?.height ?? 0) / 2,
  );
  await expect(lighter).toHaveAttribute("data-highlighted", "true");
  await lighter.click();
  expect(await page.evaluate(() => (window as any).Module?._lighter_value?.() ?? 0)).toBe(1);
  // Z again closes it
  await page.keyboard.press("z");
  await expect
    .poll(() => page.evaluate(() => (window as any).Module?._web_quick_open?.() ?? 0))
    .toBe(0);
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

test("menu nav: arrows move the green selection in the Pause menu and Enter accepts", async ({
  page,
}) => {
  await boot(page);
  await page.getByRole("button", { name: "Pause", exact: true }).click();
  const items = page.locator(".pause-item");
  await expect(items.first()).toHaveAttribute("data-menu-selected", "true");
  const px0 = await wasm(page, "player_px");
  await page.keyboard.press("ArrowDown");
  await expect(items.nth(1)).toHaveAttribute("data-menu-selected", "true");
  await expect(items.first()).not.toHaveAttribute("data-menu-selected", "true");
  // Grace does not move while the menu is navigated
  await page.keyboard.press("d");
  await page.waitForTimeout(250);
  await page.keyboard.up("d");
  expect(await wasm(page, "player_px")).toBeCloseTo(px0, 1);
  // walk the selection down to Resume and accept it: the menu closes
  const count = await items.count();
  for (let i = 1; i < count - 1; i++) {
    await page.keyboard.press("ArrowDown");
  }
  await expect(items.last()).toHaveAttribute("data-menu-selected", "true");
  await page.keyboard.press("Enter");
  await expect(page.locator(".pause-menu")).toHaveCount(0);
});

test("menu nav: arrows reach Load Game and the save/load menu, Close accepts", async ({ page }) => {
  await boot(page);
  await page.getByRole("button", { name: "Pause", exact: true }).click();
  const load = page.getByRole("button", { name: "Load Game", exact: true });
  // step down until Load Game is selected, then accept
  for (let i = 0; i < 4; i++) {
    if ((await load.getAttribute("data-menu-selected")) === "true") break;
    await page.keyboard.press("ArrowDown");
  }
  await expect(load).toHaveAttribute("data-menu-selected", "true");
  await page.keyboard.press("Enter");
  const saveMenu = page.locator(".save-menu");
  await expect(saveMenu).toBeVisible();
  const close = saveMenu.getByRole("button", { name: "Close" });
  // with no ribbon and no saves, Close is the only live item
  await expect(close).toHaveAttribute("data-menu-selected", "true");
  await page.keyboard.press("Enter");
  await expect(saveMenu).toHaveCount(0);
});

test("menu nav: arrows and Enter work in the Inventory overlay (tabs)", async ({ page }) => {
  await boot(page);
  await page.getByRole("button", { name: "Inventory", exact: true }).click();
  const overlay = page.getByRole("dialog", { name: "Inventory" });
  await expect(overlay.locator("[data-menu-selected='true']")).toHaveCount(1);
  // move until a tab is selected, accept it, and it becomes the active tab
  let chosen = "";
  for (let i = 0; i < 8 && !chosen; i++) {
    const selected = overlay.locator(".tab[data-menu-selected='true']");
    if ((await selected.count()) > 0) {
      chosen = (await selected.textContent()) ?? "";
    } else {
      await page.keyboard.press("ArrowDown");
    }
  }
  expect(chosen).not.toBe("");
  await page.keyboard.press("Enter");
  await expect(overlay.locator(".tab[data-active='true']")).toHaveText(chosen.trim());
});

test("menu nav: the Game Over menu is arrow-navigable and Enter restarts", async ({ page }) => {
  await boot(page);
  await page.evaluate(() => window.Module?._web_test_hurt?.());
  await page.waitForTimeout(100);
  await page.evaluate(() => window.Module?._web_test_hurt?.());
  await page.waitForTimeout(100);
  await page.evaluate(() => window.Module?._web_test_hurt?.());
  await expect(page.getByText("Grace died")).toBeVisible();
  const items = page.locator(".game-over .pause-item");
  await expect(items.first()).toHaveAttribute("data-menu-selected", "true");
  const restart = page.getByRole("button", { name: "Restart level" });
  for (let i = 0; i < 4; i++) {
    if ((await restart.getAttribute("data-menu-selected")) === "true") break;
    await page.keyboard.press("ArrowDown");
  }
  await page.keyboard.press("Enter");
  await expect(page.getByText("Grace died")).toHaveCount(0);
  expect(await wasm(page, "web_grace_dead")).toBe(0);
});

test("menu nav on a phone: stick flick steps the Pause menu, right-centre tap accepts", async ({
  browser,
}) => {
  const { context, page } = await phonePage(browser, { width: 390, height: 844 });
  const px0 = await page.evaluate(() => (window as any).Module?._player_px?.() ?? 0);
  await page.getByRole("button", { name: "Pause", exact: true }).click();
  const items = page.locator(".pause-item");
  await expect(items.first()).toHaveAttribute("data-menu-selected", "true");
  const touch = await touchSession(page, context);
  const centreOf = async (selector: string) => {
    const box = await page.locator(selector).boundingBox();
    return { x: (box?.x ?? 0) + (box?.width ?? 0) / 2, y: (box?.y ?? 0) + (box?.height ?? 0) / 2 };
  };
  // flick the move stick down: one step, selection moves once
  const left = await centreOf(".stick:not(.aim-stick)");
  await touch("touchStart", [{ ...left, id: 1 }]);
  await touch("touchMove", [{ x: left.x, y: left.y + 30, id: 1 }]);
  await touch("touchMove", [{ x: left.x, y: left.y + 3, id: 1 }]);
  await touch("touchEnd", [{ x: left.x, y: left.y + 3, id: 1 }]);
  await expect(items.nth(1)).toHaveAttribute("data-menu-selected", "true");
  // Grace never moved behind the menu
  expect(await page.evaluate(() => (window as any).Module?._player_px?.() ?? 0)).toBeCloseTo(px0, 1);
  // right-stick centre tap accepts (that item is Load Game)
  const right = await centreOf(".aim-stick");
  await touch("touchStart", [{ ...right, id: 1 }]);
  await touch("touchEnd", [{ ...right, id: 1 }]);
  await expect(page.locator(".save-menu")).toBeVisible();
  await context.close();
});

test("menu shortcuts: P and Esc toggle Pause, I toggles Inventory, no mouse needed", async ({
  page,
}) => {
  await boot(page);
  const px0 = await wasm(page, "player_px");
  await page.keyboard.press("p");
  await expect(page.locator(".pause-menu")).toBeVisible();
  await page.keyboard.press("p");
  await expect(page.locator(".pause-menu")).toHaveCount(0);
  // Esc with nothing open opens Pause (existing toggle), Esc again closes
  await page.keyboard.press("Escape");
  await expect(page.locator(".pause-menu")).toBeVisible();
  await page.keyboard.press("Escape");
  await expect(page.locator(".pause-menu")).toHaveCount(0);
  // I opens and closes the Inventory; P does nothing over it
  await page.keyboard.press("i");
  const overlay = page.getByRole("dialog", { name: "Inventory" });
  await expect(overlay).toBeVisible();
  await page.keyboard.press("p");
  await expect(overlay).toBeVisible();
  await expect(page.locator(".pause-menu")).toHaveCount(0);
  await page.keyboard.press("i");
  await expect(overlay).toHaveCount(0);
  expect(await wasm(page, "player_px")).toBeCloseTo(px0, 1);
});

test("menu shortcuts on a phone: hold left centre toggles Pause, double-tap toggles Inventory", async ({
  browser,
}) => {
  const { context, page } = await phonePage(browser, { width: 390, height: 844 });
  const px0 = await page.evaluate(() => (window as any).Module?._player_px?.() ?? 0);
  const touch = await touchSession(page, context);
  const leftBox = await page.locator(".stick:not(.aim-stick)").boundingBox();
  const left = {
    x: (leftBox?.x ?? 0) + (leftBox?.width ?? 0) / 2,
    y: (leftBox?.y ?? 0) + (leftBox?.height ?? 0) / 2,
  };
  // a single centre tap opens nothing
  await touch("touchStart", [{ ...left, id: 1 }]);
  await touch("touchEnd", [{ ...left, id: 1 }]);
  await page.waitForTimeout(150);
  await expect(page.locator(".pause-menu")).toHaveCount(0);
  await expect(page.getByRole("dialog", { name: "Inventory" })).toHaveCount(0);
  // hold the centre: Pause opens, Grace never moved
  await touch("touchStart", [{ ...left, id: 1 }]);
  await expect(page.locator(".pause-menu")).toBeVisible();
  await touch("touchEnd", [{ ...left, id: 1 }]);
  expect(await page.evaluate(() => (window as any).Module?._player_px?.() ?? 0)).toBeCloseTo(px0, 1);
  // hold again: Pause closes (the stick is reachable behind the menu)
  await touch("touchStart", [{ ...left, id: 1 }]);
  await expect(page.locator(".pause-menu")).toHaveCount(0);
  await touch("touchEnd", [{ ...left, id: 1 }]);
  // double-tap the centre: Inventory opens, and again closes
  const doubleTap = async () => {
    await touch("touchStart", [{ ...left, id: 1 }]);
    await touch("touchEnd", [{ ...left, id: 1 }]);
    await page.waitForTimeout(80);
    await touch("touchStart", [{ ...left, id: 1 }]);
    await touch("touchEnd", [{ ...left, id: 1 }]);
  };
  await doubleTap();
  await expect(page.getByRole("dialog", { name: "Inventory" })).toBeVisible();
  await page.waitForTimeout(500); // outlast any pending double-tap pairing
  await doubleTap();
  await expect(page.getByRole("dialog", { name: "Inventory" })).toHaveCount(0);
  expect(await page.evaluate(() => (window as any).Module?._player_px?.() ?? 0)).toBeCloseTo(px0, 1);
  await context.close();
});
