'use strict';
// Pagina d'ingresso del dominio e elenco delle rotte servite.
const lang = require('../lang');
const messages = require('../messages');
const { DEVELOPER } = require('../legal');

// GET /  e  GET /api/v1  (descrizione nella lingua della richiesta)
async function index({ lang: code = lang.SOURCE } = {}) {
  return {
    status: 200,
    body: {
      service: 'Omega',
      description: messages.t(code, 'api.description'),
      lang: code,
      languages: lang.CODES,
      developer: { name: DEVELOPER.name, url: DEVELOPER.url },
      legal: { privacy: '/legal/privacy', terms: '/legal/terms', licenses: '/legal/licenses', source: '/source' },
      auth: { register: 'POST /api/v1/auth/register', login: 'POST /api/v1/auth/login' },
      endpoints: 'GET /api/v1/endpoints',
    },
  };
}

// GET /api/v1/endpoints
async function endpoints({ routes }) {
  return {
    status: 200,
    body: {
      endpoints: routes.map((r) => ({ method: r.method, path: r.path, family: r.family, auth: !!r.auth, note: r.note })),
    },
  };
}

module.exports = { index, endpoints };
