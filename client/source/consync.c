// Omega UI — cosa la console racconta al server, in un thread a parte:
//   · nome e icona dei giochi installati che il server ancora non conosce
//     (servono nei record e nei profili, dove il gioco lo vede chi non ce l'ha);
//   · i trofei dell'utente in primo piano.
//
// Trofei sulla PS5 (percorsi sotto OMEGA_SYSROOT):
//   /user/home/<uid>/trophy2/nobackup/data/<NPWR>/TRPTITLE.DAT   stato dell'utente
//   /user/trophy2/nobackup/conf/<NPWR>/TROPHY.UCP                definizione del set
//   …/appmeta/<TID>/trophy2/trophy00.ucp                         la stessa, accanto al gioco
// L'archivio UCP ha un indice di voci da 64 byte (nome, posizione, lunghezza):
// se ne leggono tropconf.json, tropmeta_<lingua>.json e l'icona. TRPTITLE.DAT
// si manda così com'è: lo legge il server (api/src/trophyparse.js), che si può
// correggere senza un nuovo rilascio alle console.
//
// Qui si leggono soltanto file, mai si scrive fuori da OMEGA_DIR. Se il thread
// non arriva in fondo per tre avvii di fila (file di guardia), l'importazione
// dei trofei si ferma da sola fino alla versione successiva.
#include "app.h"
#include <dirent.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

#define GUARD_FILE   OMEGA_DIR "/consync.guard"
#define STATE_MAX    (512 * 1024)
#define ICON_MAX     (4 * 1024 * 1024)
#define JSON_MAX     (400 * 1024)
#define MAX_SETS     600
#define GUARD_TRIES  3

#ifdef PS5
int sceUserServiceGetForegroundUser(uint32_t *userId);
#endif

typedef struct { char np[16]; char tag[40]; } TSet;
typedef struct { char tid[16]; char name[96]; char icon[256]; } GameRef;
typedef struct { char token[700]; int ngames; GameRef games[MAX_APPS]; int force; } Job;
typedef struct { char name[36]; long off, len; } UcpEntry;

static SDL_atomic_t running, cancel, done_rev;
static int started;                   // un giro per accesso; un altro solo se lo chiede l'utente

static int stopped(void) { return SDL_AtomicGet(&cancel) || g_token[0] == 0; }

// ------------------------------------------------------------------ guardia --
// Conta gli avvii non finiti di questa versione: "<versione> <n>".
static int guard_begin(void) {
  int n = 0; char ver[40] = "";
  FILE *f = fopen(GUARD_FILE, "r");
  if (f) { if (fscanf(f, "%39s %d", ver, &n) != 2 || strcmp(ver, OMEGA_VERSION)) n = 0; fclose(f); }
  if (n >= GUARD_TRIES) return 0;
  f = fopen(GUARD_FILE, "w");
  if (f) { fprintf(f, "%s %d\n", OMEGA_VERSION, n + 1); fclose(f); }
  return 1;
}
static void guard_end(void) { unlink(GUARD_FILE); }

// --------------------------------------------------------------------- file --
static unsigned char *read_at(const char *path, long off, long len) {
  if (len <= 0) return NULL;
  int fd = open(path, O_RDONLY); if (fd < 0) return NULL;
  unsigned char *buf = malloc((size_t)len + 1);
  long got = 0; ssize_t r;
  while (buf && got < len && (r = pread(fd, buf + got, (size_t)(len - got), off + got)) > 0) got += r;
  close(fd);
  if (!buf || got != len) { free(buf); return NULL; }
  buf[len] = 0;
  return buf;
}

static int is_np_id(const char *s) {
  if (strlen(s) != 12 || strncmp(s, "NPWR", 4) || s[9] != '_') return 0;
  for (int i = 4; i < 12; i++) if (i != 9 && (s[i] < '0' || s[i] > '9')) return 0;
  return 1;
}

static uint32_t be32(const unsigned char *p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }

