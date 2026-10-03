-- Profilo esteso, notifiche, messaggi diretti, party e notizie.
-- Le notifiche nascono dagli eventi social e l'app le riceve con GET /api/v1/sync.

-- ------------------------------------------------------- profilo esteso --
ALTER TABLE lab_account ADD COLUMN IF NOT EXISTS about_me TEXT;
ALTER TABLE lab_account ADD COLUMN IF NOT EXISTS avatar   INT NOT NULL DEFAULT 0;

-- ------------------------------------------------------------- notifiche --
CREATE TABLE IF NOT EXISTS lab_notification (
  notification_id BIGSERIAL PRIMARY KEY,
  account_id      BIGINT NOT NULL REFERENCES lab_account(account_id) ON DELETE CASCADE,
  type            TEXT NOT NULL,   -- friend_request, friend_accept, message, party_invite, game_start, online, news
  actor_id        BIGINT REFERENCES lab_account(account_id) ON DELETE CASCADE,
  title           TEXT NOT NULL,
  body            TEXT,
  ref             TEXT,            -- es. online_id, party_id, game_id
  read            BOOLEAN NOT NULL DEFAULT false,
  created_at      TIMESTAMPTZ NOT NULL DEFAULT now()
);
CREATE INDEX IF NOT EXISTS idx_notification_account ON lab_notification(account_id, notification_id DESC);

-- -------------------------------------------------------------- messaggi --
-- Messaggi diretti tra due account (to_id) oppure nella chat di un party.
CREATE TABLE IF NOT EXISTS lab_party (
  party_id    BIGSERIAL PRIMARY KEY,
  owner_id    BIGINT NOT NULL REFERENCES lab_account(account_id) ON DELETE CASCADE,
  name        TEXT NOT NULL,
  closed      BOOLEAN NOT NULL DEFAULT false,
  created_at  TIMESTAMPTZ NOT NULL DEFAULT now()
);

CREATE TABLE IF NOT EXISTS lab_party_member (
  party_id    BIGINT NOT NULL REFERENCES lab_party(party_id) ON DELETE CASCADE,
  account_id  BIGINT NOT NULL REFERENCES lab_account(account_id) ON DELETE CASCADE,
  muted       BOOLEAN NOT NULL DEFAULT false,
  joined_at   TIMESTAMPTZ NOT NULL DEFAULT now(),
  PRIMARY KEY (party_id, account_id)
);
-- un account sta in un solo party alla volta
CREATE UNIQUE INDEX IF NOT EXISTS ux_party_member_account ON lab_party_member(account_id);

CREATE TABLE IF NOT EXISTS lab_party_invite (
  party_id    BIGINT NOT NULL REFERENCES lab_party(party_id) ON DELETE CASCADE,
  from_id     BIGINT NOT NULL REFERENCES lab_account(account_id) ON DELETE CASCADE,
  to_id       BIGINT NOT NULL REFERENCES lab_account(account_id) ON DELETE CASCADE,
  created_at  TIMESTAMPTZ NOT NULL DEFAULT now(),
  PRIMARY KEY (party_id, to_id)
);

CREATE TABLE IF NOT EXISTS lab_message (
  message_id  BIGSERIAL PRIMARY KEY,
  from_id     BIGINT NOT NULL REFERENCES lab_account(account_id) ON DELETE CASCADE,
  to_id       BIGINT REFERENCES lab_account(account_id) ON DELETE CASCADE,
  party_id    BIGINT REFERENCES lab_party(party_id) ON DELETE CASCADE,
  body        TEXT NOT NULL,
  read_at     TIMESTAMPTZ,
  created_at  TIMESTAMPTZ NOT NULL DEFAULT now(),
  CHECK ((to_id IS NULL) <> (party_id IS NULL))
);
CREATE INDEX IF NOT EXISTS idx_message_dm    ON lab_message(LEAST(from_id, to_id), GREATEST(from_id, to_id), message_id) WHERE to_id IS NOT NULL;
CREATE INDEX IF NOT EXISTS idx_message_to    ON lab_message(to_id) WHERE read_at IS NULL;
CREATE INDEX IF NOT EXISTS idx_message_party ON lab_message(party_id, message_id) WHERE party_id IS NOT NULL;

-- ------------------------------------------------------------------ news --
CREATE TABLE IF NOT EXISTS lab_news (
  news_id     BIGSERIAL PRIMARY KEY,
  title       TEXT NOT NULL,
  body        TEXT NOT NULL,
  tag         TEXT NOT NULL DEFAULT 'Omega',
  game_id     TEXT,                -- NULL = news generale
  color       INT NOT NULL DEFAULT 0,
  created_at  TIMESTAMPTZ NOT NULL DEFAULT now()
);
CREATE INDEX IF NOT EXISTS idx_news_game ON lab_news(game_id, created_at DESC);

INSERT INTO lab_news (title, body, tag, color, created_at)
SELECT * FROM (VALUES
  ('Benvenuto in Omega', 'La tua nuova home: giochi, amici, party e messaggi in un unico posto. Premi OPTIONS per il Centro di controllo.', 'Omega', 0, now() - interval '3 hours'),
  ('Party e chat sono online', 'Crea un party, invita i tuoi amici e chatta in tempo reale mentre giocate.', 'Novità', 1, now() - interval '2 hours'),
  ('Scopri chi sta giocando', 'Sotto ogni gioco vedi quali amici ci stanno giocando adesso e le ultime attività.', 'Funzioni', 2, now() - interval '1 hour'),
  ('Notifiche in tempo reale', 'Richieste di amicizia, messaggi e inviti arrivano come notifiche sulla home.', 'Omega', 3, now())
) AS v(title, body, tag, color, created_at)
WHERE NOT EXISTS (SELECT 1 FROM lab_news);
