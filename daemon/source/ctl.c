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
//   POST /v1/library/upload?b=<lotto>&p=<percorso>   corpo = un file di un gioco → OMEGA_DIR/uploads/<lotto>/
//   POST /v1/library/upload/done {"b":"<lotto>","title":""}   chiude il lotto: voce in libreria o homebrew installato
//   GET  /v1/library/space    spazio libero per i caricamenti
//   POST /v1/library/install  {"id":"..."} chiede alla console di installare subito quel gioco
//   GET  /v1/console/jobs     solo dalla console: caricamento in corso e giochi da installare (la coda si svuota)
//   GET  /v1/library/upload/status?b=&p=   byte già arrivati di un file (per riprendere da lì: &o=<byte>)
//   POST /v1/quit             solo dalla console: chiude il servizio (la UI avvia subito quello aggiornato)
//   GET  /v1/library/file?b=&p=   immagine dentro un lotto (icona del gioco)
//   GET  /v1/music/files      file in OMEGA_DIR/Music
//   POST /v1/music/delete     {"name":"..."}
//   GET  /v1/state            stato del lettore (JSON)
//   GET  /v1/queue            coda
//   GET  /v1/cover            copertina incorporata nel brano (404 se non c'è)
//   POST /v1/queue  {"mode":"replace|append|next","start":0,"play":true,"items":[{url,title,...}]}
//   GET  /v1/voice            voce del party nel servizio: {active,party,members,mic,muted,talking}
//   POST /v1/voice  {"cmd":"mute|unmute|toggle|leave"}
//   POST /v1/cmd    {"cmd":"play|pause|toggle|next|prev|stop|seek|volume|shuffle|repeat|jump|remove|clear","value":n}
//                   {"cmd":"prepare_power|resume_power"} solo dalla console: prima di spegnere / annulla
#include "ctl.h"
#include "json.h"
#include "player.h"
#include "lib.h"
#include "voice.h"
#include "power.h"
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
#include <sys/mount.h>
#include <sys/param.h>
#include <errno.h>
#include <poll.h>
#include <unistd.h>

#define MAX_BODY (4 * 1024 * 1024)
#define MIN_FREE (3LL * 1024 * 1024 * 1024)   // un caricamento non lascia mai meno di 3 GB liberi
#define REQ_S 8                               // tempo massimo per leggere una richiesta: /v1/ping non aspetta oltre

void player_log(const char *fmt, ...);

// Thread con uno stack nostro (1 MB). Sulla console il thread riceve comunque uno
// stack minuscolo se si chiede solo una dimensione (pthread_attr_setstacksize è
// ignorato): bastava formattare una risposta per esaurirlo. Con la memoria data
// da noi (pthread_attr_setstack) lo stack è davvero quello.
#define CTL_STACK (1024 * 1024)
// Gli stack si riusano: un caricamento di una cartella apre un thread per file e
// senza riuso migliaia di file esaurivano la memoria. I thread non sono staccati:
// prima di riusare uno stack si fa pthread_join del thread che lo usava (già finito,
// quindi ritorna subito), così si è certi che non lo stia più toccando.
#define POOL 48
static struct { void *stk; int busy; pthread_t t; } pool[POOL];   // busy: 0 libero, 1 in uso, 2 finito
static pthread_mutex_t poolmx = PTHREAD_MUTEX_INITIALIZER;
typedef struct { void *(*fn)(void *); void *arg; int slot; } ThreadJob;
static void *thread_main(void *a) {
  ThreadJob j = *(ThreadJob *)a; free(a);
  void *r = j.fn(j.arg);
  pthread_mutex_lock(&poolmx); pool[j.slot].busy = 2; pthread_mutex_unlock(&poolmx);
  return r;
}
int omega_thread(void *(*fn)(void *), void *arg) {
  int slot = -1; pthread_t done_t; int need_join = 0;
  pthread_mutex_lock(&poolmx);
  for (int i = 0; i < POOL && slot < 0; i++) if (pool[i].busy == 0) slot = i;
  for (int i = 0; i < POOL && slot < 0; i++) if (pool[i].busy == 2) { slot = i; done_t = pool[i].t; need_join = 1; }
  if (slot >= 0) {
    if (!pool[slot].stk && posix_memalign(&pool[slot].stk, 4096, CTL_STACK) != 0) pool[slot].stk = NULL;
    if (pool[slot].stk) pool[slot].busy = 1; else slot = -1;
  }
  pthread_mutex_unlock(&poolmx);
  if (slot < 0) return -1;   // mai thread con lo stack di default: sulla console è troppo piccolo
  if (need_join) pthread_join(done_t, NULL);
  ThreadJob *j = malloc(sizeof *j);
  if (!j) { pthread_mutex_lock(&poolmx); pool[slot].busy = 0; pthread_mutex_unlock(&poolmx); return -1; }
  j->fn = fn; j->arg = arg; j->slot = slot;
  pthread_attr_t at; pthread_attr_init(&at);
  pthread_attr_setstack(&at, pool[slot].stk, CTL_STACK);
  int rc = pthread_create(&pool[slot].t, &at, thread_main, j);
  pthread_attr_destroy(&at);
  if (rc) { free(j); pthread_mutex_lock(&poolmx); pool[slot].busy = 0; pthread_mutex_unlock(&poolmx); }
  return rc;
}
#define spawn omega_thread
#define NOINLINE __attribute__((noinline))
extern const char REMOTE_HTML[]; extern const size_t REMOTE_HTML_LEN;

static char data_dir[200];
static volatile int lsock = -1;
static volatile int ctl_asleep;   // riposo della console (ctl_power): porta chiusa
static pthread_t ctl_tid; static volatile int ctl_tid_ok;
static time_t started_at;   // la UI lo confronta con la data del file: se il file è più nuovo, riavvia
static char last_req[160] = "-";
const char *ctl_last_request(void) { return last_req; }
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
// token del telecomando: il servizio lo annuncia al server (solo per lo stesso account)
const char *ctl_remote_token(void) { return secret; }

static void send_all(int s, const void *p, size_t n) {
  const char *c = p; time_t until = time(NULL) + REQ_S;   // chi non legge non tiene fermo il servizio
  while (n) { ssize_t k = write(s, c, n); if (k <= 0 || time(NULL) > until) return; c += k; n -= (size_t)k; }
}
// read con una scadenza complessiva: un client lento non blocca le altre richieste
static ssize_t read_until(int s, void *b, size_t n, time_t until) {
  time_t left = until - time(NULL); if (left <= 0) return -1;
  struct timeval tv = { left < 5 ? left : 5, 0 }; setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
  return read(s, b, n);
}

