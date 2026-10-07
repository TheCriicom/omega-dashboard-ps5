// Omega UI — browser. Il server (GET /browse) scarica la pagina e la riduce a
// blocchi (titoli, paragrafi, link, immagini, risultati di ricerca): qui si
// impaginano una volta al caricamento e si disegnano solo quelli visibili.
//   su/giù link precedente/successivo   sin/des pagina su/giù   ✕ apri
//   ○ indietro o chiudi   L1/R1 cronologia   △ indirizzo   □ menu
// Segnalibri e cronologia sono file di testo in OMEGA_DIR.
#include "app.h"
#include <math.h>
#include <stdlib.h>

#define BR_X 330
#define BR_W 1260
#define BR_TOP 150
#define MAX_HIST 50
#define MAX_BM 40
#define BM_FILE   OMEGA_DIR "/browser-bookmarks.txt"
#define HIST_FILE OMEGA_DIR "/browser-history.txt"
#define SEARCH_PREFIX "omega://cerca?q="

typedef struct {
  char t;               // h titolo, p, a link, l voce di elenco, q citazione, c codice, i immagine, r risultato, m meta
  int level, big;
  char *x, *u, *s, *d;  // testo, url, snippet, url mostrato
  char img[24];
  int y, h;             // impaginazione
  char **lines; int nlines;
  char **slines; int nslines;   // righe dello snippet (risultati)
  SDL_Texture *tex; int tex_state; int tw, th;
} Blk;

typedef struct { char url[600]; float scroll; int focus; } HistEntry;

static Blk *blk; static int nblk;
static char pg_title[220], pg_site[120], pg_url[600], pg_mode[16];
static int loading, load_gen, focus = -1, content_h;
static float scroll, scroll_t;
static HistEntry hist[MAX_HIST]; static int nhist, hidx = -1;
static char bm_title[MAX_BM][120], bm_url[MAX_BM][600]; static int nbm, bm_loaded;
static char err_msg[200];
static float load_anim;
static int full_page;          // 1: pagina completa invece della modalità lettura

// Il browser vero della PS5 (WebKit di sistema): le pagine moderne con
// JavaScript, i video, gli accessi. Lo apre sceSystemServiceLaunchWebBrowser,
// come fa il comando "browse" di shsrv.
#ifdef PS5
int sceSystemServiceLaunchWebBrowser(const char *uri, void *param);
int sceUserServiceInitialize(void *params);
#endif
static void system_browser(const char *url) {
  const char *u = url && *url && strncmp(url, "omega://", 8) ? url : "https://www.google.com";
#ifdef PS5
  static int inited; if (!inited) { sceUserServiceInitialize(NULL); inited = 1; }
  int rc = sceSystemServiceLaunchWebBrowser(u, NULL);
  omega_log("browser di sistema %s -> 0x%x", u, rc);
  if (rc < 0) { char m[160]; snprintf(m, sizeof m, _("Il browser della PS5 non si è aperto (0x%08X)"), (unsigned)rc); set_msg(m, 1); }
  else set_msg(_("Apro il browser della PS5: per tornare in Omega premi il tasto PS"), 0);
#else
  omega_log("(desktop) browser di sistema: %s", u);
  set_msg(_("Apro il browser della PS5: per tornare in Omega premi il tasto PS"), 0);
#endif
}
// siti rapidi della pagina iniziale
static const struct { const char *name, *url; int ic; } QUICK[] = {
  { "Wikipedia", "https://it.m.wikipedia.org", IC_NEWS }, { "Wololo", "https://wololo.net", IC_GAMEPAD }, { "PSX-Place", "https://www.psx-place.com", IC_GAMEPAD },
  { "GitHub", "https://github.com/ps5-payload-dev", IC_BOX }, { "Reddit", "https://old.reddit.com/r/ps5homebrew", IC_CHAT }, { "Meteo", "https://wttr.in/?format=4", IC_CLOUD },
};
#define NQUICK (int)(sizeof QUICK / sizeof *QUICK)

// --------------------------------------------------------------- segnalibri --
static void bm_save(void) {
  FILE *f = fopen(BM_FILE, "w"); if (!f) return;
  for (int i = 0; i < nbm; i++) fprintf(f, "%s\t%s\n", bm_title[i], bm_url[i]);
  fclose(f);
}
static void bm_load(void) {
  if (bm_loaded) return;
  bm_loaded = 1;
  FILE *f = fopen(BM_FILE, "r");
  char line[800];
  if (f) {
    while (nbm < MAX_BM && fgets(line, sizeof line, f)) {
      line[strcspn(line, "\r\n")] = 0;
      char *tab = strchr(line, '\t'); if (!tab) continue;
      *tab = 0; snprintf(bm_title[nbm], sizeof bm_title[0], "%s", line); snprintf(bm_url[nbm], sizeof bm_url[0], "%s", tab + 1); nbm++;
    }
    fclose(f);
    return;
  }
  static const char *def[][2] = {
    { "Wikipedia", "https://it.m.wikipedia.org/" }, { "Multiplayer.it", "https://multiplayer.it/" },
    { "Everyeye", "https://www.everyeye.it/" }, { "PlayStation Blog", "https://blog.playstation.com/" },
    { "IGN Italia", "https://it.ign.com/" }, { "ANSA", "https://www.ansa.it/" },
    { "Il Meteo", "https://www.ilmeteo.it/" }, { "Push Square", "https://www.pushsquare.com/" },
  };
  for (unsigned i = 0; i < sizeof def / sizeof def[0]; i++) { snprintf(bm_title[nbm], sizeof bm_title[0], "%s", def[i][0]); snprintf(bm_url[nbm], sizeof bm_url[0], "%s", def[i][1]); nbm++; }
  bm_save();
}
static void history_append(const char *title, const char *url) {
  if (!strncmp(url, "omega://", 8)) return;
  FILE *f = fopen(HIST_FILE, "a");
  if (f) { fprintf(f, "%s\t%s\n", title, url); fclose(f); }
}

