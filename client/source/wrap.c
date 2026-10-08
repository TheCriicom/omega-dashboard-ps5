// Omega UI — Il tuo riepilogo, come il Wrap-Up di PlayStation: una settimana,
// un mese o un anno di gioco raccontati a schede, una dopo l'altra, come le
// storie. Tempo giocato, il gioco preferito, i primi cinque, il ritmo (ore e
// giorni), i record, i trofei, gli amici con cui hai giocato, il confronto con
// la community e, alla fine, il tuo profilo da condividere sulla bacheca.
// I numeri li calcola il server (GET /api/v1/wrap, endpoints/wrap.js) nel fuso
// della console. Una volta a settimana, se la settimana prima hai giocato
// almeno mezz'ora, una notifica avvisa che il riepilogo è pronto.
#include "app.h"
#include <math.h>
#include <stdlib.h>
#include <time.h>

#define SLIDE_SECS 7.0f

enum { S_INTRO, S_TOTAL, S_TOP, S_TOP5, S_RHYTHM, S_RECORDS, S_TROPHIES, S_TOGETHER, S_COMMUNITY, S_PERSONA, S_EMPTY };

typedef struct { char gid[24], name[100], icon[20]; long secs; int sessions; } WGame;
typedef struct { char oid[32]; int avatar; long secs; char game[100]; } WFriend;

static const struct { const char *period; int back; const char *label; const char *title; } OPTS[] = {
  { "week", 0, N_("Questa settimana"), N_("La tua settimana") },
  { "week", 1, N_("Settimana scorsa"), N_("La tua settimana scorsa") },
  { "month", 0, N_("Questo mese"), N_("Il tuo mese") },
  { "month", 1, N_("Mese scorso"), N_("Il tuo mese scorso") },
  { "year", 0, N_("Quest'anno"), N_("Il tuo anno") },
};
#define NOPTS (int)(sizeof OPTS / sizeof *OPTS)

static const struct { const char *key; int icon; Col c; const char *title, *desc; } PERSONAS[] = {
  { "night_owl", IC_STAR, { 120, 110, 255, 255 }, N_("Nottambulo"), N_("Le tue partite migliori arrivano quando gli altri dormono.") },
  { "early_bird", IC_IDEA, { 255, 190, 70, 255 }, N_("Mattiniero"), N_("Giochi presto, prima di tutto il resto.") },
  { "marathoner", IC_FIRE, { 255, 110, 70, 255 }, N_("Maratoneta"), N_("Quando inizi una partita, non ti fermi pi\xC3\xB9.") },
  { "hunter", IC_TROPHY, { 255, 200, 60, 255 }, N_("Cacciatore di trofei"), N_("Ogni trofeo prima o poi \xC3\xA8 tuo.") },
  { "explorer", IC_GLOBE, { 70, 200, 170, 255 }, N_("Esploratore"), N_("Tanti giochi diversi: ti piace scoprire cose nuove.") },
  { "loyal", IC_HEART, { 255, 90, 130, 255 }, N_("Fedelissimo"), N_("Un gioco sopra tutti: lo conosci a memoria.") },
  { "weekend", IC_PARTY, { 200, 100, 255, 255 }, N_("Guerriero del weekend"), N_("Il fine settimana \xC3\xA8 fatto per giocare.") },
  { "daily", IC_CLOCK, { 90, 170, 255, 255 }, N_("Costante"), N_("Un po' ogni giorno, senza saltarne uno.") },
  { "social", IC_FRIENDS, { 255, 120, 200, 255 }, N_("Giocatore di squadra"), N_("Con gli amici giochi meglio.") },
  { "casual", IC_GAMEPAD, { 110, 200, 255, 255 }, N_("Rilassato"), N_("Giochi quando ti va, senza fretta.") },
};
#define NPERSONAS (int)(sizeof PERSONAS / sizeof *PERSONAS)

static struct {
  int opt, loading, status, loaded;
  long total, prev; int games_count, sessions, new_games, days, streak, percentile, players, complete;
  WGame games[5]; int ngames;
  int has_rhythm; long hours[24], wd[7];
  int has_longest; WGame longest;
  int tr_count, tr[4]; char tr_name[130], tr_game[110], tr_icon[20]; int tr_grade, tr_pct, has_rarest;
  WFriend friends[3]; int nfriends;
  int persona;
  time_t from, to;
  int slides[12], nslides, cur;
  float st, enter;
} W;

static const Col GRADE_COL[4] = { { 176, 222, 255, 255 }, { 255, 200, 60, 255 }, { 200, 210, 225, 255 }, { 215, 140, 80, 255 } };

// ------------------------------------------------------------------ utilità --
int wrap_tz(void) {
  time_t t = time(NULL); struct tm g = *gmtime(&t), l = *localtime(&t);
  int d = (l.tm_hour - g.tm_hour) * 60 + (l.tm_min - g.tm_min);
  if (l.tm_yday != g.tm_yday) d += (l.tm_yday > g.tm_yday || (l.tm_yday == 0 && g.tm_yday > 300)) ? 1440 : -1440;
  return d;
}

static time_t parse_iso(const char *s) {
  struct tm tm; memset(&tm, 0, sizeof tm);
  if (sscanf(s, "%d-%d-%dT%d:%d:%d", &tm.tm_year, &tm.tm_mon, &tm.tm_mday, &tm.tm_hour, &tm.tm_min, &tm.tm_sec) < 3) return 0;
  tm.tm_year -= 1900; tm.tm_mon -= 1;
  return timegm(&tm);
}

