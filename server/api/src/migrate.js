'use strict';
// Migrazioni all'avvio: applica in ordine i file di migrations/ non ancora
// registrati in schema_migration, ognuno nella propria transazione.
// db/init/ è lo schema di partenza e gira solo su un volume vuoto.
// I file già applicati in produzione non si modificano mai: si aggiunge un
// nuovo file numerato.
const fs = require('node:fs');
const path = require('node:path');
const { pool } = require('./db');

const DIR = path.join(__dirname, '..', 'migrations');
// Lock consultivo: due processi avviati insieme non applicano le stesse migrazioni.
const LOCK_ID = 7_202_610;

async function migrate() {
  const client = await pool.connect();
  try {
    await client.query('SELECT pg_advisory_lock($1)', [LOCK_ID]);
    await client.query(`CREATE TABLE IF NOT EXISTS schema_migration (
      name TEXT PRIMARY KEY, applied_at TIMESTAMPTZ NOT NULL DEFAULT now())`);
    const done = new Set((await client.query('SELECT name FROM schema_migration')).rows.map((r) => r.name));
    const files = fs.readdirSync(DIR).filter((f) => /^\d+_.+\.sql$/.test(f)).sort();
    const applied = [];
    for (const file of files) {
      if (done.has(file)) continue;
      const sql = fs.readFileSync(path.join(DIR, file), 'utf8');
      try {
        await client.query('BEGIN');
        await client.query(sql);
        await client.query('INSERT INTO schema_migration (name) VALUES ($1)', [file]);
        await client.query('COMMIT');
        applied.push(file);
      } catch (err) {
        await client.query('ROLLBACK');
        throw new Error(`migrazione ${file} fallita: ${err.message}`);
      }
    }
    return applied;
  } finally {
    await client.query('SELECT pg_advisory_unlock($1)', [LOCK_ID]).catch(() => {});
    client.release();
  }
}

module.exports = { migrate };
