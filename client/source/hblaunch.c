// Omega UI — homebrew in formato websrv (/data/homebrew/<Nome>/homebrew.js).
//
// homebrew.js è lo script che il launcher web di websrv (ps5-payload-dev/websrv)
// carica in un iframe insieme a homebrewApi.js e apiClient.js:
//   main()    → { mainText, secondaryText, imgPath, onclick(), options[] }
//   onclick() → { path, args, env, cwd, daemon }  oppure showCarousel(voci)
// e carouselView.js passa il risultato a ApiClient.launchApp → GET /hbldr.
// Qui lo script gira in QuickJS dentro un ambiente che riproduce quello
// dell'iframe, con limiti di memoria e di tempo. La sessione resta viva tra un
// sottomenu e l'altro: showCarousel e pickFile diventano menu di Omega e la
// scelta prosegue lo stesso script. I parametri finali sono codificati come in
// ApiClient.launchApp e websrv avvia l'homebrew (chiudendo l'app in primo
// piano, a meno che non sia un demone). Le cartelle con solo eboot.elf si
// avviano direttamente, come fa websrv.
#include "app.h"
#include <dirent.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#if defined(PS5) || defined(OMEGA_HAVE_QUICKJS)
#define HAVE_QJS 1
#include <quickjs/quickjs.h>
#endif

// codifica application/x-www-form-urlencoded, come URLSearchParams.toString()
static void formenc(char *dst, size_t n, const char *src) {
  size_t o = 0;
  for (const unsigned char *p = (const unsigned char *)src; *p && o + 4 < n; p++) {
    if ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9') || strchr("*-._", *p)) dst[o++] = (char)*p;
    else if (*p == ' ') dst[o++] = '+';
    else o += (size_t)snprintf(dst + o, n - o, "%%%02X", *p);
  }
  dst[o] = 0;
}

// Sessione di avvio: lo script resta caricato tra un sottomenu e l'altro.
typedef struct {
  char dir[400];
  char *query;          // parametri di /hbldr pronti, NULL finché non si conoscono
  int daemon;
#ifdef HAVE_QJS
  JSRuntime *rt; JSContext *ctx;
  Uint32 deadline, hard; // scadenza del passo (le attese di rete la spostano) / oltre hard niente più rete
#endif
} HbSess;
static HbSess g_hb;     // usata da un solo thread alla volta

#ifdef HAVE_QJS
#define HB_STEP_MS 2000          // tempo di CPU per passo
#define HB_HARD_MS 25000         // oltre, rete compresa, niente più fetch nel passo
#define HB_MEM     (32u * 1024 * 1024)
#define HB_STACK   (512 * 1024)

static int qjs_interrupt(JSRuntime *rt, void *op) {
  (void)rt; HbSess *h = op;
  return SDL_GetTicks() > h->deadline;   // il tempo di rete è già aggiunto a deadline
}

// eccezione JS nel log con lo stack: sulla console è l'unico modo di vederla
static void js_log_exc(JSContext *ctx, JSValueConst e, const char *dir, char *out, size_t on) {
  const char *m = JS_ToCString(ctx, e);
  const char *st = NULL; JSValue sv = JS_UNDEFINED;
  if (JS_IsObject(e)) { sv = JS_GetPropertyStr(ctx, e, "stack"); if (JS_IsString(sv)) st = JS_ToCString(ctx, sv); }
  omega_log("homebrew.js %s: %s%s%s", dir, m ? m : "?", st ? "\n" : "", st ? st : "");
  if (out && on) snprintf(out, on, "%s", m ? m : "errore sconosciuto");
  if (st) JS_FreeCString(ctx, st);
  JS_FreeValue(ctx, sv);
  if (m) JS_FreeCString(ctx, m);
}
static void js_take_exc(JSContext *ctx, const char *dir, char *out, size_t on) {
  JSValue e = JS_GetException(ctx);
  js_log_exc(ctx, e, dir, out, on);
  JS_FreeValue(ctx, e);
}

// toglie le barre doppie, come fa websrv sulle richieste /fs
static void clean_path(char *dst, size_t n, const char *src) {
  size_t o = 0;
  for (const char *p = src; *p && o + 1 < n; p++) { if (*p == '/' && o && dst[o - 1] == '/') continue; dst[o++] = *p; }
  if (o > 1 && dst[o - 1] == '/') o--;
  dst[o] = 0;
  if (!o) snprintf(dst, n, "/");
}

// __omega_ls(path) → JSON [{name, mode, mtime, size}] come /fs/<path>?fmt=json di
// websrv (mode: "d" cartella, "m" punto di montaggio, "-" file…), senza "." e "..".
static JSValue js_ls(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
  (void)this_val;
  if (argc < 1) return JS_NULL;
  const char *a = JS_ToCString(ctx, argv[0]);
  if (!a) return JS_NULL;
  char p[1024]; clean_path(p, sizeof p, a); JS_FreeCString(ctx, a);
  struct stat ps; DIR *d = stat(p, &ps) == 0 ? opendir(p) : NULL;
  if (!d) return JS_NULL;
  size_t cap = 16384, o = 0; char *buf = malloc(cap);
  if (!buf) { closedir(d); return JS_NULL; }
  buf[o++] = '[';
  struct dirent *e; int n = 0;
  while ((e = readdir(d)) && n < 4000) {
    if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
    char full[1300]; snprintf(full, sizeof full, "%s/%s", strcmp(p, "/") ? p : "", e->d_name);
    struct stat st; if (stat(full, &st) != 0) continue;
    char mode = S_ISDIR(st.st_mode) ? (st.st_dev != ps.st_dev ? 'm' : 'd') : S_ISBLK(st.st_mode) ? 'b' : S_ISCHR(st.st_mode) ? 'c' :
                S_ISFIFO(st.st_mode) ? 'p' : S_ISSOCK(st.st_mode) ? 's' : '-';
    char name[600]; json_escape(name, sizeof name, e->d_name);
    char item[800];
    int k = snprintf(item, sizeof item, "%s{\"name\":\"%s\",\"mode\":\"%c\",\"mtime\":%ld,\"size\":%lld}", n ? "," : "", name,
                     mode, (long)st.st_mtime, (long long)st.st_size);
    if (o + (size_t)k + 2 >= cap) { cap *= 2; char *nb = realloc(buf, cap); if (!nb) break; buf = nb; }
    memcpy(buf + o, item, (size_t)k); o += (size_t)k; n++;
  }
  closedir(d);
  buf[o++] = ']'; buf[o] = 0;
  JSValue r = JS_NewString(ctx, buf);
  free(buf);
  return r;
}

