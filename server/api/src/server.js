'use strict';
// Server HTTP dell'api: applica le migrazioni, poi serve le rotte di
// config/endpoints.json. Il registro delle richieste lo tiene il proxy;
// qui si loggano solo gli errori.
const http = require('node:http');
const config = require('./config');
const db = require('./db');
const session = require('./session');
const router = require('./router');
const { migrate } = require('./migrate');
const feeds = require('./feeds');
const playtime = require('./playtime');
const { HttpError, send } = require('./http');
const adminApi = require('./endpoints/admin');

const routes = router.load();

function logError(req, err, requestId) {
  console.error(JSON.stringify({
    ts: new Date().toISOString(), event: 'handler_error', request_id: requestId,
    method: req.method, path: req.url.split('?')[0], error: err.message, stack: err.stack,
  }));
}

const server = http.createServer(async (req, res) => {
  const requestId = req.headers['x-request-id'] || null;
  const url = new URL(req.url, 'http://localhost');
  // Dietro al proxy l'IP del client è il primo valore di x-forwarded-for.
  const clientIp = String(req.headers['x-forwarded-for'] || req.socket.remoteAddress || '').split(',')[0].trim();

  try {
    if (url.pathname === '/healthz' && req.method === 'GET') {
      await db.query('SELECT 1');
      return send(res, 200, { status: 'ok', component: 'api', endpoints: routes.length });
    }

    const found = router.match(routes, req.method, url.pathname);
    if (!found) return send(res, 404, { error: 'not_found' });
    if (found.methodNotAllowed) {
      return send(res, 405, { error: 'method_not_allowed' }, { allow: found.methodNotAllowed.join(', ') });
    }

    const { route, params } = found;
    let auth = null;
    let admin = null;
    if (route.auth === 'admin') {
      // pannello web: cookie di sessione admin, non il token della console
      admin = await adminApi.resolve(req);
      if (!admin) return send(res, 401, { error: 'unauthorized', detail: 'accesso amministratore richiesto' }, { 'cache-control': 'no-store' });
    } else if (route.auth) {
      auth = await session.resolve(req);
      if (!auth) {
        return send(res, 401, { error: 'unauthorized', detail: 'serve un token di sessione: POST /api/v1/auth/login' },
          { 'www-authenticate': 'Bearer realm="omega"' });
      }
    }

    const out = await route.fn({ req, res, url, params, auth, admin, routes, clientIp });
    if (out && out.sent) return undefined;
    if (out.status === 204) {
      res.writeHead(204);
      return res.end();
    }
    return send(res, out.status, out.body);
  } catch (err) {
    if (err instanceof HttpError) {
      return send(res, err.status, { error: err.code, detail: err.message !== err.code ? err.message : undefined },
        err.headers || {});
    }
    logError(req, err, requestId);
    if (!res.headersSent) return send(res, 500, { error: 'internal_error', request_id: requestId });
    return res.end();
  }
});

// Prima le migrazioni, poi si serve: su uno schema vecchio metà delle richieste
// darebbe 500. Se falliscono il processo esce e Docker lo riavvia.
migrate()
  .then((applied) => {
    feeds.start();
    playtime.start();
    server.listen(config.port, () => {
      console.log(JSON.stringify({
        ts: new Date().toISOString(), event: 'startup', component: 'api', port: config.port,
        endpoints: routes.length, migrations_applied: applied,
      }));
    });
  })
  .catch((err) => {
    console.error(JSON.stringify({ ts: new Date().toISOString(), event: 'migration_failed', error: err.message }));
    process.exit(1);
  });

for (const sig of ['SIGTERM', 'SIGINT']) {
  process.on(sig, () => server.close(() => db.pool.end().finally(() => process.exit(0))));
}
