// Omega UI — Installa PKG: i .pkg trovati sulle chiavette e sui dischi USB,
// sull'archivio esteso, nella memoria interna (/data/pkg) e nelle cartelle
// scelte dall'utente. A sinistra le posizioni, a destra i pacchetti con icona,
// titolo e dimensione letti dal pkg stesso; ✕ installa (in coda se ce n'è già
// una in corso), △ li installa tutti, □ altre azioni. L'installazione vera è
// in install.c, con la tessera che avanza nella home.
//
// Da qui si scelgono anche le cartelle dei giochi (drives.c le guarda insieme
// ai dischi) e quelle dei pkg, con un piccolo esploratore di cartelle.
#include "app.h"
#include <dirent.h>
#include <math.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

#define PK_MAX 400
#define LOC_MAX 24
typedef struct { char path[400], name[160], title[96], cid[48], icon[320]; long long size; int ps5, installed; SDL_Texture *tex; int tex_st; } PkgEnt;
typedef struct { char path[300], label[96]; int kind; double free_gb; } Loc;   // kind: 0 disco, 1 interna, 2 scelta dall'utente, 9 azione

static Loc locs[LOC_MAX]; static int nloc, loc_sel;
static PkgEnt *pk; static int npk, pk_sel;          // risultato pubblicato (thread principale)
static SDL_atomic_t scanning, scan_done;
static PkgEnt *scan_buf; static int scan_n; static char scan_path[300];
static int focus_right;                             // 0 posizioni, 1 pacchetti
static float loc_anim, pk_anim, pk_scroll;
enum { A_BROWSE = 1, A_FOLDERS = 2 };

// ---------------------------------------------------------------- posizioni --
static void add_loc(const char *path, const char *label, int kind, double fr) {
  if (nloc >= LOC_MAX) return;
  for (int i = 0; i < nloc; i++) if (!strcmp(locs[i].path, path) && kind != 9) return;
  Loc *l = &locs[nloc++]; memset(l, 0, sizeof *l);
  snprintf(l->path, sizeof l->path, "%s", path); snprintf(l->label, sizeof l->label, "%s", label); l->kind = kind; l->free_gb = fr;
}
static void build_locs(void) {
  nloc = 0;
  Drive dr[12]; int nd = drives_list(dr, 12);
  for (int i = 0; i < nd; i++) add_loc(dr[i].mount, dr[i].label, 0, dr[i].free_gb);
  add_loc(OMEGA_SYSROOT "/data/pkg", _("Memoria interna (/data/pkg)"), 1, -1);
  add_loc(OMEGA_DIR "/uploads", _("Caricati dal telefono o dal PC"), 1, -1);
  static char cp[32][300]; int nc = paths_list(1, cp, 32);
  for (int i = 0; i < nc; i++) { const char *b = strrchr(cp[i], '/'); char lb[96]; snprintf(lb, sizeof lb, "%s", b && b[1] ? b + 1 : cp[i]); add_loc(cp[i], lb, 2, -1); }
  add_loc("", _("Sfoglia una cartella..."), 9, A_BROWSE);
  add_loc("", _("Cartelle di giochi e PKG"), 9, A_FOLDERS);
  if (loc_sel >= nloc) loc_sel = 0;
}