// __omega_read(path) → testo del file (fino a 4 MB) o null
static JSValue js_read(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
  (void)this_val;
  if (argc < 1) return JS_NULL;
  const char *a = JS_ToCString(ctx, argv[0]);
  if (!a) return JS_NULL;
  char p[1024]; clean_path(p, sizeof p, a); JS_FreeCString(ctx, a);
  struct stat st;
  if (stat(p, &st) != 0 || !S_ISREG(st.st_mode)) return JS_NULL;
  size_t n = 0; char *b = file_read(p, 4u * 1024 * 1024, &n);
  if (!b) return JS_NULL;
  JSValue r = JS_NewStringLen(ctx, b, n);
  free(b);
  return r;
}

// __omega_fetch(url) → {status, body}, o null se la rete non risponde. Solo GET.
// L'attesa della rete non conta come tempo di CPU dello script.
static JSValue js_fetch(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
  (void)this_val;
  HbSess *h = JS_GetRuntimeOpaque(JS_GetRuntime(ctx));
  if (argc < 1 || !h) return JS_NULL;
  if (SDL_GetTicks() > h->hard) return JS_ThrowTypeError(ctx, "Failed to fetch (tempo scaduto)");
  const char *url = JS_ToCString(ctx, argv[0]);
  if (!url) return JS_EXCEPTION;
  char tmp[300]; snprintf(tmp, sizeof tmp, OMEGA_DIR "/dl/hbfetch-%p.tmp", (void *)h);
  { char d[300]; snprintf(d, sizeof d, OMEGA_DIR "/dl"); mkdir(OMEGA_DIR, 0777); mkdir(d, 0777); }
  Uint32 t0 = SDL_GetTicks();
  int st = omega_url_download(url, tmp, NULL, NULL, NULL);
  h->deadline += SDL_GetTicks() - t0;
  omega_log("homebrew.js fetch %s -> %d", url, st);
  JS_FreeCString(ctx, url);
  if (st <= 0) { unlink(tmp); return JS_NULL; }
  size_t n = 0; char *b = file_read(tmp, 8u * 1024 * 1024, &n);
  unlink(tmp);
  JSValue o = JS_NewObject(ctx);
  JS_SetPropertyStr(ctx, o, "status", JS_NewInt32(ctx, st));
  JS_SetPropertyStr(ctx, o, "body", b ? JS_NewStringLen(ctx, b, n) : JS_NewString(ctx, ""));
  free(b);
  return o;
}

static JSValue js_log(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
  (void)this_val;
  const char *m = argc > 0 ? JS_ToCString(ctx, argv[0]) : NULL;
  if (m) { omega_log("homebrew.js: %s", m); JS_FreeCString(ctx, m); }
  return JS_UNDEFINED;
}

