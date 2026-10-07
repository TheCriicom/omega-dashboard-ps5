-- Web app (play.omegasuite.it/app): la console di ogni account si annuncia al
-- server (indirizzo nella rete di casa e token del telecomando), così il
-- telefono sa se è accesa e si collega direttamente a lei in Wi-Fi per le
-- funzioni che toccano la console. "Installa sulla PS5" dallo Store del
-- telefono è una coda di comandi: la console la legge e installa da sé.
CREATE TABLE IF NOT EXISTS lab_console (
  account_id   BIGINT PRIMARY KEY REFERENCES lab_account(account_id) ON DELETE CASCADE,
  lan_ip       TEXT,
  port         INT NOT NULL DEFAULT 9095,
  remote_token TEXT,
  version      TEXT,
  updated_at   TIMESTAMPTZ NOT NULL DEFAULT now()
);

CREATE TABLE IF NOT EXISTS lab_console_queue (
  queue_id    BIGSERIAL PRIMARY KEY,
  account_id  BIGINT NOT NULL REFERENCES lab_account(account_id) ON DELETE CASCADE,
  app_id      BIGINT NOT NULL REFERENCES lab_store_app(app_id) ON DELETE CASCADE,
  created_at  TIMESTAMPTZ NOT NULL DEFAULT now(),
  taken_at    TIMESTAMPTZ
);
CREATE INDEX IF NOT EXISTS idx_console_queue_acct ON lab_console_queue(account_id) WHERE taken_at IS NULL;