// ------------------------------------------------------------------ ricerca --
// Sui dischi grandi la ricerca può durare: thread, al massimo 3 livelli sotto la
// posizione, senza entrare nelle cartelle di sistema.
static void scan_dir(const char *dir, int depth) {
  if (scan_n >= PK_MAX) return;
  DIR *d = opendir(dir); if (!d) return;
  struct dirent *e;
  while ((e = readdir(d)) && scan_n < PK_MAX) {
    if (e->d_name[0] == '.' || !strcmp(e->d_name, "System Volume Information") || !strcmp(e->d_name, "$RECYCLE.BIN")) continue;
    char p[700]; snprintf(p, sizeof p, "%s/%s", dir, e->d_name);
    struct stat st; if (stat(p, &st) != 0) continue;
    if (S_ISDIR(st.st_mode)) { if (depth > 0) scan_dir(p, depth - 1); continue; }
    size_t L = strlen(e->d_name);
    if (L < 5 || strcasecmp(e->d_name + L - 4, ".pkg")) continue;
    PkgEnt *k = &scan_buf[scan_n]; memset(k, 0, sizeof *k);
    snprintf(k->path, sizeof k->path, "%s", p);
    snprintf(k->name, sizeof k->name, "%.*s", (int)(L - 4), e->d_name);
    k->size = (long long)st.st_size;
    snprintf(k->icon, sizeof k->icon, OMEGA_DIR "/dl/icons/%08x.png", fnv1a(p));
    int r = pkg_info(p, k->title, sizeof k->title, k->cid, sizeof k->cid, access(k->icon, 0) == 0 ? NULL : k->icon);
    if (!r) continue;                                   // non è un pkg
    k->ps5 = r == 2;
    if (access(k->icon, 0) != 0) k->icon[0] = 0;
    // già installato? Title ID dentro il Content ID (EP0000-CUSA00000_00-...)
    if (strlen(k->cid) >= 16) { char tid[10]; snprintf(tid, sizeof tid, "%.9s", k->cid + 7); char m[96]; snprintf(m, sizeof m, OMEGA_SYSROOT "/user/appmeta/%s", tid); k->installed = access(m, 0) == 0; }
    scan_n++;
  }
  closedir(d);
}
static int scan_thread(void *ud) {
  (void)ud;
  scan_n = 0;
  scan_dir(scan_path, 3);
  SDL_AtomicSet(&scan_done, 1);
  SDL_AtomicSet(&scanning, 0);
  return 0;
}
static void free_tex(void) { for (int i = 0; i < npk; i++) if (pk[i].tex) { SDL_DestroyTexture(pk[i].tex); pk[i].tex = NULL; } }
static void start_scan(void) {
  if (loc_sel < 0 || loc_sel >= nloc || locs[loc_sel].kind == 9) { free_tex(); npk = 0; return; }
  if (SDL_AtomicGet(&scanning)) return;
  if (!scan_buf) scan_buf = calloc(PK_MAX, sizeof *scan_buf);
  if (!pk) pk = calloc(PK_MAX, sizeof *pk);
  if (!scan_buf || !pk) return;
  mkdir(OMEGA_DIR "/dl", 0777); mkdir(OMEGA_DIR "/dl/icons", 0777);
  snprintf(scan_path, sizeof scan_path, "%s", locs[loc_sel].path);
  SDL_AtomicSet(&scanning, 1);
  SDL_Thread *t = SDL_CreateThread(scan_thread, "pkgscan", NULL);
  if (t) SDL_DetachThread(t); else SDL_AtomicSet(&scanning, 0);
}
static void poll_scan(void) {
  if (!SDL_AtomicGet(&scan_done)) return;
  SDL_AtomicSet(&scan_done, 0);
  free_tex();
  npk = scan_n; memcpy(pk, scan_buf, sizeof(PkgEnt) * (size_t)npk);
  if (pk_sel >= npk) pk_sel = npk ? npk - 1 : 0;
}

static void on_icon(const char *key, SDL_Texture *t, SDL_Color avg, void *ud) {
  (void)avg; (void)ud;
  // chiave corta (il caricatore tiene 47 caratteri): il nome del file dell'icona
  for (int i = 0; i < npk; i++) { const char *b = strrchr(pk[i].icon, '/'); if (pk[i].tex_st == 1 && b && !strcmp(b + 1, key)) { pk[i].tex = t; pk[i].tex_st = t ? 2 : 3; return; } }
  if (t) SDL_DestroyTexture(t);
}