// L'ambiente dell'iframe di websrv (homebrewApi.js e apiClient.js) riscritto in
// breve, con gli stessi nomi, firme e risultati, più le parti del browser che gli
// script usano (URL, URLSearchParams, fetch, timer…).
static const char HB_PRELUDE[] =
  "var window = globalThis, self = globalThis;\n"
  "window.workingDir = __omega_wd; window.extensionId = 'omega';\n"
  // l'iframe è un blob in sandbox senza allow-same-origin: origin "null"
  "window.location = { origin: 'null', href: 'about:blank', protocol: 'blob:', host: '', hostname: '', port: '', pathname: '', search: '', hash: '' };\n"
  "window.parent = { postMessage() {} }; window.top = window.parent;\n"
  "window.addEventListener = function () {}; window.removeEventListener = function () {};\n"
  "var navigator = { userAgent: 'Mozilla/5.0 (PlayStation; PlayStation 5) Omega', language: 'it-IT' };\n"
  "var document = { title: '', body: {}, getElementById() { return null; }, querySelector() { return null; }, querySelectorAll() { return []; },\n"
  "  createElement() { return { style: {}, classList: { add() {}, remove() {} }, appendChild() {}, setAttribute() {} }; }, addEventListener() {} };\n"
  "var console = { log() {}, info() {}, debug() {}, warn(...a) { __omega_log(a.join(' ')); }, error(...a) { __omega_log(a.join(' ')); } };\n"
  "var __st = { menu: null };\n"
  "function alert(m) { __st.alert = String(m); __omega_log('alert: ' + m); }\n"
  "function confirm() { return false; }\n"
  "function prompt() { return null; }\n"
  "var sessionStorage = { _d: {}, getItem(k) { return k in this._d ? this._d[k] : null; }, setItem(k, v) { this._d[k] = String(v); }, removeItem(k) { delete this._d[k]; }, clear() { this._d = {}; } };\n"
  "var localStorage = sessionStorage;\n"
  "var crypto = { getRandomValues(a) { for (let i = 0; i < a.length; i++) a[i] = Math.floor(Math.random() * 256); return a; } };\n"
  // timer a tempo virtuale: scattano quando non c'è altro da fare (setInterval una volta sola)
  "var __timers = [], __tid = 0, __vt = 0;\n"
  "function setTimeout(fn, ms, ...a) { const id = ++__tid; __timers.push({ id, t: __vt + (+ms || 0), fn, a }); return id; }\n"
  "function clearTimeout(id) { __timers = __timers.filter(t => t.id !== id); }\n"
  "var setInterval = setTimeout, clearInterval = clearTimeout;\n"
  "function __omega_timer() { if (!__timers.length) return false; __timers.sort((x, y) => x.t - y.t); const t = __timers.shift(); __vt = t.t;\n"
  "  if (typeof t.fn === 'function') t.fn(...t.a); return true; }\n"
  // URLSearchParams e URL, quanto basta, con la stessa codifica dei browser
  "class URLSearchParams {\n"
  "  constructor(init) { this._l = []; if (init == null) return;\n"
  "    if (typeof init === 'string') { const d = s => decodeURIComponent(s.replace(/\\+/g, ' '));\n"
  "      for (const p of init.replace(/^\\?/, '').split('&')) { if (!p) continue; const i = p.indexOf('='); this._l.push(i < 0 ? [d(p), ''] : [d(p.slice(0, i)), d(p.slice(i + 1))]); } }\n"
  "    else if (init instanceof URLSearchParams || Array.isArray(init)) { for (const [k, v] of (init._l || init)) this._l.push([String(k), String(v)]); }\n"
  "    else for (const k of Object.keys(init)) this._l.push([k, String(init[k])]); }\n"
  "  append(k, v) { this._l.push([String(k), String(v)]); }\n"
  "  set(k, v) { k = String(k); const i = this._l.findIndex(x => x[0] === k); if (i < 0) return this.append(k, v); this._l[i][1] = String(v); this._l = this._l.filter((x, j) => j <= i || x[0] !== k); }\n"
  "  get(k) { const e = this._l.find(x => x[0] === String(k)); return e ? e[1] : null; }\n"
  "  getAll(k) { return this._l.filter(x => x[0] === String(k)).map(x => x[1]); }\n"
  "  has(k) { return this._l.some(x => x[0] === String(k)); }\n"
  "  delete(k) { this._l = this._l.filter(x => x[0] !== String(k)); }\n"
  "  forEach(f, t) { for (const [k, v] of this._l) f.call(t, v, k, this); }\n"
  "  entries() { return this._l.map(x => x.slice())[Symbol.iterator](); } [Symbol.iterator]() { return this.entries(); }\n"
  "  keys() { return this._l.map(x => x[0])[Symbol.iterator](); } values() { return this._l.map(x => x[1])[Symbol.iterator](); }\n"
  "  get size() { return this._l.length; }\n"
  "  toString() { const e = s => encodeURIComponent(s).replace(/[!'()~]/g, c => '%' + c.charCodeAt(0).toString(16).toUpperCase()).replace(/%20/g, '+');\n"
  "    return this._l.map(([k, v]) => e(k) + '=' + e(v)).join('&'); }\n"
  "}\n"
  "class URL {\n"
  "  constructor(u, base) { u = String(u);\n"
  "    if (base !== undefined && !/^[a-zA-Z][a-zA-Z0-9+.-]*:/.test(u)) { const b = new URL(base);\n"
  "      u = u.startsWith('//') ? b.protocol + u : u.startsWith('/') ? b.protocol + '//' + b.host + u : b.protocol + '//' + b.host + b.pathname.replace(/[^\\/]*$/, '') + u; }\n"
  "    const m = /^([a-zA-Z][a-zA-Z0-9+.-]*:)(?:\\/\\/(?:([^:@\\/?#]*)(?::([^@\\/?#]*))?@)?(\\[[^\\]]*\\]|[^:\\/?#]*)(?::(\\d*))?)?([^?#]*)(\\?[^#]*)?(#.*)?$/.exec(u);\n"
  "    if (!m) throw new TypeError('Invalid URL: ' + u);\n"
  "    this.protocol = m[1].toLowerCase(); this.username = m[2] || ''; this.password = m[3] || ''; this.hostname = (m[4] || '').toLowerCase();\n"
  "    const def = { 'http:': '80', 'https:': '443', 'ws:': '80', 'wss:': '443', 'ftp:': '21' }[this.protocol];\n"
  "    this.port = m[5] && m[5] !== def ? m[5] : ''; this.pathname = m[6] || (m[4] !== undefined && def ? '/' : '');\n"
  "    this.search = m[7] && m[7] !== '?' ? m[7] : ''; this.hash = m[8] && m[8] !== '#' ? m[8] : '';\n"
  "    this.searchParams = new URLSearchParams(this.search); }\n"
  "  get host() { return this.hostname + (this.port ? ':' + this.port : ''); }\n"
  "  get origin() { return /^(https?|wss?|ftp):$/.test(this.protocol) ? this.protocol + '//' + this.host : 'null'; }\n"
  "  get href() { const q = this.searchParams.toString(); return this.protocol + (this.hostname || this.pathname.startsWith('//') ? '//' : '') +\n"
  "    (this.username ? this.username + (this.password ? ':' + this.password : '') + '@' : '') + this.host + this.pathname + (q ? '?' + q : '') + this.hash; }\n"
  "  toString() { return this.href; } toJSON() { return this.href; }\n"
  "}\n"
  // fetch: le richieste a websrv (/fs, /version) si servono dal disco, il resto va in rete
  "class Response {\n"
  "  constructor(status, body, url) { this.status = status; this.ok = status >= 200 && status < 300; this.statusText = ''; this.url = url; this._b = body == null ? '' : String(body);\n"
  "    this.headers = { get() { return null; }, has() { return false; } }; this.body = null; this.redirected = false; this.type = 'basic'; }\n"
  "  async text() { return this._b; } async json() { return JSON.parse(this._b); } clone() { return new Response(this.status, this._b, this.url); }\n"
  "}\n"
  "function __omega_local(rel) {\n"
  "  const q = rel.indexOf('?'), path = q < 0 ? rel : rel.slice(0, q), qs = new URLSearchParams(q < 0 ? '' : rel.slice(q));\n"
  "  if (path === '/version') return new Response(200, JSON.stringify({ tag: 'omega', date: '', time: '', api: 1 }), baseURL + rel);\n"
  "  if (path === '/fs' || path.startsWith('/fs/')) {\n"
  "    const p = decodeURIComponent(path.slice(3)) || '/';\n"
  "    const f = __omega_read(p);\n"
  "    if (f != null && !path.endsWith('/')) return new Response(200, f, baseURL + rel);\n"
  "    const l = __omega_ls(p);\n"
  "    if (l == null) return new Response(404, '', baseURL + rel);\n"
  "    if (qs.get('fmt') !== 'json') return new Response(200, l, baseURL + rel);\n"
  "    return new Response(200, JSON.stringify([{ name: '.', mode: 'd', mtime: 0, size: 0 }].concat(JSON.parse(l))), baseURL + rel);\n"
  "  }\n"
  "  return null;\n"
  "}\n"
  "async function fetch(input, init) {\n"
  "  let url = String(input && input.url ? input.url : input);\n"
  "  if (!/^[a-zA-Z][a-zA-Z0-9+.-]*:/.test(url)) url = baseURL + (url.startsWith('/') ? '' : '/') + url;\n"
  "  if ((init && init.method || 'GET').toUpperCase() !== 'GET') throw new TypeError('Failed to fetch');\n"
  "  if (url.startsWith(baseURL + '/')) { const r = __omega_local(url.slice(baseURL.length)); if (r) return r; }\n"
  "  const r = __omega_fetch(url.replace(/#.*$/, ''));\n"
  "  if (!r) throw new TypeError('Failed to fetch');\n"
  "  return new Response(r.status, r.body, url);\n"
  "}\n"
  // apiClient.js
  "class DirectoryListing {\n"
  "  constructor(name, mode, unixTimestamp = 0, sizeBytes = 0) { this.name = name; this.mode = mode; this.mtime = new Date(unixTimestamp * 1000); this.size = sizeBytes; }\n"
  "  static isDir(i) { return (i.mode === 'd' || i.mode === 'm' || i.mode === 'smb-share') && !ignoredFileNames.includes(i.name); }\n"
  "  isDir() { return DirectoryListing.isDir(this); }\n"
  "  isFile() { return this.mode === '-' && !ignoredFileNames.includes(this.name); }\n"
  "  getIcon() { return this.mode === 'm' || this.mode === 'd' ? DIR_ICON : this.mode === 'smb-share' ? SMB_SHARE_ICON : FILE_ICON; }\n"
  "  getHumanReadableMode() { return { b: 'Block device', c: 'Char device', l: 'Symbolic link', p: 'Fifo or socket', s: 'Socket' }[this.mode] || ''; }\n"
  "  getHumanReadableSize() { const s = this.size; return s < 1024 ? s + ' B' : s < 1048576 ? (s / 1024).toFixed(2) + ' KB' : s < 1073741824 ? (s / 1048576).toFixed(2) + ' MB' : (s / 1073741824).toFixed(2) + ' GB'; }\n"
  "}\n"
  "const HTTP_OK = 200, HTTP_ERROR_BAD_REQUEST = 400, HTTP_UNAUTHORIZED = 401, HTTP_FORBIDDEN = 403, HTTP_INTERNAL_SERVER_ERROR = 500;\n"
  "const SMB_PORT = 445, SMB_PROTOCOL = 'smb:', SMB_SCHEME_PREFIX = 'smb://';\n"
  "const DIR_ICON = 'dir-icon', FILE_ICON = 'file-icon', SMB_SHARE_ICON = 'smb-share-icon';\n"
  "const baseURL = window.location.origin == 'null' ? 'http://127.0.0.1:8080' : window.location.origin;\n"
  "const ignoredFileNames = ['.', '..'];\n"
  // parametri di /hbldr come ApiClient.launchApp (pipe=0: Omega non legge l'output)
  "function __omega_query(path, args, env, cwd, daemon) {\n"
  "  let params = new URLSearchParams({ pipe: '0', daemon: new Number(daemon).toString(), path: path });\n"
  "  if (typeof args === 'string') params.append('args', args.replace(/ /g, '\\\\ '));\n"
  "  else if (Array.isArray(args)) params.append('args', args.map(arg => arg.replace(/ /g, '\\\\ ')).join(' '));\n"
  "  if (env != null) params.append('env', Object.entries(env).map(([key, val]) => `${key}=${val}`.replace(/ /g, '\\\\ ')).join(' '));\n"
  "  if (cwd != null) params.append('cwd', cwd);\n"
  "  return params;\n"
  "}\n"
  "function __omega_sort(data) { data = data.filter(e => !ignoredFileNames.includes(e.name));\n"
  "  data.sort((x, y) => (DirectoryListing.isDir(x) == DirectoryListing.isDir(y)) ? x.name.localeCompare(y.name) : y.mode.localeCompare(x.mode));\n"
  "  return data.map(e => new DirectoryListing(e.name, e.mode, e.mtime, e.size)); }\n"
  "class ApiClient {\n"
  "  static async launchApp(path, args = null, env = null, cwd = null, daemon = false) {\n"
  "    const p = __omega_query(path, args, env, cwd, daemon); __st.launch = p.toString(); __st.daemon = p.get('daemon') !== '0';\n"
  "    return { status: 200, data: null }; }\n"
  "  static async fsListInternalDir(path) { if (!path.endsWith('/')) path += '/';\n"
  "    const r = await fetch(baseURL + '/fs' + path + '?fmt=json'); if (!r.ok) return { status: r.status, data: null };\n"
  "    return { status: r.status, data: __omega_sort(await r.json()) }; }\n"
  "  static getNetworkShareHttpProxyUrl(networkShareUrl) { const url = new URL(networkShareUrl);\n"
  "    if (url.protocol !== SMB_PROTOCOL) throw new Error('Unsupported network share protocol: ' + url.protocol);\n"
  "    let result = `${baseURL}/${url.protocol.replace(':', '')}${url.pathname}?addr=${url.hostname}`;\n"
  "    if (url.username) result += `&user=${url.username}`; if (url.password) result += `&pass=${url.password}`; return result; }\n"
  "  static async fsListSmbDir(path) { if (!path.startsWith(SMB_SCHEME_PREFIX)) return { status: HTTP_ERROR_BAD_REQUEST, data: null };\n"
  "    if (!path.endsWith('/')) path += '/';\n"
  "    const r = await fetch(ApiClient.getNetworkShareHttpProxyUrl(path)); if (!r.ok) return { status: r.status, data: null };\n"
  "    return { status: r.status, data: __omega_sort(await r.json()) }; }\n"
  "  static async listSmbShares(path) { if (!path.startsWith(SMB_SCHEME_PREFIX)) return { status: HTTP_ERROR_BAD_REQUEST, data: null };\n"
  "    if (path.endsWith('/')) path = path.substring(0, path.length - 1);\n"
  "    const r = await fetch(ApiClient.getNetworkShareHttpProxyUrl(path)); if (!r.ok) return { status: r.status, data: null };\n"
  "    return { status: r.status, data: (await r.json()).filter(e => !e.name.endsWith('$')).map(e => new DirectoryListing(e.name, 'smb-share')) }; }\n"
  "  static async fsListDir(path) { if (path.startsWith(SMB_SCHEME_PREFIX)) { const u = new URL(path);\n"
  "      return (u.pathname === '' || u.pathname === '/') ? await ApiClient.listSmbShares(path) : await ApiClient.fsListSmbDir(path); }\n"
  "    return await ApiClient.fsListInternalDir(path); }\n"
  "  static async fsGetFileStream(path) { if (path.endsWith('/') || !path.startsWith('/')) return { status: HTTP_ERROR_BAD_REQUEST, data: null };\n"
  "    const r = await fetch(baseURL + '/fs' + path); return { status: r.status, data: r.ok ? r._b : null }; }\n"
  "  static async fsGetFileText(path) { if (path.endsWith('/') || !path.startsWith('/')) return { status: HTTP_ERROR_BAD_REQUEST, data: null };\n"
  "    const r = await fetch(baseURL + '/fs' + path); return { status: r.status, data: r.ok ? await r.text() : null }; }\n"
  "  static async getMdnsDiscoveredLocations() { try { const r = await fetch(baseURL + '/mdns'); if (r.ok) return await r.json(); } catch (e) { } return []; }\n"
  "  static async getVersion() { try { const r = await fetch(baseURL + '/version'); if (r.ok) return await r.json(); } catch (e) { }\n"
  "    return { api: 0, tag: '', date: '', time: '' }; }\n"
  "}\n"
  // homebrewApi.js
  "function uuidv4() { return '10000000-1000-4000-8000-100000000000'.replace(/[018]/g, c => (+c ^ crypto.getRandomValues(new Uint8Array(1))[0] & 15 >> +c / 4).toString(16)); }\n"
  "function remapFunctionsToFunctionIds(o) { return o; }\n"
  "function showCarousel(items) { __st.carousel = Array.isArray(items) ? items : []; }\n"
  "function pickPath(initialPath = '', title = 'Select file...', allowNetworkLocations = false, pathType = 'any') {\n"
  "  return new Promise(resolve => { __st.pick = { cur: String(initialPath || ''), title: String(title), type: pathType, resolve }; }); }\n"
  "function pickFile(initialPath = '', title = 'Select file...', allowNetworkLocations = false) { return pickPath(initialPath, title, allowNetworkLocations, 'file'); }\n"
  "function pickDirectory(initialPath = '', title = 'Select directory...', allowNetworkLocations = false) { return pickPath(initialPath, title, allowNetworkLocations, 'dir'); }\n"
  "function pickDevice(title = 'Select storage device...', allowNetworkLocations = false) { return pickPath('', title, allowNetworkLocations, 'dev'); }\n";

