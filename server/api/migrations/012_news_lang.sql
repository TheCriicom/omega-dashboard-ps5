-- Notizie nella lingua dell'utente: ogni feed ha la sua lingua (feeds.js) e
-- /news sceglie le righe con la lingua della richiesta, completando con
-- l'inglese. NULL = notizia scritta a mano, valida per tutte le lingue.
ALTER TABLE lab_news ADD COLUMN IF NOT EXISTS lang TEXT;
UPDATE lab_news SET lang = 'it' WHERE lang IS NULL AND source IN ('Multiplayer.it', 'Everyeye', 'IGN Italia');
UPDATE lab_news SET lang = 'en' WHERE lang IS NULL AND source IN ('PlayStation Blog', 'Push Square');
CREATE INDEX IF NOT EXISTS idx_news_lang ON lab_news (lang, created_at DESC);
-- Le quattro notizie di benvenuto di 004_hub.sql sono scritte in italiano.
UPDATE lab_news SET lang = 'it' WHERE lang IS NULL AND ext_id IS NULL
   AND title IN ('Benvenuto in Omega', 'Party e chat sono online', 'Scopri chi sta giocando', 'Notifiche in tempo reale');
