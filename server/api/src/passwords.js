'use strict';
// Hash delle password: scrypt con sale casuale, confronto a tempo costante.
// Formato: scrypt$N$r$p$<sale b64url>$<hash b64url>. I parametri viaggiano con
// l'hash, così si possono alzare senza invalidare le password esistenti.
const crypto = require('node:crypto');

const N = 16384;
const R = 8;
const P = 1;
const KEYLEN = 32;

function scrypt(password, salt, n, r, p) {
  return new Promise((resolve, reject) => {
    crypto.scrypt(password, salt, KEYLEN, { N: n, r, p, maxmem: 64 * 1024 * 1024 }, (err, key) => {
      if (err) reject(err); else resolve(key);
    });
  });
}

async function hash(password) {
  const salt = crypto.randomBytes(16);
  const key = await scrypt(password, salt, N, R, P);
  return `scrypt$${N}$${R}$${P}$${salt.toString('base64url')}$${key.toString('base64url')}`;
}

async function verify(password, stored) {
  const parts = String(stored || '').split('$');
  if (parts.length !== 6 || parts[0] !== 'scrypt') return false;
  const [, n, r, p, salt, expected] = parts;
  const key = await scrypt(password, Buffer.from(salt, 'base64url'), Number(n), Number(r), Number(p));
  const want = Buffer.from(expected, 'base64url');
  return want.length === key.length && crypto.timingSafeEqual(want, key);
}

// Hash di un valore a caso, calcolato una volta: quando l'account non esiste
// si verifica comunque contro questo, così il tempo di risposta non rivela
// quali online_id sono registrati.
let decoy = null;
async function verifyDecoy(password) {
  decoy = decoy || (await hash(crypto.randomBytes(16).toString('hex')));
  await verify(password, decoy);
  return false;
}

module.exports = { hash, verify, verifyDecoy };
