// Omega UI — Salvataggi online, come su PS Plus, ma cifrati qui sulla console.
//
// Chiave: 32 byte casuali generati sulla console. Il server riceve solo la
// chiave cifrata con la parola d'ordine dell'utente (Argon2id 32 MB, 4 passaggi,
// poi XChaCha20-Poly1305): su un'altra console la parola d'ordine la sblocca.
// Il server non ha mai né la chiave né la parola d'ordine, quindi non può
// leggere i salvataggi; e se qualcuno li manomettesse, qui il controllo
// Poly1305 fallisce e non si scrive niente.
//
// Lettura (PS5): l'immagine del salvataggio si copia in OMEGA_DIR/saves/work,
// la chiave sigillata (dentro l'immagine a 0x800 per i giochi PS5, nel file
// <dir>.bin per quelli PS4) si apre con /dev/pfsmgr e la copia si monta con
// sceFsMountSaveData (come garlic-savemgr e PS5 Save Mounter). L'originale non
// si tocca.
//
// Ripristino: solo su un salvataggio che sulla console c'è già (lo crea il
// gioco). Si monta una copia, si sostituiscono i file tranne sce_sys (dove
// param.sfo lega il salvataggio a questo utente), si smonta e la copia prende
// il posto dell'originale, che resta in OMEGA_DIR/saves/undo per annullare.
//
// Formato (lo stesso che il server controlla in testa, "OMSAVE1\0"):
//   intestazione 64 byte: magic 8 · key_id 8 · nonce 16 · byte in chiaro u64 ·
//   pezzo u32 · title_id 16 · 4 zeri
//   poi pezzi da 1 MB: cifrato + MAC 16; nonce = nonce16 ‖ indice u64,
//   dati associati = intestazione ‖ "ultimo pezzo" (niente tagli né scambi).
//   In chiaro: "OMSA" v1, record {tipo 1, lunghezza percorso u16, percorso,
//   dimensione u64, permessi u32, dati}, tipo 0 alla fine.
#include "app.h"
#include "monocypher.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <stdlib.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#ifdef PS5
#include <ps5/kernel.h>
#include <signal.h>
#include <sys/ioctl.h>
typedef struct { uint8_t reserved; char *budgetid; } FsMountOpt;
typedef struct { uint8_t dummy; } FsUmountOpt;
int sceFsInitMountSaveDataOpt(FsMountOpt *opt);
int sceFsMountSaveData(FsMountOpt *opt, const char *path, const char *mount, uint8_t *key);
int sceFsInitUmountSaveDataOpt(FsUmountOpt *opt);
int sceFsUmountSaveData(FsUmountOpt *opt, const char *mount, int handle, int ignore);
int sceSystemServiceGetAppIdOfRunningBigApp(void);
int sceSystemServiceGetAppTitleId(int appId, char *titleId);
int sceUserServiceGetForegroundUser(uint32_t *userId);
#endif

#define SV_DIR    OMEGA_DIR "/saves"
#define WORK      SV_DIR "/work"
#define UNDO      SV_DIR "/undo"
#define MNT       SV_DIR "/mnt"
#define PLAIN_F   WORK "/plain.bin"
#define ENC_F     WORK "/enc.bin"
#define IMG_F     WORK "/img"
#define PCHUNK    (1024 * 1024)
#define HDR       64
#define KDF_MEM   32768
#define KDF_ITER  4
#define MAX_TITLES 160
#define MAX_DIRS  32
#define MAX_FILES 20000
static const unsigned char MAGIC[8] = { 'O', 'M', 'S', 'A', 'V', 'E', '1', 0 };

typedef struct { char dir[64]; char img[400]; char bin[400]; int ps4; long mtime; long long size; } SaveDir;
typedef struct { char id[40]; char at[40]; char device[48]; long long size; int chunks; char key_id[20]; } SaveVer;
typedef struct {
  char tid[16]; char name[96];
  int ndirs; long mtime; long long size;        // sulla console
  int nver; SaveVer ver[3];                     // online, la più recente per prima
  long synced;                                  // mtime locale all'ultimo caricamento/ripristino
  int undo;
} SaveTitle;

static SaveTitle T[MAX_TITLES]; static int nT;
static SDL_mutex *mx;
static char key_id[20]; static unsigned char key[32]; static int key_ok;   // chiave sbloccata su questa console
static int srv_key;            // il server ha una chiave (1), nessuna (0), non si sa (-1)
static char srv_key_id[20];
static long long q_used, q_limit = 0, q_max;
static int loaded, sel; static float anim, scroll;
static char focus_tid[16];
static SDL_atomic_t st;        // 0 libero, 1 al lavoro, 2 finito (risultato da mostrare)
static volatile long long jb_done, jb_total;
static char jb_label[96], jb_phase[96], jb_result[300]; static int jb_err, jb_quiet; static Uint32 jb_ended;
static Uint32 auto_next;

enum { J_REFRESH, J_SETUP, J_UNLOCK, J_RESET, J_REPASS, J_UPLOAD, J_RESTORE, J_UNDO, J_DELETE, J_AUTO };
typedef struct { int kind; char tid[16]; char id[40]; char pass[256]; char token[700]; char me[32]; } Job;

// ------------------------------------------------------------- utilità --
static void hex(const unsigned char *b, int n, char *o) { for (int i = 0; i < n; i++) snprintf(o + i * 2, 3, "%02x", b[i]); }
static int unhex(const char *s, unsigned char *b, int n) {
  for (int i = 0; i < n; i++) { unsigned v; if (sscanf(s + i * 2, "%2x", &v) != 1) return -1; b[i] = (unsigned char)v; }
  return 0;
}
static const char B64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
static void b64enc(const unsigned char *in, int n, char *out) {
  int o = 0;
  for (int i = 0; i < n; i += 3) {
    unsigned v = (unsigned)in[i] << 16 | (i + 1 < n ? (unsigned)in[i + 1] << 8 : 0) | (i + 2 < n ? in[i + 2] : 0);
    out[o++] = B64[v >> 18 & 63]; out[o++] = B64[v >> 12 & 63];
    out[o++] = i + 1 < n ? B64[v >> 6 & 63] : '='; out[o++] = i + 2 < n ? B64[v & 63] : '=';
  }
  out[o] = 0;
}
static int b64dec(const char *s, unsigned char *out, int max) {
  int n = 0, bits = 0; unsigned v = 0;
  for (; *s && *s != '='; s++) {
    const char *p = strchr(B64, *s); if (!p) return -1;
    v = v << 6 | (unsigned)(p - B64); bits += 6;
    if (bits >= 8) { bits -= 8; if (n >= max) return -1; out[n++] = (unsigned char)(v >> bits); }
  }
  return n;
}
static void put64(unsigned char *p, uint64_t v) { for (int i = 0; i < 8; i++) p[i] = (unsigned char)(v >> (8 * i)); }
static uint64_t get64(const unsigned char *p) { uint64_t v = 0; for (int i = 7; i >= 0; i--) v = v << 8 | p[i]; return v; }
static void put32(unsigned char *p, uint32_t v) { for (int i = 0; i < 4; i++) p[i] = (unsigned char)(v >> (8 * i)); }
static uint32_t get32(const unsigned char *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }

static void fmt_bytes(long long b, char *o, size_t n) {
  if (b >= 1000000000LL) snprintf(o, n, "%.2f GB", b / 1e9);
  else if (b >= 1000000) snprintf(o, n, "%.1f MB", b / 1e6);
  else snprintf(o, n, "%lld KB", (b + 999) / 1000);
}
static void fmt_ago(long t, char *o, size_t n) {
  long d = (long)time(NULL) - t;
  if (t <= 0) { snprintf(o, n, "-"); return; }
  if (d < 90) snprintf(o, n, "%s", _("adesso"));
  else if (d < 3600) snprintf(o, n, _("%ld min fa"), d / 60);
  else if (d < 86400) snprintf(o, n, _("%ld h fa"), d / 3600);
  else { struct tm tm = *localtime(&(time_t){ t }); strftime(o, n, "%d/%m/%Y", &tm); }
}

static int rm_tree(const char *p) {
  struct stat s; if (lstat(p, &s) != 0) return 0;
  if (!S_ISDIR(s.st_mode)) return unlink(p);
  DIR *d = opendir(p); if (!d) return -1;
  struct dirent *e; int rc = 0;
  while ((e = readdir(d))) { if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue; char q[1024]; snprintf(q, sizeof q, "%s/%s", p, e->d_name); if (rm_tree(q)) rc = -1; }
  closedir(d);
  return rmdir(p) || rc;
}
static int copy_file(const char *a, const char *b, int mode) {
  int in = open(a, O_RDONLY); if (in < 0) return -1;
  int out = open(b, O_WRONLY | O_CREAT | O_TRUNC, mode); if (out < 0) { close(in); return -2; }
  char *buf = malloc(1 << 20); int rc = buf ? 0 : -3; ssize_t k;
  while (!rc && (k = read(in, buf, 1 << 20)) > 0) {
    for (ssize_t o = 0; o < k;) { ssize_t w = write(out, buf + o, (size_t)(k - o)); if (w <= 0) { rc = -4; break; } o += w; }
  }
  if (!rc && k < 0) rc = -5;
  if (!rc && fsync(out) != 0) rc = -6;
  free(buf); close(in); if (close(out) != 0 && !rc) rc = -7;
  return rc;
}
#ifndef PS5
static int copy_tree(const char *a, const char *b) {
  struct stat s; if (stat(a, &s) != 0) return -1;
  if (!S_ISDIR(s.st_mode)) return copy_file(a, b, 0666);
  mkdir(b, 0777); DIR *d = opendir(a); if (!d) return -1;
  struct dirent *e; int rc = 0;
  while (!rc && (e = readdir(d))) { if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue; char pa[1024], pb[1024]; snprintf(pa, sizeof pa, "%s/%s", a, e->d_name); snprintf(pb, sizeof pb, "%s/%s", b, e->d_name); rc = copy_tree(pa, pb); }
  closedir(d); return rc;
}
#endif
static long long tree_bytes(const char *p) {
  struct stat s; if (lstat(p, &s) != 0) return 0;
  if (!S_ISDIR(s.st_mode)) return s.st_size;
  DIR *d = opendir(p); if (!d) return 0; long long t = 0; struct dirent *e;
  while ((e = readdir(d))) { if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue; char q[1024]; snprintf(q, sizeof q, "%s/%s", p, e->d_name); t += tree_bytes(q); }
  closedir(d); return t;
}

// ----------------------------------------------------- utente e percorsi --
static unsigned user_id(void) {
#ifdef PS5
  uint32_t u = 0; if (sceUserServiceGetForegroundUser(&u) != 0) return 0; return u;
#else
  return 0x10000000;
#endif
}
static const char *running_tid(char *o) {
  o[0] = 0;
#ifdef PS5
  int app = sceSystemServiceGetAppIdOfRunningBigApp();
  if (app >= 0) sceSystemServiceGetAppTitleId(app, o);
#endif
  return o;
}
static int valid_tid(const char *s) {
  if (strlen(s) != 9) return 0;
  for (int i = 0; i < 4; i++) if (s[i] < 'A' || s[i] > 'Z') return 0;
  for (int i = 4; i < 9; i++) if (s[i] < '0' || s[i] > '9') return 0;
  return 1;
}

