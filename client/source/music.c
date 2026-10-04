// Omega UI — Musica. Suona il demone (omega_redirect, player.c), così la musica
// continua anche durante i giochi; qui si sceglie cosa ascoltare e lo si
// comanda attraverso il suo server di controllo su 127.0.0.1.
// Sorgenti: radio (catalogo radio-browser.info tramite il server Omega),
// server musicali dell'utente con API Subsonic (Navidrome, Gonic, Airsonic...),
// file audio su chiavetta USB o nella console.
#include "app.h"
#include <dirent.h>
#include <math.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <time.h>

#define CTL        "http://127.0.0.1:9095"
#define MUSIC_FILE OMEGA_DIR "/music.json"
#define MAX_ROWS   400
#define MAX_DEPTH  6
#define MAX_SRV    8
#define MAX_FAV    100
#define ROW_H      96
#define LIST_X     110
#define LIST_TOP   170
#define LIST_W     (SCREEN_W - 2 * LIST_X)
#define LIST_BOTTOM (SCREEN_H - 230)
#define MBG mix(RGB(10, 12, 20), g_theme_base, 0.12f)

// ------------------------------------------------------------------ dati --
enum { MT_NOW, MT_RADIO, MT_LIB, MT_FILES, MT_N };
enum { R_ACTION, R_HEADER, R_STATION, R_SERVER, R_ALBUM, R_ARTIST, R_PLAYLIST, R_TRACK, R_DIR, R_FILE, R_EMPTY };
// azioni delle righe R_ACTION
enum { A_RADIO_SEARCH = 1, A_RADIO_FAV, A_RADIO_LOCAL, A_RADIO_WORLD, A_RADIO_TAG, A_ADD_SERVER,
       A_SUB_SEARCH, A_SUB_NEWEST, A_SUB_FREQUENT, A_SUB_RANDOM_ALB, A_SUB_ARTISTS, A_SUB_PLAYLISTS, A_SUB_RANDOM_SONGS,
       A_DIR_PLAYALL };

typedef struct {
  int kind, action, srv;
  char title[200], sub[200];
  char id[128];             // stazione, album, brano... nella sorgente
  char url[640];            // flusso della radio o percorso del file
  char art[512];            // immagine: percorso del server Omega, URL completo o vuoto
  double dur;
} Row;

typedef struct {
  char title[160];
  Row *rows; int n, sel; float anim, scroll;
  int loading, gen;
  int kind;                 // cosa elenca (per ricaricare)
  char key[300];            // parametro della pagina (id, cartella, ricerca)
  int srv;
} Page;

typedef struct { char name[80], url[300], user[80], salt[24], token[40]; } Server;
typedef struct { char id[64], name[200], url[640], art[512], sub[160]; } Fav;

static Server srv[MAX_SRV]; static int nsrv;
static Fav fav[MAX_FAV]; static int nfav;

static int tab;
static Page stack[MT_N][MAX_DEPTH]; static int depth[MT_N];
static float tab_anim;
static int page_gen;

// stato del lettore letto dal demone
typedef struct {
  unsigned seq; char state[12]; int index, count; double pos, dur; int live, volume, shuffle, repeat, has_cover;
  char title[256], artist[256], album[256], cover[640], source[32], id[128], stream_title[256], error[160];
} PState;
static PState P;
static int ctl_ok = -2;        // -2 mai chiesto, -1 il demone non risponde, 1 ok
static Uint32 polled_at, pos_at; static int polling;
static unsigned queue_seq = 0xffffffff;
typedef struct { char title[200], artist[200]; double dur; } QItem;
static QItem queue[300]; static int nqueue, q_sel, now_zone, ctl_sel; static float q_anim, q_scroll;

int music_is_open(void);

// ------------------------------------------------------------------ MD5 --
// Per l'autenticazione Subsonic: token = md5(password + sale). RFC 1321.
typedef struct { uint32_t a, b, c, d; uint64_t len; unsigned char buf[64]; size_t n; } Md5;
static uint32_t rol(uint32_t x, int c) { return (x << c) | (x >> (32 - c)); }
static void md5_block(Md5 *m, const unsigned char *p) {
  static const uint32_t K[64] = {
    0xd76aa478,0xe8c7b756,0x242070db,0xc1bdceee,0xf57c0faf,0x4787c62a,0xa8304613,0xfd469501,0x698098d8,0x8b44f7af,0xffff5bb1,0x895cd7be,0x6b901122,0xfd987193,0xa679438e,0x49b40821,
    0xf61e2562,0xc040b340,0x265e5a51,0xe9b6c7aa,0xd62f105d,0x02441453,0xd8a1e681,0xe7d3fbc8,0x21e1cde6,0xc33707d6,0xf4d50d87,0x455a14ed,0xa9e3e905,0xfcefa3f8,0x676f02d9,0x8d2a4c8a,
    0xfffa3942,0x8771f681,0x6d9d6122,0xfde5380c,0xa4beea44,0x4bdecfa9,0xf6bb4b60,0xbebfbc70,0x289b7ec6,0xeaa127fa,0xd4ef3085,0x04881d05,0xd9d4d039,0xe6db99e5,0x1fa27cf8,0xc4ac5665,
    0xf4292244,0x432aff97,0xab9423a7,0xfc93a039,0x655b59c3,0x8f0ccc92,0xffeff47d,0x85845dd1,0x6fa87e4f,0xfe2ce6e0,0xa3014314,0x4e0811a1,0xf7537e82,0xbd3af235,0x2ad7d2bb,0xeb86d391 };
  static const int S[64] = { 7,12,17,22,7,12,17,22,7,12,17,22,7,12,17,22, 5,9,14,20,5,9,14,20,5,9,14,20,5,9,14,20,
                             4,11,16,23,4,11,16,23,4,11,16,23,4,11,16,23, 6,10,15,21,6,10,15,21,6,10,15,21,6,10,15,21 };
  uint32_t w[16];
  for (int i = 0; i < 16; i++) w[i] = (uint32_t)p[i * 4] | (uint32_t)p[i * 4 + 1] << 8 | (uint32_t)p[i * 4 + 2] << 16 | (uint32_t)p[i * 4 + 3] << 24;
  uint32_t a = m->a, b = m->b, c = m->c, d = m->d;
  for (int i = 0; i < 64; i++) {
    uint32_t f; int g;
    if (i < 16) { f = (b & c) | (~b & d); g = i; }
    else if (i < 32) { f = (d & b) | (~d & c); g = (5 * i + 1) % 16; }
    else if (i < 48) { f = b ^ c ^ d; g = (3 * i + 5) % 16; }
    else { f = c ^ (b | ~d); g = (7 * i) % 16; }
    uint32_t t = d; d = c; c = b; b = b + rol(a + f + K[i] + w[g], S[i]); a = t;
  }
  m->a += a; m->b += b; m->c += c; m->d += d;
}
static void md5_hex(const char *s, char out[33]) {
  Md5 m = { 0x67452301, 0xefcdab89, 0x98badcfe, 0x10325476, 0, {0}, 0 };
  size_t n = strlen(s);
  for (size_t i = 0; i < n; i++) { m.buf[m.n++] = (unsigned char)s[i]; if (m.n == 64) { md5_block(&m, m.buf); m.n = 0; } }
  m.len = (uint64_t)n * 8;
  m.buf[m.n++] = 0x80;
  if (m.n > 56) { while (m.n < 64) m.buf[m.n++] = 0; md5_block(&m, m.buf); m.n = 0; }
  while (m.n < 56) m.buf[m.n++] = 0;
  for (int i = 0; i < 8; i++) m.buf[56 + i] = (unsigned char)(m.len >> (8 * i));
  md5_block(&m, m.buf);
  uint32_t v[4] = { m.a, m.b, m.c, m.d };
  for (int i = 0; i < 16; i++) snprintf(out + i * 2, 3, "%02x", (v[i / 4] >> (8 * (i % 4))) & 0xff);
}

// ------------------------------------------------------------ salvataggio --
static void music_save(void) {
  FILE *f = fopen(MUSIC_FILE ".tmp", "w"); if (!f) return;
  char a[700], b[700], c[700], d[700], e[700];
  fputs("{\"servers\":[", f);
  for (int i = 0; i < nsrv; i++) {
    json_escape(a, sizeof a, srv[i].name); json_escape(b, sizeof b, srv[i].url); json_escape(c, sizeof c, srv[i].user);
    fprintf(f, "%s{\"type\":\"subsonic\",\"name\":\"%s\",\"url\":\"%s\",\"user\":\"%s\",\"salt\":\"%s\",\"token\":\"%s\"}", i ? "," : "", a, b, c, srv[i].salt, srv[i].token);
  }
  fputs("],\"fav\":[", f);
  for (int i = 0; i < nfav; i++) {
    json_escape(a, sizeof a, fav[i].id); json_escape(b, sizeof b, fav[i].name); json_escape(c, sizeof c, fav[i].url);
    json_escape(d, sizeof d, fav[i].art); json_escape(e, sizeof e, fav[i].sub);
    fprintf(f, "%s{\"id\":\"%s\",\"name\":\"%s\",\"url\":\"%s\",\"art\":\"%s\",\"sub\":\"%s\"}", i ? "," : "", a, b, c, d, e);
  }
  fputs("]}\n", f); fclose(f);
  rename(MUSIC_FILE ".tmp", MUSIC_FILE);
}

