'use strict';
// Carica nello Store il catalogo curato (config/store-catalog.json): homebrew
// open source con icone e immagini in store-assets/. Idempotente sulla
// catalog_key: si aggiornano solo i metadati, voti, commenti e installazioni
// restano. L'autore delle voci è l'account curatore, senza password.
// Le traduzioni di tagline e description (i18n: { "<codice>": {...} }) vanno
// in lab_store_app.i18n; l'api le serve nella lingua della richiesta.
//
//   node src/seed-store.js [--dry-run] [--prune]
//   --prune toglie le voci non più presenti nel catalogo.
const path = require('node:path');
const db = require('./db');

const CATALOG = path.join(__dirname, '..', 'config', 'store-catalog.json');

(async () => {
  const dry = process.argv.includes('--dry-run');
  const prune = process.argv.includes('--prune');
  const cat = require(CATALOG);
  const apps = cat.apps || [];

  const cur = await db.query(
    `INSERT INTO lab_account (online_id) VALUES ($1)
     ON CONFLICT (online_id) DO UPDATE SET online_id=EXCLUDED.online_id
     RETURNING account_id`, [cat.curator || 'OmegaStore']);
  const author = cur.rows[0].account_id;

  let added = 0, updated = 0;
  // l'ordine del file decide "Recenti" e il banner In evidenza: il primo è il più recente
  for (const [i, a] of apps.entries()) {
    if (dry) { console.log(`${a.file_kind.padEnd(3)} ${a.platform}  ${a.title}`); continue; }
    const r = await db.query(
      `INSERT INTO lab_store_app
         (author_id, catalog_key, title, tagline, description, category, version, platform, icon_url, cover_url,
          screenshots, hashtags, download_url, file_kind, size_bytes, homepage_url, license, i18n, created_at, title_id)
       VALUES ($1,$2,$3,$4,$5,$6,$7,$8,$9,$10,$11::jsonb,$12,$13,$14,$15,$16,$17,$19::jsonb, now() - make_interval(secs => $18), $20)
       ON CONFLICT (catalog_key) WHERE catalog_key IS NOT NULL DO UPDATE SET
         title=EXCLUDED.title, tagline=EXCLUDED.tagline, description=EXCLUDED.description, category=EXCLUDED.category,
         version=EXCLUDED.version, platform=EXCLUDED.platform, icon_url=EXCLUDED.icon_url, cover_url=EXCLUDED.cover_url,
         screenshots=EXCLUDED.screenshots, hashtags=EXCLUDED.hashtags, download_url=EXCLUDED.download_url,
         file_kind=EXCLUDED.file_kind, size_bytes=EXCLUDED.size_bytes, homepage_url=EXCLUDED.homepage_url,
         license=EXCLUDED.license, i18n=EXCLUDED.i18n, title_id=EXCLUDED.title_id, updated_at=now()   -- published resta com'è: un'oscuratura dell'admin vale
       RETURNING (xmax = 0) AS inserted`,
      [author, a.catalog_key, a.title, a.tagline, a.description, a.category, a.version, a.platform, a.icon, a.cover,
        JSON.stringify(a.screenshots || []), a.hashtags || [], a.download_url, a.file_kind, a.size_bytes || null,
        a.homepage_url || null, a.license || null, i, a.i18n ? JSON.stringify(a.i18n) : null,
        /^[A-Z]{4}[0-9]{5}$/.test(a.title_id || '') ? a.title_id : null]);   // app intere (zip con Title ID)
    if (r.rows[0].inserted) added++; else updated++;
  }
  let removed = 0;
  if (prune && !dry) {
    const r = await db.query(
      `DELETE FROM lab_store_app WHERE catalog_key IS NOT NULL AND NOT (catalog_key = ANY($1::text[]))`,
      [apps.map((a) => a.catalog_key)]);
    removed = r.rowCount;
  }
  console.log(dry ? `${apps.length} voci nel catalogo (nessuna modifica)`
    : `Catalogo Store: ${added} aggiunte, ${updated} aggiornate${prune ? `, ${removed} rimosse` : ''} (totale ${apps.length})`);
  await db.pool.end();
})().catch(async (err) => {
  console.error(`Caricamento del catalogo fallito: ${err.message}`);
  await db.pool.end().catch(() => {});
  process.exit(1);
});
