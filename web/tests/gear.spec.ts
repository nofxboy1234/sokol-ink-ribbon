import { expect, test, type Page } from "@playwright/test";

// Item ids from native/src/level.h (save ordinals; never reordered).
const HANDGUN_AMMO = 10;
const MAGNUM_AMMO = 11;
const M232 = 25;
const REQUIEM = 27;
const WRENCH = 47;
const ENEMY_STUNNED = 4;

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

test("guns: the M232 equips on pickup, needs handgun ammo, aims like the bottle and stuns", async ({
  page,
}) => {
  const box = await boot(page);
  await wasm(page, "web_test_give_qty", M232, 1);
  expect(await wasm(page, "web_equipped_item")).toBe(M232);

  // Inventory: slot marked equipped, weapon slot filled, C copy shown, no ammo yet
  await page.getByRole("button", { name: "Inventory", exact: true }).click();
  const slot = page.locator(".inventory-slot", { hasText: "S&S M232" });
  await expect(slot).toHaveAttribute("data-equipped", "true");
  await expect(page.locator(".gun-slot-item")).toHaveAttribute("data-equipped-item", String(M232));
  await expect(page.getByTestId("gear-line")).toContainText("Handgun 0");
  await slot.click();
  await expect(page.locator(".item-detail p")).not.toBeEmpty();
  await slot.click({ button: "right" });
  await expect(page.locator(".item-menu").getByRole("button", { name: "Unequip" })).toBeVisible();
  await page.keyboard.press("Escape");
  await page.getByRole("dialog", { name: "Inventory" }).getByRole("button", { name: "Close" }).click();

  // no ammo: Quick Equip can't make it active
  await page.keyboard.press("z");
  const quick = page.getByRole("group", { name: "Quick Equip" });
  const gun = quick.getByRole("button", { name: "S&S M232", exact: true });
  await expect(gun).toHaveAttribute("data-slot", "East");
  await gun.click();
  expect(await wasm(page, "web_weapon_active")).toBe(0);

  await wasm(page, "web_test_give_qty", HANDGUN_AMMO, 6);
  expect(await wasm(page, "web_gear_stat", 0)).toBe(6);
  await gun.click();
  expect(await wasm(page, "web_weapon_active")).toBe(1);

  // aim at the enemy 3 cells west and click: one round, a stun
  const gx = await wasm(page, "player_cell_x");
  const gy = await wasm(page, "player_cell_y");
  await wasm(page, "web_test_enemy_place", (gx - 3) * 100 + 50, gy * 100 + 50);
  await page.keyboard.press("q");
  await expect.poll(() => wasm(page, "web_test_aiming")).toBe(1);
  const cell = await wasm(page, "web_cell_css_px");
  const sx = box.x + (await wasm(page, "web_grace_screen_x")) - cell * 3;
  const sy = box.y + (await wasm(page, "web_grace_screen_y"));
  await page.mouse.move(sx, sy);
  await page.waitForTimeout(150);
  await page.mouse.click(sx, sy);
  await expect.poll(() => wasm(page, "web_gear_stat", 0)).toBe(5);
  await expect.poll(() => wasm(page, "web_enemy_state")).toBe(ENEMY_STUNNED);
});

test("the Requiem needs 12.7mm rounds; handgun ammo won't arm it", async ({ page }) => {
  await boot(page);
  await wasm(page, "web_test_give_qty", HANDGUN_AMMO, 10);
  await wasm(page, "web_test_give_qty", REQUIEM, 1);
  await page.getByRole("button", { name: "Inventory", exact: true }).click();
  const slot = page.locator(".inventory-slot", { hasText: "Requiem" });
  await expect(slot).toHaveAttribute("data-equipped", "true");
  await page.getByRole("dialog", { name: "Inventory" }).getByRole("button", { name: "Close" }).click();
  await page.keyboard.press("z");
  const gun = page.getByRole("group", { name: "Quick Equip" }).getByRole("button", { name: "Requiem" });
  await gun.click();
  expect(await wasm(page, "web_weapon_active")).toBe(0);
  await wasm(page, "web_test_give_qty", MAGNUM_AMMO, 1);
  await gun.click();
  expect(await wasm(page, "web_weapon_active")).toBe(1);
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

test("phone: an active gun arms the aim stick, and releasing it fires", async ({ browser }) => {
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
  await wasm(page, "web_test_give_qty", HANDGUN_AMMO, 4);
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
  await expect.poll(() => wasm(page, "web_gear_stat", 0)).toBe(3);
  await context.close();
});
