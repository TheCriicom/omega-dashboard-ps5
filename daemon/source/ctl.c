// Omega — server di controllo del demone (HTTP/1.1 minimo, una richiesta per
// connessione). La UI lo usa da 127.0.0.1 senza credenziali; dalla rete di casa
// (telecomando sul telefono) serve il token che si ottiene col PIN mostrato
// nella UI. Cinque PIN sbagliati bloccano l'abbinamento per un minuto.
//
//   GET  /                    pagina del telecomando (remote/remote.html)
//   POST /v1/pair {"pin":"123456"}  → {"token":"..."}
//   POST /v1/pair/reset       nuovo PIN e nuovo token (solo da 127.0.0.1)
//   GET  /v1/remote           PIN e stato del telecomando (solo da 127.0.0.1)
//   GET  /v1/system           gioco in primo piano, temperatura, ventola
//   GET  /v1/favorites        radio preferite salvate dalla UI
//   GET  /v1/library          "La mia libreria" (lib.c)
//   POST /v1/library/item     aggiunge o modifica un gioco {title,url,cover,...,id?}
//   POST /v1/library/remove   {"id":"..."}
//   POST /v1/library/import   {"items":[...]} o {"json":"<testo>"}
//   POST /v1/library/source   {"url":"..."} collega un JSON (vuoto = scollega) e sincronizza
//   POST /v1/library/sync     risincronizza il JSON collegato (in background)
//   POST /v1/music/upload?name=<file>   corpo = il file audio → OMEGA_DIR/Music
//   GET  /v1/music/files      file in OMEGA_DIR/Music
//   POST /v1/music/delete     {"name":"..."}
//   GET  /v1/state            stato del lettore (JSON)
//   GET  /v1/queue            coda
//   GET  /v1/cover            copertina incorporata nel brano (404 se non c'è)
//   POST /v1/queue  {"mode":"replace|append|next","start":0,"play":true,"items":[{url,title,...}]}
//   POST /v1/cmd    {"cmd":"play|pause|toggle|next|prev|stop|seek|volume|shuffle|repeat|jump|remove|clear","value":n}
#include "ctl.h"
#include "json.h"
#include "player.h"
#include "lib.h"
#include <arpa/inet.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <time.h>
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#define MAX_BODY (4 * 1024 * 1024)

void player_log(const char *fmt, ...);
extern const char REMOTE_HTML[]; extern const size_t REMOTE_HTML_LEN;

static char data_dir[200];
static char pin[8], secret[40];
static int fails; static time_t locked_until;
static ctl_system_fn system_fn;

static void rnd_bytes(unsigned char *b, size_t n) {
  int fd = open("/dev/urandom", O_RDONLY);
  size_t got = 0;
  if (fd >= 0) { ssize_t k; while (got < n && (k = read(fd, b + got, n - got)) > 0) got += (size_t)k; close(fd); }
  static unsigned x; if (!x) x = (unsigned)time(NULL) ^ (unsigned)getpid() * 2654435761u;
  for (; got < n; got++) { x ^= x << 13; x ^= x >> 17; x ^= x << 5; b[got] = (unsigned char)x; }
}
static void remote_new(void) {
  unsigned char r[20]; rnd_bytes(r, sizeof r);
  snprintf(pin, sizeof pin, "%06u", (unsigned)((r[0] << 16 | r[1] << 8 | r[2]) % 1000000));
  for (int i = 0; i < 16; i++) snprintf(secret + i * 2, 3, "%02x", r[4 + i]);
  char p[260]; snprintf(p, sizeof p, "%s/remote.json", data_dir);
  FILE *f = fopen(p, "w"); if (f) { fprintf(f, "{\"pin\":\"%s\",\"token\":\"%s\"}\n", pin, secret); fclose(f); }
  fails = 0;
}
static void remote_load(void) {
  char p[260]; snprintf(p, sizeof p, "%s/remote.json", data_dir);
  FILE *f = fopen(p, "r"); char b[256] = "";
  if (f) { size_t n = fread(b, 1, sizeof b - 1, f); b[n] = 0; fclose(f); }
  JVal *j = b[0] ? json_parse(b) : NULL;
  jcpy(pin, sizeof pin, j, "pin"); jcpy(secret, sizeof secret, j, "token");
  json_free(j);
  if (strlen(pin) != 6 || strlen(secret) != 32) remote_new();
}
static int token_ok(const char *t) {
  if (!t || strlen(t) != 32 || !secret[0]) return 0;
  int d = 0; for (int i = 0; i < 32; i++) d |= t[i] ^ secret[i];   // confronto a tempo costante
  return d == 0;
}
void ctl_on_system(ctl_system_fn fn) { system_fn = fn; }

