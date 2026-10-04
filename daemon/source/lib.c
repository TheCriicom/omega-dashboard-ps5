// Omega — "La mia libreria": i backup dei giochi che l'utente possiede, con link,
// copertina e dati. Resta solo sulla console (OMEGA_DIR/library.json) e il demone
// è l'unico a scriverla: la UI e il telecomando passano dalle sue rotte.
// Si riempie a mano (dalla UI o dal telefono) oppure da un JSON: incollato o
// caricato dal telefono, o da un link che il demone scarica e risincronizza.
// I nomi dei campi accettati sono gli stessi del server (api/src/endpoints/store.js).
#include "lib.h"
#include "json.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <ctype.h>

#define MAX_ITEMS 2000
#define MAX_JSON (8 * 1024 * 1024)

void player_log(const char *fmt, ...);

typedef struct {
  char id[40], title[128], url[1024], cover[1024], platform[8], version[24], title_id[16], kind[8], origin[8];   // origin: manual, import, json, upload
  char desc[1200];
  char shots[4][512]; int nshots;
  double size;
} Item;

static pthread_mutex_t mx = PTHREAD_MUTEX_INITIALIZER;
static Item *items; static int n;
static char src_url[1024]; static long src_sync; static char src_status[64];
static char path[300];
static lib_fetch_fn fetch_fn;
static unsigned seq;

static unsigned fnv(const char *s) { unsigned h = 2166136261u; for (; *s; s++) { h ^= (unsigned char)*s; h *= 16777619u; } return h; }

static int is_http(const char *u) { return !strncasecmp(u, "http://", 7) || !strncasecmp(u, "https://", 8); }
// file caricati dal telefono o dal PC (ctl.c): restano sulla console
#ifdef PS5
static int is_local(const char *u) { return !strncmp(u, "file:///data/", 13) && !strstr(u, "/../"); }
#else
static int is_local(const char *u) { return !strncmp(u, "file:///", 8) && !strstr(u, "/../"); }   // prova sul Mac: cartella di prova
#endif

static void kind_of(const char *hint, const char *url, char *out, size_t on) {
  const char *k = hint && *hint ? hint : NULL;
  if (k && (!strcasecmp(k, "pkg") || !strcasecmp(k, "zip") || !strcasecmp(k, "elf") || !strcasecmp(k, "folder"))) { snprintf(out, on, "%s", k); for (char *c = out; *c; c++) *c = (char)(*c | 0x20); return; }
  const char *q = strchr(url, '?'); size_t L = q ? (size_t)(q - url) : strlen(url);
  if (L >= 4 && !strncasecmp(url + L - 4, ".pkg", 4)) snprintf(out, on, "pkg");
  else if (L >= 4 && !strncasecmp(url + L - 4, ".zip", 4)) snprintf(out, on, "zip");
  else if (L >= 4 && !strncasecmp(url + L - 4, ".elf", 4)) snprintf(out, on, "elf");
  else snprintf(out, on, "auto");
}

static const char *first(JVal *o, const char *const *keys) {
  for (int i = 0; keys[i]; i++) { const char *v = jstr(o, keys[i], NULL); if (v && *v) return v; }
  return "";
}

