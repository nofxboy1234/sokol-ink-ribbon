import { useEffect, useState } from "react";
import "../src/app.css";
import { formatMs } from "../src/panes";

type Run = {
  id: number;
  playerName: string;
  durationMs: number;
  completed: number;
  createdAt: string;
};

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
                <span className="records-time">{formatMs(run.durationMs)}</span>
              </li>
            ))}
          </ul>
        )}
      </section>
    </main>
  );
}