static void music_load(void) {
  static int done; if (done) return; done = 1;
  size_t len; char *buf = file_read(MUSIC_FILE, 1024 * 1024, &len);
  JVal *j = buf ? json_parse(buf) : NULL;
  JFOR(o, jget(j, "servers")) {
    if (nsrv >= MAX_SRV) break;
    Server *s = &srv[nsrv++];
    jcpy(s->name, sizeof s->name, o, "name"); jcpy(s->url, sizeof s->url, o, "url"); jcpy(s->user, sizeof s->user, o, "user");
    jcpy(s->salt, sizeof s->salt, o, "salt"); jcpy(s->token, sizeof s->token, o, "token");
  }
  JFOR(o, jget(j, "fav")) {
    if (nfav >= MAX_FAV) break;
    Fav *f = &fav[nfav++];
    jcpy(f->id, sizeof f->id, o, "id"); jcpy(f->name, sizeof f->name, o, "name"); jcpy(f->url, sizeof f->url, o, "url");
    jcpy(f->art, sizeof f->art, o, "art"); jcpy(f->sub, sizeof f->sub, o, "sub");
  }
  json_free(j); free(buf);
}

static int fav_index(const char *id) { for (int i = 0; i < nfav; i++) if (!strcmp(fav[i].id, id)) return i; return -1; }

// -------------------------------------------------------------- Subsonic --
static void sub_url(char *out, size_t n, int s, const char *method, const char *params) {
  char user[240]; url_encode(user, sizeof user, srv[s].user, "-_.~");
  snprintf(out, n, "%s/rest/%s?u=%s&t=%s&s=%s&v=1.16.1&c=Omega&f=json%s%s", srv[s].url, method, user, srv[s].token, srv[s].salt,
           params && *params ? "&" : "", params ? params : "");
}
static void sub_art(char *out, size_t n, int s, const char *cover_id) {
  if (!cover_id || !*cover_id) { out[0] = 0; return; }
  char p[200], id[160]; url_encode(id, sizeof id, cover_id, "-_.~"); snprintf(p, sizeof p, "id=%s&size=300", id);
  sub_url(out, n, s, "getCoverArt", p);
  // f=json non serve per le immagini, ma il server lo ignora
}
static JVal *sub_body(JVal *j, const char **err) {
  JVal *r = jget(j, "subsonic-response");
  if (!r) { *err = _("Risposta non valida dal server musicale"); return NULL; }
  if (strcmp(jstr(r, "status", ""), "ok")) {
    JVal *e = jget(r, "error"); int code = (int)jnum(e, "code", 0);
    *err = code == 40 ? _("Nome utente o password errati") : code == 41 ? _("Questo server non accetta l'accesso con token") : _("Il server musicale ha rifiutato la richiesta");
    return NULL;
  }
  return r;
}

// ------------------------------------------------------- pagine e righe --
static Page *cur(void) { return &stack[tab][depth[tab] - 1]; }

static Page *page_push(int kind, const char *title, const char *key, int s) {
  if (depth[tab] >= MAX_DEPTH) return NULL;
  Page *p = &stack[tab][depth[tab]++];
  if (!p->rows) p->rows = calloc(MAX_ROWS, sizeof(Row));
  p->n = 0; p->sel = 0; p->anim = 0; p->scroll = 0; p->loading = 0; p->gen = ++page_gen;
  p->kind = kind; p->srv = s;
  snprintf(p->title, sizeof p->title, "%s", title ? title : "");
  snprintf(p->key, sizeof p->key, "%s", key ? key : "");
  return p;
}
static void page_pop(void) { if (depth[tab] > 1) { depth[tab]--; sfx_play(SFX_BACK); } }

static Row *add_row(Page *p, int kind) {
  if (!p->rows || p->n >= MAX_ROWS) return NULL;
  Row *r = &p->rows[p->n++]; memset(r, 0, sizeof *r); r->kind = kind; r->srv = p->srv;
  return r;
}
static void add_action(Page *p, int action, int icon_unused, const char *title, const char *sub) {
  (void)icon_unused;
  Row *r = add_row(p, R_ACTION); if (!r) return;
  r->action = action; snprintf(r->title, sizeof r->title, "%s", title); snprintf(r->sub, sizeof r->sub, "%s", sub ? sub : "");
}
static void add_header(Page *p, const char *t) { Row *r = add_row(p, R_HEADER); if (r) snprintf(r->title, sizeof r->title, "%s", t); }
static void add_empty(Page *p, const char *t) { Row *r = add_row(p, R_EMPTY); if (r) snprintf(r->title, sizeof r->title, "%s", t); }

// la richiesta risponde solo se la pagina è ancora quella
typedef struct { int tab, depth, gen; } Ticket;
static void *ticket(Page *p) {
  Ticket *t = malloc(sizeof *t); t->tab = (int)(p - &stack[0][0]) / MAX_DEPTH; t->depth = (int)(p - &stack[t->tab][0]); t->gen = p->gen;
  p->loading = 1; return t;
}
static Page *redeem(void *ud) {
  Ticket *t = ud; Page *p = NULL;
  if (t->depth < depth[t->tab] && stack[t->tab][t->depth].gen == t->gen) p = &stack[t->tab][t->depth];
  free(t); if (p) p->loading = 0; return p;
}

// ------------------------------------------------------------------ radio --
static const char *country_of_lang(void) {
  static const char *map[][2] = { {"it","IT"},{"en","US"},{"ja","JP"},{"fr","FR"},{"es","ES"},{"de","DE"},{"nl","NL"},{"pt-PT","PT"},{"pt-BR","BR"},
    {"ru","RU"},{"ko","KR"},{"zh-Hans","CN"},{"zh-Hant","TW"},{"fi","FI"},{"sv","SE"},{"da","DK"},{"nb","NO"},{"pl","PL"},{"tr","TR"},{"cs","CZ"},
    {"hu","HU"},{"el","GR"},{"ro","RO"},{"th","TH"},{"vi","VN"},{"id","ID"},{"uk","UA"} };
  for (size_t i = 0; i < sizeof map / sizeof *map; i++) if (!strcmp(map[i][0], i18n_code())) return map[i][1];
  return "US";
}

static void station_row(Page *p, JVal *s) {
  Row *r = add_row(p, R_STATION); if (!r) return;
  jcpy(r->id, sizeof r->id, s, "id"); jcpy(r->title, sizeof r->title, s, "name"); jcpy(r->url, sizeof r->url, s, "url"); jcpy(r->art, sizeof r->art, s, "icon");
  char tags[120] = ""; int k = 0;
  JFOR(t, jget(s, "tags")) { if (k++ >= 3) break; size_t l = strlen(tags); snprintf(tags + l, sizeof tags - l, "%s%s", l ? ", " : "", t->s ? t->s : ""); }
  const char *cc = jstr(s, "country", ""), *codec = jstr(s, "codec", "");
  int br = (int)jnum(s, "bitrate", 0);
  char q[40] = ""; if (codec[0]) snprintf(q, sizeof q, br ? "%s %d kbps" : "%s", codec, br);
  snprintf(r->sub, sizeof r->sub, "%s%s%s%s%s", tags, tags[0] && cc[0] ? " \xC2\xB7 " : "", cc, q[0] ? " \xC2\xB7 " : "", q);
}

static void stations_done(int st, JVal *j, const char *raw, void *ud) {
  (void)raw; Page *p = redeem(ud); if (!p) return;
  if (st != 200) { add_empty(p, st == 429 ? _("Troppe richieste: riprova tra poco") : _("Radio non disponibili in questo momento")); return; }
  JFOR(s, jget(j, "stations")) station_row(p, s);
  if (!p->n) add_empty(p, _("Nessuna stazione trovata"));
}

static void tags_done(int st, JVal *j, const char *raw, void *ud) {
  (void)raw; Page *p = redeem(ud); if (!p || st != 200) return;
  if (jlen(jget(j, "tags"))) add_header(p, _("Generi"));
  JFOR(t, jget(j, "tags")) {
    Row *r = add_row(p, R_ACTION); if (!r) break;
    r->action = A_RADIO_TAG; jcpy(r->title, sizeof r->title, t, "name"); jcpy(r->id, sizeof r->id, t, "name");
    snprintf(r->sub, sizeof r->sub, _("%d stazioni"), (int)jnum(t, "count", 0));
  }
}

static void radio_root(void) {
  depth[MT_RADIO] = 0; int keep = tab; tab = MT_RADIO;
  Page *p = page_push(0, _("Radio"), NULL, -1);
  char sub[80]; snprintf(sub, sizeof sub, _("%d salvate"), nfav);
  add_action(p, A_RADIO_SEARCH, 0, _("Cerca una radio"), _("Per nome della stazione"));
  add_action(p, A_RADIO_FAV, 0, _("Preferite"), sub);
  add_action(p, A_RADIO_LOCAL, 0, _("Le più ascoltate nel tuo paese"), NULL);
  add_action(p, A_RADIO_WORLD, 0, _("Le più ascoltate nel mondo"), NULL);
  net_req(HTTP_GET, OMEGA_API "/radio/tags", NULL, tags_done, ticket(p));
  p->loading = 0;   // le azioni ci sono già: i generi arrivano dopo
  tab = keep;
}

static void radio_list(const char *title, const char *query) {
  Page *p = page_push(1, title, query, -1); if (!p) return;
  char path[400]; snprintf(path, sizeof path, OMEGA_API "/radio/%s", query);
  net_req(HTTP_GET, path, NULL, stations_done, ticket(p));
}

static void radio_favs(void) {
  Page *p = page_push(2, _("Radio preferite"), NULL, -1); if (!p) return;
  for (int i = 0; i < nfav; i++) {
    Row *r = add_row(p, R_STATION); if (!r) break;
    snprintf(r->id, sizeof r->id, "%s", fav[i].id); snprintf(r->title, sizeof r->title, "%s", fav[i].name);
    snprintf(r->url, sizeof r->url, "%s", fav[i].url); snprintf(r->art, sizeof r->art, "%s", fav[i].art); snprintf(r->sub, sizeof r->sub, "%s", fav[i].sub);
  }
  if (!p->n) add_empty(p, _("Nessuna preferita: premi \xE2\x96\xB3 su una stazione per salvarla"));
}

