import { useState } from "react";
import { auth } from "void/client";

type SessionUser = { email?: string };
type AuthClient = {
  useSession: () => {
    data: { user?: SessionUser } | null;
    isPending: boolean;
  };
  signIn: { email: (value: { email: string; password: string }) => Promise<unknown> };
  signUp: {
    email: (value: { email: string; password: string; name: string }) => Promise<unknown>;
  };
  signOut: () => Promise<unknown>;
};

const client = auth as unknown as AuthClient;

export function AuthPanel() {
  const { data: session, isPending } = client.useSession();
  const [email, setEmail] = useState("");
  const [password, setPassword] = useState("");
  const [error, setError] = useState("");
  const [busy, setBusy] = useState(false);

  if (isPending) {
    return null;
  }

  if (session?.user) {
    return (
      <div className="auth">
        <span className="auth-user">{session.user.email}</span>
        <button type="button" onClick={() => void client.signOut()}>
          Sign out
        </button>
      </div>
    );
  }

  const submit = async (mode: "signin" | "signup") => {
    setBusy(true);
    setError("");
    try {
      if (mode === "signup") {
        await client.signUp.email({ email, password, name: email });
      } else {
        await client.signIn.email({ email, password });
      }
    } catch (cause) {
      setError(cause instanceof Error ? cause.message : "Sign in failed");
    } finally {
      setBusy(false);
    }
  };

  return (
    <form
      className="auth"
      onSubmit={(event) => {
        event.preventDefault();
        void submit("signin");
      }}
    >
      <input
        type="email"
        placeholder="email"
        value={email}
        onChange={(event) => setEmail(event.target.value)}
      />
      <input
        type="password"
        placeholder="password"
        value={password}
        onChange={(event) => setPassword(event.target.value)}
      />
      <button type="submit" disabled={busy}>
        Sign in
      </button>
      <button type="button" disabled={busy} onClick={() => void submit("signup")}>
        Sign up
      </button>
      {error && <span className="auth-error">{error}</span>}
    </form>
  );
}
