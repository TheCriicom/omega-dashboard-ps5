-- Salvataggi online (stile PSN Plus). La console cifra tutto prima di spedire
-- (XChaCha20-Poly1305) con una chiave che il server non vede mai: qui restano
-- solo blocchi opachi e, per ritrovare la chiave su un'altra console, la stessa
-- chiave cifrata con una parola d'ordine scelta dall'utente (Argon2id).
-- Il server non apre, non decomprime e non interpreta i file.

CREATE TABLE IF NOT EXISTS lab_save_key (
  account_id  BIGINT PRIMARY KEY REFERENCES lab_account(account_id) ON DELETE CASCADE,
  key_id      TEXT NOT NULL,              -- 16 esadecimali: impronta pubblica della chiave
  kdf_mem     INT NOT NULL,               -- KiB di Argon2id
  kdf_iter    INT NOT NULL,
  salt        BYTEA NOT NULL,             -- 16 byte
  nonce       BYTEA NOT NULL,             -- 24 byte
  wrapped     BYTEA NOT NULL,             -- 32 byte di chiave + 16 di MAC
  created_at  TIMESTAMPTZ NOT NULL DEFAULT now(),
  updated_at  TIMESTAMPTZ NOT NULL DEFAULT now()
);

CREATE TABLE IF NOT EXISTS lab_save (
  save_id      TEXT PRIMARY KEY,          -- 32 esadecimali casuali, scelti dal server
  account_id   BIGINT NOT NULL REFERENCES lab_account(account_id) ON DELETE CASCADE,
  title_id     TEXT NOT NULL,
  key_id       TEXT NOT NULL,
  size         BIGINT NOT NULL,
  chunks       INT NOT NULL,
  sha256       TEXT NOT NULL,
  device       TEXT,
  status       TEXT NOT NULL DEFAULT 'uploading',   -- uploading | ready
  created_at   TIMESTAMPTZ NOT NULL DEFAULT now(),
  committed_at TIMESTAMPTZ
);
CREATE INDEX IF NOT EXISTS idx_save_acct ON lab_save(account_id, title_id, committed_at DESC);
CREATE INDEX IF NOT EXISTS idx_save_pending ON lab_save(created_at) WHERE status = 'uploading';