// Il carousel di websrv tradotto in passi: Omega chiede lo stato, mostra un menu
// e riferisce la scelta. Le voci restano funzioni vive nella sessione.
static const char HB_DRIVER[] =
  // esegue una funzione dello script; l'esito si legge poi con __omega_state()
  "function __omega_run(fn) {\n"
  "  Object.assign(__st, { settled: false, result: undefined, error: null, carousel: null, pick: null, alert: null, menu: null });\n"
  "  Promise.resolve().then(() => typeof fn === 'function' ? fn() : undefined).then(r => { __st.settled = true; __st.result = r; }, e => { __st.settled = true; __st.error = e; });\n"
  "}\n"
  "function __omega_err(e) { return String(e) + (e && e.stack ? '\\n' + e.stack : ''); }\n"
  // dopo main(): nome, sottotitolo, immagine (come carouselView.js)
  "function __omega_meta() {\n"
  "  if (__st.error) return JSON.stringify({ error: __omega_err(__st.error) });\n"
  "  if (!__st.settled) return JSON.stringify({ pending: true });\n"
  "  const r = __st.result, it = Array.isArray(r) ? r[0] : r; __st.top = it;\n"
  "  if (!it || typeof it !== 'object') return JSON.stringify({ empty: true });\n"
  "  return JSON.stringify({ mainText: it.mainText == null ? '' : String(it.mainText), secondaryText: it.secondaryText == null ? '' : String(it.secondaryText),\n"
  "    imgPath: it.imgPath == null ? '' : String(it.imgPath), multi: Array.isArray(r) });\n"
  "}\n"
  "function __omega_menu(title, entries) { __st.menu = entries; __st.mtitle = title; __st.page = 0; }\n"
  // click sulla voce principale; con le options (tasto ••• di websrv) prima un menu
  "function __omega_begin() {\n"
  "  const it = __st.top;\n"
  "  if (it && Array.isArray(it.options) && it.options.length) {\n"
  "    __omega_menu('', [{ label: 'Avvia', act: () => __omega_run(it.onclick) }].concat(it.options.map(o => ({ label: String(o && o.text || '?'), act: () => __omega_run(o && o.onclick) }))));\n"
  "    Object.assign(__st, { settled: false, error: null, carousel: null, pick: null });\n"
  "  } else __omega_run(it && it.onclick);\n"
  "}\n"
  // pickFile & co.: un piccolo esploratore di file fatto di menu
  "function __omega_pickmenu() {\n"
  "  const pk = __st.pick, R = __omega_root, cur = pk.cur.replace(/\\/+$/, ''), rel = cur.slice(R.length), e = [];\n"
  "  const go = p => () => { pk.cur = p; };\n"
  "  const done = p => () => { __st.pick = null; __st.carousel = null; pk.resolve(p); };\n"
  // "Annulla" fa come il tasto indietro del file picker di websrv: pickFile() restituisce null
  "  const no = { label: '[ Annulla ]', act: done(null) };\n"
  "  if (pk.cur === '') {\n"
  "    e.push(no);\n"
  "    e.push({ label: 'Memoria della console (/)', act: pk.type === 'dev' ? done(R + '/') : go(R + '/') });\n"
  "    let mnt = []; try { mnt = JSON.parse(__omega_ls(R + '/mnt') || '[]'); } catch (x) { }\n"
  "    for (const m of mnt.sort((x, y) => x.name.localeCompare(y.name))) {\n"
  "      const n = parseInt(m.name.substring(3)); if (!/^(usb|ext)/.test(m.name) || isNaN(n) || (m.mode !== 'm' && m.mode !== 'd')) continue;\n"
  "      const lb = m.name == 'ext0' ? 'Archiviazione estesa USB' : m.name == 'ext1' ? 'SSD M.2' : (m.name.startsWith('usb') ? 'USB ' : 'Archiviazione ') + (n + 1);\n"
  "      e.push({ label: lb + ' (/mnt/' + m.name + ')', act: pk.type === 'dev' ? done(R + '/mnt/' + m.name + '/') : go(R + '/mnt/' + m.name + '/') });\n"
  "    }\n"
  "    __omega_menu(pk.title, e); return;\n"
  "  }\n"
  "  e.push({ label: '.. (indietro)', act: go(!cur.startsWith(R) || rel === '' || /^\\/mnt\\/[^\\/]+$/.test(rel) ? '' : cur.replace(/\\/[^\\/]*$/, '') + '/') });\n"
  "  e.push(no);\n"
  "  if (pk.type === 'dir' || pk.type === 'any') e.push({ label: '[ Usa questa cartella ]', act: done(cur + '/') });\n"
  "  let l = []; try { l = __omega_sort(JSON.parse(__omega_ls(cur || '/') || '[]')); } catch (x) { }\n"
  "  for (const d of l) {\n"
  "    if (d.isDir()) e.push({ label: d.name + '/', act: go(cur + '/' + d.name + '/') });\n"
  "    else if (pk.type !== 'dir') e.push({ label: d.name, act: done(cur + '/' + d.name) });\n"
  "  }\n"
  "  __omega_menu(pk.title + ' ' + (rel || '/'), e);\n"
  "}\n"
  "function __omega_carmenu() { const c = __st.carousel; __st.carousel = null;\n"
  "  __omega_menu('', c.filter(x => x && typeof x === 'object').map(x => ({ label: String(x.mainText == null ? '?' : x.mainText), act: () => __omega_run(x.onclick) }))); }\n"
  // stato dopo un passo: launch, menu (a pagine di maxn voci), errore o niente
  "function __omega_state(maxn) {\n"
  "  const st = __st;\n"
  "  if (st.launch) return JSON.stringify({ launch: st.launch, daemon: st.daemon });\n"
  "  if (st.error) { const e = st.error; st.error = null; return JSON.stringify({ error: __omega_err(e) }); }\n"
  "  if (!st.menu && st.pick) __omega_pickmenu();\n"
  "  if (!st.menu && st.settled) {\n"
  // il risultato passa da JSON come nel postMessage di websrv, poi come carouselView: if (res && res.path) launchApp(...)
  "    const res = st.result == null ? null : JSON.parse(JSON.stringify(st.result));\n"
  "    if (res && res.path) { const p = __omega_query(res.path, res.args, res.env, res.cwd, res.daemon === undefined ? false : res.daemon);\n"
  "      st.launch = p.toString(); st.daemon = p.get('daemon') !== '0'; return JSON.stringify({ launch: st.launch, daemon: st.daemon }); }\n"
  "  }\n"
  "  if (!st.menu && st.carousel) { if (!st.carousel.length) { st.carousel = null; return JSON.stringify({ empty: true }); } __omega_carmenu(); }\n"
  "  if (st.menu) {\n"
  "    const m = st.menu, n = m.length; if (!n) return JSON.stringify({ empty: true });\n"
  "    if (n <= maxn) return JSON.stringify({ title: st.mtitle, items: m.map(x => x.label) });\n"
  "    const per = maxn - 1, pages = Math.ceil(n / per), p = st.page % pages;\n"
  "    return JSON.stringify({ title: (st.mtitle ? st.mtitle + ' ' : '') + '(' + (p + 1) + '/' + pages + ')', items: m.slice(p * per, p * per + per).map(x => x.label).concat(['Altri... (' + (p + 1 < pages ? p + 2 : 1) + '/' + pages + ')']) });\n"
  "  }\n"
  "  if (st.settled) return JSON.stringify({ none: true, alert: st.alert });\n"
  "  return JSON.stringify({ pending: true });\n"
  "}\n"
  // scelta i del menu mostrato (con le pagine)
  "function __omega_choose(i, maxn) {\n"
  "  const m = __st.menu; if (!m) return;\n"
  "  let k = i;\n"
  "  if (m.length > maxn) { const per = maxn - 1, pages = Math.ceil(m.length / per), p = __st.page % pages;\n"
  "    if (i >= per) { __st.page = p + 1; return; } k = p * per + i; }\n"
  "  const e = m[k]; if (!e) return;\n"
  "  __st.menu = null; e.act();\n"
  "}\n";

