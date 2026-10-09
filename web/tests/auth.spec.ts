import { expect, test } from "@playwright/test";

test("sign up and save a run", async ({ request, baseURL }) => {
  const email = `player_${Date.now()}@example.com`;
  const origin = baseURL ?? "http://localhost:4173";

  const signup = await request.post("/api/auth/sign-up/email", {
    headers: { origin },
    data: { email, password: "password123", name: "Player" },
  });
  expect(signup.ok()).toBeTruthy();

  const save = await request.post("/runs", {
    headers: { origin },
    data: {
      levelId: "care-center-01",
      durationMs: 1234,
      completed: false,
      replay: [{ x: 89, y: 4 }],
    },
  });
  expect(save.ok()).toBeTruthy();

  const mine = await request.get("/runs", { headers: { origin } });
  expect(mine.ok()).toBeTruthy();
  const body = (await mine.json()) as { runs?: unknown[] };
  expect((body.runs ?? []).length).toBeGreaterThan(0);
});

test("sign up through the form", async ({ page }) => {
  await page.goto("/");
  const email = `ui_${Date.now()}@example.com`;
  await page.locator('input[type="email"]').click();
  await page.keyboard.type(email);
  await page.locator('input[type="password"]').click();
  await page.keyboard.type("password123");
  await page.getByRole("button", { name: "Sign up" }).click();
  await expect(page.getByText(email)).toBeVisible({ timeout: 10_000 });
});
