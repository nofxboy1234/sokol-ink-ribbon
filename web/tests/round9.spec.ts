import { expect, test, type Page } from "@playwright/test";

// Round 9: the aim-stick centre tap closes aiming, only the nearest prompt
// shows, and doors work on the move. Objects are found by kind and cell.
const WEST_WING_KEYCARD = 30;
const M232 = 23;
const SHOTS = "/workspace/doors-items-artifacts/round9";

type Wasm = Record<string, (...args: number[]) => number> & { UTF8ToString: (p: number) => string };

async function wasm(page: Page, name: string, ...args: number[]) {
  return page.evaluate(
    ([fn, rest]) => (window as unknown as { Module: Wasm }).Module[`_${fn}`](...(rest as number[])),
    [name, args] as const,
  );
}

async function promptAt(page: Page, x: number, y: number) {
  return page.evaluate(
    ([px, py]) => {
      const M = (window as unknown as { Module: Wasm }).Module;
      return { text: M.UTF8ToString(M._web_test_prompt_at(px, py)), alert: M._web_test_prompt_alert_at(px, py) };
    },
    [x, y] as const,
  );
}

async function boot(page: Page) {
  await page.goto("/");
  await page.waitForFunction(() => ((window as any).Module?._grid_width?.() ?? 0) > 0, null, {
    timeout: 30_000,
  });
  const box = await page.locator("canvas#canvas").boundingBox();
  if (!box) throw new Error("canvas has no box");
  await wasm(page, "web_test_enemy_place", 11550, 4550);
  return box;
}

// Grace's position, sampled once per animation frame for ms.
async function track(page: Page, ms: number) {
  return page.evaluate(async (dur) => {
    const M = (window as unknown as { Module: Wasm }).Module;
    const out: { x: number; y: number }[] = [];
    const t0 = performance.now();
    while (performance.now() - t0 < dur) {
      await new Promise((r) => requestAnimationFrame(r));
      out.push({ x: M._player_px(), y: M._player_py() });
    }
    return out;
  }, ms);
}

async function phone(browser: import("@playwright/test").Browser) {
  const context = await browser.newContext({
    viewport: { width: 390, height: 844 },
    deviceScaleFactor: 3,
    isMobile: true,
    hasTouch: true,
  });
  const page = await context.newPage();
  await boot(page);
  const aim = await page.locator(".aim-stick").boundingBox();
  if (!aim) throw new Error("no aim stick");
  const c = { x: aim.x + aim.width / 2, y: aim.y + aim.height / 2 };
  const cdp = await context.newCDPSession(page);
  const touch = (type: string, points: { x: number; y: number; id: number }[]) =>
    cdp.send("Input.dispatchTouchEvent", { type, touchPoints: points });
  const tap = async (id: number) => {
    await touch("touchStart", [{ ...c, id }]);
    await page.waitForTimeout(60);
    await touch("touchEnd", [{ ...c, id }]);
  };
  return { context, page, tap };
}

async function equipM232(page: Page) {
  await wasm(page, "web_test_give_qty", M232, 1);
  await expect
    .poll(async () => {
      if (!(await wasm(page, "web_quick_open"))) await wasm(page, "web_quick_toggle");
      await wasm(page, "web_quick_select", 1);
      return wasm(page, "web_weapon_active");
    })
    .toBe(1);
}

test("phone: tapping the aim stick's centre while aiming closes aiming, without interacting", async ({ browser }) => {
  const { context, page, tap } = await phone(browser);
  await equipM232(page);
  await wasm(page, "web_test_grace_at", 69, 211); // the keycard is one cell east
  await page.waitForTimeout(150);
  expect(await wasm(page, "web_aim_toggle")).toBe(1);
  expect(await wasm(page, "web_test_aiming")).toBe(1);
  await page.screenshot({ path: `${SHOTS}/phone-aiming-before-tap.png` });
  await tap(4);
  await expect.poll(() => wasm(page, "web_test_aiming")).toBe(0);
  expect(await wasm(page, "web_inventory_has", WEST_WING_KEYCARD)).toBe(0);
  expect(await wasm(page, "web_gear_stat", 12)).toBe(9); // no shot either
  await page.screenshot({ path: `${SHOTS}/phone-aim-closed-by-tap.png` });
  // not aiming, the same tap interacts
  await tap(5);
  await expect.poll(() => wasm(page, "web_inventory_has", WEST_WING_KEYCARD)).toBe(1);
  expect(await wasm(page, "web_test_aiming")).toBe(0);
  await context.close();
});

