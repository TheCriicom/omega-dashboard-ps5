// Omega UI — Archivio e spostamenti: dove sta ogni gioco e quanto occupa, e
// lo spostamento (o la copia) tra memoria interna e dischi esterni.
//
// Si spostano i giochi in cartella (con eboot.bin): quelli installati da Omega
// come cartella, caricati dal PC o già su un disco. La copia avviene in un
// thread con l'avanzamento nella fila della home; si controlla lo spazio prima,
// si verifica la dimensione dopo e solo allora si toglie l'originale.
//   interna → disco   /user/app/<TID> → <disco>/homebrew/<TID>, poi il gioco si
//                     disinstalla dalla console e il montaggio automatico (o il
//                     primo avvio) lo registra di nuovo dal disco;
//   disco → interna   si smonta da /system_ex/app, i file vanno in /user/app/<TID>
//                     (dove c'era solo la registrazione) e si registra lì;
//   disco → disco     si copia e, spostando, si smonta e si cancella l'originale.
// I giochi installati dal sistema come pacchetto (app.pkg cifrato) non si
// possono montare da un disco: per quelli c'è lo spostamento della console.
// Anche i file .pkg (gli installer) si spostano o si copiano da qui (pkgs.c).
#include "app.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <stdlib.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <unistd.h>

#ifdef PS5
#define APP_ROOT "/user/app"
int sceAppInstUtilInitialize(void);
int sceAppInstUtilAppUnInstall(const char *);
int unmount(const char *dir, int flags);
int app_register(const char *title_id, const char *dir);   // install.c
#else
#define APP_ROOT OMEGA_DIR "/apps"                          // sul Mac i giochi "installati" stanno qui
#endif

enum { SK_FOLDER, SK_SYSPKG, SK_EXT, SK_OTHER };
typedef struct { char tid[16], name[96], path[400], where[48]; int kind, app; long long size; } StEnt;
#define ST_MAX 200
static StEnt st[ST_MAX]; static int nst, st_sel; static float st_anim, st_scroll;
static SDL_atomic_t sizing; static SDL_mutex *smx;

// ------------------------------------------------------------- dimensioni --
static long long tree_size(const char *p, int depth) {
  struct stat s; if (lstat(p, &s) != 0) return 0;
  if (!S_ISDIR(s.st_mode)) return (long long)s.st_size;
  if (depth > 24) return 0;
  long long t = 0; DIR *d = opendir(p); if (!d) return 0;
  struct dirent *e;
  while ((e = readdir(d))) { if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue; char q[1024]; snprintf(q, sizeof q, "%s/%s", p, e->d_name); t += tree_size(q, depth + 1); }
  closedir(d); return t;
}
static int size_thread(void *ud) {
  (void)ud;
  for (int i = 0; i < nst; i++) {
    char p[400]; SDL_LockMutex(smx); snprintf(p, sizeof p, "%s", st[i].path); SDL_UnlockMutex(smx);
    long long s = tree_size(p, 0);
    SDL_LockMutex(smx); if (i < nst && !strcmp(st[i].path, p)) st[i].size = s; SDL_UnlockMutex(smx);
  }
  SDL_AtomicSet(&sizing, 0);
  return 0;
}

static void fmt_size(long long b, char *o, size_t n) {
  if (b <= 0) snprintf(o, n, "\xE2\x80\xA6");
  else if (b >= 1000000000LL) snprintf(o, n, _("%.1f GB"), b / 1e9); else snprintf(o, n, _("%.0f MB"), b / 1e6);
}
static double free_gb(const char *p) { struct statfs s; return statfs(p, &s) == 0 ? (double)s.f_bavail * s.f_bsize / 1e9 : -1; }