static void send_all(int s, const void *p, size_t n) {
  const char *c = p;
  while (n) { ssize_t k = write(s, c, n); if (k <= 0) return; c += k; n -= (size_t)k; }
}

static void reply(int s, int code, const char *type, const void *body, size_t len) {
  const char *msg = code == 200 ? "OK" : code == 400 ? "Bad Request" : code == 404 ? "Not Found" : code == 413 ? "Payload Too Large" : code == 202 ? "Accepted" : code == 401 ? "Unauthorized" : code == 403 ? "Forbidden" : code == 429 ? "Too Many Requests" : "Error";
  char h[256];
  int n = snprintf(h, sizeof h, "HTTP/1.1 %d %s\r\nContent-Type: %s\r\nContent-Length: %zu\r\nCache-Control: no-store\r\nConnection: close\r\n\r\n", code, msg, type, len);
  send_all(s, h, (size_t)n);
  if (len) send_all(s, body, len);
}
static void reply_json(int s, int code, const char *js) { reply(s, code, "application/json", js, strlen(js)); }

static int cmd_is(const char *a, const char *b) { return a && !strcmp(a, b); }

static void do_cmd(int s, JVal *j) {
  const char *c = jstr(j, "cmd", "");
  double v = jnum(j, "value", -1);
  if (cmd_is(c, "play")) player_play();
  else if (cmd_is(c, "pause")) player_pause();
  else if (cmd_is(c, "toggle")) player_toggle();
  else if (cmd_is(c, "next")) player_next();
  else if (cmd_is(c, "prev")) player_prev();
  else if (cmd_is(c, "stop")) player_stop();
  else if (cmd_is(c, "seek")) player_seek(v);
  else if (cmd_is(c, "volume")) player_volume((int)v);
  else if (cmd_is(c, "shuffle")) player_shuffle((int)v);
  else if (cmd_is(c, "repeat")) player_repeat((int)v);
  else if (cmd_is(c, "jump")) player_jump((int)v);
  else if (cmd_is(c, "remove")) player_remove((int)v);
  else if (cmd_is(c, "clear")) player_clear();
  else { reply_json(s, 400, "{\"error\":\"unknown_cmd\"}"); return; }
  static char st[8192]; player_state_json(st, sizeof st);
  reply_json(s, 200, st);
}

static void do_queue(int s, JVal *j) {
  JVal *arr = jget(j, "items");
  int n = jlen(arr);
  if (n <= 0 || n > PLAYER_MAX_ITEMS) { reply_json(s, 400, "{\"error\":\"items\"}"); return; }
  PlayerItem *its = calloc((size_t)n, sizeof *its);
  if (!its) { reply_json(s, 500, "{\"error\":\"memory\"}"); return; }
  int k = 0;
  JFOR(o, arr) {
    PlayerItem *it = &its[k];
    jcpy(it->url, sizeof it->url, o, "url");
    if (!it->url[0]) continue;
    jcpy(it->title, sizeof it->title, o, "title");
    jcpy(it->artist, sizeof it->artist, o, "artist");
    jcpy(it->album, sizeof it->album, o, "album");
    jcpy(it->cover, sizeof it->cover, o, "cover");
    jcpy(it->source, sizeof it->source, o, "source");
    jcpy(it->id, sizeof it->id, o, "id");
    it->dur = jnum(o, "dur", 0);
    k++;
  }
  const char *mode = jstr(j, "mode", "replace");
  int m = !strcmp(mode, "append") ? 1 : !strcmp(mode, "next") ? 2 : 0;
  JVal *pl = jget(j, "play");
  int play = pl ? jbool(j, "play") : 1;
  player_enqueue(its, k, m, (int)jnum(j, "start", 0), play);
  free(its);
  static char st[8192]; player_state_json(st, sizeof st);
  reply_json(s, 200, st);
}