static void hb_js_free(HbSess *h) {
  if (h->ctx) JS_FreeContext(h->ctx);
  if (h->rt) JS_FreeRuntime(h->rt);
  h->ctx = NULL; h->rt = NULL;
}

// carica l'ambiente e homebrew.js in un nuovo runtime
static int hb_js_open(HbSess *h, const char *dir, char *err, size_t en) {
  char path[600]; snprintf(path, sizeof path, "%s/homebrew.js", dir);
  size_t sl = 0; char *src = file_read(path, 256 * 1024, &sl);
  if (!src) { snprintf(err, en, "homebrew.js non leggibile"); return -1; }
  h->rt = JS_NewRuntime();
  if (!h->rt) { free(src); snprintf(err, en, "Memoria insufficiente per lo script"); return -1; }
  JS_SetMemoryLimit(h->rt, HB_MEM);
  JS_SetMaxStackSize(h->rt, HB_STACK);
  JS_SetRuntimeOpaque(h->rt, h);
  h->deadline = SDL_GetTicks() + HB_STEP_MS; h->hard = SDL_GetTicks() + HB_HARD_MS;
  JS_SetInterruptHandler(h->rt, qjs_interrupt, h);
  h->ctx = JS_NewContext(h->rt);
  if (!h->ctx) { free(src); hb_js_free(h); snprintf(err, en, "Memoria insufficiente per lo script"); return -1; }
  JSContext *ctx = h->ctx;
  JSValue g = JS_GetGlobalObject(ctx);
  JS_SetPropertyStr(ctx, g, "__omega_ls", JS_NewCFunction(ctx, js_ls, "__omega_ls", 1));
  JS_SetPropertyStr(ctx, g, "__omega_read", JS_NewCFunction(ctx, js_read, "__omega_read", 1));
  JS_SetPropertyStr(ctx, g, "__omega_fetch", JS_NewCFunction(ctx, js_fetch, "__omega_fetch", 1));
  JS_SetPropertyStr(ctx, g, "__omega_log", JS_NewCFunction(ctx, js_log, "__omega_log", 1));
  // workingDir come in websrv: la cartella di homebrew.js
  JS_SetPropertyStr(ctx, g, "__omega_wd", JS_NewString(ctx, dir));
  JS_SetPropertyStr(ctx, g, "__omega_root", JS_NewString(ctx, OMEGA_SYSROOT));
  JS_FreeValue(ctx, g);
  int ok = 1;
  const char *parts[2] = { HB_PRELUDE, HB_DRIVER }; const char *pn[2] = { "<websrv>", "<omega>" };
  for (int i = 0; i < 2 && ok; i++) {
    JSValue v = JS_Eval(ctx, parts[i], strlen(parts[i]), pn[i], JS_EVAL_TYPE_GLOBAL);
    if (JS_IsException(v)) { ok = 0; js_take_exc(ctx, dir, err, en); }
    JS_FreeValue(ctx, v);
  }
  if (ok) {
    JSValue v = JS_Eval(ctx, src, sl, "homebrew.js", JS_EVAL_TYPE_GLOBAL);
    if (JS_IsException(v)) { ok = 0; js_take_exc(ctx, dir, err, en); }
    JS_FreeValue(ctx, v);
  }
  free(src);
  if (!ok) { hb_js_free(h); return -1; }
  return 0;
}

