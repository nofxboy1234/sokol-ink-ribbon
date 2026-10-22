import { expect, test, type Page } from "@playwright/test";

const labels = async (page: Page): Promise<string[]> =>
  page.evaluate(() => window.__ink!.graph().nodes.map((node) => node.label));

const snapshot = async (page: Page) =>
  page.evaluate(() => window.__ink!.snapshot());

const waitUntilStill = async (page: Page): Promise<void> => {
  await page.waitForFunction(() => window.__ink!.snapshot().pending === null);
};

const choose = async (page: Page, label: string): Promise<void> => {
  await page.keyboard.press("g");
  const node = page.getByRole("button", { name: `Decision: ${label}` });
  await expect(node).toBeVisible();
  await node.click();
  await expect(page.locator(".confirm-copy strong")).toHaveText(label);
  await page.getByRole("button", { name: /^Confirm/ }).click();
  await waitUntilStill(page);
};

test.beforeEach(async ({ page }) => {
  await page.goto("/");
  await page.waitForFunction(() => Boolean(window.__ink));
  await expect(page.getByRole("heading", { name: "Ink Ribbon" })).toBeVisible();
});

test("starts with only the reachable ink-ribbon and green herb trees", async ({ page }) => {
  const initial = await page.evaluate(() => window.__ink!.graph());
  const initialLabels = initial.nodes.map((node) => node.label);

  expect(initialLabels).toContain("move to ink-ribbon");
  expect(initialLabels).toContain("take ink-ribbon");
  expect(initialLabels).toContain("move to green herb");
  expect(initialLabels).toContain("take green herb");
  expect(initialLabels).toContain("open inventory");
  expect(initialLabels).not.toContain("move to type-writer");
  expect(initialLabels).not.toContain("move to brass key");
  expect(initialLabels).not.toContain("move to study door");

  await page.keyboard.press("g");
  await expect(page.getByRole("button", { name: "Decision: move to green herb" })).toBeVisible();
  await expect(page.getByRole("button", { name: "Decision: take green herb" })).toBeVisible();
  await expect(page.getByRole("button", { name: "Decision: move to ink-ribbon" })).toBeVisible();

  await page.getByRole("button", { name: "Decision: take ink-ribbon" }).click();
  await expect(page.locator(".route-line")).toContainText("move to ink-ribbon");
  await expect(page.locator(".route-line")).toContainText("take ink-ribbon");
});

test("moving to the herb rearranges the graph around the player's new position", async ({ page }) => {
  await choose(page, "move to green herb");

  const state = await snapshot(page);
  expect(state.player.pos.x).toBeCloseTo(9.2, 1);
  expect(state.player.pos.z).toBeCloseTo(5.8, 1);

  const graph = await page.evaluate(() => window.__ink!.graph());
  const player = graph.nodes.find((node) => node.id === "player")!;
  expect(player.anchor.x).toBeCloseTo(9.2, 1);
  expect(player.anchor.z).toBeCloseTo(5.8, 1);
  expect(graph.edges).toContainEqual({ from: "player", to: "move:ink-ribbon" });
  expect(graph.nodes.map((node) => node.label)).toContain("take green herb");
});

