// Omega UI — home in stile console:
//   barra: Giochi · Esplora ............ store  cerca  browser  notifiche  amici  ⚙  avatar  ora
//   fila dei giochi (quello a fuoco si ingrandisce e lo sfondo sfuma sul suo)
//   titolo  ·  [ Gioca ]  [ ••• ]
//   schede: amici che giocano ora · chi ci ha giocato · notizie del gioco
//   attività degli amici
// Scendendo, la pagina scorre verso l'alto come sulla console.
#include "app.h"
#include <dirent.h>
#include <fcntl.h>
#include <math.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

AppEntry apps[MAX_APPS];
int napps;

#ifdef PS5
typedef struct { uint32_t sz; uint32_t user_id; uint32_t app_opt; uint64_t crash_report; uint64_t check_flag; } LncAppParam;
int sceUserServiceGetForegroundUser(uint32_t *userId);
int sceLncUtilLaunchApp(const char *tid, const char *argv[], LncAppParam *param);
int sceLncUtilKillApp(uint32_t appId);
int sceSystemServiceGetAppIdOfRunningBigApp(void);
int sceSystemServiceGetAppTitleId(int appId, char *titleId);
static const char *APP_DIRS[] = { "/user/app", "/system_ex/app" };
static const char *META_DIRS[] = { "/user/appmeta", "/user/app", "/system_ex/app" };
static const char *META_SUB[] = { "", "/sce_sys", "/sce_sys" };
#define N_APP_DIRS 2
#define N_META 3
#else
static const char *APP_DIRS[] = { OMEGA_DIR "/apps" };
static const char *META_DIRS[] = { OMEGA_DIR "/apps" };
static const char *META_SUB[] = { "/sce_sys" };
#define N_APP_DIRS 1
#define N_META 1
#endif

// -------------------------------------------------------------------- stato --
enum { Z_TOP, Z_ROW, Z_ACT, Z_CARDS, Z_FEED };
enum { T_GAMES, T_EXPLORE };
static int tab = T_GAMES, zone = Z_ROW;
static int app_sel, top_sel = 0, act_sel, card_sel, feed_sel;
static int ex_zone = 1, ex_news_sel, ex_fr_sel, ex_act_sel;   // Esplora: 1 notizie, 2 amici, 3 attività
static float row_scroll, page_scroll, page_scroll_t, tab_anim;
static float tile_size[MAX_APPS];
static float sel_anim_card, sel_anim_feed, sel_anim_act, sel_anim_top;
static Uint32 focus_at; static int focus_loaded = -1;
static Uint32 last_feed;
static Uint32 entered_at;
static int launching = -1; static float launch_t; static Uint32 launch_done_at;

#define TOP_N 10  // Giochi, Esplora, Store, Musica, Cerca, Browser, Notifiche, Game Base, Impostazioni, Profilo
#define META_MAX (128 * 1024 - 1)
#define FOCUS_MS 300   // il fuoco deve restare fermo tanto prima di caricare sfondo e scheda

// ------------------------------------------------------------ scansione app --
static int is_title_id(const char *s) {
  if (strlen(s) != 9) return 0;
  for (int i = 0; i < 4; i++) if (s[i] < 'A' || s[i] > 'Z') return 0;
  for (int i = 4; i < 9; i++) if (s[i] < '0' || s[i] > '9') return 0;
  return 1;
}

// param.sfo (giochi PS4) da un buffer: titolo in italiano (TITLE_05) se c'è, altrimenti TITLE
static int sfo_title_buf(const char *b, int len, char *out, size_t n) {
  if (len <= 20 || memcmp(b, "\0PSF", 4)) return 0;
  uint32_t keyt = *(const uint32_t *)(b + 8), datat = *(const uint32_t *)(b + 12), cnt = *(const uint32_t *)(b + 16);
  int found = 0;
  for (int pass = 0; pass < 2 && !found; pass++) {
    const char *want = pass == 0 ? "TITLE_05" : "TITLE";
    for (uint32_t i = 0; i < cnt && 20 + i * 16 + 16 <= (uint32_t)len; i++) {
      const unsigned char *e = (const unsigned char *)b + 20 + i * 16;
      uint16_t ko = *(const uint16_t *)e; uint32_t dl = *(const uint32_t *)(e + 4), doff = *(const uint32_t *)(e + 12);
      if (keyt + ko >= (uint32_t)len || datat + doff + dl > (uint32_t)len) continue;
      if (!strcmp(b + keyt + ko, want)) { snprintf(out, n, "%.*s", (int)dl, b + datat + doff); found = out[0] != 0; break; }
    }
  }
  return found;
}

static int sfo_title(const char *path, char *out, size_t n) {
  size_t len = 0; char *b = file_read(path, META_MAX, &len);
  if (!b) return 0;
  int ok = sfo_title_buf(b, (int)len, out, n);
  free(b);
  return ok;
}

#ifdef PS5
static uint32_t be32(const unsigned char *p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }

// Giochi PS4 installati come un unico app.pkg, senza param.sfo estratto: il
// param.sfo sta in chiaro nella tabella delle voci dell'intestazione (id 0x1000).
static int pkg_title(const char *path, char *out, size_t n) {
  int fd = open(path, O_RDONLY); if (fd < 0) return 0;
  unsigned char h[32]; int ok = 0;
  if (read(fd, h, 32) == 32 && !memcmp(h, "\x7f" "CNT", 4)) {
    uint32_t count = be32(h + 0x10), table = be32(h + 0x18);
    if (count && count < 4096) {
      unsigned char *t = malloc((size_t)count * 32);
      if (t && lseek(fd, table, SEEK_SET) == (off_t)table && read(fd, t, (size_t)count * 32) == (ssize_t)(count * 32)) {
        for (uint32_t i = 0; i < count && !ok; i++) {
          const unsigned char *e = t + i * 32;
          if (be32(e) != 0x1000) continue;                 // param.sfo
          uint32_t off = be32(e + 16), size = be32(e + 20);
          if (!size || size > 256 * 1024) break;
          char *sfo = malloc(size);
          if (sfo && lseek(fd, off, SEEK_SET) == (off_t)off && read(fd, sfo, size) == (ssize_t)size) ok = sfo_title_buf(sfo, (int)size, out, n);
          free(sfo);
        }
      }
      free(t);
    }
  }
  close(fd);
  return ok;
}

// ripiego: il nome usato dal riconoscimento vocale (pronunciation.xml), in inglese
static int pron_title(const char *path, char *out, size_t n) {
  char *b = file_read(path, META_MAX, NULL); if (!b) return 0;
  int ok = 0;
  const char *l = strstr(b, "<language id=\"01\""); if (!l) l = b;
  const char *t = strstr(l, "<text>");
  if (t) { t += 6; const char *e = strstr(t, "</text>"); if (e && e > t) { snprintf(out, n, "%.*s", (int)(e - t), t); ok = out[0] != 0; } }
  free(b);
  return ok;
}
#endif

// param.json (giochi PS5): titolo nella lingua dell'app se c'è, altrimenti lingua predefinita
static int json_title(const char *path, char *out, size_t n) {
  char *b = file_read(path, META_MAX, NULL);
  if (!b) return 0;
  int ok = 0;
  JVal *j = json_parse(b);
  JVal *lp = jget(j, "localizedParameters");
  if (lp) {
    const char *def = jstr(lp, "defaultLanguage", "en-US");
    const char *langs[3] = { i18n_locale(), def, "en-US" };
    for (int i = 0; i < 3 && !ok; i++) {
      const char *t = jstr(jget(lp, langs[i]), "titleName", NULL);
      if (t && *t) { snprintf(out, n, "%s", t); ok = 1; }
    }
  }
  if (!ok) { const char *t = jstr(j, "titleName", NULL); if (t && *t) { snprintf(out, n, "%s", t); ok = 1; } }
  json_free(j);
  if (!ok && json_str(b, "titleName", out, n) == 0 && out[0]) ok = 1;
  free(b);
  return ok;
}

static void app_meta(AppEntry *a) {
  char p[300];
  a->name[0] = a->icon[0] = a->art[0] = 0;
  for (int d = 0; d < N_META; d++) {
    if (!a->name[0]) { snprintf(p, sizeof p, "%s/%s%s/param.json", META_DIRS[d], a->tid, META_SUB[d]); json_title(p, a->name, sizeof a->name); }
    if (!a->name[0]) { snprintf(p, sizeof p, "%s/%s%s/param.sfo", META_DIRS[d], a->tid, META_SUB[d]); sfo_title(p, a->name, sizeof a->name); }
    if (!a->icon[0]) { snprintf(p, sizeof p, "%s/%s%s/icon0.png", META_DIRS[d], a->tid, META_SUB[d]); if (access(p, 0) == 0) snprintf(a->icon, sizeof a->icon, "%s", p); }
    if (!a->art[0]) {
      const char *pics[3] = { "pic1.png", "pic0.png", "pic2.png" };
      for (int k = 0; k < 3 && !a->art[0]; k++) {
        snprintf(p, sizeof p, "%s/%s%s/%s", META_DIRS[d], a->tid, META_SUB[d], pics[k]);
        if (access(p, 0) == 0) snprintf(a->art, sizeof a->art, "%s", p);
      }
    }
  }
#ifdef PS5
  if (!a->name[0]) { snprintf(p, sizeof p, "/user/app/%s/app.pkg", a->tid); pkg_title(p, a->name, sizeof a->name); }
  if (!a->name[0]) { snprintf(p, sizeof p, "/user/appmeta/%s/pronunciation.xml", a->tid); pron_title(p, a->name, sizeof a->name); }
#endif
  if (!a->name[0]) snprintf(a->name, sizeof a->name, "%s", a->tid);
}