// -------------------------------------------------------------- installare --
static void install_one(const PkgEnt *k) {
  InstallReq r; memset(&r, 0, sizeof r);
  r.kind = 1;
  snprintf(r.url, sizeof r.url, "file://%s", k->path);
  snprintf(r.name, sizeof r.name, "%s", k->title[0] ? k->title : k->name);
  snprintf(r.icon, sizeof r.icon, "%s", k->icon);
  snprintf(r.filename, sizeof r.filename, "%s.pkg", k->name);
  install_begin(&r);
}
static void all_yes(int idx, void *ud) {
  (void)ud; if (idx != 0) return;
  int n = 0; for (int i = 0; i < npk; i++) if (!pk[i].installed) { install_one(&pk[i]); n++; }
  char m[120]; snprintf(m, sizeof m, _("%d pacchetti in installazione: segui l'avanzamento nella home"), n); set_msg(m, 0);
}
static char del_path[400];
static void del_yes(int idx, void *ud) {
  (void)ud; if (idx != 0) return;
  if (unlink(del_path) == 0) { set_msg(_("File eliminato"), 0); start_scan(); } else set_msg(_("Impossibile eliminare il file (disco in sola lettura?)"), 1);
}
static void pkg_opts(int idx, void *ud) {
  (void)ud; if (pk_sel >= npk) return;
  PkgEnt *k = &pk[pk_sel];
  if (idx == 0) install_one(k);
  else if (idx == 1) { snprintf(del_path, sizeof del_path, "%s", k->path); confirm_open(_("Eliminare questo file .pkg dal disco?"), _("Elimina"), del_yes, NULL); }
  else if (idx == 2) { char dir[300]; snprintf(dir, sizeof dir, "%s", k->path); char *sl = strrchr(dir, '/'); if (sl) *sl = 0; if (paths_set(1, dir, 1) > 0) { set_msg(_("Cartella aggiunta alle posizioni dei PKG"), 0); build_locs(); } }
}

// ----------------------------------------------------- esploratore cartelle --
// purpose 0: posizione dei pkg, 1: cartella dei giochi
#define BR_MAX 600
static int br_on, br_purpose, br_sel; static char br_path[400];
static char (*br_names)[200]; static int br_n; static float br_anim, br_scroll;
static int name_cmp(const void *a, const void *b) { return strcasecmp((const char *)a, (const char *)b); }
static void br_load(void) {
  if (!br_names) br_names = calloc(BR_MAX, 200);
  br_n = 0; if (!br_names) return;
  DIR *d = opendir(br_path[0] ? br_path : "/");
  struct dirent *e;
  while (d && (e = readdir(d)) && br_n < BR_MAX) {
    if (e->d_name[0] == '.') continue;
    char p[700]; snprintf(p, sizeof p, "%s/%s", br_path, e->d_name);
    struct stat st; if (stat(p, &st) != 0 || !S_ISDIR(st.st_mode)) continue;
    snprintf(br_names[br_n++], 200, "%s", e->d_name);
  }
  if (d) closedir(d);
  qsort(br_names, (size_t)br_n, 200, name_cmp);
  br_sel = 0; br_scroll = 0;
}
static void browse_open(int purpose, const char *start) {
  br_on = 1; br_purpose = purpose;
  snprintf(br_path, sizeof br_path, "%s", start && *start ? start : OMEGA_SYSROOT "/mnt");
  br_load();
}

