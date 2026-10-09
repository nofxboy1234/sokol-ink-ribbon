import { defineConfig } from "@playwright/test";

export default defineConfig({
  testDir: "./tests",
  timeout: 60_000,
  use: {
    baseURL: "http://localhost:4173",
  },
  webServer: {
    command:
      "BETTER_AUTH_SECRET=playwright-local-secret npm run build && BETTER_AUTH_SECRET=playwright-local-secret npm run preview -- --port 4173 --strictPort",
    url: "http://localhost:4173",
    reuseExistingServer: !process.env.CI,
    timeout: 300_000,
  },
});
