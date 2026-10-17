import { useEffect, useMemo, useState } from "react";
import { Link } from "@void/react";
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

function ReplayViewer({ replay, durationMs }: { replay: string; durationMs: number }) {
  const points = useMemo(() => {
    try {
      const parsed = JSON.parse(replay) as { x: number; y: number; t?: number }[];
      return Array.isArray(parsed) ? parsed : [];
    } catch {
      return [];
    }
  }, [replay]);
  const [playing, setPlaying] = useState(false);
  const [fast, setFast] = useState(false);
  const [pos, setPos] = useState(0); // ms into the run
  const total = Math.max(1, durationMs || points[points.length - 1]?.t || points.length * 100);
  useEffect(() => {
    if (!playing) return;
    const started = performance.now() - pos / (fast ? 4 : 1);
    let raf = 0;
    const tick = () => {
      const next = (performance.now() - started) * (fast ? 4 : 1);
      if (next >= total) { setPos(total); setPlaying(false); return; }
      setPos(next);
      raf = requestAnimationFrame(tick);
    };
    raf = requestAnimationFrame(tick);
    return () => cancelAnimationFrame(raf);
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [playing, fast, total]);

  if (points.length < 2) return null;
  const xs = points.map((p) => p.x);
  const ys = points.map((p) => p.y);
  const minX = Math.min(...xs);
  const minY = Math.min(...ys);
  const width = Math.max(1, Math.max(...xs) - minX);
  const height = Math.max(1, Math.max(...ys) - minY);
  const scale = 420 / Math.max(width, height);
  const at = (p: { x: number; y: number; t?: number }, i: number) => p.t ?? (i / Math.max(1, points.length - 1)) * total;
  let head = points[0];
  for (let i = 0; i < points.length; i++) if (at(points[i], i) <= pos) head = points[i];
  const shown = points.filter((p, i) => at(p, i) <= pos);
  const d = shown
    .map((p, i) => `${i ? "L" : "M"}${((p.x - minX) * scale).toFixed(1)},${((p.y - minY) * scale).toFixed(1)}`)
    .join(" ");
  return (
    <div className="replay-player">
      <svg className="replay" width={width * scale + 8} height={height * scale + 8}>
        {d && <path d={d} fill="none" stroke="#961eff" strokeWidth="2" />}
        <circle cx={(head.x - minX) * scale} cy={(head.y - minY) * scale} r="6" fill="#961eff" />
      </svg>
      <div className="replay-controls">
        <button type="button" onClick={() => { if (pos >= total) setPos(0); setPlaying((v) => !v); }}>
          {playing ? "Pause" : "Play"}
        </button>
        <button type="button" data-active={fast} onClick={() => setFast((v) => !v)}>Fast-forward</button>
        <span>{formatMs(pos)} / {formatMs(total)}</span>
      </div>
    </div>
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
        <Link href="/">Back to map</Link>
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
                <ReplayViewer replay={run.replay} durationMs={run.durationMs} />
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
                <ReplayViewer replay={run.replay} durationMs={run.durationMs} />
                <span className="records-time">{formatMs(run.durationMs)}</span>
              </li>
            ))}
          </ul>
        )}
      </section>
    </main>
  );
}