// "3 h 20 min", "45 min"
static void dur(long secs, char *out, size_t n) {
  long h = secs / 3600, m = (secs % 3600) / 60;
  if (!h) snprintf(out, n, _("%ld min"), m);
  else if (!m || h >= 100) snprintf(out, n, _("%ld h"), h);
  else snprintf(out, n, _("%ld h %ld min"), h, m);
}

// "28 set – 4 ott"
static void range_text(char *out, size_t n) {
  static const char *const MO[12] = { N_("gen"), N_("feb"), N_("mar"), N_("apr"), N_("mag"), N_("giu"), N_("lug"), N_("ago"), N_("set"), N_("ott"), N_("nov"), N_("dic") };
  if (!W.from || !W.to) { out[0] = 0; return; }
  time_t a = W.from, b = W.to - 1;
  struct tm la = *localtime(&a), lb = *localtime(&b);
  time_t now = time(NULL); if (b > now) { b = now; lb = *localtime(&b); }
  if (!strcmp(OPTS[W.opt].period, "year")) snprintf(out, n, "%d", la.tm_year + 1900);
  else if (strchr("jkz", i18n_code()[0]))   // ja, ko, zh: 9/28 – 10/4
    snprintf(out, n, "%d/%d \xE2\x80\x93 %d/%d", la.tm_mon + 1, la.tm_mday, lb.tm_mon + 1, lb.tm_mday);
  else snprintf(out, n, "%d %s \xE2\x80\x93 %d %s", la.tm_mday, _(MO[la.tm_mon % 12]), lb.tm_mday, _(MO[lb.tm_mon % 12]));
}

static void wgame_icon(const char *gid, const char *media, int x, int y, int s, int a) {
  for (int k = 0; k < napps; k++) if (apps[k].tex && !strcmp(apps[k].tid, gid)) { draw_tex(apps[k].tex, x, y, s, s, a); return; }
  if (media[0] && draw_media_frames(media, 1, x, y, s, s, 128, 128, s / 6, 0, a)) return;
  fill_rrect(x, y, s, s, s / 6, RGB(40, 48, 70), a);
  draw_icon(IC_GAMEPAD, x + s / 2, y + s / 2, s * 55 / 100, C_DIM, a);
}

static void parse_game(JVal *o, WGame *g) {
  memset(g, 0, sizeof *g);
  jcpy(g->gid, sizeof g->gid, o, "game_id"); jcpy(g->name, sizeof g->name, o, "game_name"); jcpy(g->icon, sizeof g->icon, o, "game_icon");
  if (!g->name[0]) snprintf(g->name, sizeof g->name, "%s", g->gid);
  g->secs = (long)jnum(o, "seconds", 0); g->sessions = (int)jnum(o, "sessions", 0);
}

static int grade_index(const char *g) { return g[0] == 'P' ? 0 : g[0] == 'G' ? 1 : g[0] == 'S' ? 2 : 3; }

static void build_slides(void) {
  int n = 0;
  W.slides[n++] = S_INTRO;
  if (W.total <= 0) W.slides[n++] = S_EMPTY;
  else {
    W.slides[n++] = S_TOTAL;
    if (W.ngames) W.slides[n++] = S_TOP;
    if (W.ngames >= 2) W.slides[n++] = S_TOP5;
    if (W.has_rhythm) W.slides[n++] = S_RHYTHM;
    if (W.has_longest || W.streak >= 2 || W.new_games) W.slides[n++] = S_RECORDS;
    if (W.tr_count) W.slides[n++] = S_TROPHIES;
    if (W.nfriends) W.slides[n++] = S_TOGETHER;
    if (W.percentile >= 0) W.slides[n++] = S_COMMUNITY;
    W.slides[n++] = S_PERSONA;
  }
  W.nslides = n;
  if (W.cur >= n) W.cur = 0;
}