// ------------------------------------------------------------- libreria --
static void lib_root(void) {
  depth[MT_LIB] = 0; int keep = tab; tab = MT_LIB;
  Page *p = page_push(0, _("Libreria"), NULL, -1);
  for (int i = 0; i < nsrv; i++) {
    Row *r = add_row(p, R_SERVER); if (!r) break;
    r->srv = i; snprintf(r->title, sizeof r->title, "%s", srv[i].name);
    snprintf(r->sub, sizeof r->sub, "%s \xC2\xB7 %s", srv[i].user, srv[i].url);
  }
  add_action(p, A_ADD_SERVER, 0, _("Aggiungi un server musicale"), _("Navidrome, Gonic, Airsonic o altri compatibili Subsonic"));
  tab = keep;
}

static void server_menu_page(int s) {
  Page *p = page_push(3, srv[s].name, NULL, s); if (!p) return;
  add_action(p, A_SUB_SEARCH, 0, _("Cerca"), _("Artisti, album e brani"));
  add_action(p, A_SUB_NEWEST, 0, _("Aggiunti di recente"), NULL);
  add_action(p, A_SUB_FREQUENT, 0, _("Più ascoltati"), NULL);
  add_action(p, A_SUB_RANDOM_ALB, 0, _("Album a caso"), NULL);
  add_action(p, A_SUB_ARTISTS, 0, _("Artisti"), NULL);
  add_action(p, A_SUB_PLAYLISTS, 0, _("Playlist"), NULL);
  add_action(p, A_SUB_RANDOM_SONGS, 0, _("Mix casuale"), _("50 brani a caso dalla tua libreria"));
}

static void album_row(Page *p, JVal *a) {
  Row *r = add_row(p, R_ALBUM); if (!r) return;
  jcpy(r->id, sizeof r->id, a, "id"); jcpy(r->title, sizeof r->title, a, "name");
  if (!r->title[0]) jcpy(r->title, sizeof r->title, a, "title");
  int year = (int)jnum(a, "year", 0), songs = (int)jnum(a, "songCount", 0);
  char y[16] = ""; if (year) snprintf(y, sizeof y, " \xC2\xB7 %d", year);
  char c[48] = ""; if (songs) snprintf(c, sizeof c, _(" \xC2\xB7 %d brani"), songs);
  snprintf(r->sub, sizeof r->sub, "%s%s%s", jstr(a, "artist", ""), y, c);
  sub_art(r->art, sizeof r->art, p->srv, jstr(a, "coverArt", ""));
}
static void song_row(Page *p, JVal *s) {
  Row *r = add_row(p, R_TRACK); if (!r) return;
  jcpy(r->id, sizeof r->id, s, "id"); jcpy(r->title, sizeof r->title, s, "title");
  snprintf(r->sub, sizeof r->sub, "%s \xC2\xB7 %s", jstr(s, "artist", ""), jstr(s, "album", ""));
  r->dur = jnum(s, "duration", 0);
  sub_art(r->art, sizeof r->art, p->srv, jstr(s, "coverArt", ""));
}

static void sub_done(int st, JVal *j, const char *raw, void *ud) {
  (void)raw; Page *p = redeem(ud); if (!p) return;
  const char *err = NULL;
  JVal *r = st == 200 ? sub_body(j, &err) : NULL;
  if (!r) { add_empty(p, err ? err : st < 0 ? _("Server musicale non raggiungibile") : _("Il server musicale ha rifiutato la richiesta")); return; }
  JVal *x;
  if ((x = jget(r, "albumList2"))) { JFOR(a, jget(x, "album")) album_row(p, a); }
  else if ((x = jget(r, "album")) && jget(x, "song")) { JFOR(s, jget(x, "song")) song_row(p, s); }
  else if ((x = jget(r, "artists"))) {
    JFOR(ix, jget(x, "index")) JFOR(a, jget(ix, "artist")) {
      Row *w = add_row(p, R_ARTIST); if (!w) break;
      jcpy(w->id, sizeof w->id, a, "id"); jcpy(w->title, sizeof w->title, a, "name");
      snprintf(w->sub, sizeof w->sub, _("%d album"), (int)jnum(a, "albumCount", 0));
      sub_art(w->art, sizeof w->art, p->srv, jstr(a, "coverArt", ""));
    }
  }
  else if ((x = jget(r, "artist"))) { JFOR(a, jget(x, "album")) album_row(p, a); }
  else if ((x = jget(r, "playlists"))) {
    JFOR(pl, jget(x, "playlist")) {
      Row *w = add_row(p, R_PLAYLIST); if (!w) break;
      jcpy(w->id, sizeof w->id, pl, "id"); jcpy(w->title, sizeof w->title, pl, "name");
      snprintf(w->sub, sizeof w->sub, _("%d brani"), (int)jnum(pl, "songCount", 0));
      sub_art(w->art, sizeof w->art, p->srv, jstr(pl, "coverArt", ""));
    }
  }
  else if ((x = jget(r, "playlist"))) { JFOR(s, jget(x, "entry")) song_row(p, s); }
  else if ((x = jget(r, "randomSongs"))) { JFOR(s, jget(x, "song")) song_row(p, s); }
  else if ((x = jget(r, "searchResult3"))) {
    if (jlen(jget(x, "artist"))) { add_header(p, _("Artisti"));
      JFOR(a, jget(x, "artist")) { Row *w = add_row(p, R_ARTIST); if (!w) break; jcpy(w->id, sizeof w->id, a, "id"); jcpy(w->title, sizeof w->title, a, "name"); snprintf(w->sub, sizeof w->sub, _("%d album"), (int)jnum(a, "albumCount", 0)); sub_art(w->art, sizeof w->art, p->srv, jstr(a, "coverArt", "")); } }
    if (jlen(jget(x, "album"))) { add_header(p, _("Album")); JFOR(a, jget(x, "album")) album_row(p, a); }
    if (jlen(jget(x, "song"))) { add_header(p, _("Brani")); JFOR(s, jget(x, "song")) song_row(p, s); }
  }
  if (!p->n) add_empty(p, _("Niente da mostrare qui"));
  // la prima riga utile
  while (p->sel < p->n - 1 && p->rows[p->sel].kind == R_HEADER) p->sel++;
}

static void sub_page(int s, const char *title, const char *method, const char *params) {
  Page *p = page_push(4, title, params, s); if (!p) return;
  char url[1200]; sub_url(url, sizeof url, s, method, params);
  net_req(HTTP_GET, url, NULL, sub_done, ticket(p));
}

// aggiunta di un server: indirizzo, utente, password → prova con ping
static Server pending_srv;
static void ping_done(int st, JVal *j, const char *raw, void *ud) {
  (void)raw; (void)ud;
  const char *err = NULL;
  if (st == 200 && sub_body(j, &err)) {
    if (nsrv < MAX_SRV) srv[nsrv++] = pending_srv;
    music_save();
    set_msg(_("Server musicale aggiunto"), 0);
    lib_root();
  } else set_msg(err ? err : st < 0 ? _("Server musicale non raggiungibile: controlla l'indirizzo") : _("Questo indirizzo non sembra un server Subsonic"), 1);
}
static void add_server(void) {
  char url[300] = "http://", user[80] = "", pass[128] = "";
  if (!edit_text(_("Indirizzo del server (es. http://192.168.1.10:4533)"), url, sizeof url, 0) || strlen(url) < 10) return;
  size_t l = strlen(url); while (l && url[l - 1] == '/') url[--l] = 0;
  if (strncmp(url, "http://", 7) && strncmp(url, "https://", 8)) { char t[300]; snprintf(t, sizeof t, "http://%s", url); snprintf(url, sizeof url, "%s", t); }
  if (!edit_text(_("Nome utente"), user, sizeof user, 0) || !user[0]) return;
  if (!edit_text(_("Password"), pass, sizeof pass, 1) || !pass[0]) return;
  memset(&pending_srv, 0, sizeof pending_srv);
  snprintf(pending_srv.url, sizeof pending_srv.url, "%s", url);
  snprintf(pending_srv.user, sizeof pending_srv.user, "%s", user);
  const char *host = strstr(url, "://"); host = host ? host + 3 : url;
  snprintf(pending_srv.name, sizeof pending_srv.name, "%.*s", (int)strcspn(host, "/:"), host);
  unsigned r = (unsigned)time(NULL) ^ fnv1a(user) ^ (unsigned)SDL_GetTicks();
  snprintf(pending_srv.salt, sizeof pending_srv.salt, "%08x%04x", r, (unsigned)(SDL_GetPerformanceCounter() & 0xffff));
  char mix_[200]; snprintf(mix_, sizeof mix_, "%s%s", pass, pending_srv.salt);
  md5_hex(mix_, pending_srv.token);
  memset(pass, 0, sizeof pass); memset(mix_, 0, sizeof mix_);
  // il ping usa il server non ancora salvato: lo metto in fondo per costruire l'URL
  int s = nsrv < MAX_SRV ? nsrv : MAX_SRV - 1;
  Server keep = srv[s]; srv[s] = pending_srv;
  char u[1200]; sub_url(u, sizeof u, s, "ping", NULL);
  srv[s] = keep;
  set_msg(_("Verifico il server..."), 0);
  net_req(HTTP_GET, u, NULL, ping_done, NULL);
}

