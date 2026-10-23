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
  await expect(page.locator(".route-chips")).toContainText("move to ink-ribbon");
  await expect(page.locator(".route-chips")).toContainText("take ink-ribbon");
});

test("move nodes can be chained into a multi-waypoint route", async ({ page }) => {
  await page.keyboard.press("g");
  await page.getByRole("button", { name: "Decision: move to green herb" }).click();
  await page.getByRole("button", { name: "Decision: move to ink-ribbon" }).click();
  await expect(page.locator(".route-chip")).toHaveCount(2);
  await page.getByRole("button", { name: /^Confirm/ }).click();
  const pending = await page.evaluate(() => window.__ink!.snapshot().pending);
  expect(pending?.label).toContain("move to green herb");
  expect(pending?.label).toContain("move to ink-ribbon");
  await waitUntilStill(page);
  const finalPosition = (await snapshot(page)).player.pos;
  expect(finalPosition.x).toBeCloseTo(6, 1);
  expect(finalPosition.z).toBeCloseTo(6.2, 1);
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

  await page.keyboard.press("g");
  await page.getByRole("button", { name: "Decision: enter the study" }).click();
  await expect(page.locator(".route-option")).toHaveCount(2);
  await expect(page.locator(".route-option", { hasText: "via the arch" })).toBeVisible();
  await expect(page.locator(".route-option", { hasText: "via the study door" })).toBeVisible();
  await page.locator(".route-option", { hasText: "via the arch" }).click();
  await page.getByRole("button", { name: /^Confirm/ }).click();
  const routePending = await page.evaluate(() => window.__ink!.snapshot().pending);
  expect(routePending?.label).toContain("via the arch");
  await waitUntilStill(page);
  expect((await snapshot(page)).player.roomId).toBe("study");
  expect(await labels(page)).toContain("move to daybook");
  expect(await labels(page)).toContain("read daybook");
  expect(await labels(page)).not.toContain("open the drawer");

  await choose(page, "read daybook");
  expect((await snapshot(page)).flags.readDaybook).toBe(true);
  expect(await labels(page)).not.toContain("read daybook");
  expect(await labels(page)).toContain("open the drawer");

  await choose(page, "move to study cabinet");
  await choose(page, "open the drawer");
  expect((await snapshot(page)).status).toBe("won");
  await expect(page.getByRole("heading", { name: "The Drawer Opens" })).toBeVisible();
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

  await page.getByRole("button", { name: "Room", exact: true }).click();
  await page.getByRole("button", { name: "Wall", exact: true }).click();
  await page.waitForTimeout(250);

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

  const wallShape = page.locator('[data-shape-id="shape:wall-wall-study-north"]');
  const wallBox = await wallShape.boundingBox();
  expect(wallBox).not.toBeNull();
  const wallX = wallBox!.x + wallBox!.width * 0.35;
  const wallY = wallBox!.y + wallBox!.height / 2;
  await page.mouse.move(wallX, wallY);
  await page.mouse.down();
  await page.mouse.move(wallX + 20, wallY - 35, { steps: 8 });
  await page.mouse.up();
  await page.waitForTimeout(250);

  await page.getByRole("button", { name: "Apply & play" }).click();
  await expect(page.getByRole("heading", { name: "Level editor" })).toBeHidden();

  const moved = await page.evaluate(() => {
    const level = window.__ink!.level();
    const entity = level.entities.find((candidate) => candidate.id === "ink-ribbon");
    return {
      pos: entity ? entity.pos : null,
      rooms: level.rooms.length,
      walls: level.walls.length,
      wallZ: level.walls.find((wall) => wall.id === "wall-study-north")?.a.z ?? null,
    };
  });
  expect(moved.pos).not.toBeNull();
  expect(moved.pos!.x).toBeGreaterThan(6.8);
  expect(moved.rooms).toBe(3);
  expect(moved.walls).toBe(10);
  expect(moved.wallZ).not.toBeNull();
  expect(moved.wallZ!).toBeLessThan(-7.1);

  await page.reload({ waitUntil: "networkidle" });
  await page.waitForFunction(() => Boolean(window.__ink));
  const persisted = await page.evaluate(() => {
    const level = window.__ink!.level();
    const entity = level.entities.find((candidate) => candidate.id === "ink-ribbon");
    return {
      pos: entity ? entity.pos : null,
      rooms: level.rooms.length,
      walls: level.walls.length,
      wallZ: level.walls.find((wall) => wall.id === "wall-study-north")?.a.z ?? null,
    };
  });
  expect(persisted.pos).not.toBeNull();
  expect(persisted.pos!.x).toBeCloseTo(moved.pos!.x, 1);
  expect(persisted.rooms).toBe(3);
  expect(persisted.walls).toBe(10);
  expect(persisted.wallZ).not.toBeNull();
  expect(persisted.wallZ!).toBeCloseTo(moved.wallZ!, 1);
});