// "Ultimi giocati": quando si è avviato ogni titolo (OMEGA_DIR/recent.txt, "TID secondi")
#define RECENT_FILE OMEGA_DIR "/recent.txt"
static long recent_of(const char *tid) {
  FILE *f = fopen(RECENT_FILE, "r"); if (!f) return 0;
  char t[40]; long v, out = 0;
  while (fscanf(f, "%39s %ld", t, &v) == 2) if (!strcmp(t, tid)) out = v;
  fclose(f); return out;
}
static void recent_note(const char *tid) {
  char lines[200][56]; int n = 0;
  FILE *f = fopen(RECENT_FILE, "r");
  if (f) { char t[40]; long v; while (n < 199 && fscanf(f, "%39s %ld", t, &v) == 2) if (strcmp(t, tid)) snprintf(lines[n++], sizeof lines[0], "%s %ld", t, v); fclose(f); }
  f = fopen(RECENT_FILE, "w"); if (!f) return;
  for (int i = 0; i < n; i++) fprintf(f, "%s\n", lines[i]);
  fprintf(f, "%s %ld\n", tid, (long)time(NULL));
  fclose(f);
}

static int app_cmp(const AppEntry *a, const AppEntry *b) {
  if (g_prefs.sort == 1 && a->last_played != b->last_played) return a->last_played > b->last_played ? -1 : 1;
  if (g_prefs.sort == 2) { int ka = a->hb || a->pld, kb = b->hb || b->pld; if (ka != kb) return ka - kb; }
  return strcasecmp(a->name, b->name);
}

void scan_apps(void) {
  for (int i = 0; i < napps; i++) if (apps[i].tex) SDL_DestroyTexture(apps[i].tex);
  napps = 0;
  for (int d = 0; d < N_APP_DIRS && napps < MAX_APPS; d++) {
    DIR *dp = opendir(APP_DIRS[d]); if (!dp) continue;
    struct dirent *e;
    while ((e = readdir(dp)) && napps < MAX_APPS) {
      if (!is_title_id(e->d_name) || !strncmp(e->d_name, "NPXS", 4)) continue;
#ifdef PS5
      // solo i titoli registrati nel sistema (hanno l'appmeta): gli altri non si possono avviare
      { char meta[96]; snprintf(meta, sizeof meta, "/user/appmeta/%s", e->d_name); if (access(meta, 0) != 0) continue; }
#endif
      int dup = 0; for (int i = 0; i < napps; i++) if (!strcmp(apps[i].tid, e->d_name)) dup = 1;
      if (dup) continue;
      AppEntry *a = &apps[napps]; memset(a, 0, sizeof *a);
      snprintf(a->tid, sizeof a->tid, "%s", e->d_name);
      app_meta(a);
      a->avg = g_theme_base;
      napps++;
    }
    closedir(dp);
  }
  // homebrew websrv: compaiono come i giochi, tranne Omega stessa
  if (g_prefs.show_hb) {
    DIR *dp = opendir(OMEGA_HB_ROOT);
    struct dirent *e;
    while (dp && (e = readdir(dp)) && napps < MAX_APPS) {
      if (e->d_name[0] == '.' || !strcmp(e->d_name, "OmegaUI")) continue;
      char dir[256], f[320];
      snprintf(dir, sizeof dir, "%s/%s", OMEGA_HB_ROOT, e->d_name);
      int has_js = (snprintf(f, sizeof f, "%s/homebrew.js", dir), access(f, 0) == 0);
      int has_eboot = (snprintf(f, sizeof f, "%s/eboot.elf", dir), access(f, 0) == 0);
      if (!has_js && !has_eboot) continue;
      AppEntry *a = &apps[napps]; memset(a, 0, sizeof *a);
      a->hb = 1;
      snprintf(a->dir, sizeof a->dir, "%s", dir);
      snprintf(a->tid, sizeof a->tid, "HB%08x", fnv1a(e->d_name));
      if (!has_js || !hb_meta(dir, a->name, sizeof a->name, a->sub, sizeof a->sub)) snprintf(a->name, sizeof a->name, "%s", e->d_name);
      if (!hb_icon(dir, a->icon, sizeof a->icon)) a->icon[0] = 0;
      a->avg = g_theme_base;
      napps++;
    }
    if (dp) closedir(dp);
  }
  if (g_prefs.show_hb) napps = payload_scan(apps, napps, MAX_APPS);
  if (g_prefs.ext_games) napps = drives_merge(apps, napps, MAX_APPS);   // giochi sui dischi esterni
  for (int i = 0; i < napps; i++) apps[i].last_played = recent_of(apps[i].tid);
  for (int i = 0; i < napps; i++) for (int k = i + 1; k < napps; k++)
    if (app_cmp(&apps[k], &apps[i]) < 0) { AppEntry t = apps[i]; apps[i] = apps[k]; apps[k] = t; }
  omega_log("scan_apps: %d titoli", napps);
}

static void on_icon(const char *key, SDL_Texture *t, SDL_Color avg, void *ud) {
  (void)ud;
  for (int i = 0; i < napps; i++) if (!strcmp(apps[i].tid, key)) {
    apps[i].tex = t; apps[i].avg = avg; apps[i].tex_state = 2; apps[i].appear = 0;
    if (i == app_sel && focus_loaded == i) bg_set_game(apps[i].tid, apps[i].art, apps[i].avg);
    return;
  }
  if (t) SDL_DestroyTexture(t);
}

static void request_icons(void) {
  for (int i = 0; i < napps; i++) if (apps[i].tex_state == 0) {
    apps[i].tex_state = 1;
    if (apps[i].icon[0]) load_req(LOAD_ICON, apps[i].tid, apps[i].icon, 256, 256, 30, RGB(30, 60, 140), on_icon, NULL);
    else apps[i].tex_state = 2;
  }
}

void home_enter(void) {
  static int once;
  if (!once) { once = 1; drives_after_game(); }   // giochi esterni rimasti montati dall'ultima partita
  scan_apps();
  for (int i = 0; i < napps; i++) tile_size[i] = 150;
  if (app_sel >= napps) app_sel = 0;
  zone = Z_ROW; tab = T_GAMES; page_scroll = page_scroll_t = 0;
  request_icons();
  focus_at = SDL_GetTicks(); focus_loaded = -1;
  social_load_news(); social_load_activity(); social_load_friends();
  last_feed = SDL_GetTicks(); entered_at = SDL_GetTicks();
  if (!napps) bg_set_default();
}

// ---------------------------------------------------------------- avvio app --
#ifdef PS5
// Chiude il gioco già aperto (sospeso) se è un altro: altrimenti il sistema
// rifiuta il nuovo avvio con 0x80940010 (ALREADY_RUNNING_KILL_NEEDED).
static int kill_running_game(const char *except_tid) {
  int app = sceSystemServiceGetAppIdOfRunningBigApp();
  if (app < 0) return 0;
  char tid[64] = ""; sceSystemServiceGetAppTitleId(app, tid);
  if (!tid[0] || !strncmp(tid, "NPXS", 4) || !strcmp(tid, g_host_tid) || (except_tid && !strcmp(tid, except_tid))) return 0;
  int rc = sceLncUtilKillApp((uint32_t)app);
  omega_log("chiuso il gioco aperto %s (app 0x%x) rc=0x%x", tid, app, rc);
  SDL_Delay(1500);   // tempo al sistema per liberare le risorse
  return 1;
}
#endif