// esegue codice di servizio; il risultato, se è una stringa, torna con malloc
static char *hb_js_call(HbSess *h, const char *code, const char *dir) {
  JSValue v = JS_Eval(h->ctx, code, strlen(code), "<omega>", JS_EVAL_TYPE_GLOBAL);
  char *out = NULL;
  if (JS_IsException(v)) js_take_exc(h->ctx, dir, NULL, 0);
  else if (JS_IsString(v)) { const char *s = JS_ToCString(h->ctx, v); if (s) { out = strdup(s); JS_FreeCString(h->ctx, s); } }
  JS_FreeValue(h->ctx, v);
  return out;
}

// fa girare promesse e timer finché non resta niente da fare o scade il tempo
static void hb_js_pump(HbSess *h, const char *dir) {
  for (int timers = 0; ; ) {
    JSContext *c;
    int r = JS_ExecutePendingJob(h->rt, &c);
    if (r < 0) js_take_exc(c, dir, NULL, 0);
    Uint32 now = SDL_GetTicks();
    if (now > h->deadline) { omega_log("homebrew.js %s: tempo scaduto", dir); break; }
    if (r != 0) continue;
    if (++timers > 1000) break;
    JSValue v = JS_Eval(h->ctx, "__omega_timer()", 15, "<omega>", JS_EVAL_TYPE_GLOBAL);
    int more = JS_ToBool(h->ctx, v) > 0;
    if (JS_IsException(v)) js_take_exc(h->ctx, dir, NULL, 0);
    JS_FreeValue(h->ctx, v);
    if (!more) break;
  }
}

