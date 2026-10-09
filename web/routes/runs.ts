import { defineHandler } from "void";
import { getUser, requireAuth } from "void/auth";
import { db, desc, eq } from "void/db";
import { runs } from "@schema";

export const GET = defineHandler(async () => {
  const user = getUser();
  if (!user) {
    return { runs: [] };
  }
  const rows = await db
    .select()
    .from(runs)
    .where(eq(runs.userId, user.id))
    .orderBy(desc(runs.createdAt));
  return { runs: rows };
});

export const POST = defineHandler(async (c) => {
  const user = requireAuth(c);
  const body = await c.req.json<{
    levelId?: string;
    durationMs?: number;
    completed?: boolean;
    replay?: unknown;
  }>();
  const [created] = await db
    .insert(runs)
    .values({
      userId: user.id,
      playerName: user.name ?? "",
      levelId: String(body.levelId ?? "care-center-01"),
      durationMs: Number(body.durationMs ?? 0),
      completed: body.completed ? 1 : 0,
      replay: JSON.stringify(body.replay ?? []),
    })
    .returning();
  return { run: created };
});
