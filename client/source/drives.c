// Omega UI — giochi sui dischi esterni (HDD/SSD USB, chiavette, M.2).
//
// Un thread guarda ogni 4 s quali dischi sono collegati (/mnt/usb0..7,
// /mnt/ext0 archivio esteso, /mnt/ext1 M.2) e cerca le cartelle di gioco nei
// posti usati dagli strumenti della scena: la radice del disco, homebrew/,
// etaHEN/games/, games/, PS5/, PS4/ (un livello sotto). Una cartella è un gioco
// se ha sce_sys/param.json (PS5) o sce_sys/param.sfo (PS4) e eboot.bin.
// I giochi trovati compaiono in home con l'icona del disco (home.c, drives_merge);
// se il disco si scollega spariscono, senza toccare niente.
//
// Avvio (come ShadowMount / dump_runner, letti dal loro codice): la cartella si
// monta in SOLA LETTURA (nullfs) su /system_ex/app/<TID>; la prima volta si
// copiano i metadati (sce_sys) in /user/app/<TID> e si registra il titolo con
// sceAppInstUtilAppInstallTitleDir. Quando il gioco non è più in esecuzione
// (ritorno in Omega) si smonta, così scollegare il disco è sicuro.
// Sul disco esterno non si scrive e non si cancella mai niente.
//
// In più:
//  · cartelle scelte dall'utente (Impostazioni › Giochi e PKG, paths.txt):
//    giochi anche sulla memoria interna o in percorsi non standard;
//  · montaggio automatico (Personalizza › Home): appena un disco compare i suoi
//    giochi si montano e si registrano da soli, come fa ShadowMount, quindi
//    compaiono anche nella Home della console; se il disco sparisce si smontano;
//  · si guardano anche /user/appmeta, /system_ex/app, /user/app e le cartelle
//    degli homebrew: un gioco montato da ShadowMount, un pkg appena installato o
//    un homebrew caricato dal telefono compaiono in home senza riavviare Omega.
#include "app.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/uio.h>
#include <sys/mount.h>
#include <unistd.h>

#define MAX_EXT 128
typedef struct { char tid[16], name[96], src[300], icon[320], art[320], drive[40]; } ExtGame;

static ExtGame found[MAX_EXT]; static int nfound;     // ultimo risultato (sotto mx)
static SDL_mutex *mx; static SDL_atomic_t changed, wake;
static unsigned last_sig, last_apps_sig;

// --------------------------------------------------------------- dischi --
#ifdef PS5
#define ROOT ""
#else
#define ROOT OMEGA_DIR "/sysroot"                    // prova sul Mac: dischi finti
#endif
static const struct { const char *path, *label; int num; } MOUNTS[] = {
  { "/mnt/usb0", N_("Disco USB %d"), 1 }, { "/mnt/usb1", N_("Disco USB %d"), 2 }, { "/mnt/usb2", N_("Disco USB %d"), 3 }, { "/mnt/usb3", N_("Disco USB %d"), 4 },
  { "/mnt/usb4", N_("Disco USB %d"), 5 }, { "/mnt/usb5", N_("Disco USB %d"), 6 }, { "/mnt/usb6", N_("Disco USB %d"), 7 }, { "/mnt/usb7", N_("Disco USB %d"), 8 },
  { "/mnt/ext0", N_("Archivio esteso"), 0 }, { "/mnt/ext1", N_("SSD M.2"), 0 },
};
#define NMOUNTS (int)(sizeof MOUNTS / sizeof *MOUNTS)

static int mounted(const char *p, double *free_gb, double *total_gb) {
  struct statfs st;
  if (statfs(p, &st) != 0 || st.f_blocks == 0) return 0;
#ifdef PS5
  // un punto di mount vuoto sta sul disco interno: non è un disco collegato
  struct statfs root; if (statfs("/user", &root) == 0 && root.f_fsid.val[0] == st.f_fsid.val[0] && root.f_fsid.val[1] == st.f_fsid.val[1]) return 0;
#endif
  if (free_gb) *free_gb = (double)st.f_bavail * st.f_bsize / 1e9;
  if (total_gb) *total_gb = (double)st.f_blocks * st.f_bsize / 1e9;
  return 1;
}

