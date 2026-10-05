-- Presenza che si spegne da sola, record pubblici, schede dei giochi e trofei
-- importati dalla console.

-- ---------------------------------------------------------- presenza --
-- swept = offline messo dal giro periodico (console sparita), non dall'utente:
-- al primo segnale di vita la riga torna online da sola.
ALTER TABLE lab_presence ADD COLUMN IF NOT EXISTS swept BOOLEAN NOT NULL DEFAULT false;

-- ----------------------------------------------------------- privacy --
ALTER TABLE lab_account ADD COLUMN IF NOT EXISTS show_in_records  BOOLEAN NOT NULL DEFAULT true;        -- record pubblici del tempo di gioco
ALTER TABLE lab_account ADD COLUMN IF NOT EXISTS privacy_trophies TEXT    NOT NULL DEFAULT 'everyone';  -- everyone | friends | nobody
ALTER TABLE lab_account ADD COLUMN IF NOT EXISTS import_trophies  BOOLEAN NOT NULL DEFAULT true;        -- l'app può mandare i trofei della console

-- ------------------------------------------------------------ giochi --
-- Nome e icona di un gioco, mandati dalla prima console che lo ha installato:
-- servono dove il gioco compare a chi non lo possiede (record, profili).
CREATE TABLE IF NOT EXISTS lab_game (
  game_id    TEXT PRIMARY KEY,
  name       TEXT,
  icon_media TEXT,
  named_by   BIGINT REFERENCES lab_account(account_id) ON DELETE SET NULL,
  icon_by    BIGINT REFERENCES lab_account(account_id) ON DELETE SET NULL,
  updated_at TIMESTAMPTZ NOT NULL DEFAULT now()
);

-- ------------------------------------------------------------ trofei --
-- Un set di trofei (np_id = NPWRxxxxx_00) come lo descrive la console:
-- trophies = [{ id, grade: P|G|S|B, hidden, name, detail }].
CREATE TABLE IF NOT EXISTS lab_tset (
  np_id      TEXT PRIMARY KEY,
  title      TEXT,
  icon_media TEXT,
  trophies   JSONB NOT NULL DEFAULT '[]'::jsonb,
  total_p    INT NOT NULL DEFAULT 0,
  total_g    INT NOT NULL DEFAULT 0,
  total_s    INT NOT NULL DEFAULT 0,
  total_b    INT NOT NULL DEFAULT 0,
  defined_by BIGINT REFERENCES lab_account(account_id) ON DELETE SET NULL,
  created_at TIMESTAMPTZ NOT NULL DEFAULT now()
);

-- Lo stato di un set per un account. raw è il file della console così com'è:
-- si rilegge quando arriva la definizione del set o cambia il lettore.
-- earned = { "<id>": "<ISO>" | true }.
CREATE TABLE IF NOT EXISTS lab_tset_user (
  account_id  BIGINT NOT NULL REFERENCES lab_account(account_id) ON DELETE CASCADE,
  np_id       TEXT NOT NULL,
  raw         BYTEA,
  raw_tag     TEXT,                       -- "<byte>:<mtime>" del file sulla console
  parsed      BOOLEAN NOT NULL DEFAULT false,
  earned      JSONB NOT NULL DEFAULT '{}'::jsonb,
  earned_p    INT NOT NULL DEFAULT 0,
  earned_g    INT NOT NULL DEFAULT 0,
  earned_s    INT NOT NULL DEFAULT 0,
  earned_b    INT NOT NULL DEFAULT 0,
  points      INT NOT NULL DEFAULT 0,
  last_earned TIMESTAMPTZ,
  updated_at  TIMESTAMPTZ NOT NULL DEFAULT now(),
  PRIMARY KEY (account_id, np_id)
);
CREATE INDEX IF NOT EXISTS idx_tset_user_np ON lab_tset_user(np_id);