static void on_wrap(int st, JVal *j, const char *raw, void *ud) {
  (void)raw;
  if ((intptr_t)ud != W.opt) return;               // risposta di un periodo già lasciato
  W.loading = 0; W.status = st;
  if (st != 200) { set_msg(st == 429 ? _("Troppe richieste: riprova tra poco") : _("Riepilogo non disponibile"), 1); return; }
  W.total = (long)jnum(j, "total_seconds", 0); W.prev = (long)jnum(j, "prev_total_seconds", 0);
  W.games_count = (int)jnum(j, "games_count", 0); W.sessions = (int)jnum(j, "sessions", 0);
  W.new_games = (int)jnum(j, "new_games", 0); W.days = (int)jnum(j, "days_active", 0); W.streak = (int)jnum(j, "streak", 0);
  JVal *pc = jget(j, "percentile");
  W.percentile = pc && pc->t == J_NUM ? (int)pc->n : -1;
  W.players = (int)jnum(j, "players", 0); W.complete = jbool(j, "complete");
  W.from = parse_iso(jstr(j, "from", "")); W.to = parse_iso(jstr(j, "to", ""));
  W.ngames = 0;
  JFOR(o, jget(j, "games")) { if (W.ngames >= 5) break; parse_game(o, &W.games[W.ngames++]); }
  W.has_rhythm = 0;
  JVal *h = jget(j, "hours"), *d = jget(j, "weekdays");
  if (h && h->t == J_ARR && jlen(h) == 24 && d && d->t == J_ARR && jlen(d) == 7) {
    int i = 0; JFOR(o, h) W.hours[i++] = (long)o->n;
    i = 0; JFOR(o, d) W.wd[i++] = (long)o->n;
    W.has_rhythm = 1;
  }
  JVal *l = jget(j, "longest");
  W.has_longest = l && l->t == J_OBJ;
  if (W.has_longest) parse_game(l, &W.longest);
  JVal *t = jget(j, "trophies");
  W.tr_count = (int)jnum(t, "count", 0);
  W.tr[0] = (int)jnum(t, "p", 0); W.tr[1] = (int)jnum(t, "g", 0); W.tr[2] = (int)jnum(t, "s", 0); W.tr[3] = (int)jnum(t, "b", 0);
  JVal *r = jget(t, "rarest");
  W.has_rarest = r && r->t == J_OBJ;
  if (W.has_rarest) {
    jcpy(W.tr_name, sizeof W.tr_name, r, "name"); jcpy(W.tr_game, sizeof W.tr_game, r, "game"); jcpy(W.tr_icon, sizeof W.tr_icon, r, "icon_media");
    W.tr_grade = grade_index(jstr(r, "grade", "B"));
    JVal *p = jget(r, "pct"); W.tr_pct = p && p->t == J_NUM ? (int)p->n : -1;
  }
  W.nfriends = 0;
  JFOR(o, jget(j, "together")) {
    if (W.nfriends >= 3) break;
    WFriend *f = &W.friends[W.nfriends++]; memset(f, 0, sizeof *f);
    JVal *u = jget(o, "user");
    jcpy(f->oid, sizeof f->oid, u, "online_id"); f->avatar = (int)jnum(u, "avatar", 0); media_note_json(f->oid, u);
    f->secs = (long)jnum(o, "seconds", 0); jcpy(f->game, sizeof f->game, o, "game_name");
  }
  W.persona = NPERSONAS - 1;
  const char *p = jstr(j, "persona", "casual");
  for (int i = 0; i < NPERSONAS; i++) if (!strcmp(PERSONAS[i].key, p)) W.persona = i;
  W.loaded = 1; W.cur = 0; W.st = 0; W.enter = 0;
  build_slides();
}

static void load(void) {
  char path[160];
  snprintf(path, sizeof path, OMEGA_API "/wrap?period=%s&back=%d&tz=%d", OPTS[W.opt].period, OPTS[W.opt].back, wrap_tz());
  W.loading = 1; W.loaded = 0; W.nslides = 0;
  net_req(HTTP_GET, path, NULL, on_wrap, (void *)(intptr_t)W.opt);
}

// opt: 0 questa settimana, 1 settimana scorsa, 2 questo mese, 3 mese scorso, 4 quest'anno
void wrap_open(int opt) {
  memset(&W, 0, sizeof W);
  W.opt = opt >= 0 && opt < NOPTS ? opt : 0;
  W.percentile = -1;
  load();
  if (ov_top() != OV_WRAP) ov_push(OV_WRAP);
}

// ------------------------------------------------------------------ disegno --
static Col slide_col(int s) {
  switch (s) {
    case S_TOTAL: return RGB(20, 150, 160);
    case S_TOP: return RGB(60, 90, 220);
    case S_TOP5: return RGB(110, 70, 200);
    case S_RHYTHM: return RGB(40, 50, 140);
    case S_RECORDS: return RGB(220, 100, 50);
    case S_TROPHIES: return RGB(200, 150, 30);
    case S_TOGETHER: return RGB(210, 70, 140);
    case S_COMMUNITY: return RGB(40, 160, 90);
    case S_PERSONA: return PERSONAS[W.persona].c;
    default: return C_ACC;
  }
}

// entrata: 0 → 1 nel primo mezzo secondo della scheda, ritardata di `delay`
static float in(float delay) { float t = (W.st - delay) / 0.55f; return t <= 0 ? 0 : t >= 1 ? 1 : ease_out(t); }
static int rise(float delay) { return (int)((1 - in(delay)) * 46); }
static int fa(int a, float delay) { return (int)(a * in(delay)); }

static void big_center(const char *s, int y, int size, Col c, int a, float delay) {
  draw_text_fit(font(W_LIGHT, size), s, SCREEN_W / 2, y + rise(delay), SCREEN_W - 240, c, fa(a, delay), AL_C);
}
static void line_center(const char *s, int y, int size, Col c, int a, float delay) {
  draw_text_wrap_al(font(W_REG, size), s, SCREEN_W / 2, y + rise(delay), SCREEN_W - 520, 3, size + 14, c, fa(a, delay), AL_C);
}

static void draw_bg(int s, int a) {
  Col c = slide_col(s), dark = RGB(8, 10, 18);
  fill_rect(0, 0, SCREEN_W, SCREEN_H, dark, a);
  grad_v(0, 0, SCREEN_W, SCREEN_H, mix(c, dark, 0.35f), a, mix(c, dark, 0.85f), a);
  float t = (float)g_time;
  glow((int)(SCREEN_W * 0.2f + sinf(t * 0.31f) * 160), (int)(SCREEN_H * 0.3f + cosf(t * 0.23f) * 90), 520, mix(c, C_WHITE, 0.25f), a * 30 / 100);
  glow((int)(SCREEN_W * 0.8f + cosf(t * 0.27f) * 150), (int)(SCREEN_H * 0.75f + sinf(t * 0.19f) * 80), 600, mix(c, RGB(255, 80, 160), 0.3f), a * 22 / 100);
}

