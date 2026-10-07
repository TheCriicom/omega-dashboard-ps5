// Omega UI — Store, con due schede:
//  · Homebrew: app e giochi pubblicati dagli utenti, con scheda di dettaglio,
//    voti, valutazioni, commenti e installazione; chiunque può pubblicare;
//  · Libreria: ogni utente collega l'URL di un proprio JSON e lo Store mostra i
//    suoi giochi.
// Le immagini passano dal server, così la console non contatta siti terzi per
// mostrarle; l'installazione è in install.c.
#include "app.h"
#include <ctype.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define ST_X 80
#define ST_TOP 150
#define TILE_W 300
#define TILE_H 300

// ------------------------------------------------------------------ modello --
typedef struct {
  char id[16], title[96], tagline[140], category[20], version[24], title_id[16], platform[8];
  char author[32]; int author_avatar;
  int has_icon, has_cover, kind; long size, downloads;
  int likes, dislikes; float rating; int ratings, comments, mine;
  char tags[6][24]; int ntags;
  int friends_count, nfr, wished; char fr_oid[3][32]; int fr_av[3];   // amici che l'hanno installato, lista dei desideri
  SDL_Texture *cover; int cover_state;     // 0 nulla, 1 in arrivo, 2 pronta, 3 assente
  Col avg; float appear, foc;
} SApp;
typedef struct { char oid[32]; int avatar, apps, likes; long downloads; } Creator;
static Creator cre[12]; static int ncre;

// voce di "La mia libreria": i dati arrivano tutti dal demone (lib.c), sulla console
typedef struct {
  char id[40], title[128], platform[8], version[24], title_id[16], origin[8];
  char url[1024], cover_url[1024];        // descrizione e screenshot arrivano con la scheda (/v1/library/item)
  int kind; long size; int has_cover;
  SDL_Texture *cover; int cover_state; Col avg; float appear, foc;
} LItem;

typedef struct { char id[16]; char author[32]; int avatar; char body[520]; char when[32]; int mine; } SComment;

#define MAX_SAPP 80
#define MAX_LITEM 2000                      // come il servizio (lib.c)
#define MAX_SCOM 50
#define MAX_SHOT 8

static SApp sapp[MAX_SAPP]; static int nsapp;
static LItem *litem; static int nlitem;     // allocato alla prima lettura

static int tab;              // 0 homebrew, 1 libreria
static int view;             // 0 elenco, 1 dettaglio, 2 pubblica, 3 visore immagini, 4 guida al JSON
static int sort;             // 0 scopri (recenti), 1 più votati, 2 più scaricati, 3 di tendenza
static int wish_filter;       // solo la mia lista dei desideri
static int sel;              // selezione nella griglia
enum { LV_TABS, LV_BAR, LV_CONTENT };   // livello di fuoco nell'elenco
static int lv = LV_CONTENT;
static int loading, gen;
static char q[48];
static int mine_filter;       // homebrew: mostra solo le mie pubblicazioni
static int view_shot;         // visore a schermo intero (view==3): indice screenshot

// Indice in apps del titolo, se è già sulla console, altrimenti -1. Si cerca per
// Title ID oppure per nome (homebrew e payload non hanno un Title ID).
static void name_key(const char *in, char *out, size_t n) {
  size_t o = 0;
  for (; *in && o + 1 < n; in++) if (isalnum((unsigned char)*in)) out[o++] = (char)tolower((unsigned char)*in);
  out[o] = 0;
}
static int installed_index(const char *tid, const char *title) {
  if (tid && tid[0]) for (int i = 0; i < napps; i++) if (!strcmp(apps[i].tid, tid)) return i;
  if (!title || !title[0]) return -1;
  char want[96], have[96]; name_key(title, want, sizeof want);
  if (strlen(want) < 3) return -1;
  for (int i = 0; i < napps; i++) {
    if (!apps[i].hb && !apps[i].pld) continue;
    name_key(apps[i].name, have, sizeof have);
    if (!strcmp(have, want)) return i;
    const char *base = strrchr(apps[i].dir, '/');
    if (apps[i].hb && base) { name_key(base + 1, have, sizeof have); if (!strcmp(have, want)) return i; }
  }
  return -1;
}

#define ST_BG mix(RGB(12, 14, 22), g_theme_base, 0.10f)   // fondo tinto dal tema
static Col g_amb = { 18, 22, 34, 255 };    // colore d'ambiente, dalla copertina a fuoco
static Col d_cover_avg = { 18, 22, 34, 255 };
static void build_shelves(void);

// libreria: JSON collegato (facoltativo) e stato del demone
#define CTL_URL "http://127.0.0.1:9095"
static int src_set, src_loading, lib_down; static char src_url[1024], src_status[64];
static long src_last; static long watch_from = -1; static Uint32 watch_until, watch_next;   // attesa dell'esito della sincronizzazione
static const LItem *lib_find(const char *id);

// dettaglio
static int det_is_lib;              // 0 homebrew, 1 voce della libreria
static char det_id[40];
static int det_loading, det_loaded;
static char d_title[96], d_tagline[140], d_cat[20], d_ver[24], d_author[32], d_desc[4000], d_tid[16], d_platform[8];
static char d_home[300], d_license[48];
static int d_author_av, d_kind, d_has_cover, d_nscreens, d_mine;
static long d_size, d_downloads; static int d_likes, d_dislikes; static float d_rating; static int d_ratings, d_comments;
static int d_my_vote, d_my_rating, d_ntags; static char d_tags[6][24];
static int d_wished, d_wishes, d_fr_n, d_fr_count; static char d_fr_oid[8][32]; static int d_fr_av[8];
static char d_dl_url[1024], d_icon_url[1024], d_cover_url[1024], d_screens[MAX_SHOT][1024]; static int d_nscreen_urls;
static SComment scom[MAX_SCOM]; static int nscom;
static int det_zone;                // 0 azioni, 1 screenshot, 2 commenti
static int act_sel, shot_sel, com_sel;
static float det_scroll, det_scroll_t;
static SDL_Texture *d_cover, *d_shot[MAX_SHOT]; static int d_cover_st, d_shot_st[MAX_SHOT];
static SDL_Texture *d_view; static int d_view_st, d_view_idx = -1;   // screenshot a schermo intero

// pubblica / modifica
static int pub_edit;                // 0 nuovo, 1 modifica (usa det_id)
static int pub_sel;
static char p_title[96], p_tagline[140], p_desc[4000], p_ver[24], p_tid[16], p_url[1024], p_icon[1024], p_cover[1024], p_shots[1024], p_tags[256];
static int p_cat, p_kind;           // indici in CATS e KINDS
// valori inviati al server (dati, restano in italiano); nel modulo si mostrano tradotti
static const char *CATS[] = { N_("app"), N_("gioco"), N_("utility"), N_("emulatore"), N_("trucchi"), N_("tema"), N_("altro") };
static const char *KINDS[] = { "auto", "pkg", "zip", "elf" };
#define NKINDS 4
#define NCATS (int)(sizeof CATS / sizeof CATS[0])

static int kind_of(const char *s) { return !strcmp(s, "pkg") ? 1 : !strcmp(s, "zip") ? 2 : !strcmp(s, "elf") ? 3 : !strcmp(s, "folder") ? 4 : 0; }

// ------------------------------------------------------------------ texture --
static void free_covers(void) {
  for (int i = 0; i < nsapp; i++) if (sapp[i].cover) { SDL_DestroyTexture(sapp[i].cover); sapp[i].cover = NULL; }
  for (int i = 0; i < nlitem; i++) if (litem[i].cover) { SDL_DestroyTexture(litem[i].cover); litem[i].cover = NULL; }
}
static void free_detail_tex(void) {
  if (d_cover) { SDL_DestroyTexture(d_cover); d_cover = NULL; } d_cover_st = 0;
  for (int i = 0; i < MAX_SHOT; i++) { if (d_shot[i]) { SDL_DestroyTexture(d_shot[i]); d_shot[i] = NULL; } d_shot_st[i] = 0; }
  if (d_view) { SDL_DestroyTexture(d_view); d_view = NULL; } d_view_st = 0; d_view_idx = -1;
}

// -------------------------------------------------------------- caricamento --
static void on_apps(int st, JVal *j, const char *raw, void *ud) {
  (void)raw; if ((int)(intptr_t)ud != gen) return;
  loading = 0;
  for (int i = 0; i < nsapp; i++) if (sapp[i].cover) SDL_DestroyTexture(sapp[i].cover);
  nsapp = 0;
  if (st != 200 || !j) return;
  JVal *arr = jget(j, "apps");
  JFOR(a, arr) {
    if (nsapp >= MAX_SAPP) break;
    SApp *s = &sapp[nsapp]; memset(s, 0, sizeof *s);
    jcpy(s->id, sizeof s->id, a, "app_id");
    jcpy(s->title, sizeof s->title, a, "title");
    jcpy(s->tagline, sizeof s->tagline, a, "tagline");
    jcpy(s->category, sizeof s->category, a, "category");
    jcpy(s->version, sizeof s->version, a, "version");
    jcpy(s->title_id, sizeof s->title_id, a, "title_id");
    jcpy(s->platform, sizeof s->platform, a, "platform");
    JVal *au = jget(a, "author");
    if (au) { jcpy(s->author, sizeof s->author, au, "online_id"); s->author_avatar = (int)jnum(au, "avatar", 0); media_note_json(s->author, au); }
    s->has_icon = jbool(a, "has_icon"); s->has_cover = jbool(a, "has_cover");
    s->kind = kind_of(jstr(a, "file_kind", "auto"));
    s->size = (long)jnum(a, "size_bytes", 0); s->downloads = (long)jnum(a, "downloads", 0);
    s->likes = (int)jnum(a, "likes", 0); s->dislikes = (int)jnum(a, "dislikes", 0);
    s->rating = (float)jnum(a, "rating", 0); s->ratings = (int)jnum(a, "ratings", 0);
    s->comments = (int)jnum(a, "comments", 0); s->mine = jbool(a, "mine");
    JVal *tags = jget(a, "hashtags");
    JFOR(t, tags) { if (s->ntags >= 6) break; snprintf(s->tags[s->ntags++], 24, "%s", jstr(t, NULL, "")); }
    s->friends_count = (int)jnum(a, "friends_count", 0); s->wished = jbool(a, "wished");
    JFOR(f, jget(a, "friends")) { if (s->nfr >= 3) break; jcpy(s->fr_oid[s->nfr], 32, f, "online_id"); s->fr_av[s->nfr] = (int)jnum(f, "avatar", 0); media_note_json(s->fr_oid[s->nfr], f); s->nfr++; }
    nsapp++;
  }
  if (sel >= nsapp) sel = nsapp ? nsapp - 1 : 0;
  build_shelves();
}

static void on_creators(int st, JVal *j, const char *raw, void *ud) {
  (void)raw; if ((int)(intptr_t)ud != gen || st != 200 || !j) return;
  ncre = 0;
  JFOR(c, jget(j, "creators")) {
    if (ncre >= 12) break;
    Creator *k = &cre[ncre++]; jcpy(k->oid, sizeof k->oid, c, "online_id"); k->avatar = (int)jnum(c, "avatar", 0);
    k->apps = (int)jnum(c, "apps", 0); k->likes = (int)jnum(c, "likes", 0); k->downloads = (long)jnum(c, "downloads", 0);
    media_note_json(k->oid, c);
  }
  build_shelves();
}
static void load_apps(void) {
  loading = 1; gen++;
  char path[160], enc[64];
  url_encode(enc, sizeof enc, q, "");
  snprintf(path, sizeof path, OMEGA_API "/store/apps?sort=%s%s%s%s%s",
           sort == 1 ? "top" : sort == 2 ? "downloads" : sort == 3 ? "trending" : "recent", q[0] ? "&q=" : "", q[0] ? enc : "",
           mine_filter ? "&mine=1" : "", wish_filter ? "&wish=1" : "");
  net_req(HTTP_GET, path, NULL, on_apps, (void *)(intptr_t)gen);
  if (!q[0] && !mine_filter && !wish_filter && sort == 0) net_req(HTTP_GET, OMEGA_API "/store/creators", NULL, on_creators, (void *)(intptr_t)gen);
}

static int contains_ci(const char *h, const char *n) {
  if (!n[0]) return 1;
  for (; *h; h++) { size_t i = 0; while (n[i] && h[i] && tolower((unsigned char)h[i]) == tolower((unsigned char)n[i])) i++; if (!n[i]) return 1; }
  return 0;
}
static void on_items(int st, JVal *j, const char *raw, void *ud) {
  (void)raw; if ((int)(intptr_t)ud != gen) return;
  loading = 0; src_loading = 0;
  for (int i = 0; i < nlitem; i++) if (litem[i].cover) SDL_DestroyTexture(litem[i].cover);
  nlitem = 0;
  lib_down = st != 200 || !j;
  if (lib_down) return;
  if (!litem && !(litem = calloc(MAX_LITEM, sizeof *litem))) { lib_down = 1; return; }
  JVal *src = jget(j, "source");
  jcpy(src_url, sizeof src_url, src, "url"); jcpy(src_status, sizeof src_status, src, "status");
  src_set = src_url[0] != 0; src_last = (long)jnum(src, "last_sync", 0);
  // sincronizzazione finita (last_sync cambiato): si dice com'è andata
  if (watch_from >= 0 && src_last != watch_from) {
    watch_from = -1;
    int tot = 0, skip = 0;
    if (sscanf(src_status, "ok %d %d", &tot, &skip) >= 1) {
      char m[200];
      if (skip) snprintf(m, sizeof m, _("Link sincronizzato: %d giochi (%d voci senza titolo o link saltate)"), tot, skip);
      else snprintf(m, sizeof m, _("Link sincronizzato: %d giochi"), tot);
      set_msg(m, 0);
    } else if (!strcmp(src_status, "html_not_json")) set_msg(_("Il link porta a una pagina web, non al file JSON: usa il link diretto al file"), 1);
    else if (!strcmp(src_status, "invalid_json")) set_msg(_("Il file del link non è un JSON valido"), 1);
    else if (!strcmp(src_status, "no_items") || !strcmp(src_status, "no_valid_items")) set_msg(_("Nel JSON non ci sono giochi: servono almeno titolo e link"), 1);
    else if (!strcmp(src_status, "too_large")) set_msg(_("Il JSON è troppo grande (massimo 8 MB)"), 1);
    else set_msg(_("Non riesco a scaricare il link: controlla l'indirizzo e la rete"), 1);
  }
  JFOR(a, jget(j, "items")) {
    if (nlitem >= MAX_LITEM) break;
    if (!contains_ci(jstr(a, "title", ""), q)) continue;
    LItem *s = &litem[nlitem]; memset(s, 0, sizeof *s);
    jcpy(s->id, sizeof s->id, a, "id"); jcpy(s->title, sizeof s->title, a, "title");
    jcpy(s->platform, sizeof s->platform, a, "platform"); jcpy(s->version, sizeof s->version, a, "version");
    jcpy(s->title_id, sizeof s->title_id, a, "title_id"); jcpy(s->origin, sizeof s->origin, a, "origin");
    jcpy(s->url, sizeof s->url, a, "url"); jcpy(s->cover_url, sizeof s->cover_url, a, "cover");
    s->kind = kind_of(jstr(a, "kind", "auto"));
    s->size = (long)jnum(a, "size", 0);
    s->has_cover = s->cover_url[0] != 0;
    nlitem++;
  }
  if (sel >= nlitem) sel = nlitem ? nlitem - 1 : 0;
}
static void load_items(void) {
  loading = 1; gen++;
  // elenco leggero: con 2000 giochi e le loro descrizioni non starebbe nel buffer di rete
  net_req(HTTP_GET, CTL_URL "/v1/library?lite=1", NULL, on_items, (void *)(intptr_t)gen);
}
static const LItem *lib_find(const char *id) { if (!litem) return NULL; for (int i = 0; i < nlitem; i++) if (!strcmp(litem[i].id, id)) return &litem[i]; return NULL; }