// ------------------------------------------------------------------- pagina --
static void free_page(void) {
  for (int i = 0; i < nblk; i++) {
    free(blk[i].x); free(blk[i].u); free(blk[i].s); free(blk[i].d);
    for (int k = 0; k < blk[i].nlines; k++) free(blk[i].lines[k]);
    for (int k = 0; k < blk[i].nslines; k++) free(blk[i].slines[k]);
    free(blk[i].lines); free(blk[i].slines);
    if (blk[i].tex) SDL_DestroyTexture(blk[i].tex);
  }
  free(blk); blk = NULL; nblk = 0; focus = -1; content_h = 0;
}

static TTF_Font *blk_font(const Blk *b) {
  switch (b->t) {
    case 'h': return b->level == 1 ? font(W_LIGHT, 52) : b->level == 2 ? font(W_MED, 38) : font(W_MED, 32);
    case 'a': return font(W_MED, b->big ? 32 : 28);
    case 'q': return font(W_LIGHT, 30);
    case 'c': return font(W_REG, 22);
    case 'm': return font(W_REG, 24);
    case 'r': return font(W_MED, 32);
    default:  return font(W_REG, 30);
  }
}
static int blk_lineh(const Blk *b) {
  switch (b->t) { case 'h': return b->level == 1 ? 62 : b->level == 2 ? 48 : 42; case 'c': return 30; case 'm': return 32; case 'r': return 40; case 'a': return b->big ? 42 : 38; default: return 44; }
}

// a capo per parole: array di righe allocate
static char **wrap(TTF_Font *f, const char *s, int maxw, int *n) {
  int cap = 8; char **out = malloc(sizeof(char *) * (size_t)cap); *n = 0;
  if (!s) return out;
  const char *p = s; char line[2048];
  while (*p) {
    while (*p == ' ') p++;
    if (!*p) break;
    size_t best = 0; const char *q = p;
    for (;;) {
      const char *sp = q; while (*sp && *sp != ' ' && *sp != '\n') sp++;
      size_t len = (size_t)(sp - p); if (len >= sizeof line) len = sizeof line - 1;
      memcpy(line, p, len); line[len] = 0;
      if (best && text_w(f, line) > maxw) break;
      best = len;
      if (!*sp || *sp == '\n' || len >= sizeof line - 1) break;
      q = sp + 1;
    }
    // una parola più larga della riga si spezza a forza
    if (best && text_w(f, (memcpy(line, p, best), line[best] = 0, line)) > maxw) {
      size_t k = best; while (k > 1) { k--; while (k > 1 && ((unsigned char)p[k] & 0xC0) == 0x80) k--; memcpy(line, p, k); line[k] = 0; if (text_w(f, line) <= maxw) break; }
      best = k;
    }
    if (*n == cap) { cap *= 2; out = realloc(out, sizeof(char *) * (size_t)cap); }
    out[(*n)++] = strndup(p, best);
    p += best; if (*p == '\n') p++;
    if (*n > 400) break;
  }
  return out;
}

static void layout(void) {
  int y = 0;
  for (int i = 0; i < nblk; i++) {
    Blk *b = &blk[i];
    int top = y;
    if (b->t == 'h') top += b->level == 1 ? 10 : 34;
    if (b->t == 'i') {
      b->h = b->tex ? b->th : 380;
    } else if (b->t == 'r') {
      b->h = 26 + b->nlines * blk_lineh(b) + 36 + b->nslines * 34 + 26;
    } else if (b->t == 'a') {
      b->h = 18 + b->nlines * blk_lineh(b) + 18;
    } else {
      b->h = b->nlines * blk_lineh(b);
    }
    b->y = top;
    int gap = b->t == 'p' ? 22 : b->t == 'a' ? 8 : b->t == 'r' ? 18 : b->t == 'i' ? 24 : 16;
    y = top + b->h + gap;
  }
  content_h = y + 200;
}

static int focusable(int i) { return i >= 0 && i < nblk && (blk[i].t == 'a' || blk[i].t == 'r'); }