static void drive_label(int i, char *out, size_t n) {
  if (MOUNTS[i].num) snprintf(out, n, _(MOUNTS[i].label), MOUNTS[i].num); else snprintf(out, n, "%s", _(MOUNTS[i].label));
}

int drives_list(Drive *out, int max) {
  int n = 0;
  for (int i = 0; i < NMOUNTS && n < max; i++) {
    char p[96]; snprintf(p, sizeof p, ROOT "%s", MOUNTS[i].path);
    double fr = 0, tot = 0;
    if (!mounted(p, &fr, &tot)) continue;
    snprintf(out[n].mount, sizeof out[n].mount, "%s", p);
    drive_label(i, out[n].label, sizeof out[n].label);
    out[n].free_gb = fr; out[n].total_gb = tot;
    n++;
  }
  return n;
}

// ------------------------------------------------- cartelle dell'utente --
// OMEGA_DIR/paths.txt: una riga per cartella, "game <percorso>" o "pkg <percorso>".
#define PATHS_FILE OMEGA_DIR "/paths.txt"
static SDL_mutex *pmx;
static void pm_lock(int on) { if (!pmx) pmx = SDL_CreateMutex(); if (on) SDL_LockMutex(pmx); else SDL_UnlockMutex(pmx); }

int paths_list(int kind, char (*out)[300], int max) {
  pm_lock(1);
  int n = 0; FILE *f = fopen(PATHS_FILE, "r");
  if (f) {
    char line[400]; const char *pre = kind ? "pkg " : "game "; size_t pl = strlen(pre);
    while (n < max && fgets(line, sizeof line, f)) {
      line[strcspn(line, "\r\n")] = 0;
      if (strncmp(line, pre, pl) || !line[pl]) continue;
      snprintf(out[n++], 300, "%s", line + pl);
    }
    fclose(f);
  }
  pm_lock(0);
  return n;
}
// aggiunge (add=1) o toglie una cartella; 1 fatto, 0 niente da fare, -1 errore
int paths_set(int kind, const char *path, int add) {
  static char g[32][300], k[32][300];
  if (!path || !path[0]) return -1;
  char p[300]; snprintf(p, sizeof p, "%s", path);
  size_t L = strlen(p); while (L > 1 && p[L - 1] == '/') p[--L] = 0;
  int ng = paths_list(0, g, 32), nk = paths_list(1, k, 32);
  char (*l)[300] = kind ? k : g; int *n = kind ? &nk : &ng, at = -1;
  for (int i = 0; i < *n; i++) if (!strcmp(l[i], p)) at = i;
  if (add) { if (at >= 0) return 0; if (*n >= 32) return -1; snprintf(l[(*n)++], 300, "%s", p); }
  else { if (at < 0) return 0; memmove(l[at], l[at + 1], (size_t)(*n - at - 1) * 300); (*n)--; }
  pm_lock(1);
  FILE *f = fopen(PATHS_FILE ".tmp", "w");
  if (f) {
    for (int i = 0; i < ng; i++) fprintf(f, "game %s\n", g[i]);
    for (int i = 0; i < nk; i++) fprintf(f, "pkg %s\n", k[i]);
    fclose(f); rename(PATHS_FILE ".tmp", PATHS_FILE);
  }
  pm_lock(0);
  SDL_AtomicSet(&wake, 1);   // il controllo dei dischi riparte subito
  return f ? 1 : -1;
}

// cartelle della memoria interna dove gli strumenti della scena mettono i giochi
static const char *INTERNAL_GAMES[] = { "/data/etaHEN/games", "/data/games", "/data/homebrew-games" };

