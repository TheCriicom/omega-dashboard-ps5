'use strict';
// Proxy di Omega: l'unico componente raggiunto dal dominio. Inoltra ogni
// richiesta all'api così com'è e ne scrive una riga JSONL (metodo, percorso,
// query, esito, tempi) su stdout e nel file del giorno. Credenziali e token
// non arrivano mai su disco: intestazioni, parametri e segmenti di percorso
// sensibili si oscurano, i corpi non si registrano.
// Solo moduli core di Node.
const http = require('node:http');
const fs = require('node:fs');
const path = require('node:path');
const crypto = require('node:crypto');

const list = (value) => value.split(',').map((s) => s.trim()).filter(Boolean);

const PORT = Number(process.env.PROXY_PORT || 9986);
const UPSTREAM = new URL(process.env.UPSTREAM_URL || 'http://api:8080');
// Connessioni verso l'api riusate, ma scartate dopo 30 s ferme: l'api le tiene
// 65 s, quindi non capita di mandare una richiesta su un socket che l'api sta
// chiudendo in quell'istante (era la causa dei 502 "socket hang up").
const upstreamAgent = new http.Agent({ keepAlive: true, timeout: 30_000 });
const LOG_DIR = process.env.LOG_DIR || '/var/log/omega';
const RETENTION_DAYS = Math.max(1, Number(process.env.LOG_RETENTION_DAYS || 30));
const REDACT_HEADERS = new Set(list(process.env.REDACT_HEADERS || 'authorization,cookie,set-cookie').map((s) => s.toLowerCase()));
const REDACT_QUERY = new Set(list(process.env.REDACT_QUERY || 'access_token,refresh_token,code,id_token,token,password').map((s) => s.toLowerCase()));
// Inoltrati ma non registrati: la voce del party fa decine di richieste al
// secondo per console e riempirebbe il registro senza dire nulla.
const LOG_SKIP_PREFIXES = list(process.env.LOG_SKIP_PREFIXES ?? '/api/v1/party/voice,/lab/v1/party/voice');

const skipLog = (pathname) => LOG_SKIP_PREFIXES.some((p) => pathname === p || pathname.startsWith(p.endsWith('/') ? p : `${p}/`));

// Un file al giorno (requests-AAAA-MM-GG.jsonl); quelli più vecchi di
// LOG_RETENTION_DAYS si cancellano, come dichiara l'informativa privacy.
fs.mkdirSync(LOG_DIR, { recursive: true });

const today = () => new Date().toISOString().slice(0, 10);
let logDay = null;
let dailyLog = null;

function pruneLogs() {
  const cutoff = new Date(Date.now() - RETENTION_DAYS * 86400e3).toISOString().slice(0, 10);
  for (const f of fs.readdirSync(LOG_DIR)) {
    const m = /^requests-(\d{4}-\d{2}-\d{2})\.jsonl$/.exec(f);
    if (m && m[1] < cutoff) fs.rmSync(path.join(LOG_DIR, f), { force: true });
  }
}
setInterval(pruneLogs, 6 * 3600e3).unref();

function writeLog(entry) {
  const d = today();
  if (d !== logDay) {
    if (dailyLog) dailyLog.end();
    logDay = d;
    dailyLog = fs.createWriteStream(path.join(LOG_DIR, `requests-${d}.jsonl`), { flags: 'a' });
    pruneLogs();
  }
  const line = `${JSON.stringify(entry)}\n`;
  dailyLog.write(line);
  process.stdout.write(line);
}

function redactQuery(search) {
  if (!search) return '';
  const params = new URLSearchParams(search);
  for (const key of [...params.keys()]) {
    if (REDACT_QUERY.has(key.toLowerCase())) params.set(key, 'REDACTED');
  }
  return params.toString();
}

// Un token può comparire anche nel percorso: si oscurano i segmenti lunghi e
// opachi o a tre parti come i JWT. Gli id corti restano, servono a leggere il log.
const JWT_LIKE = /^[A-Za-z0-9_-]{8,}\.[A-Za-z0-9_-]{8,}\.[A-Za-z0-9_-]{8,}$/;
function redactPath(pathname) {
  return pathname
    .split('/')
    .map((seg) => (seg.length > 40 || JWT_LIKE.test(seg) ? 'REDACTED' : seg))
    .join('/');
}