static void draw_progress(int a) {
  int x0 = 120, w = SCREEN_W - 240, gap = 10, n = W.nslides;
  if (n < 2) return;
  int sw = (w - gap * (n - 1)) / n;
  for (int i = 0; i < n; i++) {
    int x = x0 + i * (sw + gap);
    fill_rrect(x, 46, sw, 6, 3, C_WHITE, a * 25 / 100);
    float f = i < W.cur ? 1 : i > W.cur ? 0 : (W.cur == n - 1 ? 1 : fminf(1, W.st / SLIDE_SECS));
    if (f > 0) fill_rrect(x, 46, (int)(sw * f), 6, 3, C_WHITE, a * 90 / 100);
  }
}

static void s_intro(int a) {
  char r[80]; range_text(r, sizeof r);
  draw_logo(SCREEN_W / 2, 300 + rise(0), 120, fa(a, 0));
  big_center(_(OPTS[W.opt].title), 420, 92, C_WHITE, a, 0.15f);
  big_center(r, 560, 40, C_DIM, a, 0.3f);
  draw_avatar(S.me, S.my_avatar, SCREEN_W / 2, 720 + rise(0.45f), 110, fa(a, 0.45f));
  big_center(S.me, 800, 34, C_TXT, a, 0.5f);
  if (W.loaded && W.total > 0) line_center(_("Ecco com'\xC3\xA8 andata. Premi X per andare avanti."), 900, 28, C_DIM, a, 0.8f);
}

static void s_empty(int a) {
  draw_icon(IC_GAMEPAD, SCREEN_W / 2, 330 + rise(0), 140, C_WHITE, fa(a, 0) * 70 / 100);
  big_center(_("Nessuna partita in questo periodo"), 440, 62, C_WHITE, a, 0.1f);
  line_center(_("Gioca qualcosa e torna qui: il riepilogo si riempie da solo. Con Quadrato scegli un altro periodo."), 560, 30, C_DIM, a, 0.25f);
}

static void s_total(int a) {
  big_center(_("Hai giocato"), 250, 44, C_DIM, a, 0);
  char t[48]; dur((long)(W.total * in(0.1f) + 0.5f), t, sizeof t);
  draw_text_fit(font(W_LIGHT, 170), t, SCREEN_W / 2, 330 + rise(0.1f), SCREEN_W - 200, C_WHITE, fa(a, 0.1f), AL_C);
  char l[160] = "";
  if (W.prev > 0) {
    int pct = (int)lround((double)(W.total - W.prev) * 100.0 / (double)W.prev);
    snprintf(l, sizeof l, pct >= 0 ? _("+%d%% rispetto al periodo precedente") : _("%d%% rispetto al periodo precedente"), pct);
    int w = text_w(font(W_MED, 30), l) + 60;
    fill_rrect(SCREEN_W / 2 - w / 2, 580 + rise(0.5f), w, 60, 30, pct >= 0 ? C_OK : C_WARN, fa(a, 0.5f) * 30 / 100);
    draw_text(font(W_MED, 30), l, SCREEN_W / 2, 592 + rise(0.5f), C_WHITE, fa(a, 0.5f), AL_C);
  }
  char s1[64] = "", s2[64] = "", s3[64] = "";
  snprintf(s1, sizeof s1, W.games_count == 1 ? _("%d gioco") : _("%d giochi"), W.games_count);
  if (W.sessions > 0) snprintf(s2, sizeof s2, W.sessions == 1 ? _("%d sessione") : _("%d sessioni"), W.sessions);
  if (W.days > 0) snprintf(s3, sizeof s3, W.days == 1 ? _("%d giorno di gioco") : _("%d giorni di gioco"), W.days);
  const char *items[3] = { s1, s2, s3 }; int k = 0, x = SCREEN_W / 2;
  int tw = 0; for (int i = 0; i < 3; i++) if (items[i][0]) tw += text_w(font(W_MED, 34), items[i]) + 90;
  x -= tw / 2;
  for (int i = 0; i < 3; i++) {
    if (!items[i][0]) continue;
    int w = text_w(font(W_MED, 34), items[i]) + 60;
    float dl = 0.7f + 0.12f * k++;
    fill_rrect(x, 720 + rise(dl), w, 76, 38, C_WHITE, fa(a, dl) * 12 / 100);
    draw_text(font(W_MED, 34), items[i], x + w / 2, 738 + rise(dl), C_WHITE, fa(a, dl), AL_C);
    x += w + 30;
  }
}

static void s_top(int a) {
  WGame *g = &W.games[0];
  big_center(_("Il tuo gioco"), 150, 44, C_DIM, a, 0);
  float pop = in(0.1f); int s = (int)(330 * (0.8f + 0.2f * pop));
  glow(SCREEN_W / 2, 420, 300, C_WHITE, (int)(a * pop * 18 / 100));
  wgame_icon(g->gid, g->icon, SCREEN_W / 2 - s / 2, 420 - s / 2, s, fa(a, 0.1f));
  big_center(g->name, 620, 64, C_WHITE, a, 0.3f);
  char t[48], l[200]; dur(g->secs, t, sizeof t);
  int share = W.total > 0 ? (int)lround(g->secs * 100.0 / W.total) : 0;
  if (g->sessions > 0) snprintf(l, sizeof l, g->sessions == 1 ? _("%s in %d sessione \xC2\xB7 il %d%% del tuo tempo") : _("%s in %d sessioni \xC2\xB7 il %d%% del tuo tempo"), t, g->sessions, share);
  else snprintf(l, sizeof l, _("%s \xC2\xB7 il %d%% del tuo tempo"), t, share);
  big_center(l, 730, 36, C_TXT, a, 0.45f);
}

