'use strict';
// Configurazione dell'api, letta una volta dall'ambiente.
// I vecchi nomi LAB_* restano accettati: i .env già in produzione li usano.
const path = require('node:path');

const env = (name, legacy) => process.env[name] || (legacy && process.env[legacy]) || '';
const num = (value, fallback) => Number(value || fallback);
const trimmed = (name) => (process.env[name] || '').trim();

module.exports = {
  port: num(process.env.API_PORT, 8080),
  databaseUrl: process.env.DATABASE_URL,
  publicUrl: process.env.PUBLIC_BASE_URL || 'https://play.omegasuite.it',

  sessionSecret: env('OMEGA_SESSION_SECRET', 'LAB_SESSION_SECRET'),
  sessionTtlSeconds: num(env('OMEGA_SESSION_TTL_SECONDS', 'LAB_SESSION_TTL_SECONDS'), 86400),
  // vuota = registrazione aperta
  registrationKey: env('OMEGA_REGISTRATION_KEY', 'LAB_REGISTRATION_KEY'),
  // solo per le prove in locale su http: in produzione il cookie admin è Secure
  adminInsecureCookie: process.env.ADMIN_INSECURE_COOKIE === '1',

  mediaDir: process.env.MEDIA_DIR || '/app/media',
  updateDir: process.env.UPDATE_DIR || '/app/updates',
  siteDir: process.env.SITE_DIR || path.join(__dirname, '..', '..', 'site'),
  sourceDir: process.env.PUBLIC_SOURCE_DIR || '/app/src-pub',
  // salvataggi online: blocchi cifrati dalla console (volume saves-data)
  savesDir: process.env.SAVES_DIR || '/app/saves',
  savesQuota: num(process.env.SAVES_ACCOUNT_QUOTA, 2 * 1024 ** 3),     // per account
  savesMax: num(process.env.SAVES_MAX, 1024 ** 3),                     // un salvataggio
  savesTotalMax: num(process.env.SAVES_TOTAL_MAX, 60 * 1024 ** 3),     // tutto il server
  savesMinFree: num(process.env.SAVES_MIN_FREE, 15 * 1024 ** 3),       // spazio da lasciare libero sul disco
  storeAssetDir: process.env.STORE_ASSET_DIR || path.join(__dirname, '..', 'store-assets'),

  logRetentionDays: num(process.env.LOG_RETENTION_DAYS, 30),
  autoHideReports: num(process.env.STORE_AUTOHIDE_REPORTS, 3),

  legal: {
    controllerName: trimmed('LEGAL_CONTROLLER_NAME'),
    controllerVat: trimmed('LEGAL_CONTROLLER_VAT'),
    controllerAddress: trimmed('LEGAL_CONTROLLER_ADDRESS'),
    contactEmail: trimmed('LEGAL_CONTACT_EMAIL'),
    hostingProvider: trimmed('LEGAL_HOSTING_PROVIDER'),
  },
};
