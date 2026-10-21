import { expect, test, type Page } from "@playwright/test";

// Item ids from native/src/level.h (save ordinals; never reordered).
const COIN = 1;
const HERB = 2;
const HANDGUN_AMMO = 10;
const M232 = 23;
const REQUIEM = 25;
const WRENCH = 43;
const STEROIDS = 46;
const ENEMY_STUNNED = 4;
const OBJ_CABINET = 12;
const OBJ_ITEMBOX = 15;

type Wasm = Record<string, (...args: number[]) => number>;

async function wasm(page: Page, name: string, ...args: number[]) {
  return page.evaluate(
    ([fn, rest]) => (window as unknown as { Module: Wasm }).Module[`_${fn}`](...(rest as number[])),
    [name, args] as const,
  );
}

async function boot(page: Page) {
  await page.goto("/");
  await page.waitForFunction(() => ((window as any).Module?._grid_width?.() ?? 0) > 0, null, {
    timeout: 30_000,
  });
  const box = await page.locator("canvas#canvas").boundingBox();
  if (!box) throw new Error("canvas has no box");
  // park the enemy on floor 3 until a test places it
  await wasm(page, "web_test_enemy_place", 11550, 4550);
  return box;
}

// Stand Grace on a floor cell beside (x, y). Teleporting reveals the
// room, so a cell is checked once she is there.
async function standBeside(page: Page, x: number, y: number) {
  for (const [dx, dy] of [[1, 0], [-1, 0], [0, 1], [0, -1]]) {
    await wasm(page, "web_test_grace_at", x + dx, y + dy);
    if (await wasm(page, "web_cell_floor", x + dx, y + dy)) return true;
  }
  return false;
}

// Aim 3 cells west with Q and click: one shot from the magazine.
async function fireWest(page: Page, box: { x: number; y: number }) {
  const cell = await wasm(page, "web_cell_css_px");
  const gx = await wasm(page, "player_cell_x");
  const gy = await wasm(page, "player_cell_y");
  if (!(await wasm(page, "web_test_aiming"))) await page.keyboard.press("q"); // aim stays up between shots
  await expect.poll(() => wasm(page, "web_test_aiming")).toBe(1);
  // place the enemy only now, so it has no time to reach and hurt her
  await wasm(page, "web_test_enemy_place", (gx - 3) * 100 + 50, gy * 100 + 50);
  const sx = box.x + (await wasm(page, "web_grace_screen_x")) - cell * 3;
  const sy = box.y + (await wasm(page, "web_grace_screen_y"));
  await page.mouse.move(sx, sy);
  await page.waitForTimeout(120);
  const before = await wasm(page, "web_gear_stat", 12);
  await page.mouse.click(sx, sy);
  await expect.poll(() => wasm(page, "web_gear_stat", 12)).toBe(before - 1);
}

test("guns: the M232 equips on pickup loaded 9/9, aims like the bottle, and a hit staggers", async ({
  page,
}) => {
  const box = await boot(page);
  await wasm(page, "web_test_give_qty", M232, 1);
  expect(await wasm(page, "web_equipped_item")).toBe(M232);

  // Inventory: slot marked equipped, weapon slot filled, magazine shown
  await page.getByRole("button", { name: "Inventory", exact: true }).click();
  const slot = page.locator(".inventory-slot", { hasText: "S&S M232" });
  await expect(slot).toHaveAttribute("data-equipped", "true");
  await expect(page.locator(".gun-slot-item")).toHaveAttribute("data-equipped-item", String(M232));
  await expect(page.getByTestId("gear-line")).toContainText("Loaded 9/9");
  await expect(page.getByTestId("gear-line")).toContainText("Health 3/3");
  await slot.click({ button: "right" });
  await expect(page.locator(".item-menu").getByRole("button", { name: "Unequip" })).toBeVisible();
  await page.keyboard.press("Escape");
  await page.getByRole("dialog", { name: "Inventory" }).getByRole("button", { name: "Close" }).click();

  await page.keyboard.press("z");
  const gun = page.getByRole("group", { name: "Quick Equip" }).getByRole("button", { name: "S&S M232", exact: true });
  await expect(gun).toHaveAttribute("data-slot", "East");
  await gun.click();
  expect(await wasm(page, "web_weapon_active")).toBe(1);

  // aim at the enemy 3 cells west and click: one round from the magazine
  await wasm(page, "web_test_gear_seed", 3);
  await fireWest(page, box);
  await expect(page.getByTestId("reload")).toContainText("8/9");
  await expect.poll(() => wasm(page, "web_enemy_hp")).toBeLessThan(100);
});