static void s_top5(int a) {
  big_center(_("I tuoi giochi pi\xC3\xB9 giocati"), 150, 56, C_WHITE, a, 0);
  int x = 360, w = SCREEN_W - 720, y = 280;
  long maxs = W.games[0].secs > 0 ? W.games[0].secs : 1;
  for (int i = 0; i < W.ngames; i++, y += 140) {
    WGame *g = &W.games[i]; float dl = 0.15f + 0.12f * i; int ra = fa(a, dl), dy = rise(dl);
    char n[8]; snprintf(n, sizeof n, "%d", i + 1);
    draw_text(font(W_BOLD, 54), n, x - 40, y + 24 + dy, i == 0 ? RGB(255, 210, 90) : C_DIM, ra, AL_R);
    wgame_icon(g->gid, g->icon, x, y + dy, 110, ra);
    draw_text_fit(font(W_MED, 36), g->name, x + 140, y + 4 + dy, w - 400, C_WHITE, ra, AL_L);
    char t[48]; dur(g->secs, t, sizeof t);
    draw_text(font(W_MED, 34), t, x + w, y + 6 + dy, C_TXT, ra, AL_R);
    int bw = w - 140;
    fill_rrect(x + 140, y + 70 + dy, bw, 14, 7, C_WHITE, ra * 15 / 100);
    fill_rrect(x + 140, y + 70 + dy, (int)(bw * (double)g->secs / maxs * in(dl + 0.2f)), 14, 7, C_WHITE, ra * 85 / 100);
  }
  if (W.games_count > W.ngames) {
    char m[64]; snprintf(m, sizeof m, _("e altri %d"), W.games_count - W.ngames);
    big_center(m, y + 10, 28, C_DIM, a, 0.9f);
  }
}

static void s_rhythm(int a) {
  static const char *const WD[7] = { N_("luned\xC3\xAC"), N_("marted\xC3\xAC"), N_("mercoled\xC3\xAC"), N_("gioved\xC3\xAC"), N_("venerd\xC3\xAC"), N_("sabato"), N_("domenica") };
  int peak = 0, wpeak = 0;
  long hmax = 1;
  for (int i = 0; i < 24; i++) { if (W.hours[i] > W.hours[peak]) peak = i; if (W.hours[i] > hmax) hmax = W.hours[i]; }
  for (int i = 0; i < 7; i++) if (W.wd[i] > W.wd[wpeak]) wpeak = i;
  const char *when = peak >= 5 && peak < 12 ? _("Giochi soprattutto la mattina") : peak >= 12 && peak < 18 ? _("Giochi soprattutto il pomeriggio")
                   : peak >= 18 && peak < 22 ? _("Giochi soprattutto la sera") : _("Giochi soprattutto di notte");
  big_center(_("Il tuo ritmo"), 140, 44, C_DIM, a, 0);
  big_center(when, 210, 68, C_WHITE, a, 0.1f);
  char l[160]; snprintf(l, sizeof l, _("Ora preferita: %02d:00\xE2\x80\x93%02d:00 \xC2\xB7 giorno preferito: %s"), peak, (peak + 1) % 24, _(WD[wpeak]));
  big_center(l, 320, 32, C_TXT, a, 0.25f);
  int x0 = 260, w = SCREEN_W - 520, base = 820, hmaxpx = 380, bw = w / 24;
  for (int i = 0; i < 24; i++) {
    float dl = 0.35f + 0.02f * i;
    int h = (int)(hmaxpx * (double)W.hours[i] / hmax * in(dl));
    if (h < 4 && W.hours[i] > 0) h = 4;
    Col c = i == peak ? RGB(255, 210, 90) : C_WHITE;
    fill_rrect(x0 + i * bw + 6, base - h, bw - 12, h, 8, c, a * (i == peak ? 95 : 55) / 100);
    if (i % 3 == 0) { char hh[8]; snprintf(hh, sizeof hh, "%02d", i); draw_text(font(W_REG, 24), hh, x0 + i * bw + bw / 2, base + 16, C_DIM, a, AL_C); }
  }
}

static void stat_card(int x, int y, int w, int icon, const char *big, const char *sub, int a, float dl) {
  int ra = fa(a, dl), dy = rise(dl);
  fill_rrect(x, y + dy, w, 300, 36, C_WHITE, ra * 10 / 100);
  draw_icon(icon, x + w / 2, y + 70 + dy, 64, C_WHITE, ra);
  draw_text_fit(font(W_LIGHT, 64), big, x + w / 2, y + 120 + dy, w - 40, C_WHITE, ra, AL_C);
  draw_text_wrap_al(font(W_REG, 27), sub, x + w / 2, y + 210 + dy, w - 60, 2, 36, C_TXT, ra, AL_C);
}

