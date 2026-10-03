-- Amicizie, presenza e attività.

-- ---------------------------------------------------------------- amicizie --
-- Una riga per relazione, con direzione (chi ha richiesto) e stato.
-- La coppia è ordinata nei confronti logici dal codice; qui teniamo richiedente
-- e destinatario espliciti per gestire le richieste in entrata/uscita.
CREATE TABLE IF NOT EXISTS lab_friendship (
  requester_id  BIGINT NOT NULL REFERENCES lab_account(account_id) ON DELETE CASCADE,
  addressee_id  BIGINT NOT NULL REFERENCES lab_account(account_id) ON DELETE CASCADE,
  status        TEXT   NOT NULL DEFAULT 'pending'
                  CHECK (status IN ('pending','accepted','blocked')),
  created_at    TIMESTAMPTZ NOT NULL DEFAULT now(),
  responded_at  TIMESTAMPTZ,
  PRIMARY KEY (requester_id, addressee_id),
  CHECK (requester_id <> addressee_id)
);
CREATE INDEX IF NOT EXISTS idx_friendship_addressee ON lab_friendship(addressee_id);
CREATE INDEX IF NOT EXISTS idx_friendship_requester ON lab_friendship(requester_id);

-- ---------------------------------------------------------------- presenza --
-- Uno stato di presenza per account. game_* servono per "sta giocando" e
-- restano NULL finché un componente autorizzato non li comunica.
CREATE TABLE IF NOT EXISTS lab_presence (
  account_id  BIGINT PRIMARY KEY REFERENCES lab_account(account_id) ON DELETE CASCADE,
  status      TEXT NOT NULL DEFAULT 'offline'
                CHECK (status IN ('online','offline','away','dnd')),
  game_id     TEXT,
  game_name   TEXT,
  started_at  TIMESTAMPTZ,            -- quando è iniziata la sessione di gioco
  last_seen   TIMESTAMPTZ NOT NULL DEFAULT now()
);

-- ---------------------------------------------------------------- attività --
-- Feed attività recenti (login, cambio stato, "ha iniziato a giocare", ...).
CREATE TABLE IF NOT EXISTS lab_activity (
  activity_id BIGSERIAL PRIMARY KEY,
  account_id  BIGINT NOT NULL REFERENCES lab_account(account_id) ON DELETE CASCADE,
  type        TEXT NOT NULL,          -- es. 'login','presence','game_start','game_stop'
  game_id     TEXT,
  game_name   TEXT,
  detail      TEXT,
  created_at  TIMESTAMPTZ NOT NULL DEFAULT now()
);
CREATE INDEX IF NOT EXISTS idx_activity_account ON lab_activity(account_id, created_at DESC);