// un elemento da JSON (manuale o importato); 0 = valido
static int item_from(Item *it, JVal *o, const char *origin, int idx) {
  static const char *const K_TITLE[] = { "title", "name", NULL };
  static const char *const K_URL[] = { "url", "download_url", "download", "file", "pkg", "link", NULL };
  static const char *const K_COVER[] = { "cover", "cover_url", "image", "icon", NULL };
  static const char *const K_DESC[] = { "description", "desc", "text", NULL };
  static const char *const K_VER[] = { "version", "ver", NULL };
  static const char *const K_TID[] = { "title_id", "titleId", "tid", NULL };
  static const char *const K_PLAT[] = { "platform", "plat", NULL };
  static const char *const K_KIND[] = { "type", "kind", "file_kind", NULL };
  memset(it, 0, sizeof *it);
  snprintf(it->title, sizeof it->title, "%s", first(o, K_TITLE));
  snprintf(it->url, sizeof it->url, "%s", first(o, K_URL));
  if (!it->title[0] || !(is_http(it->url) || (!strcmp(origin, "upload") && is_local(it->url)))) return -1;
  const char *cv = first(o, K_COVER); if (is_http(cv)) snprintf(it->cover, sizeof it->cover, "%s", cv);
  snprintf(it->desc, sizeof it->desc, "%s", first(o, K_DESC));
  snprintf(it->version, sizeof it->version, "%s", first(o, K_VER));
  const char *tid = first(o, K_TID);
  if (strlen(tid) == 9) { int ok = 1; for (int i = 0; i < 9; i++) ok &= i < 4 ? (tid[i] >= 'A' && tid[i] <= 'Z') : (tid[i] >= '0' && tid[i] <= '9'); if (ok) snprintf(it->title_id, sizeof it->title_id, "%s", tid); }
  char pl[8]; snprintf(pl, sizeof pl, "%s", first(o, K_PLAT)); for (char *c = pl; *c; c++) *c = (char)toupper((unsigned char)*c);
  if (!strcmp(pl, "PS4") || !strcmp(pl, "PS5")) snprintf(it->platform, sizeof it->platform, "%s", pl);
  kind_of(first(o, K_KIND), it->url, it->kind, sizeof it->kind);
  it->size = jnum(o, "size", jnum(o, "size_bytes", 0));
  JVal *sh = jget(o, "screenshots"); if (!sh) sh = jget(o, "images");
  JFOR(s, sh) { if (it->nshots >= 4) break; if (s->s && is_http(s->s)) snprintf(it->shots[it->nshots++], 512, "%s", s->s); }
  snprintf(it->origin, sizeof it->origin, "%s", origin);
  const char *ext = jstr(o, "id", NULL);
  if (ext && *ext) snprintf(it->id, sizeof it->id, "%c%.30s", origin[0], ext);
  else snprintf(it->id, sizeof it->id, "%c%08x%04x", origin[0], fnv(it->url), (unsigned)(idx & 0xffff));
  return 0;
}

// ---------------------------------------------------------------- file --
static void jw(FILE *f, const char *k, const char *v, int comma) {
  fprintf(f, "%s\"%s\":\"", comma ? "," : "", k);
  for (const unsigned char *p = (const unsigned char *)v; *p; p++) {
    if (*p == '"' || *p == '\\') fprintf(f, "\\%c", *p);
    else if (*p < 0x20) fprintf(f, "\\u%04x", *p);
    else fputc(*p, f);
  }
  fputc('"', f);
}
static void save_locked(void) {
  char tmp[320]; snprintf(tmp, sizeof tmp, "%s.tmp", path);
  FILE *f = fopen(tmp, "w"); if (!f) return;
  fputs("{\"source\":{", f); jw(f, "url", src_url, 0); fprintf(f, ",\"last_sync\":%ld", src_sync); jw(f, "status", src_status, 1); fputs("},\"items\":[", f);
  for (int i = 0; i < n; i++) {
    Item *it = &items[i];
    fputs(i ? ",{" : "{", f);
    jw(f, "id", it->id, 0); jw(f, "title", it->title, 1); jw(f, "url", it->url, 1); jw(f, "cover", it->cover, 1);
    jw(f, "platform", it->platform, 1); jw(f, "version", it->version, 1); jw(f, "title_id", it->title_id, 1);
    jw(f, "kind", it->kind, 1); jw(f, "origin", it->origin, 1); jw(f, "description", it->desc, 1);
    fprintf(f, ",\"size\":%.0f,\"screenshots\":[", it->size);
    for (int k = 0; k < it->nshots; k++) { if (k) fputc(',', f); fputc('"', f); fputs(it->shots[k], f); fputc('"', f); }
    fputs("]}", f);
  }
  fputs("]}\n", f); fclose(f);
  rename(tmp, path);
  seq++;
}