// Indice di un archivio UCP: numero di voci, 0 se il file non è quello atteso.
static int ucp_index(const char *path, UcpEntry *out, int max) {
  struct stat st; if (stat(path, &st) != 0 || st.st_size < 0x60) return 0;
  unsigned char *head = read_at(path, 0, 0x20); if (!head) return 0;
  long count = be32(head + 0x10), toc = be32(head + 0x14);
  free(head);
  if (count <= 0 || count > 4096 || toc < 0x20 || toc + 0x10 + count * 0x40 > st.st_size) return 0;
  unsigned char *t = read_at(path, toc + 0x10, count * 0x40); if (!t) return 0;
  int n = 0;
  for (long i = 0; i < count && n < max; i++) {
    const unsigned char *e = t + i * 0x40;
    UcpEntry *u = &out[n];
    int k = 0; for (; k < 35 && e[0x10 + k] >= 0x20 && e[0x10 + k] < 0x7f; k++) u->name[k] = (char)e[0x10 + k];
    u->name[k] = 0;
    u->off = be32(e + 0x34); u->len = be32(e + 0x3c);
    if (!k || u->off <= 0 || u->len <= 0 || u->off + u->len > st.st_size) continue;   // voce non valida: si salta
    n++;
  }
  free(t);
  return n;
}

static const UcpEntry *ucp_find(const UcpEntry *e, int n, const char *name) {
  for (int i = 0; i < n; i++) if (!strcasecmp(e[i].name, name)) return &e[i];
  return NULL;
}
static const UcpEntry *ucp_prefix(const UcpEntry *e, int n, const char *prefix, const char *ext) {
  size_t pl = strlen(prefix), el = strlen(ext);
  for (int i = 0; i < n; i++) {
    size_t l = strlen(e[i].name);
    if (l >= pl + el && !strncasecmp(e[i].name, prefix, pl) && !strcasecmp(e[i].name + l - el, ext)) return &e[i];
  }
  return NULL;
}

// L'archivio che definisce un set: quello del sistema, altrimenti quello
// accanto a uno dei giochi installati (riconosciuto dal suo tropconf.json).
static int ucp_for(const Job *job, const char *np, char *path, size_t pn, UcpEntry *idx, int max) {
  snprintf(path, pn, OMEGA_SYSROOT "/user/trophy2/nobackup/conf/%s/TROPHY.UCP", np);
  int n = ucp_index(path, idx, max);
  if (n && ucp_find(idx, n, "tropconf.json")) return n;
  static const char *ROOTS[] = { OMEGA_SYSROOT "/system_data/priv/appmeta/%s/trophy2/trophy00.ucp", OMEGA_SYSROOT "/user/appmeta/%s/trophy2/trophy00.ucp",
                                 OMEGA_SYSROOT "/user/app/%s/sce_sys/trophy2/trophy00.ucp" };
  char want[40]; snprintf(want, sizeof want, "\"%s\"", np);
  for (int g = 0; g < job->ngames && !stopped(); g++) {
    for (int r = 0; r < 3; r++) {
      snprintf(path, pn, ROOTS[r], job->games[g].tid);
      if (access(path, R_OK) != 0) continue;
      n = ucp_index(path, idx, max);
      const UcpEntry *c = n ? ucp_find(idx, n, "tropconf.json") : NULL;
      if (!c || c->len > JSON_MAX) continue;
      char *conf = (char *)read_at(path, c->off, c->len);
      int hit = conf && strstr(conf, want) != NULL;
      free(conf);
      if (hit) return n;
    }
  }
  return 0;
}

// -------------------------------------------------------------------- trofei --
static uint32_t console_user(void) {
#ifdef PS5
  uint32_t uid = 0;
  if (sceUserServiceGetForegroundUser(&uid) != 0) return 0;
  return uid;
#else
  return 0x10000001;
#endif
}

// Elenco dei file dei trofei (nome e byte): dice al server com'è fatta la
// cartella sulle console dove l'importazione non trova niente.
static void layout_dir(char *out, size_t *o, size_t cap, const char *dir, int depth) {
  DIR *d = opendir(dir);
  if (!d) { if (*o + 200 < cap) *o += (size_t)snprintf(out + *o, cap - *o, "%s: non leggibile\\n", dir); return; }
  struct dirent *e; int shown = 0;
  while ((e = readdir(d)) && *o + 400 < cap && shown < 60) {
    if (e->d_name[0] == '.') continue;
    char p[900]; snprintf(p, sizeof p, "%s/%s", dir, e->d_name);
    struct stat st; if (stat(p, &st) != 0) continue;
    char esc[160]; json_escape(esc, sizeof esc, p + strlen(OMEGA_SYSROOT));
    *o += (size_t)snprintf(out + *o, cap - *o, "%s %ld\\n", esc, S_ISDIR(st.st_mode) ? -1L : (long)st.st_size);
    shown++;
    if (S_ISDIR(st.st_mode) && depth > 0) layout_dir(out, o, cap, p, depth - 1);
  }
  closedir(d);
}