test("kills take several shots, the dead fade, and Reload refills the magazine", async ({ page }) => {
  const box = await boot(page);
  await wasm(page, "web_test_give_qty", M232, 1);
  await wasm(page, "web_test_give_qty", HANDGUN_AMMO, 20);
  await wasm(page, "web_test_gear_seed", 11);
  await page.keyboard.press("z");
  await page.getByRole("group", { name: "Quick Equip" }).getByRole("button", { name: "S&S M232", exact: true }).click();
  let shots = 0;
  while ((await wasm(page, "web_enemy_alive")) && shots < 12) {
    await fireWest(page, box);
    shots++;
    await page.waitForTimeout(100);
  }
  expect(await wasm(page, "web_enemy_alive")).toBe(0);
  expect(shots).toBeGreaterThanOrEqual(1);
  expect(shots).toBeLessThanOrEqual(6);
  await page.screenshot({ path: "test-results/kill.png" });
  // the on-screen Reload button (R on the keyboard does the same)
  const reload = page.getByTestId("reload");
  await expect(reload).toBeEnabled();
  await reload.click();
  await expect.poll(() => wasm(page, "web_gear_stat", 14)).toBe(1);
  await expect(reload).toContainText("Reloading");
  await expect.poll(() => wasm(page, "web_gear_stat", 12), { timeout: 5_000 }).toBe(9);
  expect(await wasm(page, "web_gear_stat", 0)).toBe(20 - shots);
});

test("the Requiem: one round loaded, and one round kills", async ({ page }) => {
  const box = await boot(page);
  await wasm(page, "web_test_give_qty", HANDGUN_AMMO, 10);
  await wasm(page, "web_test_give_qty", REQUIEM, 1);
  await page.getByRole("button", { name: "Inventory", exact: true }).click();
  const slot = page.locator(".inventory-slot", { hasText: "Requiem" });
  await expect(slot).toHaveAttribute("data-equipped", "true");
  await expect(page.getByTestId("gear-line")).toContainText("Loaded 1/5");
  await page.getByRole("dialog", { name: "Inventory" }).getByRole("button", { name: "Close" }).click();
  await page.keyboard.press("z");
  await page.getByRole("group", { name: "Quick Equip" }).getByRole("button", { name: "Requiem" }).click();
  expect(await wasm(page, "web_weapon_active")).toBe(1);
  await fireWest(page, box);
  await expect.poll(() => wasm(page, "web_enemy_alive")).toBe(0);
  await expect.poll(() => wasm(page, "web_weapon_active")).toBe(0);  // empty, no 12.7mm left
  expect(await wasm(page, "web_gear_stat", 0)).toBe(10);
});

test("the Item Box stores a herb and gives it back", async ({ page }) => {
  await boot(page);
  const found = await wasm(page, "web_test_find_object", OBJ_ITEMBOX, 0);
  expect(found).toBeGreaterThanOrEqual(0);
  const bx = found >> 16, by = (found >> 8) & 0xff;
  expect(await standBeside(page, bx, by)).toBe(true);
  await wasm(page, "web_test_give_qty", HERB, 1);
  await wasm(page, "web_test_interact", bx, by);
  const dialog = page.getByRole("dialog", { name: "Item Box" });
  await expect(dialog).toBeVisible();
  await dialog.getByTestId("itembox-bag").locator(`[data-item="${HERB}"]`).click();
  await expect(dialog.getByTestId("itembox-box").locator(`[data-item="${HERB}"]`)).toHaveCount(1);
  expect(await wasm(page, "web_itembox_count")).toBe(1);
  await page.screenshot({ path: "test-results/itembox.png" });
  await dialog.getByTestId("itembox-box").locator(`[data-item="${HERB}"]`).click();
  await expect(dialog.getByTestId("itembox-bag").locator(`[data-item="${HERB}"]`)).toHaveCount(1);
  await dialog.getByRole("button", { name: "Close" }).click();
  await expect(dialog).toHaveCount(0);
});

