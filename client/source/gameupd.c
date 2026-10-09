// Omega UI — Aggiornamenti dei giochi: per ogni gioco installato, l'ultimo
// aggiornamento ufficiale che il firmware della console può usare; con un
// tasto lo scarica dai server di Sony e lo installa.
//
// Il lavoro lo fa PatchDL (patchdl-src/, GPL-3.0, di Knutwurst): un servizio
// a parte (payloads/patchdl.elf, porta 12880) che legge app.db, interroga il
// version.xml ufficiale di ogni gioco, scarica i pezzi del pacchetto dai soli
// server CDN di Sony (con ripresa) e lo passa all'installatore di sistema.
// Qui c'è solo la schermata: la si apre, il servizio parte se serve, e i
// download vanno avanti anche chiudendo Omega. Installare dopo il download lo
// fa il demone (omega-redirect-src/source/main.c), leggendo GAMEUPD_PENDING.
#include "app.h"
#include <math.h>
#include <stdlib.h>
#include <sys/stat.h>

#define PDL "http://127.0.0.1:12880"
#define PDL_ELF OMEGA_HB_ROOT "/OmegaUI/payloads/patchdl.elf"
#define GAMEUPD_PENDING OMEGA_DIR "/gameupd-pending.txt"   // un title id per riga: da installare a download finito
#define MAXG 256

typedef struct {
  char tid[16], name[128], have[20], next[20], latest_fw[16], status[20], mode[24];
  int dl_state;            // 0 niente, 1 in coda/in corso, 2 in pausa, 3 scaricato, 4 errore
  int progress; long long bytes, total;
  int installing;          // installazione chiesta da qui
} Game;

static Game g[MAXG]; static int ng;
static int sel; static float sel_anim, scroll;
static int state;          // 0 avvio del servizio, 1 pronto, 2 servizio non disponibile
static Uint32 started_at, next_titles, next_dl, next_ping;
static int launched, configured, inflight_t, inflight_d;
static char err_msg[200];

// ------------------------------------------------------------- in sospeso --
static int pending_has(const char *tid) {
  char *f = file_read(GAMEUPD_PENDING, 64 * 1024, NULL); if (!f) return 0;
  int r = 0; for (char *l = strtok(f, "\n"); l; l = strtok(NULL, "\n")) if (!strcmp(l, tid)) { r = 1; break; }
  free(f); return r;
}
static void pending_set(const char *tid, int on) {
  char *f = file_read(GAMEUPD_PENDING, 64 * 1024, NULL);
  enum { OUTMAX = 64 * 1024 };
  char *out = malloc(OUTMAX); if (!out) { free(f); return; }
  size_t o = 0; out[0] = 0;
  if (f) {
    for (char *l = strtok(f, "\n"); l; l = strtok(NULL, "\n"))
      if (strcmp(l, tid) && l[0] && o + strlen(l) + 2 < OUTMAX) o += (size_t)snprintf(out + o, OUTMAX - o, "%s\n", l);
    free(f);
  }
  if (on && o + strlen(tid) + 2 < OUTMAX) o += (size_t)snprintf(out + o, OUTMAX - o, "%s\n", tid);
  FILE *w = fopen(GAMEUPD_PENDING, "w"); if (w) { fwrite(out, 1, o, w); fclose(w); }
  free(out);
}

static Game *find(const char *tid) { for (int i = 0; i < ng; i++) if (!strcmp(g[i].tid, tid)) return &g[i]; return NULL; }

// "01.004.000" → "1.04"; le versioni di Sony sono a zeri fissi
static void pretty_ver(const char *v, char *out, size_t n) {
  int a = 0, b = 0;
  if (!v || !v[0] || sscanf(v, "%d.%d", &a, &b) < 1) { snprintf(out, n, "\xE2\x80\x94"); return; }
  snprintf(out, n, "%d.%02d", a, b);
}

