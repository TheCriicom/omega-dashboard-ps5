-- Avatar e copertina personalizzati: *_media è la cartella in MEDIA_DIR,
-- *_frames il numero di fotogrammi JPEG (1 = foto, più di 1 = animazione).
ALTER TABLE lab_account ADD COLUMN IF NOT EXISTS avatar_media  TEXT;
ALTER TABLE lab_account ADD COLUMN IF NOT EXISTS avatar_frames INT NOT NULL DEFAULT 0;
ALTER TABLE lab_account ADD COLUMN IF NOT EXISTS cover_media   TEXT;
ALTER TABLE lab_account ADD COLUMN IF NOT EXISTS cover_frames  INT NOT NULL DEFAULT 0;
