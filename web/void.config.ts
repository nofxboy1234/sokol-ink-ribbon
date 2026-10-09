import { defineConfig } from "void/config";

export default defineConfig({
  worker: {
    compatibility_date: "2026-02-24",
  },
  auth: {
    providers: ["email"],
  },
  // The Worker already exists on Cloudflare with its workers.dev subdomain and
  // preview URLs enabled and no custom routes. Declaring them here (instead of
  // leaving them dashboard-managed) lets Void republish the same traffic
  // attachment during a version deploy.
  cloudflare: {
    workers_dev: true,
    preview_urls: true,
    routes: [],
  },
});