// --------------------------------------- copertine, caricate quando servono --
static void on_app_cover(const char *key, SDL_Texture *t, SDL_Color avg, void *ud) {
  (void)ud;
  for (int i = 0; i < nsapp; i++) if (sapp[i].cover_state == 1 && !strcmp(sapp[i].id, key)) {
    sapp[i].cover = t; sapp[i].cover_state = t ? 2 : 3; sapp[i].avg = avg; sapp[i].appear = 0; return;
  }
  if (t) SDL_DestroyTexture(t);
}
static void on_lib_cover(const char *key, SDL_Texture *t, SDL_Color avg, void *ud) {
  (void)ud;
  for (int i = 0; i < nlitem; i++) if (litem[i].cover_state == 1 && !strcmp(litem[i].id, key)) {
    litem[i].cover = t; litem[i].cover_state = t ? 2 : 3; litem[i].avg = avg; litem[i].appear = 0; return;
  }
  if (t) SDL_DestroyTexture(t);
}
static void req_app_cover(SApp *s) {
  if (s->cover_state || !s->has_cover) return;
  s->cover_state = 1;
  char path[80]; snprintf(path, sizeof path, OMEGA_API "/store/apps/%s/cover", s->id);
  load_req(LOAD_URL, s->id, path, TILE_W, TILE_H, 18, RGB(20, 24, 36), on_app_cover, NULL);
}
static void req_lib_cover(LItem *s) {
  if (s->cover_state || !s->has_cover) return;
  s->cover_state = 1;
  load_req(LOAD_URL, s->id, s->cover_url, TILE_W, TILE_H, 18, RGB(20, 24, 36), on_lib_cover, NULL);
}

// ---------------------------------------------------------------- dettaglio --
static void on_detail(int st, JVal *j, const char *raw, void *ud) {
  (void)raw; if ((int)(intptr_t)ud != gen) return;
  det_loading = 0;
  if (st != 200 || !j) { set_msg(_("Scheda non disponibile"), 1); return; }
  det_loaded = 1;
  jcpy(d_title, sizeof d_title, j, "title");
  jcpy(d_tagline, sizeof d_tagline, j, "tagline");
  jcpy(d_cat, sizeof d_cat, j, "category");
  jcpy(d_ver, sizeof d_ver, j, "version");
  jcpy(d_desc, sizeof d_desc, j, "description");
  jcpy(d_tid, sizeof d_tid, j, "title_id");
  jcpy(d_platform, sizeof d_platform, j, "platform");
  jcpy(d_home, sizeof d_home, j, "homepage_url");
  jcpy(d_license, sizeof d_license, j, "license");
  d_kind = kind_of(jstr(j, "file_kind", "auto"));
  d_has_cover = jbool(j, "has_cover"); d_nscreens = (int)jnum(j, "nscreens", 0);
  if (d_nscreens > MAX_SHOT) d_nscreens = MAX_SHOT;
  d_size = (long)jnum(j, "size_bytes", 0); d_size = d_size ? d_size : (long)jnum(j, "size", 0);
  d_downloads = (long)jnum(j, "downloads", 0);
  d_likes = (int)jnum(j, "likes", 0); d_dislikes = (int)jnum(j, "dislikes", 0);
  d_rating = (float)jnum(j, "rating", 0); d_ratings = (int)jnum(j, "ratings", 0);
  d_comments = (int)jnum(j, "comment_count", 0); d_mine = jbool(j, "mine");
  d_my_vote = (int)jnum(j, "my_vote", 0); d_my_rating = (int)jnum(j, "my_rating", 0);
  JVal *au = jget(j, "author");
  if (au) { jcpy(d_author, sizeof d_author, au, "online_id"); d_author_av = (int)jnum(au, "avatar", 0); media_note_json(d_author, au); }
  d_wished = jbool(j, "wished"); d_wishes = (int)jnum(j, "wishes", 0); d_fr_count = (int)jnum(j, "friends_count", 0); d_fr_n = 0;
  JFOR(f, jget(j, "friends")) { if (d_fr_n >= 8) break; jcpy(d_fr_oid[d_fr_n], 32, f, "online_id"); d_fr_av[d_fr_n] = (int)jnum(f, "avatar", 0); media_note_json(d_fr_oid[d_fr_n], f); d_fr_n++; }
  d_ntags = 0; JVal *tags = jget(j, "hashtags");
  JFOR(t, tags) { if (d_ntags >= 6) break; snprintf(d_tags[d_ntags++], 24, "%s", jstr(t, NULL, "")); }
  // commenti
  nscom = 0; JVal *cs = jget(j, "comments");
  JFOR(c, cs) {
    if (nscom >= MAX_SCOM) break;
    SComment *m = &scom[nscom]; memset(m, 0, sizeof *m);
    jcpy(m->id, sizeof m->id, c, "comment_id");
    jcpy(m->body, sizeof m->body, c, "body");
    rel_time(jstr(c, "created_at", ""), m->when, sizeof m->when);
    m->mine = jbool(c, "mine");
    JVal *ca = jget(c, "author");
    if (ca) { jcpy(m->author, sizeof m->author, ca, "online_id"); m->avatar = (int)jnum(ca, "avatar", 0); media_note_json(m->author, ca); }
    nscom++;
  }
  // campi originali per la modifica, presenti solo se l'app è mia
  JVal *e = jget(j, "edit");
  if (e) {
    jcpy(d_dl_url, sizeof d_dl_url, e, "download_url");
    jcpy(d_icon_url, sizeof d_icon_url, e, "icon_url");
    jcpy(d_cover_url, sizeof d_cover_url, e, "cover_url");
    d_nscreen_urls = 0; JVal *sc = jget(e, "screenshots");
    JFOR(u, sc) { if (d_nscreen_urls >= MAX_SHOT) break; snprintf(d_screens[d_nscreen_urls++], 1024, "%s", jstr(u, NULL, "")); }
  }
}

// descrizione e screenshot di un gioco della libreria, chiesti al servizio
static void on_lib_item(int st, JVal *j, const char *raw, void *ud) {
  (void)raw; if ((int)(intptr_t)ud != gen || st != 200 || !j) return;
  jcpy(d_desc, sizeof d_desc, j, "description");
  d_nscreen_urls = 0;
  JFOR(u, jget(j, "screenshots")) { if (d_nscreen_urls >= MAX_SHOT) break; snprintf(d_screens[d_nscreen_urls++], 1024, "%s", jstr(u, NULL, "")); }
  d_nscreens = d_nscreen_urls;
}

// scheda di un gioco della libreria: i dati principali sono già in memoria
static void lib_detail(const LItem *it) {
  det_loading = 0; det_loaded = 1;
  snprintf(d_title, sizeof d_title, "%s", it->title); d_desc[0] = 0;
  snprintf(d_ver, sizeof d_ver, "%s", it->version); snprintf(d_tid, sizeof d_tid, "%s", it->title_id);
  snprintf(d_platform, sizeof d_platform, "%s", it->platform);
  snprintf(d_dl_url, sizeof d_dl_url, "%s", it->url); snprintf(d_cover_url, sizeof d_cover_url, "%s", it->cover_url);
  d_kind = it->kind; d_has_cover = it->has_cover; d_nscreens = 0; d_size = it->size;
  d_nscreen_urls = 0;
  char path[120]; snprintf(path, sizeof path, CTL_URL "/v1/library/item?id=%s", it->id);
  net_req(HTTP_GET, path, NULL, on_lib_item, (void *)(intptr_t)gen);
  d_tagline[0] = d_cat[0] = d_author[0] = d_home[0] = d_license[0] = 0; d_mine = 0; d_comments = 0; nscom = 0; d_ntags = 0;
  d_likes = d_dislikes = d_ratings = 0; d_rating = 0; d_my_vote = d_my_rating = 0;
  d_wished = d_wishes = d_fr_n = d_fr_count = 0;
}

static void open_detail(const char *id, int is_lib) {
  free_detail_tex();
  det_is_lib = is_lib; snprintf(det_id, sizeof det_id, "%s", id);
  det_loaded = 0; det_loading = 1; view = 1; det_zone = 0; act_sel = 0; shot_sel = 0; com_sel = 0;
  det_scroll = det_scroll_t = 0; gen++;
  char path[96];
  if (is_lib) { const LItem *it = lib_find(id); if (it) lib_detail(it); else { det_loading = 0; set_msg(_("Scheda non disponibile"), 1); } }
  else { snprintf(path, sizeof path, OMEGA_API "/store/apps/%s", id); net_req(HTTP_GET, path, NULL, on_detail, (void *)(intptr_t)gen); }
}

// copertina e screenshot del dettaglio
static void on_det_cover(const char *key, SDL_Texture *t, SDL_Color avg, void *ud) {
  (void)key; if ((int)(intptr_t)ud != gen) { if (t) SDL_DestroyTexture(t); return; }
  if (d_cover) SDL_DestroyTexture(d_cover);
  d_cover = t; d_cover_st = t ? 2 : 3; if (t) d_cover_avg = avg;
}
static void on_det_shot(const char *key, SDL_Texture *t, SDL_Color avg, void *ud) {
  (void)avg; int n = (int)(intptr_t)ud & 0xff, g = (int)(intptr_t)ud >> 8;
  if (g != (gen & 0xffffff) || n < 0 || n >= MAX_SHOT) { if (t) SDL_DestroyTexture(t); return; }
  (void)key;
  if (d_shot[n]) SDL_DestroyTexture(d_shot[n]);
  d_shot[n] = t; d_shot_st[n] = t ? 2 : 3;
}
static void req_det_cover(void) {
  if (d_cover_st || !d_has_cover) return;
  d_cover_st = 1;
  char path[1100];
  if (det_is_lib) snprintf(path, sizeof path, "%s", d_cover_url); else snprintf(path, sizeof path, OMEGA_API "/store/apps/%s/cover", det_id);
  load_req(LOAD_URL, "detcover", path, 320, 320, 28, RGB(10, 14, 24), on_det_cover, (void *)(intptr_t)gen);
}
static void req_det_shot(int n) {
  if (n < 0 || n >= MAX_SHOT || d_shot_st[n]) return;
  d_shot_st[n] = 1;
  char key[24]; snprintf(key, sizeof key, "shot:%d", n);
  char path[1100];
  if (det_is_lib) snprintf(path, sizeof path, "%s", d_screens[n]); else snprintf(path, sizeof path, OMEGA_API "/store/apps/%s/shot/%d", det_id, n);
  load_req(LOAD_URL, key, path, 460, 258, 14, RGB(10, 14, 24), on_det_shot, (void *)(intptr_t)(((gen & 0xffffff) << 8) | n));
}
// screenshot in alta risoluzione per il visore a schermo intero
static void on_view(const char *key, SDL_Texture *t, SDL_Color avg, void *ud) {
  (void)key; (void)avg;
  if ((int)(intptr_t)ud >> 8 != (gen & 0xffffff)) { if (t) SDL_DestroyTexture(t); return; }
  if (d_view) SDL_DestroyTexture(d_view);
  d_view = t; d_view_st = t ? 2 : 3;
}
static void req_view(int n) {
  if (d_view_idx == n && d_view_st) return;
  d_view_idx = n; d_view_st = 1;
  if (d_view) { SDL_DestroyTexture(d_view); d_view = NULL; }
  char key[24]; snprintf(key, sizeof key, "view:%d", n);
  char path[1100];
  if (det_is_lib) snprintf(path, sizeof path, "%s", d_screens[n]); else snprintf(path, sizeof path, OMEGA_API "/store/apps/%s/shot/%d", det_id, n);
  load_req(LOAD_URL, key, path, 1280, 720, 10, RGB(0, 0, 0), on_view, (void *)(intptr_t)(((gen & 0xffffff) << 8) | n));
}
static void draw_shot_viewer(int a) {
  fill_rect(0, 0, SCREEN_W, SCREEN_H, RGB(3, 5, 10), a > 240 ? 255 : a);
  int n = d_nscreens; if (view_shot >= n) view_shot = n ? n - 1 : 0;
  req_view(view_shot);
  int bw = 1360, bh = 765, bx = (SCREEN_W - bw) / 2, by = (SCREEN_H - bh) / 2 - 20;
  if (d_view && d_view_st == 2) {
    int tw, th; SDL_QueryTexture(d_view, NULL, NULL, &tw, &th);
    float sc = (float)bw / tw; if ((float)bh / th < sc) sc = (float)bh / th;
    int dw = (int)(tw * sc), dh = (int)(th * sc), dx = (SCREEN_W - dw) / 2, dy = by + (bh - dh) / 2;
    shadow_rrect(dx, dy, dw, dh, 12, 44, a * 55 / 100);
    draw_tex(d_view, dx, dy, dw, dh, a);
  } else { fill_rrect(bx, by, bw, bh, 14, RGB(14, 18, 28), a); draw_spinner(SCREEN_W / 2, SCREEN_H / 2 - 20, 22, a); }
  char c[24]; snprintf(c, sizeof c, "%d / %d", view_shot + 1, n);
  draw_text(font(W_MED, 28), c, SCREEN_W / 2, by + bh + 16, C_DIM, a, AL_C);
  if (view_shot > 0) draw_icon(IC_BACK, 60, SCREEN_H / 2 - 20, 48, C_DIM, a);
  if (view_shot < n - 1) draw_icon(IC_FWD, SCREEN_W - 60, SCREEN_H / 2 - 20, 48, C_DIM, a);
  const int ic[] = { IC_BTN_O };
  const char *lb[] = { _("Chiudi") };
  hints(ic, lb, 1, a);
}