void lib_init(const char *file, lib_fetch_fn fn) {
  snprintf(path, sizeof path, "%s", file); fetch_fn = fn;
  items = calloc(MAX_ITEMS, sizeof *items);
  FILE *f = fopen(path, "r"); if (!f) return;
  fseek(f, 0, SEEK_END); long L = ftell(f); fseek(f, 0, SEEK_SET);
  char *b = L > 0 && L < MAX_JSON ? malloc((size_t)L + 1) : NULL;
  if (b && fread(b, 1, (size_t)L, f) == (size_t)L) {
    b[L] = 0;
    JVal *j = json_parse(b);
    JVal *s = jget(j, "source");
    jcpy(src_url, sizeof src_url, s, "url"); src_sync = (long)jnum(s, "last_sync", 0); jcpy(src_status, sizeof src_status, s, "status");
    JFOR(o, jget(j, "items")) {
      if (n >= MAX_ITEMS) break;
      Item it; const char *origin = jstr(o, "origin", "manual");
      if (item_from(&it, o, origin[0] == 'j' ? "json" : origin[0] == 'i' ? "import" : origin[0] == 'u' ? "upload" : "manual", n) == 0) { const char *id = jstr(o, "id", ""); if (*id) snprintf(it.id, sizeof it.id, "%s", id); items[n++] = it; }
    }
    json_free(j);
  }
  free(b); fclose(f);
  player_log("libreria: %d giochi", n);
}

// ------------------------------------------------------------------ JSON --
// "chiave":"valore" con escape (k NULL = solo la stringa, per gli array)
static size_t put(char *o, size_t cap, size_t at, const char *k, const char *v, int comma) {
  if (at + 16 >= cap) return at;
  if (k) at += (size_t)snprintf(o + at, cap - at, "%s\"%s\":\"", comma ? "," : "", k);
  else at += (size_t)snprintf(o + at, cap - at, "%s\"", comma ? "," : "");
  for (const unsigned char *p = (const unsigned char *)v; *p && at + 8 < cap; p++) {
    if (*p == '"' || *p == '\\') { o[at++] = '\\'; o[at++] = (char)*p; }
    else if (*p < 0x20) at += (size_t)snprintf(o + at, cap - at, "\\u%04x", *p);
    else o[at++] = (char)*p;
  }
  if (at + 2 < cap) o[at++] = '"';
  o[at] = 0;
  return at;
}

size_t lib_list_json(char *o, size_t cap) {
  pthread_mutex_lock(&mx);
  size_t at = (size_t)snprintf(o, cap, "{\"seq\":%u,\"source\":{", seq);
  at = put(o, cap, at, "url", src_url, 0);
  at += (size_t)snprintf(o + at, cap - at, ",\"last_sync\":%ld", src_sync);
  at = put(o, cap, at, "status", src_status, 1);
  at += (size_t)snprintf(o + at, cap - at, "},\"items\":[");
  for (int i = 0; i < n && at + 4096 < cap; i++) {
    Item *it = &items[i];
    o[at++] = i ? ',' : ' '; o[at++] = '{';
    at = put(o, cap, at, "id", it->id, 0); at = put(o, cap, at, "title", it->title, 1); at = put(o, cap, at, "url", it->url, 1);
    at = put(o, cap, at, "cover", it->cover, 1); at = put(o, cap, at, "platform", it->platform, 1); at = put(o, cap, at, "version", it->version, 1);
    at = put(o, cap, at, "title_id", it->title_id, 1); at = put(o, cap, at, "kind", it->kind, 1); at = put(o, cap, at, "origin", it->origin, 1);
    at = put(o, cap, at, "description", it->desc, 1);
    at += (size_t)snprintf(o + at, cap - at, ",\"size\":%.0f,\"screenshots\":[", it->size);
    for (int k = 0; k < it->nshots; k++) at = put(o, cap, at, NULL, it->shots[k], k > 0);
    at += (size_t)snprintf(o + at, cap - at, "]}");
  }
  at += (size_t)snprintf(o + at, cap - at, "]}");
  pthread_mutex_unlock(&mx);
  return at < cap ? at : cap - 1;
}