static void on_page(int st, JVal *j, const char *raw, void *ud) {
  (void)raw;
  if ((int)(intptr_t)ud != load_gen) return;          // superata da un'altra richiesta
  loading = 0;
  free_page();
  err_msg[0] = 0;
  if (st != 200 || !j) {
    const char *e = jstr(j, "error", "");
    snprintf(err_msg, sizeof err_msg, "%s",
      !strcmp(e, "dns_failed") ? _("Indirizzo non trovato. Controlla di averlo scritto bene.") :
      !strcmp(e, "address_not_allowed") ? _("Questo indirizzo non è raggiungibile dal browser Omega.") :
      !strcmp(e, "timeout") ? _("Il sito non risponde (tempo scaduto).") :
      !strcmp(e, "too_many_requests") ? _("Troppe pagine in poco tempo: attendi qualche secondo.") :
      !strcmp(e, "invalid_url") ? _("Indirizzo non valido.") :
      st < 0 ? _("Server Omega non raggiungibile.") : _("Impossibile aprire la pagina."));
    snprintf(pg_title, sizeof pg_title, "%s", _("Errore"));
    return;
  }
  jcpy(pg_title, sizeof pg_title, j, "title");
  jcpy(pg_site, sizeof pg_site, j, "site");
  jcpy(pg_mode, sizeof pg_mode, j, "mode");
  char fin[600]; jcpy(fin, sizeof fin, j, "url");
  if (fin[0]) { snprintf(pg_url, sizeof pg_url, "%s", fin); if (hidx >= 0) snprintf(hist[hidx].url, sizeof hist[hidx].url, "%s", fin); }
  JVal *bl = jget(j, "blocks");
  int n = jlen(bl);
  blk = calloc((size_t)(n ? n : 1), sizeof(Blk));
  JFOR(b, bl) {
    Blk *k = &blk[nblk];
    const char *t = jstr(b, "t", "p");
    k->t = !strcmp(t, "img") ? 'i' : !strcmp(t, "res") ? 'r' : !strcmp(t, "code") ? 'c' : !strcmp(t, "meta") ? 'm' : !strcmp(t, "li") ? 'l' : t[0];
    k->level = (int)jnum(b, "l", 2); k->big = jbool(b, "big");
    const char *x = jstr(b, "x", NULL), *u = jstr(b, "u", NULL), *s = jstr(b, "s", NULL), *d = jstr(b, "d", NULL);
    if (x) k->x = strdup(x);
    if (u) k->u = strdup(u);
    if (s) k->s = strdup(s);
    if (d) k->d = strdup(d);
    jcpy(k->img, sizeof k->img, b, "i");
    if (k->t != 'i') {
      int w = BR_W - (k->t == 'l' ? 40 : k->t == 'q' ? 40 : k->t == 'a' ? 90 : k->t == 'r' ? 60 : 0);
      char tmp[2100];
      if (k->t == 'l') snprintf(tmp, sizeof tmp, "%s", k->x ? k->x : "");
      k->lines = wrap(blk_font(k), k->t == 'l' ? tmp : k->x, w, &k->nlines);
      if (k->t == 'r' && k->s) k->slines = wrap(font(W_REG, 25), k->s, w, &k->nslines);
      if (k->nslines > 3) k->nslines = 3;
      if (k->t == 'a' && k->nlines > 3) k->nlines = 3;
    }
    nblk++;
  }
  layout();
  // posizione salvata (indietro/avanti) o inizio pagina
  scroll = scroll_t = (hidx >= 0) ? hist[hidx].scroll : 0;
  focus = (hidx >= 0) ? hist[hidx].focus : -1;
  if (focus < 0) for (int i = 0; i < nblk; i++) if (focusable(i) && blk[i].y < SCREEN_H - BR_TOP - 200) { focus = i; break; }
  history_append(pg_title, pg_url);
}

static void fetch_current(void) {
  if (hidx < 0) return;
  const char *u = hist[hidx].url;
  char path[1600], enc[1400];
  int is_search = !strncmp(u, SEARCH_PREFIX, sizeof SEARCH_PREFIX - 1);
  if (is_search) snprintf(enc, sizeof enc, "%s", u + sizeof SEARCH_PREFIX - 1);   // già codificata
  else url_encode(enc, sizeof enc, u, "-_.~");
  snprintf(path, sizeof path, is_search ? OMEGA_API "/browse?q=%s" : full_page ? OMEGA_API "/browse?full=1&url=%s" : OMEGA_API "/browse?url=%s", enc);
  snprintf(pg_url, sizeof pg_url, "%s", u);
  loading = 1; load_gen++; err_msg[0] = 0;
  net_req(HTTP_GET, path, NULL, on_page, (void *)(intptr_t)load_gen);
}

static void navigate(const char *url) {
  if (hidx >= 0) { hist[hidx].scroll = scroll_t; hist[hidx].focus = focus; }
  // tronca l'avanti
  nhist = hidx + 1;
  if (nhist >= MAX_HIST) { memmove(hist, hist + 1, sizeof(HistEntry) * (MAX_HIST - 1)); nhist = MAX_HIST - 1; }
  snprintf(hist[nhist].url, sizeof hist[nhist].url, "%s", url);
  hist[nhist].scroll = 0; hist[nhist].focus = -1;
  hidx = nhist++;
  free_page(); snprintf(pg_title, sizeof pg_title, "%s", _("Caricamento...")); pg_site[0] = 0;
  fetch_current();
}

