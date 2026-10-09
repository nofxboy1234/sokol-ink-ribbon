import { createContext, useContext, useEffect, useRef, useState, type ReactNode } from "react";
import { readSnapshot, runElapsedMs, uiRevision, type GameSnapshot } from "./wasmBridge";

const GameContext = createContext<GameSnapshot | null>(null);

export function GameStateProvider({ children }: { children: ReactNode }) {
  const [snapshot, setSnapshot] = useState<GameSnapshot>(() => readSnapshot(0));
  const revisionRef = useRef(-1);

  useEffect(() => {
    let frame = 0;
    const poll = () => {
      const revision = uiRevision();
      if (revision !== revisionRef.current) {
        revisionRef.current = revision;
        setSnapshot(readSnapshot(revision));
      }
      frame = requestAnimationFrame(poll);
    };
    frame = requestAnimationFrame(poll);
    return () => cancelAnimationFrame(frame);
  }, []);

  return <GameContext.Provider value={snapshot}>{children}</GameContext.Provider>;
}

export function useGameState(): GameSnapshot {
  const value = useContext(GameContext);
  if (!value) {
    throw new Error("useGameState must be used within GameStateProvider");
  }
  return value;
}

export function useElapsed(): number {
  const [elapsed, setElapsed] = useState(0);
  useEffect(() => {
    const id = setInterval(() => setElapsed(runElapsedMs()), 250);
    return () => clearInterval(id);
  }, []);
  return elapsed;
}
