'use strict';
// Router guidato da config/endpoints.json: ogni rotta indica metodo, percorso,
// autenticazione e il gestore come "<modulo>.<funzione>" di src/endpoints/.
const fs = require('node:fs');
const path = require('node:path');
const { CODES } = require('./lang');

const handlers = {
  info: require('./endpoints/info'),
  auth: require('./endpoints/auth'),
  account: require('./endpoints/account'),
  social: require('./endpoints/social'),
  hub: require('./endpoints/hub'),
  people: require('./endpoints/people'),
  wall: require('./endpoints/wall'),
  groups: require('./endpoints/groups'),
  stats: require('./endpoints/stats'),
  records: require('./endpoints/records'),
  games: require('./endpoints/games'),
  trophies: require('./endpoints/trophies'),
  voice: require('./endpoints/voice'),
  media: require('./endpoints/media'),
  browse: require('./endpoints/browse'),
  radio: require('./endpoints/radio'),
  store: require('./endpoints/store'),
  legal: require('./endpoints/legal'),
  updates: require('./endpoints/updates'),
  site: require('./endpoints/site'),
  admin: require('./endpoints/admin'),
  diag: require('./endpoints/diag'),
  feedback: require('./endpoints/feedback'),
  console: require('./endpoints/console'),
  notifyprefs: require('./endpoints/notifyprefs'),
  saves: require('./endpoints/saves'),
  wrap: require('./endpoints/wrap'),
  webapp: require('./endpoints/webapp'),
};

// Le versioni dell'app uscite prima di /api/v1 chiamano /lab/v1: stesso gestore.
const API_PREFIX = '/api/v1/';
const LEGACY_PREFIX = '/lab/v1/';

function canonicalPath(pathname) {
  return pathname.startsWith(LEGACY_PREFIX) ? API_PREFIX + pathname.slice(LEGACY_PREFIX.length) : pathname;
}

// "/api/v1/users/:onlineId" → regex con gruppi nominati. Il parametro ":lang"
// accetta solo i codici delle lingue supportate (lang.CODES), così "/:lang/"
// non cattura altri percorsi di un solo segmento.
const LANG_SOURCE = CODES.map((c) => c.replace(/[.*+?^${}()|[\]\\]/g, '\\$&')).join('|');
function compile(pattern) {
  const source = pattern
    .split('/')
    .map((seg) => (seg === ':lang' ? `(?<lang>${LANG_SOURCE})`
      : seg.startsWith(':') ? `(?<${seg.slice(1)}>[^/]+)` : seg.replace(/[.*+?^${}()|[\]\\]/g, '\\$&')))
    .join('/');
  return new RegExp(`^${source}/?$`);
}

function load(file = path.join(__dirname, '..', 'config', 'endpoints.json')) {
  const list = JSON.parse(fs.readFileSync(file, 'utf8')).endpoints;
  return list.map((e) => {
    const [mod, fn] = e.handler.split('.');
    const handler = handlers[mod] && handlers[mod][fn];
    if (typeof handler !== 'function') {
      throw new Error(`endpoints.json: gestore inesistente "${e.handler}" per ${e.method} ${e.path}`);
    }
    return { ...e, method: e.method.toUpperCase(), regex: compile(e.path), fn: handler };
  });
}

// { route, params }, oppure { methodNotAllowed: [metodi] }, oppure null.
function match(routes, method, pathname) {
  const target = canonicalPath(pathname);
  const allowed = [];
  for (const route of routes) {
    const m = route.regex.exec(target);
    if (!m) continue;
    if (route.method === method) {
      const params = {};
      for (const [k, v] of Object.entries(m.groups || {})) params[k] = decodeURIComponent(v);
      return { route, params };
    }
    allowed.push(route.method);
  }
  return allowed.length ? { methodNotAllowed: allowed } : null;
}

module.exports = { load, match };
