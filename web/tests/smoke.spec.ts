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
  // arrived: back to what she did before (Wait), paused
  expect(await page.evaluate(() => window.Module?._web_grace_mode?.())).toBe(0);
  expect(await page.evaluate(() => window.Module?._web_grace_last?.())).toBe(3);
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
  // the last bottle is spent: its slot stays equipped but greyed out
  await expect(slot).toHaveCount(0);
  expect(await page.evaluate(() => window.Module?._web_bottle_equipped?.())).toBe(1);
  expect(await page.evaluate(() => window.Module?._web_bottle_empty?.())).toBe(1);
  expect(await page.evaluate(() => window.Module?._web_bottle_active?.())).toBe(0);
  // the throw done, back to Paused Wait
  expect(await page.evaluate(() => window.Module?._web_grace_mode?.())).toBe(0);
  expect(await page.evaluate(() => window.Module?._web_grace_last?.())).toBe(3);
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

test("a double click while Paused resumes the paused walk, and starts waiting at start", async ({
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

  // the game starts in Paused Wait: a double click carries on waiting
  await page.reload();
  await page.waitForFunction(() => (window.Module?._grid_width?.() ?? 0) > 0, null, {
    timeout: 30_000,
  });
  expect(await mode()).toBe(0);
  expect(await page.evaluate(() => window.Module?._web_grace_resumable?.())).toBe(1);
  await page.mouse.dblclick(x, y);
  await expect.poll(mode).toBe(3);
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

test("aiming over the enemy locks onto it, even between cells", async ({ page }) => {
  const box = await boot(page);
  await wasm(page, "web_test_give", 0);
  await wasm(page, "web_bottle_equip", 1);
  const gx = await wasm(page, "player_cell_x");
  const gy = await wasm(page, "player_cell_y");
  // find an open cell in reach and put the enemy half a cell off it
  const target = await page.evaluate(
    ([px, py]) => {
      const m = window.Module as unknown as Record<string, (...x: number[]) => number>;
      for (let d = 4; d >= 2; d--) {
        for (const [dx, dy] of [
          [d, 0],
          [-d, 0],
          [0, d],
          [0, -d],
        ]) {
          if (m._web_throw_in_reach(px + dx, py + dy)) {
            return [px + dx, py + dy];
          }
        }
      }
      return null;
    },
    [gx, gy] as const,
  );
  expect(target).not.toBeNull();
  const [tx, ty] = target ?? [0, 0];
  await wasm(page, "web_test_enemy_place", tx * 100 + 50, ty * 100 + 80);
  // make the bottle active from Quick Equip, then Throw
  const at = {
    x: await wasm(page, "web_grace_screen_x"),
    y: await wasm(page, "web_grace_screen_y"),
  };
  await page.mouse.click(box.x + at.x, box.y + at.y, { button: "right" });
  await page
    .getByRole("group", { name: "Quick Equip" })
    .getByRole("button", { name: "Empty Bottle" })
    .click();
  await page.getByRole("button", { name: "Throw", exact: true }).click();
  const ex = await wasm(page, "web_enemy_screen_x");
  const ey = await wasm(page, "web_enemy_screen_y");
  await page.mouse.move(box.x + ex + 3, box.y + ey - 3);
  await page.mouse.click(box.x + ex + 3, box.y + ey - 3);
  expect(await wasm(page, "web_aim_enemy")).toBe(1);
  expect(await wasm(page, "web_grace_ready")).toBe(1);
  // tapping the enemy again throws at it
  await page.mouse.click(box.x + ex, box.y + ey);
  expect(await wasm(page, "web_impact_age_ms")).toBeGreaterThanOrEqual(0);
  expect(await wasm(page, "web_aim_enemy")).toBe(0);
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
  const box = await boot(page);
  await orderGrace(page, "Walk", 6);
  await page.waitForTimeout(200);
  await wasm(page, "web_test_hurt");
  expect(await wasm(page, "web_grace_mode")).toBe(4);
  await expect(page.getByRole("status", { name: "Grace: Hurt" })).toBeVisible();
  const x0 = await wasm(page, "player_cell_x");
  // a double click (pause) and a tap on her do nothing; she doesn't move
  const gx = await wasm(page, "web_grace_screen_x");
  const gy = await wasm(page, "web_grace_screen_y");
  await page.mouse.dblclick(box.x + gx + 60, box.y + gy + 60);
  await page.mouse.click(box.x + gx, box.y + gy);
  expect(await wasm(page, "web_grace_action", 0)).toBe(0);
  await page.waitForTimeout(1500);
  expect(await wasm(page, "web_grace_mode")).toBe(4);
  expect(await wasm(page, "player_cell_x")).toBe(x0);
  // Hurt over: Paused Wait
  await expect.poll(() => wasm(page, "web_grace_mode"), { timeout: 6000 }).toBe(0);
  expect(await wasm(page, "web_grace_last")).toBe(3);
  expect(await wasm(page, "web_grace_hurt_ms")).toBe(0);
});

test("waiting, a tap on Grace pauses her and opens the action menu", async ({ page }) => {
  const box = await boot(page);
  await page.getByRole("button", { name: "Wait", exact: true }).click();
  await page
    .getByRole("toolbar", { name: "Confirm action" })
    .getByRole("button", { name: "Ok" })
    .click();
  expect(await wasm(page, "web_grace_mode")).toBe(3);
  await page.waitForTimeout(400);
  await page.mouse.click(
    box.x + (await wasm(page, "web_grace_screen_x")),
    box.y + (await wasm(page, "web_grace_screen_y")),
  );
  expect(await wasm(page, "web_grace_mode")).toBe(0);
  await expect(page.getByRole("button", { name: "Walk", exact: true })).toBeVisible();
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

test("a throw while walking returns to Paused Walking; a double click carries on", async ({
  page,
}) => {
  const box = await boot(page);
  await orderGrace(page, "Walk", 6);
  await page.waitForTimeout(300);
  const x = box.x + box.width / 2 + 60;
  const y = box.y + box.height / 2 + 60;
  await page.mouse.dblclick(x, y);
  await expect.poll(() => wasm(page, "web_grace_mode")).toBe(0);
  expect(await wasm(page, "web_grace_last")).toBe(1);
  expect(await wasm(page, "web_test_bottle_ready")).toBe(1);
  const px = await wasm(page, "player_cell_x");
  const py = await wasm(page, "player_cell_y");
  let thrown = 0;
  for (const [dx, dy] of [[0, 2], [0, -2], [-2, 0], [2, 0], [0, 3], [0, -3]]) {
    if (await wasm(page, "web_throw_in_reach", px + dx, py + dy)) {
      thrown = await wasm(page, "web_test_throw_at", px + dx, py + dy);
      if (thrown) break;
    }
  }
  expect(thrown).toBe(1);
  // back to Paused Walking, the route kept
  expect(await wasm(page, "web_grace_mode")).toBe(0);
  expect(await wasm(page, "web_grace_last")).toBe(1);
  expect(await wasm(page, "web_grace_resumable")).toBe(1);
  await page.mouse.dblclick(x, y);
  await expect.poll(() => wasm(page, "web_grace_mode")).toBe(1);
});

test("Throw is disabled while paused running", async ({ page }) => {
  const box = await boot(page);
  expect(await wasm(page, "web_test_bottle_ready")).toBe(1);
  await orderGrace(page, "Run", 6);
  await page.waitForTimeout(150);
  await page.mouse.dblclick(box.x + box.width / 2 + 60, box.y + box.height / 2 + 60);
  await expect.poll(() => wasm(page, "web_grace_mode")).toBe(0);
  expect(await wasm(page, "web_grace_last")).toBe(2);
  expect(await wasm(page, "web_throw_allowed")).toBe(0);
  await expect(page.getByRole("button", { name: "Throw", exact: true })).toBeDisabled();
  expect(await wasm(page, "web_grace_action", 6)).toBe(0);
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