static void do_launch(int idx) {
  recent_note(apps[idx].tid);   // per l'ordine "Ultimi giocati"
  if (apps[idx].ext) {
    // gioco su disco esterno: si monta (sola lettura) e, la prima volta, si registra
    char err[300];
    if (drives_prepare_launch(&apps[idx], err, sizeof err) != 0) { set_msg(err, 1); social_presence("online", NULL, NULL); return; }
  }
  if (apps[idx].hb) {
    // parametri già risolti da hb_step: qui parte solo la richiesta a websrv
    char err[400];
    int r = hb_launch(apps[idx].dir, -1, 0, err, sizeof err, NULL, 0, NULL);
    if (r < 0) { set_msg(err, 1); social_presence("online", NULL, NULL); return; }
    if (r == 2) {              // demone (es. Transmission): Omega resta aperta
      char m[256]; snprintf(m, sizeof m, _("%s avviato in background"), apps[idx].name);
      set_msg(m, 0); social_presence("online", NULL, NULL); return;
    }
#ifdef PS5
    app_quit_later(4000);
#endif
    return;
  }
#ifdef PS5
  uint32_t uid = 0; sceUserServiceGetForegroundUser(&uid);
  const char *argv[] = { NULL };
  LncAppParam p; memset(&p, 0, sizeof p);
  p.sz = sizeof p; p.user_id = uid; p.check_flag = 1 /*SkipLaunchCheck*/;
  kill_running_game(apps[idx].tid);
  int rc = sceLncUtilLaunchApp(apps[idx].tid, argv, &p);
  omega_log("LncLaunch %s uid=0x%x rc=0x%x", apps[idx].tid, uid, rc);
  if (rc == (int)0x80940010 || rc == (int)0x8094000c) {
    // ancora un'app in mezzo: chiudila (anche se è lo stesso titolo, sospeso) e riprova
    int app = sceSystemServiceGetAppIdOfRunningBigApp();
    char tid[64] = ""; if (app >= 0) sceSystemServiceGetAppTitleId(app, tid);
    if (app >= 0 && strcmp(tid, g_host_tid)) { sceLncUtilKillApp((uint32_t)app); SDL_Delay(1500); }
    rc = sceLncUtilLaunchApp(apps[idx].tid, argv, &p);
    omega_log("LncLaunch (riprova) %s rc=0x%x", apps[idx].tid, rc);
  }
  if (rc == (int)0x80940031) { set_msg(_("Questo titolo non è registrato nel sistema e non può partire"), 1); social_presence("online", NULL, NULL); return; }
#else
  int rc = 0;
  omega_log("(desktop) avvio simulato di %s", apps[idx].tid);
#endif
  if (rc < 0) { char e[160]; snprintf(e, sizeof e, _("Avvio non riuscito (0x%x)"), rc); set_msg(e, 1); social_presence("online", NULL, NULL); return; }
#ifdef PS5
  // Omega si chiude e lascia tutta la memoria al gioco; al ritorno alla Home
  // il demone omega_redirect la riapre
  app_quit_later(2500);
#endif
}

static void start_launch(int idx);
// Homebrew websrv: homebrew.js si valuta un passo alla volta (click, sottomenu,
// scelta di file) e può usare la rete, quindi ogni passo gira in un thread e la
// home resta viva. L'esito arriva in hb_poll().
#define HB_MENU_N 16             // voci per pagina: quante ne mostra menu_open
static char hb_names[HB_MENU_N][64]; static const char *hb_items[HB_MENU_N]; static int hb_pending = -1;
static SDL_atomic_t hb_state;    // 0 libero, 1 al lavoro, 2 finito
static void hb_pick(int i, void *ud);
static int hb_rc, hb_nn, hb_arg, hb_shown; static Uint32 hb_t0;
static char hb_err[400], hb_dir[256], hb_tid[16];
static int hb_thread(void *ud) {
  (void)ud;
  hb_nn = 0;
  hb_rc = hb_launch(hb_dir, hb_arg, 1, hb_err, sizeof hb_err, hb_names, HB_MENU_N, &hb_nn);
  SDL_AtomicSet(&hb_state, 2);
  return 0;
}
// choice: -1 click sull'homebrew, >= 0 voce scelta nel menu precedente
static void hb_step(int idx, int choice) {
  if (SDL_AtomicGet(&hb_state)) return;
  snprintf(hb_dir, sizeof hb_dir, "%s", apps[idx].dir); snprintf(hb_tid, sizeof hb_tid, "%s", apps[idx].tid);
  hb_arg = choice; hb_t0 = SDL_GetTicks(); hb_shown = 0;
  SDL_AtomicSet(&hb_state, 1);
  SDL_Thread *t = SDL_CreateThreadWithStackSize(hb_thread, "hbstep", 2 * 1024 * 1024, NULL);
  if (t) SDL_DetachThread(t); else hb_thread(NULL);
}
static void hb_poll(void) {
  int s = SDL_AtomicGet(&hb_state);
  if (s == 1 && !hb_shown && SDL_GetTicks() - hb_t0 > 500) { hb_shown = 1; set_msg(_("Caricamento dell'homebrew..."), 0); }
  if (s != 2) return;
  SDL_AtomicSet(&hb_state, 0);
  int idx = -1;
  for (int i = 0; i < napps; i++) if (!strcmp(apps[i].tid, hb_tid)) idx = i;
  if (idx < 0) return;
  if (hb_rc < 0) { set_msg(hb_err, 1); return; }
  if (hb_shown) set_msg("", 0);
  if (hb_rc == 1) {
    for (int i = 0; i < hb_nn; i++) hb_items[i] = hb_names[i];
    hb_pending = idx;
    // titolo: quello dello script (es. pickFile) o il nome dell'app, con l'eventuale pagina "(1/3)"
    int own = hb_err[0] && hb_err[0] != '(';
    char t[96]; snprintf(t, sizeof t, "%s%s%s", own ? "" : apps[idx].name, !own && hb_err[0] ? " " : "", hb_err);
    menu_open(t, hb_items, hb_nn, hb_pick, NULL);
    return;
  }
  start_launch(idx);
}
// sottomenu degli homebrew: la scelta avvia il passo successivo
static void hb_pick(int i, void *ud) {
  (void)ud;
  if (hb_pending < 0 || hb_pending >= napps || i < 0) return;
  hb_step(hb_pending, i);
}

void launch_app(int idx) {
  if (idx < 0 || idx >= napps || launching >= 0 || SDL_AtomicGet(&hb_state)) return;
  if (apps[idx].hb) { hb_step(idx, -1); return; }
  if (apps[idx].pld) {
    char err[400];
    if (payload_run(apps[idx].dir, err, sizeof err) != 0) { set_msg(err, 1); return; }
    char m[256]; snprintf(m, sizeof m, _("%s avviato in background"), apps[idx].name);
    sfx_play(SFX_LAUNCH); set_msg(m, 0);
    return;
  }
  start_launch(idx);
}

static void start_launch(int idx) {
  if (idx < 0 || idx >= napps || launching >= 0) return;
  launching = idx; launch_t = 0; launch_done_at = 0;
  sfx_play(SFX_LAUNCH);
  social_presence("online", apps[idx].tid, apps[idx].name);
}

// ---------------------------------------------------- grafica delle notizie --
typedef struct { int color, w, h, r, top; SDL_Texture *t; Uint32 used; } ArtSlot;
static ArtSlot arts[24];

static SDL_Texture *make_art(int color, int w, int h, int r, int top_only) {
  SDL_Surface *s = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_ARGB8888);
  if (!s) return NULL;
  Col a = avatar_col(color * 3 + 1), b = avatar_col(color * 3 + 2);
  if (color == 0) { a = RGB(0, 70, 200); b = RGB(0, 180, 255); }
  Uint32 *px = s->pixels; int pitch = s->pitch / 4;
  for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) {
    float fx = (float)x / w, fy = (float)y / h;
    float t = clampf(fx * 0.7f + fy * 0.5f, 0, 1);
    float g1 = 0.5f / (1 + ((fx - 0.8f) * (fx - 0.8f) + (fy - 0.2f) * (fy - 0.2f)) * 14);
    float rings = 0.06f * sinf((hypotf(fx - 0.85f, fy - 0.1f)) * 40.0f) * (1 - fy);
    float k = 0.55f + g1 + rings;
    int R_ = (int)((a.r + (b.r - a.r) * t) * k), G_ = (int)((a.g + (b.g - a.g) * t) * k), B_ = (int)((a.b + (b.b - a.b) * t) * k);
    R_ = R_ > 255 ? 255 : R_; G_ = G_ > 255 ? 255 : G_; B_ = B_ > 255 ? 255 : B_;
    float cov = 1;
    float ccx = x < r ? (float)r : (x >= w - r ? (float)(w - r) : -1), ccy = y < r ? (float)r : (!top_only && y >= h - r ? (float)(h - r) : -1);
    if (ccx >= 0 && ccy >= 0) cov = clampf((float)r + 0.5f - hypotf(x + 0.5f - ccx, y + 0.5f - ccy), 0, 1);
    px[y * pitch + x] = ((Uint32)(cov * 255) << 24) | ((Uint32)R_ << 16) | ((Uint32)G_ << 8) | (Uint32)B_;
  }
  SDL_Texture *t = SDL_CreateTextureFromSurface(R, s);
  SDL_FreeSurface(s);
  if (t) SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
  return t;
}