// I salvataggi di un gioco per l'utente in primo piano.
static int scan_dirs(const char *tid, SaveDir *out, int max) {
  unsigned uid = user_id(); int n = 0;
  if (!uid) return 0;
  for (int kind = 0; kind < 2 && n < max; kind++) {
    char base[300]; snprintf(base, sizeof base, OMEGA_SYSROOT "/user/home/%x/%s/%s", uid, kind ? "savedata_prospero" : "savedata", tid);
    DIR *d = opendir(base); if (!d) continue;
    struct dirent *e;
    while ((e = readdir(d)) && n < max) {
      const char *nm = e->d_name; size_t l = strlen(nm);
      if (nm[0] == '.' || (l > 4 && !strcmp(nm + l - 4, ".bin")) || (l > 6 && (!strcmp(nm + l - 6, ".omtmp") || !strcmp(nm + l - 6, ".omold"))) || !strncmp(nm, "sdimg_sce_bu_", 13) || !strncmp(nm, "sce_bu_", 7)) continue;
      const char *dn = !strncmp(nm, "sdimg_", 6) ? nm + 6 : nm;
      if (!*dn || strlen(dn) >= sizeof out[n].dir) continue;
      SaveDir *s = &out[n]; memset(s, 0, sizeof *s);
      snprintf(s->dir, sizeof s->dir, "%s", dn);
      snprintf(s->img, sizeof s->img, "%s/%s", base, nm);
      struct stat sb; if (stat(s->img, &sb) != 0) continue;
#ifdef PS5
      if (!S_ISREG(sb.st_mode)) continue;
      int fd = open(s->img, O_RDONLY); unsigned char b0 = 0;
      if (fd >= 0) { if (pread(fd, &b0, 1, 0) != 1) b0 = 0; close(fd); }
      s->ps4 = b0 == 0x01 || kind == 0;
      if (s->ps4) snprintf(s->bin, sizeof s->bin, "%s/%s.bin", base, dn);
      s->size = sb.st_size;
#else
      if (!S_ISDIR(sb.st_mode)) continue;     // sul Mac il "contenitore" è una cartella
      s->ps4 = kind == 0;
      s->size = tree_bytes(s->img);
#endif
      s->mtime = (long)sb.st_mtime;
      if (s->bin[0] && stat(s->bin, &sb) == 0 && (long)sb.st_mtime > s->mtime) s->mtime = (long)sb.st_mtime;
      n++;
    }
    closedir(d);
  }
  return n;
}

static int scan_titles(char tids[][16], int max) {
  unsigned uid = user_id(); int n = 0;
  if (!uid) return 0;
  for (int kind = 0; kind < 2; kind++) {
    char base[300]; snprintf(base, sizeof base, OMEGA_SYSROOT "/user/home/%x/%s", uid, kind ? "savedata_prospero" : "savedata");
    DIR *d = opendir(base); if (!d) continue;
    struct dirent *e;
    while ((e = readdir(d)) && n < max) {
      if (!valid_tid(e->d_name)) continue;
      int dup = 0; for (int i = 0; i < n; i++) if (!strcmp(tids[i], e->d_name)) dup = 1;
      if (!dup) snprintf(tids[n++], 16, "%s", e->d_name);
    }
    closedir(d);
  }
  return n;
}

// --------------------------------------------------------------- stato --
static void state_path(char *o, size_t n, const char *me, const char *what) { snprintf(o, n, SV_DIR "/%s-%s", what, me); }
static long state_get(const char *me, const char *tid) {
  char p[200]; state_path(p, sizeof p, me, "synced");
  FILE *f = fopen(p, "r"); if (!f) return 0;
  char t[32]; long v, r = 0;
  while (fscanf(f, "%31s %ld", t, &v) == 2) if (!strcmp(t, tid)) r = v;
  fclose(f); return r;
}
static void state_set(const char *me, const char *tid, long v) {
  char p[200], tmp[220]; state_path(p, sizeof p, me, "synced"); snprintf(tmp, sizeof tmp, "%s.tmp", p);
  FILE *f = fopen(p, "r"), *o = fopen(tmp, "w"); if (!o) { if (f) fclose(f); return; }
  char t[32]; long x;
  if (f) { while (fscanf(f, "%31s %ld", t, &x) == 2) if (strcmp(t, tid)) fprintf(o, "%s %ld\n", t, x); fclose(f); }
  fprintf(o, "%s %ld\n", tid, v); fclose(o); rename(tmp, p);
}

// Chiave sbloccata, salvata solo su questa console (OMEGA_DIR, permessi 0600).
static void key_load(const char *me) {
  char p[200]; state_path(p, sizeof p, me, "key");
  key_ok = 0; key_id[0] = 0;
  FILE *f = fopen(p, "r"); if (!f) return;
  char id[20] = "", kh[80] = "";
  if (fscanf(f, "%19s %79s", id, kh) == 2 && strlen(id) == 16 && strlen(kh) == 64 && unhex(kh, key, 32) == 0) { snprintf(key_id, sizeof key_id, "%s", id); key_ok = 1; }
  fclose(f); crypto_wipe(kh, sizeof kh);
}
static int key_store(const char *me, const unsigned char k[32], const char *id) {
  char p[200], kh[80]; state_path(p, sizeof p, me, "key");
  mkdir(SV_DIR, 0700);
  int fd = open(p, O_WRONLY | O_CREAT | O_TRUNC, 0600); if (fd < 0) return -1;
  hex(k, 32, kh); char line[120]; int l = snprintf(line, sizeof line, "%s %s\n", id, kh);
  int ok = write(fd, line, (size_t)l) == l; close(fd);
  crypto_wipe(kh, sizeof kh); crypto_wipe(line, sizeof line);
  return ok ? 0 : -1;
}
static void key_id_of(const unsigned char k[32], char out[17]) {
  unsigned char h[8];
  crypto_blake2b_keyed(h, 8, k, 32, (const unsigned char *)"omega-save-id", 13);
  hex(h, 8, out);
}
static int kek(const char *pass, const unsigned char salt[16], int mem, int iter, unsigned char out[32]) {
  void *work = malloc((size_t)mem * 1024); if (!work) return -1;
  crypto_argon2_config cfg = { .algorithm = CRYPTO_ARGON2_ID, .nb_blocks = (uint32_t)mem, .nb_passes = (uint32_t)iter, .nb_lanes = 1 };
  crypto_argon2_inputs in = { .pass = (const uint8_t *)pass, .salt = salt, .pass_size = (uint32_t)strlen(pass), .salt_size = 16 };
  crypto_argon2(out, 32, work, cfg, in, crypto_argon2_no_extras);
  crypto_wipe(work, (size_t)mem * 1024); free(work);
  return 0;
}

// ------------------------------------------------------------- server --
static int api_json(Job *j, int method, const char *path, const char *body, JVal **out) {
  static char buf[256 * 1024];
  int s = omega_http(method, path, j->token, body, buf, sizeof buf);
  if (out) *out = s > 0 ? json_parse(buf) : NULL;
  return s;
}
static const char *api_err(JVal *v) { return v ? jstr(v, "error", "") : ""; }

static void fetch_list(Job *j) {
  JVal *v = NULL; int s = api_json(j, HTTP_GET, OMEGA_API "/saves", NULL, &v);
  if (s == 200 && v) {
    SDL_LockMutex(mx);
    for (int i = 0; i < nT; i++) T[i].nver = 0;
    JFOR(t, jget(v, "titles")) {
      const char *tid = jstr(t, "title_id", ""); if (!valid_tid(tid)) continue;
      int k = -1; for (int i = 0; i < nT; i++) if (!strcmp(T[i].tid, tid)) k = i;
      if (k < 0 && nT < MAX_TITLES) { k = nT++; memset(&T[k], 0, sizeof T[k]); snprintf(T[k].tid, sizeof T[k].tid, "%s", tid); }
      if (k < 0) continue;
      if (!T[k].name[0]) jcpy(T[k].name, sizeof T[k].name, t, "name");
      JFOR(x, jget(t, "versions")) {
        if (T[k].nver >= 3) break;
        SaveVer *sv = &T[k].ver[T[k].nver++];
        jcpy(sv->id, sizeof sv->id, x, "save_id"); jcpy(sv->at, sizeof sv->at, x, "at"); jcpy(sv->device, sizeof sv->device, x, "device");
        jcpy(sv->key_id, sizeof sv->key_id, x, "key_id"); sv->size = (long long)jnum(x, "size", 0); sv->chunks = (int)jnum(x, "chunks", 0);
      }
    }
    JVal *q = jget(v, "quota"); q_used = (long long)jnum(q, "used", 0); q_limit = (long long)jnum(q, "limit", 0); q_max = (long long)jnum(q, "max_save", 0);
    const char *kid = jstr(v, "key_id", NULL); srv_key = kid ? 1 : 0; snprintf(srv_key_id, sizeof srv_key_id, "%s", kid ? kid : "");
    SDL_UnlockMutex(mx);
  } else if (s == 404) srv_key = -2;   // server senza questa funzione
  json_free(v);
}

// Scansione locale + elenco online, nello stesso T[].
static void refresh(Job *j) {
  static SaveDir dirs[MAX_DIRS];
  static char tids[MAX_TITLES][16];
  int n = scan_titles(tids, MAX_TITLES);
  SaveTitle *nt = calloc(MAX_TITLES, sizeof *nt); int nn = 0;
  if (!nt) return;
  for (int i = 0; i < n; i++) {
    int nd = scan_dirs(tids[i], dirs, MAX_DIRS); if (!nd) continue;
    SaveTitle *t = &nt[nn++]; snprintf(t->tid, sizeof t->tid, "%s", tids[i]);
    t->ndirs = nd;
    for (int d = 0; d < nd; d++) { t->size += dirs[d].size; if (dirs[d].mtime > t->mtime) t->mtime = dirs[d].mtime; }
    t->synced = state_get(j->me, t->tid);
    char u[300]; snprintf(u, sizeof u, UNDO "/%s/paths.txt", t->tid); t->undo = access(u, 0) == 0;
  }
  SDL_LockMutex(mx);
  memcpy(T, nt, sizeof *nt * (size_t)nn); nT = nn;
  SDL_UnlockMutex(mx);
  free(nt);
  fetch_list(j);
  // nomi dai giochi installati
  SDL_LockMutex(mx);
  for (int i = 0; i < nT; i++) for (int a = 0; a < napps; a++) if (!strcmp(apps[a].tid, T[i].tid)) { snprintf(T[i].name, sizeof T[i].name, "%s", apps[a].name); break; }
  for (int i = 0; i < nT; i++) if (!T[i].name[0]) snprintf(T[i].name, sizeof T[i].name, "%s", T[i].tid);
  // prima i giochi con salvataggi sulla console, poi i più recenti
  for (int i = 1; i < nT; i++) for (int k = i; k > 0; k--) {
    SaveTitle *a = &T[k - 1], *b = &T[k];
    long ta = a->mtime ? a->mtime : 0, tb = b->mtime ? b->mtime : 0;
    if (tb > ta) { SaveTitle x = *a; *a = *b; *b = x; } else break;
  }
  loaded = 1;
  SDL_UnlockMutex(mx);
}