static int in_list(JVal *arr, const char *id) {
  JFOR(x, arr) if (!strcmp(jstr(x, NULL, ""), id)) return 1;
  return 0;
}

static void diag(const char *token, const char *event, int ok, const char *detail) {
  char body[400];
  snprintf(body, sizeof body, "{\"component\":\"trophies\",\"event\":\"%s\",\"ok\":%s,\"detail\":\"%s\",\"version\":\"" OMEGA_VERSION "\"}", event, ok ? "true" : "false", detail);
  char out[256]; omega_http(HTTP_POST, OMEGA_API "/diag/event", token, body, out, sizeof out);
}

static void sync_trophies(const Job *job) {
  uint32_t uid = console_user();
  if (!uid) { omega_log("trofei: nessun utente in primo piano"); return; }
  char base[512]; snprintf(base, sizeof base, OMEGA_SYSROOT "/user/home/%08x/trophy2/nobackup/data", uid);
  TSet *sets = calloc(MAX_SETS, sizeof *sets); if (!sets) return;
  int nsets = 0;
  DIR *d = opendir(base);
  struct dirent *e;
  while (d && (e = readdir(d)) && nsets < MAX_SETS) {
    if (!is_np_id(e->d_name)) continue;
    char p[700]; snprintf(p, sizeof p, "%s/%s/TRPTITLE.DAT", base, e->d_name);
    struct stat st; if (stat(p, &st) != 0 || st.st_size < 16 || st.st_size > STATE_MAX) continue;
    snprintf(sets[nsets].np, sizeof sets[0].np, "%s", e->d_name);
    snprintf(sets[nsets].tag, sizeof sets[0].tag, "%ld:%ld", (long)st.st_size, (long)st.st_mtime);
    nsets++;
  }
  if (d) closedir(d);

  size_t cap = 64 * 1024 + (size_t)nsets * 80, o = 0;
  char *body = malloc(cap); if (!body) { free(sets); return; }
  o += (size_t)snprintf(body + o, cap - o, "{\"sets\":[");
  for (int i = 0; i < nsets; i++) o += (size_t)snprintf(body + o, cap - o, "%s{\"np_id\":\"%s\",\"tag\":\"%s\"}", i ? "," : "", sets[i].np, sets[i].tag);
  o += (size_t)snprintf(body + o, cap - o, "],\"layout\":\"");
  { size_t lim = o + 12 * 1024;
    char p[512]; snprintf(p, sizeof p, OMEGA_SYSROOT "/user/home/%08x", uid);
    char sub[560];
    snprintf(sub, sizeof sub, "%s/trophy2", p); layout_dir(body, &o, lim, sub, 3);
    snprintf(sub, sizeof sub, "%s/trophy", p); layout_dir(body, &o, lim, sub, 1);
    layout_dir(body, &o, lim, OMEGA_SYSROOT "/user/trophy2/nobackup", 1); }
  o += (size_t)snprintf(body + o, cap - o, "\"}");

  char *resp = malloc(NET_BIG);
  int st = resp ? omega_http(HTTP_POST, OMEGA_API "/trophies/check", job->token, body, resp, NET_BIG) : -1;
  free(body);
  JVal *j = st == 200 ? json_parse(resp) : NULL;
  free(resp);
  if (!j || !jbool(j, "import")) { omega_log("trofei: %d set sulla console, controllo -> %d%s", nsets, st, j ? " (importazione spenta)" : ""); json_free(j); free(sets); return; }

  int ndef = 0, okdef = 0, nicon = 0, nstate = 0, okstate = 0, unread = 0, noucp = 0;
  UcpEntry *idx = malloc(sizeof(UcpEntry) * 1024);
  for (int i = 0; i < nsets && idx && !stopped(); i++) {
    const char *np = sets[i].np;
    int need_def = in_list(jget(j, "need_def"), np), need_icon = in_list(jget(j, "need_icon"), np), need_state = in_list(jget(j, "need_state"), np);
    char url[160], out[512];
    if (need_def || need_icon) {
      char ucp[700]; int n = ucp_for(job, np, ucp, sizeof ucp, idx, 1024);
      if (!n) noucp++;
      const UcpEntry *c = n ? ucp_find(idx, n, "tropconf.json") : NULL;
      if (need_def) ndef++;
      if (need_def && c && c->len <= JSON_MAX) {
        const UcpEntry *m = ucp_find(idx, n, "tropmeta_en-US.json");
        if (!m) m = ucp_find(idx, n, "tropmeta_en-GB.json");
        if (!m) m = ucp_prefix(idx, n, "tropmeta_", ".json");
        char *conf = (char *)read_at(ucp, c->off, c->len);
        char *meta = m && m->len <= JSON_MAX ? (char *)read_at(ucp, m->off, m->len) : NULL;
        if (conf) {
          size_t bl = strlen(conf) + (meta ? strlen(meta) : 4) + 40;
          char *b = malloc(bl);
          if (b) {
            snprintf(b, bl, "{\"conf\":%s,\"meta\":%s}", conf, meta ? meta : "null");
            snprintf(url, sizeof url, OMEGA_API "/trophies/%s/def", np);
            int rc = omega_http(HTTP_POST, url, job->token, b, out, sizeof out);
            if (rc == 200 || rc == 201) okdef++;
            free(b);
          }
        }
        free(conf); free(meta);
      }
      if (need_icon && n && !stopped()) {
        const UcpEntry *ic = ucp_find(idx, n, "icon0.png");
        if (!ic) ic = ucp_prefix(idx, n, "icon0", ".png");
        unsigned char *png = ic && ic->len <= ICON_MAX ? read_at(ucp, ic->off, ic->len) : NULL;
        if (png) {
          snprintf(url, sizeof url, OMEGA_API "/trophies/%s/icon", np);
          int rc = omega_http_upload(url, job->token, png, (size_t)ic->len, out, sizeof out);
          if (rc == 200 || rc == 201) nicon++;
          free(png);
        }
      }
    }
    if (need_state && !stopped()) {
      nstate++;
      char p[700]; snprintf(p, sizeof p, "%s/%s/TRPTITLE.DAT", base, np);
      struct stat s2;
      unsigned char *raw = stat(p, &s2) == 0 && s2.st_size <= STATE_MAX ? read_at(p, 0, (long)s2.st_size) : NULL;
      if (raw) {
        snprintf(url, sizeof url, OMEGA_API "/trophies/%s/state?tag=%s", np, sets[i].tag);
        int rc = omega_http_upload(url, job->token, raw, (size_t)s2.st_size, out, sizeof out);
        if (rc == 200) { okstate++; if (strstr(out, "\"parsed\":false")) unread++; }
        free(raw);
      }
    }
  }
  free(idx);
  char det[120];
  snprintf(det, sizeof det, "sets=%d:def=%d-%d:icon=%d:state=%d-%d:unread=%d:noucp=%d", nsets, okdef, ndef, nicon, okstate, nstate, unread, noucp);
  omega_log("trofei: %s", det);
  if (ndef || nstate) diag(job->token, "import", okstate == nstate && okdef == ndef, det);
  json_free(j); free(sets);
}