// ------------------------------------------------------------------- file --
static int audio_ext(const char *name) {
  const char *d = strrchr(name, '.'); if (!d) return 0;
  char e[8]; size_t i = 0; for (d++; d[i] && i < 7; i++) e[i] = (char)(d[i] | 0x20); e[i] = 0;
  static const char *ok[] = { "mp3", "flac", "ogg", "oga", "opus", "m4a", "aac", "wav", "wma", "ape", "wv", "aiff", "aif", "mka", "alac", "mp2" };
  for (size_t k = 0; k < sizeof ok / sizeof *ok; k++) if (!strcmp(e, ok[k])) return 1;
  return 0;
}
static int row_cmp(const void *a, const void *b) {
  const Row *x = a, *y = b;
  if (x->kind != y->kind) return x->kind == R_DIR ? -1 : 1;
  return strcasecmp(x->title, y->title);
}
static void files_list(Page *p, const char *dir) {
  DIR *dp = opendir(dir);
  if (!dp) { add_empty(p, _("Cartella non leggibile")); return; }
  struct dirent *e; int first = p->n;
  while ((e = readdir(dp))) {
    if (e->d_name[0] == '.') continue;
    char full[640]; snprintf(full, sizeof full, "%s/%s", dir, e->d_name);
    struct stat st; if (stat(full, &st) != 0) continue;
    int isdir = S_ISDIR(st.st_mode);
    if (!isdir && !audio_ext(e->d_name)) continue;
    Row *r = add_row(p, isdir ? R_DIR : R_FILE); if (!r) break;
    snprintf(r->title, sizeof r->title, "%s", e->d_name); snprintf(r->url, sizeof r->url, "%s", full);
    if (!isdir) snprintf(r->sub, sizeof r->sub, _("%.1f MB"), st.st_size / 1048576.0);
  }
  closedir(dp);
  qsort(p->rows + first, (size_t)(p->n - first), sizeof(Row), row_cmp);
  if (p->n > first && p->rows[first].kind == R_DIR) {
    // "riproduci tutta la cartella" in cima
    if (p->n < MAX_ROWS) { memmove(p->rows + first + 1, p->rows + first, (size_t)(p->n - first) * sizeof(Row)); p->n++; }
    Row *r = &p->rows[first]; memset(r, 0, sizeof *r); r->kind = R_ACTION; r->action = A_DIR_PLAYALL;
    snprintf(r->title, sizeof r->title, "%s", _("Riproduci tutta la cartella")); snprintf(r->url, sizeof r->url, "%s", dir);
    snprintf(r->sub, sizeof r->sub, "%s", _("Anche le sottocartelle, in ordine"));
  }
  if (p->n == first) add_empty(p, _("Nessun file audio in questa cartella"));
}

static void files_root(void) {
  depth[MT_FILES] = 0; int keep = tab; tab = MT_FILES;
  Page *p = page_push(0, _("File"), NULL, -1);
  static const char *where[][2] = {
    { OMEGA_SYSROOT "/mnt/usb0", N_("Chiavetta USB 1") }, { OMEGA_SYSROOT "/mnt/usb1", N_("Chiavetta USB 2") },
    { OMEGA_SYSROOT "/mnt/usb2", N_("Chiavetta USB 3") }, { OMEGA_SYSROOT "/mnt/usb3", N_("Chiavetta USB 4") },
    { OMEGA_SYSROOT "/mnt/ext0", N_("Archivio esteso") }, { OMEGA_DIR "/Music", N_("Musica in Omega") },
    { OMEGA_SYSROOT "/data/music", N_("Cartella /data/music") },
  };
  for (size_t i = 0; i < sizeof where / sizeof *where; i++) {
    struct stat st; if (stat(where[i][0], &st) != 0 || !S_ISDIR(st.st_mode)) continue;
    Row *r = add_row(p, R_DIR); if (!r) break;
    snprintf(r->title, sizeof r->title, "%s", _(where[i][1])); snprintf(r->url, sizeof r->url, "%s", where[i][0]);
    snprintf(r->sub, sizeof r->sub, "%s", where[i][0] + strlen(OMEGA_SYSROOT));
  }
  if (!p->n) add_empty(p, _("Collega una chiavetta USB con la tua musica, oppure copiala in /data/Omega/Music via FTP"));
  tab = keep;
}

// tutti i file audio sotto dir (fino a 3 livelli), in ordine
static int collect(const char *dir, int lvl, Row *out, int max, int n) {
  DIR *dp = opendir(dir); if (!dp) return n;
  Row *tmp = calloc(512, sizeof(Row)); int k = 0;
  struct dirent *e;
  while (tmp && (e = readdir(dp)) && k < 512) {
    if (e->d_name[0] == '.') continue;
    char full[640]; snprintf(full, sizeof full, "%s/%s", dir, e->d_name);
    struct stat st; if (stat(full, &st) != 0) continue;
    int isdir = S_ISDIR(st.st_mode);
    if (!isdir && !audio_ext(e->d_name)) continue;
    tmp[k].kind = isdir ? R_DIR : R_FILE; snprintf(tmp[k].title, sizeof tmp[k].title, "%s", e->d_name); snprintf(tmp[k].url, sizeof tmp[k].url, "%s", full); k++;
  }
  closedir(dp);
  if (tmp) {
    qsort(tmp, (size_t)k, sizeof(Row), row_cmp);
    // prima i file della cartella, poi le sottocartelle
    for (int i = 0; i < k && n < max; i++) if (tmp[i].kind == R_FILE) out[n++] = tmp[i];
    for (int i = 0; i < k && n < max; i++) if (tmp[i].kind == R_DIR && lvl < 3) n = collect(tmp[i].url, lvl + 1, out, max, n);
    free(tmp);
  }
  return n;
}

// ---------------------------------------------------------- comandi al demone --
static void state_parse(JVal *j) {
  if (!j) return;
  P.seq = (unsigned)jnum(j, "seq", 0);
  jcpy(P.state, sizeof P.state, j, "state");
  P.index = (int)jnum(j, "index", -1); P.count = (int)jnum(j, "count", 0);
  P.pos = jnum(j, "pos", 0); P.dur = jnum(j, "dur", 0);
  P.live = jbool(j, "live"); P.volume = (int)jnum(j, "volume", 70);
  P.shuffle = jbool(j, "shuffle"); P.repeat = (int)jnum(j, "repeat", 0); P.has_cover = jbool(j, "has_cover");
  jcpy(P.title, sizeof P.title, j, "title"); jcpy(P.artist, sizeof P.artist, j, "artist"); jcpy(P.album, sizeof P.album, j, "album");
  jcpy(P.cover, sizeof P.cover, j, "cover"); jcpy(P.source, sizeof P.source, j, "source"); jcpy(P.id, sizeof P.id, j, "id");
  jcpy(P.stream_title, sizeof P.stream_title, j, "stream_title"); jcpy(P.error, sizeof P.error, j, "error");
  pos_at = SDL_GetTicks();
}
static int playing(void) { return ctl_ok == 1 && (!strcmp(P.state, "playing") || !strcmp(P.state, "loading")); }
int music_playing(void) { return playing(); }

static void ctl_done(int st, JVal *j, const char *raw, void *ud) {
  (void)raw; (void)ud;
  polling = 0;
  if (st == 200) { ctl_ok = 1; state_parse(j); }
  else ctl_ok = -1;
  audio_external_music(playing());
}
static void ctl_cmd(const char *cmd, double value) {
  char body[96]; snprintf(body, sizeof body, "{\"cmd\":\"%s\",\"value\":%g}", cmd, value);
  net_req(HTTP_POST, CTL "/v1/cmd", body, ctl_done, NULL);
}

static void queue_done(int st, JVal *j, const char *raw, void *ud) {
  (void)raw; (void)ud;
  if (st != 200) return;
  nqueue = 0;
  JFOR(o, jget(j, "items")) {
    if (nqueue >= (int)(sizeof queue / sizeof *queue)) break;
    QItem *q = &queue[nqueue++];
    jcpy(q->title, sizeof q->title, o, "title"); jcpy(q->artist, sizeof q->artist, o, "artist"); q->dur = jnum(o, "dur", 0);
  }
}

// manda una coda al demone; per i brani Subsonic l'URL si costruisce qui
static int click_station(Page *p, int i);
static int playable(const Row *r) { return r->kind == R_STATION || r->kind == R_TRACK || r->kind == R_FILE; }

