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
#include <unistd.h>

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
static int has_any(JVal *o, const char *const *keys) {
  for (int i = 0; keys[i]; i++) if (jget(o, keys[i])) return 1;
  return 0;
}

// "45 GB", "1.2GB", "700 MB", "123456" o un numero → byte
static double size_of(JVal *o) {
  static const char *const K[] = { "size", "size_bytes", "filesize", "file_size", "pkg_size", "Size", NULL };
  for (int i = 0; K[i]; i++) {
    JVal *v = jget(o, K[i]); if (!v) continue;
    if (v->t == J_NUM) return v->n > 0 ? v->n : 0;
    if (v->t == J_STR && v->s) {
      char *end; double x = strtod(v->s, &end);
      if (x <= 0) continue;
      while (*end == ' ') end++;
      char u = (char)toupper((unsigned char)*end);
      return u == 'T' ? x * 1e12 : u == 'G' ? x * 1e9 : u == 'M' ? x * 1e6 : u == 'K' ? x * 1e3 : x;
    }
  }
  return 0;
}

// CUSA12345, anche dentro un Content ID (UP0001-CUSA12345_00-NOMEGIOCO0000000)
static int tid_ok(const char *t) {
  for (int i = 0; i < 9; i++) if (!(i < 4 ? isalpha((unsigned char)t[i]) : isdigit((unsigned char)t[i]))) return 0;
  return 1;
}
static void tid_from(JVal *o, char *out, size_t on) {
  static const char *const K_TID[] = { "title_id", "titleId", "titleID", "TITLE_ID", "tid", "TitleId", NULL };
  static const char *const K_CID[] = { "content_id", "contentId", "contentID", "CONTENT_ID", "cid", NULL };
  out[0] = 0;
  const char *t = first(o, K_TID);
  if (strlen(t) >= 9 && tid_ok(t)) { snprintf(out, on, "%.9s", t); }
  else {
    const char *c = first(o, K_CID);
    if (strlen(c) >= 16 && c[6] == '-' && tid_ok(c + 7)) snprintf(out, on, "%.9s", c + 7);
  }
  for (char *p = out; *p; p++) *p = (char)toupper((unsigned char)*p);
}

// un elemento da JSON (manuale o importato); 0 = valido. I nomi dei campi sono
// quelli dei formati che girano nella scena: il nostro, FPKGi, DPI di etaHEN,
// le liste di pkg (pkg_url, icon_url, content_id...).
static const char *const K_TITLE[] = { "title", "name", "content_name", "titleName", "title_name", "game", "Name", "Title", NULL };
static const char *const K_URL[] = { "url", "pkg_url", "download_url", "downloadUrl", "download", "download_link", "file", "pkg", "link", "uri", "PKG_URL", "Url", NULL };
static const char *const K_COVER[] = { "cover", "cover_url", "coverUrl", "icon_url", "iconUrl", "image", "image_url", "icon", "icon0", "thumbnail", "art", NULL };
static const char *const K_DESC[] = { "description", "desc", "text", "notes", NULL };
static const char *const K_VER[] = { "version", "ver", "app_ver", "Version", NULL };
static const char *const K_PLAT[] = { "platform", "plat", "system", "console", NULL };
static const char *const K_KIND[] = { "type", "kind", "file_kind", NULL };
static const char *const K_SIZE[] = { "size", "size_bytes", "filesize", "file_size", "pkg_size", "Size", NULL };
static const char *const K_SHOTS[] = { "screenshots", "images", NULL };
static const char *const K_TIDS[] = { "title_id", "titleId", "titleID", "TITLE_ID", "tid", "TitleId", "content_id", "contentId", "contentID", "CONTENT_ID", "cid", NULL };