// -------------------------------------------------------------- risposte --
static void on_titles(int st, JVal *j, const char *raw, void *ud) {
  (void)raw; (void)ud; inflight_t = 0;
  if (st != 200 || !j || j->t != J_ARR) return;
  Game old[MAXG]; int nold = ng; memcpy(old, g, sizeof(Game) * (size_t)ng);
  ng = 0;
  JFOR(t, j) {
    if (ng >= MAXG) break;
    Game *x = &g[ng]; memset(x, 0, sizeof *x);
    jcpy(x->tid, sizeof x->tid, t, "title_id"); jcpy(x->name, sizeof x->name, t, "name");
    jcpy(x->have, sizeof x->have, t, "installed_version"); jcpy(x->next, sizeof x->next, t, "compatible_version");
    jcpy(x->latest_fw, sizeof x->latest_fw, t, "latest_required_fw"); jcpy(x->status, sizeof x->status, t, "status");
    jcpy(x->mode, sizeof x->mode, t, "mode");
    if (!x->tid[0]) continue;
    if (!x->name[0]) snprintf(x->name, sizeof x->name, "%s", x->tid);
    int known = 0;
    for (int k = 0; k < nold; k++) if (!strcmp(old[k].tid, x->tid)) {   // l'avanzamento arriva da /api/downloads
      x->dl_state = old[k].dl_state; x->progress = old[k].progress; x->bytes = old[k].bytes; x->total = old[k].total; x->installing = old[k].installing; known = 1;
    }
    if (!known) x->installing = pending_has(x->tid);   // chiesto in una visita precedente
    // installato davvero: niente più da fare per questo gioco
    if (x->installing && !strcmp(x->status, "up_to_date")) { x->installing = 0; pending_set(x->tid, 0); }
    ng++;
  }
  // prima quelli da aggiornare, poi gli altri in ordine di nome
  for (int i = 0; i < ng; i++) for (int k = i + 1; k < ng; k++) {
    int ri = !strcmp(g[i].status, "available") ? 0 : 1, rk = !strcmp(g[k].status, "available") ? 0 : 1;
    if (rk < ri || (rk == ri && strcasecmp(g[k].name, g[i].name) < 0)) { Game t = g[i]; g[i] = g[k]; g[k] = t; }
  }
  if (sel >= ng) sel = ng ? ng - 1 : 0;
}

static void on_downloads(int st, JVal *j, const char *raw, void *ud) {
  (void)raw; (void)ud; inflight_d = 0;
  if (st != 200 || !j || j->t != J_ARR) return;
  for (int i = 0; i < ng; i++) if (g[i].dl_state != 3 || !g[i].installing) g[i].dl_state = 0;
  JFOR(d, j) {
    Game *x = find(jstr(d, "title_id", ""));
    if (!x) continue;
    const char *s = jstr(d, "state", "");
    x->dl_state = !strcmp(s, "queued") || !strcmp(s, "active") ? 1 : !strcmp(s, "paused") ? 2 : !strcmp(s, "done") ? 3 : 4;
    x->progress = (int)jnum(d, "progress", 0); x->bytes = (long long)jnum(d, "bytes", 0); x->total = (long long)jnum(d, "total_bytes", 0);
  }
}

static void on_ping(int st, JVal *j, const char *raw, void *ud) {
  (void)j; (void)raw; (void)ud;
  if (st == 200) {
    if (state != 1) { state = 1; next_titles = next_dl = 0; }
    // PatchDL di suo mette una tessera nella home di sistema: qui la home è Omega
    if (!configured) { configured = 1; net_req(HTTP_POST, PDL "/api/config", "{\"home_shortcut\":false}", NULL, NULL); }
  } else if (state == 1) state = 0;
}

static void on_action(int st, JVal *j, const char *raw, void *ud) {
  (void)raw; (void)ud;
  if (st >= 200 && st < 300) { next_titles = next_dl = 0; return; }
  const char *why = j ? jstr(j, "reason", "") : "";
  char m[200];
  if (!strcmp(why, "install_not_allowed_for_source")) snprintf(m, sizeof m, "%s", _("Questo gioco non è installato in modo ufficiale: l'aggiornamento si può solo scaricare"));
  else if (!strcmp(why, "no_compatible_patch")) snprintf(m, sizeof m, "%s", _("Nessun aggiornamento compatibile con il firmware della console"));
  else if (!strcmp(why, "title_disabled")) snprintf(m, sizeof m, "%s", _("Aggiornamenti disattivati per questo gioco"));
  else if (!strcmp(why, "patch_title_mismatch")) snprintf(m, sizeof m, "%s", _("L'aggiornamento trovato è di un'altra edizione del gioco: non lo installo"));
  else if (j && jstr(j, "message", NULL)) snprintf(m, sizeof m, _("Installazione non riuscita: %s"), jstr(j, "message", ""));
  else snprintf(m, sizeof m, _("Il servizio degli aggiornamenti ha risposto %d"), st);
  set_msg(m, 1);
  next_titles = next_dl = 0;
}