// ------------------------------------------------------------------- azioni --
static void after_action(int st, JVal *j, const char *raw, void *ud) {
  (void)raw; (void)ud;
  if (st == 200 && j) {
    d_likes = (int)jnum(j, "likes", d_likes); d_dislikes = (int)jnum(j, "dislikes", d_dislikes);
    if (jget(j, "my_vote")) d_my_vote = (int)jnum(j, "my_vote", d_my_vote);
    d_rating = (float)jnum(j, "rating", d_rating); d_ratings = (int)jnum(j, "ratings", d_ratings);
    if (jget(j, "my_rating")) d_my_rating = (int)jnum(j, "my_rating", d_my_rating);
  } else set_msg(_("Operazione non riuscita"), 1);
}
static void send_vote(int value) {
  char path[80], body[32];
  snprintf(path, sizeof path, OMEGA_API "/store/apps/%s/vote", det_id);
  snprintf(body, sizeof body, "{\"value\":%d}", value);
  d_my_vote = value;   // subito a schermo, poi il server conferma
  net_req(HTTP_POST, path, body, after_action, NULL);
}
static void rate_pick(int idx, void *ud) {
  (void)ud; if (idx < 0 || idx > 4) return;
  char path[80], body[24];
  snprintf(path, sizeof path, OMEGA_API "/store/apps/%s/rate", det_id);
  snprintf(body, sizeof body, "{\"stars\":%d}", idx + 1);
  d_my_rating = idx + 1;
  net_req(HTTP_POST, path, body, after_action, NULL);
}

static void wish_done(int st, JVal *j, const char *raw, void *ud) {
  (void)raw; (void)ud;
  if (st == 200 && j) { d_wished = jbool(j, "wished"); d_wishes = (int)jnum(j, "wishes", d_wishes);
    set_msg(d_wished ? _("Nella tua lista dei desideri: ti avviso quando esce una versione nuova") : _("Tolto dalla lista dei desideri"), 0);
    for (int i = 0; i < nsapp; i++) if (!strcmp(sapp[i].id, det_id)) sapp[i].wished = d_wished;
    build_shelves();
  } else set_msg(_("Operazione non riuscita"), 1);
}
static void toggle_wish(void) {
  char path[96], body[24]; snprintf(path, sizeof path, OMEGA_API "/store/apps/%s/wish", det_id);
  snprintf(body, sizeof body, "{\"on\":%s}", d_wished ? "false" : "true");
  d_wished = !d_wished;
  net_req(HTTP_POST, path, body, wish_done, NULL);
}
static char rec_oid[MAX_FRIENDS][32]; static const char *rec_items[32]; static int nrec;
static void rec_done(int st, JVal *j, const char *raw, void *ud) {
  (void)j; (void)raw; (void)ud;
  set_msg(st == 200 ? _("Consigliato: il tuo amico riceve una notifica") : st == 403 ? _("Potete consigliarvi le app solo tra amici") : _("Invio non riuscito"), st != 200);
}
static void rec_pick(int idx, void *ud) {
  (void)ud; if (idx < 0 || idx >= nrec) return;
  char esc[80], body[120], path[96]; json_escape(esc, sizeof esc, rec_oid[idx]);
  snprintf(body, sizeof body, "{\"online_id\":\"%s\"}", esc);
  snprintf(path, sizeof path, OMEGA_API "/store/apps/%s/recommend", det_id);
  net_req(HTTP_POST, path, body, rec_done, NULL);
}
static void recommend_open(void) {
  nrec = 0;
  // prima chi è online, poi gli altri
  for (int pass = 0; pass < 2; pass++)
    for (int i = 0; i < S.nfriends && nrec < 32; i++) if (friend_online(&S.friends[i]) == !pass) { snprintf(rec_oid[nrec], 32, "%s", S.friends[i].oid); rec_items[nrec] = rec_oid[nrec]; nrec++; }
  if (!nrec) { set_msg(_("Aggiungi qualche amico per consigliargli le app"), 0); return; }
  menu_open(_("Consiglia a un amico"), rec_items, nrec, rec_pick, NULL);
}

static void comment_done(int st, JVal *j, const char *raw, void *ud) {
  (void)j; (void)raw; (void)ud;
  if (st == 201) { set_msg(_("Commento pubblicato"), 0); open_detail(det_id, 0); }
  else set_msg(_("Commento non riuscito"), 1);
}
static void write_comment(void) {
  char buf[480] = "";
  if (!edit_text(_("Scrivi un commento"), buf, sizeof buf, 0) || !buf[0]) return;
  char esc[1000], body[1040], path[80];
  json_escape(esc, sizeof esc, buf);
  snprintf(body, sizeof body, "{\"text\":\"%s\"}", esc);
  snprintf(path, sizeof path, OMEGA_API "/store/apps/%s/comments", det_id);
  net_req(HTTP_POST, path, body, comment_done, NULL);
}

static void resolve_done(int st, JVal *j, const char *raw, void *ud) {
  (void)raw; (void)ud;
  if (st != 200 || !j) { set_msg(_("Link non risolvibile"), 1); return; }
  InstallReq r; memset(&r, 0, sizeof r);
  char kind[8];
  jcpy(r.url, sizeof r.url, j, "url");
  jcpy(r.filename, sizeof r.filename, j, "filename");
  jcpy(kind, sizeof kind, j, "kind");
  jcpy(r.title_id, sizeof r.title_id, j, "title_id");
  jcpy(r.name, sizeof r.name, j, "title");
  jcpy(r.version, sizeof r.version, j, "version");
  jcpy(r.category, sizeof r.category, j, "category");
  jcpy(r.desc, sizeof r.desc, j, "summary");
  jcpy(r.source, sizeof r.source, j, "homepage_url");
  if (!r.name[0]) snprintf(r.name, sizeof r.name, "%s", d_title);
  r.kind = kind_of(kind);
  r.size = (long)jnum(j, "size", 0);
  install_begin(&r);
}
static void do_install(void) {
  if (install_busy()) { set_msg(_("C'è già un'installazione in corso"), 1); return; }
  if (det_is_lib) {
    InstallReq r; memset(&r, 0, sizeof r);
    snprintf(r.url, sizeof r.url, "%s", d_dl_url); snprintf(r.name, sizeof r.name, "%s", d_title);
    snprintf(r.title_id, sizeof r.title_id, "%s", d_tid); snprintf(r.version, sizeof r.version, "%s", d_ver);
    snprintf(r.category, sizeof r.category, "gioco"); r.kind = d_kind; r.size = d_size;
    const char *f = strrchr(d_dl_url, '/'); snprintf(r.filename, sizeof r.filename, "%.*s", (int)strcspn(f ? f + 1 : "game", "?#"), f ? f + 1 : "game");
    install_begin(&r);
    return;
  }
  char path[96];
  snprintf(path, sizeof path, OMEGA_API "/store/apps/%s/download", det_id);
  set_msg(_("Risoluzione del link..."), 0);
  net_req(HTTP_GET, path, NULL, resolve_done, NULL);
}

static void delete_done(int st, JVal *j, const char *raw, void *ud) {
  (void)j; (void)raw; (void)ud;
  if (st == 200) { set_msg(_("Homebrew rimosso"), 0); view = 0; lv = LV_CONTENT; load_apps(); }
  else set_msg(_("Rimozione non riuscita"), 1);
}
static void confirm_delete(int idx, void *ud) {
  (void)ud; if (idx != 0) return;
  char path[80]; snprintf(path, sizeof path, OMEGA_API "/store/apps/%s", det_id);
  net_req(HTTP_DELETE, path, NULL, delete_done, NULL);
}

// ------------------------------------------------------------ pubblicazione --
static void publish_reset(void) {
  p_title[0] = p_tagline[0] = p_desc[0] = p_ver[0] = p_tid[0] = p_url[0] = 0;
  p_icon[0] = p_cover[0] = p_shots[0] = p_tags[0] = 0; p_cat = 0; p_kind = 0; pub_sel = 0;
}
static void publish_open_new(void) { publish_reset(); pub_edit = 0; view = 2; }
static void publish_open_edit(void) {
  publish_reset(); pub_edit = 1; view = 2;
  snprintf(p_title, sizeof p_title, "%s", d_title);
  snprintf(p_tagline, sizeof p_tagline, "%s", d_tagline);
  snprintf(p_desc, sizeof p_desc, "%s", d_desc);
  snprintf(p_ver, sizeof p_ver, "%s", d_ver);
  snprintf(p_tid, sizeof p_tid, "%s", d_tid);
  snprintf(p_url, sizeof p_url, "%s", d_dl_url);
  snprintf(p_icon, sizeof p_icon, "%s", d_icon_url);
  snprintf(p_cover, sizeof p_cover, "%s", d_cover_url);
  for (int i = 0; i < NCATS; i++) if (!strcmp(CATS[i], d_cat)) p_cat = i;
  p_kind = d_kind;
  p_shots[0] = 0;
  for (int i = 0; i < d_nscreen_urls; i++) { size_t L = strlen(p_shots); snprintf(p_shots + L, sizeof p_shots - L, "%s%s", L ? " " : "", d_screens[i]); }
  p_tags[0] = 0;
  for (int i = 0; i < d_ntags; i++) { size_t L = strlen(p_tags); snprintf(p_tags + L, sizeof p_tags - L, "%s%s", L ? " " : "", d_tags[i]); }
}

static void publish_done(int st, JVal *j, const char *raw, void *ud) {
  (void)raw; (void)ud;
  if (st == 201 || st == 200) {
    set_msg(pub_edit ? _("Homebrew aggiornato") : _("Homebrew pubblicato"), 0);
    view = 0; lv = LV_CONTENT; tab = 0; load_apps();
  } else {
    const char *e = j ? jstr(j, "error", "") : "";
    set_msg(!strcmp(e, "title_required") ? _("Serve un titolo") :
            !strcmp(e, "download_url_required") || !strcmp(e, "download_url_invalid") ? _("Serve un link http(s) al file") :
            !strcmp(e, "rights_not_confirmed") ? _("Serve la conferma sui diritti") :
            _("Pubblicazione non riuscita"), 1);
  }
}
// aggiunge "chiave":"valore" (con escape) all'oggetto JSON in costruzione
static void jadd_str(char *dst, size_t n, const char *key, const char *val, int *first) {
  char esc[2200]; json_escape(esc, sizeof esc, val);
  size_t L = strlen(dst);
  snprintf(dst + L, n - L, "%s\"%s\":\"%s\"", *first ? "" : ",", key, esc);
  *first = 0;
}
// aggiunge un array JSON di stringhe da un elenco separato da spazi o virgole
static void jadd_arr(char *dst, size_t n, const char *key, const char *val, int *first) {
  size_t L = strlen(dst);
  L += (size_t)snprintf(dst + L, n - L, "%s\"%s\":[", *first ? "" : ",", key); *first = 0;
  char tmp[1100]; snprintf(tmp, sizeof tmp, "%s", val);
  int items = 0;
  for (char *tok = strtok(tmp, " ,\t"); tok; tok = strtok(NULL, " ,\t")) {
    char esc[1100]; json_escape(esc, sizeof esc, tok);
    L = strlen(dst);
    L += (size_t)snprintf(dst + L, n - L, "%s\"%s\"", items ? "," : "", esc);
    items++;
  }
  L = strlen(dst); snprintf(dst + L, n - L, "]");
}
static void send_publish(int idx, void *ud) {
  (void)idx; (void)ud;
  static char body[8192]; body[0] = 0; int first = 1;
  jadd_str(body, sizeof body, "title", p_title, &first);
  jadd_str(body, sizeof body, "tagline", p_tagline, &first);
  jadd_str(body, sizeof body, "description", p_desc, &first);
  jadd_str(body, sizeof body, "category", CATS[p_cat], &first);
  jadd_str(body, sizeof body, "version", p_ver, &first);
  jadd_str(body, sizeof body, "title_id", p_tid, &first);
  jadd_str(body, sizeof body, "download_url", p_url, &first);
  jadd_str(body, sizeof body, "file_kind", KINDS[p_kind], &first);
  jadd_str(body, sizeof body, "icon_url", p_icon, &first);
  jadd_str(body, sizeof body, "cover_url", p_cover, &first);
  jadd_arr(body, sizeof body, "screenshots", p_shots, &first);
  jadd_arr(body, sizeof body, "hashtags", p_tags, &first);
  char wrapped[8340]; snprintf(wrapped, sizeof wrapped, "{%s,\"rights_confirmed\":true}", body);
  char path[80];
  if (pub_edit) snprintf(path, sizeof path, OMEGA_API "/store/apps/%s", det_id);
  else snprintf(path, sizeof path, OMEGA_API "/store/apps");
  net_req(HTTP_POST, path, wrapped, publish_done, NULL);
}
static void do_publish(void) {
  if (!p_title[0]) { set_msg(_("Serve un titolo"), 1); return; }
  if (!p_url[0]) { set_msg(_("Serve il link al file (pkg o zip)"), 1); return; }
  // chi pubblica dichiara di poterlo fare (termini d'uso, "Pubblicare nello Store")
  confirm_open(_("Confermi che \xC3\xA8 un homebrew tuo o redistribuibile, e non un gioco o software piratato?"), _("Confermo"), send_publish, NULL);
}

// ------------------------------------------------------ libreria: comandi --
static void lib_done(int st, JVal *j, const char *raw, void *ud) {
  (void)raw;
  const char *ok = ud;
  // 202 = sincronizzazione partita: si riguarda la libreria finché non finisce (30 s al massimo)
  if (st == 202) { watch_from = src_last; watch_until = SDL_GetTicks() + 30000; watch_next = SDL_GetTicks() + 2000; }
  if (st == 200 || st == 202) { if (ok) set_msg(ok, 0); }
  else set_msg(st < 0 ? _("Il servizio di Omega non risponde: aggiorna Omega o riavvia la console") :
               !strcmp(j ? jstr(j, "error", "") : "", "title_and_url_required") ? _("Servono il titolo e un link http(s)") : _("Operazione non riuscita"), 1);
  load_items();
}
static void set_library_url(void) {
  char buf[1024]; snprintf(buf, sizeof buf, "%s", src_url[0] ? src_url : "https://");
  if (!edit_text(_("Link al tuo JSON di giochi (solo backup di giochi che possiedi)"), buf, sizeof buf, 0) || strlen(buf) < 10) return;
  char esc[2100], body[2200]; json_escape(esc, sizeof esc, buf);
  snprintf(body, sizeof body, "{\"url\":\"%s\"}", esc);
  net_req(HTTP_POST, CTL_URL "/v1/library/source", body, lib_done, (void *)_("JSON collegato: sincronizzo, i giochi compaiono tra poco"));
}
static void unlink_yes(int idx, void *ud) {
  (void)ud; if (idx != 0) return;
  net_req(HTTP_POST, CTL_URL "/v1/library/source", "{\"url\":\"\"}", lib_done, (void *)_("JSON scollegato: restano i giochi aggiunti a mano"));
}
static void sync_library(void) { net_req(HTTP_POST, CTL_URL "/v1/library/sync", "{}", lib_done, (void *)_("Sincronizzazione in corso...")); }