function redactHeaders(headers) {
  const out = {};
  for (const [k, v] of Object.entries(headers)) {
    out[k] = REDACT_HEADERS.has(k.toLowerCase()) ? 'REDACTED' : v;
  }
  return out;
}

const server = http.createServer((req, res) => {
  const startedAt = process.hrtime.bigint();
  const requestId = crypto.randomUUID();
  const reqUrl = new URL(req.url, 'http://localhost');
  // L'orchestratore TLS aggiunge l'IP vero in coda a x-forwarded-for: i valori
  // precedenti li sceglie il client e si possono falsificare.
  const xff = String(req.headers['x-forwarded-for'] || '').split(',').map((s) => s.trim()).filter(Boolean);
  const clientIp = (xff.length ? xff[xff.length - 1] : String(req.socket.remoteAddress || ''))
    .replace(/^::ffff:/, '');

  if (reqUrl.pathname === '/healthz' && req.method === 'GET') {
    res.writeHead(200, { 'content-type': 'application/json' });
    res.end(JSON.stringify({ status: 'ok', component: 'proxy' }));
    return;
  }

  const proxyHeaders = { ...req.headers, 'x-request-id': requestId, 'x-forwarded-for': clientIp };
  delete proxyHeaders.host;

  let finished = false;
  function finish(status, bytes, errorMsg) {
    if (finished) return;
    finished = true;
    if (skipLog(reqUrl.pathname)) return;
    const durationMs = Number(process.hrtime.bigint() - startedAt) / 1e6;
    writeLog({
      ts: new Date().toISOString(),
      request_id: requestId,
      client_ip: clientIp,
      method: req.method,
      path: redactPath(reqUrl.pathname),
      query: redactQuery(reqUrl.search.replace(/^\?/, '')),
      status,
      duration_ms: Math.round(durationMs * 1000) / 1000,
      resp_bytes: bytes,
      error: errorMsg,
      req_headers: redactHeaders(req.headers),
    });
  }

  const upstreamReq = http.request(
    {
      protocol: UPSTREAM.protocol,
      hostname: UPSTREAM.hostname,
      port: UPSTREAM.port || 80,
      method: req.method,
      path: reqUrl.pathname + reqUrl.search,
      headers: proxyHeaders,
      agent: upstreamAgent,
    },
    (upstreamRes) => {
      let bytes = 0;
      upstreamRes.on('data', (chunk) => { bytes += chunk.length; });
      res.writeHead(upstreamRes.statusCode || 502, upstreamRes.headers);
      upstreamRes.pipe(res);
      upstreamRes.on('end', () => finish(upstreamRes.statusCode || 502, bytes, null));
    },
  );

  upstreamReq.on('error', (err) => {
    if (!res.headersSent) {
      res.writeHead(502, { 'content-type': 'application/json' });
      res.end(JSON.stringify({ error: 'bad_gateway', detail: 'upstream non raggiungibile' }));
    }
    finish(502, 0, err.message);
  });

  // Il corpo passa in streaming, anche se binario.
  req.pipe(upstreamReq);

  // Se il client se ne va prima della risposta (es. long-poll della voce) si
  // chiude anche la richiesta all'api, che così smette di aspettare.
  res.on('close', () => {
    if (!res.writableFinished) upstreamReq.destroy();
  });
});

server.listen(PORT, () => {
  writeLog({
    ts: new Date().toISOString(), event: 'startup', component: 'proxy',
    port: PORT, upstream: UPSTREAM.href, redact_headers: [...REDACT_HEADERS], redact_query: [...REDACT_QUERY],
    log_skip_prefixes: LOG_SKIP_PREFIXES,
  });
});

for (const sig of ['SIGTERM', 'SIGINT']) {
  process.on(sig, () => server.close(() => process.exit(0)));
}