// ------------------------------------------------------------------ elenco --
static void st_build(void) {
  if (!smx) smx = SDL_CreateMutex();
  SDL_LockMutex(smx);
  nst = 0;
  for (int i = 0; i < napps && nst < ST_MAX; i++) {
    AppEntry *a = &apps[i];
    if (a->builtin || a->hb || a->pld) continue;
    StEnt *e = &st[nst]; memset(e, 0, sizeof *e);
    snprintf(e->tid, sizeof e->tid, "%s", a->tid); snprintf(e->name, sizeof e->name, "%s", a->name); e->app = i;
    char eb[300];
    if (a->ext) { e->kind = SK_EXT; snprintf(e->path, sizeof e->path, "%s", a->src); snprintf(e->where, sizeof e->where, "%s", a->drive); }
    else if (snprintf(eb, sizeof eb, APP_ROOT "/%s/eboot.bin", a->tid), access(eb, 0) == 0) { e->kind = SK_FOLDER; snprintf(e->path, sizeof e->path, APP_ROOT "/%s", a->tid); snprintf(e->where, sizeof e->where, "%s", _("Memoria interna")); }
    else if (snprintf(eb, sizeof eb, APP_ROOT "/%s/app.pkg", a->tid), access(eb, 0) == 0) { e->kind = SK_SYSPKG; snprintf(e->path, sizeof e->path, APP_ROOT "/%s", a->tid); snprintf(e->where, sizeof e->where, "%s", _("Memoria interna")); }
    else {
      // montato su /system_ex/app da un altro strumento (ShadowMount) oppure installato
      // dal sistema altrove (archivio esteso, M.2): in entrambi i casi non lo sposta Omega
      char sx[120]; snprintf(sx, sizeof sx, OMEGA_SYSROOT "/system_ex/app/%s/eboot.bin", a->tid);
      if (access(sx, 0) == 0) { e->kind = SK_OTHER; snprintf(e->path, sizeof e->path, OMEGA_SYSROOT "/system_ex/app/%s", a->tid); snprintf(e->where, sizeof e->where, "%s", _("Montato da un altro strumento")); }
      else { e->kind = SK_SYSPKG; snprintf(e->path, sizeof e->path, APP_ROOT "/%s", a->tid); snprintf(e->where, sizeof e->where, "%s", _("Archivio della console")); }
    }
    e->size = -1;
    nst++;
  }
  SDL_UnlockMutex(smx);
  if (st_sel >= nst) st_sel = nst ? nst - 1 : 0;
  if (!SDL_AtomicGet(&sizing)) {
    SDL_AtomicSet(&sizing, 1);
    SDL_Thread *t = SDL_CreateThread(size_thread, "sizes", NULL);
    if (t) SDL_DetachThread(t); else SDL_AtomicSet(&sizing, 0);
  }
}

// --------------------------------------------------------------- lavoro --
// Una sola operazione alla volta; l'avanzamento si vede nella fila della home.
typedef struct { StEnt e; char dst_mount[160], dst_label[48], file_src[600]; int copy_only, to_internal, is_file; } MoveJob;
static SDL_atomic_t mv_state;            // 0 fermo, 1 in corso, 2 finito
static volatile long long mv_done, mv_total; static volatile int mv_cancel;
static char mv_label[96], mv_phase[96], mv_result[300]; static int mv_err; static Uint32 mv_ended, rebuild_at;