// -------------------------------------------------------- param.json/sfo --
static char *slurp(const char *p, size_t max, size_t *len) {
  int fd = open(p, O_RDONLY); if (fd < 0) return NULL;
  char *b = malloc(max + 1); if (!b) { close(fd); return NULL; }
  ssize_t k = read(fd, b, max); close(fd);
  if (k <= 0) { free(b); return NULL; }
  b[k] = 0; if (len) *len = (size_t)k; return b;
}
static int tid_ok(const char *t) {
  if (strlen(t) != 9) return 0;
  for (int i = 0; i < 9; i++) if (!(i < 4 ? (t[i] >= 'A' && t[i] <= 'Z') : (t[i] >= '0' && t[i] <= '9'))) return 0;
  return 1;
}
static int read_json(const char *p, char *tid, char *name, size_t nn) {
  char *b = slurp(p, 256 * 1024, NULL); if (!b) return 0;
  JVal *j = json_parse(b); free(b);
  if (!j) return 0;
  snprintf(tid, 16, "%s", jstr(j, "titleId", ""));
  JVal *lp = jget(j, "localizedParameters");
  const char *def = jstr(lp, "defaultLanguage", "en-US");
  const char *langs[3] = { i18n_locale(), def, "en-US" };
  name[0] = 0;
  for (int i = 0; i < 3 && !name[0]; i++) { const char *t = jstr(jget(lp, langs[i]), "titleName", NULL); if (t && *t) snprintf(name, nn, "%s", t); }
  json_free(j);
  return tid_ok(tid);
}
static int read_sfo(const char *p, char *tid, char *name, size_t nn) {
  size_t len = 0; char *b = slurp(p, 64 * 1024, &len); if (!b) return 0;
  tid[0] = name[0] = 0;
  if (len > 20 && !memcmp(b, "\0PSF", 4)) {
    uint32_t keyt = *(uint32_t *)(b + 8), datat = *(uint32_t *)(b + 12), cnt = *(uint32_t *)(b + 16);
    for (uint32_t i = 0; i < cnt && 20 + i * 16 + 16 <= len; i++) {
      const unsigned char *e = (const unsigned char *)b + 20 + i * 16;
      uint16_t ko = *(const uint16_t *)e; uint32_t dl = *(const uint32_t *)(e + 4), doff = *(const uint32_t *)(e + 12);
      if (keyt + ko >= len || datat + doff + dl > len) continue;
      if (!strcmp(b + keyt + ko, "TITLE_ID")) snprintf(tid, 16, "%.*s", (int)(dl < 15 ? dl : 15), b + datat + doff);
      if (!strcmp(b + keyt + ko, "TITLE")) snprintf(name, nn, "%.*s", (int)dl, b + datat + doff);
    }
  }
  free(b);
  tid[9] = 0;
  return tid_ok(tid);
}

// una cartella: è un gioco? (sce_sys/param.json o param.sfo, ed eboot.bin)
static void probe(const char *dir, const char *drive, ExtGame *list, int *n) {
  if (*n >= MAX_EXT) return;
  char p[400], tid[16] = "", name[96] = "";
  snprintf(p, sizeof p, "%s/eboot.bin", dir);
  if (access(p, 0) != 0) return;
  snprintf(p, sizeof p, "%s/sce_sys/param.json", dir);
  int ok = access(p, 0) == 0 && read_json(p, tid, name, sizeof name);
  if (!ok) { snprintf(p, sizeof p, "%s/sce_sys/param.sfo", dir); ok = access(p, 0) == 0 && read_sfo(p, tid, name, sizeof name); }
  if (!ok) return;
  for (int i = 0; i < *n; i++) if (!strcmp(list[i].tid, tid)) return;   // lo stesso gioco su due dischi: il primo
  ExtGame *g = &list[(*n)++]; memset(g, 0, sizeof *g);
  snprintf(g->tid, sizeof g->tid, "%s", tid);
  snprintf(g->name, sizeof g->name, "%s", name[0] ? name : tid);
  snprintf(g->src, sizeof g->src, "%s", dir);
  snprintf(g->drive, sizeof g->drive, "%s", drive);
  snprintf(p, sizeof p, "%s/sce_sys/icon0.png", dir); if (access(p, 0) == 0) snprintf(g->icon, sizeof g->icon, "%s", p);
  const char *pics[3] = { "pic1.png", "pic0.png", "pic2.png" };
  for (int k = 0; k < 3 && !g->art[0]; k++) { snprintf(p, sizeof p, "%s/sce_sys/%s", dir, pics[k]); if (access(p, 0) == 0) snprintf(g->art, sizeof g->art, "%s", p); }
}