static void play_rows(Page *p, int start, int only_one) {
  size_t cap = 512 * 1024; char *body = malloc(cap); if (!body) return;
  int first = 0;
  if (!only_one) for (int i = 0; i < start && i < p->n; i++) if (playable(&p->rows[i])) first++;
  size_t at = (size_t)snprintf(body, cap, "{\"mode\":\"replace\",\"start\":%d,\"items\":[", first);
  int k = 0;
  for (int i = 0; i < p->n && at + 4096 < cap; i++) {
    Row *r = &p->rows[i];
    if (!playable(r) || (only_one && i != start)) continue;
    char url[1200], art[700], t[420], a[420], al[420], u[2400], ar[1400], id[260];
    const char *source = r->kind == R_STATION ? "radio" : r->kind == R_TRACK ? "subsonic" : "file";
    if (r->kind == R_TRACK) { char prm[200], idq[160]; url_encode(idq, sizeof idq, r->id, "-_.~"); snprintf(prm, sizeof prm, "id=%s", idq); sub_url(url, sizeof url, r->srv, "stream", prm); }
    else snprintf(url, sizeof url, "%s", r->url);
    // immagine: i percorsi del server Omega diventano URL completi (li apre la UI, non il demone)
    if (r->art[0] == '/') snprintf(art, sizeof art, "%s%s", omega_base(), r->art); else snprintf(art, sizeof art, "%s", r->art);
    char title[200], artist[200] = "", album[200] = "";
    snprintf(title, sizeof title, "%s", r->title);
    if (r->kind == R_TRACK) { const char *dot = strstr(r->sub, " \xC2\xB7 "); if (dot) { snprintf(artist, sizeof artist, "%.*s", (int)(dot - r->sub), r->sub); snprintf(album, sizeof album, "%s", dot + 4); } }
    else if (r->kind == R_STATION) snprintf(album, sizeof album, "%s", _("Radio"));
    else { char *d = strrchr(title, '.'); if (d) *d = 0; }   // file: il demone poi usa i tag, se ci sono
    json_escape(t, sizeof t, title); json_escape(a, sizeof a, artist); json_escape(al, sizeof al, album);
    json_escape(u, sizeof u, url); json_escape(ar, sizeof ar, art); json_escape(id, sizeof id, r->id);
    at += (size_t)snprintf(body + at, cap - at, "%s{\"url\":\"%s\",\"title\":\"%s\",\"artist\":\"%s\",\"album\":\"%s\",\"cover\":\"%s\",\"source\":\"%s\",\"id\":\"%s\",\"dur\":%.0f}",
                           k ? "," : "", u, t, a, al, ar, source, id, r->dur);
    k++;
  }
  snprintf(body + at, cap - at, "]}");
  if (k) {
    net_req(HTTP_POST, CTL "/v1/queue", body, ctl_done, NULL);
    queue_seq = 0xffffffff;
    sfx_play(SFX_SELECT);
    click_station(p, start);
  }
  free(body);
}

// ---------------------------------------------------------------- polling --
void music_tick(void) {
  Uint32 now = SDL_GetTicks();
  int open = music_is_open();
  Uint32 every = open ? 700 : (g_scene == SC_HOME ? 4000 : 0);
  if (!every || polling) return;
  if (ctl_ok == -1 && !open && now - polled_at < 30000) return;    // demone assente: non insistere
  if (now - polled_at < every) return;
  polled_at = now; polling = 1;
  net_req(HTTP_GET, CTL "/v1/state", NULL, ctl_done, NULL);
  if (open && tab == MT_NOW && P.seq != queue_seq && ctl_ok == 1) { queue_seq = P.seq; net_req(HTTP_GET, CTL "/v1/queue", NULL, queue_done, NULL); }
}

static double pos_now(void) {
  double p = P.pos;
  if (!strcmp(P.state, "playing")) p += (SDL_GetTicks() - pos_at) / 1000.0;
  if (P.dur > 0 && p > P.dur) p = P.dur;
  return p;
}
static void fmt_time(double s, char *out, size_t n) {
  int t = (int)s; if (t < 0) t = 0;
  if (t >= 3600) snprintf(out, n, "%d:%02d:%02d", t / 3600, (t / 60) % 60, t % 60);
  else snprintf(out, n, "%d:%02d", t / 60, t % 60);
}

// ------------------------------------------------------- immagini (cache) --
#define NTHUMB 120
typedef struct { char url[640]; SDL_Texture *t; int state; Uint32 used; int size; } Thumb;
static Thumb th[NTHUMB];
static void thumb_cb(const char *key, SDL_Texture *t, SDL_Color avg, void *ud) {
  (void)key; (void)avg;
  int i = (int)(intptr_t)ud;
  if (i < 0 || i >= NTHUMB || th[i].state != 1) { if (t) SDL_DestroyTexture(t); return; }
  th[i].t = t; th[i].state = t ? 2 : 3;
}
static SDL_Texture *thumb(const char *url, int size) {
  if (!url || !url[0]) return NULL;
  int lru = 0;
  for (int i = 0; i < NTHUMB; i++) {
    if (th[i].state && th[i].size == size && !strcmp(th[i].url, url)) { th[i].used = g_frame; return th[i].state == 2 ? th[i].t : NULL; }
    if (th[i].used < th[lru].used) lru = i;
  }
  if (th[lru].state == 1) return NULL;     // tutto occupato da richieste in corso
  if (th[lru].t) SDL_DestroyTexture(th[lru].t);
  memset(&th[lru], 0, sizeof th[lru]);
  snprintf(th[lru].url, sizeof th[lru].url, "%s", url); th[lru].state = 1; th[lru].used = g_frame; th[lru].size = size;
  load_req(LOAD_URL, "music", url, size, size, size / 8, RGB(20, 24, 36), thumb_cb, (void *)(intptr_t)lru);
  return NULL;
}
// copertina del brano: URL dell'elemento, oppure quella dentro il file (dal demone)
static SDL_Texture *now_cover(int size) {
  if (P.cover[0]) return thumb(P.cover, size);
  if (P.has_cover) { char u[96]; snprintf(u, sizeof u, CTL "/v1/cover?i=%d&c=%u", P.index, (unsigned)fnv1a(P.title)); return thumb(u, size); }
  return NULL;
}

// --------------------------------------------------------------- disegno --
static void cover_box(SDL_Texture *t, int x, int y, int s, int icon, int a) {
  if (t) { draw_tex(t, x, y, s, s, a); return; }
  fill_rrect(x, y, s, s, s / 8, RGB(40, 46, 64), a);
  draw_icon(icon, x + s / 2, y + s / 2, s * 45 / 100, C_DIM, a);
}

static int row_icon(const Row *r) {
  switch (r->kind) {
    case R_STATION: return IC_RADIO; case R_SERVER: return IC_MUSIC; case R_ALBUM: return IC_ALBUM; case R_ARTIST: return IC_USER;
    case R_PLAYLIST: return IC_MUSIC; case R_TRACK: return IC_MUSIC; case R_DIR: return IC_FOLDER; case R_FILE: return IC_MUSIC;
  }
  switch (r->action) {
    case A_RADIO_SEARCH: case A_SUB_SEARCH: return IC_SEARCH;
    case A_RADIO_FAV: return IC_STAR; case A_RADIO_LOCAL: case A_RADIO_WORLD: return IC_GLOBE; case A_RADIO_TAG: return IC_RADIO;
    case A_ADD_SERVER: return IC_PLUS; case A_SUB_NEWEST: return IC_CLOCK; case A_SUB_FREQUENT: return IC_STAR;
    case A_SUB_RANDOM_ALB: case A_SUB_RANDOM_SONGS: return IC_SHUFFLE; case A_SUB_ARTISTS: return IC_USER; case A_SUB_PLAYLISTS: return IC_MUSIC;
    case A_DIR_PLAYALL: return IC_PLAY;
  }
  return IC_MUSIC;
}

static int is_current(const Row *r) {
  if (ctl_ok != 1 || P.index < 0) return 0;
  if (r->kind == R_STATION) return !strcmp(P.source, "radio") && !strcmp(P.id, r->id);
  if (r->kind == R_TRACK) return !strcmp(P.source, "subsonic") && !strcmp(P.id, r->id);
  return 0;
}

static void eq_bars(int x, int y, int h, Col c, int a) {
  for (int i = 0; i < 3; i++) {
    float k = playing() ? 0.35f + 0.65f * fabsf(sinf((float)g_time * (4.0f + i * 1.7f) + i)) : 0.3f;
    int bh = (int)(h * k);
    fill_rrect(x + i * 9, y + h - bh, 6, bh, 3, c, a);
  }
}

static void draw_list(Page *p, int a) {
  if (p->sel >= p->n) p->sel = p->n ? p->n - 1 : 0;
  p->anim = approach(p->anim, (float)p->sel, 20.0f);
  int lh = LIST_BOTTOM - LIST_TOP;
  float target = p->sel * ROW_H + ROW_H > lh ? (float)(p->sel * ROW_H + ROW_H - lh + ROW_H) : 0;
  p->scroll = approach(p->scroll, target, 14.0f);
  SDL_Rect clip = { 0, LIST_TOP - 8, SCREEN_W, lh + 8 }; SDL_RenderSetClipRect(R, &clip);
  for (int i = 0; i < p->n; i++) {
    int y = LIST_TOP + i * ROW_H - (int)p->scroll;
    if (y + ROW_H < LIST_TOP - 10 || y > LIST_BOTTOM) continue;
    Row *r = &p->rows[i];
    if (r->kind == R_HEADER) { draw_text(font(W_MED, 26), r->title, LIST_X + 8, y + 46, C_DIM, a, AL_L); continue; }
    if (r->kind == R_EMPTY) { draw_text_wrap_al(font(W_REG, 26), r->title, SCREEN_W / 2, y + 30, LIST_W - 200, 3, 36, C_FAINT, a, AL_C); continue; }
    float fa = clampf(1 - fabsf(p->anim - i), 0, 1);
    if (fa > 0.01f) { fill_rrect(LIST_X, y, LIST_W, ROW_H - 10, 18, C_WHITE, (int)(a * 0.10f * fa)); stroke_rrect(LIST_X - 3, y - 3, LIST_W + 6, ROW_H - 4, 21, 3, C_WHITE, (int)(a * fa)); }
    int s = ROW_H - 26, ix = LIST_X + 14, iy = y + 8;
    SDL_Texture *t = r->art[0] ? thumb(r->art, 160) : NULL;
    if (r->kind == R_ACTION || !r->art[0]) {
      fill_rrect(ix, iy, s, s, 14, r->kind == R_ACTION ? mix(C_ACC, RGB(20, 24, 36), 0.35f) : RGB(40, 46, 64), a);
      draw_icon(row_icon(r), ix + s / 2, iy + s / 2, s / 2, C_WHITE, a);
    } else cover_box(t, ix, iy, s, row_icon(r), a);
    int tx = ix + s + 26, tw = LIST_W - (tx - LIST_X) - 140;
    int cur_row = is_current(r);
    draw_text_fit(font(W_MED, 28), r->title, tx, y + 12, tw, cur_row ? C_ACC2 : C_TXT, a, AL_L);
    if (r->sub[0]) draw_text_fit(font(W_REG, 22), r->sub, tx, y + 50, tw, C_DIM, a, AL_L);
    int rx = LIST_X + LIST_W - 40;
    if (cur_row) eq_bars(rx - 24, y + 28, 30, C_ACC2, a);
    else if (r->kind == R_TRACK && r->dur > 0) { char d[16]; fmt_time(r->dur, d, sizeof d); draw_text(font(W_REG, 24), d, rx, y + 30, C_FAINT, a, AL_R); }
    else if (r->kind == R_STATION && fav_index(r->id) >= 0) draw_icon(IC_STAR, rx - 10, y + 42, 28, C_WARN, a);
    else if (r->kind == R_DIR || r->kind == R_SERVER || r->kind == R_ALBUM || r->kind == R_ARTIST || r->kind == R_PLAYLIST || (r->kind == R_ACTION && r->action != A_ADD_SERVER && r->action != A_RADIO_SEARCH && r->action != A_SUB_SEARCH && r->action != A_DIR_PLAYALL))
      draw_icon(IC_ARROW_R, rx - 10, y + 42, 26, C_FAINT, a);
  }
  if (p->loading) draw_spinner(SCREEN_W / 2, LIST_TOP + (p->n ? p->n * ROW_H - (int)p->scroll + 50 : 120), 22, a);
  SDL_RenderSetClipRect(R, NULL);
}