// --------------------------------------------- montaggio di una copia --
#ifdef PS5
static uint64_t old_authid; static uint8_t old_caps[16];
static void priv_up(void) {
  old_authid = kernel_get_ucred_authid(getpid()); kernel_get_ucred_caps(getpid(), old_caps);
  kernel_set_ucred_authid(getpid(), 0x4800000000000010ULL);
  uint8_t c[16]; memcpy(c, old_caps, 16); c[7] |= 0x40; kernel_set_ucred_caps(getpid(), c);
}
static void priv_down(void) { if (old_authid) { kernel_set_ucred_authid(getpid(), old_authid); kernel_set_ucred_caps(getpid(), old_caps); } }
static int g_mounted;
#endif

// Copia l'immagine in WORK e la monta: *mp riceve la cartella con i file.
static int mount_copy(const SaveDir *s, char *mp, size_t mpn, char *err, size_t en) {
  rm_tree(IMG_F);
#ifdef PS5
  if (copy_file(s->img, IMG_F, 0777) != 0) { snprintf(err, en, "%s", _("Copia del salvataggio non riuscita: spazio libero sulla console?")); return -1; }
  unsigned char *data = calloc(1, 0x100);   // l'ioctl scrive oltre i 0x60 byte: niente buffer sullo stack
  if (!data) return -1;
  int fd = open(s->ps4 ? s->bin : IMG_F, O_RDONLY);
  ssize_t r = fd >= 0 ? pread(fd, data, 0x60, s->ps4 ? 0 : 0x800) : -1;
  if (fd >= 0) close(fd);
  if (r != 0x60) { free(data); snprintf(err, en, "%s", _("Chiave del salvataggio non leggibile")); return -1; }
  int pm = open("/dev/pfsmgr", O_RDWR);
  int rc = pm >= 0 ? ioctl(pm, 0xc0845302, data) : -1;
  if (pm >= 0) close(pm);
  unsigned char k[32]; memcpy(k, data + 0x60, 32); free(data);
  if (rc < 0) { snprintf(err, en, _("La console non apre la chiave del salvataggio (%d): firmware troppo vecchio per questo gioco?"), rc); crypto_wipe(k, 32); return -1; }
  mkdir(MNT, 0777);
  // un montaggio rimasto da un'esecuzione interrotta
  { FsUmountOpt uo; memset(&uo, 0, sizeof uo); sceFsInitUmountSaveDataOpt(&uo); sceFsUmountSaveData(&uo, MNT, 0, 1); }
  FsMountOpt mo; memset(&mo, 0, sizeof mo); sceFsInitMountSaveDataOpt(&mo); mo.budgetid = "system";
  signal(SIGPIPE, SIG_DFL);
  rc = sceFsMountSaveData(&mo, IMG_F, MNT, k);
  signal(SIGPIPE, SIG_IGN);
  crypto_wipe(k, 32);
  if (rc < 0) { snprintf(err, en, _("Montaggio del salvataggio non riuscito (0x%08X)"), (unsigned)rc); omega_log("salvataggi: mount %s -> 0x%x", s->img, rc); return -1; }
  g_mounted = 1;
  snprintf(mp, mpn, "%s", MNT);
#else
  if (copy_tree(s->img, IMG_F) != 0) { snprintf(err, en, "%s", _("Copia del salvataggio non riuscita: spazio libero sulla console?")); return -1; }
  snprintf(mp, mpn, "%s", IMG_F);
#endif
  return 0;
}
static int umount_copy(void) {
#ifdef PS5
  if (!g_mounted) return 0;
  FsUmountOpt uo; memset(&uo, 0, sizeof uo); sceFsInitUmountSaveDataOpt(&uo);
  int rc = sceFsUmountSaveData(&uo, MNT, 0, 0);
  sync();
  if (rc < 0) { omega_log("salvataggi: umount -> 0x%x, riprovo forzato", rc); rc = sceFsUmountSaveData(&uo, MNT, 0, 1); sync(); }
  g_mounted = 0;
  return rc < 0 ? -1 : 0;
#else
  return 0;
#endif
}
static long long mount_free(const char *mp) {
#ifdef PS5
  struct statfs s; return statfs(mp, &s) == 0 ? (long long)s.f_bavail * (long long)s.f_bsize : -1;
#else
  (void)mp; return 1LL << 40;
#endif
}
// La copia modificata prende il posto dell'originale (scrittura accanto e rename).
static int commit_copy(const SaveDir *s) {
#ifdef PS5
  char tmp[420]; snprintf(tmp, sizeof tmp, "%s.omtmp", s->img);
  if (copy_file(IMG_F, tmp, 0777) != 0) { unlink(tmp); return -1; }
  if (rename(tmp, s->img) != 0) { unlink(tmp); return -1; }
  unlink(IMG_F);
  return 0;
#else
  char old[420]; snprintf(old, sizeof old, "%s.omold", s->img);
  rm_tree(old);
  if (rename(s->img, old) != 0) return -1;
  if (rename(IMG_F, s->img) != 0) { rename(old, s->img); return -1; }
  rm_tree(old);
  return 0;
#endif
}

// -------------------------------------------------- archivio in chiaro --
typedef struct { FILE *f; int files; long long bytes; int err; } Arc;
static char arc_prefix[80];
static void arc_add_tree(Arc *a, const char *root, const char *rel, int depth) {
  if (a->err || depth > 16) return;
  char full[1024]; snprintf(full, sizeof full, "%s%s%s", root, rel[0] ? "/" : "", rel);
  DIR *d = opendir(full); if (!d) { a->err = -1; return; }
  struct dirent *e;
  while (!a->err && (e = readdir(d))) {
    if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
    char r2[600]; snprintf(r2, sizeof r2, "%s%s%s", rel, rel[0] ? "/" : "", e->d_name);
    char p[1200]; snprintf(p, sizeof p, "%s/%s", root, r2);
    struct stat s; if (lstat(p, &s) != 0) continue;
    if (S_ISDIR(s.st_mode)) { arc_add_tree(a, root, r2, depth + 1); continue; }
    if (!S_ISREG(s.st_mode)) continue;
    if (++a->files > MAX_FILES) { a->err = -2; break; }
    FILE *in = fopen(p, "rb"); if (!in) { a->err = -3; break; }
    // percorso nell'archivio: <cartella del salvataggio>/<percorso>
    char ap[700]; int pl = snprintf(ap, sizeof ap, "%s/%s", arc_prefix, r2);
    unsigned char h[1 + 2 + 8 + 4]; h[0] = 1; h[1] = (unsigned char)pl; h[2] = (unsigned char)(pl >> 8);
    put64(h + 3, (uint64_t)s.st_size); put32(h + 11, (uint32_t)(s.st_mode & 0777));
    if (fwrite(h, 1, 3, a->f) != 3 || fwrite(ap, 1, (size_t)pl, a->f) != (size_t)pl || fwrite(h + 3, 1, 12, a->f) != 12) a->err = -4;
    char buf[65536]; size_t k; long long left = s.st_size;
    while (!a->err && left > 0 && (k = fread(buf, 1, sizeof buf, in)) > 0) {
      if ((long long)k > left) k = (size_t)left;
      if (fwrite(buf, 1, k, a->f) != k) a->err = -4;
      left -= (long long)k; a->bytes += (long long)k; jb_done = a->bytes;
    }
    if (left) a->err = -5;   // il file si è accorciato mentre lo leggevo
    fclose(in);
  }
  closedir(d);
}

// --------------------------------------------------------- cifratura --
static void chunk_nonce(const unsigned char base[16], uint64_t i, unsigned char out[24]) { memcpy(out, base, 16); put64(out + 16, i); }

static int encrypt_file(const char *in_p, const char *out_p, const char *tid, char sha[65]) {
  FILE *in = fopen(in_p, "rb"), *out = fopen(out_p, "wb");
  if (!in || !out) { if (in) fclose(in); if (out) fclose(out); return -1; }
  fseek(in, 0, SEEK_END); long long plain = ftell(in); fseek(in, 0, SEEK_SET);
  unsigned char h[HDR + 1]; memset(h, 0, sizeof h);
  memcpy(h, MAGIC, 8); unhex(key_id, h + 8, 8); arc4random_buf(h + 16, 16);
  put64(h + 32, (uint64_t)plain); put32(h + 40, PCHUNK); memcpy(h + 44, tid, strlen(tid) < 16 ? strlen(tid) : 16);
  Sha256 c; sha_init(&c);
  int rc = fwrite(h, 1, HDR, out) == HDR ? 0 : -2; sha_update(&c, h, HDR);
  uint64_t nch = plain ? (uint64_t)((plain + PCHUNK - 1) / PCHUNK) : 1;
  unsigned char *pb = malloc(PCHUNK), *cb = malloc(PCHUNK + 16);
  if (!pb || !cb) rc = -3;
  for (uint64_t i = 0; !rc && i < nch; i++) {
    size_t n = fread(pb, 1, PCHUNK, in);
    if (i < nch - 1 && n != PCHUNK) { rc = -4; break; }
    unsigned char nonce[24]; chunk_nonce(h + 16, i, nonce);
    h[HDR] = i == nch - 1;
    crypto_aead_lock(cb, cb + n, key, nonce, h, HDR + 1, pb, n);
    if (fwrite(cb, 1, n + 16, out) != n + 16) rc = -2;
    sha_update(&c, cb, n + 16);
    jb_done = (long long)((i + 1) * PCHUNK);
  }
  if (pb) { crypto_wipe(pb, PCHUNK); free(pb); }
  free(cb); fclose(in);
  if (fclose(out) != 0 && !rc) rc = -2;
  sha_hex(&c, sha);
  return rc;
}

// Ritorna 0, -10 se chiave diversa, -11 se il gioco non è quello, -12 se manomesso.
static int decrypt_file(const char *in_p, const char *out_p, const char *tid) {
  FILE *in = fopen(in_p, "rb"); if (!in) return -1;
  fseek(in, 0, SEEK_END); long long total = ftell(in); fseek(in, 0, SEEK_SET);
  unsigned char h[HDR + 1];
  if (fread(h, 1, HDR, in) != HDR || memcmp(h, MAGIC, 8) != 0) { fclose(in); return -12; }
  char kid[17]; hex(h + 8, 8, kid);
  if (strcmp(kid, key_id)) { fclose(in); return -10; }
  char t16[17] = ""; memcpy(t16, h + 44, 16); t16[16] = 0;
  if (strcmp(t16, tid)) { fclose(in); return -11; }
  uint64_t plain = get64(h + 32); uint32_t pc = get32(h + 40);
  if (pc != PCHUNK || plain > (uint64_t)4 << 30) { fclose(in); return -12; }
  uint64_t nch = plain ? (plain + PCHUNK - 1) / PCHUNK : 1;
  if ((long long)(HDR + plain + 16 * nch) != total) { fclose(in); return -12; }
  FILE *out = fopen(out_p, "wb"); if (!out) { fclose(in); return -1; }
  unsigned char *cb = malloc(PCHUNK + 16), *pb = malloc(PCHUNK); int rc = cb && pb ? 0 : -3;
  for (uint64_t i = 0; !rc && i < nch; i++) {
    size_t n = i < nch - 1 ? PCHUNK : (size_t)(plain - (nch - 1) * PCHUNK);
    if (fread(cb, 1, n + 16, in) != n + 16) { rc = -12; break; }
    unsigned char nonce[24]; chunk_nonce(h + 16, i, nonce);
    h[HDR] = i == nch - 1;
    if (crypto_aead_unlock(pb, cb + n, key, nonce, h, HDR + 1, cb, n) != 0) { rc = -12; break; }
    if (fwrite(pb, 1, n, out) != n) rc = -2;
    jb_done = (long long)((i + 1) * PCHUNK);
  }
  if (pb) { crypto_wipe(pb, PCHUNK); free(pb); }
  free(cb); fclose(in);
  if (fclose(out) != 0 && !rc) rc = -2;
  if (rc) unlink(out_p);
  return rc;
}