// ---------------------------------------------------------- schede dei giochi --
static void sync_games(const Job *job) {
  if (!job->ngames) return;
  size_t cap = (size_t)job->ngames * 24 + 64, o = 0;
  char *body = malloc(cap); if (!body) return;
  o += (size_t)snprintf(body + o, cap - o, "{\"ids\":[");
  for (int i = 0; i < job->ngames; i++) o += (size_t)snprintf(body + o, cap - o, "%s\"%s\"", i ? "," : "", job->games[i].tid);
  snprintf(body + o, cap - o, "]}");
  char *resp = malloc(NET_BIG);
  int st = resp ? omega_http(HTTP_POST, OMEGA_API "/games/wanted", job->token, body, resp, NET_BIG) : -1;
  free(body);
  JVal *j = st == 200 ? json_parse(resp) : NULL;
  free(resp);
  if (!j) return;
  // nomi: una sola richiesta
  size_t ncap = 64 + (size_t)jlen(jget(j, "names")) * 260; o = 0;
  char *nb = malloc(ncap); int sent = 0;
  if (nb) {
    o += (size_t)snprintf(nb + o, ncap - o, "{\"games\":[");
    JFOR(x, jget(j, "names")) {
      const char *tid = jstr(x, NULL, "");
      for (int i = 0; i < job->ngames; i++) if (!strcmp(job->games[i].tid, tid) && job->games[i].name[0] && strcmp(job->games[i].name, tid)) {
        char esc[220]; json_escape(esc, sizeof esc, job->games[i].name);
        o += (size_t)snprintf(nb + o, ncap - o, "%s{\"game_id\":\"%s\",\"name\":\"%s\"}", sent ? "," : "", tid, esc);
        sent++;
      }
    }
    snprintf(nb + o, ncap - o, "]}");
    char out[256];
    if (sent && !stopped()) omega_http(HTTP_POST, OMEGA_API "/games/meta", job->token, nb, out, sizeof out);
    free(nb);
  }
  int icons = 0;
  JFOR(x, jget(j, "icons")) {
    if (stopped()) break;
    const char *tid = jstr(x, NULL, "");
    for (int i = 0; i < job->ngames; i++) if (!strcmp(job->games[i].tid, tid) && job->games[i].icon[0]) {
      struct stat s; if (stat(job->games[i].icon, &s) != 0 || s.st_size > ICON_MAX) break;
      unsigned char *png = read_at(job->games[i].icon, 0, (long)s.st_size);
      if (png) {
        char url[96], out[256]; snprintf(url, sizeof url, OMEGA_API "/games/%s/icon", tid);
        int rc = omega_http_upload(url, job->token, png, (size_t)s.st_size, out, sizeof out);
        if (rc == 200 || rc == 201) icons++;
        free(png);
      }
      break;
    }
  }
  if (sent || icons) omega_log("giochi: %d nomi e %d icone mandati al server", sent, icons);
  json_free(j);
}

