'use strict';
// Fetch verso URL forniti dagli utenti: solo http/https su porte standard,
// niente indirizzi interni o privati (ricontrollati a ogni redirect), tempi e
// dimensioni limitati. Lo usano il browser, le immagini dello Store e la
// libreria personale: la console non contatta mai siti terzi direttamente.
const dns = require('node:dns').promises;
const net = require('node:net');
const { HttpError } = require('./http');

const UA = 'Mozilla/5.0 (Linux; Android 14; Omega) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/124.0 Mobile Safari/537.36';

function privateIp(ip) {
  if (net.isIPv4(ip)) {
    const [a, b] = ip.split('.').map(Number);
    return a === 10 || a === 127 || a === 0 || (a === 169 && b === 254) || (a === 172 && b >= 16 && b <= 31)
      || (a === 192 && b === 168) || (a === 100 && b >= 64 && b <= 127) || a >= 224;
  }
  const v = ip.toLowerCase();
  if (v.startsWith('::ffff:')) return privateIp(v.slice(7));
  return v === '::1' || v === '::' || v.startsWith('fc') || v.startsWith('fd') || v.startsWith('fe80') || v.startsWith('ff');
}

async function checkUrl(raw) {
  let u;
  try { u = new URL(raw); } catch { throw new HttpError(400, 'invalid_url'); }
  if (!['http:', 'https:'].includes(u.protocol)) throw new HttpError(400, 'unsupported_scheme');
  if (u.port && !['80', '443', '8080', '8443'].includes(u.port)) throw new HttpError(400, 'port_not_allowed');
  const host = u.hostname.replace(/^\[|\]$/g, '');
  const addrs = net.isIP(host) ? [{ address: host }] : await dns.lookup(host, { all: true }).catch(() => []);
  if (!addrs.length) throw new HttpError(502, 'dns_failed');
  if (addrs.some((a) => privateIp(a.address))) throw new HttpError(403, 'address_not_allowed');
  return u;
}

// Redirect seguiti a mano, ricontrollando ogni salto. HEAD serve a conoscere
// tipo e dimensione senza scaricare il corpo.
async function safeFetch(raw, accept, method = 'GET', timeout = 15000) {
  let url = raw;
  for (let hop = 0; hop < 6; hop++) {
    const u = await checkUrl(url);
    const res = await fetch(u, {
      method,
      redirect: 'manual',
      headers: { 'user-agent': UA, accept, 'accept-language': 'it-IT,it;q=0.9,en;q=0.7' },
      signal: AbortSignal.timeout(timeout),
    });
    if (res.status >= 300 && res.status < 400 && res.headers.get('location')) {
      url = new URL(res.headers.get('location'), u).toString();
      try { res.body && res.body.cancel && res.body.cancel(); } catch { /* */ }
      continue;
    }
    return { res, url: u.toString() };
  }
  throw new HttpError(508, 'too_many_redirects');
}

async function readLimited(res, max) {
  if (!res.body) return Buffer.alloc(0);
  const reader = res.body.getReader();
  const chunks = []; let size = 0;
  for (;;) {
    const { done, value } = await reader.read();
    if (done) break;
    size += value.length;
    if (size > max) { reader.cancel().catch(() => {}); break; }
    chunks.push(Buffer.from(value));
  }
  return Buffer.concat(chunks);
}

module.exports = { safeFetch, readLimited };