// ------------------------------------------------------- caricamento --
static int upload_title(Job *j, const char *tid, char *msg, size_t mn) {
  static SaveDir dirs[MAX_DIRS];
  char run[64]; if (!strcmp(running_tid(run), tid)) { snprintf(msg, mn, "%s", _("Chiudi il gioco prima di caricarne i salvataggi")); return -1; }
  int nd = scan_dirs(tid, dirs, MAX_DIRS);
  if (!nd) { snprintf(msg, mn, "%s", _("Su questa console non ci sono salvataggi di questo gioco")); return -1; }
  long newest = 0; for (int d = 0; d < nd; d++) if (dirs[d].mtime > newest) newest = dirs[d].mtime;
  rm_tree(WORK); mkdir(SV_DIR, 0700); mkdir(WORK, 0700);
  Arc a = { fopen(PLAIN_F, "wb"), 0, 0, 0 };
  if (!a.f) { snprintf(msg, mn, "%s", _("Spazio libero insufficiente sulla console")); return -1; }
  fwrite("OMSA\x01\0\0\0", 1, 8, a.f);
  snprintf(jb_phase, sizeof jb_phase, "%s", _("Lettura dei salvataggi"));
  jb_done = 0; jb_total = 0; for (int d = 0; d < nd; d++) jb_total += dirs[d].size;
  int rc = 0;
#ifdef PS5
  priv_up();
#endif
  for (int d = 0; d < nd && !rc; d++) {
    char mp[300], err[200] = "";
    if (mount_copy(&dirs[d], mp, sizeof mp, err, sizeof err) != 0) { snprintf(msg, mn, "%s", err); rc = -1; break; }
    snprintf(arc_prefix, sizeof arc_prefix, "%s", dirs[d].dir);
    arc_add_tree(&a, mp, "", 0);
    umount_copy(); rm_tree(IMG_F);
    if (a.err) { snprintf(msg, mn, _("Lettura del salvataggio %s non riuscita (%d)"), dirs[d].dir, a.err); rc = -1; }
  }
#ifdef PS5
  priv_down();
#endif
  unsigned char end = 0; fwrite(&end, 1, 1, a.f);
  if (fclose(a.f) != 0 && !rc) { snprintf(msg, mn, "%s", _("Spazio libero insufficiente sulla console")); rc = -1; }
  if (rc) { rm_tree(WORK); return rc; }
  snprintf(jb_phase, sizeof jb_phase, "%s", _("Cifratura"));
  struct stat sb; stat(PLAIN_F, &sb); jb_done = 0; jb_total = sb.st_size;
  char sha[65];
  if (encrypt_file(PLAIN_F, ENC_F, tid, sha) != 0) { rm_tree(WORK); snprintf(msg, mn, "%s", _("Cifratura non riuscita: spazio libero sulla console?")); return -1; }
  unlink(PLAIN_F);
  stat(ENC_F, &sb); long long size = sb.st_size;
  if (q_max > 0 && size > q_max) { rm_tree(WORK); snprintf(msg, mn, _("Salvataggio troppo grande per lo spazio online (%lld MB, massimo %lld MB)"), size / 1000000, q_max / 1000000); return -1; }
  // apertura
  char body[400], dev[48], dvesc[100]; snprintf(dev, sizeof dev, "PS5 %s", j->me); json_escape(dvesc, sizeof dvesc, dev);
  snprintf(body, sizeof body, "{\"title_id\":\"%s\",\"size\":%lld,\"sha256\":\"%s\",\"key_id\":\"%s\",\"device\":\"%s\"}", tid, size, sha, key_id, dvesc);
  JVal *v = NULL; int s = api_json(j, HTTP_POST, OMEGA_API "/saves/begin", body, &v);
  if (s != 201 || !v) {
    const char *e = api_err(v);
    if (!strcmp(e, "quota_exceeded")) snprintf(msg, mn, "%s", _("Spazio online esaurito: cancella qualche salvataggio online"));
    else if (!strcmp(e, "key_mismatch") || !strcmp(e, "no_key")) snprintf(msg, mn, "%s", _("La chiave dei salvataggi è cambiata da un'altra console: sbloccala di nuovo"));
    else if (!strcmp(e, "server_full")) snprintf(msg, mn, "%s", _("Il server è pieno in questo momento: riprova più tardi"));
    else if (s == 429) snprintf(msg, mn, "%s", _("Troppi caricamenti di fila: riprova tra poco"));
    else snprintf(msg, mn, _("Il server non accetta il caricamento (%d)"), s);
    json_free(v); rm_tree(WORK); return !strcmp(e, "quota_exceeded") ? -2 : -1;
  }
  char id[40]; jcpy(id, sizeof id, v, "save_id"); long long chunk = (long long)jnum(v, "chunk", 8388608); int nch = (int)jnum(v, "chunks", 1);
  json_free(v);
  snprintf(jb_phase, sizeof jb_phase, "%s", _("Invio"));
  jb_done = 0; jb_total = size;
  FILE *f = fopen(ENC_F, "rb"); unsigned char *buf = f ? malloc((size_t)chunk) : NULL;
  rc = f && buf ? 0 : -1;
  for (int n = 0; !rc && n < nch; n++) {
    size_t len = (size_t)(n < nch - 1 ? chunk : size - (long long)(nch - 1) * chunk);
    if (fread(buf, 1, len, f) != len) { rc = -1; break; }
    Sha256 c; sha_init(&c); sha_update(&c, buf, len); char cs[65]; sha_hex(&c, cs);
    char path[200], out[512]; snprintf(path, sizeof path, OMEGA_API "/saves/%s/chunk/%d?sha=%s", id, n, cs);
    int ok = 0;
    for (int tr = 0; tr < 4 && !ok; tr++) {   // la connessione può cadere: si rimanda lo stesso pezzo
      s = omega_http_upload(path, j->token, buf, len, out, sizeof out);
      ok = s == 204;
      if (!ok) { omega_log("salvataggi: pezzo %d -> %d (tentativo %d)", n, s, tr + 1); if (s == 404 || s == 400 || s == 413) break; SDL_Delay(1500 * (tr + 1)); }
    }
    if (!ok) { rc = -1; snprintf(msg, mn, _("Invio interrotto (%d): riprova"), s); break; }
    jb_done += (long long)len;
  }
  free(buf); if (f) fclose(f);
  if (!rc) {
    char path[160]; snprintf(path, sizeof path, OMEGA_API "/saves/%s/commit", id);
    v = NULL; s = api_json(j, HTTP_POST, path, "{}", &v);
    if (s != 200) { rc = -1; snprintf(msg, mn, _("Il server non ha confermato il salvataggio (%d)"), s); }
    else { JVal *q = jget(v, "quota"); if (q) { q_used = (long long)jnum(q, "used", (double)q_used); } }
    json_free(v);
  }
  if (rc && !msg[0]) snprintf(msg, mn, "%s", _("Invio non riuscito"));
  rm_tree(WORK);
  if (!rc) {
    // caricato dopo un ripristino: quello che c'è ora va bene, la copia per annullare non serve più
    char u[300]; snprintf(u, sizeof u, UNDO "/%s", tid); rm_tree(u);
    state_set(j->me, tid, newest); char sz[24]; fmt_bytes(size, sz, sizeof sz); snprintf(msg, mn, _("Salvataggi caricati online (%s), cifrati"), sz); }
  return rc;
}

// --------------------------------------------------------- ripristino --
typedef struct { char dir[64]; } DirName;
// Controlla un percorso dell'archivio: niente assoluti, "..", vuoti o strani.
static int safe_path(const char *p) {
  if (!*p || *p == '/' || strlen(p) > 600) return 0;
  const char *c = p;
  while (*c) {
    const char *e = strchr(c, '/'); size_t l = e ? (size_t)(e - c) : strlen(c);
    if (l == 0 || (l == 1 && c[0] == '.') || (l == 2 && c[0] == '.' && c[1] == '.')) return 0;
    for (size_t i = 0; i < l; i++) if ((unsigned char)c[i] < 0x20 || c[i] == '\\' || c[i] == ':') return 0;
    if (!e) break; c = e + 1;
  }
  return 1;
}
static int mkdirs_for(const char *file) {
  char p[1200]; snprintf(p, sizeof p, "%s", file);
  for (char *s = p + 1; *s; s++) if (*s == '/') { *s = 0; if (mkdir(p, 0777) != 0 && errno != EEXIST) return -1; *s = '/'; }
  return 0;
}

// Prima passata: controlla l'intero archivio e raccoglie le cartelle.
static int arc_scan(const char *path, DirName *dirs, int *nd, long long per_dir[]) {
  FILE *f = fopen(path, "rb"); if (!f) return -1;
  unsigned char m[8]; if (fread(m, 1, 8, f) != 8 || memcmp(m, "OMSA\x01", 5)) { fclose(f); return -1; }
  int files = 0; *nd = 0;
  for (;;) {
    int t = fgetc(f); if (t == 0) break;
    if (t != 1 || ++files > MAX_FILES) { fclose(f); return -1; }
    unsigned char l2[2]; if (fread(l2, 1, 2, f) != 2) { fclose(f); return -1; }
    int pl = l2[0] | l2[1] << 8; char p[700];
    if (pl <= 0 || pl >= (int)sizeof p || fread(p, 1, (size_t)pl, f) != (size_t)pl) { fclose(f); return -1; }
    p[pl] = 0;
    unsigned char h[12]; if (fread(h, 1, 12, f) != 12) { fclose(f); return -1; }
    uint64_t sz = get64(h);
    if (!safe_path(p) || memchr(p, 0, (size_t)pl) || !strchr(p, '/')) { fclose(f); return -1; }
    char dn[64]; size_t dl = (size_t)(strchr(p, '/') - p); if (dl >= sizeof dn) { fclose(f); return -1; }
    memcpy(dn, p, dl); dn[dl] = 0;
    int k = -1; for (int i = 0; i < *nd; i++) if (!strcmp(dirs[i].dir, dn)) k = i;
    if (k < 0) { if (*nd >= MAX_DIRS) { fclose(f); return -1; } k = (*nd)++; snprintf(dirs[k].dir, sizeof dirs[k].dir, "%s", dn); per_dir[k] = 0; }
    per_dir[k] += (long long)sz;
    if (fseeko(f, (off_t)sz, SEEK_CUR) != 0) { fclose(f); return -1; }
  }
  long pos = ftell(f); fseek(f, 0, SEEK_END); long end = ftell(f);
  fclose(f);
  return pos == end ? 0 : -1;   // niente dati dopo la fine
}

