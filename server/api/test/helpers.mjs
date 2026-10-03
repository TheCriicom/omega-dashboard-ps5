// Utilità comuni ai test end-to-end: girano contro un'api già avviata.
import path from 'node:path';
import { fileURLToPath } from 'node:url';

export const BASE = process.env.OMEGA_TEST_URL || 'http://127.0.0.1:18080';
export const API_DIR = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
export const PROXY_DIR = path.resolve(API_DIR, '..', 'proxy');
export const DATABASE_URL = process.env.DATABASE_URL || 'postgres://omega@127.0.0.1:5432/omega';
process.env.DATABASE_URL = DATABASE_URL;

// Un IP diverso a ogni esecuzione: i limiti per IP (registrazione, login)
// non si accumulano tra un giro e l'altro sullo stesso server.
const rnd = () => 1 + Math.floor(Math.random() * 250);
export const CLIENT_IP = `10.${rnd()}.${rnd()}.${rnd()}`;
export const IP = { 'x-forwarded-for': CLIENT_IP };

export const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

// Suffisso unico per gli online_id creati dal test.
export const suffix = () => Date.now().toString(36).slice(-5);

export function checker() {
  let passes = 0;
  let fails = 0;
  const ok = (cond, msg, detail) => {
    if (cond) passes++; else fails++;
    console.log(`${cond ? 'OK  ' : 'FAIL'} ${msg}${cond ? '' : ` → ${JSON.stringify(detail)?.slice(0, 600)}`}`);
  };
  const done = () => {
    console.log(`\n${passes} OK, ${fails} FAIL`);
    process.exitCode = fails ? 1 : 0;
  };
  return { ok, done };
}

// Richiesta JSON: { s: status, b: corpo (JSON o testo), h: intestazioni }.
export async function j(method, p, body, headers = {}, base = BASE) {
  const r = await fetch(base + p, {
    method,
    headers: { 'content-type': 'application/json', ...IP, ...headers },
    body: body !== undefined && body !== null ? JSON.stringify(body) : undefined,
  });
  const t = await r.text();
  let b;
  try { b = JSON.parse(t); } catch { b = t; }
  return { s: r.status, b, h: r.headers };
}