static void title_post(const char *tid, const char *action) {
  char url[160]; snprintf(url, sizeof url, PDL "/api/titles/%s/%s", tid, action);
  net_req(HTTP_POST, url, "{}", on_action, NULL);
}

// ------------------------------------------------------------------ azioni --
static void start_update(Game *x) {
  if (strcmp(x->status, "available")) return;
  if (x->dl_state == 3) { x->installing = 1; title_post(x->tid, "install"); return; }
  if (strcmp(x->mode, "download_only")) pending_set(x->tid, 1);   // a download finito lo installa il demone
  x->installing = strcmp(x->mode, "download_only") != 0;
  x->dl_state = 1;
  title_post(x->tid, "download");
}

static void all_yes(int idx, void *ud) {
  (void)ud; if (idx != 0) return;
  int n = 0;
  for (int i = 0; i < ng; i++) if (!strcmp(g[i].status, "available") && !g[i].dl_state) { start_update(&g[i]); n++; }
  char m[120]; snprintf(m, sizeof m, n == 1 ? _("%d aggiornamento in coda") : _("%d aggiornamenti in coda"), n);
  set_msg(m, 0);
}

static char opt_tid[16];
static void opt_pick(int idx, void *ud) {
  Game *x = find(opt_tid); if (!x || idx < 0) return;
  // le voci cambiano con lo stato: si riconoscono dall'azione
  const char **acts = (const char **)ud;
  if (!acts || !acts[idx]) return;
  if (!strcmp(acts[idx], "go")) start_update(x);
  else if (!strcmp(acts[idx], "pause")) title_post(x->tid, "pause");
  else if (!strcmp(acts[idx], "cancel")) { pending_set(x->tid, 0); x->installing = 0; title_post(x->tid, "cancel"); }
  else if (!strcmp(acts[idx], "install")) { x->installing = 1; title_post(x->tid, "install"); }
}
static void options(Game *x) {
  static const char *it[4]; static const char *acts[5];
  int n = 0;
  snprintf(opt_tid, sizeof opt_tid, "%s", x->tid);
  if (x->dl_state == 1) { it[n] = _("Metti in pausa"); acts[n++] = "pause"; }
  else if (x->dl_state == 2 || x->dl_state == 4) { it[n] = _("Riprendi il download"); acts[n++] = "go"; }
  else if (x->dl_state == 3 && strcmp(x->mode, "download_only")) { it[n] = _("Installa adesso"); acts[n++] = "install"; }
  else if (!strcmp(x->status, "available")) { it[n] = _("Scarica e installa"); acts[n++] = "go"; }
  if (x->dl_state) { it[n] = _("Annulla ed elimina il download"); acts[n++] = "cancel"; }
  acts[n] = NULL;
  if (!n) { set_msg(_("Nessuna azione per questo gioco"), 0); return; }
  menu_open(x->name, it, n, opt_pick, acts);
}

// -------------------------------------------------------------- pannello --
static void restart(void) {
  state = 0; launched = 0; configured = 0; err_msg[0] = 0;
  started_at = SDL_GetTicks(); next_ping = 0;
}
void gameupd_open(void) {
  restart();
  sel = 0; sel_anim = 0; scroll = 0;
  ov_push(OV_GAMEUPD);
}

static void tick(void) {
  Uint32 now = SDL_GetTicks();
  if (state != 1) {
    if (now >= next_ping) { next_ping = now + 1000; net_req(HTTP_GET, PDL "/api/status", NULL, on_ping, NULL); }
    // il servizio non risponde: lo si avvia (una volta), poi si aspetta fino a 25 s
    if (!launched && now - started_at > 1500) {
      launched = 1;
      struct stat st;
      if (stat(PDL_ELF, &st) != 0) { state = 2; snprintf(err_msg, sizeof err_msg, "%s", _("Manca il servizio degli aggiornamenti: aggiorna Omega")); return; }
      char e[160] = "";
      if (payload_run_service(PDL_ELF, e, sizeof e) != 0) { state = 2; snprintf(err_msg, sizeof err_msg, "%s", e); }
    }
    if (state == 0 && launched && now - started_at > 25000) { state = 2; snprintf(err_msg, sizeof err_msg, "%s", _("Il servizio degli aggiornamenti non è partito: serve un loader di payload (etaHEN o elfldr)")); }
    return;
  }
  if (now >= next_ping) { next_ping = now + 5000; net_req(HTTP_GET, PDL "/api/status", NULL, on_ping, NULL); }
  if (!inflight_t && now >= next_titles) { inflight_t = 1; next_titles = now + 4000; net_req(HTTP_GET, PDL "/api/titles", NULL, on_titles, NULL); }
  if (!inflight_d && now >= next_dl) { inflight_d = 1; next_dl = now + 1000; net_req(HTTP_GET, PDL "/api/downloads", NULL, on_downloads, NULL); }
}