static void s_records(int a) {
  big_center(_("I tuoi record"), 150, 60, C_WHITE, a, 0);
  int n = (W.has_longest ? 1 : 0) + (W.streak >= 2 ? 1 : 0) + (W.new_games ? 1 : 0);
  int w = 480, gap = 50, x = SCREEN_W / 2 - (n * w + (n - 1) * gap) / 2, k = 0;
  if (W.has_longest) {
    char t[48], s[200]; dur(W.longest.secs, t, sizeof t);
    snprintf(s, sizeof s, _("La sessione pi\xC3\xB9 lunga, a %s"), W.longest.name);
    stat_card(x, 380, w, IC_FIRE, t, s, a, 0.2f + 0.15f * k); x += w + gap; k++;
  }
  if (W.streak >= 2) {
    char t[32]; snprintf(t, sizeof t, "%d", W.streak);
    stat_card(x, 380, w, IC_CLOCK, t, _("giorni di fila con almeno una partita"), a, 0.2f + 0.15f * k); x += w + gap; k++;
  }
  if (W.new_games) {
    char t[32]; snprintf(t, sizeof t, "%d", W.new_games);
    stat_card(x, 380, w, IC_STAR, t, W.new_games == 1 ? _("gioco nuovo, mai giocato prima") : _("giochi nuovi, mai giocati prima"), a, 0.2f + 0.15f * k);
  }
}

static void s_trophies(int a) {
  big_center(_("Trofei ottenuti"), 140, 44, C_DIM, a, 0);
  char n[16]; snprintf(n, sizeof n, "%d", (int)lround(W.tr_count * in(0.1f)));
  draw_text(font(W_LIGHT, 170), n, SCREEN_W / 2, 200 + rise(0.1f), C_WHITE, fa(a, 0.1f), AL_C);
  int cw = 200, x = SCREEN_W / 2 - 2 * cw;
  for (int i = 0; i < 4; i++) {
    float dl = 0.3f + 0.1f * i; int ra = fa(a, dl), cx = x + i * cw + cw / 2;
    fill_circle(cx, 500 + rise(dl), 44, GRADE_COL[i], ra);
    draw_icon(IC_STAR, cx, 500 + rise(dl), 56, RGB(20, 22, 32), ra);
    char c[12]; snprintf(c, sizeof c, "%d", W.tr[i]);
    draw_text(font(W_MED, 40), c, cx, 560 + rise(dl), C_WHITE, ra, AL_C);
  }
  if (W.has_rarest) {
    float dl = 0.75f; int ra = fa(a, dl), dy = rise(dl);
    int w = 1100, x0 = SCREEN_W / 2 - w / 2, y = 700 + dy;
    fill_rrect(x0, y, w, 170, 34, C_WHITE, ra * 12 / 100);
    wgame_icon("", W.tr_icon, x0 + 30, y + 25, 120, ra);
    char head[120];
    if (W.tr_pct >= 0) snprintf(head, sizeof head, _("Il pi\xC3\xB9 raro: ce l'ha il %d%% dei giocatori"), W.tr_pct);
    else snprintf(head, sizeof head, "%s", _("Il pi\xC3\xB9 prezioso"));
    draw_text(font(W_REG, 26), head, x0 + 180, y + 26, C_DIM, ra, AL_L);
    fill_circle(x0 + 196, y + 98, 16, GRADE_COL[W.tr_grade], ra);
    draw_text_fit(font(W_MED, 36), W.tr_name[0] ? W.tr_name : _("Trofeo nascosto"), x0 + 226, y + 76, w - 260, C_WHITE, ra, AL_L);
    draw_text_fit(font(W_REG, 24), W.tr_game, x0 + 180, y + 126, w - 220, C_TXT, ra, AL_L);
  }
}

static void s_together(int a) {
  big_center(_("Hai giocato insieme a"), 150, 56, C_WHITE, a, 0);
  int n = W.nfriends, w = 440, gap = 60, x = SCREEN_W / 2 - (n * w + (n - 1) * gap) / 2;
  for (int i = 0; i < n; i++, x += w + gap) {
    WFriend *f = &W.friends[i]; float dl = 0.2f + 0.15f * i; int ra = fa(a, dl), dy = rise(dl);
    fill_rrect(x, 330 + dy, w, 460, 40, C_WHITE, ra * 10 / 100);
    draw_avatar(f->oid, f->avatar, x + w / 2, 460 + dy, 170, ra);
    draw_text_fit(font(W_MED, 38), f->oid, x + w / 2, 580 + dy, w - 40, C_WHITE, ra, AL_C);
    char t[48]; dur(f->secs, t, sizeof t);
    draw_text(font(W_LIGHT, 44), t, x + w / 2, 640 + dy, C_TXT, ra, AL_C);
    draw_text_fit(font(W_REG, 25), f->game, x + w / 2, 712 + dy, w - 40, C_DIM, ra, AL_C);
  }
  line_center(_("Contano le partite allo stesso gioco nello stesso momento."), 860, 26, C_DIM, a, 0.7f);
}

