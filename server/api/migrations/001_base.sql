-- Account e sessioni: la base su cui poggiano tutte le altre migrazioni.
CREATE EXTENSION IF NOT EXISTS pgcrypto;  -- gen_random_uuid()

CREATE TABLE IF NOT EXISTS lab_account (
  account_id   BIGSERIAL PRIMARY KEY,
  online_id    TEXT NOT NULL UNIQUE,
  created_at   TIMESTAMPTZ NOT NULL DEFAULT now()
);

CREATE TABLE IF NOT EXISTS lab_session (
  session_id   UUID PRIMARY KEY DEFAULT gen_random_uuid(),
  account_id   BIGINT NOT NULL REFERENCES lab_account(account_id) ON DELETE CASCADE,
  issued_at    TIMESTAMPTZ NOT NULL DEFAULT now(),
  expires_at   TIMESTAMPTZ NOT NULL,
  revoked      BOOLEAN NOT NULL DEFAULT false
);
CREATE INDEX IF NOT EXISTS idx_lab_session_account ON lab_session(account_id);