// ------------------------------------------------- cartelle giochi e pkg --
static char fm_items[40][300]; static const char *fm_ptr[40]; static int fm_kind[40], fm_n;
static void folders_pick(int idx, void *ud);
void paths_menu(void) {
  static char g[32][300], k[32][300];
  int ng = paths_list(0, g, 32), nk = paths_list(1, k, 32);
  fm_n = 0;
  snprintf(fm_items[fm_n], 300, "%s", _("+ Aggiungi una cartella di giochi")); fm_kind[fm_n++] = 10;
  snprintf(fm_items[fm_n], 300, "%s", _("+ Aggiungi una cartella di PKG")); fm_kind[fm_n++] = 11;
  for (int i = 0; i < ng && fm_n < 38; i++) { snprintf(fm_items[fm_n], 300, _("Giochi: %s  (togli)"), g[i] + strlen(OMEGA_SYSROOT)); fm_kind[fm_n++] = 100 + i; }
  for (int i = 0; i < nk && fm_n < 38; i++) { snprintf(fm_items[fm_n], 300, _("PKG: %s  (togli)"), k[i] + strlen(OMEGA_SYSROOT)); fm_kind[fm_n++] = 200 + i; }
  for (int i = 0; i < fm_n; i++) fm_ptr[i] = fm_items[i];
  menu_open(_("Cartelle di giochi e PKG"), fm_ptr, fm_n, folders_pick, NULL);
}
static void folders_pick(int idx, void *ud) {
  (void)ud; if (idx < 0 || idx >= fm_n) return;
  int k = fm_kind[idx];
  if (k == 10 || k == 11) {
    if (ov_top() != OV_PKGS) pkgs_open();
    browse_open(k == 10 ? 1 : 0, NULL);
    return;
  }
  static char l[32][300];
  int kind = k >= 200 ? 1 : 0, i = k - (kind ? 200 : 100);
  int n = paths_list(kind, l, 32);
  if (i >= 0 && i < n && paths_set(kind, l[i], 0) > 0) { set_msg(_("Cartella tolta"), 0); build_locs(); }
}

// ------------------------------------------------------------------ pannello --
void pkgs_open(void) {
  build_locs();
  loc_sel = 0; pk_sel = 0; focus_right = 0; br_on = 0; pk_scroll = 0;
  start_scan();
  if (ov_top() != OV_PKGS) ov_push(OV_PKGS);
}

static void fmt_size(long long b, char *o, size_t n) {
  if (b >= 1000000000LL) snprintf(o, n, _("%.1f GB"), b / 1e9); else snprintf(o, n, _("%.0f MB"), b / 1e6);
}

static void draw_browser(int a) {
  int x0 = 110, w = SCREEN_W - 220, top = 220, rh = 66, bottom = SCREEN_H - 150, lh = bottom - top;
  fill_rrect(x0 - 20, 130, w + 40, SCREEN_H - 260, 28, RGB(18, 22, 34), a);
  draw_icon(IC_FOLDER, x0 + 26, 172, 36, C_ACC2, a);
  draw_text(font(W_MED, 30), br_purpose ? _("Scegli la cartella dei giochi") : _("Scegli la cartella dei PKG"), x0 + 64, 150, C_WHITE, a, AL_L);
  const char *shown = br_path + strlen(OMEGA_SYSROOT);
  draw_text_fit(font(W_REG, 23), shown[0] ? shown : "/", x0 + 64, 190, w - 100, C_DIM, a, AL_L);
  int total = br_n + 1;   // + "Usa questa cartella"
  br_anim = approach(br_anim, (float)br_sel, 20.0f);
  float tgt = br_sel * rh + rh > lh ? (float)(br_sel * rh + rh - lh + rh) : 0;
  br_scroll = approach(br_scroll, tgt, 14.0f);
  SDL_Rect clip = { 0, top - 4, SCREEN_W, lh + 4 }; SDL_RenderSetClipRect(R, &clip);
  for (int i = 0; i < total; i++) {
    int y = top + 20 + i * rh - (int)br_scroll;
    if (y + rh < top || y > bottom) continue;
    float fa = clampf(1 - fabsf(br_anim - i), 0, 1);
    if (fa > 0.01f) fill_rrect(x0, y, w, rh - 8, 14, C_WHITE, (int)(a * 0.12f * fa) + (i == br_sel ? a * 4 / 100 : 0));
    if (i == br_sel) stroke_rrect(x0 - 3, y - 3, w + 6, rh - 2, 17, 3, C_WHITE, a);
    if (i == 0) { draw_icon(IC_CHECK, x0 + 36, y + (rh - 8) / 2, 28, C_OK, a); draw_text(font(W_MED, 26), _("Usa questa cartella"), x0 + 72, y + 14, C_OK, a, AL_L); }
    else { draw_icon(IC_FOLDER, x0 + 36, y + (rh - 8) / 2, 28, C_ACC2, a); draw_text_fit(font(W_REG, 26), br_names[i - 1], x0 + 72, y + 14, w - 110, C_TXT, a, AL_L); }
  }
  if (!br_n) draw_text(font(W_REG, 24), _("Nessuna sottocartella"), x0 + 72, top + 20 + rh + 14, C_FAINT, a, AL_L);
  SDL_RenderSetClipRect(R, NULL);
  int ic[3] = { IC_BTN_X, IC_BTN_O, IC_BTN_TRI }; const char *lb[3] = { _("Apri / scegli"), _("Su di un livello"), _("Annulla") };
  hints(ic, lb, 3, a);
}

