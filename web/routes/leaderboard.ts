import { defineHandler } from "void";
import { asc, db, eq } from "void/db";
import { runs } from "@schema";

export const GET = defineHandler(async () => {
  const rows = await db
    .select()
    .from(runs)
    .where(eq(runs.completed, 1))
    .orderBy(asc(runs.durationMs))
    .limit(200);
  const best = new Map<string, (typeof rows)[number]>();
  for (const row of rows) {
    if (!best.has(row.userId)) {
      best.set(row.userId, row);
    }
  }
  return { leaderboard: [...best.values()].slice(0, 50) };
});