// aggiunta o modifica dalla console: titolo, link, copertina (il resto dal telefono)
static void lib_edit(const LItem *it) {
  char title[128] = "", url[1024] = "https://", cover[1024] = "https://";
  if (it) { snprintf(title, sizeof title, "%s", it->title); snprintf(url, sizeof url, "%s", it->url); snprintf(cover, sizeof cover, "%s", it->cover_url[0] ? it->cover_url : "https://"); }
  if (!edit_text(_("Titolo del gioco"), title, sizeof title, 0) || !title[0]) return;
  if (!edit_text(_("Link per scaricarlo (pkg, zip o elf)"), url, sizeof url, 0) || strlen(url) < 10) return;
  if (!edit_text(_("Link della copertina (facoltativo)"), cover, sizeof cover, 0)) cover[0] = 0;
  if (!strcmp(cover, "https://") || !strcmp(cover, "http://")) cover[0] = 0;
  static char body[4400]; char a[300], b[2100], c[2100], d[100];
  json_escape(a, sizeof a, title); json_escape(b, sizeof b, url); json_escape(c, sizeof c, cover); json_escape(d, sizeof d, it ? it->id : "");
  if (it) snprintf(body, sizeof body, "{\"id\":\"%s\",\"title\":\"%s\",\"url\":\"%s\",\"cover\":\"%s\",\"platform\":\"%s\",\"title_id\":\"%s\",\"version\":\"%s\"}",
                   d, a, b, c, it->platform, it->title_id, it->version);
  else snprintf(body, sizeof body, "{\"title\":\"%s\",\"url\":\"%s\",\"cover\":\"%s\"}", a, b, c);
  net_req(HTTP_POST, CTL_URL "/v1/library/item", body, lib_done, (void *)(it ? _("Gioco aggiornato") : _("Gioco aggiunto alla libreria")));
}
static void lib_delete_yes(int idx, void *ud) {
  (void)ud; if (idx != 0) return;
  char esc[100], body[140]; json_escape(esc, sizeof esc, det_id); snprintf(body, sizeof body, "{\"id\":\"%s\"}", esc);
  net_req(HTTP_POST, CTL_URL "/v1/library/remove", body, lib_done, (void *)_("Tolto dalla libreria"));
  view = 0; lv = LV_CONTENT;
}

// ------------------------------------------------------------------ disegno --
static void search_store(void) {
  char buf[48]; snprintf(buf, sizeof buf, "%s", q);
  if (edit_text(tab == 0 ? _("Cerca un homebrew") : _("Cerca nella libreria"), buf, sizeof buf, 0)) {
    snprintf(q, sizeof q, "%s", buf);
    sel = 0;
    if (tab == 0) load_apps(); else load_items();
  }
}

static void draw_stars(int x, int y, int size, float rating, int alpha) {
  for (int i = 0; i < 5; i++) {
    int on = (i + 1) <= (int)(rating + 0.5f);
    draw_icon(IC_STAR, x + i * (size + 4) + size / 2, y + size / 2, size, on ? C_WARN : C_DIM, on ? alpha : alpha * 45 / 100);
  }
}

static const char *kind_label(int k) { return k == 1 ? "PKG" : k == 2 ? "ZIP" : k == 3 ? "ELF" : k == 4 ? _("CARTELLA") : "FILE"; }
static void human_size(long b, char *out, size_t n) {
  if (b <= 0) out[0] = 0;
  else if (b >= 1073741824L) snprintf(out, n, _("%.1f GB"), b / 1073741824.0);
  else snprintf(out, n, _("%.0f MB"), b / 1048576.0);
}

static int grid_count(void) { return tab == 0 ? nsapp : nlitem; }

// ------------------------------------------------------------- vista elenco --
// Livelli di fuoco, dall'alto: schede (Homebrew, Libreria; anche con L1/R1),
// barra (Cerca, filtri, Pubblica…), contenuto (banner e scaffali, oppure
// griglia). ○ risale di un livello e chiude lo Store solo da schede o barra.
static int bar_sel;
#define BAR_H 60
#define HERO_H 430
#define HERO_N 5
#define SH_TILE 220
#define SH_GAP 30
#define SHELF_H 390
#define G_TILE 240
#define G_GAP 34
#define G_COLS 6
#define G_ROWH (G_TILE + 110)
#define LIST_TOP (ST_TOP + 6)
static float list_scroll, list_scroll_t;
static int hero_i; static float hero_fade = 1; static Uint32 hero_at;
typedef struct { char title[48]; int idx[MAX_SAPP]; int n; int sel; float sx; int kind, ic; } Shelf;   // kind 1 = creatori
static Shelf shelf[12]; static int nshelf;
static int row = -1;              // scaffale a fuoco; -1 = banner

static int shelves_mode(void) { return tab == 0 && sort == 0 && !q[0] && !mine_filter && !wish_filter && nsapp > 0; }
static int hero_count(void) { return nsapp < HERO_N ? nsapp : HERO_N; }
static int sy(int cy) { return LIST_TOP + cy - (int)list_scroll; }        // contenuto → schermo

static const char *cat_label(const char *c) {
  return !strcmp(c, "gioco") ? _("Gioco") : !strcmp(c, "emulatore") ? _("Emulatore") : !strcmp(c, "utility") ? _("Strumento")
       : !strcmp(c, "tema") ? _("Tema") : !strcmp(c, "app") ? _("App") : !strcmp(c, "trucchi") ? _("Trucchi") : _("Altro");
}

static void build_shelves(void) {
  static const struct { const char *t, *c; int ps4; } D[] = {
    { N_("Giochi"), "gioco", 0 }, { N_("Emulatori"), "emulatore", 0 }, { N_("App e intrattenimento"), "app", 0 },
    { N_("Strumenti e payload"), "utility", 0 }, { N_("Trucchi e mod"), "trucchi", 0 }, { N_("Temi e altro"), "", 0 }, { N_("Per PS4"), "", 1 } };   // titoli tradotti quando si disegnano
  nshelf = 0;
  // scaffali social: amici, tendenze, lista dei desideri, creatori
  {
    Shelf *s = &shelf[nshelf]; s->n = 0; s->kind = 0; s->ic = IC_FRIENDS; snprintf(s->title, sizeof s->title, "%s", N_("Popolari tra i tuoi amici"));
    for (int i = 0; i < nsapp; i++) if (sapp[i].friends_count) s->idx[s->n++] = i;
    for (int i = 0; i < s->n; i++) for (int k = i + 1; k < s->n; k++) if (sapp[s->idx[k]].friends_count > sapp[s->idx[i]].friends_count) { int t = s->idx[i]; s->idx[i] = s->idx[k]; s->idx[k] = t; }
    if (s->n) { if (s->sel >= s->n) s->sel = s->n - 1; nshelf++; }
    s = &shelf[nshelf]; s->n = 0; s->kind = 0; s->ic = IC_FIRE; snprintf(s->title, sizeof s->title, "%s", N_("Di tendenza"));
    for (int i = 0; i < nsapp; i++) if (sapp[i].downloads > 0 || sapp[i].likes > 0) s->idx[s->n++] = i;
    for (int i = 0; i < s->n; i++) for (int k = i + 1; k < s->n; k++) {
      long a = sapp[s->idx[i]].downloads + 5L * sapp[s->idx[i]].likes, b = sapp[s->idx[k]].downloads + 5L * sapp[s->idx[k]].likes;
      if (b > a) { int t = s->idx[i]; s->idx[i] = s->idx[k]; s->idx[k] = t; } }
    if (s->n > 15) s->n = 15;
    if (s->n >= 3) { if (s->sel >= s->n) s->sel = s->n - 1; nshelf++; }
    s = &shelf[nshelf]; s->n = 0; s->kind = 0; s->ic = IC_HEART; snprintf(s->title, sizeof s->title, "%s", N_("La tua lista dei desideri"));
    for (int i = 0; i < nsapp; i++) if (sapp[i].wished) s->idx[s->n++] = i;
    if (s->n) { if (s->sel >= s->n) s->sel = s->n - 1; nshelf++; }
    if (ncre) { s = &shelf[nshelf]; s->n = ncre; s->kind = 1; s->ic = IC_CROWN; snprintf(s->title, sizeof s->title, "%s", N_("Creatori in evidenza")); if (s->sel >= s->n) s->sel = s->n - 1; nshelf++; }
  }
  for (unsigned d = 0; d < sizeof D / sizeof D[0]; d++) {
    Shelf *s = &shelf[nshelf];
    s->n = 0; s->kind = 0; s->ic = -1; snprintf(s->title, sizeof s->title, "%s", D[d].t);
    for (int i = 0; i < nsapp; i++) {
      int ps4 = !strcmp(sapp[i].platform, "PS4");
      const char *c = sapp[i].category;
      int known = !strcmp(c, "gioco") || !strcmp(c, "emulatore") || !strcmp(c, "app") || !strcmp(c, "utility") || !strcmp(c, "trucchi");
      if (D[d].ps4) { if (!ps4) continue; }
      else if (ps4 || (D[d].c[0] ? strcmp(c, D[d].c) != 0 : known)) continue;
      s->idx[s->n++] = i;
    }
    if (s->n) { if (s->sel >= s->n) s->sel = s->n - 1; nshelf++; }
  }
  if (row >= nshelf) row = nshelf - 1;
}

// -------------------------------------------------------------------- barra --
enum { A_SEARCH, A_CLEARQ, A_SORT0, A_SORT1, A_SORT2, A_SORT3, A_WISH, A_MINE, A_PUBLISH, A_SYNC, A_SETURL, A_ADDGAME, A_PHONE, A_UNLINK, A_JSONHELP, A_PKGS };
typedef struct { const char *l; int ic; int act; int on; } BarItem;
static int bar_items(BarItem *it) {
  static char qlabel[64];
  int n = 0;   // al massimo 12 voci
  it[n++] = (BarItem){ _("Cerca"), IC_SEARCH, A_SEARCH, 0 };
  if (q[0]) { snprintf(qlabel, sizeof qlabel, "\"%s\"", q); it[n++] = (BarItem){ qlabel, IC_CLOSE, A_CLEARQ, 1 }; }
  if (tab == 0) {
    it[n++] = (BarItem){ _("Scopri"), -1, A_SORT0, sort == 0 && !mine_filter && !wish_filter && !q[0] };
    it[n++] = (BarItem){ _("Di tendenza"), IC_FIRE, A_SORT3, sort == 3 && !mine_filter && !wish_filter };
    it[n++] = (BarItem){ _("Più votati"), -1, A_SORT1, sort == 1 && !mine_filter && !wish_filter };
    it[n++] = (BarItem){ _("Più scaricati"), -1, A_SORT2, sort == 2 && !mine_filter && !wish_filter };
    it[n++] = (BarItem){ _("Desideri"), IC_HEART, A_WISH, wish_filter };
    it[n++] = (BarItem){ _("Le mie"), IC_USER, A_MINE, mine_filter };
    it[n++] = (BarItem){ _("Pubblica"), IC_PLUS, A_PUBLISH, 0 };
  } else {
    it[n++] = (BarItem){ _("Aggiungi un gioco"), IC_PLUS, A_ADDGAME, 0 };
    it[n++] = (BarItem){ _("Dal telefono o dal PC"), IC_CLOUD, A_PHONE, 0 };
    it[n++] = (BarItem){ _("Come funziona il JSON"), IC_IDEA, A_JSONHELP, 0 };
    it[n++] = (BarItem){ _("PKG da USB"), IC_USB, A_PKGS, 0 };
    if (src_set) {
      it[n++] = (BarItem){ _("Aggiorna il JSON"), IC_RELOAD, A_SYNC, 0 };
      it[n++] = (BarItem){ _("Scollega il JSON"), IC_CLOSE, A_UNLINK, 0 };
    } else it[n++] = (BarItem){ _("Collega un JSON"), IC_NEWS, A_SETURL, 0 };
  }
  return n;
}
static void bar_do(int act) {
  switch (act) {
    case A_SEARCH: search_store(); break;
    case A_CLEARQ: q[0] = 0; sel = 0; row = -1; if (tab == 0) load_apps(); else load_items(); break;
    case A_SORT0: case A_SORT1: case A_SORT2: case A_SORT3: sort = act - A_SORT0; mine_filter = 0; wish_filter = 0; q[0] = 0; sel = 0; row = -1; load_apps(); break;
    case A_WISH: wish_filter = !wish_filter; mine_filter = 0; sel = 0; load_apps(); break;
    case A_MINE: mine_filter = !mine_filter; wish_filter = 0; sel = 0; load_apps(); break;
    case A_PUBLISH: publish_open_new(); break;
    case A_SYNC: sync_library(); break;
    case A_SETURL: set_library_url(); break;
    case A_ADDGAME: lib_edit(NULL); break;
    case A_PHONE: remote_open(); break;
    case A_JSONHELP: view = 4; break;
    case A_PKGS: pkgs_open(); break;
    case A_UNLINK: confirm_open(_("Scollegare il JSON? I giochi arrivati da lì spariscono, quelli aggiunti a mano restano."), _("Scollega"), unlink_yes, NULL); break;
  }
}
static void switch_tab(int t) {
  if (t == tab) return;
  tab = t; sel = 0; row = -1; bar_sel = 0; q[0] = 0; list_scroll = list_scroll_t = 0;
  if (tab == 0) load_apps(); else load_items();
}

// ----------------------------------------------------------------- elementi --
static int chip(int x, int y, const char *t, Col fg, int a) {
  TTF_Font *f = font(W_MED, 21);
  int w = text_w(f, t) + 28;
  fill_rrect(x, y, w, 36, 18, C_WHITE, a * 14 / 100);
  draw_text(f, t, x + 14, y + 18 - TTF_FontHeight(f) / 2, fg, a, AL_L);
  return w;
}
static void l_pill(int x, int y, const char *t, int a) {      // "L1" / "R1"
  fill_rrect(x, y, 48, 30, 9, C_WHITE, a * 18 / 100);
  draw_text(font(W_BOLD, 18), t, x + 24, y + 15 - TTF_FontHeight(font(W_BOLD, 18)) / 2, C_TXT, a, AL_C);
}

// testata fissa: logo, schede (L1/R1), velo che si scurisce scorrendo
static void draw_header(int a) {
  float k = view == 0 ? clampf(list_scroll / 120.0f, 0, 1) : 1;
  fill_rect(0, 0, SCREEN_W, ST_TOP - 8, ST_BG, (int)(a * (0.55f + 0.4f * k)));
  grad_v(0, ST_TOP - 8, SCREEN_W, 40, ST_BG, (int)(a * (0.55f + 0.4f * k)), ST_BG, 0);
  draw_icon(IC_STORE, ST_X + 18, 74, 46, C_ACC2, a);
  draw_text(font(W_LIGHT, 38), _("Store"), ST_X + 56, 52, C_WHITE, a, AL_L);
  int x = ST_X + 230, y = 46;
  int tabs_on = view == 0;
  if (tabs_on) { l_pill(x, y + 12, "L1", a); x += 66; }
  const char *names[2] = { _("Homebrew"), _("Libreria") };
  for (int i = 0; i < 2; i++) {
    TTF_Font *f = font(tab == i ? W_BOLD : W_MED, 30);
    int w = text_w(f, names[i]) + 52, h = 54;
    int foc = tabs_on && lv == LV_TABS && tab == i;
    if (foc) { fill_rrect(x, y, w, h, 27, C_WHITE, a); stroke_rrect(x - 4, y - 4, w + 8, h + 8, 31, 3, C_WHITE, a * 60 / 100); }
    else if (tab == i) fill_rrect(x, y, w, h, 27, C_WHITE, a * 16 / 100);
    draw_text(f, names[i], x + w / 2, y + h / 2 - TTF_FontHeight(f) / 2, foc ? ST_BG : tab == i ? C_WHITE : C_DIM, a, AL_C);
    x += w + 12;
  }
  if (tabs_on) l_pill(x + 6, y + 12, "R1", a);
}