void pkgs_draw(float t) {
  poll_scan();
  int a = (int)(255 * t);
  fill_rect(0, 0, SCREEN_W, SCREEN_H, mix(RGB(10, 12, 20), g_theme_base, 0.12f), t > 0.98f ? 255 : a);
  glow(SCREEN_W - 260, 160, 520, RGB(70, 210, 130), a * 12 / 100);
  if (br_on) { draw_browser(a); return; }
  int x0 = 110;
  draw_icon(IC_BOX, x0 + 22, 74, 44, RGB(90, 220, 150), a);
  draw_text(font(W_LIGHT, 38), _("Installa PKG"), x0 + 62, 52, C_WHITE, a, AL_L);
  draw_text_fit(font(W_REG, 22), _("Collega una chiavetta o un disco: i .pkg compaiono qui da soli. Installi con un tasto e segui la barra nella home."), x0 + 360, 66, SCREEN_W - x0 - 470, C_DIM, a, AL_L);

  // posizioni
  int lx = x0, lw = 470, ly = 150, lrh = 76;
  loc_anim = approach(loc_anim, (float)loc_sel, 20.0f);
  for (int i = 0; i < nloc; i++) {
    int y = ly + i * lrh; if (y > SCREEN_H - 200) break;
    float fa = clampf(1 - fabsf(loc_anim - i), 0, 1) * (focus_right ? 0.45f : 1);
    int on = i == loc_sel;
    fill_rrect(lx, y, lw, lrh - 10, 18, on ? mix(RGB(34, 40, 56), C_WHITE, fa) : RGB(255, 255, 255), on ? a : a * 5 / 100);
    Col fg = on && !focus_right ? RGB(12, 14, 22) : C_TXT;
    int ic = locs[i].kind == 0 ? IC_USB : locs[i].kind == 9 ? (locs[i].free_gb == A_BROWSE ? IC_SEARCH : IC_GEAR) : IC_FOLDER;
    draw_icon(ic, lx + 38, y + (lrh - 10) / 2, 28, on && !focus_right ? fg : C_ACC2, a);
    draw_text_fit(font(W_MED, 24), locs[i].label, lx + 72, y + (locs[i].kind == 0 ? 8 : 18), lw - 90, fg, a, AL_L);
    if (locs[i].kind == 0) { char fr[64]; snprintf(fr, sizeof fr, _("%.0f GB liberi"), locs[i].free_gb); draw_text(font(W_REG, 19), fr, lx + 72, y + 38, on && !focus_right ? RGB(40, 44, 60) : C_DIM, a, AL_L); }
  }

  // pacchetti
  int gx = lx + lw + 50, gw = SCREEN_W - gx - 100, top = 150, bottom = SCREEN_H - 140, rh = 132;
  if (SDL_AtomicGet(&scanning)) { draw_spinner(gx + gw / 2, top + 140, 22, a); draw_text(font(W_REG, 24), _("Cerco i pacchetti..."), gx + gw / 2, top + 190, C_DIM, a, AL_C); }
  else if (loc_sel < nloc && locs[loc_sel].kind == 9) {
    const char *msg = locs[loc_sel].free_gb == A_BROWSE ? _("Scegli una cartella qualsiasi (anche su un disco) e la aggiungi alle posizioni dei PKG.")
                                                        : _("Scegli dove stanno i tuoi giochi (cartelle con eboot.bin) e i tuoi PKG: Omega li guarda da sola, come ShadowMount, e appena colleghi un disco i giochi compaiono in home.");
    draw_text_wrap(font(W_REG, 27), msg, gx, top + 40, gw, 5, 40, C_TXT, a);
    draw_text(font(W_MED, 24), _("Premi ✕ per continuare"), gx, top + 260, C_ACC2, a, AL_L);
  } else if (!npk) {
    draw_icon(IC_DOWNLOAD, gx + gw / 2, top + 120, 80, C_FAINT, a);
    draw_text(font(W_MED, 30), _("Nessun .pkg qui"), gx + gw / 2, top + 190, C_TXT, a, AL_C);
    draw_text_wrap_al(font(W_REG, 23), _("Metti i file .pkg nella radice del disco o in una cartella qualsiasi (fino a 3 livelli sotto), oppure mandali dal telefono o dal PC con il Telecomando."), gx + gw / 2, top + 240, gw - 120, 3, 32, C_DIM, a, AL_C);
  } else {
    pk_anim = approach(pk_anim, (float)pk_sel, 20.0f);
    float tgt = pk_sel * rh + rh > bottom - top ? (float)(pk_sel * rh + rh - (bottom - top) + 20) : 0;
    pk_scroll = approach(pk_scroll, tgt, 14.0f);
    SDL_Rect clip = { gx - 20, top - 10, gw + 40, bottom - top + 10 }; SDL_RenderSetClipRect(R, &clip);
    for (int i = 0; i < npk; i++) {
      int y = top + i * rh - (int)pk_scroll;
      if (y + rh < top || y > bottom) continue;
      PkgEnt *k = &pk[i];
      if (k->icon[0] && !k->tex_st) { k->tex_st = 1; load_req(LOAD_ICON, strrchr(k->icon, '/') + 1, k->icon, 128, 128, 18, RGB(30, 60, 140), on_icon, NULL); }
      float fa = clampf(1 - fabsf(pk_anim - i), 0, 1) * (focus_right ? 1 : 0.3f);
      fill_rrect(gx, y, gw, rh - 14, 20, C_WHITE, (int)(a * (0.05f + 0.10f * fa)));
      if (focus_right && i == pk_sel) stroke_rrect(gx - 4, y - 4, gw + 8, rh - 6, 24, 3, C_WHITE, a);
      int is = rh - 34;
      if (k->tex) draw_tex(k->tex, gx + 12, y + 10, is, is, a);
      else { fill_rrect(gx + 12, y + 10, is, is, 16, RGB(30, 40, 62), a); draw_icon(IC_BOX, gx + 12 + is / 2, y + 10 + is / 2, is / 2, C_DIM, a); }
      int tx = gx + 12 + is + 24;
      draw_text_fit(font(W_MED, 27), k->title[0] ? k->title : k->name, tx, y + 14, gw - is - 260, C_TXT, a, AL_L);
      char sz[24], sub[200]; fmt_size(k->size, sz, sizeof sz);
      snprintf(sub, sizeof sub, "%s  \xC2\xB7  %s%s%s", sz, k->ps5 ? "PS5" : "PS4", k->cid[0] ? "  \xC2\xB7  " : "", k->cid);
      draw_text_fit(font(W_REG, 21), sub, tx, y + 54, gw - is - 260, C_DIM, a, AL_L);
      draw_text_fit(font(W_REG, 18), k->path + strlen(OMEGA_SYSROOT), tx, y + 84, gw - is - 260, C_FAINT, a, AL_L);
      if (k->installed) { int bw = 170; fill_rrect(gx + gw - bw - 20, y + (rh - 14) / 2 - 20, bw, 40, 20, RGB(0, 150, 90), a); draw_text(font(W_MED, 20), _("Installato"), gx + gw - bw / 2 - 20, y + (rh - 14) / 2 - 13, C_WHITE, a, AL_C); }
    }
    SDL_RenderSetClipRect(R, NULL);
    char cnt[64]; snprintf(cnt, sizeof cnt, npk == 1 ? _("%d pacchetto") : _("%d pacchetti"), npk);
    draw_text(font(W_REG, 22), cnt, gx + gw, 112, C_DIM, a, AL_R);
  }
  int ic[5]; const char *lb[5]; int n = 0;
  ic[n] = IC_BTN_X; lb[n++] = focus_right ? _("Installa") : _("Apri");
  if (focus_right && npk) { ic[n] = IC_BTN_TRI; lb[n++] = _("Installa tutti"); ic[n] = IC_BTN_SQ; lb[n++] = _("Opzioni"); }
  ic[n] = IC_BTN_O; lb[n++] = focus_right ? _("Posizioni") : _("Indietro");
  hints(ic, lb, n, a);
}

