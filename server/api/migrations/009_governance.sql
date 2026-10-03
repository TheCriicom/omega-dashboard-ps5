-- Ruoli e ban, consenso ai termini, segnalazioni con oscuramento automatico,
-- sessioni del pannello admin e registro delle azioni di moderazione.
ALTER TABLE lab_account ADD COLUMN IF NOT EXISTS role              TEXT NOT NULL DEFAULT 'user';   -- user | admin
ALTER TABLE lab_account ADD COLUMN IF NOT EXISTS banned_at         TIMESTAMPTZ;
ALTER TABLE lab_account ADD COLUMN IF NOT EXISTS ban_reason        TEXT;
ALTER TABLE lab_account ADD COLUMN IF NOT EXISTS terms_version     TEXT;
ALTER TABLE lab_account ADD COLUMN IF NOT EXISTS terms_accepted_at TIMESTAMPTZ;

ALTER TABLE lab_store_app ADD COLUMN IF NOT EXISTS hidden_reason    TEXT;
ALTER TABLE lab_store_app ADD COLUMN IF NOT EXISTS rights_confirmed BOOLEAN NOT NULL DEFAULT false;

ALTER TABLE lab_store_comment ADD COLUMN IF NOT EXISTS hidden BOOLEAN NOT NULL DEFAULT false;

-- segnalazioni: su un homebrew (comment_id NULL) o su un suo commento
CREATE TABLE IF NOT EXISTS lab_store_report (
  report_id   BIGSERIAL PRIMARY KEY,
  app_id      BIGINT NOT NULL REFERENCES lab_store_app(app_id) ON DELETE CASCADE,
  comment_id  BIGINT REFERENCES lab_store_comment(comment_id) ON DELETE CASCADE,
  reporter_id BIGINT REFERENCES lab_account(account_id) ON DELETE SET NULL,
  reason      TEXT NOT NULL,       -- pirateria | malware | contenuto_offensivo | spam | link_rotto | altro
  note        TEXT,
  status      TEXT NOT NULL DEFAULT 'open',     -- open | resolved | dismissed
  resolved_by BIGINT REFERENCES lab_account(account_id) ON DELETE SET NULL,
  resolved_at TIMESTAMPTZ,
  created_at  TIMESTAMPTZ NOT NULL DEFAULT now()
);
CREATE INDEX IF NOT EXISTS idx_store_report_status ON lab_store_report(status, created_at DESC);
-- una segnalazione aperta per utente e contenuto
CREATE UNIQUE INDEX IF NOT EXISTS idx_store_report_once
  ON lab_store_report(app_id, coalesce(comment_id, 0), reporter_id) WHERE status = 'open';

-- sessioni del pannello admin (cookie HttpOnly): separate da quelle della console
CREATE TABLE IF NOT EXISTS lab_admin_session (
  session_id  UUID PRIMARY KEY DEFAULT gen_random_uuid(),
  account_id  BIGINT NOT NULL REFERENCES lab_account(account_id) ON DELETE CASCADE,
  created_at  TIMESTAMPTZ NOT NULL DEFAULT now(),
  expires_at  TIMESTAMPTZ NOT NULL
);

CREATE TABLE IF NOT EXISTS lab_admin_log (
  log_id      BIGSERIAL PRIMARY KEY,
  admin_id    BIGINT REFERENCES lab_account(account_id) ON DELETE SET NULL,
  admin_name  TEXT,
  action      TEXT NOT NULL,
  target_type TEXT,
  target_id   TEXT,
  detail      TEXT,
  created_at  TIMESTAMPTZ NOT NULL DEFAULT now()
);
CREATE INDEX IF NOT EXISTS idx_admin_log_created ON lab_admin_log(created_at DESC);