// ------------------------------------------------------------- modifiche --
int lib_add(JVal *o, char *err, size_t en) {
  if (!items) { snprintf(err, en, "starting"); return -1; }
  Item it;
  const char *id = jstr(o, "id", NULL);
  // un gioco caricato si può rinominare o completare, ma file e tipo restano quelli
  char up_url[1024] = "", up_kind[8] = "";
  pthread_mutex_lock(&mx);
  if (id && *id) for (int i = 0; i < n; i++) if (!strcmp(items[i].id, id) && !strcmp(items[i].origin, "upload")) { snprintf(up_url, sizeof up_url, "%s", items[i].url); snprintf(up_kind, sizeof up_kind, "%s", items[i].kind); }
  pthread_mutex_unlock(&mx);
  if (up_url[0]) {
    if (item_from(&it, o, "upload", 0) != 0 && !it.title[0]) { snprintf(err, en, "title_and_url_required"); return -1; }
    snprintf(it.url, sizeof it.url, "%s", up_url); snprintf(it.kind, sizeof it.kind, "%s", up_kind);
  } else if (item_from(&it, o, "manual", (int)time(NULL)) != 0) { snprintf(err, en, "title_and_url_required"); return -1; }
  pthread_mutex_lock(&mx);
  int at = -1;
  if (id && *id) for (int i = 0; i < n; i++) if (!strcmp(items[i].id, id)) { at = i; break; }
  if (at >= 0) { snprintf(it.id, sizeof it.id, "%s", items[at].id); snprintf(it.origin, sizeof it.origin, "%s", items[at].origin); items[at] = it; }
  else if (n >= MAX_ITEMS) { pthread_mutex_unlock(&mx); snprintf(err, en, "library_full"); return -1; }
  else { snprintf(it.id, sizeof it.id, "m%08x%04x", fnv(it.url) ^ (unsigned)time(NULL), (unsigned)(rand() & 0xffff)); items[n++] = it; }
  save_locked();
  pthread_mutex_unlock(&mx);
  return 0;
}

int lib_remove(const char *id, char *url_out, size_t un) {
  if (!items) return -1;
  pthread_mutex_lock(&mx);
  int rc = -1;
  if (url_out && un) url_out[0] = 0;
  for (int i = 0; i < n; i++) if (!strcmp(items[i].id, id)) { if (url_out && !strcmp(items[i].origin, "upload")) snprintf(url_out, un, "%s", items[i].url); memmove(&items[i], &items[i + 1], (size_t)(n - i - 1) * sizeof *items); n--; rc = 0; break; }
  if (!rc) save_locked();
  pthread_mutex_unlock(&mx);
  return rc;
}

// Un gioco caricato dal telefono o dal PC (file:///data/...): cartella, pkg, zip o elf.
int lib_add_upload(const char *title, const char *url, const char *kind, const char *cover,
                   const char *title_id, const char *version, const char *platform, char *id_out, size_t idn) {
  if (!items || !title[0] || !is_local(url)) return -1;
  Item it; memset(&it, 0, sizeof it);
  snprintf(it.title, sizeof it.title, "%s", title); snprintf(it.url, sizeof it.url, "%s", url);
  snprintf(it.kind, sizeof it.kind, "%s", kind); snprintf(it.origin, sizeof it.origin, "upload");
  if (cover && is_http(cover)) snprintf(it.cover, sizeof it.cover, "%s", cover);
  if (title_id) snprintf(it.title_id, sizeof it.title_id, "%s", title_id);
  if (version) snprintf(it.version, sizeof it.version, "%s", version);
  if (platform) snprintf(it.platform, sizeof it.platform, "%s", platform);
  pthread_mutex_lock(&mx);
  if (n >= MAX_ITEMS) { pthread_mutex_unlock(&mx); return -1; }
  snprintf(it.id, sizeof it.id, "u%08x%04x", fnv(url) ^ (unsigned)time(NULL), (unsigned)(rand() & 0xffff));
  items[n++] = it;
  if (id_out) snprintf(id_out, idn, "%s", it.id);
  save_locked();
  pthread_mutex_unlock(&mx);
  return 0;
}

