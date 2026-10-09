import { useEffect, useMemo, useState } from "react";
import "../src/app.css";
import { formatMs } from "../src/panes";

type Run = {
  id: number;
  playerName: string;
  durationMs: number;
  completed: number;
  createdAt: string;
  replay: string;
};

function ReplayViewer({ replay }: { replay: string }) {
  const points = useMemo(() => {
    try {
      const parsed = JSON.parse(replay) as { x: number; y: number }[];
      return Array.isArray(parsed) ? parsed : [];
    } catch {
      return [];
    }
  }, [replay]);

  if (points.length < 2) {
    return null;
  }

  const xs = points.map((p) => p.x);
  const ys = points.map((p) => p.y);
  const minX = Math.min(...xs);
  const minY = Math.min(...ys);
  const width = Math.max(1, Math.max(...xs) - minX);
  const height = Math.max(1, Math.max(...ys) - minY);
  const scale = 90 / Math.max(width, height);
  const d = points
    .map(
      (p, i) =>
        `${i ? "L" : "M"}${((p.x - minX) * scale).toFixed(1)},${((p.y - minY) * scale).toFixed(1)}`,
    )
    .join(" ");

  return (
    <svg className="replay" width={width * scale + 4} height={height * scale + 4}>
      <path d={d} fill="none" stroke="#961eff" strokeWidth="1.5" />
    </svg>
  );
}

export default function RecordsPage() {
  const [leaderboard, setLeaderboard] = useState<Run[]>([]);
  const [mine, setMine] = useState<Run[]>([]);

  useEffect(() => {
    fetch("/leaderboard")
      .then((response) => response.json() as Promise<{ leaderboard?: Run[] }>)
      .then((data) => setLeaderboard(data.leaderboard ?? []))
      .catch(() => setLeaderboard([]));
    fetch("/runs")
      .then((response) => response.json() as Promise<{ runs?: Run[] }>)
      .then((data) => setMine(data.runs ?? []))
      .catch(() => setMine([]));
  }, []);

  return (
    <main className="records">
      <header className="records-header">
        <h1>Records</h1>
        <a href="/">Back to map</a>
      </header>
      <section className="records-section">
        <h2>Leaderboard</h2>
        {leaderboard.length === 0 ? (
          <p className="records-empty">No completed runs yet.</p>
        ) : (
          <ol className="records-list">
            {leaderboard.map((run) => (
              <li key={run.id}>
                <span className="records-name">{run.playerName || "Anonymous"}</span>
                <ReplayViewer replay={run.replay} />
                <span className="records-time">{formatMs(run.durationMs)}</span>
              </li>
            ))}
          </ol>
        )}
      </section>
      <section className="records-section">
        <h2>Your runs</h2>
        {mine.length === 0 ? (
          <p className="records-empty">Sign in and save a run to see it here.</p>
        ) : (
          <ul className="records-list">
            {mine.map((run) => (
              <li key={run.id}>
                <span className="records-name">{run.createdAt}</span>
                <ReplayViewer replay={run.replay} />
                <span className="records-time">{formatMs(run.durationMs)}</span>
              </li>
            ))}
          </ul>
        )}
      </section>
    </main>
  );
}
