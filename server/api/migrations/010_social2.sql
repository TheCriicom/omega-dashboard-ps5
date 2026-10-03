-- Bacheca, stato personalizzato, blocchi, privacy, segnalazioni social,
-- gruppi di chat e tempo di gioco (totale e settimanale).

-- ------------------------------------------------ stato personalizzato --
ALTER TABLE lab_account ADD COLUMN IF NOT EXISTS status_mode    TEXT NOT NULL DEFAULT 'online';  -- online | away | dnd | invisible
ALTER TABLE lab_account ADD COLUMN IF NOT EXISTS status_message TEXT;

-- ---------------------------------------------------------- privacy --
ALTER TABLE lab_account ADD COLUMN IF NOT EXISTS privacy_messages        TEXT    NOT NULL DEFAULT 'everyone';  -- everyone | friends
ALTER TABLE lab_account ADD COLUMN IF NOT EXISTS privacy_friend_requests TEXT    NOT NULL DEFAULT 'everyone';  -- everyone | friends_of_friends | nobody
ALTER TABLE lab_account ADD COLUMN IF NOT EXISTS show_activity           BOOLEAN NOT NULL DEFAULT true;

-- inizio del tratto di gioco non ancora contato in lab_playtime (NULL = niente aperto)
ALTER TABLE lab_presence ADD COLUMN IF NOT EXISTS play_since TIMESTAMPTZ;
CREATE INDEX IF NOT EXISTS idx_presence_play_open ON lab_presence(last_seen) WHERE play_since IS NOT NULL;

-- ----------------------------------------------------------- blocchi --
-- Tabella a sé (non lab_friendship.status): un blocco vale in tutte e due le
-- direzioni per richieste, messaggi, inviti, post e ricerca.
CREATE TABLE IF NOT EXISTS lab_block (
  blocker_id  BIGINT NOT NULL REFERENCES lab_account(account_id) ON DELETE CASCADE,
  blocked_id  BIGINT NOT NULL REFERENCES lab_account(account_id) ON DELETE CASCADE,
  created_at  TIMESTAMPTZ NOT NULL DEFAULT now(),
  PRIMARY KEY (blocker_id, blocked_id),
  CHECK (blocker_id <> blocked_id)
);
CREATE INDEX IF NOT EXISTS idx_block_blocked ON lab_block(blocked_id);

-- ---------------------------------------------------------- bacheca --
CREATE TABLE IF NOT EXISTS lab_post (
  post_id       BIGSERIAL PRIMARY KEY,
  author_id     BIGINT NOT NULL REFERENCES lab_account(account_id) ON DELETE CASCADE,
  body          TEXT NOT NULL,
  game_id       TEXT,
  game_name     TEXT,
  hidden        BOOLEAN NOT NULL DEFAULT false,
  hidden_reason TEXT,
  created_at    TIMESTAMPTZ NOT NULL DEFAULT now()
);
CREATE INDEX IF NOT EXISTS idx_post_author ON lab_post(author_id, post_id DESC);

CREATE TABLE IF NOT EXISTS lab_post_like (
  post_id     BIGINT NOT NULL REFERENCES lab_post(post_id) ON DELETE CASCADE,
  account_id  BIGINT NOT NULL REFERENCES lab_account(account_id) ON DELETE CASCADE,
  created_at  TIMESTAMPTZ NOT NULL DEFAULT now(),
  PRIMARY KEY (post_id, account_id)
);

CREATE TABLE IF NOT EXISTS lab_post_comment (
  comment_id  BIGSERIAL PRIMARY KEY,
  post_id     BIGINT NOT NULL REFERENCES lab_post(post_id) ON DELETE CASCADE,
  account_id  BIGINT NOT NULL REFERENCES lab_account(account_id) ON DELETE CASCADE,
  body        TEXT NOT NULL,
  hidden      BOOLEAN NOT NULL DEFAULT false,
  created_at  TIMESTAMPTZ NOT NULL DEFAULT now()
);
CREATE INDEX IF NOT EXISTS idx_post_comment_post ON lab_post_comment(post_id, comment_id);