// Importa un JSON (array, oppure {"items"|"games"|"library":[...]}). replace_json:
// toglie prima le voci arrivate da un JSON (sincronizzazione del link).
// origin: "json" = dal link collegato (rimpiazzato a ogni sincronizzazione),
// "import" = importato una volta dal telefono (resta finché non lo togli tu)
static int import_buf(const char *buf, int replace_json, const char *origin, char *err, size_t en) {
  if (!items) { snprintf(err, en, "starting"); return -1; }
  JVal *j = json_parse(buf);
  if (!j) { snprintf(err, en, "invalid_json"); return -1; }
  JVal *arr = j->t == J_ARR ? j : jget(j, "items");
  if (!arr) arr = jget(j, "games"); if (!arr) arr = jget(j, "library");
  if (!arr || arr->t != J_ARR) { json_free(j); snprintf(err, en, "no_items"); return -1; }
  pthread_mutex_lock(&mx);
  if (replace_json) { int w = 0; for (int i = 0; i < n; i++) if (strcmp(items[i].origin, "json")) items[w++] = items[i]; n = w; }
  int added = 0, idx = 0;
  JFOR(o, arr) {
    if (n >= MAX_ITEMS) break;
    Item it; if (item_from(&it, o, origin, idx++) != 0) continue;
    int dup = -1; for (int i = 0; i < n; i++) if (!strcmp(items[i].url, it.url)) { dup = i; break; }
    if (dup >= 0) { if (!strcmp(items[dup].origin, origin)) items[dup] = it; continue; }
    items[n++] = it; added++;
  }
  save_locked();
  pthread_mutex_unlock(&mx);
  json_free(j);
  return added;
}

int lib_import(JVal *body, char *err, size_t en) {
  // {"items":[...]} o {"json":"<testo>"} dal telefono
  const char *txt = jstr(body, "json", NULL);
  if (txt && *txt) return import_buf(txt, 0, "import", err, en);
  JVal *arr = jget(body, "items");
  if (!arr) { snprintf(err, en, "no_items"); return -1; }
  // si riserializza in un array per riusare lo stesso percorso
  size_t cap = 4 * 1024 * 1024; char *b = malloc(cap); if (!b) { snprintf(err, en, "memory"); return -1; }
  size_t at = 0; b[at++] = '[';
  int k = 0;
  JFOR(o, arr) {
    Item it; if (item_from(&it, o, "json", k) != 0) continue;
    if (at + 3000 > cap) break;
    if (k++) b[at++] = ',';
    b[at++] = '{';
    at = put(b, cap, at, "title", it.title, 0); at = put(b, cap, at, "url", it.url, 1); at = put(b, cap, at, "cover", it.cover, 1);
    at = put(b, cap, at, "platform", it.platform, 1); at = put(b, cap, at, "version", it.version, 1); at = put(b, cap, at, "title_id", it.title_id, 1);
    at = put(b, cap, at, "kind", it.kind, 1); at = put(b, cap, at, "description", it.desc, 1);
    b[at++] = '}';
  }
  b[at++] = ']'; b[at] = 0;
  int r = import_buf(b, 0, "import", err, en);
  free(b);
  return r;
}

// Collega (o scollega, url vuoto) il link a un JSON e lo sincronizza subito.
int lib_set_source(const char *url, char *err, size_t en) {
  if (!items) { snprintf(err, en, "starting"); return -1; }
  pthread_mutex_lock(&mx);
  snprintf(src_url, sizeof src_url, "%s", url ? url : "");
  if (!src_url[0]) { int w = 0; for (int i = 0; i < n; i++) if (strcmp(items[i].origin, "json")) items[w++] = items[i]; n = w; src_status[0] = 0; src_sync = 0; }
  save_locked();
  pthread_mutex_unlock(&mx);
  return src_url[0] ? lib_sync(err, en) : 0;
}

int lib_sync(char *err, size_t en) {
  char url[1024];
  pthread_mutex_lock(&mx); snprintf(url, sizeof url, "%s", src_url); pthread_mutex_unlock(&mx);
  if (!url[0]) { snprintf(err, en, "no_source"); return -1; }
  if (!is_http(url)) { snprintf(err, en, "invalid_url"); return -1; }
  if (!fetch_fn) { snprintf(err, en, "no_network"); return -1; }
  char *buf = malloc(MAX_JSON + 1); if (!buf) { snprintf(err, en, "memory"); return -1; }
  long got = fetch_fn(url, buf, MAX_JSON);
  int r;
  if (got <= 0) { snprintf(err, en, "fetch_failed"); r = -1; }
  else { buf[got] = 0; r = import_buf(buf, 1, "json", err, en); }
  free(buf);
  pthread_mutex_lock(&mx);
  src_sync = (long)time(NULL);
  snprintf(src_status, sizeof src_status, "%s", r >= 0 ? "ok" : err);
  save_locked();
  pthread_mutex_unlock(&mx);
  player_log("libreria: sincronizzazione %s -> %d", url, r);
  return r;
}
