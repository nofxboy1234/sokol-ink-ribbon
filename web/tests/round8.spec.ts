import { expect, test, type Page } from "@playwright/test";

// Round 8: pickups on the move, door discovery colours, prompts without
// hover, focus loss, stairs speed and aim staying up. Objects are found by
// kind and cell.
const WEST_WING_KEYCARD = 30;
const M232 = 23;
const SHOTS = "/workspace/doors-items-artifacts/round8";

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
    const out: { t: number; x: number; y: number }[] = [];
    const t0 = performance.now();
    while (performance.now() - t0 < dur) {
      await new Promise((r) => requestAnimationFrame(r));
      out.push({ t: performance.now(), x: M._player_px(), y: M._player_py() });
    }
    return out;
  }, ms);
}

async function pixel(page: Page, box: { x: number; y: number }, cx: number, cy: number) {
  const gx = await wasm(page, "web_grace_screen_x");
  const gy = await wasm(page, "web_grace_screen_y");
  const cell = await wasm(page, "web_cell_css_px");
  const sx = box.x + gx + (cx - (await wasm(page, "player_px"))) * cell;
  const sy = box.y + gy + (cy - (await wasm(page, "player_py"))) * cell;
  const png = await page.screenshot({ clip: { x: sx - 1, y: sy - 1, width: 3, height: 3 } });
  return page.evaluate(async (b64) => {
    const img = new Image();
    img.src = `data:image/png;base64,${b64}`;
    await img.decode();
    const c = document.createElement("canvas");
    c.width = img.width;
    c.height = img.height;
    const g = c.getContext("2d")!;
    g.drawImage(img, 0, 0);
    const d = g.getImageData(Math.floor(img.width / 2), Math.floor(img.height / 2), 1, 1).data;
    return [d[0], d[1], d[2]];
  }, png.toString("base64"));
}

test("the Medication Room: beside blood she can't carry, Space takes the West Wing Keycard", async ({ page }) => {
  await boot(page);
  await wasm(page, "web_test_grace_at", 71, 212); // on the Infected Blood, the keycard at 70,211
  await expect.poll(async () => (await promptAt(page, 70, 211)).text).toBe("West Wing Keycard");
  await page.screenshot({ path: `${SHOTS}/medication-room-prompts.png` });
  await page.keyboard.press(" ");
  await expect.poll(() => wasm(page, "web_inventory_has", WEST_WING_KEYCARD)).toBe(1);
});

test("she picks an item up while walking, without stopping", async ({ page }) => {
  await boot(page);
  await wasm(page, "web_test_grace_at", 69, 211); // the keycard is one cell east
  await page.waitForTimeout(150);
  await page.keyboard.down("s");
  await page.waitForTimeout(60);
  await page.keyboard.press(" ");
  await expect.poll(() => wasm(page, "web_inventory_has", WEST_WING_KEYCARD)).toBe(1);
  const path = await track(page, 200);
  await page.keyboard.up("s");
  expect(path[path.length - 1].y - path[0].y).toBeGreaterThan(0.3); // still walking south
});

test("a door draws grey until she has been near it, then shows its lock colour", async ({ page }) => {
  const box = await boot(page);
  // the West Wing Door (82,195, keycard): far off, it is undiscovered
  expect(await wasm(page, "web_test_door_discovered", 82, 195)).toBe(0);
  await wasm(page, "web_test_grace_at", 82, 201);
  await page.waitForTimeout(200);
  expect(await wasm(page, "web_test_door_discovered", 82, 195)).toBe(0);
  const grey = await pixel(page, box, 82, 195.4);
  expect(Math.abs(grey[0] - grey[1]) < 20 && Math.abs(grey[1] - grey[2]) < 20 && grey[0] > 100).toBe(true);
  await page.screenshot({ path: `${SHOTS}/door-undiscovered-grey.png` });
  await wasm(page, "web_test_grace_at", 82, 197);
  await expect.poll(() => wasm(page, "web_test_door_discovered", 82, 195)).toBe(1);
  await page.waitForTimeout(100);
  const red = await pixel(page, box, 82, 195.4);
  expect(red[0] > 200 && red[1] < 80).toBe(true);
  // and its red prompt shows with the mouse nowhere near it
  expect(await promptAt(page, 82, 195)).toEqual({ text: "Needs West Wing Keycard", alert: 1 });
  await page.screenshot({ path: `${SHOTS}/door-discovered-red-prompt.png` });
});

test("the Item Box and the typewriter show prompts like items do", async ({ page }) => {
  await boot(page);
  await wasm(page, "web_test_grace_at", 80, 201); // Item Box 80,200 and typewriter 81,202
  await expect.poll(async () => (await promptAt(page, 80, 200)).text).toBe("Item Box");
  await wasm(page, "web_test_grace_at", 81, 203); // only the nearest prompt shows
  await expect.poll(async () => (await promptAt(page, 81, 202)).text).toMatch(/^Typewriter/);
  await page.screenshot({ path: `${SHOTS}/itembox-typewriter-prompts.png` });
});