static void fmt_bytes(long long b, char *out, size_t n) {
  if (b >= 1000000000LL) snprintf(out, n, _("%.1f GB"), b / 1e9);
  else snprintf(out, n, _("%.0f MB"), b / 1e6);
}

void gameupd_draw(float t) {
  tick();
  int a = (int)(255 * t);
  fill_rect(0, 0, SCREEN_W, SCREEN_H, mix(RGB(10, 12, 20), g_theme_base, 0.12f), t > 0.98f ? 255 : a);
  glow(SCREEN_W - 260, 160, 520, RGB(60, 150, 255), a * 12 / 100);
  int x0 = 110, w = SCREEN_W - 2 * x0;
  draw_icon(IC_DOWNLOAD, x0 + 22, 74, 44, RGB(90, 170, 255), a);
  draw_text(font(W_LIGHT, 38), _("Aggiornamenti dei giochi"), x0 + 62, 52, C_WHITE, a, AL_L);
  draw_text_fit(font(W_REG, 22), _("Aggiornamenti ufficiali dai server di Sony, solo per i giochi installati e compatibili con il tuo firmware. Il download continua anche se chiudi Omega."), x0 + 560, 66, w - 560, C_DIM, a, AL_L);

  int top = 150, bottom = SCREEN_H - 140, rh = 110;
  if (state == 0) {
    draw_spinner(SCREEN_W / 2, top + 200, 26, a);
    draw_text(font(W_REG, 26), _("Avvio del servizio degli aggiornamenti..."), SCREEN_W / 2, top + 250, C_DIM, a, AL_C);
  } else if (state == 2) {
    draw_icon(IC_CLOSE, SCREEN_W / 2, top + 180, 70, C_ERR, a);
    draw_text_wrap_al(font(W_REG, 26), err_msg, SCREEN_W / 2, top + 250, 1100, 3, 36, C_TXT, a, AL_C);
  } else if (!ng) {
    draw_spinner(SCREEN_W / 2, top + 200, 26, a);
    draw_text(font(W_REG, 26), _("Leggo i giochi installati..."), SCREEN_W / 2, top + 250, C_DIM, a, AL_C);
  } else {
    int avail = 0; for (int i = 0; i < ng; i++) if (!strcmp(g[i].status, "available")) avail++;
    char cnt[96]; snprintf(cnt, sizeof cnt, avail == 1 ? _("%d aggiornamento disponibile") : _("%d aggiornamenti disponibili"), avail);
    draw_text(font(W_REG, 22), cnt, x0 + w, 112, avail ? C_ACC2 : C_DIM, a, AL_R);
    sel_anim = approach(sel_anim, (float)sel, 20.0f);
    float tgt = sel * rh + rh > bottom - top ? (float)(sel * rh + rh - (bottom - top) + 20) : 0;
    scroll = approach(scroll, tgt, 14.0f);
    SDL_Rect clip = { x0 - 20, top - 10, w + 40, bottom - top + 10 }; SDL_RenderSetClipRect(R, &clip);
    for (int i = 0; i < ng; i++) {
      int y = top + i * rh - (int)scroll;
      if (y + rh < top || y > bottom) continue;
      Game *x = &g[i];
      float fa = clampf(1 - fabsf(sel_anim - i), 0, 1);
      fill_rrect(x0, y, w, rh - 14, 20, C_WHITE, (int)(a * (0.05f + 0.10f * fa)));
      if (i == sel) stroke_rrect(x0 - 4, y - 4, w + 8, rh - 6, 24, 3, C_WHITE, a);
      draw_text_fit(font(W_MED, 27), x->name, x0 + 28, y + 14, w - 560, C_TXT, a, AL_L);
      char have[24], next[24], sub[200]; pretty_ver(x->have, have, sizeof have); pretty_ver(x->next, next, sizeof next);
      if (!strcmp(x->status, "available")) snprintf(sub, sizeof sub, _("%s  \xC2\xB7  versione %s \xE2\x86\x92 %s"), x->tid, have, next);
      else snprintf(sub, sizeof sub, _("%s  \xC2\xB7  versione %s"), x->tid, have);
      draw_text_fit(font(W_REG, 21), sub, x0 + 28, y + 56, w - 560, C_DIM, a, AL_L);

      // a destra: stato, o barra del download
      int bx = x0 + w - 500, bw = 470, by = y + (rh - 14) / 2;
      if (x->dl_state == 1 || x->dl_state == 2) {
        char p[120], b1[24], b2[24]; fmt_bytes(x->bytes, b1, sizeof b1); fmt_bytes(x->total, b2, sizeof b2);
        if (x->dl_state == 2) snprintf(p, sizeof p, _("In pausa \xC2\xB7 %d%%"), x->progress);
        else if (x->total > 0) snprintf(p, sizeof p, _("%s di %s \xC2\xB7 %d%%"), b1, b2, x->progress);
        else snprintf(p, sizeof p, "%s", _("In coda..."));
        draw_text_fit(font(W_REG, 21), p, bx, by - 34, bw, x->dl_state == 2 ? C_WARN : C_TXT, a, AL_L);
        fill_rrect(bx, by + 4, bw, 12, 6, RGB(255, 255, 255), a * 12 / 100);
        fill_rrect(bx, by + 4, (int)(bw * x->progress / 100.0f), 12, 6, x->dl_state == 2 ? C_WARN : C_ACC, a);
      } else {
        const char *lab; Col c;
        char tmp[96];
        if (x->dl_state == 3 && x->installing) { lab = _("Installazione: la segui nelle notifiche"); c = C_ACC2; }
        else if (x->dl_state == 3) { lab = _("Scaricato: premi \xE2\x9C\x95 per installare"); c = C_ACC2; }
        else if (x->dl_state == 4) { lab = _("Download interrotto: \xE2\x9C\x95 per riprendere"); c = C_ERR; }
        else if (!strcmp(x->status, "available")) { lab = !strcmp(x->mode, "download_only") ? _("Disponibile (solo download)") : _("Disponibile"); c = C_OK; }
        else if (!strcmp(x->status, "up_to_date")) { lab = _("Aggiornato"); c = C_DIM; }
        else if (!strcmp(x->status, "checking")) { lab = _("Controllo..."); c = C_FAINT; }
        else if (!strcmp(x->status, "incompatible_fw")) { snprintf(tmp, sizeof tmp, _("Serve il firmware %s"), x->latest_fw); lab = tmp; c = C_WARN; }
        else if (!strcmp(x->status, "blocked")) { lab = _("Non installato in modo ufficiale"); c = C_FAINT; }
        else { lab = _("Nessun aggiornamento"); c = C_FAINT; }
        draw_text_fit(font(W_MED, 22), lab, bx + bw, by - 14, bw, c, a, AL_R);
      }
    }
    SDL_RenderSetClipRect(R, NULL);
  }
  int ic[4]; const char *lb[4]; int n = 0;
  if (state == 1 && ng) { ic[n] = IC_BTN_X; lb[n++] = _("Aggiorna"); ic[n] = IC_BTN_TRI; lb[n++] = _("Aggiorna tutti"); ic[n] = IC_BTN_SQ; lb[n++] = _("Opzioni"); }
  if (state == 2) { ic[n] = IC_BTN_X; lb[n++] = _("Riprova"); }
  ic[n] = IC_BTN_O; lb[n++] = _("Indietro");
  hints(ic, lb, n, a);
}

void gameupd_input(int b) {
  if (b == B_O) { ov_pop(); return; }
  if (state == 2 && b == B_X) { restart(); return; }
  if (state != 1 || !ng) return;
  if (b == B_UP && sel > 0) { sel--; sfx_play(SFX_MOVE); }
  else if (b == B_DOWN && sel < ng - 1) { sel++; sfx_play(SFX_MOVE); }
  else if (b == B_X) {
    Game *x = &g[sel];
    if (x->dl_state == 1) { options(x); return; }
    if (x->dl_state == 2 || x->dl_state == 4) { x->dl_state = 1; title_post(x->tid, "download"); sfx_play(SFX_SELECT); return; }
    if (x->dl_state == 3 && !x->installing && strcmp(x->mode, "download_only")) { x->installing = 1; title_post(x->tid, "install"); sfx_play(SFX_SELECT); return; }
    if (!strcmp(x->status, "available")) { start_update(x); sfx_play(SFX_SELECT); }
    else options(x);
  }
  else if (b == B_TRI) confirm_open(_("Scaricare e installare tutti gli aggiornamenti disponibili? Vanno in coda, uno alla volta."), _("Aggiorna tutti"), all_yes, NULL);
  else if (b == B_SQ) options(&g[sel]);
}