// testo scritto nella barra: indirizzo, oppure ricerca
static void go_input(const char *in) {
  while (*in == ' ') in++;
  if (!*in) return;
  int has_space = strchr(in, ' ') != NULL, has_dot = strchr(in, '.') != NULL;
  if (!strncmp(in, "http://", 7) || !strncmp(in, "https://", 8)) navigate(in);
  else if (!has_space && has_dot) { char u[600]; snprintf(u, sizeof u, "https://%s", in); navigate(u); }
  else {
    char q[600]; url_encode(q, sizeof q, in, "");
    char u[640]; snprintf(u, sizeof u, SEARCH_PREFIX "%s", q); navigate(u);
  }
}

static void go_home(void) {
  if (hidx >= 0) { hist[hidx].scroll = scroll_t; hist[hidx].focus = focus; }
  free_page(); hidx = -1; nhist = 0; pg_url[0] = 0; pg_title[0] = 0; err_msg[0] = 0; loading = 0; load_gen++;
  focus = 0; scroll = scroll_t = 0;
}

void browser_open(const char *url) {
  bm_load();
  if (url && *url) navigate(url);
  else if (hidx < 0) go_home();
  if (ov_top() != OV_BROWSER) ov_push(OV_BROWSER);
}

// ----------------------------------------------------------------- immagini --
static void on_img(const char *key, SDL_Texture *t, SDL_Color avg, void *ud) {
  (void)avg;
  int gen = (int)(intptr_t)ud;
  if (gen != load_gen) { if (t) SDL_DestroyTexture(t); return; }
  for (int i = 0; i < nblk; i++) if (blk[i].t == 'i' && blk[i].tex_state == 1 && !strcmp(blk[i].img, key + 2)) {
    blk[i].tex_state = t ? 2 : 3;
    blk[i].tex = t;
    if (t) { int w, h; SDL_QueryTexture(t, NULL, NULL, &w, &h); blk[i].tw = w; blk[i].th = h; }
    else blk[i].th = 0;
    // reimpagina; se l'immagine è sopra la vista si compensa lo scorrimento
    int old = blk[i].h;
    layout();
    if (blk[i].y + old < scroll_t) { scroll_t += (float)(blk[i].h - old); scroll += (float)(blk[i].h - old); }
    return;
  }
  if (t) SDL_DestroyTexture(t);
}

// ------------------------------------------------------------------ disegno --
static int home_sel;    // 0 barra di ricerca, 1 browser della PS5, 2..: siti rapidi, poi segnalibri
#define HOME_FIRST 2
static int home_items(void) { return HOME_FIRST + NQUICK + nbm; }

static void home_card(int x, int y, int w, int h, int f, Col c, int ic, const char *ini, const char *name, const char *sub, int a) {
  if (f) shadow_rrect(x, y, w, h, 22, 18, a / 2);
  fill_rrect(x, y, w, h, 22, f ? RGB(56, 64, 90) : RGB(28, 33, 48), a);
  if (f) stroke_rrect(x - 5, y - 5, w + 10, h + 10, 27, 3, C_WHITE, a);
  fill_circle(x + 46, y + 48, 28, c, a);
  if (ic >= 0) draw_icon(ic, x + 46, y + 48, 30, C_WHITE, a); else draw_text(font(W_BOLD, 26), ini, x + 46, y + 32, C_WHITE, a, AL_C);
  draw_text_fit(font(W_MED, 24), name, x + 24, y + 88, w - 48, C_TXT, a, AL_L);
  if (sub) draw_text_fit(font(W_REG, 18), sub, x + 24, y + 120, w - 48, C_FAINT, a, AL_L);
}