static void draw_header(int a) {
  fill_rect(0, 0, SCREEN_W, 140, MBG, a);
  draw_icon(IC_MUSIC, LIST_X + 22, 74, 46, C_ACC2, a);
  int tw = draw_text(font(W_LIGHT, 38), _("Musica"), LIST_X + 62, 52, C_WHITE, a, AL_L);
  const char *names[MT_N] = { _("In ascolto"), _("Radio"), _("Libreria"), _("File") };
  int x = LIST_X + 62 + tw + 60, y = 46;     // le schede dopo il titolo, qualunque sia la lingua
  draw_text(font(W_MED, 22), "L1", x, y + 16, C_FAINT, a, AL_L); x += 50;
  tab_anim = approach(tab_anim, (float)tab, 16.0f);
  for (int i = 0; i < MT_N; i++) {
    TTF_Font *f = font(tab == i ? W_BOLD : W_MED, 30);
    int w = text_w(f, names[i]) + 52, h = 54;
    if (tab == i) fill_rrect(x, y, w, h, 27, C_WHITE, a);
    draw_text(f, names[i], x + w / 2, y + h / 2 - TTF_FontHeight(f) / 2, tab == i ? MBG : C_DIM, a, AL_C);
    x += w + 12;
  }
  draw_text(font(W_MED, 22), "R1", x + 8, y + 16, C_FAINT, a, AL_L);
  // percorso della pagina corrente
  if (tab != MT_NOW && depth[tab] > 1) {
    char path[400] = "";
    for (int d = 1; d < depth[tab]; d++) { size_t l = strlen(path); snprintf(path + l, sizeof path - l, "%s%s", d > 1 ? "  \xE2\x80\xBA  " : "", stack[tab][d].title); }
    draw_text_fit(font(W_REG, 24), path, LIST_X + 8, 122, LIST_W, C_DIM, a, AL_L);
  }
}

// barra del brano in corso sotto le liste
static void mini_bar(int a) {
  int y = SCREEN_H - 200, h = 96, x = LIST_X, w = LIST_W;
  fill_rrect(x, y, w, h, 22, RGB(255, 255, 255), a * 8 / 100);
  if (ctl_ok != 1 || P.index < 0) {
    draw_text_fit(font(W_REG, 24), ctl_ok == -1 ? _("Il servizio musicale non risponde: aggiorna Omega o riavvia la console") : _("Scegli una radio, un album o un file per iniziare"), x + 30, y + 32, w - 60, C_FAINT, a, AL_L);
    return;
  }
  SDL_Texture *t = now_cover(160);
  cover_box(t, x + 14, y + 12, h - 24, !strcmp(P.source, "radio") ? IC_RADIO : IC_MUSIC, a);
  int tx = x + h + 14;
  const char *line1 = P.stream_title[0] ? P.stream_title : P.title;
  const char *line2 = P.stream_title[0] ? P.title : P.artist;
  draw_text_fit(font(W_MED, 26), line1, tx, y + 16, w - h - 420, C_TXT, a, AL_L);
  draw_text_fit(font(W_REG, 22), line2, tx, y + 52, w - h - 420, C_DIM, a, AL_L);
  eq_bars(x + w - 380, y + 30, 34, C_ACC2, a);
  char st[64]; const char *s = !strcmp(P.state, "paused") ? _("In pausa") : !strcmp(P.state, "loading") ? _("Caricamento...") : !strcmp(P.state, "error") ? _("Errore") : "";
  if (!s[0] && !P.live && P.dur > 0) { char p1[16], p2[16]; fmt_time(pos_now(), p1, sizeof p1); fmt_time(P.dur, p2, sizeof p2); snprintf(st, sizeof st, "%s / %s", p1, p2); s = st; }
  else if (!s[0] && P.live) s = _("In diretta");
  draw_text(font(W_REG, 24), s, x + w - 320, y + 34, C_DIM, a, AL_L);
}

// scheda "In ascolto"
enum { CB_PREV, CB_PLAY, CB_NEXT, CB_SHUFFLE, CB_REPEAT, CB_VOLDN, CB_VOLUP, CB_N };
static void draw_now(int a) {
  int cs = 560, cx = LIST_X + 20, cy = 190;
  if (ctl_ok == -1) {
    draw_icon(IC_MUSIC, SCREEN_W / 2, 420, 140, C_FAINT, a);
    draw_text_wrap_al(font(W_REG, 30), _("Il servizio musicale non risponde. Serve la versione aggiornata del demone di Omega (omega_redirect): aggiorna Omega e riavvia la console."), SCREEN_W / 2, 540, 1100, 4, 44, C_DIM, a, AL_C);
    return;
  }
  SDL_Texture *t = now_cover(560);
  if (t) shadow_rrect(cx, cy, cs, cs, 40, 40, a * 60 / 100);
  cover_box(t, cx, cy, cs, !strcmp(P.source, "radio") ? IC_RADIO : IC_MUSIC, a);
  int tx = cx + cs + 64, tw = 560;
  if (P.index < 0) {
    draw_text(font(W_LIGHT, 44), _("Niente in riproduzione"), tx, cy + 20, C_TXT, a, AL_L);
    draw_text_wrap(font(W_REG, 26), _("Scegli una radio, un album dal tuo server o un file dalla chiavetta: la musica continua anche quando avvii un gioco."), tx, cy + 90, tw, 4, 38, C_DIM, a);
  } else {
    const char *l1 = P.stream_title[0] ? P.stream_title : P.title;
    draw_text_wrap(font(W_MED, 42), l1, tx, cy + 10, tw, 2, 52, C_WHITE, a);
    int yy = cy + (text_w(font(W_MED, 42), l1) > tw ? 124 : 70);
    if (P.stream_title[0]) draw_text_fit(font(W_REG, 28), P.title, tx, yy, tw, C_ACC2, a, AL_L);
    else { draw_text_fit(font(W_REG, 30), P.artist, tx, yy, tw, C_TXT, a, AL_L); if (P.album[0]) draw_text_fit(font(W_REG, 26), P.album, tx, yy + 42, tw, C_DIM, a, AL_L); }
    // avanzamento
    int by = cy + 300;
    if (P.live) { fill_rrect(tx, by, 150, 40, 20, C_ERR, a); draw_text(font(W_BOLD, 22), _("IN DIRETTA"), tx + 75, by + 7, C_WHITE, a, AL_C); }
    else if (P.dur > 0) {
      float k = (float)clampf((float)(pos_now() / P.dur), 0, 1);
      fill_rrect(tx, by + 14, tw, 8, 4, RGB(255, 255, 255), a * 18 / 100);
      fill_rrect(tx, by + 14, (int)(tw * k), 8, 4, C_WHITE, a);
      fill_circle(tx + (int)(tw * k), by + 18, 10, C_WHITE, a);
      char p1[16], p2[16]; fmt_time(pos_now(), p1, sizeof p1); fmt_time(P.dur, p2, sizeof p2);
      draw_text(font(W_REG, 22), p1, tx, by + 34, C_DIM, a, AL_L); draw_text(font(W_REG, 22), p2, tx + tw, by + 34, C_DIM, a, AL_R);
    }
    if (!strcmp(P.state, "error") && P.error[0]) draw_text_fit(font(W_REG, 22), P.error, tx, by + 70, tw, C_ERR, a, AL_L);
    else if (!strcmp(P.state, "loading")) draw_text(font(W_REG, 22), _("Caricamento..."), tx, by + 70, C_DIM, a, AL_L);
  }
  // comandi
  static const int ico[CB_N] = { IC_PREV, IC_PLAY, IC_NEXT, IC_SHUFFLE, IC_REPEAT, IC_VOLUME, IC_VOLUME };
  int bx = tx, byy = cy + 420;
  for (int i = 0; i < CB_N; i++) {
    int big = i == CB_PLAY, r = big ? 42 : 29;
    int foc = now_zone == 0 && ctl_sel == i;
    int on = (i == CB_SHUFFLE && P.shuffle) || (i == CB_REPEAT && P.repeat);
    int ccx = bx + r;
    if (foc) fill_circle(ccx, byy, r + 6, C_WHITE, a); else if (big) fill_circle(ccx, byy, r, RGB(255, 255, 255), a * 16 / 100);
    int icon = i == CB_PLAY && playing() ? IC_PAUSE : ico[i];
    Col fg = foc ? MBG : on ? C_ACC2 : C_TXT;
    draw_icon(icon, ccx + (icon == IC_PLAY ? 3 : 0), byy, big ? 40 : 28, fg, a);
    if (i == CB_VOLDN || i == CB_VOLUP) draw_text(font(W_BOLD, 20), i == CB_VOLDN ? "\xE2\x88\x92" : "+", ccx + 17, byy - 28, fg, a, AL_C);
    if (i == CB_REPEAT && P.repeat == 2) draw_text(font(W_BOLD, 18), "1", ccx + 16, byy - 26, fg, a, AL_C);
    bx += r * 2 + 18;
  }
  char vol[32]; snprintf(vol, sizeof vol, _("Volume %d%%"), P.volume);
  draw_text(font(W_REG, 22), vol, tx, byy + 64, C_DIM, a, AL_L);

  // coda
  int qx = SCREEN_W - 500, qw = 400, qy = 190, qh = LIST_BOTTOM - qy + 80;
  draw_text(font(W_MED, 28), _("In coda"), qx, qy - 6, C_TXT, a, AL_L);
  char cnt[32]; snprintf(cnt, sizeof cnt, "%d", P.count); draw_text(font(W_REG, 24), cnt, qx + qw, qy - 2, C_FAINT, a, AL_R);
  int rh = 70, ly = qy + 50, lh = qh - 50;
  if (q_sel >= nqueue) q_sel = nqueue ? nqueue - 1 : 0;
  q_anim = approach(q_anim, (float)q_sel, 20.0f);
  int focus_i = now_zone == 1 ? q_sel : (P.index >= 0 ? P.index : 0);
  float target = focus_i * rh + rh > lh ? (float)(focus_i * rh + rh - lh + rh) : 0;
  q_scroll = approach(q_scroll, target, 12.0f);
  SDL_Rect clip = { qx - 10, ly - 6, qw + 20, lh }; SDL_RenderSetClipRect(R, &clip);
  for (int i = 0; i < nqueue; i++) {
    int y = ly + i * rh - (int)q_scroll;
    if (y + rh < ly - 10 || y > ly + lh) continue;
    float fa = now_zone == 1 ? clampf(1 - fabsf(q_anim - i), 0, 1) : 0;
    if (fa > 0.01f) { fill_rrect(qx - 8, y, qw + 16, rh - 8, 14, C_WHITE, (int)(a * 0.10f * fa)); stroke_rrect(qx - 11, y - 3, qw + 22, rh - 2, 17, 3, C_WHITE, (int)(a * fa)); }
    int curq = i == P.index;
    if (curq) eq_bars(qx + 4, y + 18, 26, C_ACC2, a);
    draw_text_fit(font(curq ? W_MED : W_REG, 24), queue[i].title, qx + 44, y + 6, qw - 130, curq ? C_ACC2 : C_TXT, a, AL_L);
    if (queue[i].artist[0]) draw_text_fit(font(W_REG, 20), queue[i].artist, qx + 44, y + 36, qw - 130, C_DIM, a, AL_L);
    if (queue[i].dur > 0) { char d[16]; fmt_time(queue[i].dur, d, sizeof d); draw_text(font(W_REG, 20), d, qx + qw, y + 20, C_FAINT, a, AL_R); }
  }
  if (!nqueue) draw_text(font(W_REG, 24), _("La coda è vuota"), qx, ly + 20, C_FAINT, a, AL_L);
  SDL_RenderSetClipRect(R, NULL);
}

