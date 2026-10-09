import { useState } from "react";
import { auth } from "void/client";

type SessionUser = { email?: string };
type AuthError = { message?: string } | null;
type AuthResult = { error?: AuthError } | undefined;
type AuthClient = {
  useSession: () => {
    data: { user?: SessionUser } | null;
    isPending: boolean;
  };
  signIn: { email: (value: { email: string; password: string }) => Promise<AuthResult> };
  signUp: {
    email: (value: { email: string; password: string; name: string }) => Promise<AuthResult>;
  };
  signOut: () => Promise<unknown>;
};

const client = auth as unknown as AuthClient;
const MIN_PASSWORD = 8;

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
    setError("");
    if (!email.includes("@")) {
      setError("Enter a valid email");
      return;
    }
    if (password.length < MIN_PASSWORD) {
      setError(`Password must be at least ${MIN_PASSWORD} characters`);
      return;
    }
    setBusy(true);
    try {
      const result =
        mode === "signup"
          ? await client.signUp.email({ email, password, name: email })
          : await client.signIn.email({ email, password });
      if (result?.error) {
        setError(result.error.message ?? "Request failed");
      }
    } catch (cause) {
      setError(cause instanceof Error ? cause.message : "Request failed");
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
        autoComplete="email"
        placeholder="email"
        value={email}
        onChange={(event) => setEmail(event.target.value)}
      />
      <input
        type="password"
        autoComplete="current-password"
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