-- ------------------------------------------------ segnalazioni social --
-- target_id: post_id, comment_id oppure account_id. Nessuna FK: la
-- segnalazione resta nel registro anche se il contenuto viene eliminato.
CREATE TABLE IF NOT EXISTS lab_social_report (
  report_id   BIGSERIAL PRIMARY KEY,
  target_type TEXT NOT NULL CHECK (target_type IN ('user','post','post_comment')),
  target_id   BIGINT NOT NULL,
  target_owner_id BIGINT REFERENCES lab_account(account_id) ON DELETE SET NULL,  -- autore/utente segnalato
  reporter_id BIGINT REFERENCES lab_account(account_id) ON DELETE SET NULL,
  reason      TEXT NOT NULL,      -- spam | molestie | contenuto_offensivo | impersonificazione | altro
  note        TEXT,
  snapshot    TEXT,               -- testo del contenuto al momento della segnalazione
  status      TEXT NOT NULL DEFAULT 'open',   -- open | resolved | dismissed
  resolved_by BIGINT REFERENCES lab_account(account_id) ON DELETE SET NULL,
  resolved_at TIMESTAMPTZ,
  created_at  TIMESTAMPTZ NOT NULL DEFAULT now()
);
CREATE INDEX IF NOT EXISTS idx_social_report_status ON lab_social_report(status, created_at DESC);
CREATE INDEX IF NOT EXISTS idx_social_report_target ON lab_social_report(target_type, target_id);
CREATE UNIQUE INDEX IF NOT EXISTS idx_social_report_once
  ON lab_social_report(target_type, target_id, reporter_id) WHERE status = 'open';

-- ----------------------------------------------------------- gruppi --
CREATE TABLE IF NOT EXISTS lab_group (
  group_id      BIGSERIAL PRIMARY KEY,
  name          TEXT NOT NULL,
  owner_id      BIGINT REFERENCES lab_account(account_id) ON DELETE SET NULL,
  created_at    TIMESTAMPTZ NOT NULL DEFAULT now(),
  last_activity TIMESTAMPTZ NOT NULL DEFAULT now()
);

CREATE TABLE IF NOT EXISTS lab_group_member (
  group_id      BIGINT NOT NULL REFERENCES lab_group(group_id) ON DELETE CASCADE,
  account_id    BIGINT NOT NULL REFERENCES lab_account(account_id) ON DELETE CASCADE,
  joined_at     TIMESTAMPTZ NOT NULL DEFAULT clock_timestamp(),
  last_read_id  BIGINT NOT NULL DEFAULT 0,
  PRIMARY KEY (group_id, account_id)
);
CREATE INDEX IF NOT EXISTS idx_group_member_account ON lab_group_member(account_id);

CREATE TABLE IF NOT EXISTS lab_group_message (
  message_id  BIGSERIAL PRIMARY KEY,
  group_id    BIGINT NOT NULL REFERENCES lab_group(group_id) ON DELETE CASCADE,
  from_id     BIGINT REFERENCES lab_account(account_id) ON DELETE CASCADE,
  body        TEXT NOT NULL,
  system      BOOLEAN NOT NULL DEFAULT false,
  created_at  TIMESTAMPTZ NOT NULL DEFAULT now()
);
CREATE INDEX IF NOT EXISTS idx_group_message_group ON lab_group_message(group_id, message_id);

-- --------------------------------------------------- tempo di gioco --
CREATE TABLE IF NOT EXISTS lab_playtime (
  account_id  BIGINT NOT NULL REFERENCES lab_account(account_id) ON DELETE CASCADE,
  game_id     TEXT NOT NULL,
  game_name   TEXT,
  seconds     BIGINT NOT NULL DEFAULT 0,
  sessions    INT NOT NULL DEFAULT 0,
  last_played TIMESTAMPTZ,
  PRIMARY KEY (account_id, game_id)
);
CREATE INDEX IF NOT EXISTS idx_playtime_game ON lab_playtime(game_id);

CREATE TABLE IF NOT EXISTS lab_playtime_week (
  account_id  BIGINT NOT NULL REFERENCES lab_account(account_id) ON DELETE CASCADE,
  game_id     TEXT NOT NULL,
  week_start  DATE NOT NULL,      -- lunedì (date_trunc('week'))
  seconds     BIGINT NOT NULL DEFAULT 0,
  PRIMARY KEY (account_id, game_id, week_start)
);
CREATE INDEX IF NOT EXISTS idx_playtime_week ON lab_playtime_week(week_start, game_id);