// riga dei filtri, in cima al contenuto (scorre con la pagina)
static void draw_bar(int a) {
  BarItem it[12]; int n = bar_items(it);
  if (bar_sel >= n) bar_sel = n - 1;
  int x = ST_X, y = sy(0);
  if (y + BAR_H < LIST_TOP - 20) return;
  for (int i = 0; i < n; i++) {
    TTF_Font *f = font(W_MED, 25);
    int w = (it[i].ic >= 0 ? 36 : 0) + text_w(f, it[i].l) + 48, h = BAR_H - 6;
    int foc = lv == LV_BAR && bar_sel == i;
    Col bg = foc ? C_WHITE : it[i].on ? C_ACC : RGB(255, 255, 255);
    int ba = foc ? a : it[i].on ? a * 90 / 100 : a * 12 / 100;
    fill_rrect(x, y, w, h, h / 2, bg, ba);
    if (foc) stroke_rrect(x - 4, y - 4, w + 8, h + 8, h / 2 + 4, 3, C_WHITE, a * 60 / 100);
    Col fg = foc ? ST_BG : C_WHITE;
    int cx = x + 24;
    if (it[i].ic >= 0) { draw_icon(it[i].ic, cx + 10, y + h / 2, 24, fg, a); cx += 36; }
    draw_text(f, it[i].l, cx, y + h / 2 - TTF_FontHeight(f) / 2, fg, a, AL_L);
    x += w + 14;
  }
}

// ------------------------------------------------------------------ tessere --
typedef struct { SDL_Texture *cov; int cs; float *appear, *foc; const char *title; char sub[96]; int kind, inst; } TileV;
static void tile_info(int is_lib, int i, TileV *t) {
  memset(t, 0, sizeof *t);
  if (!is_lib) {
    SApp *s = &sapp[i]; req_app_cover(s);
    t->cov = s->cover; t->cs = s->cover_state; t->appear = &s->appear; t->foc = &s->foc; t->title = s->title; t->kind = s->kind;
    t->inst = installed_index(s->title_id, s->title) >= 0;
    if (s->ratings) snprintf(t->sub, sizeof t->sub, "%s \xC2\xB7 %s   \xE2\x98\x85 %.1f", cat_label(s->category), s->platform[0] ? s->platform : "PS5", s->rating);
    else snprintf(t->sub, sizeof t->sub, "%s \xC2\xB7 %s", cat_label(s->category), s->platform[0] ? s->platform : "PS5");
  } else {
    LItem *s = &litem[i]; req_lib_cover(s);
    t->cov = s->cover; t->cs = s->cover_state; t->appear = &s->appear; t->foc = &s->foc; t->title = s->title; t->kind = s->kind;
    t->inst = installed_index(s->title_id, s->title) >= 0;
    char sz[16]; human_size(s->size, sz, sizeof sz);
    snprintf(t->sub, sizeof t->sub, "%s%s%s", s->platform[0] ? s->platform : _("Gioco"), sz[0] ? " \xC2\xB7 " : "", sz);
  }
}

static void draw_tile(int is_lib, int i, int x, int y, int S, int focused, int a) {
  TileV t; tile_info(is_lib, i, &t);
  (void)0;
  *t.foc = approach(*t.foc, focused ? 1.0f : 0.0f, 14.0f);
  float f = *t.foc;
  int grow = (int)(S * 0.08f * f);
  int X = x - grow / 2, Y = y - grow, SS = S + grow;                // cresce verso l'alto, il testo resta fermo
  if (f > 0.02f) shadow_rrect(X, Y, SS, SS, 24, 34, (int)(a * 0.8f * f));
  if (t.cov && t.cs == 2) { *t.appear = approach(*t.appear, 1, 8.0f); draw_tex(t.cov, X, Y, SS, SS, (int)(a * *t.appear)); }
  else {
    fill_rrect(X, Y, SS, SS, 22, RGB(28, 34, 52), a);
    draw_icon(is_lib ? IC_GAMEPAD : IC_STORE, X + SS / 2, Y + SS / 2, SS / 3, C_FAINT, a);
  }
  // tipo di file in basso a sinistra, segno di installato in alto a destra
  const char *kl = kind_label(t.kind);
  int kw = text_w(font(W_BOLD, 16), kl) + 22;
  fill_rrect(X + 12, Y + SS - 44, kw, 30, 15, C_BLACK, a * 60 / 100);
  draw_text(font(W_BOLD, 16), kl, X + 12 + kw / 2, Y + SS - 29 - TTF_FontHeight(font(W_BOLD, 16)) / 2,
            t.kind == 1 ? C_OK : t.kind == 2 ? C_ACC2 : t.kind == 3 ? C_WARN : C_DIM, a, AL_C);
  if (t.inst) { fill_circle(X + SS - 26, Y + 26, 17, RGB(0, 150, 90), a); draw_icon(IC_CHECK, X + SS - 26, Y + 26, 20, C_WHITE, a); }
  if (!is_lib) {
    SApp *sa = &sapp[i];
    // cuore: nella lista dei desideri
    if (sa->wished) { int hx = X + 26, hy = Y + 26; fill_circle(hx, hy, 17, RGB(10, 12, 20), a * 70 / 100); draw_icon(IC_HEART, hx, hy, 20, RGB(255, 90, 120), a); }
    // amici che l'hanno installato: avatar sovrapposti in basso a destra
    if (sa->nfr) {
      int r = 34, ax = X + SS - 14 - r / 2;
      for (int k = sa->nfr - 1; k >= 0; k--) { fill_circle(ax - k * 22, Y + SS - 30, r / 2 + 3, RGB(10, 12, 20), a); draw_avatar(sa->fr_oid[k], sa->fr_av[k], ax - k * 22, Y + SS - 30, r, a); }
      if (sa->friends_count > sa->nfr) { char c[8]; snprintf(c, sizeof c, "+%d", sa->friends_count - sa->nfr); draw_text(font(W_BOLD, 16), c, ax - sa->nfr * 22 - 4, Y + SS - 40, C_WHITE, a, AL_R); }
    }
  }
  if (focused) stroke_rrect(X - 6, Y - 6, SS + 12, SS + 12, 28, 4, C_WHITE, (int)(a * (0.75f + 0.25f * sinf((float)g_time * 3.2f))));
  draw_text_fit(font(focused ? W_BOLD : W_MED, 24), t.title, x, y + S + 16, S, focused ? C_WHITE : C_TXT, a, AL_L);
  draw_text_fit(font(W_REG, 20), t.sub, x, y + S + 50, S, C_DIM, a, AL_L);
}

// ------------------------------------------------------------------- banner --
static void draw_hero(int x, int y, int w, int h, int focused, int a) {
  int n = hero_count(); if (!n) return;
  if (hero_i >= n) hero_i = 0;
  if (!focused && SDL_GetTicks() - hero_at > 7000) { hero_i = (hero_i + 1) % n; hero_at = SDL_GetTicks(); hero_fade = 0; }
  hero_fade = approach(hero_fade, 1, 4.0f);
  SApp *s = &sapp[hero_i];
  req_app_cover(s);
  Col base = s->avg.r + s->avg.g + s->avg.b > 40 ? s->avg : RGB(40, 70, 150), dark = RGB(14, 16, 26);
  int fa = (int)(a * (0.35f + 0.65f * hero_fade));
  if (focused) shadow_rrect(x, y, w, h, 32, 44, a * 75 / 100);
  fill_rrect(x, y, w, h, 32, dark, a);
  grad_h(x + 16, y, w - 32, h, mix(dark, base, 0.12f), fa, mix(dark, base, 0.7f), fa);
  // la copertina, grande e velata, fa da fondale a destra
  if (s->cover && s->cover_state == 2) {
    int bw = h + 160, bx = x + w - bw - 16;
    draw_tex(s->cover, bx, y + 8, bw, h - 16, fa * 22 / 100);
    grad_h(bx, y + 8, bw / 2, h - 16, mix(dark, base, 0.55f), fa, mix(dark, base, 0.55f), 0);
  }
  glow(x + w - 330, y + h / 2, 440, base, fa * 50 / 100);
  glow(x + 200, y + 60, 260, C_WHITE, fa * 5 / 100);
  if (focused) stroke_rrect(x - 5, y - 5, w + 10, h + 10, 37, 4, C_WHITE, a);
  int S = h - 96, ix = x + w - S - 96, iy = y + 48 + (int)(5 * sinf((float)g_time * 1.3f));
  if (s->cover && s->cover_state == 2) { shadow_rrect(ix, iy, S, S, 26, 36, fa * 70 / 100); draw_tex(s->cover, ix, iy, S, S, fa); }
  else { fill_rrect(ix, iy, S, S, 26, RGB(30, 36, 54), fa); draw_icon(IC_STORE, ix + S / 2, iy + S / 2, 110, C_DIM, fa); }
  int tx = x + 72, tw = ix - tx - 60;
  draw_text(font(W_BOLD, 21), _("IN EVIDENZA"), tx, y + 52, C_ACC2, fa, AL_L);
  draw_text_fit(font(W_LIGHT, 72), s->title, tx, y + 84, tw, C_WHITE, fa, AL_L);
  draw_text_wrap(font(W_REG, 29), s->tagline, tx, y + 178, tw, 2, 38, RGB(222, 228, 242), fa);
  int cx = tx, cy = y + 268;
  cx += chip(cx, cy, s->platform[0] ? s->platform : "PS5", C_WHITE, fa) + 10;
  cx += chip(cx, cy, cat_label(s->category), C_WHITE, fa) + 10;
  cx += chip(cx, cy, kind_label(s->kind), C_WHITE, fa) + 18;
  if (s->ratings) { draw_stars(cx, cy + 7, 22, s->rating, fa); cx += 5 * 26 + 10; char rc[24]; snprintf(rc, sizeof rc, "%.1f", s->rating); cx += draw_text(font(W_MED, 22), rc, cx, cy + 6, C_WHITE, fa, AL_L) + 18; }
  if (s->nfr) {
    for (int k = 0; k < s->nfr; k++) draw_avatar(s->fr_oid[k], s->fr_av[k], cx + 16 + k * 24, cy + 18, 32, fa);
    char fm[96]; snprintf(fm, sizeof fm, s->friends_count == 1 ? _("%d amico lo usa") : _("%d amici lo usano"), s->friends_count);
    draw_text(font(W_REG, 21), fm, cx + 16 + s->nfr * 24 + 14, cy + 7, RGB(222, 228, 242), fa, AL_L);
  }
  int inst = installed_index(s->title_id, s->title) >= 0;
  int bw = 220, bx = tx, by = y + h - 98;
  fill_rrect(bx, by, bw, 60, 30, C_WHITE, focused ? a : a * 22 / 100);
  Col bf = focused ? ST_BG : C_WHITE;
  draw_icon(inst ? IC_PLAY : IC_ARROW_R, bx + 38, by + 30, 26, bf, a);
  draw_text(font(W_BOLD, 26), inst ? _("Avvia") : _("Scopri"), bx + 66, by + 30 - TTF_FontHeight(font(W_BOLD, 26)) / 2, bf, a, AL_L);
  for (int i = 0; i < n; i++) {             // pallini della rotazione
    int dx = bx + bw + 40 + i * 26;
    if (i == hero_i) fill_rrect(dx - 4, by + 24, 22, 12, 6, C_WHITE, a); else fill_circle(dx + 5, by + 30, 5, C_WHITE, a * 40 / 100);
  }
  if (focused) {
    draw_icon(IC_BACK, x + 30, y + h / 2, 34, C_WHITE, a * 70 / 100);
    draw_icon(IC_FWD, x + w - 30, y + h / 2, 34, C_WHITE, a * 70 / 100);
  }
}

// ------------------------------------------------------------------- pagine --
static void amb_toward(Col c) { if (c.r + c.g + c.b > 30) g_amb = mix(g_amb, c, clampf(g_dt * 3.0f, 0, 1)); }

static void draw_list_empty(int a) {
  int y = sy(BAR_H + 80);
  if (loading) { draw_spinner(SCREEN_W / 2, y + 120, 22, a); return; }
  draw_icon(tab == 0 ? IC_STORE : IC_GAMEPAD, SCREEN_W / 2, y + 80, 90, C_FAINT, a);
  draw_text(font(W_MED, 32), q[0] ? _("Nessun risultato") : tab == 0 ? (mine_filter ? _("Non hai ancora pubblicato nulla") : _("Ancora nessun homebrew")) : _("La tua libreria è vuota"),
            SCREEN_W / 2, y + 160, C_TXT, a, AL_C);
  draw_text(font(W_REG, 25), q[0] ? _("Prova un'altra parola, o cancella la ricerca dalla barra in alto") : tab == 0 ? _("Usa Pubblica nella barra in alto") : _("Usa Aggiorna nella barra in alto"),
            SCREEN_W / 2, y + 208, C_DIM, a, AL_C);
}

// libreria vuota o servizio assente: cosa è e come si riempie
static void draw_link_library(int a) {
  int w = 1240, h = 430, x = (SCREEN_W - w) / 2, y = sy(BAR_H + 50);
  fill_rrect(x, y, w, h, 30, RGB(24, 28, 44), a);
  draw_icon(IC_GAMEPAD, x + 120, y + 110, 96, C_ACC2, a);
  if (lib_down) {
    draw_text(font(W_MED, 36), _("La libreria è sulla console"), x + 220, y + 50, C_WHITE, a, AL_L);
    draw_text_wrap(font(W_REG, 25), _("La custodisce il servizio di Omega, che non risponde: aggiorna Omega o riavvia la console."), x + 220, y + 104, w - 270, 3, 36, C_DIM, a);
    return;
  }
  draw_text(font(W_MED, 36), _("La mia libreria"), x + 220, y + 44, C_WHITE, a, AL_L);
  draw_text_wrap(font(W_REG, 25), _("I backup dei giochi che possiedi, con copertina e link: li installi da qui con un tasto. Restano solo sulla tua console."),
                 x + 220, y + 96, w - 270, 2, 34, C_DIM, a);
  const char *how[3] = { _("Dal telefono: Dal telefono nella barra qui sopra, poi La mia libreria › Aggiungi un gioco. Scrivere link lunghi è più comodo."),
                         _("Dalla console: Aggiungi un gioco nella barra, con titolo, link e copertina."),
                         _("Da un JSON: Collega un JSON con l'elenco dei tuoi giochi, oppure importalo dal telefono.") };
  for (int i = 0; i < 3; i++) {
    int yy = y + 190 + i * 70;
    fill_circle(x + 246, yy + 18, 18, C_ACC, a);
    char nb[4]; snprintf(nb, sizeof nb, "%d", i + 1);
    draw_text(font(W_BOLD, 22), nb, x + 246, yy + 18 - TTF_FontHeight(font(W_BOLD, 22)) / 2, C_WHITE, a, AL_C);
    draw_text_wrap(font(W_REG, 24), how[i], x + 284, yy + 2, w - 330, 2, 30, C_TXT, a);
  }
  draw_text_fit(font(W_REG, 21), _("Solo backup di giochi che possiedi, a uso personale: Omega non ospita né condivide questi file."), x + 220, y + h - 44, w - 270, C_FAINT, a, AL_L);
}