int music_is_open(void) { return ov_top() == OV_MUSIC; }

void music_draw(float t) {
  int a = (int)(255 * t);
  fill_rect(0, 0, SCREEN_W, SCREEN_H, MBG, t > 0.98f ? 255 : a);
  glow(SCREEN_W - 300, 200, 700, C_ACC, a * 12 / 100);
  if (tab == MT_NOW) draw_now(a);
  else { draw_list(cur(), a); mini_bar(a); }
  draw_header(a);
  int ic[6]; const char *lb[6]; int n = 0;
  if (tab == MT_NOW) {
    if (now_zone == 0) { ic[n] = IC_BTN_X; lb[n++] = _("Scegli"); }
    else { ic[n] = IC_BTN_X; lb[n++] = _("Suona"); ic[n] = IC_BTN_SQ; lb[n++] = _("Togli dalla coda"); }
  } else {
    Page *p = cur(); Row *r = p->n ? &p->rows[p->sel] : NULL;
    ic[n] = IC_BTN_X; lb[n++] = r && (r->kind == R_STATION || r->kind == R_TRACK || r->kind == R_FILE) ? _("Ascolta") : _("Apri");
    if (r && r->kind == R_STATION) { ic[n] = IC_BTN_TRI; lb[n++] = fav_index(r->id) >= 0 ? _("Togli dalle preferite") : _("Salva tra le preferite"); }
    else if (r && (r->kind == R_ALBUM || r->kind == R_PLAYLIST || r->kind == R_DIR)) { ic[n] = IC_BTN_TRI; lb[n++] = _("Ascolta tutto"); }
    if (r && r->kind == R_SERVER) { ic[n] = IC_BTN_SQ; lb[n++] = _("Rimuovi server"); }
  }
  if (tab == MT_NOW) { ic[n] = IC_BTN_TRI; lb[n++] = _("Dal telefono"); }
  ic[n] = IC_BTN_O; lb[n++] = tab != MT_NOW && depth[tab] > 1 ? _("Indietro") : _("Chiudi");
  ic[n] = -1; lb[n++] = _("L2/R2  Volume");
  grad_v(0, SCREEN_H - 110, SCREEN_W, 110, MBG, 0, MBG, a);
  hints(ic, lb, n, a);
}

void music_open(void) {
  music_load();
  if (!depth[MT_RADIO]) radio_root();
  if (!depth[MT_LIB]) lib_root();
  if (!depth[MT_FILES]) files_root();
  tab = playing() || ctl_ok != 1 ? MT_NOW : MT_RADIO;
  if (ctl_ok == 1 && P.index < 0) tab = MT_RADIO;
  now_zone = 0; ctl_sel = CB_PLAY;
  polled_at = 0; queue_seq = 0xffffffff;
  ov_push(OV_MUSIC);
}

// --------------------------------------------------------------- comandi --
static void after_album_tracks(int st, JVal *j, const char *raw, void *ud);
typedef struct { int srv; } AlbumPlay;

static void play_album_now(int s, const char *method, const char *id) {
  // carica i brani in una pagina nascosta e li suona tutti
  char prm[200], idq[160]; url_encode(idq, sizeof idq, id, "-_.~"); snprintf(prm, sizeof prm, "id=%s", idq);
  char url[1200]; sub_url(url, sizeof url, s, method, prm);
  AlbumPlay *ap = malloc(sizeof *ap); ap->srv = s;
  net_req(HTTP_GET, url, NULL, after_album_tracks, ap);
  set_msg(_("Preparo la coda..."), 0);
}
static void after_album_tracks(int st, JVal *j, const char *raw, void *ud) {
  (void)raw; AlbumPlay *ap = ud; int s = ap->srv; free(ap);
  const char *err = NULL; JVal *r = st == 200 ? sub_body(j, &err) : NULL;
  if (!r) { set_msg(err ? err : _("Server musicale non raggiungibile"), 1); return; }
  static Page tmp; if (!tmp.rows) tmp.rows = calloc(MAX_ROWS, sizeof(Row));
  tmp.n = 0; tmp.srv = s;
  JVal *x = jget(r, "album"); JVal *list = x ? jget(x, "song") : NULL;
  if (!list && (x = jget(r, "playlist"))) list = jget(x, "entry");
  JFOR(so, list) song_row(&tmp, so);
  if (!tmp.n) { set_msg(_("Nessun brano"), 1); return; }
  play_rows(&tmp, 0, 0);
}

static void play_dir_all(const char *dir, const char *from) {
  static Page tmp; if (!tmp.rows) tmp.rows = calloc(MAX_ROWS, sizeof(Row));
  tmp.n = collect(dir, 0, tmp.rows, MAX_ROWS, 0); tmp.srv = -1;
  if (!tmp.n) { set_msg(_("Nessun file audio in questa cartella"), 1); return; }
  int start = 0; if (from) for (int i = 0; i < tmp.n; i++) if (!strcmp(tmp.rows[i].url, from)) { start = i; break; }
  play_rows(&tmp, start, 0);
}

static void toggle_fav(Row *r) {
  int i = fav_index(r->id);
  if (i >= 0) { memmove(&fav[i], &fav[i + 1], (size_t)(nfav - i - 1) * sizeof *fav); nfav--; set_msg(_("Tolta dalle preferite"), 0); }
  else if (nfav < MAX_FAV) {
    Fav *f = &fav[nfav++]; memset(f, 0, sizeof *f);
    snprintf(f->id, sizeof f->id, "%s", r->id); snprintf(f->name, sizeof f->name, "%s", r->title); snprintf(f->url, sizeof f->url, "%s", r->url);
    snprintf(f->art, sizeof f->art, "%s", r->art); snprintf(f->sub, sizeof f->sub, "%s", r->sub);
    set_msg(_("Salvata tra le preferite"), 0);
  }
  music_save();
  // aggiorna il conteggio nella radice
  Page *root = &stack[MT_RADIO][0];
  for (int k = 0; k < root->n; k++) if (root->rows[k].action == A_RADIO_FAV) snprintf(root->rows[k].sub, sizeof root->rows[k].sub, _("%d salvate"), nfav);
}