// nuovo passo: tempo pieno e stack del thread corrente (ogni passo può girare in un thread diverso)
static void hb_js_step(HbSess *h) {
  JS_UpdateStackTop(h->rt);
  h->deadline = SDL_GetTicks() + HB_STEP_MS; h->hard = SDL_GetTicks() + HB_HARD_MS;
}

// esegue main() nella sessione aperta; 0 = ok, metadati JSON in *meta (malloc)
static int hb_js_main(HbSess *h, const char *dir, char **meta, char *err, size_t en) {
  hb_js_step(h);
  char *s = hb_js_call(h, "__omega_run(() => main())", dir); free(s);
  hb_js_pump(h, dir);
  *meta = hb_js_call(h, "__omega_meta()", dir);
  JVal *j = *meta ? json_parse(*meta) : NULL;
  int rc = 0;
  if (!j) { rc = -1; snprintf(err, en, "main() non ha risposto"); }
  else if (jget(j, "error")) {
    rc = -1; omega_log("homebrew.js %s: main() fallita: %s", dir, jstr(j, "error", "?"));
    snprintf(err, en, "%s", jstr(j, "error", "?")); char *nl = strchr(err, '\n'); if (nl) *nl = 0;   // lo stack resta nel log
  }
  else if (jget(j, "pending")) { rc = -1; snprintf(err, en, "main() non ha risposto in tempo"); }
  else if (jget(j, "empty")) { rc = -1; snprintf(err, en, "main() non ha restituito nulla"); }
  json_free(j);
  return rc;
}
#endif

// imgPath restituito dall'ultimo hb_meta, per hb_icon
static char g_meta_dir[400], g_meta_img[600];

// Nome e sottotitolo dell'homebrew, da main() di homebrew.js; 1 se trovati.
int hb_meta(const char *dir, char *name, size_t n, char *sub, size_t sn) {
  name[0] = sub[0] = 0;
  snprintf(g_meta_dir, sizeof g_meta_dir, "%s", dir); g_meta_img[0] = 0;
#ifdef HAVE_QJS
  HbSess h; memset(&h, 0, sizeof h);
  char err[200], *meta = NULL;
  if (hb_js_open(&h, dir, err, sizeof err) == 0) {
    if (hb_js_main(&h, dir, &meta, err, sizeof err) == 0) {
      JVal *j = json_parse(meta);
      if (j && !jbool(j, "multi")) jcpy(name, n, j, "mainText");
      jcpy(sub, sn, j, "secondaryText");
      jcpy(g_meta_img, sizeof g_meta_img, j, "imgPath");
      json_free(j);
    }
    free(meta);
    hb_js_free(&h);
  }
#else
  (void)n; (void)sn;
#endif
  return name[0] != 0;
}

// Icona come la sceglie websrv: imgPath di main() se è un file locale
// ("/fs/<dir>/x.jpg", anche con baseURL davanti), altrimenti sce_sys/icon0.png.
int hb_icon(const char *dir, char *out, size_t n) {
  out[0] = 0;
  if (!strcmp(dir, g_meta_dir) && g_meta_img[0]) {
    const char *p = g_meta_img;
    if (!strncmp(p, WEBSRV_URL, sizeof WEBSRV_URL - 1)) p += sizeof WEBSRV_URL - 1;
    if (!strncmp(p, "/fs/", 4) || !strcmp(p, "/fs")) {
      char raw[700]; snprintf(raw, sizeof raw, "/%s", p + 3);
      char t[700]; size_t o = 0;
      for (const char *c = raw; *c && o + 1 < sizeof t; c++) { if (*c == '/' && o && t[o - 1] == '/') continue; t[o++] = *c; }
      t[o] = 0;
      struct stat st;
      if (stat(t, &st) == 0 && S_ISREG(st.st_mode)) { snprintf(out, n, "%s", t); return 1; }
    }
  }
  snprintf(out, n, "%s/sce_sys/icon0.png", dir);
  if (access(out, 0) == 0) return 1;
  out[0] = 0; return 0;
}

static void hb_end(void) {
#ifdef HAVE_QJS
  hb_js_free(&g_hb);
#endif
  free(g_hb.query);
  memset(&g_hb, 0, sizeof g_hb);
}