static void s_community(int a) {
  big_center(_("Rispetto alla community"), 180, 44, C_DIM, a, 0);
  char n[16]; snprintf(n, sizeof n, "%d%%", (int)lround(W.percentile * in(0.1f)));
  draw_text(font(W_LIGHT, 220), n, SCREEN_W / 2, 260 + rise(0.1f), C_WHITE, fa(a, 0.1f), AL_C);
  char l[200]; snprintf(l, sizeof l, _("Hai giocato pi\xC3\xB9 del %d%% dei %d giocatori di Omega in questo periodo"), W.percentile, W.players);
  line_center(l, 560, 40, C_WHITE, a, 0.35f);
  const char *tag = W.percentile >= 90 ? _("Sei tra i pi\xC3\xB9 attivi in assoluto.") : W.percentile >= 50 ? _("Pi\xC3\xB9 della met\xC3\xA0 dei giocatori dietro di te.") : _("C'\xC3\xA8 sempre tempo per un'altra partita.");
  line_center(tag, 700, 30, C_TXT, a, 0.55f);
  line_center(_("Il confronto usa solo totali anonimi."), 900, 24, C_DIM, a, 0.8f);
}

static void s_persona(int a) {
  const int p = W.persona;
  int cx = SCREEN_W / 2;
  big_center(_("Il tuo profilo"), 110, 40, C_DIM, a, 0);
  float pop = in(0.1f);
  glow(cx, 290, 260, PERSONAS[p].c, (int)(a * pop * 45 / 100));
  fill_circle(cx, 290 + rise(0.1f), (int)(110 * (0.7f + 0.3f * pop)), mix(PERSONAS[p].c, C_BLACK, 0.2f), fa(a, 0.1f));
  draw_icon(PERSONAS[p].icon, cx, 290 + rise(0.1f), 120, C_WHITE, fa(a, 0.1f));
  big_center(_(PERSONAS[p].title), 430, 84, C_WHITE, a, 0.25f);
  big_center(_(PERSONAS[p].desc), 545, 34, C_TXT, a, 0.35f);
  // la carta riassuntiva
  float dl = 0.55f; int ra = fa(a, dl), dy = rise(dl);
  int w = 1300, x0 = cx - w / 2, y = 640 + dy, cw = w / 4;
  fill_rrect(x0, y, w, 210, 36, C_WHITE, ra * 10 / 100);
  char v[4][110], *lab[4];
  dur(W.total, v[0], sizeof v[0]); lab[0] = (char *)_("di gioco");
  snprintf(v[1], sizeof v[1], "%s", W.ngames ? W.games[0].name : "-"); lab[1] = (char *)_("il pi\xC3\xB9 giocato");
  if (W.days > 0) { snprintf(v[2], sizeof v[2], "%d", W.days); lab[2] = (char *)(W.days == 1 ? _("giorno di gioco") : _("giorni di gioco")); }
  else { snprintf(v[2], sizeof v[2], "%d", W.games_count); lab[2] = (char *)(W.games_count == 1 ? _("gioco") : _("giochi")); }
  snprintf(v[3], sizeof v[3], "%d", W.tr_count); lab[3] = (char *)(W.tr_count == 1 ? _("trofeo") : _("trofei"));
  for (int i = 0; i < 4; i++) {
    int sz = i == 1 ? 34 : 50;
    while (sz > 28 && text_w(font(W_MED, sz), v[i]) > cw - 40) sz -= 2;    // "17 Std. 25 Min." in tedesco
    draw_text_fit(font(W_MED, sz), v[i], x0 + cw * i + cw / 2, y + 44 + (50 - sz) * 6 / 10, cw - 40, C_WHITE, ra, AL_C);
    draw_text_fit(font(W_REG, 24), lab[i], x0 + cw * i + cw / 2, y + 128, cw - 30, C_DIM, ra, AL_C);
  }
  char r[80], foot[160]; range_text(r, sizeof r);
  snprintf(foot, sizeof foot, "%s \xC2\xB7 %s \xC2\xB7 Omega", S.me, r);
  draw_text(font(W_REG, 24), foot, cx, y + 230, C_DIM, ra, AL_C);
}

void wrap_draw(float t) {
  int a = (int)(255 * t);
  int s = W.nslides ? W.slides[W.cur] : S_INTRO;
  draw_bg(s, a);
  if (W.loaded && ov_top() == OV_WRAP) {
    W.st += g_dt;
    if (W.cur < W.nslides - 1 && W.st >= SLIDE_SECS) { W.cur++; W.st = 0; }
  }
  W.enter = approach(W.enter, 1, 6.0f);
  int ca = (int)(a * W.enter);
  draw_progress(a);
  char per[80]; snprintf(per, sizeof per, "%s", _(OPTS[W.opt].label));
  draw_text(font(W_MED, 26), per, 120, 72, C_WHITE, a * 80 / 100, AL_L);
  if (W.loading) { draw_spinner(SCREEN_W / 2, 640, 26, a); s_intro(ca); }
  else if (!W.loaded) { big_center(_("Riepilogo non disponibile"), 460, 54, C_WHITE, a, 0); line_center(_("Controlla la connessione e riprova con Quadrato."), 560, 30, C_DIM, a, 0); }
  else switch (s) {
    case S_INTRO: s_intro(ca); break;
    case S_EMPTY: s_empty(ca); break;
    case S_TOTAL: s_total(ca); break;
    case S_TOP: s_top(ca); break;
    case S_TOP5: s_top5(ca); break;
    case S_RHYTHM: s_rhythm(ca); break;
    case S_RECORDS: s_records(ca); break;
    case S_TROPHIES: s_trophies(ca); break;
    case S_TOGETHER: s_together(ca); break;
    case S_COMMUNITY: s_community(ca); break;
    case S_PERSONA: s_persona(ca); break;
  }
  int ic[5]; const char *lb[5]; int n = 0;
  int last = W.loaded && W.cur == W.nslides - 1;
  if (W.loaded && W.total > 0 && last) { ic[n] = IC_BTN_X; lb[n++] = _("Condividi sulla bacheca"); }
  else if (W.loaded && !last) { ic[n] = IC_BTN_X; lb[n++] = _("Avanti"); }
  ic[n] = IC_BTN_SQ; lb[n++] = _("Periodo");
  if (W.loaded && W.total > 0 && !last) { ic[n] = IC_BTN_TRI; lb[n++] = _("Condividi"); }
  ic[n] = IC_BTN_O; lb[n++] = _("Chiudi");
  hints(ic, lb, n, a);
}