// Svuota la cartella montata, tranne sce_sys (param.sfo lega il salvataggio all'utente).
static int wipe_except_sys(const char *mp) {
  DIR *d = opendir(mp); if (!d) return -1; struct dirent *e; int rc = 0;
  while ((e = readdir(d))) {
    if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..") || !strcmp(e->d_name, "sce_sys")) continue;
    char p[700]; snprintf(p, sizeof p, "%s/%s", mp, e->d_name); if (rm_tree(p)) rc = -1;
  }
  closedir(d); return rc;
}
static int extract_dir(const char *arc, const char *dir, const char *mp) {
  FILE *f = fopen(arc, "rb"); if (!f) return -1;
  fseek(f, 8, SEEK_SET);
  size_t dl = strlen(dir); int rc = 0; char *buf = malloc(65536); if (!buf) { fclose(f); return -1; }
  for (;;) {
    int t = fgetc(f); if (t != 1) break;
    unsigned char l2[2]; if (fread(l2, 1, 2, f) != 2) { rc = -1; break; }
    int pl = l2[0] | l2[1] << 8; char p[700]; if (fread(p, 1, (size_t)pl, f) != (size_t)pl) { rc = -1; break; }
    p[pl] = 0;
    unsigned char h[12]; if (fread(h, 1, 12, f) != 12) { rc = -1; break; }
    long long sz = (long long)get64(h);
    const char *rel = p + dl + 1;
    int mine = !strncmp(p, dir, dl) && p[dl] == '/';
    if (!mine || !strncmp(rel, "sce_sys/", 8) || !strcmp(rel, "sce_sys")) { if (fseeko(f, (off_t)sz, SEEK_CUR)) { rc = -1; break; } continue; }
    char out[1200]; snprintf(out, sizeof out, "%s/%s", mp, rel);
    if (mkdirs_for(out)) { rc = -2; break; }
    int fd = open(out, O_WRONLY | O_CREAT | O_TRUNC, 0777); if (fd < 0) { rc = -2; break; }
    while (sz > 0) {
      size_t k = sz > 65536 ? 65536 : (size_t)sz;
      if (fread(buf, 1, k, f) != k) { rc = -1; break; }
      if (write(fd, buf, k) != (ssize_t)k) { rc = -3; break; }
      sz -= (long long)k; jb_done += (long long)k;
    }
    fchmod(fd, 0777);   // i giochi rifiutano i file del salvataggio senza 0777
    close(fd);
    if (rc) break;
  }
  free(buf); fclose(f); return rc;
}

static int backup_original(const char *tid, const SaveDir *dirs, int nd, const int *use) {
  char u[300]; snprintf(u, sizeof u, UNDO "/%s", tid);
  rm_tree(u); mkdir(SV_DIR, 0700); mkdir(UNDO, 0700); mkdir(u, 0700);
  char lst[340]; snprintf(lst, sizeof lst, "%s/paths.txt", u);
  FILE *f = fopen(lst, "w"); if (!f) return -1;
  int rc = 0;
  for (int d = 0; d < nd && !rc; d++) {
    if (!use[d]) continue;
    char dst[400]; snprintf(dst, sizeof dst, "%s/%d.img", u, d);
#ifdef PS5
    rc = copy_file(dirs[d].img, dst, 0777);
#else
    rc = copy_tree(dirs[d].img, dst);
#endif
    fprintf(f, "%d.img\t%s\n", d, dirs[d].img);
  }
  if (fclose(f) != 0) rc = -1;
  if (rc) rm_tree(u);
  return rc;
}

static int restore_title(Job *j, const char *tid, const char *id, char *msg, size_t mn) {
  static SaveDir dirs[MAX_DIRS]; static DirName ad[MAX_DIRS]; static long long per[MAX_DIRS];
  char run[64]; if (!strcmp(running_tid(run), tid)) { snprintf(msg, mn, "%s", _("Chiudi il gioco prima di ripristinarne i salvataggi")); return -1; }
  SaveVer ver; int found = 0;
  SDL_LockMutex(mx);
  for (int i = 0; i < nT; i++) if (!strcmp(T[i].tid, tid)) for (int k = 0; k < T[i].nver; k++) if (!strcmp(T[i].ver[k].id, id)) { ver = T[i].ver[k]; found = 1; }
  SDL_UnlockMutex(mx);
  if (!found || ver.chunks <= 0 || ver.chunks > 200) { snprintf(msg, mn, "%s", _("Versione non trovata: aggiorna l'elenco")); return -1; }
  if (strcmp(ver.key_id, key_id)) { snprintf(msg, mn, "%s", _("Questo salvataggio è cifrato con una chiave vecchia e non si può aprire")); return -1; }
  int nd = scan_dirs(tid, dirs, MAX_DIRS);
  if (!nd) { snprintf(msg, mn, "%s", _("Sulla console non c'è ancora un salvataggio di questo gioco: avvialo, salva una volta e poi ripristina")); return -1; }
  rm_tree(WORK); mkdir(SV_DIR, 0700); mkdir(WORK, 0700);
  // scaricamento
  snprintf(jb_phase, sizeof jb_phase, "%s", _("Scaricamento"));
  jb_done = 0; jb_total = ver.size;
  FILE *f = fopen(ENC_F, "wb"); if (!f) { snprintf(msg, mn, "%s", _("Spazio libero insufficiente sulla console")); return -1; }
  int rc = 0; long long got = 0;
  for (int n = 0; n < ver.chunks && !rc; n++) {
    char path[160]; snprintf(path, sizeof path, OMEGA_API "/saves/%s/chunk/%d", id, n);
    unsigned char *b = NULL; size_t len = 0; int s = -1;
    for (int tr = 0; tr < 4; tr++) { s = omega_http_bin(path, j->token, &b, &len, 16u << 20); if (s == 200) break; free(b); b = NULL; if (s == 404) break; SDL_Delay(1500 * (tr + 1)); }
    if (s != 200 || !b) { snprintf(msg, mn, _("Scaricamento interrotto (%d): riprova"), s); rc = -1; free(b); break; }
    if (fwrite(b, 1, len, f) != len) { snprintf(msg, mn, "%s", _("Spazio libero insufficiente sulla console")); rc = -1; }
    got += (long long)len; jb_done = got; free(b);
  }
  if (fclose(f) != 0 && !rc) { snprintf(msg, mn, "%s", _("Spazio libero insufficiente sulla console")); rc = -1; }
  if (!rc && got != ver.size) { snprintf(msg, mn, "%s", _("Il salvataggio scaricato è incompleto: riprova")); rc = -1; }
  if (rc) { rm_tree(WORK); return rc; }
  // verifica e decifratura: se un solo byte è stato toccato, qui ci si ferma
  snprintf(jb_phase, sizeof jb_phase, "%s", _("Verifica e decifratura"));
  jb_done = 0; jb_total = ver.size;
  int d = decrypt_file(ENC_F, PLAIN_F, tid);
  unlink(ENC_F);
  if (d) {
    snprintf(msg, mn, "%s", d == -10 ? _("Questo salvataggio è cifrato con una chiave diversa") : d == -11 ? _("Questo salvataggio è di un altro gioco") : _("Il salvataggio non ha superato il controllo di integrità: non lo ripristino"));
    rm_tree(WORK); return -1;
  }
  int nad = 0;
  if (arc_scan(PLAIN_F, ad, &nad, per) != 0) { snprintf(msg, mn, "%s", _("Il contenuto del salvataggio non è valido: non lo ripristino")); rm_tree(WORK); return -1; }
  // quali cartelle ci sono anche sulla console
  int use[MAX_DIRS] = { 0 }, nuse = 0;
  for (int i = 0; i < nd; i++) for (int k = 0; k < nad; k++) if (!strcmp(dirs[i].dir, ad[k].dir)) { use[i] = k + 1; nuse++; }
  if (!nuse) { snprintf(msg, mn, "%s", _("Sulla console mancano questi salvataggi: avvia il gioco, salva una volta e poi ripristina")); rm_tree(WORK); return -1; }
  if (backup_original(tid, dirs, nd, use) != 0) { snprintf(msg, mn, "%s", _("Non riesco a fare la copia di sicurezza: spazio libero sulla console?")); rm_tree(WORK); return -1; }
  snprintf(jb_phase, sizeof jb_phase, "%s", _("Ripristino"));
  jb_done = 0; jb_total = 0; for (int i = 0; i < nd; i++) if (use[i]) jb_total += per[use[i] - 1];
  int done = 0;
#ifdef PS5
  priv_up();
#endif
  for (int i = 0; i < nd && !rc; i++) {
    if (!use[i]) continue;
    char mp[300], err[200] = "";
    if (mount_copy(&dirs[i], mp, sizeof mp, err, sizeof err) != 0) { snprintf(msg, mn, "%s", err); rc = -1; break; }
    // spazio: quello occupato ora (tranne sce_sys) più il libero deve bastare
    char sys[340]; snprintf(sys, sizeof sys, "%s/sce_sys", mp);
    long long avail = mount_free(mp) + tree_bytes(mp) - tree_bytes(sys);
    if (avail >= 0 && per[use[i] - 1] > avail) { umount_copy(); rm_tree(IMG_F); snprintf(msg, mn, _("Il salvataggio online è più grande di quello sulla console (%s): nel gioco crea un salvataggio più recente e riprova"), dirs[i].dir); rc = -1; break; }
    if (wipe_except_sys(mp) != 0 || extract_dir(PLAIN_F, dirs[i].dir, mp) != 0) { umount_copy(); rm_tree(IMG_F); snprintf(msg, mn, _("Scrittura del salvataggio %s non riuscita"), dirs[i].dir); rc = -1; break; }
    if (umount_copy() != 0) { rm_tree(IMG_F); snprintf(msg, mn, "%s", _("Smontaggio non riuscito: l'originale è intatto")); rc = -1; break; }
    if (commit_copy(&dirs[i]) != 0) { snprintf(msg, mn, "%s", _("Sostituzione non riuscita: usa «Annulla l'ultimo ripristino»")); rc = -1; break; }
    done++;
  }
#ifdef PS5
  priv_down();
#endif
  rm_tree(WORK);
  if (rc && !done) { char u[300]; snprintf(u, sizeof u, UNDO "/%s", tid); rm_tree(u); return rc; }   // niente cambiato: niente da annullare
  if (rc) return rc;
  // il salvataggio ora coincide con quello online: il caricamento automatico non lo rimanda
  long newest = 0; int n2 = scan_dirs(tid, dirs, MAX_DIRS); for (int i = 0; i < n2; i++) if (dirs[i].mtime > newest) newest = dirs[i].mtime;
  state_set(j->me, tid, newest);
  if (nuse < nad) snprintf(msg, mn, _("Ripristinati %d salvataggi su %d: per gli altri avvia il gioco e salva una volta"), nuse, nad);
  else snprintf(msg, mn, "%s", _("Salvataggi ripristinati. Puoi annullare dal menu del gioco"));
  return 0;
}

