-- Localizzazione: lingua preferita dell'account (per le notifiche, scelta
-- dall'Accept-Language delle sue richieste; NULL = italiano, come prima) e
-- traduzioni delle schede del catalogo curato dello Store
-- ({ "<codice>": { "tagline": "...", "description": "..." } }, caricate da
-- seed-store.js; NULL per gli homebrew pubblicati dagli utenti).
ALTER TABLE lab_account   ADD COLUMN IF NOT EXISTS lang TEXT;
ALTER TABLE lab_store_app ADD COLUMN IF NOT EXISTS i18n JSONB;