// ------------------------------------------------------------------- thread --
static int worker(void *arg) {
  Job *job = arg;
  sync_games(job);
  if (!stopped()) {
    if (job->force) guard_end();
    if (guard_begin()) { sync_trophies(job); if (!stopped()) guard_end(); }
    else omega_log("trofei: importazione ferma dopo %d avvii non conclusi", GUARD_TRIES);
  }
  free(job);
  SDL_AtomicAdd(&done_rev, 1);
  SDL_AtomicSet(&running, 0);
  return 0;
}

// Avvia il giro (dal thread principale: copia l'elenco dei giochi). force = lo
// ha chiesto l'utente: riparte anche se la guardia aveva fermato i trofei.
void consync_start(int force) {
  if (!g_token[0] || (started && !force) || !SDL_AtomicCAS(&running, 0, 1)) return;
  started = 1;
  Job *job = calloc(1, sizeof *job);
  if (!job) { SDL_AtomicSet(&running, 0); return; }
  snprintf(job->token, sizeof job->token, "%s", g_token);
  job->force = force;
  for (int i = 0; i < napps && job->ngames < MAX_APPS; i++) {
    if (apps[i].builtin || apps[i].hb || apps[i].pld) continue;
    GameRef *g = &job->games[job->ngames++];
    snprintf(g->tid, sizeof g->tid, "%s", apps[i].tid);
    snprintf(g->name, sizeof g->name, "%s", apps[i].name);
    snprintf(g->icon, sizeof g->icon, "%s", apps[i].icon);
  }
  SDL_AtomicSet(&cancel, 0);
  SDL_Thread *t = SDL_CreateThread(worker, "consync", job);
  if (t) SDL_DetachThread(t); else { free(job); SDL_AtomicSet(&running, 0); }
}

// Chiusura o logout: il thread smette alla prima occasione. Un'uscita non è
// un blocco, quindi la guardia si toglie.
void consync_stop(void) {
  started = 0;
  if (!SDL_AtomicGet(&running)) return;
  SDL_AtomicSet(&cancel, 1);
  guard_end();
}

int consync_busy(void) { return SDL_AtomicGet(&running); }
int consync_rev(void) { return SDL_AtomicGet(&done_rev); }