static int copy_file(const char *a, const char *b) {
  int in = open(a, O_RDONLY); if (in < 0) return -1;
  int out = open(b, O_WRONLY | O_CREAT | O_TRUNC, 0777); if (out < 0) { close(in); return -2; }
  char *buf = malloc(1 << 20); ssize_t r = 0; int rc = buf ? 0 : -3;
  while (buf && (r = read(in, buf, 1 << 20)) > 0) {
    if (mv_cancel) { rc = -9; break; }
    if (write(out, buf, (size_t)r) != r) { rc = -4; break; }
    mv_done += r;
  }
  if (r < 0) rc = -5;
  free(buf); close(in); if (close(out) != 0 && !rc) rc = -6;
  if (rc) unlink(b);
  else { struct stat s; if (stat(a, &s) == 0) chmod(b, s.st_mode & 0777); }
  return rc;
}
static int copy_tree(const char *a, const char *b) {
  struct stat s; if (lstat(a, &s) != 0) return -1;
  if (!S_ISDIR(s.st_mode)) return copy_file(a, b);
  if (mkdir(b, 0777) != 0 && errno != EEXIST) return -7;
  DIR *d = opendir(a); if (!d) return -8;
  struct dirent *e; int rc = 0;
  while (!rc && (e = readdir(d))) {
    if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
    char pa[1024], pb[1024]; snprintf(pa, sizeof pa, "%s/%s", a, e->d_name); snprintf(pb, sizeof pb, "%s/%s", b, e->d_name);
    rc = copy_tree(pa, pb);
  }
  closedir(d); return rc;
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

static int move_thread(void *arg) {
  MoveJob *j = arg;
  mv_done = 0; mv_cancel = 0; mv_err = 0; mv_result[0] = 0;
  char dst[600], src[600];
  int rc = 0;
  if (j->is_file) {
    // un file .pkg in <disco>/PKG/ (o /data/pkg)
    snprintf(src, sizeof src, "%s", j->file_src);
    const char *base = strrchr(src, '/'); base = base ? base + 1 : src;
    char dir[400]; snprintf(dir, sizeof dir, "%s", j->dst_mount);
    mkdir(dir, 0777);
    snprintf(dst, sizeof dst, "%s/%s", dir, base);
    struct stat s; mv_total = stat(src, &s) == 0 ? (long long)s.st_size : 0;
    double fr = free_gb(dir);
    if (fr >= 0 && fr * 1e9 < mv_total + 64e6) { snprintf(mv_result, sizeof mv_result, _("Spazio insufficiente su %s: servono %.1f GB"), j->dst_label, mv_total / 1e9); mv_err = 1; goto out; }
    if (access(dst, 0) == 0) { snprintf(mv_result, sizeof mv_result, _("Su %s c'è già un file con questo nome"), j->dst_label); mv_err = 1; goto out; }
    snprintf(mv_phase, sizeof mv_phase, "%s", j->copy_only ? _("Copia") : _("Spostamento"));
    if (!j->copy_only && rename(src, dst) == 0) { mv_done = mv_total; }   // stesso disco: istantaneo
    else if ((rc = copy_file(src, dst)) != 0) { snprintf(mv_result, sizeof mv_result, mv_cancel ? _("Operazione annullata") : _("Copia non riuscita (errore %d): il disco è pieno o in sola lettura?"), rc); mv_err = !mv_cancel; goto out; }
    else if (!j->copy_only) unlink(src);
    snprintf(mv_result, sizeof mv_result, j->copy_only ? _("Copiato su %s") : _("Spostato su %s"), j->dst_label);
    goto out;
  }
  StEnt *e = &j->e;
  snprintf(src, sizeof src, "%s", e->path);
  if (j->to_internal) snprintf(dst, sizeof dst, APP_ROOT "/%s", e->tid);
  else { char hb[300]; snprintf(hb, sizeof hb, "%s/homebrew", j->dst_mount); mkdir(hb, 0777); snprintf(dst, sizeof dst, "%s/%s", hb, e->tid); }
  snprintf(mv_phase, sizeof mv_phase, "%s", _("Calcolo dello spazio"));
  mv_total = e->size > 0 ? e->size : tree_size(src, 0);
  // spazio: quello del gioco più mezzo giga di margine
  double fr = free_gb(j->to_internal ? APP_ROOT : j->dst_mount);
  if (fr >= 0 && fr * 1e9 < mv_total + 512e6) { snprintf(mv_result, sizeof mv_result, _("Spazio insufficiente su %s: servono %.1f GB, liberi %.1f GB"), j->dst_label, mv_total / 1e9, fr); mv_err = 1; goto out; }
  // a destinazione deve esserci al massimo la registrazione che fa Omega (mount.lnk + sce_sys)
  char eb[700]; snprintf(eb, sizeof eb, "%s/eboot.bin", dst);
  if (access(eb, 0) == 0) { snprintf(mv_result, sizeof mv_result, _("%s c'è già su %s"), e->name, j->dst_label); mv_err = 1; goto out; }
  snprintf(mv_phase, sizeof mv_phase, "%s", j->copy_only ? _("Copia") : _("Spostamento"));
  rc = copy_tree(src, dst);
  if (rc) {
    if (!j->to_internal) rm_tree(dst);   // copia a metà sul disco: via
    snprintf(mv_result, sizeof mv_result, mv_cancel ? _("Operazione annullata: l'originale è intatto") : _("Copia non riuscita (errore %d): l'originale è intatto"), rc);
    mv_err = !mv_cancel; goto out;
  }
  snprintf(mv_phase, sizeof mv_phase, "%s", _("Verifica"));
  long long got = tree_size(dst, 0);
  if (got < mv_total) { snprintf(mv_result, sizeof mv_result, _("La copia è incompleta (%lld di %lld byte): l'originale è intatto"), got, mv_total); mv_err = 1; goto out; }
  snprintf(mv_phase, sizeof mv_phase, "%s", _("Registrazione"));
#ifdef PS5
  if (j->to_internal) {
    // dal disco alla memoria interna: niente più mount, i file sono a casa
    char mp[96]; snprintf(mp, sizeof mp, "/system_ex/app/%s", e->tid);
    unmount(mp, MNT_FORCE); rmdir(mp);
    char ln[700]; snprintf(ln, sizeof ln, "%s/mount.lnk", dst); unlink(ln);
    int r = app_register(e->tid, "/user/app/");
    omega_log("archivio: %s registrato dalla memoria interna -> 0x%x", e->tid, r);
    if (r != 0 && r != (int)0x80990002) { snprintf(mv_result, sizeof mv_result, _("Copiato, ma la console non l'ha registrato (0x%08X)"), (unsigned)r); mv_err = 1; goto out; }
  } else if (e->kind == SK_FOLDER && !j->copy_only) {
    // dalla memoria interna al disco: si disinstalla; il montaggio automatico
    // (o il primo avvio) lo registra di nuovo dal disco
    sceAppInstUtilInitialize();
    int r = sceAppInstUtilAppUnInstall(e->tid);
    omega_log("archivio: %s disinstallato dalla memoria interna -> 0x%x", e->tid, r);
  }
  if (e->kind == SK_EXT && !j->copy_only) { char mp[96]; snprintf(mp, sizeof mp, "/system_ex/app/%s", e->tid); unmount(mp, MNT_FORCE); }
#endif
  if (!j->copy_only && !(e->kind == SK_FOLDER && !j->to_internal)) {
    // l'originale su disco si toglie solo adesso, a copia verificata
    if (rm_tree(src) != 0) omega_log("archivio: originale %s non tolto del tutto", src);
  }
#ifndef PS5
  if (e->kind == SK_FOLDER && !j->copy_only) rm_tree(src);   // sul Mac non c'è la disinstallazione
#endif
  snprintf(mv_result, sizeof mv_result, j->copy_only ? _("%s copiato su %s") : _("%s spostato su %s"), e->name, j->dst_label);
out:
  omega_log("archivio: %s", mv_result);
  free(j);
  SDL_AtomicSet(&mv_state, 2);
  return 0;
}

static void move_start(MoveJob *j) {
  if (SDL_AtomicGet(&mv_state) == 1) { free(j); set_msg(_("C'è già uno spostamento in corso"), 1); return; }
  snprintf(mv_label, sizeof mv_label, "%s", j->is_file ? (strrchr(j->file_src, '/') ? strrchr(j->file_src, '/') + 1 : j->file_src) : j->e.name);
  mv_phase[0] = 0; mv_total = 0; mv_done = 0; mv_ended = 0;
  SDL_AtomicSet(&mv_state, 1);
  SDL_Thread *t = SDL_CreateThreadWithStackSize(move_thread, "move", 512 * 1024, j);
  if (t) SDL_DetachThread(t); else { free(j); SDL_AtomicSet(&mv_state, 0); }
  set_msg(_("Spostamento avviato: segui la barra nella home"), 0);
}

void storage_tick(void) {
  if (SDL_AtomicGet(&mv_state) != 2) return;
  SDL_AtomicSet(&mv_state, 0);
  mv_ended = SDL_GetTicks();
  set_msg(mv_result, mv_err);
  toast(mv_err ? IC_CLOSE : IC_DRIVE, NULL, 0, mv_label, mv_result);
  rebuild_at = SDL_GetTicks() + 4500;   // dopo che la home ha riletto dischi e titoli
}

int storage_view(InstallView *v) {
  memset(v, 0, sizeof *v);
  if (SDL_AtomicGet(&mv_state) == 1) {
    v->active = 1; snprintf(v->name, sizeof v->name, "%s", mv_label);
    long long d = mv_done, t = mv_total;
    v->prog = t > 0 ? clampf((float)d / (float)t, 0, 1) : -1;
    if (t > 0) snprintf(v->phase, sizeof v->phase, _("%s  \xC2\xB7  %d%%  \xC2\xB7  %.1f / %.1f GB"), mv_phase, (int)(v->prog * 100), d / 1e9, t / 1e9);
    else snprintf(v->phase, sizeof v->phase, "%s...", mv_phase);
    return 1;
  }
  if (mv_ended && SDL_GetTicks() - mv_ended < (mv_err ? 60000u : 12000u)) {
    v->ended = mv_ended; v->err = mv_err; v->prog = 1; snprintf(v->name, sizeof v->name, "%s", mv_label); snprintf(v->result, sizeof v->result, "%s", mv_result);
    return 1;
  }
  return 0;
}
void storage_cancel(void) { if (SDL_AtomicGet(&mv_state) == 1) mv_cancel = 1; }

// ------------------------------------------------------ scelta del disco --
static Drive dd[12]; static int ndd;
static char dlab[14][120]; static const char *dit[14]; static int dkind[14];   // dkind: -1 interna, >= 0 indice in dd
static MoveJob pend; static int pend_copy;
static void dest_pick(int idx, void *ud) {
  (void)ud; if (idx < 0 || idx >= 14 || !dit[idx]) return;
  MoveJob *j = malloc(sizeof *j); if (!j) return;
  *j = pend; j->copy_only = pend_copy;
  if (dkind[idx] < 0) { j->to_internal = 1; snprintf(j->dst_label, sizeof j->dst_label, "%s", _("Memoria interna")); snprintf(j->dst_mount, sizeof j->dst_mount, "%s", j->is_file ? OMEGA_SYSROOT "/data/pkg" : APP_ROOT); }
  else {
    snprintf(j->dst_label, sizeof j->dst_label, "%s", dd[dkind[idx]].label);
    if (j->is_file) snprintf(j->dst_mount, sizeof j->dst_mount, "%s/PKG", dd[dkind[idx]].mount);
    else snprintf(j->dst_mount, sizeof j->dst_mount, "%s", dd[dkind[idx]].mount);
  }
  move_start(j);
}
// elenca le destinazioni possibili (tutte tranne quella dove sta già)
static int dest_menu(const char *title, const char *cur_path, int allow_internal) {
  ndd = drives_list(dd, 12);
  int n = 0;
  memset(dit, 0, sizeof dit);
  if (allow_internal && strncmp(cur_path, APP_ROOT, strlen(APP_ROOT)) && strncmp(cur_path, OMEGA_SYSROOT "/data/pkg", strlen(OMEGA_SYSROOT "/data/pkg"))) {
    double fr = free_gb(APP_ROOT);
    snprintf(dlab[n], sizeof dlab[0], _("Memoria interna \xC2\xB7 %.0f GB liberi"), fr < 0 ? 0 : fr); dkind[n] = -1; dit[n] = dlab[n]; n++;
  }
  for (int i = 0; i < ndd && n < 13; i++) {
    if (!strncmp(cur_path, dd[i].mount, strlen(dd[i].mount))) continue;
    snprintf(dlab[n], sizeof dlab[0], _("%s \xC2\xB7 %.0f GB liberi di %.0f"), dd[i].label, dd[i].free_gb, dd[i].total_gb); dkind[n] = i; dit[n] = dlab[n]; n++;
  }
  if (!n) { set_msg(_("Collega un disco esterno (exFAT o archivio esteso) per spostare qui"), 1); return 0; }
  menu_open(title, dit, n, dest_pick, NULL);
  return n;
}

// dal menu di un gioco o dal pannello: sposta (copy 0) o copia (copy 1)
static void move_entry(const StEnt *e, int copy) {
  if (e->kind == SK_SYSPKG) {
    confirm_open(_("Questo gioco è installato dal sistema come pacchetto cifrato e non si può leggere da un disco con Omega. Per i giochi PS4 usa Impostazioni della console \xE2\x80\xBA Archiviazione \xE2\x80\xBA Sposta su archivio esteso."), _("Ho capito"), NULL, NULL);
    return;
  }
  if (e->kind == SK_OTHER) { set_msg(_("Questo gioco lo ha montato un altro strumento (es. ShadowMount): spostalo dalla sua cartella sul disco"), 1); return; }
  memset(&pend, 0, sizeof pend); pend.e = *e; pend_copy = copy;
  char t[160]; snprintf(t, sizeof t, copy ? _("Copia %s su...") : _("Sposta %s su..."), e->name);
  dest_menu(t, e->path, 1);
}
void storage_move_app(int app_idx) {
  st_build();
  for (int i = 0; i < nst; i++) if (st[i].app == app_idx) { move_entry(&st[i], 0); return; }
  set_msg(_("Si spostano solo i giochi: homebrew e payload stanno nelle cartelle del caricatore"), 1);
}
// file .pkg (pkgs.c)
void storage_move_pkg(const char *path, int copy) {
  memset(&pend, 0, sizeof pend); pend.is_file = 1; pend_copy = copy;
  snprintf(pend.file_src, sizeof pend.file_src, "%s", path);
  dest_menu(copy ? _("Copia il pkg su...") : _("Sposta il pkg su..."), path, 1);
}

// ---------------------------------------------------------------- pannello --
void storage_open(void) {
  st_build(); st_sel = 0; st_scroll = 0;
  if (ov_top() != OV_STORAGE) ov_push(OV_STORAGE);
}

static char del_path[400];
static void del_yes(int idx, void *ud) {
  (void)ud; if (idx != 0) return;
  // solo cartelle di giochi sui dischi esterni: dentro /mnt e con eboot.bin
  char eb[500]; snprintf(eb, sizeof eb, "%s/eboot.bin", del_path);
  if (strncmp(del_path, OMEGA_SYSROOT "/mnt/", strlen(OMEGA_SYSROOT "/mnt/")) || access(eb, 0) != 0) { set_msg(_("Qui non si cancella niente"), 1); return; }
  set_msg(rm_tree(del_path) == 0 ? _("Gioco cancellato dal disco") : _("Cancellazione non riuscita (disco in sola lettura?)"), 0);
  st_build();
}
static void opts_pick(int idx, void *ud) {
  (void)ud; if (st_sel >= nst) return;
  StEnt *e = &st[st_sel];
  if (idx == 0) move_entry(e, 0);
  else if (idx == 1) move_entry(e, 1);
  else if (idx == 2 && e->kind == SK_EXT) { snprintf(del_path, sizeof del_path, "%s", e->path); static char q[300]; snprintf(q, sizeof q, _("Cancellare definitivamente %s da %s? I salvataggi restano sulla console."), e->name, e->where); confirm_open(q, _("Cancella"), del_yes, NULL); }
}

void storage_draw(float t) {
  int a = (int)(255 * t);
  if (rebuild_at && SDL_GetTicks() > rebuild_at) { rebuild_at = 0; st_build(); }
  fill_rect(0, 0, SCREEN_W, SCREEN_H, mix(RGB(10, 12, 20), g_theme_base, 0.12f), t > 0.98f ? 255 : a);
  int x0 = 110;
  draw_icon(IC_DRIVE, x0 + 22, 74, 44, C_ACC2, a);
  draw_text(font(W_LIGHT, 38), _("Archivio e spostamenti"), x0 + 62, 52, C_WHITE, a, AL_L);
  // dischi: barra dello spazio
  Drive dv[12]; int nd = drives_list(dv, 12);
  int cx = x0, cy = 140, cw = 340;
  { double fr = free_gb(APP_ROOT); struct statfs s; double tot = statfs(APP_ROOT, &s) == 0 ? (double)s.f_blocks * s.f_bsize / 1e9 : 0;
    fill_rrect(cx, cy, cw, 96, 20, C_WHITE, a * 6 / 100);
    draw_icon(IC_GAMEPAD, cx + 36, cy + 34, 28, C_TXT, a);
    draw_text_fit(font(W_MED, 23), _("Memoria interna"), cx + 66, cy + 18, cw - 80, C_TXT, a, AL_L);
    char m[64]; snprintf(m, sizeof m, _("%.0f GB liberi"), fr < 0 ? 0 : fr); draw_text(font(W_REG, 19), m, cx + 66, cy + 48, C_DIM, a, AL_L);
    float k = tot > 0 ? (float)((tot - fr) / tot) : 0; fill_rrect(cx + 20, cy + 76, cw - 40, 8, 4, C_WHITE, a * 14 / 100); fill_rrect(cx + 20, cy + 76, (int)((cw - 40) * clampf(k, 0, 1)), 8, 4, k > 0.9f ? C_ERR : C_ACC, a); }
  for (int i = 0; i < nd && i < 4; i++) {
    int x = cx + (i + 1) * (cw + 20);
    fill_rrect(x, cy, cw, 96, 20, C_WHITE, a * 6 / 100);
    draw_icon(IC_USB, x + 36, cy + 34, 28, C_ACC2, a);
    draw_text_fit(font(W_MED, 23), dv[i].label, x + 66, cy + 18, cw - 80, C_TXT, a, AL_L);
    char m[64]; snprintf(m, sizeof m, _("%.0f GB liberi di %.0f"), dv[i].free_gb, dv[i].total_gb); draw_text(font(W_REG, 19), m, x + 66, cy + 48, C_DIM, a, AL_L);
    float k = dv[i].total_gb > 0 ? (float)((dv[i].total_gb - dv[i].free_gb) / dv[i].total_gb) : 0;
    fill_rrect(x + 20, cy + 76, cw - 40, 8, 4, C_WHITE, a * 14 / 100); fill_rrect(x + 20, cy + 76, (int)((cw - 40) * clampf(k, 0, 1)), 8, 4, k > 0.9f ? C_ERR : C_ACC2, a);
  }
  if (!nd) draw_text_wrap(font(W_REG, 22), _("Nessun disco esterno collegato: collegane uno (exFAT) per spostare lì i giochi."), cx + cw + 30, cy + 22, 900, 2, 30, C_FAINT, a);
  // giochi
  int top = 270, rh = 92, w = SCREEN_W - 2 * x0, bottom = SCREEN_H - 130;
  if (!nst) { draw_text(font(W_REG, 26), _("Nessun gioco installato"), SCREEN_W / 2, top + 80, C_FAINT, a, AL_C); }
  st_anim = approach(st_anim, (float)st_sel, 20.0f);
  float tgt = st_sel * rh + rh > bottom - top ? (float)(st_sel * rh + rh - (bottom - top) + 20) : 0;
  st_scroll = approach(st_scroll, tgt, 14.0f);
  SDL_Rect clip = { 0, top - 8, SCREEN_W, bottom - top + 8 }; SDL_RenderSetClipRect(R, &clip);
  if (smx) SDL_LockMutex(smx);
  for (int i = 0; i < nst; i++) {
    int y = top + i * rh - (int)st_scroll;
    if (y + rh < top || y > bottom) continue;
    StEnt *e = &st[i];
    float fa = clampf(1 - fabsf(st_anim - i), 0, 1);
    fill_rrect(x0, y, w, rh - 12, 18, C_WHITE, (int)(a * (0.05f + 0.08f * fa)));
    if (i == st_sel) stroke_rrect(x0 - 3, y - 3, w + 6, rh - 6, 21, 3, C_WHITE, a);
    if (e->app >= 0 && e->app < napps) home_tile(e->app, x0 + 10, y + 8, rh - 28, a);
    draw_text_fit(font(W_MED, 27), e->name, x0 + rh + 10, y + 12, w - 700, C_TXT, a, AL_L);
    const char *kd = e->kind == SK_FOLDER ? _("cartella, si sposta") : e->kind == SK_EXT ? _("su disco, si sposta") : e->kind == SK_SYSPKG ? _("pacchetto di sistema") : _("montato da altro");
    char sub[160]; snprintf(sub, sizeof sub, "%s  \xC2\xB7  %s", e->tid, kd);
    draw_text_fit(font(W_REG, 20), sub, x0 + rh + 10, y + 48, w - 700, e->kind <= SK_EXT || e->kind == SK_EXT ? C_DIM : C_FAINT, a, AL_L);
    draw_text_fit(font(W_MED, 22), e->where, x0 + w - 380, y + 16, 250, e->kind == SK_EXT ? C_ACC2 : C_TXT, a, AL_L);
    char sz[24]; fmt_size(e->size, sz, sizeof sz); draw_text(font(W_REG, 22), sz, x0 + w - 30, y + 28, C_DIM, a, AL_R);
  }
  if (smx) SDL_UnlockMutex(smx);
  SDL_RenderSetClipRect(R, NULL);
  int ic[4]; const char *lb[4]; int n = 0;
  ic[n] = IC_BTN_X; lb[n++] = _("Sposta"); ic[n] = IC_BTN_SQ; lb[n++] = _("Opzioni");
  if (SDL_AtomicGet(&mv_state) == 1) { ic[n] = IC_BTN_TRI; lb[n++] = _("Annulla spostamento"); }
  ic[n] = IC_BTN_O; lb[n++] = _("Indietro");
  hints(ic, lb, n, a);
}

void storage_input(int b) {
  if (b == B_O) { ov_pop(); return; }
  if (b == B_UP && st_sel > 0) { st_sel--; sfx_play(SFX_MOVE); }
  else if (b == B_DOWN && st_sel < nst - 1) { st_sel++; sfx_play(SFX_MOVE); }
  else if (b == B_X && st_sel < nst) move_entry(&st[st_sel], 0);
  else if (b == B_TRI) { storage_cancel(); set_msg(_("Annullo: l'originale resta dov'era"), 0); }
  else if (b == B_SQ && st_sel < nst) {
    static const char *it[3]; int n = 0;
    it[n++] = _("Sposta su un altro disco"); it[n++] = _("Copia su un altro disco");
    if (st[st_sel].kind == SK_EXT) it[n++] = _("Cancella dal disco");
    menu_open(st[st_sel].name, it, n, opts_pick, NULL);
  }
}