test("a bolted one-sided door shows its bolt and says so", async ({ page }) => {
  await boot(page);
  // the Waiting Room's west door (117,195) opens from the Waiting Room
  await wasm(page, "web_test_grace_at", 115, 196);
  await expect.poll(async () => (await promptAt(page, 117, 195)).text).toBe("Bolted from the other side");
  await wasm(page, "web_test_zoom", 2);
  await page.waitForTimeout(150);
  await page.screenshot({ path: `${SHOTS}/bolted-door-wrong-side.png` });
  await wasm(page, "web_test_grace_at", 118, 196);
  await expect.poll(async () => (await promptAt(page, 117, 195)).text).toBe("unlock");
  await page.keyboard.press(" ");
  await expect.poll(async () => (await promptAt(page, 117, 195)).text).toBe("");
  await page.screenshot({ path: `${SHOTS}/bolted-door-unbolted.png` });
});

test("losing window focus mid-walk drops the held key: she stops and obeys again", async ({ page }) => {
  await boot(page);
  await wasm(page, "web_test_grace_at", 80, 205);
  await page.waitForTimeout(150);
  await page.keyboard.down("a");
  await page.waitForTimeout(150);
  await page.evaluate(() => window.dispatchEvent(new Event("blur")));
  await page.waitForTimeout(100);
  const still = await track(page, 300);
  expect(Math.abs(still[still.length - 1].x - still[0].x)).toBeLessThan(0.01);
  await page.keyboard.up("a"); // the key-up that went to the other window
  await page.keyboard.down("d");
  const moving = await track(page, 250);
  await page.keyboard.up("d");
  expect(moving[moving.length - 1].x - moving[0].x).toBeGreaterThan(0.3);
});

test("phone: losing focus mid-drag releases the move stick", async ({ browser }) => {
  const context = await browser.newContext({
    viewport: { width: 390, height: 844 },
    deviceScaleFactor: 3,
    isMobile: true,
    hasTouch: true,
  });
  const page = await context.newPage();
  await boot(page);
  await wasm(page, "web_test_grace_at", 80, 205);
  const stick = await page.locator(".stick:not(.aim-stick)").boundingBox();
  if (!stick) throw new Error("no move stick");
  const c = { x: stick.x + stick.width / 2, y: stick.y + stick.height / 2 };
  const cdp = await context.newCDPSession(page);
  const touch = (type: string, points: { x: number; y: number; id: number }[]) =>
    cdp.send("Input.dispatchTouchEvent", { type, touchPoints: points });
  await touch("touchStart", [{ ...c, id: 1 }]);
  await touch("touchMove", [{ x: c.x - stick.width * 0.3, y: c.y, id: 1 }]);
  await expect.poll(async () => (await track(page, 100)).length > 0 && wasm(page, "web_grace_mode")).not.toBe(0);
  await page.evaluate(() => window.dispatchEvent(new Event("blur")));
  await page.waitForTimeout(100);
  const still = await track(page, 300);
  expect(Math.abs(still[still.length - 1].x - still[0].x)).toBeLessThan(0.01);
  await expect(page.locator(".stick:not(.aim-stick)")).not.toHaveAttribute("data-active", "true");
  await touch("touchEnd", [{ ...c, id: 1 }]);
  await context.close();
});

test("she keeps her speed through the stairs", async ({ page }) => {
  await boot(page);
  // F1 SW stairs (32,208 4x2) walking north from 34,212; they land her on
  // the F2 SW stairs (34,119) with open floor north of them
  await wasm(page, "web_test_grace_at", 34, 212);
  await page.waitForTimeout(150);
  await page.keyboard.down("w");
  const path = await track(page, 2000);
  await page.keyboard.up("w");
  const jump = path.findIndex((p, i) => i > 0 && Math.hypot(p.x - path[i - 1].x, p.y - path[i - 1].y) > 3);
  expect(jump).toBeGreaterThan(3);
  expect(Math.round(path[jump].y)).toBeLessThan(130); // on floor 2
  // cells per second north over 200 ms either side of the jump
  const at = (t: number) => path.findIndex((p) => p.t >= t);
  const rate = (i: number, j: number) => (path[i].y - path[j].y) / ((path[j].t - path[i].t) / 1000);
  const before = rate(at(path[jump - 1].t - 200), jump - 1);
  const after = rate(jump, at(path[jump].t + 200));
  expect(before).toBeGreaterThan(1);
  expect(after).toBeGreaterThan(before * 0.8);
});