// Immagini degli articoli, ritagliate alla misura della scheda, in una piccola cache.
typedef struct { char key[40]; SDL_Texture *t; int state; Uint32 used; float appear; } NewsImg;
static NewsImg nimg[40];
static void on_news_img(const char *key, SDL_Texture *t, SDL_Color avg, void *ud) {
  (void)avg; (void)ud;
  for (int i = 0; i < 40; i++) if (nimg[i].state == 1 && !strcmp(nimg[i].key, key)) {
    nimg[i].t = t; nimg[i].state = t ? 2 : 3; nimg[i].appear = 0; return;
  }
  if (t) SDL_DestroyTexture(t);
}
static NewsImg *news_image(const News *n, int w, int h, int r) {
  char key[40]; snprintf(key, sizeof key, "%s:%dx%d", n->id, w, h);
  NewsImg *victim = NULL;
  for (int i = 0; i < 40; i++) {
    if (nimg[i].state && !strcmp(nimg[i].key, key)) { nimg[i].used = g_frame; return &nimg[i]; }
    if (nimg[i].state == 1) continue;                       // in arrivo
    if (!victim || !nimg[i].state || (victim->state && nimg[i].used < victim->used)) victim = &nimg[i];
  }
  if (!victim) return NULL;
  if (victim->t) SDL_DestroyTexture(victim->t);
  memset(victim, 0, sizeof *victim);
  snprintf(victim->key, sizeof victim->key, "%s", key);
  victim->state = 1; victim->used = g_frame;
  char path[96]; snprintf(path, sizeof path, OMEGA_API "/news/%s/image", n->id);
  load_req(LOAD_URL, key, path, w, h, -r, RGB(0, 0, 0), on_news_img, NULL);
  return victim;
}

void news_art(const News *n, int x, int y, int w, int h, int radius, int alpha) {
  // radius < 0: solo gli angoli in alto (immagine in cima a una scheda)
  int top = radius < 0; if (top) radius = -radius;
  NewsImg *img = (n && n->has_image && top) ? news_image(n, w, h, radius) : NULL;
  if (img && img->state == 2 && img->t) {
    img->appear = approach(img->appear, 1, 8.0f);
    if (img->appear < 1) news_art(NULL, x, y, w, h, -radius, alpha);   // dissolvenza dalla grafica generata
    draw_tex(img->t, x, y, w, h, (int)(alpha * img->appear));
    grad_v(x, y + h / 2, w, h - h / 2, C_BLACK, 0, C_BLACK, alpha * 55 / 100);
    return;
  }
  int color = n ? (n->color & 7) : 0;
  ArtSlot *hit = NULL, *victim = &arts[0];
  for (int i = 0; i < 24; i++) {
    if (arts[i].t && arts[i].color == color && arts[i].w == w && arts[i].h == h && arts[i].r == radius && arts[i].top == top) { hit = &arts[i]; break; }
    if (!arts[i].t) { victim = &arts[i]; break; }
    if (arts[i].used < victim->used) victim = &arts[i];
  }
  if (!hit) {
    if (victim->t) SDL_DestroyTexture(victim->t);
    victim->t = make_art(color, w, h, radius, top); victim->color = color; victim->w = w; victim->h = h; victim->r = radius; victim->top = top;
    hit = victim;
  }
  hit->used = g_frame;
  draw_tex(hit->t, x, y, w, h, alpha);
  draw_icon(IC_NEWS, x + w - 60, y + 56, 56, C_WHITE, alpha * 35 / 100);
}

// --------------------------------------------------------------- barra alta --
// ora secondo Personalizza: 24 o 12 ore, con o senza secondi
void clock_text(char *out, size_t n) {
  time_t tt = time(NULL); struct tm *lt = localtime(&tt);
  int h = lt ? lt->tm_hour : 0, m = lt ? lt->tm_min : 0, sec = lt ? lt->tm_sec : 0;
  if (g_prefs.clock12) {
    int h12 = h % 12 ? h % 12 : 12;
    if (g_prefs.clock_sec) snprintf(out, n, "%d:%02d:%02d %s", h12, m, sec, h < 12 ? "AM" : "PM");
    else snprintf(out, n, "%d:%02d %s", h12, m, h < 12 ? "AM" : "PM");
  } else if (g_prefs.clock_sec) snprintf(out, n, "%02d:%02d:%02d", h, m, sec);
  else snprintf(out, n, "%02d:%02d", h, m);
}
// "lun 5 ott", con i nomi tradotti
void date_text(char *out, size_t n) {
  static const char *const WD[7] = { N_("dom"), N_("lun"), NULL, N_("mer"), N_("gio"), N_("ven"), N_("sab") };
  static const char *const MO[12] = { N_("gen"), N_("feb"), N_("mar"), N_("apr"), N_("mag"), N_("giu"), N_("lug"), N_("ago"), N_("set"), N_("ott"), N_("nov"), N_("dic") };
  time_t tt = time(NULL); struct tm *lt = localtime(&tt);
  if (!lt) { out[0] = 0; return; }
  const char *wd = lt->tm_wday == 2 ? P_("giorno", "mar") : _(WD[lt->tm_wday % 7]);   // "mar" da solo è marzo
  const char *mo = _(MO[lt->tm_mon % 12]);
  snprintf(out, n, "%s %d %s", wd, lt->tm_mday, mo);
}
static void top_bar(int alpha) {
  sel_anim_top = approach(sel_anim_top, (float)top_sel, 18.0f);
  int focus = zone == Z_TOP;
  int x = 110, y = 44;
  const char *tabs[2] = { _("Giochi"), _("Esplora") };
  for (int i = 0; i < 2; i++) {
    int on = tab == i;
    TTF_Font *f = font(on ? W_MED : W_LIGHT, 38);
    int w = text_w(f, tabs[i]);
    int foc = focus && top_sel == i;
    if (foc) fill_rrect(x - 22, y - 10, w + 44, 66, 33, C_WHITE, alpha * 22 / 100);
    draw_text(f, tabs[i], x, y, on ? C_WHITE : C_DIM, alpha, AL_L);
    if (on) fill_rrect(x + w / 2 - (int)(20 + 10 * tab_anim), y + 58, (int)(40 + 20 * tab_anim), 4, 2, C_WHITE, alpha);
    x += w + 64;
  }
  char clock[64]; clock_text(clock, sizeof clock);
  TTF_Font *cf = font(W_REG, 32);
  int rx = SCREEN_W - 80;
  rx -= draw_text(cf, clock, rx, y + 6, C_TXT, alpha, AL_R) + 22;
  // data e temperatura accanto all'ora (Personalizza › Orologio e barra)
  char extra[96] = "";
  if (g_prefs.clock_date) date_text(extra, sizeof extra);
  if (g_prefs.bar_temp) {
    static int temp = -1; static Uint32 temp_at;
    if (!temp_at || SDL_GetTicks() - temp_at > 5000) { temp = sys_cpu_temp(); temp_at = SDL_GetTicks(); }
    if (temp > 0) { size_t l = strlen(extra); snprintf(extra + l, sizeof extra - l, "%s%d\xC2\xB0" "C", l ? "  \xC2\xB7  " : "", temp); }
  }
  if (extra[0]) rx -= draw_text(font(W_REG, 24), extra, rx, y + 14, C_DIM, alpha, AL_R) + 22;
  rx -= 18;
  const int icons[8] = { IC_STORE, IC_MUSIC, IC_SEARCH, IC_GLOBE, IC_BELL, IC_FRIENDS, IC_GEAR, -1 };
  int badges[8] = { 0, 0, 0, 0, S.unread_notif, S.in_req + S.unread_msg + S.ninv, 0, 0 };
  int pos[8];
  // da destra: avatar, impostazioni, amici, notifiche, browser, cerca, musica, store
  int cx = rx - 30;
  for (int k = 7; k >= 0; k--) { pos[k] = cx; cx -= k == 7 ? 96 : 84; }
  for (int k = 0; k < 8; k++) {
    int idx = 2 + k, foc = focus && top_sel == idx;
    int px = pos[k], py = y + 24;
    if (k == 7) {
      if (foc) ring(px, py, 38, 3, C_WHITE, alpha);
      draw_avatar(S.me, S.my_avatar, px, py, 60, alpha);
      fill_circle(px + 22, py + 22, 9, RGB(10, 14, 24), alpha);
      fill_circle(px + 22, py + 22, 6, C_OK, alpha);
    } else {
      if (foc) fill_circle(px, py, 36, C_WHITE, alpha);
      // musica in corso: barrette al posto della nota, e il titolo sotto quando è a fuoco
      if (icons[k] == IC_MUSIC && music_now_line()) {
        music_mini(px - 12, py - 14, alpha);
        if (foc) draw_text_fit(font(W_REG, 22), music_now_line(), px, py + 46, 420, C_TXT, alpha, AL_C);
      } else draw_icon(icons[k], px, py, 38, foc ? RGB(10, 12, 20) : C_TXT, alpha);
      if (badges[k]) draw_badge(px + 22, py - 20, badges[k], alpha);
    }
  }
}