static void scan_dir(const char *base, const char *drive, ExtGame *list, int *n) {
  DIR *d = opendir(base); if (!d) return;
  struct dirent *e;
  while ((e = readdir(d)) && *n < MAX_EXT) {
    if (e->d_name[0] == '.' || !strcasecmp(e->d_name, "backports") || !strcasecmp(e->d_name, "System Volume Information")) continue;
    char p[400]; snprintf(p, sizeof p, "%s/%s", base, e->d_name);
    struct stat st; if (stat(p, &st) != 0 || !S_ISDIR(st.st_mode)) continue;
    probe(p, drive, list, n);
  }
  closedir(d);
}

static void scan_all(ExtGame *list, int *n) {
  static const char *SUB[] = { "", "/homebrew", "/etaHEN/games", "/games", "/Games", "/PS5", "/PS4", "/ShadowMount", "/backups" };
  *n = 0;
  Drive dr[16]; int nd = drives_list(dr, 16);
  for (int i = 0; i < nd; i++)
    for (unsigned k = 0; k < sizeof SUB / sizeof *SUB; k++) { char b[200]; snprintf(b, sizeof b, "%s%s", dr[i].mount, SUB[k]); scan_dir(b, dr[i].label, list, n); }
  for (unsigned k = 0; k < sizeof INTERNAL_GAMES / sizeof *INTERNAL_GAMES; k++) {
    char b[200]; snprintf(b, sizeof b, ROOT "%s", INTERNAL_GAMES[k]);
    scan_dir(b, _("Memoria interna"), list, n);
  }
  // cartelle scelte dall'utente: la cartella stessa può essere un gioco, o contenerne
  static char cp[32][300]; int nc = paths_list(0, cp, 32);
  for (int i = 0; i < nc; i++) {
    struct stat st; if (stat(cp[i], &st) != 0 || !S_ISDIR(st.st_mode)) continue;   // disco scollegato
    char lab[40]; snprintf(lab, sizeof lab, "%s", _("Memoria interna"));
    for (int d = 0; d < nd; d++) if (!strncmp(cp[i], dr[d].mount, strlen(dr[d].mount))) snprintf(lab, sizeof lab, "%s", dr[d].label);
    probe(cp[i], lab, list, n);
    scan_dir(cp[i], lab, list, n);
  }
}

// Firma dei titoli installati e degli homebrew: cambia quando ShadowMount monta
// un gioco, un pkg finisce di installarsi o arriva un homebrew dal telefono.
// Somma delle firme dei nomi, così non dipende dall'ordine di readdir.
static unsigned apps_sig(void) {
  static const char *DIRS[] = { ROOT "/user/appmeta", ROOT "/system_ex/app", ROOT "/user/app", OMEGA_HB_ROOT, OMEGA_PLD_ROOT };
  unsigned h = 2166136261u;
  for (unsigned k = 0; k < sizeof DIRS / sizeof *DIRS; k++) {
    DIR *d = opendir(DIRS[k]); if (!d) continue;
    struct dirent *e; unsigned acc = 0;
    while ((e = readdir(d))) {
      if (e->d_name[0] == '.') continue;
      unsigned x = 2166136261u; for (const char *c = e->d_name; *c; c++) { x ^= (unsigned char)*c; x *= 16777619u; }
      acc += x;
    }
    closedir(d);
    h ^= acc + k; h *= 16777619u;
  }
  return h;
}

static void automount_tick(ExtGame *list, int n);

