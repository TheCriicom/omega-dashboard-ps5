-- Store: piattaforma, pagina del progetto, licenza e chiave del catalogo curato;
-- file_kind può essere anche 'elf'.
ALTER TABLE lab_store_app ADD COLUMN IF NOT EXISTS platform     TEXT;
ALTER TABLE lab_store_app ADD COLUMN IF NOT EXISTS homepage_url TEXT;
ALTER TABLE lab_store_app ADD COLUMN IF NOT EXISTS license      TEXT;
ALTER TABLE lab_store_app ADD COLUMN IF NOT EXISTS catalog_key  TEXT;
CREATE UNIQUE INDEX IF NOT EXISTS idx_store_app_catalog ON lab_store_app(catalog_key) WHERE catalog_key IS NOT NULL;