// ------------------------------------------------- musica dal telefono --
#define MAX_UPLOAD (600LL * 1024 * 1024)
static int audio_name(const char *n) {
  static const char *ok[] = { ".mp3", ".flac", ".ogg", ".oga", ".opus", ".m4a", ".aac", ".wav", ".wma", ".ape", ".wv", ".aiff", ".aif", ".mka", NULL };
  size_t L = strlen(n);
  for (int i = 0; ok[i]; i++) { size_t k = strlen(ok[i]); if (L > k && !strcasecmp(n + L - k, ok[i])) return 1; }
  return 0;
}
// solo il nome, senza cartelle né caratteri strani
static int clean_name(const char *in, char *out, size_t n) {
  const char *b = strrchr(in, '/'); b = b ? b + 1 : in;
  size_t o = 0;
  for (; *b && o + 1 < n; b++) { unsigned char c = (unsigned char)*b; out[o++] = (c < 0x20 || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|') ? '_' : (char)c; }
  out[o] = 0;
  return o > 0 && out[0] != '.' && audio_name(out);
}
static void url_decode(char *s) {
  char *o = s;
  for (; *s; s++) {
    if (*s == '%' && s[1] && s[2]) { char h[3] = { s[1], s[2], 0 }; *o++ = (char)strtol(h, NULL, 16); s += 2; }
    else *o++ = *s == '+' ? ' ' : *s;
  }
  *o = 0;
}
static void music_dir(char *out, size_t n) { snprintf(out, n, "%s/Music", data_dir); mkdir(out, 0777); }

static void do_upload(int s, const char *qs, const char *body_start, size_t have, long long clen) {
  char name[256] = "", raw[512] = "";
  const char *p = qs ? strstr(qs, "name=") : NULL;
  if (p) { snprintf(raw, sizeof raw, "%.*s", (int)strcspn(p + 5, "&"), p + 5); url_decode(raw); }
  if (!clean_name(raw, name, sizeof name)) { reply_json(s, 400, "{\"error\":\"bad_name\"}"); return; }
  if (clen <= 0 || clen > MAX_UPLOAD) { reply_json(s, 413, "{\"error\":\"too_large\"}"); return; }
  char dir[300], dst[600], tmp[620];
  music_dir(dir, sizeof dir);
  snprintf(dst, sizeof dst, "%s/%s", dir, name); snprintf(tmp, sizeof tmp, "%s.part", dst);
  int fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC, 0666);
  if (fd < 0) { reply_json(s, 500, "{\"error\":\"write\"}"); return; }
  long long got = 0; int ok = 1;
  if (have) { if (write(fd, body_start, have) != (ssize_t)have) ok = 0; got = (long long)have; }
  static char buf[256 * 1024];
  while (ok && got < clen) {
    ssize_t k = read(s, buf, (size_t)(clen - got < (long long)sizeof buf ? clen - got : (long long)sizeof buf));
    if (k <= 0) { ok = 0; break; }
    if (write(fd, buf, (size_t)k) != k) { ok = 0; break; }
    got += k;
  }
  close(fd);
  if (!ok || got != clen) { unlink(tmp); reply_json(s, 400, "{\"error\":\"incomplete\"}"); return; }
  rename(tmp, dst);
  player_log("musica: caricato %s (%lld byte)", name, got);
  reply_json(s, 200, "{\"ok\":true}");
}

static void music_files(int s) {
  char dir[300]; music_dir(dir, sizeof dir);
  size_t cap = 512 * 1024, at = 0; char *o = malloc(cap);
  if (!o) { reply_json(s, 500, "{}"); return; }
  at += (size_t)snprintf(o, cap, "{\"files\":[");
  DIR *d = opendir(dir); int k = 0;
  struct dirent *e;
  while (d && (e = readdir(d)) && at + 600 < cap) {
    if (e->d_name[0] == '.' || !audio_name(e->d_name)) continue;
    char full[600]; snprintf(full, sizeof full, "%s/%s", dir, e->d_name);
    struct stat st; if (stat(full, &st) != 0) continue;
    char esc[600]; json_escape(esc, sizeof esc, e->d_name);
    at += (size_t)snprintf(o + at, cap - at, "%s{\"name\":\"%s\",\"size\":%lld}", k++ ? "," : "", esc, (long long)st.st_size);
  }
  if (d) closedir(d);
  snprintf(o + at, cap - at, "],\"dir\":\"%s\"}", dir);
  reply_json(s, 200, o); free(o);
}

// la sincronizzazione scarica un file: va in un thread, il controllo resta libero
static volatile int syncing;
static void *sync_thread(void *arg) {
  char *url = arg, err[64] = "";
  if (url) { lib_set_source(url, err, sizeof err); free(url); } else lib_sync(err, sizeof err);
  syncing = 0;
  return NULL;
}
static void start_sync(const char *url) {
  if (syncing) return;
  syncing = 1;
  pthread_t t;
  if (pthread_create(&t, NULL, sync_thread, url ? strdup(url) : NULL) == 0) pthread_detach(t); else syncing = 0;
}

static void do_library(int s, const char *what, JVal *j) {
  char err[64] = "", b[160];
  if (!strcmp(what, "item")) {
    if (lib_add(j, err, sizeof err) == 0) reply_json(s, 200, "{\"ok\":true}");
    else { snprintf(b, sizeof b, "{\"error\":\"%s\"}", err); reply_json(s, 400, b); }
  } else if (!strcmp(what, "remove")) {
    reply_json(s, lib_remove(jstr(j, "id", "")) == 0 ? 200 : 404, "{}");
  } else if (!strcmp(what, "import")) {
    int r = lib_import(j, err, sizeof err);
    if (r >= 0) { snprintf(b, sizeof b, "{\"added\":%d}", r); reply_json(s, 200, b); }
    else { snprintf(b, sizeof b, "{\"error\":\"%s\"}", err); reply_json(s, 400, b); }
  } else if (!strcmp(what, "source")) {
    const char *u = jstr(j, "url", "");
    if (*u && strncmp(u, "http://", 7) && strncmp(u, "https://", 8)) { reply_json(s, 400, "{\"error\":\"invalid_url\"}"); return; }
    if (*u) { start_sync(u); reply_json(s, 202, "{\"syncing\":true}"); }
    else { lib_set_source("", err, sizeof err); reply_json(s, 200, "{\"ok\":true}"); }
  } else if (!strcmp(what, "sync")) {
    start_sync(NULL); reply_json(s, 202, "{\"syncing\":true}");
  } else reply_json(s, 404, "{\"error\":\"not_found\"}");
}

static void handle(int s, int local) {
  // intestazioni
  static char hdr[8192]; size_t hl = 0; char *end = NULL;
  while (hl + 1 < sizeof hdr) {
    ssize_t k = read(s, hdr + hl, sizeof hdr - 1 - hl);
    if (k <= 0) return;
    hl += (size_t)k; hdr[hl] = 0;
    if ((end = strstr(hdr, "\r\n\r\n"))) break;
  }
  if (!end) { reply_json(s, 400, "{\"error\":\"header\"}"); return; }
  char method[8] = "", path[256] = "";
  sscanf(hdr, "%7s %255s", method, path);
  size_t clen = 0;
  for (char *l = strstr(hdr, "\r\n"); l && l < end; l = strstr(l + 2, "\r\n"))
    if (!strncasecmp(l + 2, "Content-Length:", 15)) clen = (size_t)strtoul(l + 17, NULL, 10);
  long long clen_ll = 0;
  for (char *l = strstr(hdr, "\r\n"); l && l < end; l = strstr(l + 2, "\r\n"))
    if (!strncasecmp(l + 2, "Content-Length:", 15)) clen_ll = strtoll(l + 17, NULL, 10);
  // token: intestazione x-omega-token o parametro k (le immagini non mandano intestazioni)
  char tok[64] = "";
  for (char *l = strstr(hdr, "\r\n"); l && l < end; l = strstr(l + 2, "\r\n"))
    if (!strncasecmp(l + 2, "x-omega-token:", 14)) { const char *v = l + 16; while (*v == ' ') v++; snprintf(tok, sizeof tok, "%.*s", (int)strcspn(v, "\r\n "), v); }
  char *qs = strchr(path, '?'); char qcopy[600] = "";
  if (qs) { snprintf(qcopy, sizeof qcopy, "%s", qs); char *k = strstr(qs, "k="); if (k && !tok[0]) snprintf(tok, sizeof tok, "%.*s", (int)strcspn(k + 2, "&"), k + 2); *qs = 0; }

  // la pagina del telecomando e l'abbinamento sono aperti; il resto dalla rete vuole il token
  if (!strcmp(method, "GET") && (!strcmp(path, "/") || !strcmp(path, "/index.html"))) {
    char h[200]; int n = snprintf(h, sizeof h, "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\nContent-Length: %zu\r\nCache-Control: no-store\r\nConnection: close\r\n\r\n", REMOTE_HTML_LEN);
    send_all(s, h, (size_t)n); send_all(s, REMOTE_HTML, REMOTE_HTML_LEN); return;
  }
  int is_pair = !strcmp(path, "/v1/pair");
  if (!local && !is_pair && !token_ok(tok)) { reply_json(s, 401, "{\"error\":\"unauthorized\"}"); return; }
  if (!local && (!strcmp(path, "/v1/pair/reset") || !strcmp(path, "/v1/remote"))) { reply_json(s, 403, "{\"error\":\"local_only\"}"); return; }

  if (!strcmp(method, "GET")) {
    if (!strcmp(path, "/v1/state")) { static char b[8192]; size_t n = player_state_json(b, sizeof b); reply(s, 200, "application/json", b, n); return; }
    if (!strcmp(path, "/v1/queue")) {
      static char *b; if (!b) b = malloc(2 * 1024 * 1024);
      size_t n = b ? player_queue_json(b, 2 * 1024 * 1024) : 0;
      reply(s, n ? 200 : 500, "application/json", b, n); return;
    }
    if (!strcmp(path, "/v1/cover")) {
      static unsigned char *b; if (!b) b = malloc(4 * 1024 * 1024);
      const char *mime = "image/jpeg";
      size_t n = b ? player_cover(b, 4 * 1024 * 1024, &mime) : 0;
      if (n) reply(s, 200, mime, b, n); else reply_json(s, 404, "{\"error\":\"no_cover\"}");
      return;
    }
    if (!strcmp(path, "/v1/remote")) {
      char b[160]; snprintf(b, sizeof b, "{\"pin\":\"%s\",\"locked\":%s}", pin, time(NULL) < locked_until ? "true" : "false");
      reply_json(s, 200, b); return;
    }
    if (!strcmp(path, "/v1/system")) {
      char b[600] = "{}"; if (system_fn) system_fn(b, sizeof b);
      reply_json(s, 200, b); return;
    }
    if (!strcmp(path, "/v1/favorites")) {
      char p[260]; snprintf(p, sizeof p, "%s/music.json", data_dir);
      FILE *f = fopen(p, "r"); char *b = NULL; long n = 0;
      if (f) { fseek(f, 0, SEEK_END); n = ftell(f); fseek(f, 0, SEEK_SET); b = n > 0 && n < 1024 * 1024 ? malloc((size_t)n + 1) : NULL; if (b) { n = (long)fread(b, 1, (size_t)n, f); b[n] = 0; } fclose(f); }
      // solo le preferite: le credenziali dei server musicali non escono dalla console
      JVal *j = b ? json_parse(b) : NULL; free(b);
      size_t cap = 256 * 1024, at = 0; char *o = malloc(cap);
      if (!o) { json_free(j); reply_json(s, 500, "{\"error\":\"memory\"}"); return; }
      at += (size_t)snprintf(o, cap, "{\"fav\":[");
      int k = 0;
      JFOR(it, jget(j, "fav")) {
        char a[300], u[1400], i[200];
        json_escape(a, sizeof a, jstr(it, "name", "")); json_escape(u, sizeof u, jstr(it, "url", "")); json_escape(i, sizeof i, jstr(it, "id", ""));
        if (at + 2000 > cap) break;
        at += (size_t)snprintf(o + at, cap - at, "%s{\"id\":\"%s\",\"name\":\"%s\",\"url\":\"%s\"}", k++ ? "," : "", i, a, u);
      }
      snprintf(o + at, cap - at, "]}");
      json_free(j);
      reply_json(s, 200, o); free(o); return;
    }
    if (!strcmp(path, "/v1/music/files")) { music_files(s); return; }
    if (!strcmp(path, "/v1/library")) {
      size_t cap = 8 * 1024 * 1024; char *b = malloc(cap);
      if (!b) { reply_json(s, 500, "{\"error\":\"memory\"}"); return; }
      size_t n = lib_list_json(b, cap); reply(s, 200, "application/json", b, n); free(b); return;
    }
    if (!strcmp(path, "/v1/ping")) { reply_json(s, 200, "{\"ok\":true,\"service\":\"omega\"}"); return; }
    reply_json(s, 404, "{\"error\":\"not_found\"}"); return;
  }
  if (strcmp(method, "POST")) { reply_json(s, 400, "{\"error\":\"method\"}"); return; }
  // il file audio va su disco man mano che arriva, senza tenerlo in memoria
  if (!strcmp(path, "/v1/music/upload")) {
    size_t have = hl - (size_t)(end + 4 - hdr);
    do_upload(s, qcopy, end + 4, have, clen_ll);
    return;
  }
  if (clen > MAX_BODY) { reply_json(s, 413, "{\"error\":\"too_large\"}"); return; }

  // corpo
  char *body = malloc(clen + 1); if (!body) { reply_json(s, 500, "{\"error\":\"memory\"}"); return; }
  size_t have = hl - (size_t)(end + 4 - hdr);
  if (have > clen) have = clen;
  memcpy(body, end + 4, have);
  while (have < clen) { ssize_t k = read(s, body + have, clen - have); if (k <= 0) break; have += (size_t)k; }
  body[have] = 0;
  JVal *j = json_parse(body);
  free(body);
  if (!j) { reply_json(s, 400, "{\"error\":\"json\"}"); return; }
  if (is_pair) {
    if (time(NULL) < locked_until) reply_json(s, 429, "{\"error\":\"locked\"}");
    else if (!strcmp(jstr(j, "pin", ""), pin)) { fails = 0; char b[96]; snprintf(b, sizeof b, "{\"token\":\"%s\"}", secret); reply_json(s, 200, b); player_log("telecomando: nuovo dispositivo abbinato"); }
    else { if (++fails >= 5) { locked_until = time(NULL) + 60; fails = 0; } reply_json(s, 403, "{\"error\":\"bad_pin\"}"); }
  }
  else if (!strcmp(path, "/v1/pair/reset")) { remote_new(); char b[64]; snprintf(b, sizeof b, "{\"pin\":\"%s\"}", pin); reply_json(s, 200, b); player_log("telecomando: nuovo PIN, i dispositivi vanno riabbinati"); }
  else if (!strncmp(path, "/v1/library/", 12)) do_library(s, path + 12, j);
  else if (!strcmp(path, "/v1/music/delete")) {
    char name[256], dir[300], full[600];
    if (!clean_name(jstr(j, "name", ""), name, sizeof name)) reply_json(s, 400, "{\"error\":\"bad_name\"}");
    else { music_dir(dir, sizeof dir); snprintf(full, sizeof full, "%s/%s", dir, name); reply_json(s, unlink(full) == 0 ? 200 : 404, "{}"); }
  }
  else if (!strcmp(path, "/v1/cmd")) do_cmd(s, j);
  else if (!strcmp(path, "/v1/queue")) do_queue(s, j);
  else reply_json(s, 404, "{\"error\":\"not_found\"}");
  json_free(j);
}

static int lsock = -1;
static void *ctl_thread(void *arg) {
  (void)arg;
  for (;;) {
    struct sockaddr_in ca; socklen_t cl = sizeof ca;
    int c = accept(lsock, (struct sockaddr *)&ca, &cl);
    if (c < 0) { usleep(100000); continue; }
    struct timeval tv = { 5, 0 };
    setsockopt(c, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    setsockopt(c, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
    int local = (ntohl(ca.sin_addr.s_addr) >> 24) == 127;
    handle(c, local);
    close(c);
  }
  return NULL;
}

int ctl_start(int port, const char *dir) {
  snprintf(data_dir, sizeof data_dir, "%s", dir ? dir : ".");
  remote_load();
  lsock = socket(AF_INET, SOCK_STREAM, 0);
  if (lsock < 0) return -1;
  int one = 1; setsockopt(lsock, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);   // riavvio veloce; la seconda copia fallisce comunque il bind: la porta è in ascolto
  struct sockaddr_in a; memset(&a, 0, sizeof a);
  a.sin_family = AF_INET; a.sin_port = htons((unsigned short)port); a.sin_addr.s_addr = htonl(INADDR_ANY);   // anche dalla rete di casa: vedi il controllo del token
  if (bind(lsock, (struct sockaddr *)&a, sizeof a) != 0 || listen(lsock, 8) != 0) { close(lsock); lsock = -1; return -2; }
  pthread_t t; pthread_create(&t, NULL, ctl_thread, NULL);
  player_log("controllo in ascolto sulla porta %d (telecomando con PIN)", port);
  return 0;
}