static void draw_shelves(int a) {
  int content = BAR_H + 30 + HERO_H + 60 + nshelf * SHELF_H;
  // il banner sta in alto; lo scaffale a fuoco scorre nella parte alta dello schermo
  if (lv != LV_CONTENT || row < 0) list_scroll_t = 0;
  else list_scroll_t = clampf((float)(BAR_H + 30 + HERO_H + 60 + row * SHELF_H - 70), 0, (float)(content - (SCREEN_H - LIST_TOP) + 40));
  draw_hero(ST_X, sy(BAR_H + 30), SCREEN_W - 2 * ST_X, HERO_H, lv == LV_CONTENT && row < 0, a);
  if (lv == LV_CONTENT && row < 0) amb_toward(sapp[hero_i < nsapp ? hero_i : 0].avg);
  for (int k = 0; k < nshelf; k++) {
    Shelf *s = &shelf[k];
    int y = sy(BAR_H + 30 + HERO_H + 60 + k * SHELF_H);
    if (y > SCREEN_H || y + SHELF_H < LIST_TOP) continue;
    int on = lv == LV_CONTENT && row == k;
    char t[128]; snprintf(t, sizeof t, "%s", _(s->title));
    int tx0 = ST_X;
    if (s->ic >= 0) { draw_icon(s->ic, ST_X + 18, y + 20, 32, s->ic == IC_HEART ? RGB(255, 90, 120) : s->ic == IC_FIRE ? RGB(255, 150, 60) : s->ic == IC_CROWN ? C_WARN : C_ACC2, a); tx0 += 48; }
    int tw = draw_text(font(on ? W_BOLD : W_MED, 32), t, tx0, y, on ? C_WHITE : C_TXT, a, AL_L);
    char c[16]; snprintf(c, sizeof c, "%d", s->n);
    draw_text(font(W_REG, 24), c, tx0 + tw + 16, y + 6, C_FAINT, a, AL_L);
    if (s->kind == 1) {
      // creatori: avatar grandi con quanti homebrew e installazioni
      int step = 250; float target = s->sel > 4 ? (float)((s->sel - 4) * step) : 0;
      s->sx = approach(s->sx, target, 12.0f);
      for (int i = 0; i < s->n; i++) {
        int cx = ST_X + i * step - (int)s->sx + 110, cy = y + 66 + 100;
        if (cx < -120 || cx > SCREEN_W + 120) continue;
        int f = on && s->sel == i;
        fill_rrect(cx - 110, cy - 100, 220, 290, 28, C_WHITE, a * (f ? 14 : 6) / 100);
        if (f) stroke_rrect(cx - 114, cy - 104, 228, 298, 32, 4, C_WHITE, a);
        draw_avatar(cre[i].oid, cre[i].avatar, cx, cy, 120, a);
        if (i < 3) { fill_circle(cx + 46, cy - 46, 20, i == 0 ? C_WARN : i == 1 ? RGB(200, 206, 220) : RGB(205, 127, 50), a); draw_icon(IC_CROWN, cx + 46, cy - 46, 22, RGB(20, 20, 30), a); }
        draw_text_fit(font(W_MED, 24), cre[i].oid, cx, cy + 74, 200, C_WHITE, a, AL_C);
        char st[64]; snprintf(st, sizeof st, _("%d app \xC2\xB7 %ld installazioni"), cre[i].apps, cre[i].downloads);
        draw_text_fit(font(W_REG, 18), st, cx, cy + 108, 200, C_DIM, a, AL_C);
      }
      continue;
    }
    int step = SH_TILE + SH_GAP, visible = (SCREEN_W - 2 * ST_X + SH_GAP) / step;
    float target = s->sel > visible - 2 ? (float)((s->sel - (visible - 2)) * step) : 0;
    float maxs = (float)((s->n - visible) * step); if (maxs < 0) maxs = 0; if (target > maxs) target = maxs;
    s->sx = approach(s->sx, target, 12.0f);
    int ty = y + 66;
    for (int i = 0; i < s->n; i++) {
      int x = ST_X + i * step - (int)s->sx;
      if (x > SCREEN_W || x + SH_TILE < 0) continue;
      draw_tile(0, s->idx[i], x, ty, SH_TILE, on && s->sel == i, a);
    }
    if (on && s->kind == 0) amb_toward(sapp[s->idx[s->sel]].avg);
  }
}

static void draw_grid(int a) {
  int n = grid_count();
  int gw = G_COLS * G_TILE + (G_COLS - 1) * G_GAP, x0 = (SCREEN_W - gw) / 2;
  int rows = (n + G_COLS - 1) / G_COLS, top = BAR_H + 60;
  int content = top + rows * G_ROWH;
  if (sel >= n) sel = n - 1;
  if (sel < 0) sel = 0;
  if (lv != LV_CONTENT) list_scroll_t = 0;
  else { int rt = top + (sel / G_COLS) * G_ROWH; list_scroll_t = clampf((float)(rt - 100), 0, (float)(content - (SCREEN_H - LIST_TOP) + 60)); }
  for (int i = 0; i < n; i++) {
    int x = x0 + (i % G_COLS) * (G_TILE + G_GAP), y = sy(top + (i / G_COLS) * G_ROWH);
    if (y > SCREEN_H || y + G_ROWH < LIST_TOP) continue;
    draw_tile(tab == 1, i, x, y, G_TILE, lv == LV_CONTENT && sel == i, a);
  }
  if (lv == LV_CONTENT && n) amb_toward(tab == 0 ? sapp[sel].avg : litem[sel].avg);
  if (content > SCREEN_H - LIST_TOP) {
    int vh = SCREEN_H - LIST_TOP - 120, bh = vh * (SCREEN_H - LIST_TOP) / content; if (bh < 50) bh = 50;
    int by = LIST_TOP + 20 + (int)((vh - bh) * clampf(list_scroll / (float)(content - (SCREEN_H - LIST_TOP)), 0, 1));
    fill_rrect(SCREEN_W - 30, by, 6, bh, 3, C_WHITE, a * 30 / 100);
  }
}

static void draw_list(int a) {
  list_scroll = approach(list_scroll, list_scroll_t, 11.0f);
  SDL_Rect clip = { 0, LIST_TOP - 30, SCREEN_W, SCREEN_H - LIST_TOP + 30 }; SDL_RenderSetClipRect(R, &clip);
  draw_bar(a);
  if (tab == 1 && (lib_down || (!nlitem && !q[0] && !loading))) draw_link_library(a);
  else if (!grid_count()) draw_list_empty(a);
  else if (shelves_mode()) draw_shelves(a);
  else draw_grid(a);
  SDL_RenderSetClipRect(R, NULL);
}

static void list_input(int b) {
  if (b == B_L1 || b == B_R1) { switch_tab(b == B_R1 ? 1 : 0); return; }
  if (b == B_TRI) { search_store(); return; }
  if (lv == LV_TABS) {
    if (b == B_LEFT || b == B_RIGHT) switch_tab(b == B_RIGHT ? 1 : 0);
    else if (b == B_DOWN || b == B_X) lv = LV_BAR;
    else if (b == B_O) ov_pop();
    return;
  }
  if (lv == LV_BAR) {
    BarItem it[12]; int n = bar_items(it);
    if (bar_sel >= n) bar_sel = n - 1;
    if (b == B_LEFT && bar_sel > 0) bar_sel--;
    else if (b == B_RIGHT && bar_sel < n - 1) bar_sel++;
    else if (b == B_UP) lv = LV_TABS;
    else if (b == B_DOWN) { lv = LV_CONTENT; row = -1; }
    else if (b == B_X) bar_do(it[bar_sel].act);
    else if (b == B_O) ov_pop();
    return;
  }
  // contenuto: ○ risale alla barra (non chiude lo Store)
  if (b == B_O) { lv = LV_BAR; return; }
  if (tab == 1 && (lib_down || !nlitem) && !q[0]) { if (b == B_UP || b == B_X) lv = LV_BAR; return; }
  int n = grid_count();
  if (!n) { if (b == B_UP) lv = LV_BAR; return; }
  if (shelves_mode()) {
    int hn = hero_count();
    if (row < 0) {
      if (b == B_UP) lv = LV_BAR;
      else if (b == B_DOWN && nshelf) row = 0;
      else if (b == B_LEFT || b == B_RIGHT) { hero_i = (hero_i + (b == B_RIGHT ? 1 : hn - 1)) % hn; hero_fade = 0; hero_at = SDL_GetTicks(); }
      else if (b == B_X) open_detail(sapp[hero_i].id, 0);
      return;
    }
    Shelf *s = &shelf[row];
    if (b == B_UP) row--;
    else if (b == B_DOWN && row < nshelf - 1) row++;
    else if (b == B_LEFT && s->sel > 0) s->sel--;
    else if (b == B_RIGHT && s->sel < s->n - 1) s->sel++;
    else if (b == B_X) { if (s->kind == 1) profile_open(cre[s->sel].oid); else open_detail(sapp[s->idx[s->sel]].id, 0); }
    return;
  }
  if (sel < 0) sel = 0;
  if (sel >= n) sel = n - 1;
  if (b == B_UP) { if (sel < G_COLS) lv = LV_BAR; else sel -= G_COLS; }
  else if (b == B_DOWN) { if (sel + G_COLS < n) sel += G_COLS; else if (sel / G_COLS < (n - 1) / G_COLS) sel = n - 1; }
  else if (b == B_LEFT) { if (sel % G_COLS) sel--; }
  else if (b == B_RIGHT) { if (sel % G_COLS != G_COLS - 1 && sel + 1 < n) sel++; }
  else if (b == B_X) { if (tab == 0) open_detail(sapp[sel].id, 0); else open_detail(litem[sel].id, 1); }
}

// dettaglio
static int det_action_count(void) { return det_is_lib ? 1 : 6; }   // Installa; per gli homebrew anche voti, stelle, desideri e consiglia

static void info_row(int x, int y, int w, const char *k, const char *v, int a) {
  if (!v || !v[0]) return;
  draw_text(font(W_REG, 22), k, x, y, C_DIM, a, AL_L);
  draw_text_fit(font(W_MED, 22), v, x + w, y, w - 190, C_TXT, a, AL_R);
}