test("collecting and using the herb, then typing, reveals the key and opens the study", async ({ page }) => {
  await choose(page, "move to green herb");
  await choose(page, "take green herb");
  expect((await snapshot(page)).inventory).toEqual(["green-herb"]);
  expect(await labels(page)).toContain("use green herb");
  expect(await labels(page)).not.toContain("take green herb");

  await choose(page, "use green herb");
  expect((await snapshot(page)).inventory).toEqual([]);
  expect(await labels(page)).not.toContain("use green herb");

  await choose(page, "move to ink-ribbon");
  await choose(page, "take ink-ribbon");
  expect((await snapshot(page)).inventory).toEqual(["ink-ribbon"]);
  expect(await labels(page)).toContain("move to type-writer");
  expect(await labels(page)).toContain("use type-writer");

  await choose(page, "use type-writer");
  const typed = await snapshot(page);
  expect(typed.inventory).toEqual([]);
  expect(typed.flags.typedClue).toBe(true);
  expect(await labels(page)).toContain("move to brass key");

  await choose(page, "move to brass key");
  await choose(page, "take brass key");
  expect((await snapshot(page)).inventory).toEqual(["brass-key"]);
  expect(await labels(page)).toContain("move to study door");
  expect(await labels(page)).toContain("unlock door");

  await choose(page, "unlock door");
  const unlocked = await snapshot(page);
  expect(unlocked.doors["study-door"].open).toBe(true);
  expect(unlocked.doors["study-door"].locked).toBe(false);
  expect(await labels(page)).toContain("enter the study");

  await choose(page, "enter the study");
  expect((await snapshot(page)).player.roomId).toBe("study");
  expect(await labels(page)).toContain("move to daybook");
  expect(await labels(page)).toContain("read daybook");

  await choose(page, "read daybook");
  expect((await snapshot(page)).flags.readDaybook).toBe(true);
  expect(await labels(page)).not.toContain("read daybook");
});

test("map, inventory and 3D graph open full screen and confirm a route", async ({ page }) => {
  await page.keyboard.press("m");
  await expect(page.getByRole("heading", { name: "Map" })).toBeVisible();
  await expect(page.locator(".map-view .tldraw-host canvas")).toBeVisible();
  await page.keyboard.press("m");
  await expect(page.getByRole("heading", { name: "Map" })).toBeHidden();

  await page.keyboard.press("i");
  await expect(page.getByRole("heading", { name: "Inventory" })).toBeVisible();
  await page.keyboard.press("Escape");
  await expect(page.getByRole("heading", { name: "Inventory" })).toBeHidden();

  await page.keyboard.press("t");
  await expect(page.locator(".graph-view-3d canvas")).toBeVisible();
  await page.waitForTimeout(400);
  const point = await page.evaluate(() => window.__ink3d?.point("move:green-herb") ?? null);
  expect(point).not.toBeNull();
  await page.mouse.click(point!.x, point!.y);
  await expect(page.locator(".confirm-copy strong")).toHaveText("move to green herb");
  await page.getByRole("button", { name: /^Confirm/ }).click();
  await waitUntilStill(page);
  expect((await snapshot(page)).player.pos.x).toBeCloseTo(9.2, 1);
  await page.keyboard.press("t");
  await expect(page.locator(".graph-view-3d")).toBeVisible();
});

test("level editor maps a dragged object into the three.js level and persists it", async ({ page }) => {
  await page.keyboard.press("e");
  await expect(page.getByRole("heading", { name: "Level editor" })).toBeVisible();

  const shape = page.locator('[data-shape-id="shape:game-entity-ink-ribbon"]');
  await expect(shape).toBeVisible();
  const box = await shape.boundingBox();
  expect(box).not.toBeNull();
  const startX = box!.x + box!.width / 2;
  const startY = box!.y + box!.height / 2;
  await page.mouse.move(startX, startY);
  await page.mouse.down();
  await page.mouse.move(startX + 110, startY + 50, { steps: 8 });
  await page.mouse.up();

  await page.getByRole("button", { name: "Apply & play" }).click();
  await expect(page.getByRole("heading", { name: "Level editor" })).toBeHidden();

  const moved = await page.evaluate(() => {
    const entity = window.__ink!.level().entities.find((candidate) => candidate.id === "ink-ribbon");
    return entity ? entity.pos : null;
  });
  expect(moved).not.toBeNull();
  expect(moved!.x).toBeGreaterThan(6.8);

  await page.reload({ waitUntil: "networkidle" });
  await page.waitForFunction(() => Boolean(window.__ink));
  const persisted = await page.evaluate(() => {
    const entity = window.__ink!.level().entities.find((candidate) => candidate.id === "ink-ribbon");
    return entity ? entity.pos : null;
  });
  expect(persisted).not.toBeNull();
  expect(persisted!.x).toBeCloseTo(moved!.x, 1);
});