static void reply(int s, int code, const char *type, const void *body, size_t len) {
  const char *msg = code == 200 ? "OK" : code == 400 ? "Bad Request" : code == 404 ? "Not Found" : code == 413 ? "Payload Too Large" : code == 202 ? "Accepted" : code == 401 ? "Unauthorized" : code == 403 ? "Forbidden" : code == 429 ? "Too Many Requests" : code == 409 ? "Conflict" : code == 507 ? "Insufficient Storage" : code == 503 ? "Service Unavailable" : "Error";
  char h[256];
  static int trace = 4;   // diagnosi: i primi passi delle prime risposte nel log
  if (trace > 0) player_log("risposta %d: inizio (stack %p)", code, (void *)h);
  int n = snprintf(h, sizeof h, "HTTP/1.1 %d %s\r\nContent-Type: %s\r\nContent-Length: %lu\r\nCache-Control: no-store\r\nConnection: close\r\n\r\n", code, msg, type, (unsigned long)len);
  if (trace > 0) player_log("risposta %d: intestazione %d byte", code, n);
  send_all(s, h, (size_t)n);
  if (trace > 0) player_log("risposta %d: intestazione inviata", code);
  if (len) send_all(s, body, len);
  if (trace > 0) { player_log("risposta %d: corpo inviato", code); trace--; }
}
static void reply_json(int s, int code, const char *js) { reply(s, code, "application/json", js, strlen(js)); }

static int cmd_is(const char *a, const char *b) { return a && !strcmp(a, b); }