static void draw_detail(int a) {
  if (det_loading && !det_loaded) { draw_spinner(SCREEN_W / 2, 480, 22, a); return; }
  req_det_cover();
  amb_toward(d_cover_avg);
  det_scroll = approach(det_scroll, det_scroll_t, 12.0f);
  int top = ST_TOP + 16 - (int)det_scroll;          // y a schermo dell'inizio del contenuto
  SDL_Rect clip = { 0, ST_TOP - 30, SCREEN_W, SCREEN_H - ST_TOP + 30 }; SDL_RenderSetClipRect(R, &clip);
  int lx = ST_X, y = top;
  int S = 320;
  if (d_cover && d_cover_st == 2) { shadow_rrect(lx, y, S, S, 28, 40, a * 70 / 100); draw_tex(d_cover, lx, y, S, S, a); }
  else { fill_rrect(lx, y, S, S, 28, RGB(28, 34, 52), a); draw_icon(det_is_lib ? IC_GAMEPAD : IC_STORE, lx + S / 2, y + S / 2, 110, C_FAINT, a); if (d_cover_st == 1) draw_spinner(lx + S / 2, y + S - 44, 14, a); }
  // titolo, sottotitolo, autore
  int ix = lx + S + 56, iw = SCREEN_W - ix - ST_X, iy = y + 4;
  int tl = text_w(font(W_LIGHT, 64), d_title) > iw ? 2 : 1;
  draw_text_wrap(font(W_LIGHT, 64), d_title, ix, iy, iw, 2, 70, C_WHITE, a);
  iy += 76 * tl + 4;
  if (d_tagline[0]) { draw_text_wrap(font(W_REG, 28), d_tagline, ix, iy, iw, 2, 34, RGB(214, 220, 236), a); iy += 46; }
  if (!det_is_lib && d_author[0]) {
    draw_avatar(d_author, d_author_av, ix + 16, iy + 18, 34, a);
    char by[128]; snprintf(by, sizeof by, _("di %s"), d_author);
    int w = draw_text(font(W_REG, 23), by, ix + 42, iy + 4, C_DIM, a, AL_L);
    if (d_ratings) {
      int sx = ix + 42 + w + 28;
      draw_stars(sx, iy + 3, 20, d_rating, a);
      char rc[40]; snprintf(rc, sizeof rc, "%.1f  (%d)", d_rating, d_ratings);
      draw_text(font(W_REG, 22), rc, sx + 5 * 24 + 8, iy + 5, C_DIM, a, AL_L);
    }
    iy += 46;
  }
  char sz[16]; human_size(d_size, sz, sizeof sz);
  int cx = ix;
  if (d_platform[0]) cx += chip(cx, iy, d_platform, C_WHITE, a) + 10;
  cx += chip(cx, iy, kind_label(d_kind), d_kind == 1 ? C_OK : d_kind == 2 ? C_ACC2 : d_kind == 3 ? C_WARN : C_WHITE, a) + 10;
  if (d_cat[0]) cx += chip(cx, iy, cat_label(d_cat), C_WHITE, a) + 10;
  if (d_ver[0]) { char v[32]; snprintf(v, sizeof v, "v%s", d_ver); cx += chip(cx, iy, v, C_WHITE, a) + 10; }
  if (sz[0]) chip(cx, iy, sz, C_WHITE, a);
  iy += 58;
  int by = iy, bx = ix;
  int aw[6] = { 240, 104, 104, 160, 84, 84 };
  for (int i = 0; i < det_action_count(); i++) {
    int foc = det_zone == 0 && act_sel == i, h = 64, w = aw[i];
    int on = (i == 1 && d_my_vote == 1) || (i == 2 && d_my_vote == -1) || (i == 4 && d_wished);
    Col bg = foc ? C_WHITE : i == 0 ? C_ACC : on ? RGB(52, 70, 100) : RGB(255, 255, 255);
    int ba = foc || i == 0 || on ? a : a * 14 / 100;
    fill_rrect(bx, by, w, h, 32, bg, ba);
    if (foc) stroke_rrect(bx - 4, by - 4, w + 8, h + 8, 36, 3, C_WHITE, a * 60 / 100);
    Col fg = foc ? ST_BG : C_WHITE;
    TTF_Font *f = font(W_BOLD, 26); int fy = by + h / 2 - TTF_FontHeight(f) / 2;
    if (i == 0) { int ins = installed_index(d_tid, d_title) >= 0; draw_icon(ins ? IC_PLAY : IC_DOWNLOAD, bx + 42, by + h / 2, 28, fg, a); draw_text(f, ins ? _("Avvia") : _("Installa"), bx + 70, fy, fg, a, AL_L); }
    else if (i == 1) { draw_icon(IC_LIKE, bx + 36, by + h / 2, 26, foc ? fg : on ? C_OK : C_WHITE, a); char c[8]; snprintf(c, sizeof c, "%d", d_likes); draw_text(f, c, bx + 60, fy, fg, a, AL_L); }
    else if (i == 2) { draw_icon(IC_DISLIKE, bx + 36, by + h / 2, 26, foc ? fg : on ? C_ERR : C_WHITE, a); char c[8]; snprintf(c, sizeof c, "%d", d_dislikes); draw_text(f, c, bx + 60, fy, fg, a, AL_L); }
    else if (i == 3) { draw_icon(IC_STAR, bx + 34, by + h / 2, 26, d_my_rating ? C_WARN : fg, a); char c[64]; if (d_my_rating) snprintf(c, sizeof c, "%d/5", d_my_rating); else snprintf(c, sizeof c, "%s", _("Valuta")); draw_text(f, c, bx + 58, fy, fg, a, AL_L); }
    else if (i == 4) draw_icon(IC_HEART, bx + w / 2, by + h / 2, 30, d_wished ? RGB(255, 90, 120) : fg, a);
    else draw_icon(IC_SHARE, bx + w / 2, by + h / 2, 28, fg, a);
    bx += w + 14;
  }
  if (det_zone == 0 && act_sel >= 4 && !det_is_lib) {
    const char *tip = act_sel == 4 ? (d_wished ? _("Nella lista dei desideri") : _("Aggiungi ai desideri: ti avviso degli aggiornamenti")) : _("Consiglia a un amico");
    draw_text(font(W_REG, 20), tip, ix, by + 76, C_DIM, a, AL_L);
  }
  iy = by + 64;
  // amici che lo usano
  if (!det_is_lib && d_fr_n) {
    int fx = ix, fy2 = iy + 44;
    for (int k = 0; k < d_fr_n; k++) draw_avatar(d_fr_oid[k], d_fr_av[k], fx + 18 + k * 30, fy2 + 18, 36, a);
    char fm[120]; snprintf(fm, sizeof fm, d_fr_count == 1 ? _("%s lo usa") : _("%s e altri %d amici lo usano"), d_fr_oid[0], d_fr_count - 1);
    draw_text_fit(font(W_REG, 22), fm, fx + 18 + d_fr_n * 30 + 12, fy2 + 6, iw - d_fr_n * 30 - 40, C_TXT, a, AL_L);
    iy = fy2 + 40;
  }
  y = (iy > top + S ? iy : top + S) + 56;
  // descrizione a sinistra, informazioni a destra
  int cardw = 520, cardx = SCREEN_W - ST_X - cardw, leftw = cardx - lx - 60;
  int ly = y;
  if (d_desc[0]) {
    draw_text(font(W_BOLD, 28), _("Descrizione"), lx, ly, C_WHITE, a, AL_L); ly += 48;
    ly += draw_text_wrap(font(W_REG, 25), d_desc, lx, ly, leftw, 20, 36, C_TXT, a) * 36 + 24;
  }
  if (d_ntags) {
    int hx = lx;
    for (int i = 0; i < d_ntags; i++) {
      char t[28]; snprintf(t, sizeof t, "#%s", d_tags[i]);
      int w = text_w(font(W_MED, 22), t) + 30;
      if (hx + w > lx + leftw) break;
      fill_rrect(hx, ly, w, 38, 19, C_ACC2, a * 18 / 100);
      draw_text(font(W_MED, 22), t, hx + 15, ly + 19 - TTF_FontHeight(font(W_MED, 22)) / 2, C_ACC2, a, AL_L);
      hx += w + 10;
    }
    ly += 60;
  }
  {
    char inst[24] = "", host[120] = "";
    if (!det_is_lib) snprintf(inst, sizeof inst, "%ld", d_downloads);
    if (d_home[0]) { const char *h = strstr(d_home, "://"); snprintf(host, sizeof host, "%s", h ? h + 3 : d_home); char *sl = strchr(host, '/'); if (sl && sl[1] == 0) *sl = 0; }
    const char *K[] = { _("Versione"), _("Dimensione"), _("Tipo di file"), _("Piattaforma"), _("Categoria"), _("Licenza"), _("Installazioni"), _("Title ID"), _("Progetto") };
    const char *V[] = { d_ver, sz, kind_label(d_kind), d_platform, d_cat[0] ? cat_label(d_cat) : "", d_license, inst, d_tid, host };
    int rows = 0; for (int i = 0; i < 9; i++) rows += V[i] && V[i][0];
    int ch = 74 + rows * 44 + 10;
    fill_rrect(cardx, y, cardw, ch, 24, RGB(255, 255, 255), a * 7 / 100);
    draw_text(font(W_BOLD, 26), _("Informazioni"), cardx + 28, y + 24, C_WHITE, a, AL_L);
    int ry = y + 76;
    for (int i = 0; i < 9; i++) if (V[i] && V[i][0]) { info_row(cardx + 28, ry, cardw - 56, K[i], V[i], a); ry += 44; }
    if (y + ch > ly) ly = y + ch;
  }
  y = ly + 40;
  int shots_y = y;
  if (d_nscreens) {
    draw_text(font(W_BOLD, 28), _("Immagini"), lx, y, C_WHITE, a, AL_L);
    if (det_zone == 1) draw_text(font(W_REG, 22), _("\xE2\x9C\x95 ingrandisci"), lx + 170, y + 6, C_DIM, a, AL_L);
    y += 50;
    int sw = 480, sh = 270;
    for (int i = 0; i < d_nscreens; i++) {
      req_det_shot(i);
      int sx = lx + i * (sw + 24);
      int foc = det_zone == 1 && shot_sel == i;
      if (d_shot[i] && d_shot_st[i] == 2) draw_tex(d_shot[i], sx, y, sw, sh, a);
      else { fill_rrect(sx, y, sw, sh, 16, RGB(26, 32, 48), a); if (d_shot_st[i] == 1) draw_spinner(sx + sw / 2, y + sh / 2, 14, a); }
      if (foc) stroke_rrect(sx - 5, y - 5, sw + 10, sh + 10, 20, 4, C_WHITE, a);
    }
    y += sh + 50;
  }
  // commenti, solo per gli homebrew
  int com_y0 = y, com_h = 104;
  if (!det_is_lib) {
    char h[96]; snprintf(h, sizeof h, _("Commenti (%d)"), d_comments);
    int w = draw_text(font(W_BOLD, 28), h, lx, y, C_WHITE, a, AL_L);
    draw_text(font(W_REG, 22), _("Triangolo: scrivi un commento"), lx + w + 24, y + 6, C_DIM, a, AL_L);
    y += 54; com_y0 = y;
    if (!nscom) { draw_text(font(W_REG, 24), _("Ancora nessun commento: premi Triangolo e scrivi il primo."), lx, y, C_DIM, a, AL_L); y += 44; }
    for (int i = 0; i < nscom; i++) {
      int foc = det_zone == 2 && com_sel == i, cw = SCREEN_W - 2 * ST_X;
      fill_rrect(lx - 16, y - 10, cw + 32, com_h - 12, 18, C_WHITE, a * (foc ? 12 : 5) / 100);
      if (foc) stroke_rrect(lx - 19, y - 13, cw + 38, com_h - 6, 21, 3, C_WHITE, a);
      draw_avatar(scom[i].author, scom[i].avatar, lx + 28, y + 26, 50, a);
      int nw = draw_text(font(W_BOLD, 23), scom[i].author, lx + 66, y, C_TXT, a, AL_L);
      draw_text(font(W_REG, 20), scom[i].when, lx + 66 + nw + 14, y + 3, C_FAINT, a, AL_L);
      draw_text_wrap(font(W_REG, 24), scom[i].body, lx + 66, y + 34, cw - 90, 2, 30, C_DIM, a);
      y += com_h;
    }
  }
  SDL_RenderSetClipRect(R, NULL);
  // lo scorrimento segue la sezione a fuoco (coordinate del contenuto: y a schermo - top)
  int bottom = y - top + 80, maxs = bottom - (SCREEN_H - ST_TOP) + 60; if (maxs < 0) maxs = 0;
  float tgt = det_zone == 0 ? 0 : det_zone == 1 ? (float)(shots_y - top - 160) : (float)(com_y0 - top + com_sel * com_h - 300);
  det_scroll_t = clampf(tgt, 0, (float)maxs);
}

// modulo di pubblicazione
typedef struct { const char *label; char *buf; size_t n; int choice; int multiline; } Field;
static int build_fields(Field *f) {
  int k = 0;
  f[k++] = (Field){ _("Titolo"), p_title, sizeof p_title, -1, 0 };
  f[k++] = (Field){ _("Sottotitolo"), p_tagline, sizeof p_tagline, -1, 0 };
  f[k++] = (Field){ _("Descrizione"), p_desc, sizeof p_desc, -1, 1 };
  f[k++] = (Field){ _("Categoria"), NULL, 0, 0, 0 };            // scelta
  f[k++] = (Field){ _("Versione"), p_ver, sizeof p_ver, -1, 0 };
  f[k++] = (Field){ _("Link al file (pkg o zip)"), p_url, sizeof p_url, -1, 0 };
  f[k++] = (Field){ _("Tipo di file"), NULL, 0, 1, 0 };         // scelta
  f[k++] = (Field){ _("Title ID (per gli zip)"), p_tid, sizeof p_tid, -1, 0 };
  f[k++] = (Field){ _("URL icona"), p_icon, sizeof p_icon, -1, 0 };
  f[k++] = (Field){ _("URL copertina"), p_cover, sizeof p_cover, -1, 0 };
  f[k++] = (Field){ _("URL screenshot (separati da spazio)"), p_shots, sizeof p_shots, -1, 0 };
  f[k++] = (Field){ _("Hashtag (separati da spazio)"), p_tags, sizeof p_tags, -1, 0 };
  return k;
}
#define NFIELDS 12

static void draw_publish(int a) {
  Field f[NFIELDS]; int nf = build_fields(f);
  int total = nf + 1;   // più il pulsante Pubblica
  int lx = (SCREEN_W - 1100) / 2, w = 1100;
  draw_text(font(W_LIGHT, 46), pub_edit ? _("Modifica homebrew") : _("Pubblica un homebrew"), lx, ST_TOP - 6, C_WHITE, a, AL_L);
  int rowh = 92, y0 = ST_TOP + 70;
  float target = pub_sel * rowh > (SCREEN_H - y0 - rowh) ? (float)(pub_sel * rowh - (SCREEN_H - y0 - rowh)) : 0;
  static float psc; psc = approach(psc, target, 14.0f);
  SDL_Rect clip = { 0, y0 - 10, SCREEN_W, SCREEN_H - y0 }; SDL_RenderSetClipRect(R, &clip);
  for (int i = 0; i < total; i++) {
    int y = y0 + i * rowh - (int)psc;
    if (y + rowh < y0 || y > SCREEN_H) continue;
    int foc = pub_sel == i;
    if (i == nf) {
      int bw = 300, bx = lx;
      fill_rrect(bx, y + 6, bw, 60, 30, foc ? C_ACC : RGB(0, 120, 90), a);
      if (foc) stroke_rrect(bx - 3, y + 3, bw + 6, 66, 33, 3, C_WHITE, a);
      draw_icon(IC_CHECK, bx + 36, y + 36, 26, C_WHITE, a);
      draw_text(font(W_MED, 28), pub_edit ? _("Salva modifiche") : _("Pubblica"), bx + 64, y + 20, C_WHITE, a, AL_L);
      continue;
    }
    if (foc) { fill_rrect(lx - 14, y, w + 28, rowh - 12, 16, C_WHITE, a * 10 / 100); stroke_rrect(lx - 17, y - 3, w + 34, rowh - 6, 19, 2, C_WHITE, a); }
    draw_text(font(W_REG, 23), f[i].label, lx, y + 8, C_DIM, a, AL_L);
    char shown[300];
    if (f[i].choice == 0) snprintf(shown, sizeof shown, "%s", _(CATS[p_cat]));
    else if (f[i].choice == 1) snprintf(shown, sizeof shown, "%s", KINDS[p_kind]);
    else snprintf(shown, sizeof shown, "%s", f[i].buf[0] ? f[i].buf : "—");
    draw_text_fit(font(W_MED, 28), shown, lx, y + 40, w - 60, f[i].buf && f[i].buf[0] ? C_WHITE : C_FAINT, a, AL_L);
    if (f[i].choice >= 0) draw_text(font(W_REG, 22), "\xE2\x80\xB9  \xE2\x80\xBA", lx + w - 60, y + 46, C_DIM, a, AL_L);
  }
  SDL_RenderSetClipRect(R, NULL);
}

