import { sql } from "void/db";
import { integer, sqliteTable, text } from "void/schema-d1";

export const runs = sqliteTable("runs", {
  id: integer("id").primaryKey({ autoIncrement: true }),
  userId: text("user_id").notNull(),
  playerName: text("player_name").notNull().default(""),
  levelId: text("level_id").notNull(),
  durationMs: integer("duration_ms").notNull(),
  completed: integer("completed").notNull().default(0),
  replay: text("replay").notNull().default("[]"),
  createdAt: text("created_at")
    .notNull()
    .default(sql`(datetime('now'))`),
});