// Before the fix the jump re-inited her, the frame loop read that as
// stopping and dropped the run latch: 0.109 cells a frame became 0.050.
test("running, she keeps her run through the stairs", async ({ page }) => {
  await boot(page);
  // F1 SW stairs (32,208 4x2) walking north from 34,212; they land her on
  // the F2 SW stairs (34,119) with open floor north of them
  await wasm(page, "web_test_grace_at", 34, 212);
  await page.waitForTimeout(150);
  await page.keyboard.down("w");
  await page.waitForTimeout(50);
  const box = await page.locator("canvas#canvas").boundingBox();
  await page.mouse.move(box!.x + box!.width / 2, box!.y + box!.height / 3);
  await page.mouse.down({ button: "middle" });
  await page.mouse.up({ button: "middle" });
  const path = await track(page, 1400);
  await page.keyboard.up("w");
  const jump = path.findIndex((p, i) => i > 0 && Math.hypot(p.x - path[i - 1].x, p.y - path[i - 1].y) > 3);
  expect(jump).toBeGreaterThan(3);
  expect(Math.round(path[jump].y)).toBeLessThan(130); // on floor 2
  // cells per second north over 200 ms either side of the jump
  const at = (t: number) => path.findIndex((p) => p.t >= t);
  const rate = (i: number, j: number) => (path[i].y - path[j].y) / ((path[j].t - path[i].t) / 1000);
  const before = rate(at(path[jump - 1].t - 200), jump - 1);
  const after = rate(jump, at(path[jump].t + 200));
  expect(before).toBeGreaterThan(1);
  expect(after).toBeGreaterThan(before * 0.8);
});

test("after a shot the aim stays up for the next one", async ({ page }) => {
  const box = await boot(page);
  await wasm(page, "web_test_give_qty", M232, 1);
  await expect
    .poll(async () => {
      if (!(await wasm(page, "web_quick_open"))) await wasm(page, "web_quick_toggle");
      await wasm(page, "web_quick_select", 1);
      return wasm(page, "web_weapon_active");
    })
    .toBe(1);
  await page.keyboard.press("q");
  await expect.poll(() => wasm(page, "web_test_aiming")).toBe(1);
  const cell = await wasm(page, "web_cell_css_px");
  const sx = box.x + (await wasm(page, "web_grace_screen_x")) - cell * 3;
  const sy = box.y + (await wasm(page, "web_grace_screen_y"));
  await page.mouse.move(sx, sy);
  await page.waitForTimeout(100);
  await page.mouse.click(sx, sy);
  await expect.poll(() => wasm(page, "web_gear_stat", 12)).toBe(8);
  expect(await wasm(page, "web_test_aiming")).toBe(1);
  await page.mouse.click(sx, sy);
  await expect.poll(() => wasm(page, "web_gear_stat", 12)).toBe(7);
  expect(await wasm(page, "web_test_aiming")).toBe(1);
  await page.screenshot({ path: `${SHOTS}/aim-stays-after-shot.png` });
  await page.keyboard.press("q");
  await expect.poll(() => wasm(page, "web_test_aiming")).toBe(0);
});

test("phone: releasing the aim stick fires and the aim stays up; a dead-zone release closes it", async ({
  browser,
}) => {
  const context = await browser.newContext({
    viewport: { width: 390, height: 844 },
    deviceScaleFactor: 3,
    isMobile: true,
    hasTouch: true,
  });
  const page = await context.newPage();
  await boot(page);
  await wasm(page, "web_test_give_qty", M232, 1);
  await expect
    .poll(async () => {
      if (!(await wasm(page, "web_quick_open"))) await wasm(page, "web_quick_toggle");
      await wasm(page, "web_quick_select", 1);
      return wasm(page, "web_weapon_active");
    })
    .toBe(1);
  const aim = await page.locator(".aim-stick").boundingBox();
  if (!aim) throw new Error("no aim stick");
  const c = { x: aim.x + aim.width / 2, y: aim.y + aim.height / 2 };
  const push = Math.round(aim.width * 0.38);
  const cdp = await context.newCDPSession(page);
  const touch = (type: string, points: { x: number; y: number; id: number }[]) =>
    cdp.send("Input.dispatchTouchEvent", { type, touchPoints: points });
  await touch("touchStart", [{ ...c, id: 2 }]);
  await touch("touchMove", [{ x: c.x - push, y: c.y, id: 2 }]);
  await expect.poll(() => wasm(page, "web_test_aiming")).toBe(1);
  await page.waitForTimeout(100);
  await touch("touchEnd", [{ x: c.x - push, y: c.y, id: 2 }]);
  await expect.poll(() => wasm(page, "web_gear_stat", 12)).toBe(8);
  expect(await wasm(page, "web_test_aiming")).toBe(1);
  await page.screenshot({ path: `${SHOTS}/phone-aim-stays-after-shot.png` });
  // push out, come back to the centre and let go: no shot, aim closes
  await touch("touchStart", [{ ...c, id: 3 }]);
  await touch("touchMove", [{ x: c.x - push, y: c.y, id: 3 }]);
  await touch("touchMove", [{ ...c, id: 3 }]);
  await touch("touchEnd", [{ ...c, id: 3 }]);
  await expect.poll(() => wasm(page, "web_test_aiming")).toBe(0);
  expect(await wasm(page, "web_gear_stat", 12)).toBe(8);
  await context.close();
});