// ------------------------------------------------------- guida al JSON --
// Cos'è il JSON dei giochi, i campi, un esempio, e i tre modi di riempire la
// libreria (dal telefono o dal PC, con un link, con un file).
static void draw_json_help(int a) {
  int x = ST_X, w = SCREEN_W - 2 * ST_X, y = ST_TOP;
  draw_text(font(W_LIGHT, 46), _("Come funziona il JSON dei giochi"), x, y - 4, C_WHITE, a, AL_L);
  draw_text_wrap(font(W_REG, 25), _("È un file di testo con l'elenco dei tuoi giochi: per ognuno il titolo e il link diretto al file, più copertina e dettagli se vuoi. Omega lo legge e mette i giochi in Libreria, pronti da installare con un tasto."),
                 x, y + 64, w - 40, 2, 34, C_DIM, a);
  // tre modi
  const struct { int ic; const char *t, *d; } W[3] = {
    { IC_CLOUD, N_("Dal telefono o dal PC"), N_("Apri il Telecomando nel browser (Impostazioni › Sistema › Telecomando), sezione La mia libreria: importa il file, incollalo, o scarica l'esempio e modificalo.") },
    { IC_GLOBE, N_("Con un link"), N_("Metti il JSON online (link diretto al file) e usa Collega un JSON qui sopra: resta sincronizzato e si aggiorna da solo.") },
    { IC_BOX, N_("Anche senza JSON"), N_("Dal Telecomando carichi direttamente .pkg, .zip, .elf o cartelle di giochi: arrivano via Wi-Fi e si installano da soli.") } };
  int cw = (w - 40) / 3, cy = y + 150;
  for (int i = 0; i < 3; i++) {
    int cx = x + i * (cw + 20);
    fill_rrect(cx, cy, cw, 200, 22, C_WHITE, a * 6 / 100);
    fill_circle(cx + 50, cy + 50, 30, C_ACC, a); draw_icon(W[i].ic, cx + 50, cy + 50, 30, C_WHITE, a);
    draw_text_fit(font(W_MED, 27), _(W[i].t), cx + 96, cy + 32, cw - 116, C_WHITE, a, AL_L);
    draw_text_wrap(font(W_REG, 21), _(W[i].d), cx + 24, cy + 96, cw - 48, 4, 26, C_DIM, a);
  }
  // campi ed esempio
  int ty = cy + 230, tw = 820;
  draw_text(font(W_BOLD, 26), _("Campi"), x, ty, C_WHITE, a, AL_L);
  const char *K[] = { "title", "url", "cover", "images", "platform", "title_id", "version", "description", "type" };
  const char *V[] = { N_("nome del gioco (obbligatorio)"), N_("link diretto al .pkg, .zip o .elf (obbligatorio)"), N_("immagine quadrata"), N_("elenco di screenshot"), N_("PS5 o PS4"),
                      N_("es. CUSA12345"), N_("es. 1.00"), N_("qualche riga sul gioco"), N_("pkg, zip, elf (se manca si capisce dal link)") };
  for (int i = 0; i < 9; i++) {
    int ry = ty + 44 + i * 40;
    fill_rrect(x, ry - 4, tw, 36, 10, C_WHITE, a * (i % 2 ? 3 : 6) / 100);
    draw_text(font(W_MED, 21), K[i], x + 16, ry, C_ACC2, a, AL_L);
    draw_text_fit(font(W_REG, 21), _(V[i]), x + 200, ry, tw - 220, C_TXT, a, AL_L);
  }
  int ex = x + tw + 40, ew = w - tw - 40;
  draw_text(font(W_BOLD, 26), _("Esempio"), ex, ty, C_WHITE, a, AL_L);
  fill_rrect(ex, ty + 40, ew, 360, 18, RGB(6, 8, 14), a * 85 / 100);
  static const char *EX[] = { "{ \"name\": \"I miei giochi\", \"games\": [", "  { \"title\": \"Il mio gioco\",", "    \"url\": \"https://.../gioco.pkg\",",
    "    \"cover\": \"https://.../copertina.jpg\",", "    \"platform\": \"PS4\", \"title_id\": \"CUSA12345\",", "    \"version\": \"1.00\" },", "  { \"title\": \"Emulatore\",",
    "    \"url\": \"https://.../emu.zip\" }", "] }" };
  for (int i = 0; i < 9; i++) draw_text_fit(font(W_REG, 20), EX[i], ex + 22, ty + 58 + i * 36, ew - 40, i == 0 || i == 8 ? C_DIM : RGB(170, 220, 255), a, AL_L);
  char ip[48], m[200]; console_ip(ip, sizeof ip);
  snprintf(m, sizeof m, _("Scarica questo esempio dal Telecomando: http://%s:9095 › La mia libreria"), ip[0] ? ip : "IP");
  draw_text_fit(font(W_REG, 21), m, ex, ty + 414, ew, C_FAINT, a, AL_L);
}

// ----------------------------------------------------------------- pannello --
void store_open(void) {
  free_covers(); free_detail_tex();
  tab = 0; view = 0; lv = LV_CONTENT; sort = 0; sel = 0; row = -1; bar_sel = 0; q[0] = 0; mine_filter = 0; wish_filter = 0;
  list_scroll = list_scroll_t = 0; hero_i = 0; hero_fade = 1; hero_at = SDL_GetTicks();
  load_apps();
  if (ov_top() != OV_STORE) ov_push(OV_STORE);
}

void store_draw(float t) {
  if (watch_from >= 0) {
    Uint32 now = SDL_GetTicks();
    if (now > watch_until) watch_from = -1;
    else if (now > watch_next && !loading) { watch_next = now + 2000; load_items(); }
  }
  int a = (int)(255 * t);
  fill_rect(0, 0, SCREEN_W, SCREEN_H, ST_BG, t > 0.98f ? 255 : a);
  if (view == 3) { draw_shot_viewer(a); return; }
  if (view == 4) { draw_header(a); draw_json_help(a); int ic[2] = { IC_BTN_O, IC_BTN_X }; const char *lb[2] = { _("Indietro"), _("Apri il Telecomando") }; hints(ic, lb, 2, a); return; }
  // alone d'ambiente col colore della copertina a fuoco
  grad_v(0, 0, SCREEN_W, 760, g_amb, a * 55 / 100, ST_BG, 0);
  glow(SCREEN_W - 300, 260, 620, g_amb, a * 20 / 100);
  if (view == 2) draw_publish(a);
  else if (view == 1) draw_detail(a);
  else draw_list(a);
  draw_header(a);
  // i comandi in basso dipendono dal punto in cui si è
  int ic[5]; const char *lb[5]; int n = 0;
  if (view == 0) {
    ic[n] = IC_BTN_X; lb[n++] = lv == LV_CONTENT ? _("Apri") : _("Seleziona");
    ic[n] = IC_BTN_O; lb[n++] = lv == LV_CONTENT ? _("Indietro") : _("Esci dallo Store");
    ic[n] = IC_BTN_TRI; lb[n++] = _("Cerca");
    ic[n] = -1; lb[n++] = _("L1/R1  Schede");
  } else if (view == 1) {
    ic[n] = IC_BTN_X; lb[n++] = _("Scegli");
    ic[n] = IC_BTN_O; lb[n++] = _("Indietro");
    if (!det_is_lib) { ic[n] = IC_BTN_TRI; lb[n++] = _("Commenta"); }
    if (!det_is_lib && det_zone == 2 && com_sel < nscom && !scom[com_sel].mine) { ic[n] = IC_BTN_SQ; lb[n++] = _("Segnala commento"); }
    else { ic[n] = IC_BTN_SQ; lb[n++] = _("Opzioni"); }
  } else { ic[n] = IC_BTN_X; lb[n++] = _("Modifica campo"); ic[n] = IC_BTN_O; lb[n++] = _("Annulla"); }
  grad_v(0, SCREEN_H - 110, SCREEN_W, 110, ST_BG, 0, ST_BG, a);
  hints(ic, lb, n, a);
}

// -------------------------------------------------------------------- input --
static void launch_installed(void) {
  int idx = installed_index(d_tid, d_title);
  if (idx < 0) { set_msg(_("Non installato"), 1); return; }
  ov_clear();
  launch_app(idx);
}

static void confirm_uninstall(int idx, void *ud) {
  (void)ud; if (idx != 0) return;
  int k = installed_index(d_tid, d_title), rc = -1;
  if (k >= 0 && apps[k].pld) rc = payload_remove(apps[k].dir);
  else if (k >= 0 && apps[k].hb) { char root[256]; snprintf(root, sizeof root, "%s", apps[k].dir); char *sl = strrchr(root, '/'); if (sl) *sl = 0; rc = hb_remove(apps[k].dir, root); }
  else rc = store_uninstall(d_tid);
  if (rc == 0) { set_msg(_("Titolo disinstallato"), 0); scan_apps(); }
  else { char m[160]; snprintf(m, sizeof m, _("Disinstallazione non riuscita (0x%x)"), (unsigned)rc); set_msg(m, 1); }
}

// segnalazione di un homebrew o di un commento (termini d'uso: "Segnalazioni e moderazione")
static const char *REPORT_KEYS[] = { "pirateria", "malware", "contenuto_offensivo", "spam", "link_rotto", "altro" };
static char rep_comment[16];
static void report_done(int st, JVal *j, const char *raw, void *ud) {
  (void)raw; (void)ud;
  const char *e = j ? jstr(j, "error", "") : "";
  if (st == 200 || st == 201) set_msg(jbool(j, "hidden") ? _("Grazie: contenuto oscurato in attesa di verifica") : _("Grazie, la segnalazione \xC3\xA8 stata inviata"), 0);
  else if (!strcmp(e, "cannot_report_own")) set_msg(_("Non puoi segnalare un tuo contenuto"), 1);
  else if (st == 429) set_msg(_("Troppe segnalazioni: riprova pi\xC3\xB9 tardi"), 1);
  else set_msg(_("Segnalazione non riuscita"), 1);
}
static void report_pick(int idx, void *ud) {
  (void)ud; if (idx < 0 || idx > 5) return;
  char note[300] = "", esc[640] = "", body[900], path[96];
  if (idx == 5 && !edit_text(_("Descrivi il problema"), note, sizeof note, 0)) return;
  json_escape(esc, sizeof esc, note);
  if (rep_comment[0]) snprintf(body, sizeof body, "{\"reason\":\"%s\",\"note\":\"%s\",\"comment_id\":%s}", REPORT_KEYS[idx], esc, rep_comment);
  else snprintf(body, sizeof body, "{\"reason\":\"%s\",\"note\":\"%s\"}", REPORT_KEYS[idx], esc);
  snprintf(path, sizeof path, OMEGA_API "/store/apps/%s/report", det_id);
  net_req(HTTP_POST, path, body, report_done, NULL);
}
static void report_open(const char *comment_id) {
  const char *items[] = { _("Pirateria o contenuto non autorizzato"), _("Malware o file pericoloso"), _("Contenuto offensivo"), _("Spam"), _("Link non funzionante"), _("Altro...") };
  snprintf(rep_comment, sizeof rep_comment, "%s", comment_id ? comment_id : "");
  menu_open(comment_id ? _("Segnala il commento") : _("Segnala questo homebrew"), items, 6, report_pick, NULL);
}

static int opt_act[8]; static int nopt_act;
static void options_pick(int idx, void *ud) {
  (void)ud; if (idx < 0 || idx >= nopt_act) return;
  switch (opt_act[idx]) {
    case 1: do_install(); break;   // reinstalla o aggiorna
    case 2: confirm_open(_("Disinstallare questo titolo?"), _("Disinstalla"), confirm_uninstall, NULL); break;
    case 3: publish_open_edit(); break;
    case 4: confirm_open(_("Eliminare questo homebrew?"), _("Elimina"), confirm_delete, NULL); break;
    case 5: report_open(NULL); break;
    case 6: { const LItem *it = lib_find(det_id); if (it) { LItem copy = *it; lib_edit(&copy); view = 0; lv = LV_CONTENT; } break; }
    case 7: confirm_open(_("Togliere questo gioco dalla libreria? Il gioco installato resta."), _("Togli"), lib_delete_yes, NULL); break;
  }
}
static void detail_options(void) {
  static const char *lbl[8];
  nopt_act = 0;
  if (installed_index(d_tid, d_title) >= 0) { lbl[nopt_act] = _("Reinstalla / aggiorna"); opt_act[nopt_act++] = 1; lbl[nopt_act] = _("Disinstalla"); opt_act[nopt_act++] = 2; }
  if (!det_is_lib && d_mine) { lbl[nopt_act] = _("Modifica"); opt_act[nopt_act++] = 3; lbl[nopt_act] = _("Elimina"); opt_act[nopt_act++] = 4; }
  if (!det_is_lib && !d_mine) { lbl[nopt_act] = _("Segnala"); opt_act[nopt_act++] = 5; }
  if (det_is_lib) { lbl[nopt_act] = _("Modifica"); opt_act[nopt_act++] = 6; lbl[nopt_act] = _("Togli dalla libreria"); opt_act[nopt_act++] = 7; }
  if (!nopt_act) { set_msg(_("Nessuna opzione disponibile"), 0); return; }
  menu_open(d_title, lbl, nopt_act, options_pick, NULL);
}

static void detail_input(int b) {
  if (b == B_O) { view = 0; lv = LV_CONTENT; return; }
  if (b == B_TRI && !det_is_lib) { write_comment(); return; }
  if (b == B_SQ) {
    // sul commento a fuoco, se non è mio, segnala quello; altrimenti le opzioni della scheda
    if (det_zone == 2 && com_sel < nscom && !scom[com_sel].mine) report_open(scom[com_sel].id);
    else detail_options();
    return;
  }
  if (det_zone == 0) {
    int ac = det_action_count();
    if (b == B_LEFT && act_sel > 0) act_sel--;
    else if (b == B_RIGHT && act_sel < ac - 1) act_sel++;
    else if (b == B_DOWN) { if (d_nscreens) { det_zone = 1; shot_sel = 0; } else if (!det_is_lib) { det_zone = 2; com_sel = 0; } det_scroll_t += 340; }
    else if (b == B_X) {
      if (act_sel == 0) { if (installed_index(d_tid, d_title) >= 0) launch_installed(); else do_install(); }
      else if (act_sel == 1) send_vote(d_my_vote == 1 ? 0 : 1);
      else if (act_sel == 2) send_vote(d_my_vote == -1 ? 0 : -1);
      else if (act_sel == 4) toggle_wish();
      else if (act_sel == 5) recommend_open();
      else if (act_sel == 3) { const char *st[] = { _("1 stella"), _("2 stelle"), _("3 stelle"), _("4 stelle"), _("5 stelle") }; menu_open(_("Valuta"), st, 5, rate_pick, NULL); }
    }
    return;
  }
  if (det_zone == 1) {
    if (b == B_LEFT && shot_sel > 0) shot_sel--;
    else if (b == B_RIGHT && shot_sel < d_nscreens - 1) shot_sel++;
    else if (b == B_UP) { det_zone = 0; det_scroll_t -= 340; if (det_scroll_t < 0) det_scroll_t = 0; }
    else if (b == B_DOWN && !det_is_lib) { det_zone = 2; com_sel = 0; det_scroll_t += 300; }
    else if (b == B_X && d_nscreens) { view = 3; view_shot = shot_sel; }
    return;
  }
  // commenti
  if (b == B_UP) { if (com_sel > 0) { com_sel--; det_scroll_t -= 96; if (det_scroll_t < 0) det_scroll_t = 0; } else { det_zone = d_nscreens ? 1 : 0; } }
  else if (b == B_DOWN && com_sel < nscom - 1) { com_sel++; det_scroll_t += 96; }
}

static void publish_input(int b) {
  Field f[NFIELDS]; int nf = build_fields(f);
  if (b == B_O) { view = 0; lv = LV_CONTENT; return; }
  if (b == B_UP && pub_sel > 0) pub_sel--;
  else if (b == B_DOWN && pub_sel < nf) pub_sel++;
  else if (pub_sel < nf && f[pub_sel].choice == 0 && (b == B_LEFT || b == B_RIGHT)) p_cat = (p_cat + (b == B_RIGHT ? 1 : NCATS - 1)) % NCATS;
  else if (pub_sel < nf && f[pub_sel].choice == 1 && (b == B_LEFT || b == B_RIGHT)) p_kind = (p_kind + (b == B_RIGHT ? 1 : NKINDS - 1)) % NKINDS;
  else if (b == B_X) {
    if (pub_sel == nf) { do_publish(); return; }
    Field *fl = &f[pub_sel];
    if (fl->choice >= 0) { if (fl->choice == 0) p_cat = (p_cat + 1) % NCATS; else p_kind = (p_kind + 1) % NKINDS; return; }
    char buf[4096]; snprintf(buf, sizeof buf, "%s", fl->buf);
    if (edit_text(fl->label, buf, sizeof buf, 0)) snprintf(fl->buf, fl->n, "%s", buf);
  }
}

void store_input(int b) {
  if (view == 4) { if (b == B_O) view = 0; else if (b == B_X) remote_open(); return; }
  if (view == 3) {
    if (b == B_O || b == B_X) view = 1;
    else if (b == B_LEFT && view_shot > 0) view_shot--;
    else if (b == B_RIGHT && view_shot < d_nscreens - 1) view_shot++;
    return;
  }
  if (view == 2) publish_input(b);
  else if (view == 1) detail_input(b);
  else list_input(b);
}