static int item_from(Item *it, JVal *o, const char *origin, int idx) {
  memset(it, 0, sizeof *it);
  snprintf(it->title, sizeof it->title, "%s", first(o, K_TITLE));
  snprintf(it->url, sizeof it->url, "%s", first(o, K_URL));
  if (!it->title[0] || !(is_http(it->url) || (!strcmp(origin, "upload") && is_local(it->url)))) return -1;
  const char *cv = first(o, K_COVER); if (is_http(cv)) snprintf(it->cover, sizeof it->cover, "%s", cv);
  snprintf(it->desc, sizeof it->desc, "%s", first(o, K_DESC));
  snprintf(it->version, sizeof it->version, "%s", first(o, K_VER));
  tid_from(o, it->title_id, sizeof it->title_id);
  char pl[8]; snprintf(pl, sizeof pl, "%s", first(o, K_PLAT)); for (char *c = pl; *c; c++) *c = (char)toupper((unsigned char)*c);
  if (!strcmp(pl, "PS4") || !strcmp(pl, "PS5")) snprintf(it->platform, sizeof it->platform, "%s", pl);
  else if (it->title_id[0]) snprintf(it->platform, sizeof it->platform, "%s", !strncmp(it->title_id, "PPSA", 4) ? "PS5" : "PS4");   // PPSA = PS5, il resto (CUSA...) = PS4
  kind_of(first(o, K_KIND), it->url, it->kind, sizeof it->kind);
  it->size = size_of(o);
  JVal *sh = jget(o, "screenshots"); if (!sh) sh = jget(o, "images");
  JFOR(s, sh) { if (it->nshots >= 4) break; if (s->s && is_http(s->s)) snprintf(it->shots[it->nshots++], 512, "%s", s->s); }
  snprintf(it->origin, sizeof it->origin, "%s", origin);
  const char *ext = jstr(o, "id", NULL);
  if (ext && *ext) snprintf(it->id, sizeof it->id, "%c%.30s", origin[0], ext);
  else snprintf(it->id, sizeof it->id, "%c%08x", origin[0], fnv(it->url));   // stabile: non cambia se la lista cambia ordine
  (void)idx;
  return 0;
}