static int scan_thread(void *arg) {
  (void)arg;
  static ExtGame tmp[MAX_EXT];
  for (;;) {
    int n = 0;
    if (g_prefs.ext_games) scan_all(tmp, &n);
    // firma dell'elenco: se cambia (disco collegato o tolto) la home si rifà
    unsigned sig = 2166136261u;
    for (int i = 0; i < n; i++) for (const char *c = tmp[i].src; *c; c++) { sig ^= (unsigned char)*c; sig *= 16777619u; }
    sig ^= (unsigned)n;
    if (sig != last_sig) {
      SDL_LockMutex(mx); memcpy(found, tmp, sizeof(ExtGame) * (size_t)n); nfound = n; SDL_UnlockMutex(mx);
      last_sig = sig; SDL_AtomicSet(&changed, 1);
      omega_log("dischi esterni: %d giochi", n);
    }
    if (g_prefs.ext_games && g_prefs.automount) automount_tick(tmp, n);
    unsigned as = apps_sig();
    if (as != last_apps_sig) {
      if (last_apps_sig) { SDL_AtomicSet(&changed, 1); omega_log("titoli o homebrew cambiati: si rifà la home"); }
      last_apps_sig = as;
    }
    // ogni 3 s, subito se una cartella è appena stata aggiunta o tolta
    for (int i = 0; i < 30 && !SDL_AtomicGet(&wake); i++) SDL_Delay(100);
    SDL_AtomicSet(&wake, 0);
  }
  return 0;
}

static void ensure(void) {
  if (mx) return;
  mx = SDL_CreateMutex();
  SDL_Thread *t = SDL_CreateThread(scan_thread, "drives", NULL);
  if (t) SDL_DetachThread(t);
}

int drives_merge(AppEntry *apps, int n, int max) {
  ensure();
  SDL_LockMutex(mx);
  for (int i = 0; i < nfound && n < max; i++) {
    int dup = 0; for (int k = 0; k < n; k++) if (!strcmp(apps[k].tid, found[i].tid)) dup = 1;   // installato anche dentro
    if (dup) continue;
    AppEntry *a = &apps[n++]; memset(a, 0, sizeof *a);
    snprintf(a->tid, sizeof a->tid, "%s", found[i].tid); snprintf(a->name, sizeof a->name, "%s", found[i].name);
    snprintf(a->icon, sizeof a->icon, "%s", found[i].icon); snprintf(a->art, sizeof a->art, "%s", found[i].art);
    a->ext = 1; snprintf(a->src, sizeof a->src, "%s", found[i].src); snprintf(a->drive, sizeof a->drive, "%s", found[i].drive);
    snprintf(a->sub, sizeof a->sub, "%s", found[i].drive);
    a->avg = g_theme_base;
  }
  SDL_UnlockMutex(mx);
  return n;
}

// dal ciclo principale: 1 se l'elenco dei giochi esterni è cambiato
int drives_changed(void) { ensure(); return SDL_AtomicSet(&changed, 0); }
void drives_tick(void) { ensure(); }

// ------------------------------------------------------------ avvio --
#ifdef PS5
int sceSystemServiceGetAppIdOfRunningBigApp(void);
int sceSystemServiceGetAppTitleId(int appId, char *titleId);
int nmount(struct iovec *iov, unsigned int niov, int flags);
int unmount(const char *dir, int flags);
int app_register(const char *title_id, const char *dir);   // install.c
#define IOV(s) { (void *)(s), strlen(s) + 1 }