static void draw_home(int a) {
  int cx = SCREEN_W / 2;
  glow(cx, 230, 340, C_ACC, a * 14 / 100);
  draw_icon(IC_GLOBE, cx - 250, 222, 70, C_ACC2, a);
  draw_text(font(W_LIGHT, 56), _("Browser Omega"), cx - 200, 186, C_WHITE, a, AL_L);
  int fw = 1100, fx = cx - fw / 2, fy = 300, foc = home_sel == 0;
  fill_rrect(fx, fy, fw, 86, 43, foc ? RGB(52, 60, 84) : RGB(36, 42, 60), a);
  if (foc) stroke_rrect(fx - 4, fy - 4, fw + 8, 94, 47, 3, C_WHITE, a);
  draw_icon(IC_SEARCH, fx + 50, fy + 43, 34, C_TXT, a);
  draw_text(font(W_LIGHT, 30), _("Cerca sul web o scrivi un indirizzo"), fx + 90, fy + 24, C_DIM, a, AL_L);
  // il browser vero della PS5, per i siti con JavaScript, video e accessi
  int by = fy + 108, bf = home_sel == 1;
  fill_rrect(fx, by, fw, 70, 35, bf ? C_WHITE : RGB(255, 255, 255), bf ? a : a * 7 / 100);
  draw_icon(IC_GLOBE, fx + 46, by + 35, 28, bf ? RGB(12, 14, 22) : C_ACC2, a);
  draw_text_fit(font(W_MED, 25), _("Apri il browser completo della PS5 (siti moderni, video, accessi)"), fx + 84, by + 20, fw - 120, bf ? RGB(12, 14, 22) : C_TXT, a, AL_L);
  // siti rapidi e segnalibri, in una griglia
  int w = 250, h = 150, gap = 22, cols = 6, gx = cx - (cols * w + (cols - 1) * gap) / 2, gy = by + 120;
  draw_text(font(W_MED, 26), _("Siti rapidi"), gx, gy, C_DIM, a, AL_L);
  gy += 44;
  for (int i = 0; i < NQUICK; i++) {
    int x = gx + (i % cols) * (w + gap), y = gy + (i / cols) * (h + gap);
    const char *host = strstr(QUICK[i].url, "://"); host = host ? host + 3 : QUICK[i].url;
    home_card(x, y, w, h, home_sel == HOME_FIRST + i, avatar_col(i * 5 + 2), QUICK[i].ic, NULL, QUICK[i].name, host, a);
  }
  int qrows = (NQUICK + cols - 1) / cols;
  gy += qrows * (h + gap) + 14;
  if (nbm) {
    draw_text(font(W_MED, 26), _("Segnalibri"), gx, gy, C_DIM, a, AL_L);
    gy += 44;
    for (int i = 0; i < nbm; i++) {
      int x = gx + (i % cols) * (w + gap), y = gy + (i / cols) * (h + gap);
      if (y > SCREEN_H - 150) break;
      char ini[4] = { bm_title[i][0], 0 };
      home_card(x, y, w, h, home_sel == HOME_FIRST + NQUICK + i, avatar_col(i * 3 + 1), -1, ini, bm_title[i], NULL, a);
    }
  } else draw_text(font(W_REG, 22), _("Nessun segnalibro: aprendo una pagina, □ › Aggiungi ai segnalibri."), gx, gy, C_FAINT, a, AL_L);
}

static void draw_block(Blk *b, int y, int a, int focused) {
  int x = BR_X;
  TTF_Font *f = blk_font(b); int lh = blk_lineh(b);
  switch (b->t) {
    case 'h': case 'p': case 'm':
      for (int k = 0; k < b->nlines; k++) draw_text(f, b->lines[k], x, y + k * lh, b->t == 'm' ? C_DIM : C_TXT, a, AL_L);
      break;
    case 'l':
      fill_circle(x + 12, y + 22, 5, C_DIM, a);
      for (int k = 0; k < b->nlines; k++) draw_text(f, b->lines[k], x + 40, y + k * lh, C_TXT, a, AL_L);
      break;
    case 'q':
      fill_rect(x, y, 5, b->h, C_ACC2, a);
      for (int k = 0; k < b->nlines; k++) draw_text(f, b->lines[k], x + 40, y + k * lh, C_DIM, a, AL_L);
      break;
    case 'c':
      fill_rrect(x - 16, y - 10, BR_W + 32, b->h + 20, 12, RGB(10, 12, 18), a);
      for (int k = 0; k < b->nlines; k++) draw_text(f, b->lines[k], x, y + k * lh, RGB(170, 220, 170), a, AL_L);
      break;
    case 'a':
      if (focused) { fill_rrect(x - 20, y, BR_W + 40, b->h, 16, C_WHITE, a * 12 / 100); stroke_rrect(x - 23, y - 3, BR_W + 46, b->h + 6, 19, 3, C_WHITE, a); }
      draw_icon(IC_ARROW_R, x + 20, y + 18 + lh / 2, 24, C_ACC2, a);
      for (int k = 0; k < b->nlines; k++) draw_text(f, b->lines[k], x + 60, y + 18 + k * lh, C_ACC2, a, AL_L);
      break;
    case 'r': {
      fill_rrect(x - 20, y, BR_W + 40, b->h, 20, focused ? RGB(44, 52, 74) : C_PANEL, a);
      if (focused) stroke_rrect(x - 24, y - 4, BR_W + 48, b->h + 8, 24, 3, C_WHITE, a);
      int yy = y + 26;
      for (int k = 0; k < b->nlines; k++) { draw_text(f, b->lines[k], x + 10, yy, C_ACC2, a, AL_L); yy += lh; }
      if (b->d) draw_text_fit(font(W_REG, 22), b->d, x + 10, yy + 4, BR_W - 40, C_OK, a, AL_L);
      yy += 36;
      for (int k = 0; k < b->nslines; k++) { draw_text(font(W_REG, 25), b->slines[k], x + 10, yy, C_DIM, a, AL_L); yy += 34; }
      break;
    }
    case 'i':
      if (b->tex_state == 0 && b->img[0]) {
        b->tex_state = 1;
        char key[48], path[96];
        snprintf(key, sizeof key, "b:%s", b->img);
        snprintf(path, sizeof path, OMEGA_API "/browse/img/%s", b->img);
        load_req(LOAD_URL, key, path, BR_W, 0, 14, RGB(0, 0, 0), on_img, (void *)(intptr_t)load_gen);
      }
      if (b->tex) draw_tex(b->tex, x + (BR_W - b->tw) / 2, y, b->tw, b->th, a);
      else if (b->tex_state == 1) { fill_rrect(x, y, BR_W, b->h, 14, C_PANEL, a); draw_spinner(x + BR_W / 2, y + b->h / 2, 16, a); }
      break;
  }
}

