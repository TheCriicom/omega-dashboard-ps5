'use strict';
// Pagina d'ingresso del dominio e elenco delle rotte servite.

// GET /
async function index() {
  return {
    status: 200,
    body: {
      service: 'Omega',
      description: 'Account, funzioni social e Store di homebrew per la console. Progetto indipendente, non affiliato a Sony.',
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
