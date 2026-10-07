-- Segnalazioni di bug e richieste di funzioni dall'app (Impostazioni › Aiuto)
-- e funzioni social dello Store: chi ha installato cosa (per "Popolari tra i
-- tuoi amici"), lista dei desideri con avviso degli aggiornamenti.
CREATE TABLE IF NOT EXISTS lab_feedback (
  feedback_id  BIGSERIAL PRIMARY KEY,
  account_id   BIGINT REFERENCES lab_account(account_id) ON DELETE SET NULL,
  kind         TEXT NOT NULL CHECK (kind IN ('bug', 'idea')),
  body         TEXT NOT NULL,
  app_version  TEXT,
  lang         TEXT,
  log          TEXT,                                  -- coda del registro dell'app, solo se l'utente l'ha allegata
  status       TEXT NOT NULL DEFAULT 'open' CHECK (status IN ('open', 'done', 'dismissed')),
  created_at   TIMESTAMPTZ NOT NULL DEFAULT now()
);
CREATE INDEX IF NOT EXISTS idx_feedback_status ON lab_feedback(status, feedback_id DESC);

CREATE TABLE IF NOT EXISTS lab_store_install (
  app_id     BIGINT NOT NULL REFERENCES lab_store_app(app_id) ON DELETE CASCADE,
  account_id BIGINT NOT NULL REFERENCES lab_account(account_id) ON DELETE CASCADE,
  created_at TIMESTAMPTZ NOT NULL DEFAULT now(),
  PRIMARY KEY (app_id, account_id)
);
CREATE INDEX IF NOT EXISTS idx_store_install_acct ON lab_store_install(account_id);

CREATE TABLE IF NOT EXISTS lab_store_wish (
  app_id     BIGINT NOT NULL REFERENCES lab_store_app(app_id) ON DELETE CASCADE,
  account_id BIGINT NOT NULL REFERENCES lab_account(account_id) ON DELETE CASCADE,
  created_at TIMESTAMPTZ NOT NULL DEFAULT now(),
  PRIMARY KEY (app_id, account_id)
);
CREATE INDEX IF NOT EXISTS idx_store_wish_acct ON lab_store_wish(account_id);