static void top_activate(void) {
  switch (top_sel) {
    case 0: tab = T_GAMES; tab_anim = 0; break;
    case 1: tab = T_EXPLORE; tab_anim = 0; social_load_news(); social_load_activity(); break;
    case 2: store_open(); break;
    case 3: music_open(); break;
    case 4: search_open(); break;
    case 5: browser_open(NULL); break;
    case 6: social_load_notifications(); ov_push(OV_NOTIF); break;
    case 7: gb_open(0); break;
    case 8: ov_push(OV_SETTINGS); break;
    case 9: profile_open(S.me); break;
  }
}

// -------------------------------------------------------------- fila giochi --
static void game_row(int y0, int alpha) {
  if (!napps) {
    int w = 900, h = 220, x = 110;
    fill_rrect(x, y0, w, h, 26, C_PANEL, alpha * 80 / 100);
    draw_icon(IC_GAMEPAD, x + 110, y0 + h / 2, 90, C_DIM, alpha);
    draw_text(font(W_MED, 34), _("Nessun gioco installato"), x + 200, y0 + 64, C_TXT, alpha, AL_L);
    draw_text(font(W_REG, 26), _("I giochi e le app installati compariranno qui."), x + 200, y0 + 116, C_DIM, alpha, AL_L);
    return;
  }
  // Personalizza › Dimensione delle icone: piccole, medie, grandi
  static const float BASE[3] = { 124, 150, 178 }, BIG[3] = { 200, 236, 270 }, MID[3] = { 170, 200, 230 };
  int ts = g_prefs.tiles % 3, gap = g_prefs.labels ? 30 : 22;
  for (int i = 0; i < napps; i++) {
    float target = (i == app_sel && tab == T_GAMES) ? (zone == Z_ROW ? BIG[ts] : MID[ts]) : BASE[ts];
    tile_size[i] = approach(tile_size[i], target, 14.0f);
  }
  float before = 0; for (int i = 0; i < app_sel; i++) before += tile_size[i] + gap;
  row_scroll = approach(row_scroll, before, 12.0f);
  float x = 110 - row_scroll;
  float pulse = 0.5f + 0.5f * sinf((float)g_time * 3.2f);
  for (int i = 0; i < napps; i++) {
    int s = (int)tile_size[i];
    int tx = (int)x, ty = y0;
    if (tx > SCREEN_W) break;
    if (tx + s > -40) {
      int a = alpha;
      if (tx < 110) a = (int)(alpha * clampf(1 - (110 - tx) / 200.0f, 0, 1) * 0.6f);   // quelli già passati sfumano
      AppEntry *ap = &apps[i];
      int foc = i == app_sel;
      if (foc) shadow_rrect(tx, ty, s, s, 28, 26, a * 70 / 100);
      if (ap->tex) {
        ap->appear = approach(ap->appear, 1, 8.0f);
        SDL_SetTextureColorMod(ap->tex, 255, 255, 255);
        draw_tex(ap->tex, tx, ty, s, s, (int)(a * ap->appear));
      } else {
        fill_rrect(tx, ty, s, s, 28, RGB(30, 38, 60), a);
        draw_icon(IC_GAMEPAD, tx + s / 2, ty + s / 2 - 14, s / 3, C_DIM, a);
        draw_text_fit(font(W_REG, 20), ap->tid, tx + s / 2, ty + s - 46, s - 20, C_FAINT, a, AL_C);
      }
      if (foc && zone == Z_ROW && tab == T_GAMES) focus_ring(tx, ty, s, s, 28, pulse, a);
      // gioco su disco esterno: piccolo disco in basso a destra
      if (ap->ext) {
        fill_circle(tx + s - 24, ty + s - 24, 19, RGB(10, 14, 24), a * 90 / 100);
        draw_icon(IC_DRIVE, tx + s - 24, ty + s - 24, 24, C_ACC2, a);
      }
      // Personalizza › Nomi sotto le icone (quello scelto ha già il titolo grande)
      if (g_prefs.labels && !foc) draw_text_fit(font(W_REG, 19), ap->name, tx + s / 2, ty + s + 8, s + 10, C_DIM, a * 85 / 100, AL_C);
      // amici che ci stanno giocando
      int playing = 0; for (int k = 0; k < S.nfriends; k++) if (!strcmp(S.friends[k].game_id, ap->tid)) playing++;
      if (playing) {
        fill_rrect(tx + 10, ty + s - 42, 70, 32, 16, RGB(10, 14, 24), a * 85 / 100);
        draw_icon(IC_FRIENDS, tx + 32, ty + s - 26, 22, C_OK, a);
        char n[8]; snprintf(n, sizeof n, "%d", playing);
        draw_text(font(W_BOLD, 20), n, tx + 50, ty + s - 38, C_TXT, a, AL_L);
      }
    }
    x += s + gap;
  }
}

// ------------------------------------------------------- dettagli del gioco --
static int count_playing(const char *tid) {
  int n = 0; for (int k = 0; k < S.nfriends; k++) if (!strcmp(S.friends[k].game_id, tid)) n++; return n;
}

static void game_info(int y0, int alpha) {
  if (!napps) return;
  AppEntry *ap = &apps[app_sel];
  TTF_Font *tf = font(W_LIGHT, 72);
  draw_text_fit(tf, ap->name, 110, y0, 1300, C_WHITE, alpha, AL_L);
  char sub[256], pl[96]; int np = count_playing(ap->tid);
  if (np) { snprintf(pl, sizeof pl, np == 1 ? _("%d amico sta giocando") : _("%d amici stanno giocando"), np); snprintf(sub, sizeof sub, "%s  \xC2\xB7  %s", ap->tid, pl); }
  else if (ap->hb) snprintf(sub, sizeof sub, "%s%s%s", _("Homebrew"), ap->sub[0] ? "  \xC2\xB7  " : "", ap->sub);
  else if (ap->ext) { char on[96]; snprintf(on, sizeof on, _("Su %s"), ap->drive); snprintf(sub, sizeof sub, "%s  \xC2\xB7  %s", ap->tid, on); }
  else snprintf(sub, sizeof sub, "%s  \xC2\xB7  %s", ap->tid, _("Installato"));
  int sx = 114;
  if (np) { fill_circle(sx + 8, y0 + 112, 7, C_OK, alpha); sx += 26; }
  draw_text(font(W_REG, 28), sub, sx, y0 + 96, np ? C_TXT : C_DIM, alpha, AL_L);

  sel_anim_act = approach(sel_anim_act, zone == Z_ACT ? (float)act_sel : -1.0f, 16.0f);
  int by = y0 + 168;
  float f0 = clampf(1 - fabsf(sel_anim_act - 0), 0, 1), f1 = clampf(1 - fabsf(sel_anim_act - 1), 0, 1);
  int w0 = pill(110, by, 84, _("Gioca"), IC_PLAY, zone == Z_ACT && act_sel == 0, f0 > 0 ? f0 : 0.0f, alpha);
  pill(110 + w0 + 20, by, 84, "", IC_MORE, zone == Z_ACT && act_sel == 1, f1, alpha);
  if (zone == Z_ACT) focus_ring(act_sel == 0 ? 110 : 110 + w0 + 20, by, act_sel == 0 ? w0 : 84, 84, 42, 0.5f + 0.5f * sinf((float)g_time * 3.2f), (int)(alpha * (act_sel == 0 ? f0 : f1)));
}

// schede sotto il gioco: amici che giocano, chi ha giocato, notizie
#define NCARD_FIXED 2
static int ncards(void) { return NCARD_FIXED + S.ngame_news; }

static void card_frame(int x, int y, int w, int h, int focused, float fa, int alpha) {
  if (focused) shadow_rrect(x, y, w, h, 24, 24, alpha * 60 / 100);
  fill_rrect(x, y, w, h, 24, C_PANEL, pref_panel_alpha(alpha * 88 / 100));
  if (fa > 0.02f) stroke_rrect(x - 6, y - 6, w + 12, h + 12, 30, 4, C_WHITE, (int)(alpha * fa));
}