// --------------------------------------------------------------- comandi --
static void go(int d) {
  int c = W.cur + d;
  if (c < 0 || c >= W.nslides) return;
  W.cur = c; W.st = 0; W.enter = 0.3f;
  sfx_play(SFX_MOVE);
}

static void on_shared(int st, JVal *j, const char *raw, void *ud) {
  (void)j; (void)raw; (void)ud;
  if (st == 201 || st == 200) set_msg(_("Riepilogo condiviso sulla bacheca"), 0);
  else set_msg(st == 429 ? _("Hai scritto molto: riprova pi\xC3\xB9 tardi") : _("Pubblicazione non riuscita"), 1);
}
static void share_yes(int idx, void *ud) {
  (void)ud;
  if (idx != 0) return;
  char t[48], txt[600], e[1300], body[1400];
  dur(W.total, t, sizeof t);
  snprintf(txt, sizeof txt, _("Il mio riepilogo su Omega (%s): %s di gioco, il pi\xC3\xB9 giocato \xC3\xA8 %s. Il mio profilo: %s \xE2\x80\x93 %s"),
           _(OPTS[W.opt].label), t, W.ngames ? W.games[0].name : "-", _(PERSONAS[W.persona].title), _(PERSONAS[W.persona].desc));
  json_escape(e, sizeof e, txt);
  snprintf(body, sizeof body, "{\"text\":\"%s\"}", e);
  net_req(HTTP_POST, OMEGA_API "/posts", body, on_shared, NULL);
}
static void share(void) {
  if (!W.loaded || W.total <= 0) return;
  confirm_open(_("Condividere il riepilogo sulla bacheca? Lo vedono i tuoi amici."), _("Condividi"), share_yes, NULL);
}

static void period_pick(int idx, void *ud) {
  (void)ud;
  if (idx < 0 || idx >= NOPTS) return;
  W.opt = idx; W.cur = 0; W.st = 0;
  load();
}

void wrap_input(int b) {
  switch (b) {
    case B_O: ov_pop(); return;
    case B_RIGHT: case B_R1: go(1); return;
    case B_LEFT: case B_L1: go(-1); return;
    case B_X:
      if (!W.loaded) return;
      if (W.cur == W.nslides - 1) share(); else go(1);
      return;
    case B_TRI: share(); return;
    case B_SQ: {
      const char *items[NOPTS];
      for (int i = 0; i < NOPTS; i++) items[i] = _(OPTS[i].label);
      menu_open(_("Periodo del riepilogo"), items, NOPTS, period_pick, NULL);
      return;
    }
  }
}

// ---------------------------------------------- «il tuo riepilogo è pronto» --
// Una volta per settimana (dal lunedì), poco dopo l'accesso: se la settimana
// prima c'è almeno mezz'ora di gioco arriva una notifica. La settimana già
// annunciata sta in OMEGA_DIR/wrap-seen-<utente>.
static Uint32 prompt_at; static char prompt_for[32], prompt_week[16];

static void on_prompt(int st, JVal *j, const char *raw, void *ud) {
  (void)raw; (void)ud;
  if (st != 200 || jnum(j, "total_seconds", 0) < 1800) return;
  toast(IC_STAR, NULL, 0, _("Il tuo riepilogo \xC3\xA8 pronto"), _("Community \xE2\x80\xBA Tempo di gioco \xE2\x80\xBA Triangolo"));
}

void wrap_tick(void) {
  if (!g_token[0] || !S.me[0] || !S.synced_once) return;
  if (strcmp(prompt_for, S.me)) { snprintf(prompt_for, sizeof prompt_for, "%s", S.me); prompt_at = SDL_GetTicks() + 45000; return; }
  if (!prompt_at || SDL_GetTicks() < prompt_at) return;
  prompt_at = 0;
  time_t now = time(NULL); struct tm l = *localtime(&now);
  time_t mon = now - (time_t)(((l.tm_wday + 6) % 7) * 86400);
  struct tm m = *localtime(&mon);
  snprintf(prompt_week, sizeof prompt_week, "%04d%02d%02d", m.tm_year + 1900, m.tm_mon + 1, m.tm_mday);
  char path[160]; snprintf(path, sizeof path, OMEGA_DIR "/wrap-seen-%s", S.me);
  char *seen = file_read(path, 32, NULL);
  int same = seen && !strncmp(seen, prompt_week, 8);
  free(seen);
  if (same) return;
  FILE *f = fopen(path, "w");
  if (f) { fprintf(f, "%s\n", prompt_week); fclose(f); }
  char q[160]; snprintf(q, sizeof q, OMEGA_API "/wrap?period=week&back=1&tz=%d", wrap_tz());
  net_req(HTTP_GET, q, NULL, on_prompt, NULL);
}