static int undo_title(Job *j, const char *tid, char *msg, size_t mn) {
  char run[64]; if (!strcmp(running_tid(run), tid)) { snprintf(msg, mn, "%s", _("Chiudi il gioco prima di annullare")); return -1; }
  char u[300], lst[340]; snprintf(u, sizeof u, UNDO "/%s", tid); snprintf(lst, sizeof lst, "%s/paths.txt", u);
  FILE *f = fopen(lst, "r"); if (!f) { snprintf(msg, mn, "%s", _("Non c'è niente da annullare")); return -1; }
  char line[600]; int rc = 0, n = 0;
  unsigned uid = user_id(); char home[64]; snprintf(home, sizeof home, OMEGA_SYSROOT "/user/home/%x/", uid);
  while (!rc && fgets(line, sizeof line, f)) {
    char *tab = strchr(line, '\t'); if (!tab) continue; *tab = 0; char *dst = tab + 1; dst[strcspn(dst, "\r\n")] = 0;
    // solo dentro i salvataggi dell'utente e di questo gioco
    if (strncmp(dst, home, strlen(home)) || !strstr(dst, tid) || strstr(dst, "..")) { rc = -1; break; }
    char src[400]; snprintf(src, sizeof src, "%s/%s", u, line);
#ifdef PS5
    char tmp[420]; snprintf(tmp, sizeof tmp, "%s.omtmp", dst);
    if (copy_file(src, tmp, 0777) != 0 || rename(tmp, dst) != 0) { unlink(tmp); rc = -1; }
#else
    rm_tree(dst); if (copy_tree(src, dst) != 0) rc = -1;
#endif
    n++;
  }
  fclose(f);
  if (rc) { snprintf(msg, mn, "%s", _("Annullamento non riuscito: la copia di sicurezza resta dov'è")); return -1; }
  rm_tree(u);
  state_set(j->me, tid, 0);
  snprintf(msg, mn, _("Ripristino annullato: tornati i salvataggi di prima (%d)"), n);
  return 0;
}

// ------------------------------------------------------------ chiave --
static int key_post(Job *j, const unsigned char k[32], const char *kid, int reset, char *msg, size_t mn) {
  unsigned char salt[16], nonce[24], kk[32], wrapped[48];
  arc4random_buf(salt, 16); arc4random_buf(nonce, 24);
  snprintf(jb_phase, sizeof jb_phase, "%s", _("Protezione della chiave"));
  if (kek(j->pass, salt, KDF_MEM, KDF_ITER, kk) != 0) { snprintf(msg, mn, "%s", _("Memoria insufficiente")); return -1; }
  crypto_aead_lock(wrapped, wrapped + 32, kk, nonce, (const uint8_t *)kid, 16, k, 32);
  crypto_wipe(kk, 32);
  char s1[32], s2[48], s3[80], body[400];
  b64enc(salt, 16, s1); b64enc(nonce, 24, s2); b64enc(wrapped, 48, s3);
  snprintf(body, sizeof body, "{\"key_id\":\"%s\",\"mem\":%d,\"iter\":%d,\"salt\":\"%s\",\"nonce\":\"%s\",\"wrapped\":\"%s\"%s}", kid, KDF_MEM, KDF_ITER, s1, s2, s3, reset ? ",\"reset\":true" : "");
  JVal *v = NULL; int s = api_json(j, HTTP_POST, OMEGA_API "/saves/key", body, &v);
  int ok = s == 200;
  if (!ok) {
    if (!strcmp(api_err(v), "key_exists")) snprintf(msg, mn, "%s", _("Hai già una chiave da un'altra console: sbloccala con la tua parola d'ordine"));
    else snprintf(msg, mn, _("Il server non ha salvato la chiave (%d)"), s);
  }
  json_free(v);
  return ok ? 0 : -1;
}

static int key_setup(Job *j, int reset, char *msg, size_t mn) {
  unsigned char k[32]; arc4random_buf(k, 32);
  char kid[17]; key_id_of(k, kid);
  if (key_post(j, k, kid, reset, msg, mn) != 0) { crypto_wipe(k, 32); return -1; }
  if (key_store(j->me, k, kid) != 0) { crypto_wipe(k, 32); snprintf(msg, mn, "%s", _("Non riesco a salvare la chiave sulla console")); return -1; }
  memcpy(key, k, 32); snprintf(key_id, sizeof key_id, "%s", kid); key_ok = 1; crypto_wipe(k, 32);
  // nuova chiave: tutto va ricaricato
  char p[200]; state_path(p, sizeof p, j->me, "synced"); unlink(p);
  snprintf(msg, mn, "%s", reset ? _("Chiave nuova creata: i salvataggi online di prima sono stati cancellati") : _("Salvataggi online attivi. Ricorda la parola d'ordine: serve su un'altra console"));
  return 0;
}

static int key_unlock(Job *j, char *msg, size_t mn) {
  JVal *v = NULL; int s = api_json(j, HTTP_GET, OMEGA_API "/saves/key", NULL, &v);
  if (s != 200 || !v || !jbool(v, "exists")) { json_free(v); snprintf(msg, mn, _("Chiave non disponibile sul server (%d)"), s); return -1; }
  unsigned char salt[16], nonce[24], wrapped[48], kk[32], k[32];
  char kid[20]; jcpy(kid, sizeof kid, v, "key_id");
  int mem = (int)jnum(v, "mem", 0), iter = (int)jnum(v, "iter", 0);
  int ok = b64dec(jstr(v, "salt", ""), salt, 16) == 16 && b64dec(jstr(v, "nonce", ""), nonce, 24) == 24 && b64dec(jstr(v, "wrapped", ""), wrapped, 48) == 48
           && strlen(kid) == 16 && mem >= 8192 && mem <= 262144 && iter >= 1 && iter <= 16;
  json_free(v);
  if (!ok) { snprintf(msg, mn, "%s", _("La chiave sul server non è valida")); return -1; }
  snprintf(jb_phase, sizeof jb_phase, "%s", _("Sblocco della chiave"));
  if (kek(j->pass, salt, mem, iter, kk) != 0) { snprintf(msg, mn, "%s", _("Memoria insufficiente")); return -1; }
  int bad = crypto_aead_unlock(k, wrapped + 32, kk, nonce, (const uint8_t *)kid, 16, wrapped, 32);
  crypto_wipe(kk, 32);
  char check[17]; if (!bad) key_id_of(k, check);
  if (bad || strcmp(check, kid)) { crypto_wipe(k, 32); snprintf(msg, mn, "%s", _("Parola d'ordine sbagliata")); return -1; }
  if (key_store(j->me, k, kid) != 0) { crypto_wipe(k, 32); snprintf(msg, mn, "%s", _("Non riesco a salvare la chiave sulla console")); return -1; }
  memcpy(key, k, 32); snprintf(key_id, sizeof key_id, "%s", kid); key_ok = 1; crypto_wipe(k, 32);
  snprintf(msg, mn, "%s", _("Chiave sbloccata: ora vedi e ripristini i tuoi salvataggi online"));
  return 0;
}

// --------------------------------------------------------------- lavori --
static int auto_on(void) { return g_prefs.save_auto; }

static int worker(void *arg) {
  Job *j = arg; char msg[300] = ""; int rc = 0;
  switch (j->kind) {
    case J_REFRESH: refresh(j); jb_quiet = 1; break;
    case J_SETUP: rc = key_setup(j, 0, msg, sizeof msg); refresh(j); break;
    case J_RESET: rc = key_setup(j, 1, msg, sizeof msg); refresh(j); break;
    case J_UNLOCK: rc = key_unlock(j, msg, sizeof msg); refresh(j); break;
    case J_REPASS: {
      if (!key_ok) { snprintf(msg, sizeof msg, "%s", _("Prima sblocca la chiave")); rc = -1; break; }
      rc = key_post(j, key, key_id, 0, msg, sizeof msg);
      if (!rc) snprintf(msg, sizeof msg, "%s", _("Parola d'ordine cambiata"));
      break;
    }
    case J_UPLOAD: rc = upload_title(j, j->tid, msg, sizeof msg); refresh(j); break;
    case J_RESTORE: rc = restore_title(j, j->tid, j->id, msg, sizeof msg); refresh(j); break;
    case J_UNDO: rc = undo_title(j, j->tid, msg, sizeof msg); refresh(j); break;
    case J_DELETE: {
      char path[160]; snprintf(path, sizeof path, OMEGA_API "/saves/%s/delete", j->id);
      int s = api_json(j, HTTP_POST, path, "{}", NULL);
      rc = s == 200 ? 0 : -1;
      snprintf(msg, sizeof msg, "%s", rc ? _("Cancellazione non riuscita") : _("Versione cancellata dallo spazio online"));
      refresh(j); break;
    }
    case J_AUTO: {
      // dopo una partita: i giochi con salvataggi più nuovi dell'ultimo caricamento
      refresh(j);
      char tids[16][16]; int n = 0;
      SDL_LockMutex(mx);
      for (int i = 0; i < nT && n < 16; i++) if (T[i].ndirs && T[i].mtime > T[i].synced) snprintf(tids[n++], 16, "%s", T[i].tid);
      SDL_UnlockMutex(mx);
      int up = 0; char run[64]; running_tid(run);
      for (int i = 0; i < n; i++) {
        if (!strcmp(run, tids[i])) continue;
        char m2[300] = "";
        snprintf(jb_label, sizeof jb_label, "%s", tids[i]);
        SDL_LockMutex(mx); for (int k = 0; k < nT; k++) if (!strcmp(T[k].tid, tids[i])) snprintf(jb_label, sizeof jb_label, "%s", T[k].name); SDL_UnlockMutex(mx);
        int r = upload_title(j, tids[i], m2, sizeof m2);
        if (r == 0) up++;
        else { omega_log("salvataggi: automatico %s: %s", tids[i], m2); if (r == -2) { snprintf(msg, sizeof msg, "%s", m2); rc = -1; break; } }
      }
      if (up) { refresh(j); if (!rc) snprintf(msg, sizeof msg, up == 1 ? _("%d gioco: salvataggi caricati online") : _("%d giochi: salvataggi caricati online"), up); }
      jb_quiet = !up && !rc;
      break;
    }
  }
  crypto_wipe(j->pass, sizeof j->pass);
  snprintf(jb_result, sizeof jb_result, "%s", msg);
  jb_err = rc != 0;
  omega_log("salvataggi: lavoro %d -> %d %s", j->kind, rc, msg);
  free(j);
  SDL_AtomicSet(&st, 2);
  return 0;
}

static int start(int kind, const char *tid, const char *id, const char *pass, const char *label) {
  if (SDL_AtomicGet(&st) == 1) { if (kind != J_REFRESH && kind != J_AUTO) set_msg(_("Attendi: c'è già un'operazione sui salvataggi in corso"), 1); return -1; }
  if (!g_token[0] || !S.me[0]) return -1;
  if (!mx) mx = SDL_CreateMutex();
  Job *j = calloc(1, sizeof *j); if (!j) return -1;
  j->kind = kind;
  if (tid) snprintf(j->tid, sizeof j->tid, "%s", tid);
  if (id) snprintf(j->id, sizeof j->id, "%s", id);
  if (pass) snprintf(j->pass, sizeof j->pass, "%s", pass);
  snprintf(j->token, sizeof j->token, "%s", g_token); snprintf(j->me, sizeof j->me, "%s", S.me);
  snprintf(jb_label, sizeof jb_label, "%s", label ? label : _("Salvataggi online"));
  jb_phase[0] = 0; jb_done = 0; jb_total = 0; jb_quiet = 0; jb_ended = 0;
  SDL_AtomicSet(&st, 1);
  SDL_Thread *t = SDL_CreateThreadWithStackSize(worker, "saves", 1024 * 1024, j);
  if (!t) { free(j); SDL_AtomicSet(&st, 0); return -1; }
  SDL_DetachThread(t);
  return 0;
}