static void card_people(int x, int y, int w, int h, const char *title, UserRef *u, int n, int now, int focused, float fa, int alpha) {
  card_frame(x, y, w, h, focused, fa, alpha);
  draw_icon(now ? IC_GAMEPAD : IC_CLOCK, x + 44, y + 44, 30, now ? C_OK : C_DIM, alpha);
  draw_text(font(W_MED, 26), title, x + 74, y + 28, C_TXT, alpha, AL_L);
  if (!n) {
    const char *empty = now ? _("Nessun amico sta giocando ora") : _("Nessun amico ci ha ancora giocato");
    draw_text_wrap(font(W_REG, 24), empty, x + 32, y + 100, w - 64, 2, 32, C_DIM, alpha);
    if (now && S.game_players > 0) {
      char m[160]; snprintf(m, sizeof m, S.game_players == 1 ? _("%d giocatore su Omega in questo momento") : _("%d giocatori su Omega in questo momento"), S.game_players);
      draw_text_fit(font(W_REG, 22), m, x + 32, y + h - 56, w - 64, C_FAINT, alpha, AL_L);
    }
    return;
  }
  int yy = y + 90;
  for (int i = 0; i < n && i < 3; i++) {
    draw_avatar(u[i].oid, u[i].avatar, x + 58, yy + 26, 52, alpha);
    if (now) { fill_circle(x + 78, yy + 46, 9, C_PANEL, alpha); fill_circle(x + 78, yy + 46, 6, C_OK, alpha); }
    draw_text_fit(font(W_MED, 25), u[i].oid, x + 100, yy + 2, w - 130, C_TXT, alpha, AL_L);
    char t[64]; if (now) play_time(u[i].when, t, sizeof t); else rel_time(u[i].when, t, sizeof t);
    draw_text(font(W_REG, 21), t, x + 100, yy + 32, C_DIM, alpha, AL_L);
    yy += 66;
  }
  if (n > 3) { char m[24]; snprintf(m, sizeof m, "+%d", n - 3); draw_text(font(W_MED, 22), m, x + w - 30, y + 32, C_DIM, alpha, AL_R); }
}

static void card_news(int x, int y, int w, int h, const News *n, int focused, float fa, int alpha) {
  card_frame(x, y, w, h, focused, fa, alpha);
  news_art(n, x, y, w, 128, -24, alpha);
  fill_rrect(x + 22, y + 22, text_w(font(W_BOLD, 18), n->tag) + 26, 32, 16, RGB(0, 0, 0), alpha * 45 / 100);
  draw_text(font(W_BOLD, 18), n->tag, x + 35, y + 27, C_WHITE, alpha, AL_L);
  draw_text_wrap(font(W_MED, 26), n->title, x + 26, y + 142, w - 52, 2, 32, C_TXT, alpha);
  char t[64]; rel_time(n->when, t, sizeof t);
  draw_text(font(W_REG, 20), t, x + 26, y + h - 40, C_FAINT, alpha, AL_L);
}

static void game_cards(int y0, int alpha) {
  sel_anim_card = approach(sel_anim_card, zone == Z_CARDS ? (float)card_sel : -5.0f, 16.0f);
  draw_text(font(W_MED, 30), _("Amici e notizie"), 110, y0 - 56, C_TXT, alpha, AL_L);
  if (S.game_loading && !S.ngame_now && !S.ngame_played) draw_spinner(380, y0 - 38, 10, alpha);
  int x = 110, h = 330;
  int widths[2] = { 470, 420 };
  float scroll_x = 0;
  // scorre quando la scheda a fuoco uscirebbe dallo schermo
  { int cx = 110; for (int i = 0; i < card_sel && zone == Z_CARDS; i++) cx += (i < 2 ? widths[i] : 440) + 26;
    if (cx > 1100) scroll_x = (float)(cx - 1100); }
  static float sx = 0; sx = approach(sx, scroll_x, 12.0f);
  x -= (int)sx;
  for (int i = 0; i < ncards(); i++) {
    int w = i < 2 ? widths[i] : 440;
    float fa = clampf(1 - fabsf(sel_anim_card - i), 0, 1);
    int foc = zone == Z_CARDS && card_sel == i;
    if (x < SCREEN_W && x + w > 0) {
      if (i == 0) card_people(x, y0, w, h, _("Stanno giocando ora"), S.game_now, S.ngame_now, 1, foc, fa, alpha);
      else if (i == 1) card_people(x, y0, w, h, _("Hanno giocato di recente"), S.game_played, S.ngame_played, 0, foc, fa, alpha);
      else card_news(x, y0, w, h, &S.game_news[i - 2], foc, fa, alpha);
    }
    x += w + 26;
  }
}

// attività degli amici
static void act_line(const Activity *a, char *out, size_t n) {
  if (!strcmp(a->type, "game_start")) snprintf(out, n, _("ha iniziato a giocare a %s"), a->game_name[0] ? a->game_name : a->game_id);
  else if (!strcmp(a->type, "online")) snprintf(out, n, "%s", _("è online"));
  else snprintf(out, n, "%s", a->detail[0] ? a->detail : a->type);
}

static void feed_row(int y0, int alpha, int *sel, float *anim, int focused_zone) {
  *anim = approach(*anim, focused_zone ? (float)*sel : -5.0f, 16.0f);
  draw_text(font(W_MED, 30), _("Cosa fanno i tuoi amici"), 110, y0 - 56, C_TXT, alpha, AL_L);
  int n = 0; const Activity *list[MAX_ACT];
  for (int i = 0; i < S.nact; i++) if (strcasecmp(S.act[i].oid, S.me)) list[n++] = &S.act[i];
  if (!n) {
    fill_rrect(110, y0, 900, 150, 24, C_PANEL, alpha * 80 / 100);
    draw_icon(IC_FRIENDS, 190, y0 + 75, 54, C_DIM, alpha);
    draw_text(font(W_MED, 28), _("Nessuna attività recente"), 260, y0 + 40, C_TXT, alpha, AL_L);
    draw_text(font(W_REG, 23), _("Aggiungi amici per vedere cosa giocano."), 260, y0 + 82, C_DIM, alpha, AL_L);
    return;
  }
  if (*sel >= n) *sel = n - 1;
  int w = 400, h = 180;
  static float sx[2]; float *sxp = &sx[sel == &feed_sel ? 0 : 1];
  float target = *sel > 3 && focused_zone ? (float)((*sel - 3) * (w + 24)) : 0;
  *sxp = approach(*sxp, target, 12.0f);
  int x = 110 - (int)*sxp;
  for (int i = 0; i < n; i++, x += w + 24) {
    if (x > SCREEN_W || x + w < 0) continue;
    float fa = clampf(1 - fabsf(*anim - i), 0, 1);
    card_frame(x, y0, w, h, focused_zone && *sel == i, fa, alpha);
    draw_avatar(list[i]->oid, list[i]->avatar, x + 62, y0 + 62, 72, alpha);
    draw_text_fit(font(W_MED, 26), list[i]->oid, x + 114, y0 + 32, w - 140, C_TXT, alpha, AL_L);
    char t[64]; rel_time(list[i]->when, t, sizeof t);
    draw_text(font(W_REG, 21), t, x + 114, y0 + 66, C_FAINT, alpha, AL_L);
    char l[200]; act_line(list[i], l, sizeof l);
    draw_text_wrap(font(W_REG, 24), l, x + 28, y0 + 112, w - 56, 2, 30, C_DIM, alpha);
  }
}

static const Activity *feed_item(int idx) {
  int n = 0;
  for (int i = 0; i < S.nact; i++) if (strcasecmp(S.act[i].oid, S.me)) { if (n == idx) return &S.act[i]; n++; }
  return NULL;
}
static int feed_count(void) { int n = 0; for (int i = 0; i < S.nact; i++) n += strcasecmp(S.act[i].oid, S.me) != 0; return n; }

// ------------------------------------------------------------------ Esplora --
static float ex_anim_news, ex_anim_fr, ex_anim_act, ex_scroll;