NOINLINE static void do_cmd(int s, JVal *j, int local) {
  const char *c = jstr(j, "cmd", "");
  double v = jnum(j, "value", -1);
  if (cmd_is(c, "prepare_power") || cmd_is(c, "resume_power")) {
    if (!local) { reply_json(s, 403, "{\"error\":\"local_only\"}"); return; }
    // la UI sta per spegnere: si risponde solo a lavoro finito. power_prepare
    // chiude la porta in ascolto (ctl_power), non questa connessione.
    if (cmd_is(c, "prepare_power")) power_prepare(); else power_resume();
    reply_json(s, 200, "{\"ok\":true}");
    return;
  }
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

NOINLINE static void do_queue(int s, JVal *j) {
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

// statfs come il pannello Sistema della UI (sulla console statvfs non è affidabile)
static long long free_bytes(const char *dir) {
  struct statfs v; if (statfs(dir, &v) != 0) return -1;
  return (long long)v.f_bavail * (long long)v.f_bsize;
}

// Il file arriva in un thread suo, come i giochi: fino a 600 MB dal telefono
// tenevano fermo il server, e la UI senza risposta a /v1/ping chiudeva il servizio.
typedef struct { int s; char name[256], dst[600]; long long clen; char *pre; size_t have; } MusicJob;
static void *music_thread(void *arg) {
  MusicJob *m = arg;
  struct timeval tv = { 60, 0 };
  setsockopt(m->s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
  char tmp[620]; snprintf(tmp, sizeof tmp, "%s.part", m->dst);
  int fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC, 0666);
  long long got = 0; int ok = fd >= 0;
  if (ok && m->have) { if (write(fd, m->pre, m->have) != (ssize_t)m->have) ok = 0; got = (long long)m->have; }
  char *buf = ok ? malloc(256 * 1024) : NULL; if (!buf) ok = 0;
  while (ok && got < m->clen) {
    if (power_sleeping()) { ok = 0; break; }   // riposo o spegnimento: niente scritture nuove
    ssize_t k = read(m->s, buf, (size_t)(m->clen - got < 256 * 1024 ? m->clen - got : 256 * 1024));
    if (k <= 0) { ok = 0; break; }
    if (write(fd, buf, (size_t)k) != k) { ok = 0; break; }
    got += k;
  }
  free(buf);
  if (fd >= 0) { if (ok && fsync(fd) != 0) ok = 0; close(fd); }
  if (fd < 0) reply_json(m->s, 500, "{\"error\":\"write\"}");
  else if (!ok || got != m->clen) { unlink(tmp); reply_json(m->s, 400, "{\"error\":\"incomplete\"}"); }
  else { rename(tmp, m->dst); player_log("musica: caricato %s (%lld byte)", m->name, got); reply_json(m->s, 200, "{\"ok\":true}"); }
  close(m->s); free(m->pre); free(m);
  return NULL;
}
// 1 = la connessione passa al thread (il chiamante non la chiude)
NOINLINE static int do_upload(int s, const char *qs, const char *body_start, size_t have, long long clen) {
  char name[256] = "", raw[512] = "";
  const char *p = qs ? strstr(qs, "name=") : NULL;
  if (p) { snprintf(raw, sizeof raw, "%.*s", (int)strcspn(p + 5, "&"), p + 5); url_decode(raw); }
  if (!clean_name(raw, name, sizeof name)) { reply_json(s, 400, "{\"error\":\"bad_name\"}"); return 0; }
  if (clen <= 0 || clen > MAX_UPLOAD) { reply_json(s, 413, "{\"error\":\"too_large\"}"); return 0; }
  if (power_sleeping()) { reply_json(s, 503, "{\"error\":\"busy\"}"); return 0; }
  char dir[300];
  music_dir(dir, sizeof dir);
  long long fb = free_bytes(dir);
  if (fb >= 0 && clen + MIN_FREE > fb) { reply_json(s, 507, "{\"error\":\"no_space\"}"); return 0; }
  MusicJob *m = calloc(1, sizeof *m); if (!m) { reply_json(s, 500, "{\"error\":\"memory\"}"); return 0; }
  m->s = s; m->clen = clen;
  snprintf(m->name, sizeof m->name, "%s", name); snprintf(m->dst, sizeof m->dst, "%s/%s", dir, name);
  if (have > (size_t)clen) have = (size_t)clen;
  if (have) { m->pre = malloc(have); if (!m->pre) { free(m); reply_json(s, 500, "{\"error\":\"memory\"}"); return 0; } memcpy(m->pre, body_start, have); m->have = have; }
  if (spawn(music_thread, m) != 0) { free(m->pre); free(m); player_log("musica: nessun thread libero, il telefono riprova"); reply_json(s, 503, "{\"error\":\"busy\"}"); return 0; }
  return 1;
}

NOINLINE static void music_files(int s) {
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

// ------------------------------------------- giochi dal telefono o dal PC --
// Un "lotto" è un caricamento: un file (.pkg, .zip, .elf) o una cartella intera,
// mandata file per file con il suo percorso. I file arrivano in un thread per
// connessione, così musica e comandi restano liberi anche durante un pkg da 50 GB.
#ifdef PS5
#define HB_ROOT "/data/homebrew"
#else
#define HB_ROOT hb_root_desktop()
static const char *hb_root_desktop(void) { static char b[260]; snprintf(b, sizeof b, "%s/homebrew", data_dir); return b; }
#endif
#define UP_SELF "http://127.0.0.1:9095"

static void up_root(char *out, size_t n) { snprintf(out, n, "%s/uploads", data_dir); mkdir(out, 0777); }
static int batch_ok(const char *b) {
  size_t L = strlen(b); if (L < 6 || L > 32) return 0;
  for (; *b; b++) if (!((*b >= 'a' && *b <= 'z') || (*b >= '0' && *b <= '9'))) return 0;
  return 1;
}
// percorso relativo dentro il lotto: niente "..", niente assoluti, caratteri sicuri
static int clean_rel(const char *in, char *out, size_t n) {
  size_t o = 0; int depth = 0;
  const char *p = in;
  while (*p) {
    while (*p == '/') p++;
    if (!*p) break;
    size_t L = strcspn(p, "/");
    if ((L == 1 && p[0] == '.') || (L == 2 && p[0] == '.' && p[1] == '.') || ++depth > 24) return 0;
    if (o && o + 1 < n) out[o++] = '/';
    for (size_t i = 0; i < L && o + 1 < n; i++) {
      unsigned char c = (unsigned char)p[i];
      out[o++] = (c < 0x20 || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|') ? '_' : (char)c;
    }
    p += L;
  }
  out[o] = 0;
  return o > 0 && o + 1 < n;
}
static void mkparents(const char *path) {
  char t[1200]; snprintf(t, sizeof t, "%s", path);
  for (char *c = t + 1; *c; c++) if (*c == '/') { *c = 0; mkdir(t, 0777); *c = '/'; }
}
static int qparam(const char *qs, const char *key, char *out, size_t n) {
  char k[16]; snprintf(k, sizeof k, "%s=", key);
  const char *p = qs;
  while (p && (p = strstr(p, k))) {
    if (p == qs || p[-1] == '?' || p[-1] == '&') { snprintf(out, n, "%.*s", (int)strcspn(p + strlen(k), "&"), p + strlen(k)); url_decode(out); return 1; }
    p++;
  }
  out[0] = 0; return 0;
}
static void rm_rf(const char *path) {
  struct stat st; if (lstat(path, &st) != 0) return;
  if (S_ISDIR(st.st_mode)) {
    DIR *d = opendir(path); struct dirent *e;
    while (d && (e = readdir(d))) {
      if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
      char c[1200]; snprintf(c, sizeof c, "%s/%s", path, e->d_name); rm_rf(c);
    }
    if (d) closedir(d);
    rmdir(path);
  } else unlink(path);
}
static void *rm_thread(void *arg) { rm_rf(arg); player_log("libreria: tolti i file caricati %s", (char *)arg); free(arg); return NULL; }

// Avanzamento del lotto in corso, per le notifiche sulla console (anche in gioco)
const char *i18n_tr(const char *msgid) __attribute__((format_arg(1)));
#define _(x) i18n_tr(x)
static ctl_notify_fn notify_fn;
void ctl_on_notify(ctl_notify_fn fn) { notify_fn = fn; }
static pthread_mutex_t upmx = PTHREAD_MUTEX_INITIALIZER;
static struct { char batch[40]; int quarter; } UPN;
static void upa_note(const char *batch, const char *name, long long done, long long total);
static void up_progress(const char *batch, long long done, long long total) {
  upa_note(batch, NULL, done, total);
  if (!notify_fn || total <= 0) return;
  int q = (int)(done * 4 / total); if (q > 3) q = 3;   // 0, 25, 50, 75 %
  char msg[200] = "";
  pthread_mutex_lock(&upmx);
  if (strcmp(UPN.batch, batch)) { snprintf(UPN.batch, sizeof UPN.batch, "%s", batch); UPN.quarter = -1; }
  if (q > UPN.quarter) {
    UPN.quarter = q;
    snprintf(msg, sizeof msg, _("Omega: ricevo i giochi dal telefono o dal PC (%d%%)"), q * 25);
  }
  pthread_mutex_unlock(&upmx);
  if (msg[0]) notify_fn(msg);
}

// Caricamento in corso, per la tessera che avanza nella home della console
static struct { char batch[40], name[160]; long long done, total; time_t at; } UPA;
static void upa_note(const char *batch, const char *name, long long done, long long total) {
  pthread_mutex_lock(&upmx);
  if (strcmp(UPA.batch, batch)) { snprintf(UPA.batch, sizeof UPA.batch, "%s", batch); UPA.name[0] = 0; }
  if (name && name[0] && !UPA.name[0]) snprintf(UPA.name, sizeof UPA.name, "%s", name);
  if (done >= 0) UPA.done = done;
  if (total > 0) UPA.total = total;
  UPA.at = time(NULL);
  pthread_mutex_unlock(&upmx);
}
static void upa_end(const char *batch) { pthread_mutex_lock(&upmx); if (!strcmp(UPA.batch, batch)) UPA.at = 0; pthread_mutex_unlock(&upmx); }

// Giochi da installare subito, chiesti dal telefono o dal PC: la UI li prende
// con /v1/console/jobs e li mette nella sua coda di installazione.
#define IQ_MAX 16
static char *IQ[IQ_MAX]; static int IQn;
static int iq_push(const char *id) {
  static char item[16384];
  size_t n = lib_item_json(id, item, sizeof item);
  if (!n) return -1;
  pthread_mutex_lock(&upmx);
  int ok = IQn < IQ_MAX && (IQ[IQn] = strdup(item)) != NULL;
  if (ok) IQn++;
  pthread_mutex_unlock(&upmx);
  return ok ? 0 : -1;
}
NOINLINE static void console_jobs(int s) {
  static char out[IQ_MAX * 16384 + 1024];
  char nm[340]; size_t o = 0;
  pthread_mutex_lock(&upmx);
  int active = UPA.at && time(NULL) - UPA.at < 10;
  json_escape(nm, sizeof nm, UPA.name);
  o += (size_t)snprintf(out + o, sizeof out - o, "{\"upload\":{\"active\":%s,\"name\":\"%s\",\"done\":%lld,\"total\":%lld},\"install\":[",
                        active ? "true" : "false", nm, UPA.done, UPA.total);
  for (int i = 0; i < IQn; i++) { if (o + strlen(IQ[i]) + 4 < sizeof out) o += (size_t)snprintf(out + o, sizeof out - o, "%s%s", i ? "," : "", IQ[i]); free(IQ[i]); }
  IQn = 0;
  pthread_mutex_unlock(&upmx);
  o += (size_t)snprintf(out + o, sizeof out - o, "]}");
  reply(s, 200, "application/json", out, o);
}

// File orfani: lotti caricati che non sono finiti in La mia libreria (invio
// interrotto e mai ripreso) si tolgono dopo un giorno.
static void *orphan_thread(void *arg) {
  (void)arg;
  sleep(60);
  char up[300]; up_root(up, sizeof up);
  size_t cap = 4 * 1024 * 1024; char *lib = malloc(cap);
  if (!lib) return NULL;
  lib_list_json(lib, cap);
  DIR *d = opendir(up); struct dirent *e; int n = 0;
  while (d && (e = readdir(d))) {
    if (e->d_name[0] == '.' || !batch_ok(e->d_name)) continue;
    char p[600]; struct stat st; snprintf(p, sizeof p, "%s/%s", up, e->d_name);
    if (stat(p, &st) != 0 || !S_ISDIR(st.st_mode) || time(NULL) - st.st_mtime < 24 * 3600) continue;
    if (strstr(lib, e->d_name)) continue;            // ancora in libreria
    pthread_mutex_lock(&upmx); int busy = !strcmp(UPA.batch, e->d_name) && UPA.at && time(NULL) - UPA.at < 600; pthread_mutex_unlock(&upmx);
    if (busy) continue;
    rm_rf(p); n++;
    player_log("pulizia: tolto il caricamento abbandonato %s", e->d_name);
  }
  if (d) closedir(d);
  free(lib);
  if (n) player_log("pulizia: %d caricamenti abbandonati tolti", n);
  return NULL;
}

typedef struct { int s; char dst[1200]; char batch[40]; long long clen, off, base, total; char *pre; size_t have; } UpJob;
static void *up_thread(void *arg) {
  UpJob *u = arg;
  struct timeval tv = { 60, 0 };
  setsockopt(u->s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
  char tmp[1220]; snprintf(tmp, sizeof tmp, "%s.part", u->dst);
  mkparents(tmp);
  int fd = open(tmp, O_WRONLY | O_CREAT | (u->off > 0 ? O_APPEND : O_TRUNC), 0666);
  int ok = fd >= 0;
  long long got = 0;
  up_progress(u->batch, u->base + u->off, u->total);
  if (ok && u->have) { ok = write(fd, u->pre, u->have) == (ssize_t)u->have; got = (long long)u->have; up_progress(u->batch, u->base + u->off + got, u->total); }
  char *buf = ok ? malloc(512 * 1024) : NULL; if (!buf) ok = 0;
  while (ok && got < u->clen) {
    if (power_sleeping()) { ok = 0; break; }   // riposo o spegnimento: il .part resta, si riprende da lì
    long long want = u->clen - got; if (want > 512 * 1024) want = 512 * 1024;
    ssize_t k = read(u->s, buf, (size_t)want);
    if (k <= 0) { ok = 0; break; }
    if (write(fd, buf, (size_t)k) != k) { ok = 0; break; }
    got += k;
    up_progress(u->batch, u->base + u->off + got, u->total);
  }
  free(buf);
  if (fd >= 0) { if (ok && fsync(fd) != 0) ok = 0; close(fd); }   // su disco prima del rename
  if (ok && got == u->clen && rename(tmp, u->dst) == 0) reply_json(u->s, 200, "{\"ok\":true}");
  else reply_json(u->s, 400, fd < 0 ? "{\"error\":\"write\"}" : "{\"error\":\"incomplete\"}");   // il .part resta: si riprende da lì
  close(u->s); free(u->pre); free(u);
  return NULL;
}
// 1 = la connessione passa al thread (il chiamante non la chiude)
NOINLINE static int start_upload(int s, const char *qs, const char *pre, size_t have, long long clen) {
  char b[40], rel[1024], relc[1024], root[300], dst[1200], num[32];
  qparam(qs, "b", b, sizeof b); qparam(qs, "p", rel, sizeof rel);
  long long off = qparam(qs, "o", num, sizeof num) ? atoll(num) : 0;
  long long base = qparam(qs, "sb", num, sizeof num) ? atoll(num) : 0;
  long long total = qparam(qs, "tb", num, sizeof num) ? atoll(num) : 0;
  if (!batch_ok(b) || !clean_rel(rel, relc, sizeof relc)) { reply_json(s, 400, "{\"error\":\"bad_path\"}"); return 0; }
  if (clen < 0) { reply_json(s, 400, "{\"error\":\"length\"}"); return 0; }
  up_root(root, sizeof root);
  long long fb = free_bytes(root);
  if (fb >= 0 && clen + MIN_FREE > fb) { reply_json(s, 507, "{\"error\":\"no_space\"}"); return 0; }
  if (power_sleeping()) { reply_json(s, 503, "{\"error\":\"busy\"}"); return 0; }
  snprintf(dst, sizeof dst, "%s/%s/%s", root, b, relc);
  if (clen == 0 && off == 0) {   // file vuoto (le cartelle dei giochi ne hanno): si crea e basta
    mkparents(dst);
    int fd = open(dst, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd >= 0) { close(fd); reply_json(s, 200, "{\"ok\":true}"); } else reply_json(s, 500, "{\"error\":\"write\"}");
    return 0;
  }
  if (off > 0) {   // ripresa: il .part deve avere esattamente i byte che il telefono crede arrivati
    char tmp[1220]; struct stat st; snprintf(tmp, sizeof tmp, "%s.part", dst);
    long long have = stat(tmp, &st) == 0 ? (long long)st.st_size : 0;
    if (have != off) { char o[80]; snprintf(o, sizeof o, "{\"error\":\"offset\",\"have\":%lld}", have); reply_json(s, 409, o); return 0; }
  }
  { char first[160]; snprintf(first, sizeof first, "%.*s", (int)strcspn(relc, "/"), relc);
    char *dot = strrchr(first, '.'); if (dot && !strchr(relc, '/')) *dot = 0;   // file singolo: senza estensione
    upa_note(b, first, base + off, total); }
  UpJob *u = calloc(1, sizeof *u); if (!u) { reply_json(s, 500, "{\"error\":\"memory\"}"); return 0; }
  u->s = s; u->clen = clen; u->off = off; u->base = base; u->total = total; snprintf(u->dst, sizeof u->dst, "%s", dst); snprintf(u->batch, sizeof u->batch, "%s", b);
  if (have > (size_t)clen) have = (size_t)clen;
  if (have) { u->pre = malloc(have); if (!u->pre) { free(u); reply_json(s, 500, "{\"error\":\"memory\"}"); return 0; } memcpy(u->pre, pre, have); u->have = have; }
  if (spawn(up_thread, u) != 0) { free(u->pre); free(u); player_log("caricamento: nessun thread libero, il telefono riprova"); reply_json(s, 503, "{\"error\":\"busy\"}"); return 0; }
  return 1;
}

static char *read_small(const char *path, size_t max, size_t *len) {
  FILE *f = fopen(path, "rb"); if (!f) return NULL;
  fseek(f, 0, SEEK_END); long L = ftell(f); fseek(f, 0, SEEK_SET);
  char *b = L > 0 && (size_t)L <= max ? malloc((size_t)L + 1) : NULL;
  if (b) { L = (long)fread(b, 1, (size_t)L, f); b[L] = 0; if (len) *len = (size_t)L; }
  fclose(f);
  return b;
}
static int exists(const char *dir, const char *name) { char p[1200]; struct stat st; snprintf(p, sizeof p, "%s/%s", dir, name); return stat(p, &st) == 0; }

// param.sfo dei giochi PS4: TITLE, TITLE_ID, APP_VER
NOINLINE static void sfo_read(const char *path, char *title, size_t tn, char *tid, size_t idn, char *ver, size_t vn) {
  size_t L = 0; unsigned char *b = (unsigned char *)read_small(path, 1024 * 1024, &L);
  if (!b || L < 20 || memcmp(b, "\0PSF", 4)) { free(b); return; }
  #define U32(o) ((unsigned)b[o] | (unsigned)b[o + 1] << 8 | (unsigned)b[o + 2] << 16 | (unsigned)b[o + 3] << 24)
  unsigned kt = U32(8), dt = U32(12), cnt = U32(16);
  for (unsigned i = 0; i < cnt && 20 + i * 16 + 16 <= L; i++) {
    unsigned e = 20 + i * 16, ko = kt + ((unsigned)b[e] | (unsigned)b[e + 1] << 8), fmt = (unsigned)b[e + 2] | (unsigned)b[e + 3] << 8, len = U32(e + 4), dof = dt + U32(e + 12);
    if (ko >= L || dof + len > L || fmt != 0x0204) continue;
    const char *k = (const char *)b + ko; const char *v = (const char *)b + dof;
    if (!strcmp(k, "TITLE")) snprintf(title, tn, "%.*s", (int)len, v);
    else if (!strcmp(k, "TITLE_ID")) snprintf(tid, idn, "%.*s", (int)len, v);
    else if (!strcmp(k, "APP_VER")) snprintf(ver, vn, "%.*s", (int)len, v);
  }
  #undef U32
  free(b);
}

NOINLINE static void do_upload_done(int s, JVal *j) {
  char b[40], up[300], batch[600], root[900], name[256] = "", title[128], cover[1400] = "", tid[16] = "", ver[24] = "", plat[8] = "", url[1100], id[40] = "";
  snprintf(b, sizeof b, "%s", jstr(j, "b", ""));
  if (!batch_ok(b)) { reply_json(s, 400, "{\"error\":\"bad_batch\"}"); return; }
  snprintf(title, sizeof title, "%s", jstr(j, "title", ""));
  up_root(up, sizeof up);
  snprintf(batch, sizeof batch, "%s/%s", up, b);
  // cosa c'è dentro: un file solo, una cartella sola, o più cose
  int files = 0, dirs = 0, part = 0; char only[256] = "";
  DIR *d = opendir(batch); struct dirent *e;
  while (d && (e = readdir(d))) {
    if (e->d_name[0] == '.') continue;
    char p[900]; struct stat st; snprintf(p, sizeof p, "%s/%s", batch, e->d_name);
    if (stat(p, &st) != 0) continue;
    size_t L = strlen(e->d_name); if (L > 5 && !strcmp(e->d_name + L - 5, ".part")) part = 1;
    if (S_ISDIR(st.st_mode)) dirs++; else files++;
    snprintf(only, sizeof only, "%s", e->d_name);
  }
  if (d) closedir(d); else { reply_json(s, 404, "{\"error\":\"no_batch\"}"); return; }
  if (part) { reply_json(s, 409, "{\"error\":\"incomplete\"}"); return; }
  upa_end(b);
  int want_install = jbool(j, "install");

  if (files == 1 && dirs == 0) {
    size_t L = strlen(only); const char *ext = L > 4 ? only + L - 4 : "";
    const char *kind = !strcasecmp(ext, ".pkg") ? "pkg" : !strcasecmp(ext, ".zip") ? "zip" : !strcasecmp(ext, ".elf") ? "elf" : NULL;
    if (!kind) { rm_rf(batch); reply_json(s, 400, "{\"error\":\"unknown_file\"}"); return; }
    if (!title[0]) snprintf(title, sizeof title, "%.*s", (int)(L - 4), only);
    // nome pulito sul disco (l'installatore di sistema non vuole spazi e simboli); il titolo resta quello originale
    char safe[256]; size_t so = 0;
    for (const char *c = only; *c && so + 1 < sizeof safe; c++) safe[so++] = ((*c >= 'a' && *c <= 'z') || (*c >= 'A' && *c <= 'Z') || (*c >= '0' && *c <= '9') || *c == '.' || *c == '-' || *c == '_') ? *c : '_';
    safe[so] = 0;
    if (strcmp(safe, only)) {
      char from[900], to[900]; snprintf(from, sizeof from, "%s/%s", batch, only); snprintf(to, sizeof to, "%s/%s", batch, safe);
      if (rename(from, to) == 0) snprintf(only, sizeof only, "%s", safe);
    }
    snprintf(url, sizeof url, "file://%s/%s", batch, only);
    if (lib_add_upload(title, url, kind, "", "", "", "", id, sizeof id)) { reply_json(s, 500, "{\"error\":\"library\"}"); return; }
    int q = want_install && iq_push(id) == 0;
    char o[200]; snprintf(o, sizeof o, "{\"added\":\"%s\",\"kind\":\"%s\",\"installing\":%s}", id, kind, q ? "true" : "false"); reply_json(s, 200, o);
    player_log("libreria: caricato %s (%s)", title, kind);
  if (notify_fn) { char m[300]; snprintf(m, sizeof m, _("Omega: %s \xC3\xA8 in La mia libreria, pronto da installare"), title); notify_fn(m); }
    return;
  }
  const char *rel = "";
  if (files == 0 && dirs == 1) { snprintf(root, sizeof root, "%s/%s", batch, only); snprintf(name, sizeof name, "%s", only); rel = only; }
  else { snprintf(root, sizeof root, "%s", batch); snprintf(name, sizeof name, "%s", title[0] ? title : "Homebrew"); }

  // homebrew in formato websrv: va subito tra gli homebrew, niente da installare
  if (exists(root, "homebrew.js") || exists(root, "eboot.elf")) {
    char hb[300], dst[600], safe[200]; size_t o = 0;
    for (const char *c = name; *c && o + 1 < sizeof safe; c++) safe[o++] = (*c == '/' || *c == '\\') ? '_' : *c;
    safe[o] = 0;
    snprintf(hb, sizeof hb, "%s", HB_ROOT); mkdir(hb, 0777);
    snprintf(dst, sizeof dst, "%s/%s", hb, safe);
    for (int i = 2; exists(hb, strrchr(dst, '/') + 1) && i < 50; i++) snprintf(dst, sizeof dst, "%s/%s-%d", hb, safe, i);
    if (rename(root, dst) != 0) { char o[96]; snprintf(o, sizeof o, "{\"error\":\"move_failed\",\"errno\":%d}", errno); reply_json(s, 500, o); return; }
    rm_rf(batch);
    char esc[400], res[500]; json_escape(esc, sizeof esc, strrchr(dst, '/') + 1);
    snprintf(res, sizeof res, "{\"homebrew\":\"%s\"}", esc); reply_json(s, 200, res);
    if (notify_fn) { char m[300]; snprintf(m, sizeof m, _("Omega: %s \xC3\xA8 tra gli homebrew"), strrchr(dst, '/') + 1); notify_fn(m); }
    player_log("libreria: homebrew caricato in %s", dst);
    return;
  }

  // gioco in cartella: PS5 (sce_sys/param.json) o PS4 (sce_sys/param.sfo)
  char pj[1000]; snprintf(pj, sizeof pj, "%s/sce_sys/param.json", root);
  char ps[1000]; snprintf(ps, sizeof ps, "%s/sce_sys/param.sfo", root);
  char t2[128] = "";
  if (exists(root, "sce_sys/param.json")) {
    char *txt = read_small(pj, 4 * 1024 * 1024, NULL);
    JVal *p = txt ? json_parse(txt) : NULL; free(txt);
    jcpy(tid, sizeof tid, p, "titleId"); jcpy(ver, sizeof ver, p, "contentVersion");
    JVal *lp = jget(p, "localizedParameters");
    const char *def = jstr(lp, "defaultLanguage", "en-US");
    JVal *loc = jget(lp, def); if (!loc) loc = jget(lp, "en-US");
    jcpy(t2, sizeof t2, loc, "titleName");
    json_free(p);
    snprintf(plat, sizeof plat, "PS5");
  } else if (exists(root, "sce_sys/param.sfo")) {
    sfo_read(ps, t2, sizeof t2, tid, sizeof tid, ver, sizeof ver);
    snprintf(plat, sizeof plat, "PS4");
  } else { rm_rf(batch); reply_json(s, 400, "{\"error\":\"unknown_folder\"}"); return; }
  if (strlen(tid) != 9) { reply_json(s, 400, "{\"error\":\"no_title_id\"}"); return; }
  if (!title[0]) snprintf(title, sizeof title, "%s", t2[0] ? t2 : name);
  if (exists(root, "sce_sys/icon0.png")) {
    char pr[700], enc[1400]; size_t o = 0;
    snprintf(pr, sizeof pr, "%s%ssce_sys/icon0.png", rel, rel[0] ? "/" : "");
    for (const unsigned char *c = (const unsigned char *)pr; *c && o + 4 < sizeof enc; c++) {
      if ((*c >= 'a' && *c <= 'z') || (*c >= 'A' && *c <= 'Z') || (*c >= '0' && *c <= '9') || *c == '/' || *c == '-' || *c == '_' || *c == '.') enc[o++] = (char)*c;
      else o += (size_t)snprintf(enc + o, sizeof enc - o, "%%%02X", *c);
    }
    enc[o] = 0;
    snprintf(cover, sizeof cover, UP_SELF "/v1/library/file?b=%s&p=%s", b, enc);
  }
  snprintf(url, sizeof url, "file://%s", root);
  if (lib_add_upload(title, url, "folder", cover, tid, ver, plat, id, sizeof id)) { reply_json(s, 500, "{\"error\":\"library\"}"); return; }
  int q = want_install && iq_push(id) == 0;
  char o[240]; snprintf(o, sizeof o, "{\"added\":\"%s\",\"kind\":\"folder\",\"title_id\":\"%s\",\"installing\":%s}", id, tid, q ? "true" : "false"); reply_json(s, 200, o);
  player_log("libreria: gioco in cartella %s (%s)", title, tid);
  if (notify_fn) { char m[300]; snprintf(m, sizeof m, _("Omega: %s \xC3\xA8 in La mia libreria, pronto da installare"), title); notify_fn(m); }
}

// quanto di un file è già arrivato: {"have":byte,"done":true|false}
NOINLINE static void upload_status(int s, const char *qs) {
  char b[40], rel[1024], relc[1024], up[300], p[1400], o[96];
  qparam(qs, "b", b, sizeof b); qparam(qs, "p", rel, sizeof rel);
  if (!batch_ok(b) || !clean_rel(rel, relc, sizeof relc)) { reply_json(s, 400, "{\"error\":\"bad_path\"}"); return; }
  up_root(up, sizeof up); snprintf(p, sizeof p, "%s/%s/%s", up, b, relc);
  struct stat st; long long have = 0; int done = 0;
  if (stat(p, &st) == 0) { have = (long long)st.st_size; done = 1; }
  else { char t[1420]; snprintf(t, sizeof t, "%s.part", p); if (stat(t, &st) == 0) have = (long long)st.st_size; }
  snprintf(o, sizeof o, "{\"have\":%lld,\"done\":%s}", have, done ? "true" : "false");
  reply_json(s, 200, o);
}

// icona dentro un lotto (solo immagini)
NOINLINE static void upload_file(int s, const char *qs) {
  char b[40], rel[1024], relc[1024], up[300], p[1400];
  qparam(qs, "b", b, sizeof b); qparam(qs, "p", rel, sizeof rel);
  size_t L = strlen(rel);
  int img = (L > 4 && (!strcasecmp(rel + L - 4, ".png") || !strcasecmp(rel + L - 4, ".jpg"))) || (L > 5 && !strcasecmp(rel + L - 5, ".jpeg"));
  if (!batch_ok(b) || !img || !clean_rel(rel, relc, sizeof relc)) { reply_json(s, 400, "{\"error\":\"bad_path\"}"); return; }
  up_root(up, sizeof up); snprintf(p, sizeof p, "%s/%s/%s", up, b, relc);
  size_t n = 0; char *d = read_small(p, 8 * 1024 * 1024, &n);
  if (!d) { reply_json(s, 404, "{\"error\":\"not_found\"}"); return; }
  reply(s, 200, rel[L - 1] == 'g' && rel[L - 2] == 'n' ? "image/png" : "image/jpeg", d, n);
  free(d);
}

// la sincronizzazione scarica un file: va in un thread, il controllo resta libero
// Un link nuovo che arriva mentre se ne sincronizza un altro non si perde: si
// tiene da parte e parte appena finisce quello in corso.
static volatile int syncing;
static pthread_mutex_t sync_mx = PTHREAD_MUTEX_INITIALIZER;
static char *sync_next; static int sync_next_set;
static void *sync_thread(void *arg) {
  char *url = arg, err[64] = "";
  for (;;) {
    if (url) { lib_set_source(url, err, sizeof err); free(url); } else lib_sync(err, sizeof err);
    pthread_mutex_lock(&sync_mx);
    if (!sync_next_set) { syncing = 0; pthread_mutex_unlock(&sync_mx); break; }
    url = sync_next; sync_next = NULL; sync_next_set = 0;
    pthread_mutex_unlock(&sync_mx);
  }
  return NULL;
}
static void start_sync(const char *url) {
  pthread_mutex_lock(&sync_mx);
  if (syncing) { if (url) { free(sync_next); sync_next = strdup(url); sync_next_set = 1; } pthread_mutex_unlock(&sync_mx); return; }
  syncing = 1;
  pthread_mutex_unlock(&sync_mx);
  if (spawn(sync_thread, url ? strdup(url) : NULL) != 0) syncing = 0;
}

NOINLINE static void do_library(int s, const char *what, JVal *j) {
  char err[64] = "", b[160];
  if (!strcmp(what, "item")) {
    if (lib_add(j, err, sizeof err) == 0) reply_json(s, 200, "{\"ok\":true}");
    else { snprintf(b, sizeof b, "{\"error\":\"%s\"}", err); reply_json(s, 400, b); }
  } else if (!strcmp(what, "remove")) {
    char url[1024], up[300];
    int rc = lib_remove(jstr(j, "id", ""), url, sizeof url);
    reply_json(s, rc == 0 ? 200 : 404, "{}");
    // gioco caricato: via anche i suoi file (in un thread, una cartella può essere enorme)
    up_root(up, sizeof up);
    size_t ul = strlen(up);
    if (!rc && !strncmp(url, "file://", 7) && !strncmp(url + 7, up, ul) && url[7 + ul] == '/') {
      const char *bb = url + 8 + ul; char bdir[400]; snprintf(bdir, sizeof bdir, "%s/%.*s", up, (int)strcspn(bb, "/"), bb);
      char *arg = strdup(bdir);
      if (!arg || spawn(rm_thread, arg) != 0) free(arg);
    }
  } else if (!strcmp(what, "import")) {
    int r = lib_import(j, err, sizeof err);
    int ad = 0, up = 0, sk = 0; lib_last_import(&ad, &up, &sk);
    if (r >= 0) { snprintf(b, sizeof b, "{\"added\":%d,\"updated\":%d,\"skipped\":%d}", r, up, sk); reply_json(s, 200, b); }
    else if (sk) { snprintf(b, sizeof b, "{\"error\":\"%s\",\"skipped\":%d}", err, sk); reply_json(s, 400, b); }
    else { snprintf(b, sizeof b, "{\"error\":\"%s\"}", err); reply_json(s, 400, b); }
  } else if (!strcmp(what, "source")) {
    const char *u = jstr(j, "url", "");
    if (*u && strncmp(u, "http://", 7) && strncmp(u, "https://", 8)) { reply_json(s, 400, "{\"error\":\"invalid_url\"}"); return; }
    if (*u) { start_sync(u); reply_json(s, 202, "{\"syncing\":true}"); }
    else { lib_set_source("", err, sizeof err); reply_json(s, 200, "{\"ok\":true}"); }
  } else if (!strcmp(what, "upload/done")) {
    do_upload_done(s, j);
  } else if (!strcmp(what, "install")) {
    if (iq_push(jstr(j, "id", "")) == 0) reply_json(s, 200, "{\"queued\":true}");
    else reply_json(s, 404, "{\"error\":\"not_found\"}");
  } else if (!strcmp(what, "sync")) {
    start_sync(NULL); reply_json(s, 202, "{\"syncing\":true}");
  } else reply_json(s, 404, "{\"error\":\"not_found\"}");
}

static int handle(int s, int local) {
  // intestazioni
  static char hdr[8192]; size_t hl = 0; char *end = NULL;
  time_t until = time(NULL) + REQ_S;
  while (hl + 1 < sizeof hdr) {
    ssize_t k = read_until(s, hdr + hl, sizeof hdr - 1 - hl, until);
    if (k <= 0) return 0;
    hl += (size_t)k; hdr[hl] = 0;
    if ((end = strstr(hdr, "\r\n\r\n"))) break;
  }
  if (!end) { reply_json(s, 400, "{\"error\":\"header\"}"); return 0; }
  static char path[2048]; char method[8] = ""; path[0] = 0;   // statici: un solo thread serve le richieste
  sscanf(hdr, "%7s %2047s", method, path);
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
  snprintf(last_req, sizeof last_req, "%s %.*s", method, (int)strcspn(path, "?"), path);
  char *qs = strchr(path, '?'); static char qcopy[2048]; qcopy[0] = 0;
  if (qs) { snprintf(qcopy, sizeof qcopy, "%s", qs); char *k = strstr(qs, "k="); if (k && !tok[0]) snprintf(tok, sizeof tok, "%.*s", (int)strcspn(k + 2, "&"), k + 2); *qs = 0; }

  // la pagina del telecomando e l'abbinamento sono aperti; il resto dalla rete vuole il token
  if (!strcmp(method, "GET") && (!strcmp(path, "/") || !strcmp(path, "/index.html"))) {
    char h[200]; int n = snprintf(h, sizeof h, "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\nContent-Length: %lu\r\nCache-Control: no-store\r\nConnection: close\r\n\r\n", (unsigned long)REMOTE_HTML_LEN);
    send_all(s, h, (size_t)n); send_all(s, REMOTE_HTML, REMOTE_HTML_LEN); return 0;
  }
  int is_pair = !strcmp(path, "/v1/pair");
  if (!local && !is_pair && !token_ok(tok)) { reply_json(s, 401, "{\"error\":\"unauthorized\"}"); return 0; }
  if (!local && (!strcmp(path, "/v1/pair/reset") || !strcmp(path, "/v1/remote"))) { reply_json(s, 403, "{\"error\":\"local_only\"}"); return 0; }

  if (!strcmp(method, "GET")) {
    if (!strcmp(path, "/v1/state")) { static char b[8192]; size_t n = player_state_json(b, sizeof b); reply(s, 200, "application/json", b, n); return 0; }
    if (!strcmp(path, "/v1/queue")) {
      static char *b; if (!b) b = malloc(2 * 1024 * 1024);
      size_t n = b ? player_queue_json(b, 2 * 1024 * 1024) : 0;
      reply(s, n ? 200 : 500, "application/json", b, n); return 0;
    }
    if (!strcmp(path, "/v1/cover")) {
      static unsigned char *b; if (!b) b = malloc(4 * 1024 * 1024);
      const char *mime = "image/jpeg";
      size_t n = b ? player_cover(b, 4 * 1024 * 1024, &mime) : 0;
      if (n) reply(s, 200, mime, b, n); else reply_json(s, 404, "{\"error\":\"no_cover\"}");
      return 0;
    }
    if (!strcmp(path, "/v1/remote")) {
      char b[160]; snprintf(b, sizeof b, "{\"pin\":\"%s\",\"locked\":%s}", pin, time(NULL) < locked_until ? "true" : "false");
      reply_json(s, 200, b); return 0;
    }
    if (!strcmp(path, "/v1/system")) {
      static int traced; if (traced < 3) player_log("richiesta /v1/system (thread delle richieste)");
      char b[600] = "{}"; if (system_fn) system_fn(b, sizeof b);
      reply_json(s, 200, b);
      if (traced < 3) { player_log("richiesta /v1/system: risposta inviata"); traced++; }
      return 0;
    }
    if (!strcmp(path, "/v1/favorites")) {
      char p[260]; snprintf(p, sizeof p, "%s/music.json", data_dir);
      FILE *f = fopen(p, "r"); char *b = NULL; long n = 0;
      if (f) { fseek(f, 0, SEEK_END); n = ftell(f); fseek(f, 0, SEEK_SET); b = n > 0 && n < 1024 * 1024 ? malloc((size_t)n + 1) : NULL; if (b) { n = (long)fread(b, 1, (size_t)n, f); b[n] = 0; } fclose(f); }
      // solo le preferite: le credenziali dei server musicali non escono dalla console
      JVal *j = b ? json_parse(b) : NULL; free(b);
      size_t cap = 256 * 1024, at = 0; char *o = malloc(cap);
      if (!o) { json_free(j); reply_json(s, 500, "{\"error\":\"memory\"}"); return 0; }
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
      reply_json(s, 200, o); free(o); return 0;
    }
    if (!strcmp(path, "/v1/music/files")) { music_files(s); return 0; }
    if (!strcmp(path, "/v1/library")) {
      size_t cap = 8 * 1024 * 1024; char *b = malloc(cap);
      if (!b) { reply_json(s, 500, "{\"error\":\"memory\"}"); return 0; }
      size_t n = lib_list_json_lite(b, cap, strstr(qcopy, "lite=1") != NULL); reply(s, 200, "application/json", b, n); free(b); return 0;
    }
    if (!strcmp(path, "/v1/library/item")) {
      char id[48] = ""; const char *q = strstr(qcopy, "id="); if (q) snprintf(id, sizeof id, "%.*s", (int)strcspn(q + 3, "&"), q + 3);
      char *b = malloc(64 * 1024); if (!b) { reply_json(s, 500, "{\"error\":\"memory\"}"); return 0; }
      size_t n = lib_item_json(id, b, 64 * 1024);
      if (n) reply(s, 200, "application/json", b, n); else reply_json(s, 404, "{\"error\":\"not_found\"}");
      free(b); return 0;
    }
    if (!strcmp(path, "/v1/console/jobs")) { if (!local) { reply_json(s, 403, "{\"error\":\"local_only\"}"); return 0; } console_jobs(s); return 0; }
    if (!strcmp(path, "/v1/library/space")) { char up[300], o[96]; up_root(up, sizeof up); snprintf(o, sizeof o, "{\"free\":%lld}", free_bytes(up)); reply_json(s, 200, o); return 0; }
    if (!strcmp(path, "/v1/library/file")) { upload_file(s, qcopy); return 0; }
    if (!strcmp(path, "/v1/library/upload/status")) { upload_status(s, qcopy); return 0; }
    if (!strcmp(path, "/v1/voice")) { char o[1024]; voice_state_json(o, sizeof o); reply_json(s, 200, o); return 0; }
    if (!strcmp(path, "/v1/ping")) { char o[96]; snprintf(o, sizeof o, "{\"ok\":true,\"service\":\"omega\",\"started\":%ld}", (long)started_at); reply_json(s, 200, o); return 0; }
    reply_json(s, 404, "{\"error\":\"not_found\"}"); return 0;
  }
  if (strcmp(method, "POST")) { reply_json(s, 400, "{\"error\":\"method\"}"); return 0; }
  // il file audio va su disco man mano che arriva, senza tenerlo in memoria
  if (!strcmp(path, "/v1/library/upload")) {
    size_t have = hl - (size_t)(end + 4 - hdr);
    return start_upload(s, qcopy, end + 4, have, clen_ll);
  }
  if (!strcmp(path, "/v1/music/upload")) {
    size_t have = hl - (size_t)(end + 4 - hdr);
    return do_upload(s, qcopy, end + 4, have, clen_ll);
  }
  if (clen > MAX_BODY) { reply_json(s, 413, "{\"error\":\"too_large\"}"); return 0; }

  // corpo
  char *body = malloc(clen + 1); if (!body) { reply_json(s, 500, "{\"error\":\"memory\"}"); return 0; }
  size_t have = hl - (size_t)(end + 4 - hdr);
  if (have > clen) have = clen;
  memcpy(body, end + 4, have);
  while (have < clen) { ssize_t k = read_until(s, body + have, clen - have, until); if (k <= 0) break; have += (size_t)k; }
  body[have] = 0;
  JVal *j = json_parse(body);
  free(body);
  if (!j) { reply_json(s, 400, "{\"error\":\"json\"}"); return 0; }
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
  else if (!strcmp(path, "/v1/quit")) {
    if (!local) reply_json(s, 403, "{\"error\":\"local_only\"}");
    else {
      reply_json(s, 200, "{\"ok\":true}");
      player_log("servizio: chiuso dalla UI per l'aggiornamento");
      // exit() con FFmpeg, audio e microfono ancora vivi poteva bloccare la
      // console: prima si fermano (al massimo un secondo ciascuno), poi _exit
      voice_power(1);
      player_power(1);
      close(s); if (lsock >= 0) close(lsock);
      _exit(0);
    }
  }
  else if (!strcmp(path, "/v1/voice")) {   // le richieste al server le fa la voce nel suo thread
    int r = voice_command(jstr(j, "cmd", ""));
    reply_json(s, r == 2 ? 202 : r ? 200 : 400, "{}");
  }
  else if (!strcmp(path, "/v1/cmd")) do_cmd(s, j, local);
  else if (!strcmp(path, "/v1/queue")) do_queue(s, j);
  else reply_json(s, 404, "{\"error\":\"not_found\"}");
  json_free(j);
  return 0;
}

static int ctl_port = OMEGA_CTL_PORT;
static int listen_open(void) {
  int ls = socket(AF_INET, SOCK_STREAM, 0); if (ls < 0) return -1;
  int one = 1; setsockopt(ls, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
  struct sockaddr_in a; memset(&a, 0, sizeof a);
  a.sin_family = AF_INET; a.sin_port = htons((unsigned short)ctl_port); a.sin_addr.s_addr = htonl(INADDR_ANY);
  if (bind(ls, (struct sockaddr *)&a, sizeof a) != 0 || listen(ls, 8) != 0) { close(ls); return -1; }
  return ls;
}
// solo dal thread delle richieste: è l'unico che usa lsock
static void listen_close(void) { int ls = lsock; lsock = -1; if (ls >= 0) close(ls); }

static void *ctl_thread(void *arg) {
  (void)arg;
  { char probe; player_log("thread delle richieste: stack intorno a %p", (void *)&probe); }
  ctl_tid = pthread_self(); ctl_tid_ok = 1;
  int fails = 0, logged = 0;
  for (;;) {
    if (ctl_asleep) { if (lsock >= 0) { listen_close(); player_log("controllo: porta %d chiusa per il riposo", ctl_port); } usleep(100000); continue; }
    if (lsock < 0) {
      lsock = listen_open();
      if (lsock < 0) { if (!logged++) player_log("controllo: porta %d non disponibile (errno %d), riprovo", ctl_port, errno); sleep(2); continue; }
      logged = 0;
      player_log("controllo: di nuovo in ascolto sulla porta %d", ctl_port);
    }
    // accept solo quando c'è qualcuno: il ciclo resta libero di vedere il riposo
    struct pollfd pf = { lsock, POLLIN, 0 };
    int pr = poll(&pf, 1, 250);
    if (pr == 0) continue;
    struct sockaddr_in ca; socklen_t cl = sizeof ca;
    int c = pr > 0 && !(pf.revents & (POLLERR | POLLNVAL)) ? accept(lsock, (struct sockaddr *)&ca, &cl) : -1;
    if (c < 0) {
      // dopo il riposo della console il socket in ascolto può restare morto:
      // dopo 5 s di errori di fila lo si chiude e si torna in ascolto
      if (++fails >= 50) {
        player_log("controllo: accept fallisce (errno %d), riapro la porta %d", errno, ctl_port);
        listen_close();
        fails = 0;
      }
      usleep(100000); continue;
    }
    fails = 0;
    if (ctl_asleep) { close(c); continue; }
    struct timeval tv = { 5, 0 };
    setsockopt(c, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    setsockopt(c, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
    int local = (ntohl(ca.sin_addr.s_addr) >> 24) == 127;
    if (!handle(c, local)) close(c);
  }
  return NULL;
}

// Riposo o spegnimento (power.c). 1: la porta in ascolto si chiude (le richieste
// già accettate finiscono); dal thread delle richieste (prepare_power) subito,
// da un altro thread la chiude lui entro mezzo secondo. 0: si riapre.
void ctl_power(int sleeping) {
  ctl_asleep = sleeping ? 1 : 0;
  if (!sleeping) return;
  if (ctl_tid_ok && pthread_equal(pthread_self(), ctl_tid)) { if (lsock >= 0) { listen_close(); player_log("controllo: porta %d chiusa per il riposo", ctl_port); } return; }
  for (int i = 0; i < 50 && lsock >= 0; i++) usleep(10000);
}

int ctl_start(int port, const char *dir) {
  started_at = time(NULL); ctl_port = port;
  snprintf(data_dir, sizeof data_dir, "%s", dir ? dir : ".");
  remote_load();
  lsock = socket(AF_INET, SOCK_STREAM, 0);
  if (lsock < 0) return -1;
  int one = 1; setsockopt(lsock, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);   // riavvio veloce; la seconda copia fallisce comunque il bind: la porta è in ascolto
  struct sockaddr_in a; memset(&a, 0, sizeof a);
  a.sin_family = AF_INET; a.sin_port = htons((unsigned short)port); a.sin_addr.s_addr = htonl(INADDR_ANY);   // anche dalla rete di casa: vedi il controllo del token
  if (bind(lsock, (struct sockaddr *)&a, sizeof a) != 0 || listen(lsock, 8) != 0) { close(lsock); lsock = -1; return -2; }
  if (spawn(ctl_thread, NULL) != 0) { close(lsock); lsock = -1; return -3; }
  spawn(orphan_thread, NULL);
  player_log("controllo in ascolto sulla porta %d (telecomando con PIN)", port);
  return 0;
}