static int remount_system_ex(void) {
  // come ShadowMount: /system_ex in scrittura, per creare il punto di mount
  struct iovec iov[] = { IOV("from"), IOV("/dev/ssd0.system_ex"), IOV("fspath"), IOV("/system_ex"), IOV("fstype"), IOV("exfatfs") };
  return nmount(iov, sizeof iov / sizeof *iov, MNT_UPDATE);
}
static int mount_ro(const char *src, const char *dst) {
  struct iovec iov[] = { IOV("fstype"), IOV("nullfs"), IOV("from"), IOV(src), IOV("fspath"), IOV(dst) };
  return nmount(iov, sizeof iov / sizeof *iov, MNT_RDONLY);
}
static int copy_file(const char *a, const char *b) {
  int in = open(a, O_RDONLY); if (in < 0) return -1;
  int out = open(b, O_WRONLY | O_CREAT | O_TRUNC, 0666); if (out < 0) { close(in); return -1; }
  static char buf[64 * 1024]; ssize_t k; int rc = 0;
  while ((k = read(in, buf, sizeof buf)) > 0) if (write(out, buf, (size_t)k) != k) { rc = -1; break; }
  close(in); close(out); return rc;
}
static void copy_tree(const char *a, const char *b) {
  mkdir(b, 0777);
  DIR *d = opendir(a); if (!d) return;
  struct dirent *e;
  while ((e = readdir(d))) {
    if (e->d_name[0] == '.') continue;
    char pa[600], pb[600]; snprintf(pa, sizeof pa, "%s/%s", a, e->d_name); snprintf(pb, sizeof pb, "%s/%s", b, e->d_name);
    struct stat st; if (stat(pa, &st) != 0) continue;
    if (S_ISDIR(st.st_mode)) copy_tree(pa, pb); else copy_file(pa, pb);
  }
  closedir(d);
}
#endif

int drives_prepare_launch(const AppEntry *a, char *err, size_t en) {
  char p[400];
  snprintf(p, sizeof p, "%s/eboot.bin", a->src);
  if (access(p, 0) != 0) { snprintf(err, en, _("%s non risponde: il disco è ancora collegato?"), a->drive); return -1; }
#ifdef PS5
  char dst[64]; snprintf(dst, sizeof dst, "/system_ex/app/%s", a->tid);
  remount_system_ex();
  mkdir(dst, 0755);
  unmount(dst, 0);                       // mount vecchio rimasto (avvio precedente)
  if (mount_ro(a->src, dst) != 0) { snprintf(err, en, _("Non riesco a collegare il gioco dal disco (errore %d)"), errno); return -1; }
  // prima volta: metadati in /user/app/<TID> e registrazione nel sistema
  char meta[96]; snprintf(meta, sizeof meta, "/user/appmeta/%s", a->tid);
  if (access(meta, 0) != 0) {
    char ua[96], us[128], ic[128], ln[128], ss[400];
    snprintf(ua, sizeof ua, "/user/app/%s", a->tid); mkdir(ua, 0777);
    snprintf(us, sizeof us, "%s/sce_sys", ua); snprintf(ss, sizeof ss, "%s/sce_sys", a->src); copy_tree(ss, us);
    snprintf(ic, sizeof ic, "%s/icon0.png", ua); snprintf(ss, sizeof ss, "%s/sce_sys/icon0.png", a->src); copy_file(ss, ic);
    snprintf(ln, sizeof ln, "%s/mount.lnk", ua);
    FILE *f = fopen(ln, "w"); if (f) { fprintf(f, "%s\n", a->src); fclose(f); }
    int rc = app_register(a->tid, "/user/app/");
    omega_log("dischi: registrazione %s da %s -> 0x%x", a->tid, a->src, rc);
    if (rc != 0 && rc != (int)0x80990002) { unmount(dst, 0); snprintf(err, en, _("La console non ha accettato il gioco (0x%08X): serve kstuff attivo per i giochi su disco"), (unsigned)rc); return -1; }
  }
  omega_log("dischi: %s montato da %s (sola lettura)", a->tid, a->src);
#else
  (void)err; (void)en;
  omega_log("(desktop) gioco esterno %s da %s", a->tid, a->src);
#endif
  FILE *f = fopen(OMEGA_DIR "/ext-mounted.txt", "a"); if (f) { fprintf(f, "%s\n", a->tid); fclose(f); }
  return 0;
}