static void explore_draw(int alpha) {
  float target = ex_zone >= 3 ? 520.0f : ex_zone == 2 ? 200.0f : 0;
  ex_scroll = approach(ex_scroll, zone == Z_TOP ? 0 : target, 9.0f);
  int y = 150 - (int)ex_scroll;
  // notizie: la prima grande, le altre in fila
  ex_anim_news = approach(ex_anim_news, zone != Z_TOP && ex_zone == 1 ? (float)ex_news_sel : -5.0f, 16.0f);
  draw_text(font(W_MED, 30), _("Notizie dal mondo dei videogiochi"), 110, y, C_TXT, alpha, AL_L);
  y += 60;
  if (!S.nnews) { draw_spinner(140, y + 40, 14, alpha); }
  static float nsx; float nt = 0;
  { int cx = 0; for (int i = 0; i < ex_news_sel; i++) cx += (i == 0 ? 980 : 420) + 26; if (cx > 700) nt = (float)(cx - 700); }
  nsx = approach(nsx, ex_zone == 1 ? nt : 0, 12.0f);
  int x = 110 - (int)nsx;
  for (int i = 0; i < S.nnews; i++) {
    int w = i == 0 ? 980 : 420, h = 400;
    float fa = clampf(1 - fabsf(ex_anim_news - i), 0, 1);
    int foc = zone != Z_TOP && ex_zone == 1 && ex_news_sel == i;
    if (x < SCREEN_W && x + w > 0) {
      card_frame(x, y, w, h, foc, fa, alpha);
      int ah = i == 0 ? 250 : 180;
      news_art(&S.news[i], x, y, w, ah, -24, alpha);
      fill_rrect(x + 24, y + 24, text_w(font(W_BOLD, 18), S.news[i].tag) + 26, 32, 16, C_BLACK, alpha * 45 / 100);
      draw_text(font(W_BOLD, 18), S.news[i].tag, x + 37, y + 29, C_WHITE, alpha, AL_L);
      if (i == 0) {
        draw_text_fit(font(W_LIGHT, 44), S.news[i].title, x + 30, y + ah - 4, w - 60, C_WHITE, alpha, AL_L);
        draw_text_wrap(font(W_REG, 24), S.news[i].body, x + 30, y + ah + 56, w - 60, 2, 30, C_DIM, alpha);
      } else {
        draw_text_wrap(font(W_MED, 27), S.news[i].title, x + 26, y + ah + 6, w - 52, 2, 34, C_TXT, alpha);
        draw_text_wrap(font(W_REG, 22), S.news[i].body, x + 26, y + ah + 84, w - 52, 2, 28, C_DIM, alpha);
      }
      char t[64]; rel_time(S.news[i].when, t, sizeof t);
      draw_text(font(W_REG, 20), t, x + w - 28, y + h - 40, C_FAINT, alpha, AL_R);
    }
    x += w + 26;
  }
  y += 470;
  // amici online
  ex_anim_fr = approach(ex_anim_fr, zone != Z_TOP && ex_zone == 2 ? (float)ex_fr_sel : -5.0f, 16.0f);
  char hdr[128]; snprintf(hdr, sizeof hdr, _("Amici  \xC2\xB7  %d online"), friends_online_count());
  draw_text(font(W_MED, 30), hdr, 110, y, C_TXT, alpha, AL_L);
  y += 64;
  int nf = 0;
  for (int i = 0; i < S.nfriends; i++) {
    const Friend *f = &S.friends[i];
    int cx = 180 + nf * 190;
    float fa = clampf(1 - fabsf(ex_anim_fr - nf), 0, 1);
    if (fa > 0.02f) ring(cx, y + 70, 70, 4, C_WHITE, (int)(alpha * fa));
    draw_avatar(f->oid, f->avatar, cx, y + 70, 120, friend_online(f) ? alpha : alpha * 55 / 100);
    if (friend_online(f)) { fill_circle(cx + 42, y + 112, 13, RGB(10, 14, 24), alpha); fill_circle(cx + 42, y + 112, 9, f->game_id[0] ? C_ACC2 : C_OK, alpha); }
    draw_text_fit(font(W_MED, 24), f->oid, cx, y + 150, 170, friend_online(f) ? C_TXT : C_DIM, alpha, AL_C);
    draw_text_fit(font(W_REG, 20), f->game_id[0] ? f->game_name : friend_online(f) ? _("Online") : _("Offline"), cx, y + 182, 170, C_FAINT, alpha, AL_C);
    nf++;
    if (cx > SCREEN_W) break;
  }
  if (!S.nfriends) {
    draw_text(font(W_REG, 26), _("Nessun amico ancora: apri la Game Base per aggiungerne."), 110, y + 30, C_DIM, alpha, AL_L);
  }
  y += 290;
  feed_row(y, alpha, &ex_act_sel, &ex_anim_act, zone != Z_TOP && ex_zone == 3);
}

static float row_scroll_target_diff(void) {
  float before = 0; for (int i = 0; i < app_sel; i++) before += tile_size[i] + 22;
  return before - row_scroll;
}

// ------------------------------------------------------------------ disegno --
// disco collegato o tolto: si rifà la fila tenendo il gioco scelto, e lo si dice
static void ext_rescan(void) {
  static int seen;   // il primo giro (all'avvio) non va annunciato
  char keep[16]; snprintf(keep, sizeof keep, "%s", napps ? apps[app_sel].tid : "");
  int ext0 = 0; for (int i = 0; i < napps; i++) ext0 += apps[i].ext;
  scan_apps();
  int ext1 = 0; for (int i = 0; i < napps; i++) { ext1 += apps[i].ext; tile_size[i] = 150; }
  app_sel = 0; for (int i = 0; i < napps; i++) if (!strcmp(apps[i].tid, keep)) app_sel = i;
  request_icons();
  if (seen && ext1 > ext0) { char m[160]; snprintf(m, sizeof m, ext1 - ext0 == 1 ? _("Disco collegato: 1 gioco in più in home") : _("Disco collegato: %d giochi in più in home"), ext1 - ext0); set_msg(m, 0); }
  else if (seen && ext1 < ext0) set_msg(_("Disco scollegato: i suoi giochi sono spariti dalla home"), 0);
  seen = 1;
}

void home_update(void) {
  if (drives_changed() && launching < 0) ext_rescan();
  for (int i = 0; i < napps; i++) if (apps[i].tex_state == 0) { request_icons(); break; }
  // mentre la fila scorre o la pagina si muove, niente effetti costosi
  g_busy_motion = (fabsf(row_scroll_target_diff()) > 2 || fabsf(page_scroll - page_scroll_t) > 2) ? 1 : 0;
  if (napps && tab == T_GAMES && launching < 0) {
    if (focus_loaded != app_sel && SDL_GetTicks() - focus_at > FOCUS_MS) {
      focus_loaded = app_sel;
      bg_set_game(apps[app_sel].tid, apps[app_sel].art, apps[app_sel].avg);
      if (!apps[app_sel].hb) social_load_game(apps[app_sel].tid);
    }
  }
  if (tab == T_EXPLORE) bg_set_default();
  else if (focus_loaded == app_sel && napps && SDL_GetTicks() - focus_at > FOCUS_MS) bg_set_game(apps[app_sel].tid, apps[app_sel].art, apps[app_sel].avg);
  if (SDL_GetTicks() - last_feed > 30000) {
    last_feed = SDL_GetTicks();
    social_load_activity(); social_load_news();
    if (napps && tab == T_GAMES && !apps[app_sel].hb) { S.game_loading = 0; social_load_game(apps[app_sel].tid); }
  }
  hb_poll();
  if (launching >= 0) {
    launch_t += g_dt;
    if (launch_t > 0.75f && !launch_done_at) { do_launch(launching); launch_done_at = SDL_GetTicks(); }
    if (launch_done_at && SDL_GetTicks() - launch_done_at > 1500) launching = -1;
  }
  tab_anim = approach(tab_anim, 1, 8.0f);
}

void home_draw(void) {
  page_scroll_t = tab == T_GAMES ? (zone == Z_CARDS ? 470.0f : zone == Z_FEED ? 860.0f : 0.0f) : 0.0f;
  page_scroll = approach(page_scroll, page_scroll_t, 9.0f);
  int ps = (int)page_scroll;
  // velo più scuro quando la pagina scorre, per leggere le schede
  if (ps > 0) fill_rect(0, 0, SCREEN_W, SCREEN_H, RGB(4, 8, 18), (int)(150 * clampf(page_scroll / 470.0f, 0, 1)));
  particles_draw(90);   // quante e quali: Personalizza › Particelle
  SDL_Rect clip = { 0, 118, SCREEN_W, SCREEN_H - 118 };
  if (ps > 20 || tab == T_EXPLORE) SDL_RenderSetClipRect(R, &clip);
  if (tab == T_GAMES) {
    game_row(130 - ps, 255);
    game_info(440 - ps, 255);
    if (g_prefs.cards) {   // Personalizza › Sotto il gioco scelto
      game_cards(830 - ps, 255);
      feed_row(1260 - ps, 255, &feed_sel, &sel_anim_feed, zone == Z_FEED);
    }
  } else {
    explore_draw(255);
  }
  SDL_RenderSetClipRect(R, NULL);
  // sfumatura sotto la barra quando la pagina scorre
  if (ps > 20 || tab == T_EXPLORE) grad_v(0, 118, SCREEN_W, 70, RGB(4, 8, 18), (int)(170 * clampf(page_scroll / 200.0f + (tab == T_EXPLORE), 0, 1)), RGB(4, 8, 18), 0);
  top_bar(255);
  int ic[4]; const char *lb[4]; int n = 0;
  ic[n] = IC_BTN_X; lb[n++] = tab == T_GAMES && zone <= Z_ACT ? _("Gioca") : _("Seleziona");
  ic[n] = IC_BTN_TRI; lb[n++] = _("Game Base");
  ic[n] = IC_BTN_SQ; lb[n++] = _("Notifiche");
  ic[n] = IC_BTN_OPT; lb[n++] = _("Centro di controllo");
  // i comandi si vedono solo nei primi secondi, poi la home resta pulita
  Uint32 since = SDL_GetTicks() - entered_at;
  int ha = since < 9000 ? 255 : since < 10000 ? (int)(255 * (10000 - since) / 1000) : 0;
  if (g_prefs.hints == 0) ha = 0; else if (g_prefs.hints == 2) ha = 255;   // Personalizza › Barra dei comandi
  if (ov_depth() == 0 && ha > 0) {
    grad_v(0, SCREEN_H - 120, SCREEN_W, 120, RGB(4, 8, 18), 0, RGB(4, 8, 18), ha * 80 / 100);
    hints(ic, lb, n, ha);
  }

  // avvio gioco: il riquadro "vola" verso il centro e lo schermo si spegne
  if (launching >= 0) {
    float t = clampf(launch_t / 0.75f, 0, 1), e = ease_in_out(t);
    AppEntry *ap = &apps[launching];
    int s0 = 236, s1 = 420;
    int s = s0 + (int)((s1 - s0) * e);
    int x = (int)(110 + (SCREEN_W / 2 - s / 2 - 110) * e), y = (int)(130 + (SCREEN_H / 2 - s / 2 - 60 - 130) * e);
    fill_rect(0, 0, SCREEN_W, SCREEN_H, C_BLACK, (int)(235 * e));
    glow(x + s / 2, y + s / 2, s, ap->avg, (int)(160 * e));
    if (ap->tex) draw_tex(ap->tex, x, y, s, s, 255); else fill_rrect(x, y, s, s, 30, RGB(30, 38, 60), 255);
    draw_text(font(W_LIGHT, 40), ap->name, SCREEN_W / 2, y + s + 40, C_WHITE, (int)(255 * e), AL_C);
    if (launch_done_at) draw_spinner(SCREEN_W / 2, y + s + 130, 16, 220);
  }
}

