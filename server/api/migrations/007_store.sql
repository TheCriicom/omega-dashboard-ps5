-- Store: homebrew pubblicati dagli utenti (con voti, valutazioni e commenti) e
-- libreria personale, riempita dal JSON indicato da ciascun account.

-- --------------------------------------------------------- homebrew pubblicati --
CREATE TABLE IF NOT EXISTS lab_store_app (
  app_id       BIGSERIAL PRIMARY KEY,
  author_id    BIGINT NOT NULL REFERENCES lab_account(account_id) ON DELETE CASCADE,
  title        TEXT NOT NULL,
  tagline      TEXT,
  description  TEXT,
  category     TEXT NOT NULL DEFAULT 'app',   -- app | gioco | utility | emulatore | tema | altro
  version      TEXT,
  title_id     TEXT,                          -- TID opzionale (zip di app registrabili)
  icon_url     TEXT,
  cover_url    TEXT,
  screenshots  JSONB NOT NULL DEFAULT '[]',   -- array di URL
  hashtags     TEXT[] NOT NULL DEFAULT '{}',
  download_url TEXT NOT NULL,
  file_kind    TEXT NOT NULL DEFAULT 'auto',  -- auto | pkg | zip
  size_bytes   BIGINT,
  downloads    BIGINT NOT NULL DEFAULT 0,
  published    BOOLEAN NOT NULL DEFAULT true,
  created_at   TIMESTAMPTZ NOT NULL DEFAULT now(),
  updated_at   TIMESTAMPTZ NOT NULL DEFAULT now()
);
CREATE INDEX IF NOT EXISTS idx_store_app_author  ON lab_store_app(author_id);
CREATE INDEX IF NOT EXISTS idx_store_app_created ON lab_store_app(created_at DESC);

-- mi piace (+1) / non mi piace (-1), uno per account
CREATE TABLE IF NOT EXISTS lab_store_vote (
  app_id     BIGINT NOT NULL REFERENCES lab_store_app(app_id) ON DELETE CASCADE,
  account_id BIGINT NOT NULL REFERENCES lab_account(account_id) ON DELETE CASCADE,
  value      SMALLINT NOT NULL CHECK (value IN (-1, 1)),
  created_at TIMESTAMPTZ NOT NULL DEFAULT now(),
  PRIMARY KEY (app_id, account_id)
);

-- valutazione a stelle 1..5, una per account
CREATE TABLE IF NOT EXISTS lab_store_rating (
  app_id     BIGINT NOT NULL REFERENCES lab_store_app(app_id) ON DELETE CASCADE,
  account_id BIGINT NOT NULL REFERENCES lab_account(account_id) ON DELETE CASCADE,
  stars      SMALLINT NOT NULL CHECK (stars BETWEEN 1 AND 5),
  created_at TIMESTAMPTZ NOT NULL DEFAULT now(),
  PRIMARY KEY (app_id, account_id)
);

CREATE TABLE IF NOT EXISTS lab_store_comment (
  comment_id BIGSERIAL PRIMARY KEY,
  app_id     BIGINT NOT NULL REFERENCES lab_store_app(app_id) ON DELETE CASCADE,
  account_id BIGINT NOT NULL REFERENCES lab_account(account_id) ON DELETE CASCADE,
  body       TEXT NOT NULL,
  created_at TIMESTAMPTZ NOT NULL DEFAULT now()
);
CREATE INDEX IF NOT EXISTS idx_store_comment_app ON lab_store_comment(app_id, comment_id DESC);

-- ----------------------------------------------------------- libreria personale --
-- Ogni account punta a un proprio JSON esterno; da lì si riempie la libreria.
CREATE TABLE IF NOT EXISTS lab_library_source (
  account_id   BIGINT PRIMARY KEY REFERENCES lab_account(account_id) ON DELETE CASCADE,
  url          TEXT NOT NULL,
  name         TEXT,
  last_sync    TIMESTAMPTZ,
  last_status  TEXT,
  item_count   INT NOT NULL DEFAULT 0,
  updated_at   TIMESTAMPTZ NOT NULL DEFAULT now()
);

CREATE TABLE IF NOT EXISTS lab_library_item (
  item_id      BIGSERIAL PRIMARY KEY,
  account_id   BIGINT NOT NULL REFERENCES lab_account(account_id) ON DELETE CASCADE,
  ext_id       TEXT NOT NULL,            -- id stabile dal JSON (sync idempotente)
  title        TEXT NOT NULL,
  description  TEXT,
  platform     TEXT,                     -- PS4 | PS5
  version      TEXT,
  title_id     TEXT,
  cover_url    TEXT,
  screenshots  JSONB NOT NULL DEFAULT '[]',
  download_url TEXT NOT NULL,
  file_kind    TEXT NOT NULL DEFAULT 'auto',
  size_bytes   BIGINT,
  created_at   TIMESTAMPTZ NOT NULL DEFAULT now(),
  UNIQUE (account_id, ext_id)
);
CREATE INDEX IF NOT EXISTS idx_library_item_acct ON lab_library_item(account_id, title);