// All'avvio di Omega (cioè dopo un gioco): si smontano i giochi esterni che non
// sono più in esecuzione, così il disco si può scollegare.
void drives_after_game(void) {
  FILE *f = fopen(OMEGA_DIR "/ext-mounted.txt", "r"); if (!f) return;
  char tid[32], keep[2048] = ""; size_t kl = 0; (void)kl;
#ifdef PS5
  int app = sceSystemServiceGetAppIdOfRunningBigApp(); char run[64] = "";
  if (app >= 0) sceSystemServiceGetAppTitleId(app, run);
#endif
  while (fscanf(f, "%31s", tid) == 1) {
#ifdef PS5
    // col montaggio automatico i giochi restano montati finché c'è il disco
    if (!strcmp(tid, run) || (g_prefs.automount && g_prefs.ext_games)) { kl += (size_t)snprintf(keep + kl, sizeof keep - kl, "%s\n", tid); continue; }
    char dst[64]; snprintf(dst, sizeof dst, "/system_ex/app/%s", tid);
    int rc = unmount(dst, 0);
    omega_log("dischi: smontato %s -> %d", tid, rc);
#endif
  }
  fclose(f);
  f = fopen(OMEGA_DIR "/ext-mounted.txt", "w"); if (f) { fputs(keep, f); fclose(f); }
}

// ---------------------------------------------------- montaggio automatico --
// Come ShadowMount: ogni gioco trovato si monta e si registra subito (così è
// anche nella Home della console); quando la sua cartella sparisce (disco
// tolto) il mount si toglie. Un gioco che non si riesce a montare non si
// riprova finché i dischi non spariscono tutti.
static char am_tid[MAX_EXT][16], am_src[MAX_EXT][300]; static int am_n;
static char am_bad[MAX_EXT][300]; static int am_nbad;
static void automount_tick(ExtGame *list, int n) {
  for (int i = 0; i < am_n; ) {
    char p[400]; snprintf(p, sizeof p, "%s/eboot.bin", am_src[i]);
    if (access(p, 0) == 0) { i++; continue; }
#ifdef PS5
    char dst[64]; snprintf(dst, sizeof dst, "/system_ex/app/%s", am_tid[i]);
    int rc = unmount(dst, MNT_FORCE);
    omega_log("montaggio automatico: %s scollegato, smontato -> %d", am_tid[i], rc);
#endif
    memmove(am_tid[i], am_tid[i + 1], (size_t)(am_n - i - 1) * 16);
    memmove(am_src[i], am_src[i + 1], (size_t)(am_n - i - 1) * 300);
    am_n--;
  }
  if (!n) am_nbad = 0;
  for (int i = 0; i < n && am_n < MAX_EXT; i++) {
    int have = 0;
    for (int k = 0; k < am_n && !have; k++) have = !strcmp(am_tid[k], list[i].tid);
    for (int k = 0; k < am_nbad && !have; k++) have = !strcmp(am_bad[k], list[i].src);
    if (have) continue;
    // c'è già una copia vera nella memoria interna (copiata da qui): niente mount sopra
    { char eb[96]; snprintf(eb, sizeof eb, ROOT "/user/app/%s/eboot.bin", list[i].tid); if (access(eb, 0) == 0) continue; }
    AppEntry a; memset(&a, 0, sizeof a);
    snprintf(a.tid, sizeof a.tid, "%s", list[i].tid); snprintf(a.src, sizeof a.src, "%s", list[i].src); snprintf(a.drive, sizeof a.drive, "%s", list[i].drive);
    char err[300];
    if (drives_prepare_launch(&a, err, sizeof err) == 0) {
      snprintf(am_tid[am_n], 16, "%s", a.tid); snprintf(am_src[am_n], 300, "%s", a.src); am_n++;
      omega_log("montaggio automatico: %s pronto da %s", a.tid, a.src);
    } else {
      omega_log("montaggio automatico: %s non montato: %s", a.tid, err);
      if (am_nbad < MAX_EXT) snprintf(am_bad[am_nbad++], 300, "%s", a.src);
    }
  }
}
