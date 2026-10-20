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

  expect(grid.cols).toBe(184);
  expect(grid.rows).toBe(252);
  expect(grid.cellX).toBe(159); // Room 203
  expect(grid.cellY).toBe(68);
  expect(grid.replay).toBeGreaterThan(0);
  // the whole starting section (Room 203, 9x9 floor) is revealed as one chunk
  expect(grid.revealed).toBeGreaterThan(60);
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

test("clicking an item, the X marker or Grace does nothing; X no longer interacts, Space picks the item up", async ({
  page,
}) => {
  await page.goto("/");
  await page.waitForFunction(() => (window.Module?._grid_width?.() ?? 0) > 0, null, {
    timeout: 30_000,
  });
  // stand just west of a lit Green Herb (found by type, not coordinates)
  const herb = await findItem(page, 2, 0, 0);
  await page.evaluate(
    ([hx, hy]) => window.Module?._web_test_grace_at?.(hx, hy),
    [herb.x - 1, herb.y] as const,
  );
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
  // X is unbound now: it must not interact
  await page.keyboard.press("x");
  await page.waitForTimeout(250);
  expect(await page.evaluate(() => window.Module?._items_collected?.() ?? 0)).toBe(before);
  // Space is the interact/confirm key, and it works
  await page.keyboard.press(" ");
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

// Packed test-hook result: (x << 16) | (y << 8) | extra.
function unpack(v: number) {
  return { x: v >> 16, y: (v >> 8) & 0xff, extra: v & 0xff };
}

// Item types: bottle 0, coin 1, herb 2, ink_ribbon 3, screwdriver 4,
// injector 5, fuse 6, lighter 7, cherub_key 8. dark=1 finds unlit items.
async function findItem(page: Page, item: number, nth: number, dark: number) {
  const v = await wasm(page, "web_test_find_item", item, nth, dark);
  expect(v).toBeGreaterThanOrEqual(0);
  return unpack(v);
}

// The nth item of a type, lit or not.
async function findAnyItem(page: Page, item: number, nth: number) {
  let v = await wasm(page, "web_test_find_item", item, nth, 0);
  if (v < 0) v = await wasm(page, "web_test_find_item", item, nth, 1);
  expect(v).toBeGreaterThanOrEqual(0);
  return unpack(v);
}

async function findObject(page: Page, kind: number, nth: number) {
  const v = await wasm(page, "web_test_find_object", kind, nth);
  expect(v).toBeGreaterThanOrEqual(0);
  return unpack(v);
}

async function findDoor(page: Page, mode: number) {
  const v = await wasm(page, "web_test_find_door", mode);
  expect(v).toBeGreaterThanOrEqual(0);
  return unpack(v);
}

// The cell before a door, the key that walks through it, and the axis
// that shows whether she crossed (extra = 1 for a horizontal door).
function approach(d: { x: number; y: number; extra: number }) {
  return d.extra
    ? { x: d.x, y: d.y - 1, key: "s", axis: "player_cell_y", before: d.y - 1, past: d.y }
    : { x: d.x - 1, y: d.y, key: "d", axis: "player_cell_x", before: d.x - 1, past: d.x };
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
  // put the enemy 3 cells west of Grace, in the open (Room 203 is at the
  // map's north-east corner, under the HUD buttons to her east)
  const gx = await wasm(page, "player_cell_x");
  const gy = await wasm(page, "player_cell_y");
  await wasm(page, "web_test_enemy_place", (gx - 3) * 100 + 50, gy * 100 + 80);
  await page.keyboard.press("q");
  await expect.poll(() => wasm(page, "web_test_aiming")).toBe(1);
  // the world is live: track the enemy with the mouse until the reticle snaps
  await expect
    .poll(
      async () => {
        await wasm(page, "web_test_enemy_place", (gx - 3) * 100 + 50, gy * 100 + 80);
        const ex = await wasm(page, "web_enemy_screen_x");
        const ey = await wasm(page, "web_enemy_screen_y");
        await page.mouse.move(box.x + ex + 3, box.y + ey - 3);
        return wasm(page, "web_aim_enemy");
      },
      { timeout: 5000 },
    )
    .toBe(1);
  // she spots Grace and closes in within a few frames: pin her back in place
  // so the click lands on her and not on Grace's own (inert) dot
  await wasm(page, "web_test_enemy_place", (gx - 3) * 100 + 50, gy * 100 + 80);
  const ex = await wasm(page, "web_enemy_screen_x");
  const ey = await wasm(page, "web_enemy_screen_y");
  await page.mouse.click(box.x + ex + 3, box.y + ey - 3);
  // the click is consumed on the next frame: poll rather than read once
  await expect.poll(() => wasm(page, "web_impact_age_ms")).toBeGreaterThanOrEqual(0);
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

test("every item is visible without the Lighter", async ({ page }) => {
  await boot(page);
  expect(await wasm(page, "lighter_value")).toBe(0);
  // no item is left in the dark: the dark search finds nothing
  for (let type = 0; type <= 8; type++) {
    expect(await wasm(page, "web_test_find_item", type, 0, 1)).toBe(-1);
  }
  const coin = await findItem(page, 1, 0, 0);
  expect(await wasm(page, "web_item_visible_at", coin.x, coin.y)).toBe(1);
});

test("items in the dark can't be picked up until lit", async ({ page }) => {
  await boot(page);
  // a Green Herb with no light near it (found by type, in the dark).
  // nth=2: the nearer dark herbs are the Bar nook and one behind its
  // restored Kitchenette wall; this one stands open to the west.
  await wasm(page, "web_test_items_always_visible", 0); // dark items back on
  const herb = await findItem(page, 2, 2, 1);
  await wasm(page, "web_test_grace_at", herb.x - 1, herb.y);
  await page.waitForTimeout(200);
  await steer(page, "d", 60); // face the herb without leaving the cell
  expect(await wasm(page, "web_light_at", herb.x, herb.y)).toBe(0);
  // interact is the Space key now (map clicks on the item do nothing)
  const tap = async () => {
    await page.keyboard.press(" ");
    await page.waitForTimeout(120);
  };
  const before = await wasm(page, "items_collected");
  await tap();
  expect(await wasm(page, "items_collected")).toBe(before);
  await wasm(page, "web_test_give", 7);
  await wasm(page, "lighter_set", 1);
  await page.waitForTimeout(100);
  expect(await wasm(page, "web_light_at", herb.x, herb.y)).toBe(1);
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
  await wasm(page, "web_test_enemy_place", 11550, 4550); // enemy on floor 3 (she never takes the stairs)
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

test("Space triggers the interact in front of her (pickup)", async ({ page }) => {
  await boot(page);
  // stand just west of a lit Green Herb, facing east after a nudge
  const herb = await findItem(page, 2, 0, 0);
  await page.evaluate(
    ([hx, hy]) => window.Module?._web_test_grace_at?.(hx, hy),
    [herb.x - 1, herb.y] as const,
  );
  await page.waitForTimeout(200);
  await steer(page, "d", 60); // face east without leaving the cell
  const before = await wasm(page, "items_collected");
  await page.keyboard.press(" ");
  await expect.poll(() => wasm(page, "items_collected")).toBe(before + 1);
});

test("an item the Lighter has shown stays visible", async ({ page }) => {
  await boot(page);
  await wasm(page, "web_test_items_always_visible", 0); // dark items back on
  const herb = await findItem(page, 2, 2, 1); // a Green Herb in the dark (open west side)
  await wasm(page, "web_test_grace_at", herb.x - 1, herb.y);
  await wasm(page, "web_test_give", 7);
  expect(await wasm(page, "web_item_visible_at", herb.x, herb.y)).toBe(0);
  await wasm(page, "lighter_set", 1);
  expect(await wasm(page, "web_item_visible_at", herb.x, herb.y)).toBe(1);
  await wasm(page, "lighter_set", 0);
  expect(await wasm(page, "web_light_at", herb.x, herb.y)).toBe(0);
  expect(await wasm(page, "web_item_visible_at", herb.x, herb.y)).toBe(1);
});

test("a typewriter save keeps the enemy where it was", async ({ page }) => {
  await boot(page);
  await wasm(page, "web_test_give", 3);
  await wasm(page, "web_test_enemy_place", 9550, 2550);
  expect(await wasm(page, "save_game", 0)).toBe(1);
  await wasm(page, "web_test_enemy_place", 2050, 4050);
  expect(await wasm(page, "load_game", 0)).toBe(1);
  expect(await wasm(page, "web_enemy_px")).toBeCloseTo(95.5, 3);
  expect(await wasm(page, "web_enemy_py")).toBeCloseTo(25.5, 3);
});

test("throwing on the move: Q aims while walking and the click throws (hitscan)", async ({
  page,
}) => {
  const box = await boot(page);
  await wasm(page, "web_test_enemy_place", 11550, 4550); // enemy on floor 3: no random spawn interrupts
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
  await expect.poll(() => wasm(page, "web_impact_age_ms")).toBeGreaterThanOrEqual(0);
  expect(await wasm(page, "web_test_aiming")).toBe(0);
});

test("hitscan: a clear ray hits the enemy, a door in between blocks it", async ({ page }) => {
  await boot(page);
  // the locked Cherub door (found by role; locked, so it cannot
  // bump-open while Grace stands next to it): Grace on one side, the
  // enemy first on her side, then past the door where the ray must stop
  const door = await findDoor(page, 1);
  const horiz = door.extra === 1;
  const gx = horiz ? door.x : door.x - 1; // Grace's cell, near side
  const gy = horiz ? door.y - 1 : door.y;
  const fx = door.x; // far cell, past the slab
  const fy = door.y;
  const dx = horiz ? 0 : 100; // ray direction across the door
  const dy = horiz ? 100 : 0;
  await wasm(page, "web_test_grace_at", gx, gy);
  await page.waitForTimeout(150);
  await wasm(
    page,
    "web_test_enemy_place",
    (gx + 0.5) * 100 - dx,
    (gy + 0.5) * 100 - dy,
  );
  expect(await wasm(page, "web_test_hitscan", -dx, -dy, 300)).toBe(1);
  await wasm(page, "web_test_enemy_place", (fx + 0.5) * 100 + dx, (fy + 0.5) * 100 + dy);
  expect(await wasm(page, "web_test_hitscan", dx, dy, 300)).toBe(2);
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
  // park the enemy on floor 3 so a random spawn near Room 203 can't interrupt the throw
  await page.evaluate(() => (window as any).Module?._web_test_enemy_place?.(11550, 4550));
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
  // stand just west of a lit Green Herb (found by type, not coordinates)
  const herb = await findItem(page, 2, 0, 0);
  await page.evaluate(
    ([hx, hy]) => (window as any).Module?._web_test_grace_at?.(hx, hy),
    [herb.x - 1, herb.y] as const,
  );
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
  // React polls the bottle state every 150 ms: wait until the aim stick is live
  await expect(page.locator(".aim-stick")).not.toHaveAttribute("data-inactive", /.*/);
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
  // park the enemy on floor 3 so a random spawn near Room 203 can't interrupt the aim
  await page.evaluate(() => (window as any).Module?._web_test_enemy_place?.(11550, 4550));
  await page.evaluate(() => (window as any).Module?._web_test_bottle_ready?.());
  // React polls the bottle state every 150 ms: wait until the aim stick is live
  await expect(page.locator(".aim-stick")).not.toHaveAttribute("data-inactive", /.*/);
  const aim = await page.locator(".aim-stick").boundingBox();
  expect(aim).not.toBeNull();
  const c = { x: (aim?.x ?? 0) + (aim?.width ?? 0) / 2, y: (aim?.y ?? 0) + (aim?.height ?? 0) / 2 };
  const touch = await touchSession(page, context);
  // on the shrunk landscape stick the dead zone is ~13px: drift 11px
  await touch("touchStart", [{ ...c, id: 1 }]);
  await touch("touchMove", [{ x: c.x + 11, y: c.y - 3, id: 1 }]);
  await page.waitForTimeout(100); // well under the 450ms hold-to-equip threshold, even at a slow frame rate
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
  const herb = await findItem(page, 2, 0, 0); // a lit Green Herb, Grace west of it
  await page.evaluate(
    ([hx, hy]) => (window as any).Module?._web_test_grace_at?.(hx, hy),
    [herb.x - 1, herb.y] as const,
  );
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
  const fusebox = await findObject(page, 2, 0); // OBJ_FUSEBOX
  const goal = await findObject(page, 10, 0); // OBJ_GOAL
  await page.evaluate(
    ([fx2, fy2]) => window.Module?._web_test_grace_at?.(fx2, fy2),
    [fusebox.x, fusebox.y - 1] as const,
  );
  await page.waitForTimeout(200);
  expect(await wasm(page, "web_test_interact", fusebox.x, fusebox.y)).toBe(1);
  expect(await wasm(page, "web_goal_met")).toBe(0);
  await page.evaluate(() => window.Module?._web_test_give?.(6));
  expect(await wasm(page, "web_test_interact", fusebox.x, fusebox.y)).toBe(1);
  expect(await wasm(page, "web_goal_met")).toBe(1);
  await page.evaluate(
    ([gx2, gy2]) => window.Module?._web_test_grace_at?.(gx2, gy2),
    [goal.x, goal.y] as const,
  );
  await expect.poll(() => wasm(page, "web_level_complete")).toBe(1);
});

test("climbing: Space climbs on after the delay, and Space climbs off again", async ({ page }) => {
  await boot(page);
  // the parlor table (third obstacle: fuse locker, bar counter, table),
  // with floor on its west side
  const stairs = await findObject(page, 8, 2); // OBJ_OBSTACLE
  expect([stairs.x, stairs.y]).toEqual([69, 190]);
  await page.evaluate(
    ([sx, sy]) => window.Module?._web_test_grace_at?.(sx, sy),
    [stairs.x - 1, stairs.y] as const,
  );
  await page.waitForTimeout(200);
  expect(await wasm(page, "web_test_set_climbable", stairs.x + 1, stairs.y)).toBe(1);
  await steer(page, "d", 60); // face east, stay in her cell
  await page.waitForTimeout(250);
  await page.keyboard.press(" ");
  await expect.poll(() => wasm(page, "web_on_top"), { timeout: 3000 }).toBe(1);
  // climb off with Space again (facing decides the landing cell)
  await page.keyboard.press(" ");
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

test("Space in a menu accepts there and never leaks into the world", async ({ page }) => {
  await boot(page);
  // stand just west of a lit Green Herb: a leaked Space would pick it up
  const herb = await findItem(page, 2, 0, 0);
  await page.evaluate(
    ([hx, hy]) => window.Module?._web_test_grace_at?.(hx, hy),
    [herb.x - 1, herb.y] as const,
  );
  await page.waitForTimeout(200);
  const before = await wasm(page, "items_collected");
  await page.keyboard.press("p"); // Pause opens; Space belongs to the menu now
  await expect(page.locator(".pause-menu")).toBeVisible();
  await page.keyboard.press(" ");
  await page.waitForTimeout(250);
  expect(await wasm(page, "items_collected")).toBe(before);
  // the page did not scroll either
  expect(await page.evaluate(() => window.scrollY)).toBe(0);
});

test("items cannot be picked up through a wall, but can from the open side", async ({ page }) => {
  await boot(page);
  await wasm(page, "web_test_enemy_place", 11550, 4550); // enemy on floor 3 (she never takes the stairs)
  // the Antique Coin at (84,202) in the Guard Office sits against its
  // east wall: light it with the Lighter so only the wall can block
  // the pickup
  await wasm(page, "web_test_give", 7);
  await wasm(page, "lighter_set", 1);
  let coin = { x: -1, y: -1, extra: 0 };
  for (let n = 0; coin.x !== 84 || coin.y !== 202; n++) coin = await findAnyItem(page, 1, n);
  const before = await wasm(page, "items_collected");
  // east of the wall: the wall is between us
  await wasm(page, "web_test_grace_at", coin.x + 2, coin.y);
  await page.waitForTimeout(200);
  await page.keyboard.press(" ");
  await page.waitForTimeout(250);
  expect(await wasm(page, "items_collected")).toBe(before);
  // west of it is the open side (inside the room)
  await wasm(page, "web_test_grace_at", coin.x - 1, coin.y);
  await page.waitForTimeout(200);
  await page.keyboard.press(" ");
  await expect.poll(() => wasm(page, "items_collected")).toBe(before + 1);
});

test("inventory: Space opens the cell context menu, arrows + Space use the item, Esc steps back", async ({
  page,
}) => {
  await boot(page);
  await wasm(page, "web_test_enemy_place", 11550, 4550); // enemy on floor 3 (she never takes the stairs)
  await wasm(page, "web_test_hurt");
  await wasm(page, "web_test_hurt");
  expect(await wasm(page, "health_value")).toBe(2);
  await wasm(page, "web_test_give", 5); // Green Herb in the first cell
  await page.keyboard.press("i");
  const overlay = page.getByRole("dialog", { name: "Inventory" });
  await expect(overlay).toBeVisible();
  // arrow down until a filled inventory cell carries the green selection
  const herbCell = overlay.locator(".inventory-slot[data-filled='true']").first();
  for (let i = 0; i < 12; i++) {
    if ((await herbCell.getAttribute("data-menu-selected")) === "true") break;
    await page.keyboard.press("ArrowDown");
  }
  await expect(herbCell).toHaveAttribute("data-menu-selected", "true");
  // Space opens that item's context window (not a world interact)
  await page.keyboard.press(" ");
  const menu = overlay.locator(".item-menu");
  await expect(menu).toBeVisible();
  await expect(menu.locator("button").first()).toHaveAttribute("data-menu-selected", "true");
  // Esc closes only the context window, back to the inventory
  await page.keyboard.press("Escape");
  await expect(menu).toHaveCount(0);
  await expect(overlay).toBeVisible();
  // reopen with Space, step to Use, accept with Space: the herb heals her
  await page.keyboard.press(" ");
  await expect(menu).toBeVisible();
  const useButton = menu.getByRole("button", { name: "Use", exact: true });
  for (let i = 0; i < 4; i++) {
    if ((await useButton.getAttribute("data-menu-selected")) === "true") break;
    await page.keyboard.press("ArrowDown");
  }
  await expect(useButton).toHaveAttribute("data-menu-selected", "true");
  await page.keyboard.press(" ");
  await expect(menu).toHaveCount(0);
  expect(await wasm(page, "health_value")).toBe(0); // the herb healed her back to Fine
  await expect(overlay.locator(".inventory-slot[data-filled='true']")).toHaveCount(0);
});

test("the enemy chases diagonally (both axes at once) and catches Grace", async ({ page }) => {
  await boot(page);
  // Grace waits at the Room 203 spawn (159.5, 68.5); put the enemy
  // north-east of her in the same room
  await wasm(page, "web_test_enemy_place", 16250, 6550);
  const x0 = await wasm(page, "web_enemy_px");
  const y0 = await wasm(page, "web_enemy_py");
  expect(x0).toBeCloseTo(162.5, 0);
  expect(y0).toBeCloseTo(65.5, 0);
  // axis-aligned movement would first change only one coordinate; a
  // diagonal leg changes both before either has moved a full cell
  await expect
    .poll(
      async () => {
        const x = await wasm(page, "web_enemy_px");
        const y = await wasm(page, "web_enemy_py");
        return Math.abs(x - x0) > 0.08 && Math.abs(y - y0) > 0.08
          ? Math.min(Math.abs(x - x0), Math.abs(y - y0))
          : 0;
      },
      { timeout: 4000 },
    )
    .toBeGreaterThan(0.08);
  // and the chase connects: she ends up Hurt
  await expect.poll(() => wasm(page, "web_grace_mode"), { timeout: 15000 }).toBe(4);
});

test("key chain: screwdriver and Cherub Key open the way to the fuse, gate and goal", async ({
  page,
}) => {
  await boot(page);
  await wasm(page, "web_test_enemy_place", 11550, 4550); // enemy on floor 3 (she never takes the stairs)
  // the chain's items sit in unlit rooms: carry a lit Lighter
  await wasm(page, "web_test_give", 7);
  await wasm(page, "lighter_set", 1);
  const collected = () => wasm(page, "items_collected");
  // the Cherub door (Rehabilitation Ward) is locked without its key
  const cdoor = approach(await findDoor(page, 1));
  await wasm(page, "web_test_grace_at", cdoor.x, cdoor.y);
  await page.waitForTimeout(200);
  await page.keyboard.press(" ");
  await steer(page, cdoor.key, 700);
  expect(await wasm(page, cdoor.axis)).toBe(cdoor.before);
  // the Screwdriver (Chairman's Office)
  const screw = await findAnyItem(page, 4, 0);
  let before = await collected();
  await wasm(page, "web_test_grace_at", screw.x, screw.y);
  await page.waitForTimeout(150);
  await page.keyboard.press(" ");
  await expect.poll(collected).toBe(before + 1);
  // the Cherub Key (Nurses' Station)
  const key = await findAnyItem(page, 8, 0);
  before = await collected();
  await wasm(page, "web_test_grace_at", key.x, key.y);
  await page.waitForTimeout(150);
  await page.keyboard.press(" ");
  await expect.poll(collected).toBe(before + 1);
  // unlock the Cherub door and walk through into the office
  await wasm(page, "web_test_grace_at", cdoor.x, cdoor.y);
  await page.waitForTimeout(150);
  await page.keyboard.press(" "); // unlock with the key
  await page.waitForTimeout(150);
  await steer(page, cdoor.key, 900);
  expect(await wasm(page, cdoor.axis)).toBeGreaterThan(cdoor.past);
  // the Fuse is in the screwed-shut locker in Room 201: Space opens it,
  // Space takes it (from the floor east of the locker)
  const fuse = await findAnyItem(page, 6, 0);
  await wasm(page, "web_test_grace_at", fuse.x + 1, fuse.y);
  await page.waitForTimeout(150);
  before = await collected();
  await page.keyboard.press(" "); // opens the cabinet (she has the screwdriver)
  await page.waitForTimeout(200);
  for (let i = 0; i < 4 && (await collected()) === before; i++) {
    await page.keyboard.press(" "); // takes the fuse (a file may take a press first)
    await page.waitForTimeout(200);
  }
  expect(await collected()).toBe(before + 1);
  // the fusebox by the gate powers it open; the goal is past it
  const fusebox = await findObject(page, 2, 0);
  await wasm(page, "web_test_grace_at", fusebox.x, fusebox.y - 1);
  await page.waitForTimeout(150);
  await page.keyboard.press(" ");
  await expect.poll(() => wasm(page, "web_goal_met")).toBe(1);
  const goal = await findObject(page, 10, 0);
  await wasm(page, "web_test_grace_at", goal.x, goal.y);
  await expect.poll(() => wasm(page, "web_level_complete")).toBe(1);
});

test("the security gates, security door and staircases are sealed", async ({ page }) => {
  await boot(page);
  await wasm(page, "web_test_enemy_place", 11550, 4550); // enemy on floor 3 (she never takes the stairs)
  // the striped gate in the Rehabilitation Ward stays shut without the fuse
  const gd = await findDoor(page, 2);
  const gate = approach({ ...gd, y: gd.extra ? gd.y : gd.y + 1 });
  await wasm(page, "web_test_grace_at", gate.x, gate.y);
  await page.waitForTimeout(200);
  await steer(page, gate.key, 700);
  expect(await wasm(page, gate.axis)).toBe(gate.before);
  expect(await wasm(page, "web_goal_met")).toBe(0);
  // the striped security door (Isolation Ward) is a plain unlocked door
  const sec = approach(await findDoor(page, 3));
  await wasm(page, "web_test_grace_at", sec.x, sec.y);
  await page.waitForTimeout(200);
  await steer(page, sec.key, 700);
  expect(await wasm(page, sec.axis)).toBeGreaterThan(sec.before);
});

test("stairs carry Grace between floors", async ({ page }) => {
  await boot(page);
  await wasm(page, "web_test_enemy_place", 11550, 4550);
  // the first stairs object (F3 west) links to the F2 north-west stairs up
  const top = await findObject(page, 13, 0); // OBJ_STAIRS
  const down = await findObject(page, 13, 1);
  expect([top.x, top.y, down.x, down.y]).toEqual([32, 5, 30, 62]);
  await wasm(page, "web_test_grace_at", down.x, down.y);
  await expect.poll(() => wasm(page, "player_cell_y"), { timeout: 3000 }).toBeLessThan(10);
  expect(Math.abs((await wasm(page, "player_cell_x")) - (top.x + 1))).toBeLessThanOrEqual(1);
});

test("the Central Hall flights lead up to the floor 3 attic", async ({ page }) => {
  await boot(page);
  await wasm(page, "web_test_enemy_place", 2850, 3550); // floor 3 Lounge
  const attic = await findObject(page, 13, 10); // OBJ_STAIRS
  const hall = await findObject(page, 13, 11);
  expect([attic.x, attic.y, hall.x, hall.y]).toEqual([78, 52, 81, 93]);
  await wasm(page, "web_test_grace_at", hall.x + 1, hall.y);
  await expect.poll(() => wasm(page, "player_cell_y"), { timeout: 3000 }).toBeLessThan(56);
  expect(await wasm(page, "player_cell_x")).toBeGreaterThanOrEqual(attic.x);
});

test("Grace walks in rooms across floor 2 (Central Hall, Bar & Lounge, Room 203)", async ({
  page,
}) => {
  await boot(page);
  await wasm(page, "web_test_enemy_place", 11550, 4550); // park the enemy on floor 3 (she never takes the stairs)
  const rooms: Array<[number, number, string]> = [
    [70, 100, "d"], // Gallery
    [50, 70, "d"], // Bar & Lounge
    [160, 68, "a"], // Room 203
  ];
  for (const [x, y, key] of rooms) {
    await wasm(page, "web_test_grace_at", x, y);
    await page.waitForTimeout(150);
    const x0 = await wasm(page, "player_cell_x");
    await steer(page, key, 600);
    const x1 = await wasm(page, "player_cell_x");
    expect(Math.abs(x1 - x0)).toBeGreaterThanOrEqual(1);
  }
});

test("reveal loads section by section as Grace moves", async ({ page }) => {
  await boot(page);
  await wasm(page, "web_test_enemy_place", 11550, 4550); // park the enemy on floor 3 (she never takes the stairs)
  // the Bar & Lounge is floor but unrevealed from the Room 203 start
  expect(await wasm(page, "web_cell_floor", 50, 70)).toBe(0);
  const revealed0 = await page.evaluate(() => window.Module?._level_revealed?.() ?? 0);
  // walk her into the Bar: its section reveals as one sensible chunk
  await wasm(page, "web_test_grace_at", 50, 70);
  await expect.poll(() => wasm(page, "web_cell_floor", 50, 70)).toBe(1);
  const revealed1 = await page.evaluate(() => window.Module?._level_revealed?.() ?? 0);
  expect(revealed1).toBeGreaterThan(revealed0 + 50);
});

test("steering into walls and the hall void never leaves the floor", async ({ page }) => {
  await boot(page);
  await wasm(page, "web_test_enemy_place", 11550, 4550); // park the enemy on floor 3 (she never takes the stairs)
  // west out of Room 203: its west wall holds her in the room
  await steer(page, "a", 1800);
  const cx = await wasm(page, "player_cell_x");
  const cy = await wasm(page, "player_cell_y");
  expect(cx).toBeGreaterThanOrEqual(155);
  expect(await wasm(page, "web_cell_floor", cx, cy)).toBe(1);
  // east out of Room 203 into the void past the building's east wall
  await wasm(page, "web_test_grace_at", 160, 68);
  await page.waitForTimeout(200);
  await steer(page, "d", 1800);
  const hx = await wasm(page, "player_cell_x");
  const hy = await wasm(page, "player_cell_y");
  expect(hx).toBeLessThanOrEqual(164);
  expect(await wasm(page, "web_cell_floor", hx, hy)).toBe(1);
});

test("Space picks up an Antique Coin", async ({ page }) => {
  await boot(page);
  await wasm(page, "web_test_enemy_place", 11550, 4550);
  const coin = await findItem(page, 1, 0, 0); // first lit Antique Coin
  const before = await wasm(page, "items_collected");
  await wasm(page, "web_test_grace_at", coin.x, coin.y);
  await page.waitForTimeout(150);
  await page.keyboard.press(" ");
  await expect.poll(() => wasm(page, "items_collected")).toBe(before + 1);
});
