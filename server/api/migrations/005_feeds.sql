-- Notizie dai feed RSS (src/feeds.js): ext_id evita i doppioni tra un aggiornamento e l'altro.
ALTER TABLE lab_news ADD COLUMN IF NOT EXISTS source    TEXT;
ALTER TABLE lab_news ADD COLUMN IF NOT EXISTS link      TEXT;
ALTER TABLE lab_news ADD COLUMN IF NOT EXISTS image_url TEXT;
ALTER TABLE lab_news ADD COLUMN IF NOT EXISTS ext_id    TEXT;
CREATE UNIQUE INDEX IF NOT EXISTS ux_news_ext ON lab_news(ext_id) WHERE ext_id IS NOT NULL;
CREATE INDEX IF NOT EXISTS idx_news_created ON lab_news(created_at DESC);