void browser_draw(float t) {
  int a = (int)(255 * t);
  float e = ease_out(t);
  // schermo intero opaco: è il fondo più economico da disegnare
  int oy = (int)((1 - e) * 60);
  fill_rect(0, 0, SCREEN_W, SCREEN_H, RGB(14, 16, 24), t > 0.98f ? 255 : a);
  scroll = approach(scroll, scroll_t, 14.0f);

  SDL_Rect clip = { 0, BR_TOP, SCREEN_W, SCREEN_H - BR_TOP };
  SDL_RenderSetClipRect(R, &clip);
  if (hidx < 0) draw_home(a);
  else if (err_msg[0]) {
    draw_icon(IC_GLOBE, SCREEN_W / 2, 380, 100, C_FAINT, a);
    draw_text(font(W_MED, 36), _("Pagina non disponibile"), SCREEN_W / 2, 470, C_TXT, a, AL_C);
    draw_text(font(W_REG, 28), err_msg, SCREEN_W / 2, 530, C_DIM, a, AL_C);
    draw_text(font(W_REG, 24), _("Torna indietro oppure scrivi un altro indirizzo"), SCREEN_W / 2, 600, C_FAINT, a, AL_C);
  } else {
    int base = BR_TOP + 40 - (int)scroll + oy;
    for (int i = 0; i < nblk; i++) {
      int y = base + blk[i].y;
      if (y + blk[i].h < BR_TOP - 40) continue;
      if (y > SCREEN_H + 40) break;
      draw_block(&blk[i], y, a, i == focus);
    }
    if (content_h > SCREEN_H - BR_TOP) {
      int vh = SCREEN_H - BR_TOP - 40, bh = vh * vh / content_h; if (bh < 60) bh = 60;
      int by = BR_TOP + 20 + (int)((vh - bh) * clampf(scroll / (float)(content_h - vh), 0, 1));
      fill_rrect(SCREEN_W - 40, by, 8, bh, 4, C_WHITE, a * 35 / 100);
    }
  }
  if (loading) draw_spinner(SCREEN_W / 2, 520, 22, a);
  SDL_RenderSetClipRect(R, NULL);

  // barra dell'indirizzo
  fill_rect(0, 0, SCREEN_W, BR_TOP - 10, RGB(22, 25, 36), t > 0.98f ? 255 : a);
  fill_rect(0, BR_TOP - 10, SCREEN_W, 2, RGB(60, 70, 100), a);
  int canb = hidx > 0, canf = hidx >= 0 && hidx < nhist - 1;
  draw_icon(IC_BACK, 70, 70, 40, canb ? C_TXT : C_FAINT, a);
  draw_icon(IC_FWD, 140, 70, 40, canf ? C_TXT : C_FAINT, a);
  draw_icon(IC_RELOAD, 210, 70, 38, hidx >= 0 ? C_TXT : C_FAINT, a);
  int ux = 270, uw = SCREEN_W - 270 - 220;
  fill_rrect(ux, 34, uw, 72, 36, RGB(36, 42, 60), a);
  draw_icon(hidx < 0 ? IC_SEARCH : IC_GLOBE, ux + 40, 70, 30, C_DIM, a);
  const char *shown = hidx < 0 ? _("Cerca o scrivi un indirizzo") : (!strncmp(pg_url, SEARCH_PREFIX, sizeof SEARCH_PREFIX - 1) ? pg_title : pg_url);
  draw_text_fit(font(W_REG, 27), shown, ux + 76, 52, uw - 110, hidx < 0 ? C_DIM : C_TXT, a, AL_L);
  int starred = 0; for (int i = 0; i < nbm; i++) if (hidx >= 0 && !strcmp(bm_url[i], pg_url)) starred = 1;
  draw_icon(IC_STAR, SCREEN_W - 180, 70, 36, starred ? C_WARN : C_FAINT, a);
  draw_icon(IC_CLOSE, SCREEN_W - 90, 70, 36, C_TXT, a);
  if (loading) {
    load_anim += g_dt;
    int bw = 360, bx = (int)(fmodf(load_anim * 900, SCREEN_W + bw)) - bw;
    fill_rect(bx < 0 ? 0 : bx, BR_TOP - 12, bw + (bx < 0 ? bx : 0), 4, C_ACC2, a);
  }
  if (hidx >= 0 && pg_site[0] && !loading) draw_text(font(W_REG, 20), pg_site, ux + uw - 30, 60, C_FAINT, a, AL_R);

  grad_v(0, SCREEN_H - 140, SCREEN_W, 60, RGB(14, 16, 24), 0, RGB(14, 16, 24), a);
  fill_rect(0, SCREEN_H - 80, SCREEN_W, 80, RGB(14, 16, 24), a);
  const int ic[] = { IC_BTN_X, IC_BTN_O, IC_BTN_TRI, IC_BTN_SQ, IC_BTN_OPT };
  const char *lb[] = { _("Apri"), _("Indietro"), _("Indirizzo"), _("Menu"), _("Browser PS5") };
  hints(ic, lb, hidx >= 0 ? 5 : 4, a);
  if (hidx >= 0 && !loading) draw_text(font(W_MED, 18), full_page ? _("PAGINA COMPLETA") : !strcmp(pg_mode, "reader") ? _("MODALITÀ LETTURA") : "", BR_X, 112, C_ACC2, a, AL_L);
}