static void br_input(int b) {
  int total = br_n + 1;
  if (b == B_TRI) { br_on = 0; return; }
  if (b == B_UP && br_sel > 0) { br_sel--; sfx_play(SFX_MOVE); }
  else if (b == B_DOWN && br_sel < total - 1) { br_sel++; sfx_play(SFX_MOVE); }
  else if (b == B_O) {
    char *sl = strrchr(br_path, '/');
    if (!sl || sl <= br_path + strlen(OMEGA_SYSROOT)) { br_on = 0; return; }
    *sl = 0; br_load(); sfx_play(SFX_BACK);
  } else if (b == B_X) {
    if (br_sel == 0) {
      int r = paths_set(br_purpose, br_path, 1);
      set_msg(r > 0 ? (br_purpose ? _("Cartella dei giochi aggiunta: i giochi compaiono in home tra pochi secondi") : _("Cartella dei PKG aggiunta")) : r == 0 ? _("Questa cartella c'è già") : _("Impossibile salvare la cartella"), r < 0);
      br_on = 0; build_locs();
      if (!br_purpose) for (int i = 0; i < nloc; i++) if (!strcmp(locs[i].path, br_path)) { loc_sel = i; start_scan(); }
    } else {
      size_t L = strlen(br_path);
      snprintf(br_path + L, sizeof br_path - L, "/%s", br_names[br_sel - 1]);
      br_load(); sfx_play(SFX_OPEN);
    }
  }
}