test("the Parlor: 4 Antique Coins buy the Steroids at 58,193, which raise max health", async ({ page }) => {
  await boot(page);
  let cab = -1;
  for (let n = 0; n < 8; n++) {
    const o = await wasm(page, "web_test_find_object", OBJ_CABINET, n);
    if (o < 0) break;
    if (o >> 16 === 58 && ((o >> 8) & 0xff) === 193) cab = o;
  }
  expect(cab).toBeGreaterThanOrEqual(0);
  expect(await standBeside(page, 58, 193)).toBe(true);
  await wasm(page, "web_test_give_qty", COIN, 3);
  await wasm(page, "web_test_interact", 58, 193);
  expect(await wasm(page, "web_inventory_has", STEROIDS)).toBe(0);
  await wasm(page, "web_test_give_qty", COIN, 1);
  await wasm(page, "web_test_interact", 58, 193);
  expect(await wasm(page, "web_inventory_has", STEROIDS)).toBe(1);
  expect(await wasm(page, "web_gear_stat", 15)).toBe(0);
  await page.getByRole("button", { name: "Inventory", exact: true }).click();
  const slot = page.locator(".inventory-slot", { hasText: "Steroids" });
  await slot.click({ button: "right" });
  await page.locator(".item-menu").getByRole("button", { name: "Use" }).click();
  await expect(page.getByTestId("gear-line")).toContainText("Health 4/4");
});

test("tools go on the key ring, shown with coloured shape icons", async ({ page }) => {
  await boot(page);
  await wasm(page, "web_test_give_qty", WRENCH, 1);
  await wasm(page, "web_test_give_qty", M232, 1);
  await page.getByRole("button", { name: "Inventory", exact: true }).click();
  await expect(page.getByTestId("key-ring")).toContainText("Wrench");
  await expect(page.locator(".inventory-slot", { hasText: "Wrench" })).toHaveCount(0);
  // the M232 (shape 5) draws as a diamond
  await expect(page.locator(".inventory-slot", { hasText: "S&S M232" }).locator(".glyph-diamond")).toHaveCount(1);
});

test("phone: an active gun arms the aim stick, releasing it fires, and Reload refills", async ({ browser }) => {
  const context = await browser.newContext({
    viewport: { width: 390, height: 844 },
    deviceScaleFactor: 3,
    isMobile: true,
    hasTouch: true,
  });
  const page = await context.newPage();
  await boot(page);
  const aimStick = page.locator(".aim-stick");
  await expect(aimStick).toHaveAttribute("data-inactive", "true");
  await wasm(page, "web_test_give_qty", M232, 1);
  // Quick Equip opens only while she stands still; retry until it takes
  await expect
    .poll(async () => {
      if (!(await wasm(page, "web_quick_open"))) await wasm(page, "web_quick_toggle");
      await wasm(page, "web_quick_select", 1);
      return wasm(page, "web_weapon_active");
    })
    .toBe(1);
  await expect(aimStick).not.toHaveAttribute("data-inactive", "true");

  const aim = await aimStick.boundingBox();
  if (!aim) throw new Error("no aim stick");
  const c = { x: aim.x + aim.width / 2, y: aim.y + aim.height / 2 };
  const push = Math.round(aim.width * 0.38);
  const cdp = await context.newCDPSession(page);
  const touch = (type: string, points: { x: number; y: number; id: number }[]) =>
    cdp.send("Input.dispatchTouchEvent", { type, touchPoints: points });
  await touch("touchStart", [{ ...c, id: 2 }]);
  await touch("touchMove", [{ x: c.x - push, y: c.y, id: 2 }]);
  await expect.poll(() => wasm(page, "web_test_aiming")).toBe(1);
  await touch("touchEnd", [{ x: c.x - push, y: c.y, id: 2 }]);
  await expect.poll(() => wasm(page, "web_gear_stat", 12)).toBe(8);
  // the Reload button is a phone-sized tap target
  const reload = page.getByTestId("reload");
  await expect(reload).toContainText("8/9");
  const rb = await reload.boundingBox();
  expect(rb && rb.height >= 44).toBe(true);
  await wasm(page, "web_test_give_qty", HANDGUN_AMMO, 5);
  await reload.tap();
  await expect.poll(() => wasm(page, "web_gear_stat", 12), { timeout: 5_000 }).toBe(9);
  await page.screenshot({ path: "test-results/phone-reload.png" });
  await context.close();
});
