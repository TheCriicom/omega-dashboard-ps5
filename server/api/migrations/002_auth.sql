-- Registrazione e accesso: hash scrypt della password, email facoltativa,
-- online_id unico senza distinzione tra maiuscole e minuscole.
ALTER TABLE lab_account ADD COLUMN IF NOT EXISTS password_hash TEXT;
ALTER TABLE lab_account ADD COLUMN IF NOT EXISTS email         TEXT;
ALTER TABLE lab_account ADD COLUMN IF NOT EXISTS disabled      BOOLEAN NOT NULL DEFAULT false;
ALTER TABLE lab_account ADD COLUMN IF NOT EXISTS last_login_at TIMESTAMPTZ;

CREATE UNIQUE INDEX IF NOT EXISTS ux_lab_account_online_id_ci ON lab_account (lower(online_id));
CREATE UNIQUE INDEX IF NOT EXISTS ux_lab_account_email_ci     ON lab_account (lower(email)) WHERE email IS NOT NULL;