void pkgs_input(int b) {
  if (br_on) { br_input(b); return; }
  if (!focus_right) {
    if (b == B_O) { ov_pop(); return; }
    if (b == B_UP && loc_sel > 0) { loc_sel--; sfx_play(SFX_MOVE); start_scan(); pk_sel = 0; pk_scroll = 0; }
    else if (b == B_DOWN && loc_sel < nloc - 1) { loc_sel++; sfx_play(SFX_MOVE); start_scan(); pk_sel = 0; pk_scroll = 0; }
    else if (b == B_X || b == B_RIGHT) {
      if (loc_sel < nloc && locs[loc_sel].kind == 9) {
        if (b != B_X) return;
        if (locs[loc_sel].free_gb == A_BROWSE) browse_open(0, NULL); else paths_menu();
      } else if (npk) { focus_right = 1; sfx_play(SFX_SELECT); }
    }
    return;
  }
  if (b == B_O || b == B_LEFT) { focus_right = 0; return; }
  if (b == B_UP && pk_sel > 0) { pk_sel--; sfx_play(SFX_MOVE); }
  else if (b == B_DOWN && pk_sel < npk - 1) { pk_sel++; sfx_play(SFX_MOVE); }
  else if (b == B_X && pk_sel < npk) install_one(&pk[pk_sel]);
  else if (b == B_TRI && npk) confirm_open(_("Installare tutti i pacchetti di questa posizione non ancora installati? Vanno in coda, uno alla volta."), _("Installa tutti"), all_yes, NULL);
  else if (b == B_SQ && pk_sel < npk) {
    static const char *it[3]; it[0] = _("Installa"); it[1] = _("Elimina il file"); it[2] = _("Aggiungi questa cartella alle posizioni");
    menu_open(pk[pk_sel].title[0] ? pk[pk_sel].title : pk[pk_sel].name, it, 3, pkg_opts, NULL);
  }
}