// -------------------------------------------------------------------- input --
static void ensure_visible(int i) {
  int vh = SCREEN_H - BR_TOP - 160;
  if (blk[i].y < scroll_t + 40) scroll_t = (float)(blk[i].y - 120);
  else if (blk[i].y + blk[i].h > scroll_t + vh) scroll_t = (float)(blk[i].y + blk[i].h - vh);
  if (scroll_t < 0) scroll_t = 0;
}

static void scroll_by(float d) {
  scroll_t += d;
  float maxs = (float)(content_h - (SCREEN_H - BR_TOP)); if (maxs < 0) maxs = 0;
  if (scroll_t > maxs) scroll_t = maxs;
  if (scroll_t < 0) scroll_t = 0;
  // il fuoco segue la vista: va al primo link visibile
  int vh = SCREEN_H - BR_TOP - 160;
  if (focus < 0 || blk[focus].y < scroll_t || blk[focus].y > scroll_t + vh) {
    focus = -1;
    for (int i = 0; i < nblk; i++) if (focusable(i) && blk[i].y >= scroll_t && blk[i].y < scroll_t + vh) { focus = i; break; }
  }
}

static void move_focus(int dir) {
  int vh = SCREEN_H - BR_TOP - 160;
  float view_top = scroll_t, view_bot = scroll_t + vh;
  int i = focus;
  for (;;) {
    i += dir;
    if (i < 0 || i >= nblk) { scroll_by(dir * 300.0f); return; }
    if (!focusable(i)) continue;
    // link troppo lontano: si scorre prima il testo in mezzo
    if (dir > 0 && blk[i].y > view_bot + 260) { scroll_by(300); return; }
    if (dir < 0 && blk[i].y + blk[i].h < view_top - 260) { scroll_by(-300); return; }
    focus = i; ensure_visible(i); return;
  }
}

static void bm_toggle(void) {
  if (hidx < 0 || !pg_url[0]) return;
  for (int i = 0; i < nbm; i++) if (!strcmp(bm_url[i], pg_url)) {
    memmove(bm_title + i, bm_title + i + 1, sizeof bm_title[0] * (size_t)(nbm - i - 1));
    memmove(bm_url + i, bm_url + i + 1, sizeof bm_url[0] * (size_t)(nbm - i - 1));
    nbm--; bm_save(); set_msg(_("Segnalibro rimosso"), 0); return;
  }
  if (nbm >= MAX_BM) { set_msg(_("Troppi segnalibri"), 1); return; }
  snprintf(bm_title[nbm], sizeof bm_title[0], "%s", pg_title[0] ? pg_title : pg_url);
  snprintf(bm_url[nbm], sizeof bm_url[0], "%s", pg_url);
  nbm++; bm_save(); set_msg(_("Aggiunto ai segnalibri"), 0);
}

static char hist_urls[16][600]; static int nhist_items;
static void history_pick(int idx, void *ud) { (void)ud; if (idx >= 0 && idx < nhist_items) navigate(hist_urls[idx]); }
static void open_history(void) {
  static char titles[16][120]; static const char *items[16];
  char lines[64][720]; int n = 0;
  FILE *f = fopen(HIST_FILE, "r");
  if (f) { char l[800]; while (fgets(l, sizeof l, f)) { l[strcspn(l, "\r\n")] = 0; snprintf(lines[n % 64], sizeof lines[0], "%s", l); n++; } fclose(f); }
  nhist_items = 0;
  for (int k = n - 1; k >= 0 && k >= n - 64 && nhist_items < 16; k--) {
    char *tab = strchr(lines[k % 64], '\t'); if (!tab) continue;
    *tab = 0;
    int dup = 0; for (int q = 0; q < nhist_items; q++) if (!strcmp(hist_urls[q], tab + 1)) dup = 1;
    if (dup) continue;
    snprintf(titles[nhist_items], sizeof titles[0], "%s", lines[k % 64]);
    snprintf(hist_urls[nhist_items], sizeof hist_urls[0], "%s", tab + 1);
    items[nhist_items] = titles[nhist_items]; nhist_items++;
  }
  if (!nhist_items) { set_msg(_("Cronologia vuota"), 0); return; }
  menu_open(_("Cronologia"), items, nhist_items, history_pick, NULL);
}
static void bm_pick(int idx, void *ud) { (void)ud; if (idx >= 0 && idx < nbm) navigate(bm_url[idx]); }
static void open_bookmarks(void) {
  static const char *items[MAX_BM];
  for (int i = 0; i < nbm; i++) items[i] = bm_title[i];
  if (!nbm) { set_msg(_("Nessun segnalibro"), 0); return; }
  menu_open(_("Segnalibri"), items, nbm > 16 ? 16 : nbm, bm_pick, NULL);
}
static void clear_history(void) { FILE *f = fopen(HIST_FILE, "w"); if (f) fclose(f); set_msg(_("Cronologia cancellata"), 0); }