// come item_from, ma con l'URL dato da fuori (FPKGi lo mette nella chiave)
static int item_with_url(Item *it, JVal *o, const char *url, const char *origin) {
  if (item_from(it, o, origin, 0) == 0) return 0;                 // c'era anche un url dentro
  memset(it, 0, sizeof *it);
  snprintf(it->title, sizeof it->title, "%s", first(o, K_TITLE));
  snprintf(it->url, sizeof it->url, "%s", url);
  if (!it->title[0] || !is_http(it->url)) return -1;
  const char *cv = first(o, K_COVER); if (is_http(cv)) snprintf(it->cover, sizeof it->cover, "%s", cv);
  snprintf(it->desc, sizeof it->desc, "%s", first(o, K_DESC));
  snprintf(it->version, sizeof it->version, "%s", first(o, K_VER));
  tid_from(o, it->title_id, sizeof it->title_id);
  if (it->title_id[0]) snprintf(it->platform, sizeof it->platform, "%s", !strncmp(it->title_id, "PPSA", 4) ? "PS5" : "PS4");
  kind_of(first(o, K_KIND), it->url, it->kind, sizeof it->kind);
  it->size = size_of(o);
  snprintf(it->origin, sizeof it->origin, "%s", origin);
  snprintf(it->id, sizeof it->id, "%c%08x", origin[0], fnv(it->url));
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
  fputs("]}\n", f);
  // su disco prima del rename: una console spenta a metà non lascia la libreria vuota
  int ok = fflush(f) == 0 && fsync(fileno(f)) == 0;
  if (fclose(f) != 0) ok = 0;
  if (ok) rename(tmp, path); else unlink(tmp);
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

static size_t item_json(char *o, size_t cap, size_t at, const Item *it, int lite) {
  o[at++] = '{';
  at = put(o, cap, at, "id", it->id, 0); at = put(o, cap, at, "title", it->title, 1); at = put(o, cap, at, "url", it->url, 1);
  at = put(o, cap, at, "cover", it->cover, 1); at = put(o, cap, at, "platform", it->platform, 1); at = put(o, cap, at, "version", it->version, 1);
  at = put(o, cap, at, "title_id", it->title_id, 1); at = put(o, cap, at, "kind", it->kind, 1); at = put(o, cap, at, "origin", it->origin, 1);
  if (!lite) at = put(o, cap, at, "description", it->desc, 1);
  at += (size_t)snprintf(o + at, cap - at, ",\"size\":%.0f", it->size);
  if (!lite) {
    at += (size_t)snprintf(o + at, cap - at, ",\"screenshots\":[");
    for (int k = 0; k < it->nshots; k++) at = put(o, cap, at, NULL, it->shots[k], k > 0);
    at += (size_t)snprintf(o + at, cap - at, "]");
  }
  at += (size_t)snprintf(o + at, cap - at, "}");
  return at;
}

// Un gioco solo, completo (la UI chiede l'elenco leggero e la scheda quando la apri).
size_t lib_item_json(const char *id, char *o, size_t cap) {
  pthread_mutex_lock(&mx);
  size_t at = 0;
  for (int i = 0; i < n; i++) if (!strcmp(items[i].id, id)) { at = item_json(o, cap, 0, &items[i], 0); break; }
  pthread_mutex_unlock(&mx);
  if (at < cap) o[at] = 0;
  return at;
}

size_t lib_list_json_lite(char *o, size_t cap, int lite);
size_t lib_list_json(char *o, size_t cap) { return lib_list_json_lite(o, cap, 0); }
size_t lib_list_json_lite(char *o, size_t cap, int lite) {
  pthread_mutex_lock(&mx);
  size_t at = (size_t)snprintf(o, cap, "{\"seq\":%u,\"source\":{", seq);
  at = put(o, cap, at, "url", src_url, 0);
  at += (size_t)snprintf(o + at, cap - at, ",\"last_sync\":%ld", src_sync);
  at = put(o, cap, at, "status", src_status, 1);
  at += (size_t)snprintf(o + at, cap - at, "},\"items\":[");
  for (int i = 0; i < n && at + 8192 < cap; i++) {
    o[at++] = i ? ',' : ' ';
    at = item_json(o, cap, at, &items[i], lite);
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
  if (at >= 0) {
    // modifica: i campi che la richiesta non manda restano com'erano (la UI ne
    // manda solo alcuni, e prima descrizione, dimensione e screenshot sparivano)
    Item *old = &items[at];
    if (!has_any(o, K_COVER)) snprintf(it.cover, sizeof it.cover, "%s", old->cover);
    if (!has_any(o, K_DESC)) snprintf(it.desc, sizeof it.desc, "%s", old->desc);
    if (!has_any(o, K_VER)) snprintf(it.version, sizeof it.version, "%s", old->version);
    if (!has_any(o, K_TIDS)) snprintf(it.title_id, sizeof it.title_id, "%s", old->title_id);
    if (!has_any(o, K_PLAT) && !it.platform[0]) snprintf(it.platform, sizeof it.platform, "%s", old->platform);
    if (!has_any(o, K_KIND) && !strcmp(it.url, old->url)) snprintf(it.kind, sizeof it.kind, "%s", old->kind);
    if (!has_any(o, K_SIZE)) it.size = old->size;
    if (!has_any(o, K_SHOTS)) { it.nshots = old->nshots; memcpy(it.shots, old->shots, sizeof it.shots); }
    snprintf(it.id, sizeof it.id, "%s", old->id); snprintf(it.origin, sizeof it.origin, "%s", old->origin);
    items[at] = it;
  }
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

// Esito dell'ultima importazione, per la risposta (ctl.c) e lo stato del link.
static int last_added, last_updated, last_skipped;
void lib_last_import(int *added, int *updated, int *skipped) {
  pthread_mutex_lock(&mx); *added = last_added; *updated = last_updated; *skipped = last_skipped; pthread_mutex_unlock(&mx);
}

// L'elenco dei giochi dentro un JSON qualsiasi: un array; un oggetto con
// items/games/library/packages/pkgs/list/apps/content/data; FPKGi, cioè
// {"DATA":{"<url>":{...}}} con l'URL come chiave; un oggetto solo.
static JVal *list_of(JVal *j, int *keyed) {
  static const char *const K[] = { "items", "games", "library", "packages", "pkgs", "list", "apps", "content", "entries", "DATA", "data", "Data", NULL };
  *keyed = 0;
  if (!j) return NULL;
  if (j->t == J_ARR) return j;
  if (j->t != J_OBJ) return NULL;
  for (int i = 0; K[i]; i++) {
    JVal *v = jget(j, K[i]);
    if (v && v->t == J_ARR) return v;
    if (v && v->t == J_OBJ && v->child && v->child->t == J_OBJ) { *keyed = 1; return v; }   // FPKGi
  }
  if (*first(j, K_URL) || *first(j, K_TITLE)) return j;   // un gioco solo
  return NULL;
}

// Importa un JSON. replace_json: toglie prima le voci arrivate dal link
// (sincronizzazione), ma solo se il JSON nuovo ha almeno un gioco valido: un
// link che oggi risponde male non deve svuotare la libreria.
// origin: "json" = dal link collegato (rimpiazzato a ogni sincronizzazione),
// "import" = importato una volta dal telefono (resta finché non lo togli tu)
static int import_val(JVal *j, int replace_json, const char *origin, char *err, size_t en);
static int import_buf(const char *buf, int replace_json, const char *origin, char *err, size_t en) {
  if (!items) { snprintf(err, en, "starting"); return -1; }
  if (!strncmp(buf, "\xEF\xBB\xBF", 3)) buf += 3;              // BOM UTF-8 (file salvati da Windows)
  while (*buf == ' ' || *buf == '\n' || *buf == '\r' || *buf == '\t') buf++;
  if (*buf == '<') { snprintf(err, en, "html_not_json"); return -1; }   // pagina web al posto del file (Drive, GitHub "blob"...)
  JVal *j = json_parse(buf);
  if (!j) { snprintf(err, en, "invalid_json"); return -1; }
  int r = import_val(j, replace_json, origin, err, en);
  json_free(j);
  return r;
}
static int import_val(JVal *j, int replace_json, const char *origin, char *err, size_t en) {
  if (!items) { snprintf(err, en, "starting"); return -1; }
  int keyed = 0;
  JVal *arr = list_of(j, &keyed);
  if (!arr) { snprintf(err, en, "no_items"); return -1; }
  // prima si leggono tutte le voci, poi si tocca la libreria
  int cap = MAX_ITEMS, nv = 0, skipped = 0, idx = 0;
  Item *val = calloc((size_t)cap, sizeof *val);
  if (!val) { snprintf(err, en, "memory"); return -1; }
  if (arr == j && j->t == J_OBJ) { if (item_from(&val[0], j, origin, 0) == 0) nv = 1; else skipped = 1; }
  else JFOR(o, arr) {
    if (nv >= cap) { skipped++; continue; }
    Item *it = &val[nv];
    int ok;
    if (keyed && o->key && is_http(o->key) && o->t == J_OBJ && !*first(o, K_URL)) {
      // FPKGi: l'URL è la chiave dell'oggetto, i dati sono dentro
      ok = item_with_url(it, o, o->key, origin) == 0;
    } else ok = item_from(it, o, origin, idx) == 0;
    idx++;
    if (ok) nv++; else skipped++;
  }
  if (!nv) { free(val); snprintf(err, en, skipped ? "no_valid_items" : "no_items"); pthread_mutex_lock(&mx); last_added = last_updated = 0; last_skipped = skipped; pthread_mutex_unlock(&mx); return -1; }
  pthread_mutex_lock(&mx);
  if (replace_json) { int w = 0; for (int i = 0; i < n; i++) if (strcmp(items[i].origin, "json")) items[w++] = items[i]; n = w; }
  int added = 0, updated = 0;
  for (int v = 0; v < nv; v++) {
    Item *it = &val[v];
    int dup = -1; for (int i = 0; i < n; i++) if (!strcmp(items[i].url, it->url)) { dup = i; break; }
    if (dup >= 0) { if (!strcmp(items[dup].origin, origin)) { items[dup] = *it; updated++; } else skipped++; continue; }
    if (n >= MAX_ITEMS) { skipped++; continue; }
    items[n++] = *it; added++;
  }
  last_added = added; last_updated = updated; last_skipped = skipped;
  save_locked();
  pthread_mutex_unlock(&mx);
  free(val);
  player_log("libreria: importati %d nuovi, %d aggiornati, %d scartati", added, updated, skipped);
  return added;
}

int lib_import(JVal *body, char *err, size_t en) {
  // {"json":"<testo>"} dal telefono, oppure il JSON stesso ({"items":[...]}, FPKGi...)
  const char *txt = jstr(body, "json", NULL);
  if (txt && *txt) return import_buf(txt, 0, "import", err, en);
  return import_val(body, 0, "import", err, en);
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

// Link "da browser" che non portano al file: GitHub blob, Dropbox, Google Drive.
static void direct_link(const char *u, char *out, size_t on) {
  const char *p;
  if ((p = strstr(u, "://github.com/")) && strstr(u, "/blob/")) {
    // https://github.com/<utente>/<repo>/blob/<ramo>/<file> → raw.githubusercontent.com/<utente>/<repo>/<ramo>/<file>
    const char *rest = p + 14, *blob = strstr(rest, "/blob/");
    snprintf(out, on, "https://raw.githubusercontent.com/%.*s/%s", (int)(blob - rest), rest, blob + 6);
    return;
  }
  if (strstr(u, "dropbox.com/") && (p = strstr(u, "dl=0"))) { snprintf(out, on, "%.*sdl=1%s", (int)(p - u), u, p + 4); return; }
  if ((p = strstr(u, "drive.google.com/file/d/"))) {
    const char *id = p + 24; size_t L = strcspn(id, "/?");
    snprintf(out, on, "https://drive.google.com/uc?export=download&id=%.*s", (int)L, id);
    return;
  }
  snprintf(out, on, "%s", u);
}

int lib_sync(char *err, size_t en) {
  char url[1024];
  pthread_mutex_lock(&mx); snprintf(url, sizeof url, "%s", src_url); pthread_mutex_unlock(&mx);
  if (!url[0]) { snprintf(err, en, "no_source"); return -1; }
  if (!is_http(url)) { snprintf(err, en, "invalid_url"); return -1; }
  if (!fetch_fn) { snprintf(err, en, "no_network"); return -1; }
  char fetch_url[1100]; direct_link(url, fetch_url, sizeof fetch_url);
  char *buf = malloc(MAX_JSON + 1); if (!buf) { snprintf(err, en, "memory"); return -1; }
  long got = fetch_fn(fetch_url, buf, MAX_JSON);
  int r;
  if (got <= 0) { snprintf(err, en, got == -2 ? "too_large" : "fetch_failed"); r = -1; }
  else { buf[got] = 0; r = import_buf(buf, 1, "json", err, en); }
  free(buf);
  pthread_mutex_lock(&mx);
  src_sync = (long)time(NULL);
  // "ok 12 3" = giochi dal link e scartati; altrimenti il codice d'errore
  if (r >= 0) { int tot = 0; for (int i = 0; i < n; i++) tot += !strcmp(items[i].origin, "json"); snprintf(src_status, sizeof src_status, "ok %d %d", tot, last_skipped); }
  else snprintf(src_status, sizeof src_status, "%s", err);
  save_locked();
  pthread_mutex_unlock(&mx);
  player_log("libreria: sincronizzazione %s -> %d", url, r);
  return r;
}