test("only the nearest prompt shows when several objects are in reach", async ({ page }) => {
  await boot(page);
  await wasm(page, "web_test_grace_at", 80, 201); // Item Box 80,200 and typewriter 81,202 both in reach
  await expect.poll(async () => (await promptAt(page, 80, 200)).text).toBe("Item Box");
  expect(await wasm(page, "web_test_item_labels")).toBe(1);
  expect((await promptAt(page, 81, 202)).text).toBe("");
  await page.screenshot({ path: `${SHOTS}/nearest-prompt-itembox.png` });
  await wasm(page, "web_test_grace_at", 81, 202 + 1);
  await expect.poll(async () => (await promptAt(page, 81, 202)).text).toMatch(/^Typewriter/);
  expect(await wasm(page, "web_test_item_labels")).toBe(1);
  expect((await promptAt(page, 80, 200)).text).toBe("");
  await page.screenshot({ path: `${SHOTS}/nearest-prompt-typewriter.png` });
  // the Medication Room: on the blood, its prompt wins over the keycard beside it
  await wasm(page, "web_test_grace_at", 71, 212);
  await expect.poll(async () => (await promptAt(page, 71, 212)).text).toMatch(/^Infected Blood/);
  expect(await wasm(page, "web_test_item_labels")).toBe(1);
  expect((await promptAt(page, 70, 211)).text).toBe("");
});

test("a door unlocks while she walks, and she keeps walking", async ({ page }) => {
  await boot(page);
  // the Waiting Room's west door (117,195) unlocks from the Waiting Room side
  await wasm(page, "web_test_grace_at", 118, 196);
  await expect.poll(async () => (await promptAt(page, 117, 195)).text).toBe("unlock");
  await page.keyboard.down("d");
  await page.waitForTimeout(60);
  await page.keyboard.press(" ");
  const path = await track(page, 250);
  await page.keyboard.up("d");
  await expect.poll(async () => (await promptAt(page, 117, 195)).text).not.toBe("unlock");
  expect(await wasm(page, "web_test_door_discovered", 117, 195)).toBe(1);
  expect(path[path.length - 1].x - path[0].x).toBeGreaterThan(0.3); // still walking east
  await page.screenshot({ path: `${SHOTS}/door-unlocked-on-the-move.png` });
});

test("phone: a door unlocks from the aim-stick tap while the move stick is held", async ({ browser }) => {
  const { context, page, tap } = await phone(browser);
  await wasm(page, "web_test_grace_at", 118, 196);
  await expect.poll(async () => (await promptAt(page, 117, 195)).text).toBe("unlock");
  const stick = await page.locator(".stick:not(.aim-stick)").boundingBox();
  if (!stick) throw new Error("no move stick");
  const m = { x: stick.x + stick.width / 2, y: stick.y + stick.height / 2 };
  const cdp = await context.newCDPSession(page);
  await cdp.send("Input.dispatchTouchEvent", { type: "touchStart", touchPoints: [{ ...m, id: 1 }] });
  await cdp.send("Input.dispatchTouchEvent", {
    type: "touchMove",
    touchPoints: [{ x: m.x + stick.width * 0.3, y: m.y, id: 1 }],
  });
  await page.waitForTimeout(100);
  await tap(6);
  await expect.poll(async () => (await promptAt(page, 117, 195)).text).not.toBe("unlock");
  const path = await track(page, 250);
  expect(path[path.length - 1].x - path[0].x).toBeGreaterThan(0.2);
  await cdp.send("Input.dispatchTouchEvent", { type: "touchEnd", touchPoints: [] });
  await context.close();
});