// -------------------------------------------------------------------- input --
// I giochi si disinstallano, homebrew e payload si cancellano dalla loro cartella.
static void delete_confirmed(int idx, void *ud) {
  (void)ud;
  if (idx != 0 || !napps) return;
  AppEntry *ap = &apps[app_sel];
  char name[96]; snprintf(name, sizeof name, "%s", ap->name);
  int rc = ap->hb ? hb_remove(ap->dir, OMEGA_HB_ROOT) : ap->pld ? payload_remove(ap->dir) : store_uninstall(ap->tid);
  omega_log("elimina %s (%s): rc=0x%x", name, ap->hb ? ap->dir : ap->tid, (unsigned)rc);
  if (rc != 0) { char m[160]; snprintf(m, sizeof m, _("Eliminazione non riuscita (0x%x)"), (unsigned)rc); set_msg(m, 1); return; }
  scan_apps();
  for (int i = 0; i < napps; i++) tile_size[i] = 150;
  if (app_sel >= napps) app_sel = napps ? napps - 1 : 0;
  focus_at = SDL_GetTicks(); focus_loaded = -1;
  if (!napps) bg_set_default();
  char m[256]; snprintf(m, sizeof m, _("%s eliminato dalla console"), name);
  set_msg(m, 0);
}

static void game_more_menu(int idx, void *ud) {
  (void)ud;
  if (!napps) return;
  AppEntry *ap = &apps[app_sel];
  if (idx == 4) { invite_to_game_menu(ap->tid, ap->name); return; }
  if (idx == 3) {
    static char q[512];
    snprintf(q, sizeof q, ap->hb ? _("Eliminare l'homebrew %s dalla console?") : _("Eliminare %s dalla console? Il gioco verrà disinstallato."), ap->name);
    confirm_open(q, _("Elimina"), delete_confirmed, NULL);
    return;
  }
  if (idx == 0) launch_app(app_sel);
  else if (idx == 1) {
    if (!S.party.active) { set_msg(_("Non sei in un party: creane uno dalla Game Base"), 1); return; }
    char m[200]; snprintf(m, sizeof m, _("Giochiamo a %s?"), ap->name);
    social_party_send(m); set_msg(_("Invito a giocare inviato al party"), 0);
  } else if (idx == 2) { S.game_loading = 0; social_load_game(ap->tid); set_msg(_("Informazioni aggiornate"), 0); }
}

static UserRef pick_list[16]; static int npick;
static void pick_friend(int idx, void *ud) { (void)ud; if (idx >= 0 && idx < npick) profile_open(pick_list[idx].oid); }

static void open_people(UserRef *u, int n, const char *title) {
  if (!n) { set_msg(_("Nessun amico da mostrare"), 0); return; }
  static const char *items[16]; static char names[16][40];
  npick = n > 16 ? 16 : n;
  for (int i = 0; i < npick; i++) { pick_list[i] = u[i]; snprintf(names[i], sizeof names[i], "%s", u[i].oid); items[i] = names[i]; }
  menu_open(title, items, npick, pick_friend, NULL);
}

void home_input(int b) {
  if (launching >= 0) return;
  if (b == B_TRI) { gb_open(0); return; }
  if (b == B_OPT) { ov_push(OV_CC); return; }
  if (b == B_SQ) { social_load_notifications(); ov_push(OV_NOTIF); return; }
  if (b == B_L1 || b == B_R1) { tab = b == B_L1 ? T_GAMES : T_EXPLORE; tab_anim = 0; if (tab == T_EXPLORE) { social_load_news(); social_load_activity(); } return; }

  if (zone == Z_TOP) {
    if (b == B_LEFT && top_sel > 0) top_sel--;
    else if (b == B_RIGHT && top_sel < TOP_N - 1) top_sel++;
    else if (b == B_DOWN) { zone = Z_ROW; if (tab == T_EXPLORE) ex_zone = 1; }
    else if (b == B_X) top_activate();
    else if (b == B_O) { zone = Z_ROW; }
    return;
  }

  if (tab == T_EXPLORE) {
    if (b == B_UP) { if (ex_zone == 1) { zone = Z_TOP; top_sel = 1; } else ex_zone--; }
    else if (b == B_DOWN && ex_zone < 3) ex_zone++;
    else if (b == B_O) { zone = Z_TOP; top_sel = 1; }
    else if (ex_zone == 1) {
      if (b == B_LEFT && ex_news_sel > 0) ex_news_sel--;
      else if (b == B_RIGHT && ex_news_sel < S.nnews - 1) ex_news_sel++;
      else if (b == B_X && S.nnews) news_open(&S.news[ex_news_sel]);
    } else if (ex_zone == 2) {
      if (b == B_LEFT && ex_fr_sel > 0) ex_fr_sel--;
      else if (b == B_RIGHT && ex_fr_sel < S.nfriends - 1) ex_fr_sel++;
      else if (b == B_X && S.nfriends) profile_open(S.friends[ex_fr_sel].oid);
    } else {
      int n = feed_count();
      if (b == B_LEFT && ex_act_sel > 0) ex_act_sel--;
      else if (b == B_RIGHT && ex_act_sel < n - 1) ex_act_sel++;
      else if (b == B_X) { const Activity *a = feed_item(ex_act_sel); if (a) profile_open(a->oid); }
    }
    return;
  }

  switch (zone) {
    case Z_ROW:
      if (b == B_LEFT && app_sel > 0) { app_sel--; focus_at = SDL_GetTicks(); }
      else if (b == B_RIGHT && app_sel < napps - 1) { app_sel++; focus_at = SDL_GetTicks(); }
      else if (b == B_UP) { zone = Z_TOP; top_sel = 0; }
      else if (b == B_DOWN) { if (napps) { zone = Z_ACT; act_sel = 0; } else if (g_prefs.cards) zone = Z_FEED; }
      else if (b == B_X && napps) launch_app(app_sel);
      break;
    case Z_ACT:
      if (b == B_LEFT && act_sel > 0) act_sel--;
      else if (b == B_RIGHT && act_sel < 1) act_sel++;
      else if (b == B_UP || b == B_O) zone = Z_ROW;
      else if (b == B_DOWN && g_prefs.cards) { zone = Z_CARDS; card_sel = 0; }
      else if (b == B_X) {
        if (act_sel == 0) launch_app(app_sel);
        else { const char *it[] = { _("Gioca"), _("Proponi al party"), _("Aggiorna informazioni"), _("Elimina dalla console"), _("Invita un amico a giocare") }; menu_open(apps[app_sel].name, it, 5, game_more_menu, NULL); }
      }
      break;
    case Z_CARDS:
      if (b == B_LEFT && card_sel > 0) card_sel--;
      else if (b == B_RIGHT && card_sel < ncards() - 1) card_sel++;
      else if (b == B_UP || b == B_O) zone = Z_ACT;
      else if (b == B_DOWN) { zone = Z_FEED; feed_sel = 0; }
      else if (b == B_X) {
        if (card_sel == 0) open_people(S.game_now, S.ngame_now, _("Stanno giocando ora"));
        else if (card_sel == 1) open_people(S.game_played, S.ngame_played, _("Hanno giocato di recente"));
        else news_open(&S.game_news[card_sel - 2]);
      }
      break;
    case Z_FEED: {
      int n = feed_count();
      if (b == B_LEFT && feed_sel > 0) feed_sel--;
      else if (b == B_RIGHT && feed_sel < n - 1) feed_sel++;
      else if (b == B_UP || b == B_O) zone = napps ? Z_CARDS : Z_ROW;
      else if (b == B_X) { const Activity *a = feed_item(feed_sel); if (a) profile_open(a->oid); }
      break;
    }
  }
}