// Chiamato a ogni fotogramma: risultati e caricamento automatico.
void saves_tick(void) {
  if (SDL_AtomicGet(&st) == 2) {
    SDL_AtomicSet(&st, 0);
    jb_ended = SDL_GetTicks();
    if (!jb_quiet && jb_result[0]) {
      if (ov_top() == OV_SAVES) set_msg(jb_result, jb_err);
      toast(jb_err ? IC_CLOSE : IC_CLOUD, NULL, 0, jb_label, jb_result);
    }
    if (jb_quiet) jb_ended = 0;
  }
  if (!g_token[0] || !S.me[0] || !S.synced_once) return;
  static char loaded_for[32];
  if (strcmp(loaded_for, S.me)) { snprintf(loaded_for, sizeof loaded_for, "%s", S.me); key_load(S.me); auto_next = SDL_GetTicks() + 20000; }
  // all'avvio di Omega (cioè quasi sempre dopo una partita) e poi ogni 10 minuti
  if (auto_on() && key_ok && SDL_GetTicks() > auto_next && SDL_AtomicGet(&st) == 0) {
    auto_next = SDL_GetTicks() + 10 * 60 * 1000;
    start(J_AUTO, NULL, NULL, NULL, _("Salvataggi online"));
  }
}

int saves_view(InstallView *v) {
  memset(v, 0, sizeof *v);
  if (SDL_AtomicGet(&st) == 1 && jb_phase[0] && strcmp(jb_phase, _("Protezione della chiave")) && strcmp(jb_phase, _("Sblocco della chiave"))) {
    v->active = 1; snprintf(v->name, sizeof v->name, "%s", jb_label);
    long long d = jb_done, t = jb_total;
    v->prog = t > 0 ? clampf((float)d / (float)t, 0, 1) : -1;
    if (t > 0) snprintf(v->phase, sizeof v->phase, "%s  \xC2\xB7  %d%%", jb_phase, (int)(v->prog * 100)); else snprintf(v->phase, sizeof v->phase, "%s...", jb_phase);
    return 1;
  }
  if (jb_ended && SDL_GetTicks() - jb_ended < (jb_err ? 30000u : 10000u)) {
    v->ended = jb_ended; v->err = jb_err; v->prog = 1; snprintf(v->name, sizeof v->name, "%s", jb_label); snprintf(v->result, sizeof v->result, "%s", jb_result);
    return 1;
  }
  return 0;
}

// --------------------------------------------------------------- pannello --
static int title_count(void) { return nT; }

void saves_open(const char *tid) {
  if (!mx) mx = SDL_CreateMutex();
  if (S.me[0]) key_load(S.me);
  focus_tid[0] = 0; if (tid) snprintf(focus_tid, sizeof focus_tid, "%s", tid);
  sel = 0; scroll = 0;
  if (!loaded || SDL_AtomicGet(&st) != 1) { srv_key = -1; start(J_REFRESH, NULL, NULL, NULL, NULL); }
  if (ov_top() != OV_SAVES) ov_push(OV_SAVES);
}

static int need_setup(void) { return srv_key == 0 && !key_ok; }
static int need_unlock(void) { return srv_key == 1 && (!key_ok || strcmp(key_id, srv_key_id)); }

static void ask_new_pass(int kind) {
  char p1[256] = "", p2[256] = "";
  if (!edit_text(_("Scegli una parola d'ordine per i salvataggi (almeno 8 caratteri)"), p1, sizeof p1, 1)) return;
  if (strlen(p1) < 8) { set_msg(_("Almeno 8 caratteri"), 1); crypto_wipe(p1, sizeof p1); return; }
  if (!edit_text(_("Ripeti la parola d'ordine"), p2, sizeof p2, 1)) { crypto_wipe(p1, sizeof p1); return; }
  if (strcmp(p1, p2)) set_msg(_("Le due parole d'ordine non coincidono"), 1);
  else start(kind, NULL, NULL, p1, _("Salvataggi online"));
  crypto_wipe(p1, sizeof p1); crypto_wipe(p2, sizeof p2);
}
static void ask_unlock(void) {
  char p[256] = "";
  if (edit_text(_("Parola d'ordine dei salvataggi online"), p, sizeof p, 1) && p[0]) start(J_UNLOCK, NULL, NULL, p, _("Salvataggi online"));
  crypto_wipe(p, sizeof p);
}
static void reset_yes(int idx, void *ud) { (void)ud; if (idx == 0) ask_new_pass(J_RESET); }

static char pick_tid[16]; static char pick_ids[3][40];
static void restore_yes(int idx, void *ud) {
  int k = (int)(intptr_t)ud; if (idx != 0) return;
  start(J_RESTORE, pick_tid, pick_ids[k], NULL, NULL);
  for (int i = 0; i < nT; i++) if (!strcmp(T[i].tid, pick_tid)) snprintf(jb_label, sizeof jb_label, "%s", T[i].name);
}
static void ver_pick(int idx, void *ud) {
  int del = (int)(intptr_t)ud; if (idx < 0 || idx > 2 || !pick_ids[idx][0]) return;
  if (del) { start(J_DELETE, pick_tid, pick_ids[idx], NULL, NULL); return; }
  static char q[400];
  snprintf(q, sizeof q, "%s", _("Ripristinare questa versione? I salvataggi sulla console vengono sostituiti: ne tengo una copia, così puoi annullare."));
  confirm_open(q, _("Sì, ripristina"), restore_yes, (void *)(intptr_t)idx);
}
static void versions_menu(SaveTitle *t, int del) {
  static char lab[3][160]; static const char *it[3];
  snprintf(pick_tid, sizeof pick_tid, "%s", t->tid);
  int n = 0;
  for (int k = 0; k < t->nver && k < 3; k++) {
    char when[64] = "", sz[24]; rel_time(t->ver[k].at, when, sizeof when); fmt_bytes(t->ver[k].size, sz, sizeof sz);
    snprintf(lab[n], sizeof lab[n], "%s  \xC2\xB7  %s  \xC2\xB7  %s%s", when, sz, t->ver[k].device[0] ? t->ver[k].device : "PS5", k == 0 ? _("  (la più recente)") : "");
    snprintf(pick_ids[n], sizeof pick_ids[n], "%s", t->ver[k].id); it[n] = lab[n]; n++;
  }
  for (int k = n; k < 3; k++) pick_ids[k][0] = 0;
  menu_open(del ? _("Quale versione cancellare dallo spazio online?") : _("Quale versione ripristinare?"), it, n, ver_pick, (void *)(intptr_t)del);
}
static void title_pick(int idx, void *ud) {
  (void)ud; if (sel >= nT) return;
  SaveTitle *t = &T[sel];
  static int acts[5]; int n = 0;
  if (t->ndirs) acts[n++] = 0;
  if (t->nver) acts[n++] = 1;
  if (t->undo) acts[n++] = 2;
  if (t->nver) acts[n++] = 3;
  if (idx < 0 || idx >= n) return;
  switch (acts[idx]) {
    case 0: start(J_UPLOAD, t->tid, NULL, NULL, t->name); break;
    case 1: versions_menu(t, 0); break;
    case 2: start(J_UNDO, t->tid, NULL, NULL, t->name); break;
    case 3: versions_menu(t, 1); break;
  }
}
static void title_menu(SaveTitle *t) {
  static const char *it[5]; int n = 0;
  if (t->ndirs) it[n++] = _("Carica online ora");
  if (t->nver) it[n++] = _("Ripristina da online...");
  if (t->undo) it[n++] = _("Annulla l'ultimo ripristino");
  if (t->nver) it[n++] = _("Cancella una versione online...");
  if (!n) return;
  menu_open(t->name, it, n, title_pick, NULL);
}
static void auto_toggle(void) {
  int n = 0; const PrefDef *tb = prefs_table(&n);
  for (int i = 0; i < n; i++) if (!strcmp(tb[i].key, "save_auto")) pref_set(&tb[i], !g_prefs.save_auto);
  set_msg(g_prefs.save_auto ? _("Caricamento automatico attivo: dopo ogni partita i salvataggi nuovi vanno online") : _("Caricamento automatico spento"), 0);
}
static void opts_pick(int idx, void *ud) {
  (void)ud;
  if (idx == 0) auto_toggle();
  else if (idx == 1) ask_new_pass(J_REPASS);
  else if (idx == 2) confirm_open(_("Senza la parola d'ordine i salvataggi online non si possono aprire, da nessuno. Creo una chiave nuova e cancello quelli online? Quelli sulla console restano."), _("Crea chiave nuova"), reset_yes, NULL);
  else if (idx == 3) start(J_REFRESH, NULL, NULL, NULL, NULL);
}