// Valuta lo script fino a un menu o ai parametri di avvio (vedi hb_launch).
static int hb_resolve(const char *dir, int choice, char *err, size_t en, char (*names)[64], int maxn, int *nn) {
  if (choice < 0 || strcmp(g_hb.dir, dir)) {
    hb_end();
    snprintf(g_hb.dir, sizeof g_hb.dir, "%s", dir);
    char js_path[600]; snprintf(js_path, sizeof js_path, "%s/homebrew.js", dir);
    if (access(js_path, 0) != 0) {
      // solo eboot.elf: websrv lo avvia come onclick → { path: <dir>/eboot.elf }
      char p[600], ep[1800], q[2000]; snprintf(p, sizeof p, "%s/eboot.elf", dir);
      formenc(ep, sizeof ep, p);
      snprintf(q, sizeof q, "pipe=0&daemon=0&path=%s", ep);
      g_hb.query = strdup(q);
      return g_hb.query ? 0 : -1;
    }
#ifdef HAVE_QJS
    char *meta = NULL, e2[300];
    if (hb_js_open(&g_hb, dir, e2, sizeof e2) || hb_js_main(&g_hb, dir, &meta, e2, sizeof e2)) {
      free(meta); hb_end();
      snprintf(err, en, "Lo script di avvio di questo homebrew non funziona: %.150s", e2);
      return -1;
    }
    free(meta);
    char *s = hb_js_call(&g_hb, "__omega_begin()", dir); free(s);
#else
    (void)names; (void)maxn; (void)nn;
    snprintf(err, en, "(desktop senza QuickJS) avvio non disponibile"); return -1;
#endif
  } else {
    if (g_hb.query) return 0;
#ifdef HAVE_QJS
    if (!g_hb.ctx) { snprintf(err, en, "Sessione di avvio scaduta: riprova"); return -1; }
    hb_js_step(&g_hb);
    char code[64]; snprintf(code, sizeof code, "__omega_choose(%d, %d)", choice, maxn > 1 ? maxn : 16);
    char *s = hb_js_call(&g_hb, code, dir); free(s);
#endif
  }
#ifdef HAVE_QJS
  hb_js_pump(&g_hb, dir);
  char code[48]; snprintf(code, sizeof code, "__omega_state(%d)", maxn > 1 ? maxn : 16);
  char *s = hb_js_call(&g_hb, code, dir);
  JVal *j = s ? json_parse(s) : NULL;
  free(s);
  int rc = -1;
  JVal *it = jget(j, "items");
  if (!j) snprintf(err, en, "Lo script di avvio non ha risposto");
  else if (jget(j, "launch")) {
    g_hb.query = strdup(jstr(j, "launch", ""));
    g_hb.daemon = jbool(j, "daemon");
    hb_js_free(&g_hb);      // lo script non serve più
    rc = g_hb.query ? 0 : -1;
  } else if (it && it->t == J_ARR) {
    if (names && nn) {
      JFOR(x, it) { if (*nn >= maxn) break; snprintf(names[*nn], 64, "%s", jstr(x, NULL, "?")); (*nn)++; }
      snprintf(err, en, "%s", jstr(j, "title", ""));
      rc = 1;
    } else snprintf(err, en, "Questo homebrew chiede di scegliere tra più voci");
  } else if (jget(j, "error")) {
    char m[160]; snprintf(m, sizeof m, "%s", jstr(j, "error", "?"));
    char *nl = strchr(m, '\n'); if (nl) *nl = 0;
    omega_log("homebrew.js %s: %s", dir, jstr(j, "error", "?"));
    if (strstr(m, "Failed to fetch")) snprintf(err, en, "Il server che l'homebrew usa non risponde: controlla la rete (o l'indirizzo scritto nello script)");
    else snprintf(err, en, "Errore nello script di avvio: %s", m);
  } else if (jget(j, "empty")) snprintf(err, en, "Nessun contenuto da avviare (mancano i file, es. ROM o dati del gioco?)");
  else if (jget(j, "none")) {
    const char *al = jstr(j, "alert", NULL);
    if (al && *al) snprintf(err, en, "%.180s", al);
    else snprintf(err, en, "Nessun file scelto: l'homebrew non ha indicato cosa avviare");
  } else snprintf(err, en, "Lo script di avvio non ha risposto in tempo");
  json_free(j);
  if (rc < 0) hb_end();
  return rc;
#else
  return -1;
#endif
}

// Avvia l'homebrew della cartella.
//   choice: -1 nuovo avvio (main() e click), >= 0 voce scelta nell'ultimo menu
//   dry:    valuta soltanto, un passo alla volta, senza chiedere a websrv di avviare;
//           con dry = 0 usa i parametri già risolti, se ci sono
//   names/maxn: voci del menu (maxn per pagina, oltre compare "Altri...")
// Ritorna 0 avviato (o pronto, con dry), 2 avviato come demone (Omega resta
// aperta), 1 serve una scelta (voci in names, titolo o "" in err), -1 errore
// (in err). Può bloccare sulla rete: va chiamata fuori dal thread principale.
int hb_launch(const char *dir, int choice, int dry, char *err, size_t en, char (*names)[64], int maxn, int *nn) {
  if (nn) *nn = 0;
  err[0] = 0;
  if (dry || !g_hb.query || strcmp(g_hb.dir, dir)) {
    int r = hb_resolve(dir, dry ? choice : -1, err, en, names, maxn, nn);
    if (r < 0 || dry) return r;
    if (r == 1) { hb_end(); snprintf(err, en, "Scegli prima una voce dal menu dell'homebrew"); return -1; }
  }
  static char url[8192];
  snprintf(url, sizeof url, WEBSRV_URL "/hbldr?%s", g_hb.query);
  int daemon = g_hb.daemon;
  hb_end();
  omega_log("avvio homebrew%s: %s", daemon ? " (daemon)" : "", url);
#ifdef PS5
  unsigned char b[16];
  long r = omega_url_peek(url, b, sizeof b);   // websrv chiude Omega e avvia l'homebrew
  if (r < 0) { snprintf(err, en, "websrv non risponde o non è riuscito ad avviarlo (porta 8080): è avviato?"); return -1; }
#endif
  return daemon ? 2 : 0;
}

static int rmtree(const char *p) {
  struct stat st;
  if (lstat(p, &st) != 0) return 0;
  if (S_ISDIR(st.st_mode)) {
    DIR *d = opendir(p);
    if (d) {
      struct dirent *e;
      while ((e = readdir(d))) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
        char c[1024]; snprintf(c, sizeof c, "%s/%s", p, e->d_name);
        rmtree(c);
      }
      closedir(d);
    }
    return rmdir(p);
  }
  return unlink(p);
}
// Cancella dir solo se sta almeno un livello sotto root e non contiene "..".
int hb_remove(const char *dir, const char *root) {
  size_t L = strlen(root);
  if (strncmp(dir, root, L) || dir[L] != '/' || !dir[L + 1] || strstr(dir, "..")) return -1;
  return rmtree(dir);
}