static int remove_srv_idx;
static void remove_srv(int idx, void *ud) {
  (void)ud; if (idx != 0) return;
  int s = remove_srv_idx; if (s < 0 || s >= nsrv) return;
  memmove(&srv[s], &srv[s + 1], (size_t)(nsrv - s - 1) * sizeof *srv); nsrv--;
  music_save(); lib_root(); set_msg(_("Server rimosso"), 0);
}

static void click_done(int st, JVal *j, const char *raw, void *ud) { (void)st; (void)j; (void)raw; (void)ud; }
static int click_station(Page *p, int i) {
  if (i < 0 || i >= p->n || p->rows[i].kind != R_STATION) return 0;
  char path[160]; snprintf(path, sizeof path, OMEGA_API "/radio/click/%s", p->rows[i].id);
  net_req(HTTP_POST, path, "{}", click_done, NULL);
  return 1;
}

static void activate(Page *p) {
  if (!p->n) return;
  Row *r = &p->rows[p->sel];
  char q[400], term[160] = "";
  switch (r->kind) {
    case R_FILE: {
      // i file della stessa cartella, a partire da quello scelto
      static Page tmp; if (!tmp.rows) tmp.rows = calloc(MAX_ROWS, sizeof(Row));
      tmp.n = 0; tmp.srv = -1; int start = 0;
      for (int i = 0; i < p->n && tmp.rows; i++) if (p->rows[i].kind == R_FILE) { if (i == p->sel) start = tmp.n; tmp.rows[tmp.n++] = p->rows[i]; }
      if (tmp.n) play_rows(&tmp, start, 0);
      return;
    }
    case R_STATION: case R_TRACK: play_rows(p, p->sel, 0); return;
    case R_DIR: { Page *np = page_push(5, r->title, r->url, -1); if (np) files_list(np, r->url); sfx_play(SFX_OPEN); return; }
    case R_SERVER: server_menu_page(r->srv); sfx_play(SFX_OPEN); return;
    case R_ALBUM: { char prm[200], idq[160]; url_encode(idq, sizeof idq, r->id, "-_.~"); snprintf(prm, sizeof prm, "id=%s", idq); sub_page(r->srv, r->title, "getAlbum", prm); return; }
    case R_ARTIST: { char prm[200], idq[160]; url_encode(idq, sizeof idq, r->id, "-_.~"); snprintf(prm, sizeof prm, "id=%s", idq); sub_page(r->srv, r->title, "getArtist", prm); return; }
    case R_PLAYLIST: { char prm[200], idq[160]; url_encode(idq, sizeof idq, r->id, "-_.~"); snprintf(prm, sizeof prm, "id=%s", idq); sub_page(r->srv, r->title, "getPlaylist", prm); return; }
    case R_ACTION: break;
    default: return;
  }
  int s = p->srv;
  switch (r->action) {
    case A_RADIO_SEARCH:
      if (!edit_text(_("Cerca una radio"), term, sizeof term, 0) || !term[0]) return;
      { char e[400]; url_encode(e, sizeof e, term, "-_.~"); snprintf(q, sizeof q, "search?q=%s&limit=80", e); }
      radio_list(term, q); return;
    case A_RADIO_FAV: radio_favs(); return;
    case A_RADIO_LOCAL: snprintf(q, sizeof q, "top?country=%s&limit=80", country_of_lang()); radio_list(r->title, q); return;
    case A_RADIO_WORLD: radio_list(r->title, "top?limit=80"); return;
    case A_RADIO_TAG: { char e[200]; url_encode(e, sizeof e, r->id, "-_.~"); snprintf(q, sizeof q, "search?tag=%s&limit=80", e); radio_list(r->title, q); return; }
    case A_ADD_SERVER: add_server(); return;
    case A_SUB_SEARCH:
      if (!edit_text(_("Cerca nella libreria"), term, sizeof term, 0) || !term[0]) return;
      { char e[400]; url_encode(e, sizeof e, term, "-_.~"); snprintf(q, sizeof q, "query=%s&artistCount=10&albumCount=20&songCount=50", e); }
      sub_page(s, term, "search3", q); return;
    case A_SUB_NEWEST: sub_page(s, r->title, "getAlbumList2", "type=newest&size=100"); return;
    case A_SUB_FREQUENT: sub_page(s, r->title, "getAlbumList2", "type=frequent&size=100"); return;
    case A_SUB_RANDOM_ALB: sub_page(s, r->title, "getAlbumList2", "type=random&size=60"); return;
    case A_SUB_ARTISTS: sub_page(s, r->title, "getArtists", NULL); return;
    case A_SUB_PLAYLISTS: sub_page(s, r->title, "getPlaylists", NULL); return;
    case A_SUB_RANDOM_SONGS: sub_page(s, r->title, "getRandomSongs", "size=50"); return;
    case A_DIR_PLAYALL: play_dir_all(r->url, NULL); return;
  }
}

static void move_sel(Page *p, int d) {
  int i = p->sel;
  for (;;) {
    i += d;
    if (i < 0 || i >= p->n) return;
    if (p->rows[i].kind != R_HEADER && p->rows[i].kind != R_EMPTY) break;
  }
  p->sel = i; sfx_play(SFX_MOVE);
}

void music_input(int b) {
  if (b == B_L1 || b == B_R1) {
    int nt = tab + (b == B_R1 ? 1 : -1);
    if (nt >= 0 && nt < MT_N) { tab = nt; sfx_play(SFX_MOVE); queue_seq = 0xffffffff; polled_at = 0; }
    return;
  }
  if (b == B_L2 || b == B_R2) { int v = P.volume + (b == B_R2 ? 5 : -5); ctl_cmd("volume", v < 0 ? 0 : v > 100 ? 100 : v); P.volume = v < 0 ? 0 : v > 100 ? 100 : v; return; }
  if (tab == MT_NOW) {
    if (b == B_O) { ov_pop(); return; }
    if (b == B_TRI) { remote_open(); return; }
    if (now_zone == 0) {
      if (b == B_LEFT && ctl_sel > 0) { ctl_sel--; sfx_play(SFX_MOVE); }
      else if (b == B_RIGHT && ctl_sel < CB_N - 1) { ctl_sel++; sfx_play(SFX_MOVE); }
      else if ((b == B_DOWN || b == B_UP) && nqueue) { now_zone = 1; q_sel = P.index >= 0 ? P.index : 0; sfx_play(SFX_MOVE); }
      else if (b == B_X) {
        switch (ctl_sel) {
          case CB_PREV: ctl_cmd("prev", 0); break;
          case CB_PLAY: ctl_cmd("toggle", 0); break;
          case CB_NEXT: ctl_cmd("next", 0); break;
          case CB_SHUFFLE: ctl_cmd("shuffle", !P.shuffle); P.shuffle = !P.shuffle; break;
          case CB_REPEAT: ctl_cmd("repeat", (P.repeat + 1) % 3); P.repeat = (P.repeat + 1) % 3; break;
          case CB_VOLDN: { int v = P.volume - 5; if (v < 0) v = 0; ctl_cmd("volume", v); P.volume = v; break; }
          case CB_VOLUP: { int v = P.volume + 5; if (v > 100) v = 100; ctl_cmd("volume", v); P.volume = v; break; }
        }
        sfx_play(SFX_SELECT);
      }
    } else {
      if (b == B_UP) { if (q_sel > 0) { q_sel--; sfx_play(SFX_MOVE); } else now_zone = 0; }
      else if (b == B_DOWN && q_sel < nqueue - 1) { q_sel++; sfx_play(SFX_MOVE); }
      else if (b == B_LEFT || b == B_RIGHT) now_zone = 0;
      else if (b == B_X) { ctl_cmd("jump", q_sel); sfx_play(SFX_SELECT); }
      else if (b == B_SQ) { ctl_cmd("remove", q_sel); queue_seq = 0xffffffff; sfx_play(SFX_BACK); }
    }
    return;
  }
  Page *p = cur();
  if (b == B_O) { if (depth[tab] > 1) page_pop(); else ov_pop(); return; }
  if (b == B_UP) move_sel(p, -1);
  else if (b == B_DOWN) move_sel(p, 1);
  else if (b == B_X) activate(p);
  else if (b == B_TRI && p->n) {
    Row *r = &p->rows[p->sel];
    if (r->kind == R_STATION) toggle_fav(r);
    else if (r->kind == R_ALBUM) play_album_now(r->srv, "getAlbum", r->id);
    else if (r->kind == R_PLAYLIST) play_album_now(r->srv, "getPlaylist", r->id);
    else if (r->kind == R_DIR) play_dir_all(r->url, NULL);
  } else if (b == B_SQ && p->n && p->rows[p->sel].kind == R_SERVER) {
    remove_srv_idx = p->rows[p->sel].srv;
    confirm_open(_("Rimuovere questo server musicale da Omega?"), _("Rimuovi"), remove_srv, NULL);
  }
}

// ------------------------------------------- per la home e il centro di controllo --
const char *music_now_line(void) {
  static char line[300];
  if (ctl_ok != 1 || P.index < 0 || !strcmp(P.state, "stopped")) return NULL;
  const char *t = P.stream_title[0] ? P.stream_title : P.title;
  if (!P.stream_title[0] && P.artist[0]) snprintf(line, sizeof line, "%s \xC2\xB7 %s", t, P.artist);
  else snprintf(line, sizeof line, "%s", t);
  return line;
}
void music_toggle(void) { if (ctl_ok == 1 && P.index >= 0) { ctl_cmd("toggle", 0); sfx_play(SFX_SELECT); } }
void music_mini(int x, int y, int alpha) {
  if (!music_now_line()) return;
  eq_bars(x, y, 28, playing() ? C_ACC2 : C_FAINT, alpha);
}