void saves_draw(float t) {
  int a = (int)(255 * t);
  fill_rect(0, 0, SCREEN_W, SCREEN_H, mix(RGB(10, 12, 20), g_theme_base, 0.12f), t > 0.98f ? 255 : a);
  int x0 = 110, w = SCREEN_W - 2 * x0;
  draw_icon(IC_CLOUD, x0 + 22, 74, 44, C_ACC2, a);
  draw_text(font(W_LIGHT, 38), _("Salvataggi online"), x0 + 62, 52, C_WHITE, a, AL_L);
  int sx = x0 + 62 + text_w(font(W_LIGHT, 38), _("Salvataggi online")) + 48;
  draw_icon(IC_SHIELD, sx, 74, 24, C_OK, a);
  draw_text_fit(font(W_REG, 21), _("Cifrati sulla console: nessuno può leggerli, nemmeno il server"), sx + 22, 62, x0 + w - sx - 22, C_DIM, a, AL_L);
  int busy = SDL_AtomicGet(&st) == 1;
  if (srv_key == -2) { draw_text_wrap(font(W_REG, 26), _("Il server non ha ancora questa funzione: riprova più tardi."), SCREEN_W / 2 - 500, 400, 1000, 3, 36, C_DIM, a); hints((int[]){ IC_BTN_O }, (const char *[]){ _("Indietro") }, 1, a); return; }
  if (srv_key == -1 && !loaded) { draw_spinner(SCREEN_W / 2, 420, 22, a); return; }
  // --- attivazione o sblocco
  if (need_setup() || need_unlock()) {
    int cw = 1100, cx = SCREEN_W / 2 - cw / 2, cy = 200;
    fill_rrect(cx, cy, cw, 560, 32, C_WHITE, a * 6 / 100);
    fill_circle(SCREEN_W / 2, cy + 110, 60, C_ACC, a);
    draw_icon(need_setup() ? IC_CLOUD : IC_SHIELD, SCREEN_W / 2, cy + 110, 56, C_WHITE, a);
    draw_text(font(W_LIGHT, 36), need_setup() ? _("Porta i tuoi salvataggi online") : _("Sblocca i tuoi salvataggi online"), SCREEN_W / 2, cy + 196, C_WHITE, a, AL_C);
    const char *txt = need_setup()
      ? _("Dopo ogni partita i salvataggi nuovi vanno online da soli, e li ripristini su questa o su un'altra console. Sono cifrati qui con una chiave protetta dalla tua parola d'ordine: il server riceve solo dati illeggibili. Se dimentichi la parola d'ordine, nessuno può recuperarli.")
      : _("Hai attivato i salvataggi online su un'altra console. Scrivi la parola d'ordine che hai scelto allora: la chiave si sblocca qui sulla console e non viaggia mai in chiaro.");
    draw_text_wrap(font(W_REG, 24), txt, cx + 80, cy + 260, cw - 160, 6, 34, C_DIM, a);
    if (busy) { draw_spinner(SCREEN_W / 2, cy + 490, 20, a); draw_text(font(W_REG, 22), jb_phase[0] ? jb_phase : _("Attendi..."), SCREEN_W / 2, cy + 520, C_DIM, a, AL_C); }
    else {
      int bw = 420; fill_rrect(SCREEN_W / 2 - bw / 2, cy + 460, bw, 64, 32, C_WHITE, a);
      draw_text(font(W_MED, 26), need_setup() ? _("Attiva") : _("Scrivi la parola d'ordine"), SCREEN_W / 2, cy + 476, RGB(12, 14, 22), a, AL_C);
    }
    int ic[3]; const char *lb[3]; int n = 0;
    ic[n] = IC_BTN_X; lb[n++] = need_setup() ? _("Attiva") : _("Sblocca");
    if (need_unlock()) { ic[n] = IC_BTN_SQ; lb[n++] = _("Ho dimenticato la parola d'ordine"); }
    ic[n] = IC_BTN_O; lb[n++] = _("Indietro");
    hints(ic, lb, n, a);
    return;
  }
  // --- riepilogo: spazio online e automatico
  int cy = 130;
  fill_rrect(x0, cy, 640, 96, 20, C_WHITE, a * 6 / 100);
  draw_icon(IC_CLOUD, x0 + 40, cy + 34, 28, C_ACC2, a);
  char ub[24], lb2[24], line[120]; fmt_bytes(q_used, ub, sizeof ub); fmt_bytes(q_limit, lb2, sizeof lb2);
  snprintf(line, sizeof line, _("Spazio online: %s di %s"), ub, lb2);
  draw_text_fit(font(W_MED, 23), line, x0 + 72, cy + 18, 540, C_TXT, a, AL_L);
  float k = q_limit > 0 ? (float)q_used / (float)q_limit : 0;
  fill_rrect(x0 + 24, cy + 64, 592, 10, 5, C_WHITE, a * 14 / 100); fill_rrect(x0 + 24, cy + 64, (int)(592 * clampf(k, 0, 1)), 10, 5, k > 0.9f ? C_ERR : C_ACC2, a);
  int ax = x0 + 660, aw = w - 660;
  fill_rrect(ax, cy, aw, 96, 20, C_WHITE, a * 6 / 100);
  draw_icon(IC_WAVE, ax + 40, cy + 48, 28, g_prefs.save_auto ? C_OK : C_DIM, a);
  draw_text_fit(font(W_MED, 23), g_prefs.save_auto ? _("Caricamento automatico: attivo") : _("Caricamento automatico: spento"), ax + 72, cy + 18, aw - 100, C_TXT, a, AL_L);
  draw_text_fit(font(W_REG, 19), _("Dopo ogni partita i salvataggi cambiati vanno online. \xE2\x96\xB3 per cambiare"), ax + 72, cy + 52, aw - 100, C_DIM, a, AL_L);
  // --- giochi
  int top = 262, rh = 104, bottom = SCREEN_H - 130;
  if (mx) SDL_LockMutex(mx);
  if (focus_tid[0]) { for (int i = 0; i < nT; i++) if (!strcmp(T[i].tid, focus_tid)) { sel = i; focus_tid[0] = 0; break; } }
  if (sel >= nT) sel = nT ? nT - 1 : 0;
  if (!nT) {
    draw_text(font(W_REG, 26), loaded ? _("Nessun salvataggio trovato per questo utente") : _("Cerco i salvataggi..."), SCREEN_W / 2, top + 90, C_FAINT, a, AL_C);
    if (loaded) draw_text(font(W_REG, 21), _("Gioca e salva in un gioco: lo vedrai qui"), SCREEN_W / 2, top + 132, C_FAINT, a, AL_C);
  }
  anim = approach(anim, (float)sel, 20.0f);
  float tgt = sel * rh + rh > bottom - top ? (float)(sel * rh + rh - (bottom - top) + 20) : 0;
  scroll = approach(scroll, tgt, 14.0f);
  SDL_Rect clip = { 0, top - 8, SCREEN_W, bottom - top + 8 }; SDL_RenderSetClipRect(R, &clip);
  for (int i = 0; i < nT; i++) {
    int y = top + i * rh - (int)scroll;
    if (y + rh < top || y > bottom) continue;
    SaveTitle *s = &T[i];
    float fa = clampf(1 - fabsf(anim - i), 0, 1);
    fill_rrect(x0, y, w, rh - 14, 18, C_WHITE, (int)(a * (0.05f + 0.08f * fa)));
    if (i == sel) stroke_rrect(x0 - 3, y - 3, w + 6, rh - 8, 21, 3, C_WHITE, a);
    int ai = -1; for (int q = 0; q < napps; q++) if (!strcmp(apps[q].tid, s->tid)) { ai = q; break; }
    if (ai >= 0) home_tile(ai, x0 + 10, y + 8, rh - 30, a);
    else { fill_rrect(x0 + 10, y + 8, rh - 30, rh - 30, 14, C_WHITE, a * 8 / 100); draw_icon(IC_GAMEPAD, x0 + 10 + (rh - 30) / 2, y + 8 + (rh - 30) / 2, 30, C_DIM, a); }
    int tx = x0 + rh + 4;
    draw_text_fit(font(W_MED, 27), s->name, tx, y + 12, w - 900, C_TXT, a, AL_L);
    char sub[200], sz[24], ago[48];
    if (s->ndirs) { fmt_bytes(s->size, sz, sizeof sz); fmt_ago(s->mtime, ago, sizeof ago); snprintf(sub, sizeof sub, s->ndirs == 1 ? _("Sulla console: %d salvataggio \xC2\xB7 %s \xC2\xB7 %s") : _("Sulla console: %d salvataggi \xC2\xB7 %s \xC2\xB7 %s"), s->ndirs, sz, ago); }
    else snprintf(sub, sizeof sub, "%s", _("Non è sulla console: avvia il gioco e salva per poterlo ripristinare"));
    draw_text_fit(font(W_REG, 20), sub, tx, y + 50, w - 900, s->ndirs ? C_DIM : C_FAINT, a, AL_L);
    // stato online a destra
    int rx = x0 + w - 640;
    if (s->nver) {
      char when[64] = ""; rel_time(s->ver[0].at, when, sizeof when);
      char l1[120]; snprintf(l1, sizeof l1, _("Online: %s"), when);
      draw_icon(IC_CLOUD, rx + 16, y + 30, 22, C_ACC2, a);
      draw_text_fit(font(W_MED, 22), l1, rx + 38, y + 16, 380, C_TXT, a, AL_L);
      char l2[80]; snprintf(l2, sizeof l2, s->nver == 1 ? _("%d versione") : _("%d versioni"), s->nver);
      draw_text(font(W_REG, 19), l2, rx + 38, y + 50, C_DIM, a, AL_L);
    } else draw_text(font(W_REG, 21), _("Non ancora online"), rx + 16, y + 30, C_FAINT, a, AL_L);
    const char *badge = NULL; Col bc = C_ACC;
    if (s->ndirs && s->mtime > s->synced && s->nver) { badge = _("Da caricare"); bc = C_WARN; }
    else if (s->ndirs && !s->nver) { badge = _("Da caricare"); bc = C_WARN; }
    else if (s->ndirs && s->nver) { badge = _("Sincronizzato"); bc = C_OK; }
    if (s->undo) { badge = _("Ripristinato"); bc = C_ACC2; }
    if (badge) {
      TTF_Font *bf = font(W_MED, 19); int bw = text_w(bf, badge) + 32;
      fill_rrect(x0 + w - 30 - bw, y + 26, bw, 38, 19, bc, a * 85 / 100);
      draw_text(bf, badge, x0 + w - 30 - bw / 2, y + 34, RGB(12, 14, 22), a, AL_C);
    }
  }
  SDL_RenderSetClipRect(R, NULL);
  if (mx) SDL_UnlockMutex(mx);
  // barra del lavoro in corso
  InstallView v;
  if (busy && saves_view(&v) && v.active) {
    int by = SCREEN_H - 118;
    draw_text_fit(font(W_REG, 20), v.phase, x0, by - 30, 900, C_DIM, a, AL_L);
    fill_rrect(x0, by, w, 8, 4, C_WHITE, a * 14 / 100);
    if (v.prog >= 0) fill_rrect(x0, by, (int)(w * v.prog), 8, 4, C_ACC2, a);
  }
  int ic[4]; const char *lb[4]; int n = 0;
  if (nT) { ic[n] = IC_BTN_X; lb[n++] = _("Opzioni del gioco"); }
  ic[n] = IC_BTN_TRI; lb[n++] = _("Automatico");
  ic[n] = IC_BTN_SQ; lb[n++] = _("Chiave e altro");
  ic[n] = IC_BTN_O; lb[n++] = _("Indietro");
  hints(ic, lb, n, a);
}

void saves_input(int b) {
  if (b == B_O) { ov_pop(); return; }
  if (srv_key == -2) return;
  int busy = SDL_AtomicGet(&st) == 1;
  if (need_setup() || need_unlock()) {
    if (busy) return;
    if (b == B_X) { if (need_setup()) ask_new_pass(J_SETUP); else ask_unlock(); }
    else if (b == B_SQ && need_unlock()) confirm_open(_("Senza la parola d'ordine i salvataggi online non si possono aprire, da nessuno. Creo una chiave nuova e cancello quelli online? Quelli sulla console restano."), _("Crea chiave nuova"), reset_yes, NULL);
    return;
  }
  if (b == B_UP && sel > 0) { sel--; sfx_play(SFX_MOVE); }
  else if (b == B_DOWN && sel < title_count() - 1) { sel++; sfx_play(SFX_MOVE); }
  else if (b == B_X && sel < nT) title_menu(&T[sel]);
  else if (b == B_TRI) auto_toggle();
  else if (b == B_SQ) {
    static const char *it[4];
    it[0] = g_prefs.save_auto ? _("Spegni il caricamento automatico") : _("Accendi il caricamento automatico");
    it[1] = _("Cambia la parola d'ordine"); it[2] = _("Ho dimenticato la parola d'ordine"); it[3] = _("Aggiorna l'elenco");
    menu_open(_("Salvataggi online"), it, 4, opts_pick, NULL);
  }
}
