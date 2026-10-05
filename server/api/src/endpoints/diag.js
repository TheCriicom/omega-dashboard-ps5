'use strict';
// Diagnostica dalle console: senza la PS5 in mano, è l'unico modo di sapere
// perché la musica tace o il microfono non si apre.
//  · POST /api/v1/diag/event  {component, event, ok, rc?, detail?}  — solo codici
//    tecnici, mandati da soli dal servizio (niente URL, niente nomi di file);
//  · POST /api/v1/diag/report {app_version, logs:{nome:testo}}  — i registri
//    completi, solo quando l'utente preme «Invia diagnostica».
// Tutto finisce in MEDIA_DIR/diag (un file JSONL al giorno per gli eventi, un
// file per resoconto) e si cancella dopo LOG_RETENTION_DAYS, come i registri del proxy.
const fs = require('node:fs');
const path = require('node:path');
const config = require('../config');
const { HttpError, retryLater, readJson } = require('../http');
const limiter = require('../ratelimit');

const DIR = path.join(config.mediaDir, 'diag');
const KEEP_DAYS = Number(process.env.LOG_RETENTION_DAYS || 30);
const word = (v, n = 40) => String(v == null ? '' : v).replace(/[^\w.:-]/g, '').slice(0, n);

function ensureDir() { fs.mkdirSync(DIR, { recursive: true }); }

async function event({ req, auth }) {
  const wait = limiter.hit(`diag_ev|${auth.accountId}`, 60, 3600);
  if (wait) throw retryLater('too_many_events', wait);
  const b = await readJson(req);
  const row = {
    ts: new Date().toISOString(), account: String(auth.accountId), online_id: auth.onlineId,
    component: word(b.component, 20), event: word(b.event), ok: !!b.ok,
    rc: word(b.rc, 20), detail: word(b.detail, 120), version: word(b.version, 24),
  };
  if (!row.component || !row.event) throw new HttpError(400, 'invalid_event');
  ensureDir();
  fs.appendFileSync(path.join(DIR, `events-${row.ts.slice(0, 10)}.jsonl`), JSON.stringify(row) + '\n');
  return { status: 204 };
}

async function report({ req, auth }) {
  const wait = limiter.hit(`diag_rep|${auth.accountId}`, 5, 3600);
  if (wait) throw retryLater('too_many_reports', wait);
  const b = await readJson(req);
  const logs = b && typeof b.logs === 'object' && b.logs ? b.logs : {};
  const out = { ts: new Date().toISOString(), account: String(auth.accountId), online_id: auth.onlineId, app_version: word(b.app_version, 24), logs: {} };
  let total = 0;
  for (const [k, v] of Object.entries(logs).slice(0, 12)) {
    const text = String(v || '').slice(-200 * 1024);
    total += text.length;
    if (total > 900 * 1024) break;
    out.logs[word(k, 40)] = text;
  }
  ensureDir();
  const file = `report-${out.ts.replace(/[:.]/g, '-')}-${out.account}.json`;
  fs.writeFileSync(path.join(DIR, file), JSON.stringify(out));
  console.log(JSON.stringify({ ts: out.ts, event: 'diag_report', account: out.account, file }));
  return { status: 201, body: { received: true } };
}

// pulizia una volta al giorno
setInterval(() => {
  try {
    const limit = Date.now() - KEEP_DAYS * 86400e3;
    for (const f of fs.readdirSync(DIR)) {
      const p = path.join(DIR, f);
      if (fs.statSync(p).mtimeMs < limit) fs.rmSync(p);
    }
  } catch { /* cartella non ancora creata */ }
}, 86400e3).unref();

module.exports = { event, report };