static void menu_pick(int idx, void *ud) {
  (void)ud;
  switch (idx) {
    case 0: bm_toggle(); break;
    case 1: open_bookmarks(); break;
    case 2: open_history(); break;
    case 3: go_home(); break;
    case 4: if (hidx >= 0) { free_page(); fetch_current(); } break;
    case 5: clear_history(); break;
    case 6: system_browser(hidx >= 0 ? pg_url : NULL); break;
    case 7: full_page = !full_page; set_msg(full_page ? _("Pagina completa: come la impagina il sito") : _("Modalità lettura: solo il testo e le immagini dell'articolo"), 0); if (hidx >= 0) { free_page(); fetch_current(); } break;
    case 8: ov_pop(); break;
  }
}

static void address_bar(void) {
  char buf[600]; snprintf(buf, sizeof buf, "%s", (hidx >= 0 && strncmp(pg_url, "omega://", 8)) ? pg_url : "");
  if (edit_text(_("Cerca o scrivi un indirizzo"), buf, sizeof buf, 0)) go_input(buf);
}

void browser_input(int b) {
  if (b == B_TRI) { address_bar(); return; }
  if (b == B_SQ) {
    int starred = 0; for (int i = 0; i < nbm; i++) if (hidx >= 0 && !strcmp(bm_url[i], pg_url)) starred = 1;
    const char *mode = full_page ? _("Passa alla modalità lettura") : _("Mostra la pagina completa");
    const char *it1[] = { _("Aggiungi ai segnalibri"), _("Segnalibri"), _("Cronologia"), _("Pagina iniziale"), _("Ricarica"), _("Cancella cronologia"), _("Apri nel browser della PS5"), mode, _("Chiudi browser") };
    const char *it2[] = { _("Rimuovi dai segnalibri"), _("Segnalibri"), _("Cronologia"), _("Pagina iniziale"), _("Ricarica"), _("Cancella cronologia"), _("Apri nel browser della PS5"), mode, _("Chiudi browser") };
    menu_open(_("Browser"), starred ? it2 : it1, 9, menu_pick, NULL);
    return;
  }
  if (b == B_L1 || (b == B_O && hidx >= 0)) {
    if (hidx > 0) { hist[hidx].scroll = scroll_t; hist[hidx].focus = focus; hidx--; free_page(); fetch_current(); }
    else if (hidx == 0) go_home();
    else if (b == B_O) ov_pop();
    return;
  }
  if (b == B_O) { ov_pop(); return; }
  if (b == B_R1) { if (hidx >= 0 && hidx < nhist - 1) { hist[hidx].scroll = scroll_t; hist[hidx].focus = focus; hidx++; free_page(); fetch_current(); } return; }

  if (hidx < 0) {           // pagina iniziale: ricerca, browser della PS5, griglia di 6 colonne
    int n = home_items(), g = home_sel - HOME_FIRST;
    if (b == B_X) {
      if (home_sel == 0) address_bar();
      else if (home_sel == 1) system_browser(NULL);
      else if (g < NQUICK) navigate(QUICK[g].url);
      else if (g - NQUICK < nbm) navigate(bm_url[g - NQUICK]);
    }
    else if (b == B_DOWN) { if (home_sel < HOME_FIRST) home_sel++; else if (home_sel + 6 < n) home_sel += 6; else if (g < NQUICK && nbm) home_sel = HOME_FIRST + NQUICK; }
    else if (b == B_UP) { if (home_sel <= 1) home_sel = 0; else if (g >= 6 && !(g >= NQUICK && g - NQUICK < 6)) home_sel -= 6; else if (g >= NQUICK) home_sel = HOME_FIRST; else home_sel = 1; }
    else if (b == B_RIGHT && home_sel >= HOME_FIRST && home_sel < n - 1) home_sel++;
    else if (b == B_LEFT && home_sel > HOME_FIRST) home_sel--;
    if (home_sel >= n) home_sel = n - 1;
    return;
  }
  if (b == B_OPT) { system_browser(pg_url); return; }
  if (loading || err_msg[0]) return;
  if (b == B_DOWN) move_focus(1);
  else if (b == B_UP) move_focus(-1);
  else if (b == B_RIGHT) scroll_by(SCREEN_H * 0.7f);
  else if (b == B_LEFT) scroll_by(-SCREEN_H * 0.7f);
  else if (b == B_X && focusable(focus) && blk[focus].u) navigate(blk[focus].u);
}
