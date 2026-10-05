// Omega UI — grafica. Il renderer è software (l'unico che funziona nel contesto
// homebrew), quindi tutto ciò che costa si prepara una volta: testo in cache,
// angoli, cerchi e ombre come texture riusate a nove fette, icone vettoriali
// rasterizzate con SDF all'avvio, sfondi già scalati.
#include "app.h"
#include <unistd.h>
#include <SDL_image.h>
#include <dirent.h>
#include <math.h>
#include <stdlib.h>
#include <sys/stat.h>

SDL_Renderer *R;
float g_dt = 1.0f / 60; double g_time = 0; Uint32 g_frame = 0;

const Col C_WHITE = { 255, 255, 255, 255 };
const Col C_TXT   = { 242, 245, 250, 255 };
const Col C_DIM   = { 168, 178, 198, 255 };
const Col C_FAINT = { 110, 120, 142, 255 };
Col C_ACC         = { 0, 112, 243, 255 };
Col C_ACC2        = { 56, 160, 255, 255 };
const Col C_OK    = { 46, 204, 113, 255 };
const Col C_ERR   = { 255, 92, 92, 255 };
const Col C_WARN  = { 255, 184, 48, 255 };
Col C_PANEL       = { 20, 24, 36, 255 };
const Col C_BLACK = { 0, 0, 0, 255 };

// --------------------------------------------------------------------- font --
// font di sistema della console: non si ridistribuiscono
#ifndef OMEGA_DESKTOP
static const char *FONT_FILES[4] = {
  "/preinst/common/font/SST-Light.otf", "/preinst/common/font/SST-Roman.otf",
  "/preinst/common/font/SST-Medium.otf", "/preinst/common/font/SST-Bold.otf",
};
#endif
static const char *FONT_FALLBACK = "/preinst/common/font/n023055ms.ttf";
#define MAX_FONTS 48
typedef struct { int w, size; TTF_Font *f; } FontSlot;
static FontSlot fonts[MAX_FONTS]; static int nfonts;

static TTF_Font *open_primary(int weight, int size) {
#ifdef OMEGA_DESKTOP
  static const int HN_IDX[4] = { 7, 0, 10, 1 };   // Helvetica Neue: Light, Regular, Medium, Bold
  return TTF_OpenFontIndex("/System/Library/Fonts/HelveticaNeue.ttc", size, HN_IDX[weight]);
#else
  return TTF_OpenFont(FONT_FILES[weight], size);
#endif
}

TTF_Font *font(int weight, int size) {
  for (int i = 0; i < nfonts; i++) if (fonts[i].w == weight && fonts[i].size == size) return fonts[i].f;
  TTF_Font *f = open_primary(weight, size);
  if (!f) { f = TTF_OpenFont(FONT_FALLBACK, size); if (f && weight >= W_MED) TTF_SetFontStyle(f, TTF_STYLE_BOLD); }
  if (!f) { omega_log("font %d/%d: %s", weight, size, TTF_GetError()); return nfonts ? fonts[0].f : NULL; }
  TTF_SetFontHinting(f, TTF_HINTING_LIGHT);
  if (nfonts < MAX_FONTS) { fonts[nfonts].w = weight; fonts[nfonts].size = size; fonts[nfonts].f = f; nfonts++; }
  return f;
}

// ---------------------------------------------------------- font di ripiego --
// I font SST principali non hanno tutti gli alfabeti (giapponese, cinese,
// coreano, thai...). All'avvio si elencano gli altri font di sistema; per ogni
// stringa con caratteri che il font principale non ha si sceglie il primo
// font che li ha tutti (o il maggior numero), della stessa dimensione.
// La scelta resta in cache per stringa, così misure e disegno usano lo stesso font.
#define MAX_FACES 24
typedef struct { char path[200]; int weight; TTF_Font *probe; int bad; } Face;
static Face faces[MAX_FACES]; static int nfaces;
typedef struct { int face, w, size, bold, thai; TTF_Font *f; } FbSlot;   // face -1 = font principale (thai)
#define MAX_FB 64
static FbSlot fbs[MAX_FB]; static int nfbs;

static int face_weight(const char *name) {
  if (strstr(name, "Bold") || strstr(name, "Heavy") || strstr(name, "W6") || strstr(name, "W7") || strstr(name, "W8")) return W_BOLD;
  if (strstr(name, "Medium") || strstr(name, "W5")) return W_MED;
  if (strstr(name, "Light") || strstr(name, "Thin") || strstr(name, "W0") || strstr(name, "W1") || strstr(name, "W2")) return W_LIGHT;
  return W_REG;
}
static void face_add(const char *path, const char *name) {
  struct stat st;
  if (nfaces >= MAX_FACES || stat(path, &st) != 0) return;
  Face *fc = &faces[nfaces++]; memset(fc, 0, sizeof *fc);
  snprintf(fc->path, sizeof fc->path, "%s", path); fc->weight = face_weight(name);
}
static int name_cmp(const void *a, const void *b) { return strcmp(*(char *const *)a, *(char *const *)b); }

static void fonts_scan(void) {
#ifdef OMEGA_DESKTOP
  // equivalenti di macOS, per provare la UI in tutte le lingue
  static const char *MAC[] = {
    "/System/Library/Fonts/\xE3\x83\x92\xE3\x83\xA9\xE3\x82\xAE\xE3\x83\x8E\xE8\xA7\x92\xE3\x82\xB4\xE3\x82\xB7\xE3\x83\x83\xE3\x82\xAF W3.ttc",   // Hiragino Sans W3
    "/System/Library/Fonts/\xE3\x83\x92\xE3\x83\xA9\xE3\x82\xAE\xE3\x83\x8E\xE8\xA7\x92\xE3\x82\xB4\xE3\x82\xB7\xE3\x83\x83\xE3\x82\xAF W6.ttc",   // Hiragino Sans W6
    "/System/Library/Fonts/Hiragino Sans GB.ttc", "/System/Library/Fonts/STHeiti Light.ttc", "/System/Library/Fonts/STHeiti Medium.ttc",
    "/System/Library/Fonts/AppleSDGothicNeo.ttc", "/System/Library/Fonts/Supplemental/Thonburi.ttc",
    "/System/Library/Fonts/Supplemental/Arial Unicode.ttf",
  };
  for (unsigned i = 0; i < sizeof MAC / sizeof MAC[0]; i++) face_add(MAC[i], MAC[i]);
#else
  const char *dir = "/preinst/common/font";
  DIR *d = opendir(dir);
  char *names[64]; int n = 0;
  if (d) {
    struct dirent *e;
    while ((e = readdir(d)) && n < 64) {
      const char *x = strrchr(e->d_name, '.');
      if (!x || (strcasecmp(x, ".otf") && strcasecmp(x, ".ttf") && strcasecmp(x, ".ttc"))) continue;
      int primary = 0; for (int i = 0; i < 4; i++) if (!strcmp(strrchr(FONT_FILES[i], '/') + 1, e->d_name)) primary = 1;
      if (!primary) names[n++] = strdup(e->d_name);
    }
    closedir(d);
  }
  qsort(names, (size_t)n, sizeof names[0], name_cmp);
  for (int i = 0; i < n; i++) { char p[200]; snprintf(p, sizeof p, "%s/%s", dir, names[i]); face_add(p, names[i]); free(names[i]); }
#endif
  (void)name_cmp;
  char list[1024] = ""; size_t o = 0;
  for (int i = 0; i < nfaces && o < sizeof list - 1; i++) o += (size_t)snprintf(list + o, sizeof list - o, "%s%s", i ? ", " : "", strrchr(faces[i].path, '/') + 1);
  omega_log("font di ripiego (%d): %s", nfaces, list);
}

static TTF_Font *face_probe(int i) {
  Face *fc = &faces[i];
  if (!fc->probe && !fc->bad) {
    fc->probe = TTF_OpenFontIndex(fc->path, 16, 0);
    if (!fc->probe) { fc->bad = 1; omega_log("font %s: %s", fc->path, TTF_GetError()); }
  }
  return fc->probe;
}

static TTF_Font *fb_get(int face, int weight, int size, int bold, int thai) {
  for (int i = 0; i < nfbs; i++)
    if (fbs[i].face == face && fbs[i].size == size && fbs[i].bold == bold && fbs[i].thai == thai && (face >= 0 || fbs[i].w == weight)) return fbs[i].f;
  if (nfbs >= MAX_FB) return NULL;                    // pieno: si resta sul font principale
  TTF_Font *f = face >= 0 ? TTF_OpenFontIndex(faces[face].path, size, 0) : open_primary(weight, size);
  if (!f) return NULL;
  TTF_SetFontHinting(f, TTF_HINTING_LIGHT);
  if (face >= 0 && bold) TTF_SetFontStyle(f, TTF_STYLE_BOLD);
  if (thai) TTF_SetFontScriptName(f, "Thai");      // HarfBuzz: segni vocalici e toni al posto giusto
  { FbSlot *b = &fbs[nfbs++]; b->face = face; b->w = weight; b->size = size; b->bold = bold; b->thai = thai; b->f = f; }
  return f;
}

static int utf8_next(const unsigned char **pp, Uint32 *cp) {   // 0 a fine stringa
  const unsigned char *p = *pp;
  if (!*p) return 0;
  if (*p < 0x80) { *cp = *p; *pp = p + 1; return 1; }
  if ((*p & 0xE0) == 0xC0 && p[1]) { *cp = ((Uint32)(*p & 0x1F) << 6) | (p[1] & 0x3F); *pp = p + 2; return 1; }
  if ((*p & 0xF0) == 0xE0 && p[1] && p[2]) { *cp = ((Uint32)(*p & 0x0F) << 12) | ((Uint32)(p[1] & 0x3F) << 6) | (p[2] & 0x3F); *pp = p + 3; return 1; }
  if ((*p & 0xF8) == 0xF0 && p[1] && p[2] && p[3]) { *cp = ((Uint32)(*p & 0x07) << 18) | ((Uint32)(p[1] & 0x3F) << 12) | ((Uint32)(p[2] & 0x3F) << 6) | (p[3] & 0x3F); *pp = p + 4; return 1; }
  *cp = 0xFFFD; *pp = p + 1; return 1;
}

static int font_weight_size(TTF_Font *f, int *w, int *size) {
  for (int i = 0; i < nfonts; i++) if (fonts[i].f == f) { *w = fonts[i].w; *size = fonts[i].size; return 1; }
  return 0;
}

// Font con cui disegnare s: f se ha tutti i caratteri, altrimenti un ripiego.
#define RC_SIZE 2048
typedef struct { TTF_Font *f, *rf; Uint32 h; char *s; } RCEntry;
static RCEntry rc[RC_SIZE];
static Uint32 fnv(const char *s, TTF_Font *f, Uint32 col);
static TTF_Font *resolve(TTF_Font *f, const char *s) {
  if (!f || !s) return f;
  const unsigned char *p = (const unsigned char *)s;
  while (*p && *p < 0x80) p++;
  if (!*p) return f;                                   // solo ASCII: sempre il font principale
  Uint32 h = fnv(s, f, 0x5EED);
  RCEntry *e = &rc[h % RC_SIZE];
  if (e->s && e->f == f && e->h == h && !strcmp(e->s, s)) return e->rf;
  Uint32 miss[48]; int nmiss = 0, thai = 0; Uint32 cp;
  p = (const unsigned char *)s;
  while (utf8_next(&p, &cp)) {
    if (cp < 0x80 || cp == 0xFE0F || cp == 0x200D) continue;
    if (cp >= 0x0E00 && cp <= 0x0E7F) thai = 1;
    if (TTF_GlyphIsProvided32(f, cp)) continue;
    int dup = 0; for (int i = 0; i < nmiss; i++) if (miss[i] == cp) dup = 1;
    if (!dup && nmiss < 48) miss[nmiss++] = cp;
  }
  TTF_Font *rf = f;
  int w = W_REG, size = 0;
  if (font_weight_size(f, &w, &size)) {
    if (nmiss) {
      int best = -1, best_cov = 0, best_wd = 99;
      for (int i = 0; i < nfaces; i++) {
        TTF_Font *pf = face_probe(i); if (!pf) continue;
        int cov = 0; for (int k = 0; k < nmiss; k++) cov += TTF_GlyphIsProvided32(pf, miss[k]) != 0;
        int wd = abs(faces[i].weight - w);
        if (cov > best_cov || (cov == best_cov && cov > 0 && wd < best_wd)) { best = i; best_cov = cov; best_wd = wd; }
        if (cov == nmiss && wd == 0) break;
      }
      if (best >= 0) { TTF_Font *ff = fb_get(best, w, size, w >= W_MED && faces[best].weight < W_MED, thai); if (ff) rf = ff; }
    } else if (thai) { TTF_Font *ff = fb_get(-1, w, size, 0, 1); if (ff) rf = ff; }
  }
  free(e->s); e->s = strdup(s); e->f = f; e->h = h; e->rf = rf;
  return rf;
}

// -------------------------------------------------------------- cache testo --
#define TC_SIZE 1024
typedef struct { TTF_Font *f; Uint32 col; Uint32 hash; char *s; SDL_Texture *t; int w, h, dy; Uint32 used; } TCEntry;
static TCEntry tc[TC_SIZE];

static Uint32 fnv(const char *s, TTF_Font *f, Uint32 col) {
  Uint32 h = 2166136261u ^ (Uint32)(uintptr_t)f ^ (col * 16777619u);
  while (*s) { h ^= (unsigned char)*s++; h *= 16777619u; }
  return h;
}

// Toglie i caratteri che il font non ha (emoji ecc.): meglio niente che un quadratino.
static void sanitize(TTF_Font *f, const char *s, char *out, size_t n) {
  size_t o = 0;
  const unsigned char *p = (const unsigned char *)s;
  while (*p && o + 5 < n) {
    Uint32 cp; int len;
    if (*p < 0x80) { cp = *p; len = 1; }
    else if ((*p & 0xE0) == 0xC0 && p[1]) { cp = ((Uint32)(*p & 0x1F) << 6) | (p[1] & 0x3F); len = 2; }
    else if ((*p & 0xF0) == 0xE0 && p[1] && p[2]) { cp = ((Uint32)(*p & 0x0F) << 12) | ((Uint32)(p[1] & 0x3F) << 6) | (p[2] & 0x3F); len = 3; }
    else if ((*p & 0xF8) == 0xF0 && p[1] && p[2] && p[3]) { cp = ((Uint32)(*p & 0x07) << 18) | ((Uint32)(p[1] & 0x3F) << 12) | ((Uint32)(p[2] & 0x3F) << 6) | (p[3] & 0x3F); len = 4; }
    else { p++; continue; }
    int keep = cp < 0x80 || (cp != 0xFE0F && cp != 0x200D && TTF_GlyphIsProvided32(f, cp));
    if (keep) { memcpy(out + o, p, (size_t)len); o += (size_t)len; }
    p += len;
  }
  // niente spazi finali rimasti dopo un'emoji tolta
  while (o > 0 && out[o - 1] == ' ') o--;
  out[o] = 0;
}

// Larghezza di s col font che lo disegnerà davvero (eventuale ripiego).
static int measure(TTF_Font *f, const char *s) {
  TTF_Font *rf = resolve(f, s);
  char clean[1024]; sanitize(rf, s, clean, sizeof clean);
  int w = 0, hh = 0; TTF_SizeUTF8(rf, clean, &w, &hh);
  return w;
}

static TCEntry *text_get(TTF_Font *f, const char *s, Col c) {
  if (!f || !s || !*s) return NULL;
  Uint32 col = ((Uint32)c.r << 16) | ((Uint32)c.g << 8) | c.b;
  Uint32 h = fnv(s, f, col);
  int idx = (int)(h % TC_SIZE), victim = -1; Uint32 oldest = 0xFFFFFFFF;
  for (int k = 0; k < 8; k++) {
    TCEntry *e = &tc[(idx + k) % TC_SIZE];
    if (e->t && e->hash == h && e->f == f && e->col == col && !strcmp(e->s, s)) { e->used = g_frame; return e; }
    if (!e->t) { victim = (idx + k) % TC_SIZE; oldest = 0; break; }
    if (e->used < oldest) { oldest = e->used; victim = (idx + k) % TC_SIZE; }
  }
  TCEntry *e = &tc[victim];
  if (e->t) { SDL_DestroyTexture(e->t); free(e->s); e->t = NULL; }
  TTF_Font *rf = resolve(f, s);
  char clean[1024];
  sanitize(rf, s, clean, sizeof clean);
  if (!clean[0]) return NULL;
  SDL_Surface *su = TTF_RenderUTF8_Blended(rf, clean, c);
  if (!su) return NULL;
  e->t = SDL_CreateTextureFromSurface(R, su);
  e->w = su->w; e->h = su->h; SDL_FreeSurface(su);
  if (!e->t) return NULL;
  SDL_SetTextureBlendMode(e->t, SDL_BLENDMODE_BLEND);
  // un font di ripiego si allinea sulla linea di base di quello principale
  e->dy = rf != f ? TTF_FontAscent(f) - TTF_FontAscent(rf) : 0;
  e->f = f; e->col = col; e->hash = h; e->s = strdup(s); e->used = g_frame;
  return e;
}

void gfx_frame(void) {
  g_frame++;
  // libera il testo non usato da circa 10 secondi
  if ((g_frame & 63) == 0)
    for (int i = 0; i < TC_SIZE; i++)
      if (tc[i].t && g_frame - tc[i].used > 600) { SDL_DestroyTexture(tc[i].t); free(tc[i].s); tc[i].t = NULL; tc[i].s = NULL; }
}

// Misure in cache: TTF_SizeUTF8 costa, e molte stringhe si misurano a ogni fotogramma.
#define WC_SIZE 4096
typedef struct { TTF_Font *f; Uint32 h; char *s; int w; } WCEntry;
static WCEntry wc[WC_SIZE];
int text_w(TTF_Font *f, const char *s) {
  if (!f || !s || !*s) return 0;
  Uint32 h = fnv(s, f, 0);
  WCEntry *e = &wc[h % WC_SIZE];
  if (e->s && e->f == f && e->h == h && !strcmp(e->s, s)) return e->w;
  int w = measure(f, s);
  free(e->s); e->s = strdup(s); e->f = f; e->h = h; e->w = w;
  return w;
}

int draw_text(TTF_Font *f, const char *s, int x, int y, Col c, int alpha, int align) {
  if (alpha <= 0) return text_w(f, s);
  TCEntry *e = text_get(f, s, c);
  if (!e) return 0;
  if (align == AL_C) x -= e->w / 2; else if (align == AL_R) x -= e->w;
  SDL_SetTextureAlphaMod(e->t, (Uint8)(alpha > 255 ? 255 : alpha));
  SDL_Rect d = { x, y + e->dy, e->w, e->h };
  SDL_RenderCopy(R, e->t, NULL, &d);
  return e->w;
}

// Tronca con "…" oltre maxw. Il risultato va in cache perché la ricerca del
// punto di taglio misura la stringa molte volte.
#define FC_SIZE 512
typedef struct { TTF_Font *f; Uint32 h; int maxw; char *s; char *out; } FCEntry;
static FCEntry fc[FC_SIZE];
int draw_text_fit(TTF_Font *f, const char *s, int x, int y, int maxw, Col c, int alpha, int align) {
  if (!s || !*s) return 0;
  if (text_w(f, s) <= maxw) return draw_text(f, s, x, y, c, alpha, align);
  Uint32 h = fnv(s, f, (Uint32)maxw);
  FCEntry *e = &fc[h % FC_SIZE];
  if (!(e->s && e->f == f && e->h == h && e->maxw == maxw && !strcmp(e->s, s))) {
    char buf[512]; size_t n = strlen(s); if (n > sizeof buf - 4) n = sizeof buf - 4;
    memcpy(buf, s, n); buf[n] = 0;
    // ricerca binaria sul punto di taglio
    size_t lo = 0, hi = n;
    while (lo < hi) {
      size_t mid = (lo + hi + 1) / 2, m = mid;
      while (m > 0 && ((unsigned char)s[m] & 0xC0) == 0x80) m--;
      char tmp[516]; memcpy(tmp, s, m); memcpy(tmp + m, "\xE2\x80\xA6", 4);
      if (measure(f, tmp) <= maxw) lo = mid; else hi = mid - 1;
    }
    size_t m = lo; while (m > 0 && ((unsigned char)s[m] & 0xC0) == 0x80) m--;
    while (m > 0 && s[m - 1] == ' ') m--;
    memcpy(buf, s, m); memcpy(buf + m, "\xE2\x80\xA6", 4);
    free(e->s); free(e->out);
    e->s = strdup(s); e->out = strdup(buf); e->f = f; e->h = h; e->maxw = maxw;
  }
  return draw_text(f, e->out, x, y, c, alpha, align);
}

// ---------------------------------------------------------------- a capo --
// Si va a capo agli spazi e, per cinese e giapponese che non li usano, tra un
// ideogramma e l'altro (mai prima di 。、」 ecc. né dopo 「（). Le righe calcolate
// restano in cache per stringa, font e larghezza.
static int is_cjk(Uint32 cp) {
  return (cp >= 0x2E80 && cp <= 0x9FFF) || (cp >= 0xF900 && cp <= 0xFAFF) || (cp >= 0xFF00 && cp <= 0xFFEF) || (cp >= 0x20000 && cp <= 0x2FFFF);
}
static int no_break_before(Uint32 cp) {
  static const Uint32 L[] = { 0x3001, 0x3002, 0xFF0C, 0xFF0E, 0xFF01, 0xFF1F, 0xFF1A, 0xFF1B, 0xFF09, 0x300D, 0x300F, 0x3011, 0x3015,
                              0x3009, 0x300B, 0x30FC, 0x2026, 0x3005, 0x30FB, 0x0021, 0x003F, 0x002C, 0x002E, 0x003A, 0x0029 };
  for (unsigned i = 0; i < sizeof L / sizeof L[0]; i++) if (L[i] == cp) return 1;
  return 0;
}
static int no_break_after(Uint32 cp) { return cp == 0x300C || cp == 0x300E || cp == 0xFF08 || cp == 0x3010 || cp == 0x3014 || cp == 0x3008 || cp == 0x300A || cp == 0x0028; }

#define WR_SIZE 256
typedef struct { TTF_Font *f; Uint32 h; int maxw, maxlines, n, fit; char *s; char *lines; } WREntry;
static WREntry wr[WR_SIZE];

// Righe di s separate da '\0'; con fit l'ultima va troncata con draw_text_fit.
static int wrap_lines(TTF_Font *f, const char *s, int maxw, int maxlines, char *out, size_t outn, int *fit) {
  int lines = 0; size_t o = 0; const char *p = s;
  char line[512];
  *fit = 0;
  while (*p && lines < maxlines) {
    while (*p == ' ') p++;
    size_t best = 0; const char *q = p;
    for (;;) {
      // prossimo punto in cui si può andare a capo dopo q
      const unsigned char *r = (const unsigned char *)q; Uint32 prev = 0, cp = 0; const char *e = NULL; int last = 0;
      if (q > p) { const unsigned char *b = (const unsigned char *)q; do b--; while (b > (const unsigned char *)p && (*b & 0xC0) == 0x80); utf8_next(&b, &prev); }
      for (;;) {
        const unsigned char *at = r;
        if (!*r || *r == '\n') { e = (const char *)r; last = 1; break; }
        utf8_next(&r, &cp);
        if ((const char *)at > p && (const char *)at > q) {
          if (cp == ' ') { e = (const char *)at; break; }
          if ((is_cjk(cp) || is_cjk(prev)) && prev != ' ' && !no_break_before(cp) && !no_break_after(prev)) { e = (const char *)at; break; }
        }
        prev = cp;
      }
      size_t len = (size_t)(e - p); if (len >= sizeof line) len = sizeof line - 1;
      memcpy(line, p, len); line[len] = 0;
      while (len > 0 && line[len - 1] == ' ') line[--len] = 0;
      if (text_w(f, line) > maxw && best) break;
      if (!best && text_w(f, line) > maxw) {
        // parola più larga della riga: per thai e CJK si spezza tra i caratteri
        int wide = 0; const unsigned char *u = (const unsigned char *)line; Uint32 c2;
        while (utf8_next(&u, &c2)) if (c2 >= 0x0E00) wide = 1;
        if (wide) {
          size_t cut = 0; const unsigned char *v = (const unsigned char *)line;
          while (*v) {
            const unsigned char *nx = v; utf8_next(&nx, &c2);
            char tmp[512]; size_t l2 = (size_t)(nx - (const unsigned char *)line); memcpy(tmp, line, l2); tmp[l2] = 0;
            if (cut && text_w(f, tmp) > maxw) break;
            cut = l2; v = nx;
          }
          best = cut; break;
        }
      }
      best = len;
      if (last) break;
      q = e; while (*q == ' ') q++;
      if (!*q || *q == '\n') break;
    }
    if (!best) break;
    const char *next = p + best;
    if (lines + 1 == maxlines && *next && *next != '\n' && strspn(next, " ") != strlen(next)) {
      *fit = 1; size_t l = strlen(p); if (o + l + 1 > outn) break; memcpy(out + o, p, l); out[o + l] = 0; o += l + 1;
    } else {
      size_t l = best; if (o + l + 1 > outn) break; memcpy(out + o, p, l); out[o + l] = 0; o += l + 1;
    }
    lines++;
    p = next; while (*p == ' ') p++; if (*p == '\n') p++;
  }
  return lines;
}

// a capo per parole; ritorna le righe usate. Con AL_C x è il centro di ogni riga.
int draw_text_wrap_al(TTF_Font *f, const char *s, int x, int y, int maxw, int maxlines, int lineh, Col c, int alpha, int align) {
  if (!s || !*s) return 0;
  Uint32 h = fnv(s, f, (Uint32)(maxw * 64 + maxlines));
  WREntry *e = &wr[h % WR_SIZE];
  if (!(e->s && e->f == f && e->h == h && e->maxw == maxw && e->maxlines == maxlines && !strcmp(e->s, s))) {
    // le righe sono pezzi di s: bastano strlen(s) byte più un terminatore per riga
    size_t cap = strlen(s) + (size_t)maxlines + 2; int fit = 0, n = 0;
    free(e->s); free(e->lines);
    e->lines = malloc(cap);
    if (e->lines) n = wrap_lines(f, s, maxw, maxlines, e->lines, cap, &fit);
    e->s = strdup(s); e->f = f; e->h = h; e->maxw = maxw; e->maxlines = maxlines; e->n = n; e->fit = fit;
  }
  const char *l = e->lines;
  for (int i = 0; i < e->n; i++) {
    if (i == e->n - 1 && e->fit) draw_text_fit(f, l, x, y, maxw, c, alpha, align);
    else draw_text(f, l, x, y, c, alpha, align);
    y += lineh; l += strlen(l) + 1;
  }
  return e->n;
}
int draw_text_wrap(TTF_Font *f, const char *s, int x, int y, int maxw, int maxlines, int lineh, Col c, int alpha) {
  return draw_text_wrap_al(f, s, x, y, maxw, maxlines, lineh, c, alpha, AL_L);
}

// -------------------------------------------------------------------- forme --
static void setc(SDL_Texture *t, Col c, int alpha) {
  SDL_SetTextureColorMod(t, c.r, c.g, c.b);
  SDL_SetTextureAlphaMod(t, (Uint8)(alpha < 0 ? 0 : alpha > 255 ? 255 : alpha));
}

// Veli scuri grandi (dietro i pannelli): con la scena congelata (main.c) sono già
// dentro la fotografia della scena e non si ridisegnano; mentre la si fotografa
// si annotano per applicarli una volta sola.
int g_veil_skip, g_veil_rec;
Veil g_veils[8]; int g_nveils;
void fill_rect(int x, int y, int w, int h, Col c, int alpha) {
  if (w <= 0 || h <= 0 || alpha <= 0) return;
  if (alpha < 255 && (long)w * h >= (long)SCREEN_W * SCREEN_H * 2 / 5) {
    if (g_veil_skip) return;
    if (g_veil_rec && g_nveils < 8) g_veils[g_nveils++] = (Veil){ x, y, w, h, c, alpha };
  }
  SDL_SetRenderDrawBlendMode(R, alpha >= 255 ? SDL_BLENDMODE_NONE : SDL_BLENDMODE_BLEND);
  SDL_SetRenderDrawColor(R, c.r, c.g, c.b, (Uint8)(alpha > 255 ? 255 : alpha));
  SDL_Rect rc = { x, y, w, h }; SDL_RenderFillRect(R, &rc);
}

// Texture bianche con copertura antialias, in cache per raggio.
typedef struct { int key; SDL_Texture *t; } TexSlot;
static TexSlot disc_cache[96], ring_cache[64], soft_cache[32];

static SDL_Texture *make_alpha_tex(int w, int h, float (*cov)(float x, float y, void *u), void *u) {
  SDL_Surface *s = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_ARGB8888);
  if (!s) return NULL;
  Uint32 *px = s->pixels; int pitch = s->pitch / 4;
  for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) {
    float a = cov((float)x + 0.5f, (float)y + 0.5f, u);
    a = a < 0 ? 0 : a > 1 ? 1 : a;
    px[y * pitch + x] = ((Uint32)(a * 255.0f + 0.5f) << 24) | 0xFFFFFF;
  }
  SDL_Texture *t = SDL_CreateTextureFromSurface(R, s);
  SDL_FreeSurface(s);
  if (t) { SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND); SDL_SetTextureScaleMode(t, SDL_ScaleModeLinear); }
  return t;
}

typedef struct { float r, t; } RingP;
static float cov_disc(float x, float y, void *u) { float r = *(float *)u; return r + 0.5f - hypotf(x - r, y - r); }
static float cov_ring(float x, float y, void *u) {
  RingP *p = u; float d = hypotf(x - p->r, y - p->r);
  float o = p->r + 0.5f - d, i = d - (p->r - p->t) + 0.5f;
  return o < i ? o : i;
}
static float cov_soft(float x, float y, void *u) {
  float r = *(float *)u; float d = hypotf(x - r, y - r) / r;
  if (d >= 1) return 0;
  float v = 1 - d; return v * v * (3 - 2 * v) * 0.9f;      // smoothstep
}

static SDL_Texture *cached(TexSlot *cache, int n, int key, SDL_Texture *(*mk)(int)) {
  for (int i = 0; i < n; i++) if (cache[i].t && cache[i].key == key) return cache[i].t;
  for (int i = 0; i < n; i++) if (!cache[i].t) { cache[i].key = key; cache[i].t = mk(key); return cache[i].t; }
  SDL_DestroyTexture(cache[n - 1].t); cache[n - 1].key = key; cache[n - 1].t = mk(key); return cache[n - 1].t;
}
static SDL_Texture *mk_disc(int r) { float rf = (float)r; return make_alpha_tex(r * 2, r * 2, cov_disc, &rf); }
static SDL_Texture *mk_ring(int key) { RingP p = { (float)(key >> 8), (float)(key & 255) }; int r = key >> 8; return make_alpha_tex(r * 2, r * 2, cov_ring, &p); }
static SDL_Texture *mk_soft(int r) { float rf = (float)r; return make_alpha_tex(r * 2, r * 2, cov_soft, &rf); }

// i quattro quarti di una texture circolare di raggio r agli angoli del rettangolo
static void corners(SDL_Texture *t, int x, int y, int w, int h, int r) {
  SDL_Rect s, d;
  s = (SDL_Rect){ 0, 0, r, r };      d = (SDL_Rect){ x, y, r, r };                 SDL_RenderCopy(R, t, &s, &d);
  s = (SDL_Rect){ r, 0, r, r };      d = (SDL_Rect){ x + w - r, y, r, r };         SDL_RenderCopy(R, t, &s, &d);
  s = (SDL_Rect){ 0, r, r, r };      d = (SDL_Rect){ x, y + h - r, r, r };         SDL_RenderCopy(R, t, &s, &d);
  s = (SDL_Rect){ r, r, r, r };      d = (SDL_Rect){ x + w - r, y + h - r, r, r }; SDL_RenderCopy(R, t, &s, &d);
}

void fill_rrect(int x, int y, int w, int h, int r, Col c, int alpha) {
  if (w <= 0 || h <= 0 || alpha <= 0) return;
  r = (int)(r * pref_corner_k());   // Personalizza › Angoli
  if (r * 2 > w) r = w / 2;
  if (r * 2 > h) r = h / 2;
  if (r < 2) { fill_rect(x, y, w, h, c, alpha); return; }
  SDL_Texture *t = cached(disc_cache, 96, r, mk_disc);
  if (!t) { fill_rect(x, y, w, h, c, alpha); return; }
  setc(t, c, alpha);
  corners(t, x, y, w, h, r);
  fill_rect(x + r, y, w - 2 * r, h, c, alpha);
  fill_rect(x, y + r, r, h - 2 * r, c, alpha);
  fill_rect(x + w - r, y + r, r, h - 2 * r, c, alpha);
}

void stroke_rrect(int x, int y, int w, int h, int r, int t, Col c, int alpha) {
  if (w <= 0 || h <= 0 || alpha <= 0) return;
  r = (int)(r * pref_corner_k());
  if (r * 2 > w) r = w / 2;
  if (r * 2 > h) r = h / 2;
  if (r < t + 1) r = t + 1;
  SDL_Texture *tx = cached(ring_cache, 64, (r << 8) | t, mk_ring);
  if (!tx) return;
  setc(tx, c, alpha);
  corners(tx, x, y, w, h, r);
  fill_rect(x + r, y, w - 2 * r, t, c, alpha);
  fill_rect(x + r, y + h - t, w - 2 * r, t, c, alpha);
  fill_rect(x, y + r, t, h - 2 * r, c, alpha);
  fill_rect(x + w - t, y + r, t, h - 2 * r, c, alpha);
}

// ombra morbida: angoli sfumati e bordi stirati da una riga o colonna della texture
void shadow_rrect(int x, int y, int w, int h, int r, int spread, int alpha) {
  r = (int)(r * pref_corner_k());
  int R2 = r + spread; if (R2 < 4) R2 = 4;
  if (R2 > 120) R2 = 120;
  SDL_Texture *t = cached(soft_cache, 32, R2, mk_soft);
  if (!t) return;
  setc(t, C_BLACK, alpha);
  int X = x - spread, Y = y - spread + spread / 3, W = w + spread * 2, H = h + spread * 2;
  if (W < 2 * R2) W = 2 * R2;
  if (H < 2 * R2) H = 2 * R2;
  corners(t, X, Y, W, H, R2);
  SDL_Rect s, d;
  s = (SDL_Rect){ R2, 0, 1, R2 };  d = (SDL_Rect){ X + R2, Y, W - 2 * R2, R2 };          SDL_RenderCopy(R, t, &s, &d);
  s = (SDL_Rect){ R2, R2, 1, R2 }; d = (SDL_Rect){ X + R2, Y + H - R2, W - 2 * R2, R2 }; SDL_RenderCopy(R, t, &s, &d);
  s = (SDL_Rect){ 0, R2, R2, 1 };  d = (SDL_Rect){ X, Y + R2, R2, H - 2 * R2 };          SDL_RenderCopy(R, t, &s, &d);
  s = (SDL_Rect){ R2, R2, R2, 1 }; d = (SDL_Rect){ X + W - R2, Y + R2, R2, H - 2 * R2 }; SDL_RenderCopy(R, t, &s, &d);
  fill_rect(X + R2, Y + R2, W - 2 * R2, H - 2 * R2, C_BLACK, alpha * 9 / 10);
}

void fill_circle(int cx, int cy, int rad, Col c, int alpha) {
  if (rad < 1 || alpha <= 0) return;
  int key = rad <= 64 ? rad : (rad <= 128 ? 128 : 256);    // i cerchi grandi si scalano da 128 o 256
  SDL_Texture *t = cached(disc_cache, 96, key, mk_disc);
  if (!t) return;
  setc(t, c, alpha);
  SDL_Rect d = { cx - rad, cy - rad, rad * 2, rad * 2 };
  SDL_RenderCopy(R, t, NULL, &d);
}

void ring(int cx, int cy, int rad, int t, Col c, int alpha) {
  if (rad < 2 || alpha <= 0) return;
  if (t > 255) t = 255;
  SDL_Texture *tx = cached(ring_cache, 64, (rad << 8) | t, mk_ring);
  if (!tx) return;
  setc(tx, c, alpha);
  SDL_Rect d = { cx - rad, cy - rad, rad * 2, rad * 2 };
  SDL_RenderCopy(R, tx, NULL, &d);
}

void glow(int cx, int cy, int rad, Col c, int alpha) {
  SDL_Texture *t = cached(soft_cache, 32, 64, mk_soft);
  if (!t) return;
  setc(t, c, alpha);
  SDL_SetTextureBlendMode(t, SDL_BLENDMODE_ADD);
  SDL_Rect d = { cx - rad, cy - rad, rad * 2, rad * 2 };
  SDL_RenderCopy(R, t, NULL, &d);
  SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
}

// gradienti: texture 2x256 stirate con filtro lineare
static SDL_Texture *grad_tex(int horizontal) {
  static SDL_Texture *gt[2];
  if (gt[horizontal]) return gt[horizontal];
  int w = horizontal ? 256 : 2, h = horizontal ? 2 : 256;
  SDL_Surface *s = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_ARGB8888);
  Uint32 *px = s->pixels; int pitch = s->pitch / 4;
  for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) {
    int a = horizontal ? x : y;
    px[y * pitch + x] = ((Uint32)a << 24) | 0xFFFFFF;
  }
  gt[horizontal] = SDL_CreateTextureFromSurface(R, s);
  SDL_FreeSurface(s);
  SDL_SetTextureBlendMode(gt[horizontal], SDL_BLENDMODE_BLEND);
  SDL_SetTextureScaleMode(gt[horizontal], SDL_ScaleModeLinear);
  return gt[horizontal];
}

// gradiente tra due alpha dello stesso colore
static void grad_one(int x, int y, int w, int h, Col c, int a0, int a1, int horizontal) {
  if (w <= 0 || h <= 0) return;
  SDL_Texture *t = grad_tex(horizontal);
  // la texture va da alpha 0 a 255: se ne usa il tratto [lo, hi]
  int lo = a0 < a1 ? a0 : a1, hi = a0 < a1 ? a1 : a0;
  if (hi <= 0) return;
  setc(t, c, 255);
  SDL_SetTextureAlphaMod(t, 255);
  SDL_Rect s;
  if (horizontal) s = (SDL_Rect){ lo, 0, hi - lo > 0 ? hi - lo : 1, 2 };
  else s = (SDL_Rect){ 0, lo, 2, hi - lo > 0 ? hi - lo : 1 };
  SDL_Rect d = { x, y, w, h };
  SDL_RendererFlip fl = SDL_FLIP_NONE;
  if (a0 > a1) fl = horizontal ? SDL_FLIP_HORIZONTAL : SDL_FLIP_VERTICAL;
  SDL_RenderCopyEx(R, t, &s, &d, 0, NULL, fl);
}
void grad_v(int x, int y, int w, int h, Col top, int atop, Col bot, int abot) {
  (void)bot; grad_one(x, y, w, h, top, atop, abot, 0);
}
void grad_h(int x, int y, int w, int h, Col l, int al, Col r, int ar) {
  (void)r; grad_one(x, y, w, h, l, al, ar, 1);
}

void draw_tex(SDL_Texture *t, int x, int y, int w, int h, int alpha) {
  if (!t || alpha <= 0) return;
  SDL_SetTextureAlphaMod(t, (Uint8)(alpha > 255 ? 255 : alpha));
  SDL_Rect d = { x, y, w, h };
  SDL_RenderCopy(R, t, NULL, &d);
}

Col mix(Col a, Col b, float t) {
  t = clampf(t, 0, 1);
  return (Col){ (Uint8)(a.r + (b.r - a.r) * t), (Uint8)(a.g + (b.g - a.g) * t), (Uint8)(a.b + (b.b - a.b) * t), 255 };
}

// ------------------------------------------------------------------- avatar --
static const Col AV[16][2] = {
  {{0,112,243,255},{0,200,255,255}}, {{140,60,255,255},{255,80,200,255}}, {{255,90,60,255},{255,190,60,255}},
  {{20,180,120,255},{140,240,90,255}}, {{230,40,90,255},{255,130,110,255}}, {{40,60,200,255},{120,90,255,255}},
  {{0,150,160,255},{80,230,210,255}}, {{255,140,0,255},{255,220,90,255}}, {{90,90,110,255},{170,180,200,255}},
  {{200,30,160,255},{120,40,255,255}}, {{10,120,90,255},{0,200,160,255}}, {{180,50,40,255},{250,110,60,255}},
  {{60,130,255,255},{150,210,255,255}}, {{110,40,200,255},{40,170,255,255}}, {{220,170,0,255},{255,110,60,255}},
  {{30,30,40,255},{90,100,130,255}},
};
Col avatar_col(int i) { return AV[(i < 0 ? 0 : i) & 15][0]; }

static SDL_Texture *av_tex[16];
static SDL_Texture *avatar_base(int idx) {
  idx &= 15;
  if (av_tex[idx]) return av_tex[idx];
  const int S2 = 192; float r = S2 / 2.0f;
  SDL_Surface *s = SDL_CreateRGBSurfaceWithFormat(0, S2, S2, 32, SDL_PIXELFORMAT_ARGB8888);
  Uint32 *px = s->pixels; int pitch = s->pitch / 4;
  Col a = AV[idx][0], b = AV[idx][1];
  for (int y = 0; y < S2; y++) for (int x = 0; x < S2; x++) {
    float fx = x + 0.5f, fy = y + 0.5f;
    float cov = r + 0.5f - hypotf(fx - r, fy - r); cov = cov < 0 ? 0 : cov > 1 ? 1 : cov;
    float t = clampf((fx + fy) / (2.0f * S2), 0, 1);
    // luce in alto a sinistra
    float hl = 1.0f - clampf(hypotf(fx - S2 * 0.3f, fy - S2 * 0.25f) / (S2 * 0.8f), 0, 1);
    int R_ = (int)(a.r + (b.r - a.r) * t + 40 * hl * hl), G_ = (int)(a.g + (b.g - a.g) * t + 40 * hl * hl), B_ = (int)(a.b + (b.b - a.b) * t + 40 * hl * hl);
    if (R_ > 255) R_ = 255; if (G_ > 255) G_ = 255; if (B_ > 255) B_ = 255;
    px[y * pitch + x] = ((Uint32)(cov * 255) << 24) | ((Uint32)R_ << 16) | ((Uint32)G_ << 8) | (Uint32)B_;
  }
  av_tex[idx] = SDL_CreateTextureFromSurface(R, s);
  SDL_FreeSurface(s);
  SDL_SetTextureBlendMode(av_tex[idx], SDL_BLENDMODE_BLEND);
  SDL_SetTextureScaleMode(av_tex[idx], SDL_ScaleModeLinear);
  return av_tex[idx];
}

void draw_avatar_color(const char *oid, int avatar, int cx, int cy, int size, int alpha) {
  SDL_Texture *t = avatar_base(avatar);
  if (t) { SDL_SetTextureColorMod(t, 255, 255, 255); draw_tex(t, cx - size / 2, cy - size / 2, size, size, alpha); }
  char ini[8] = "?";
  if (oid && oid[0]) { ini[0] = (char)((oid[0] >= 'a' && oid[0] <= 'z') ? oid[0] - 32 : oid[0]); ini[1] = 0; }
  int fs = size * 52 / 100; if (fs < 12) fs = 12;
  TTF_Font *f = font(W_MED, fs);
  int th = TTF_FontHeight(f);
  draw_text(f, ini, cx, cy - th / 2, C_WHITE, alpha, AL_C);
}

void draw_badge(int cx, int cy, int n, int alpha) {
  if (n <= 0) return;
  char b[8]; snprintf(b, sizeof b, n > 99 ? "99+" : "%d", n);
  TTF_Font *f = font(W_BOLD, 18);
  int w = text_w(f, b) + 14; if (w < 26) w = 26;
  fill_rrect(cx - w / 2, cy - 13, w, 26, 13, C_ACC, alpha);
  draw_text(f, b, cx, cy - TTF_FontHeight(f) / 2, C_WHITE, alpha, AL_C);
}

void draw_spinner(int cx, int cy, int rad, int alpha) {
  for (int i = 0; i < 8; i++) {
    float a = (float)(g_time * 7.0) - i * 0.55f;
    float k = (float)i / 8.0f;
    fill_circle(cx + (int)(cosf(a) * rad), cy + (int)(sinf(a) * rad), rad / 5 + 1, C_WHITE, (int)(alpha * (1.0f - k)));
  }
}

int pill(int x, int y, int h, const char *label, int icon, int focused, float fa, int alpha) {
  TTF_Font *f = font(W_MED, h * 42 / 100);
  int tw = text_w(f, label), pad = h * 55 / 100, isz = icon >= 0 ? h * 45 / 100 : 0;
  int w = tw + pad * 2 + (icon >= 0 ? isz + 12 : 0);
  if (!label || !*label) w = h;
  Col bg = mix(RGB(48, 54, 72), C_WHITE, fa);
  Col fg = mix(C_TXT, RGB(10, 12, 20), fa);
  if (focused) shadow_rrect(x, y, w, h, h / 2, 16, alpha * 50 / 100);
  fill_rrect(x, y, w, h, h / 2, bg, focused ? alpha : alpha * 85 / 100);
  int cx = x + pad;
  if (!label || !*label) { draw_icon(icon, x + w / 2, y + h / 2, isz, fg, alpha); return w; }
  if (icon >= 0) { draw_icon(icon, cx + isz / 2, y + h / 2, isz, fg, alpha); cx += isz + 12; }
  draw_text(f, label, cx, y + (h - TTF_FontHeight(f)) / 2, fg, alpha, AL_L);
  return w;
}

// Personalizza › Selezione: anello bianco, anello colorato, bagliore, entrambi
void focus_ring(int x, int y, int w, int h, int r, float pulse, int alpha) {
  int a = (int)(alpha * (0.75f + 0.25f * pulse));
  int st = g_prefs.focus;
  if (st >= 2) {   // bagliore del colore d'accento dietro l'elemento, che respira
    int gr = (w > h ? w : h) * 3 / 4;
    glow(x + w / 2, y + h / 2, gr + (int)(10 * pulse), C_ACC, (int)(alpha * (0.30f + 0.18f * pulse)));
  }
  if (st != 2) stroke_rrect(x - 6, y - 6, w + 12, h + 12, r + 6, st == 1 ? 5 : 4, st == 1 || st == 3 ? C_ACC2 : C_WHITE, a);
}

// -------------------------------------------------------------- icone (SDF) --
// Ogni icona è un insieme di primitive in coordinate 0..1, rasterizzato una
// volta a 128 px con bordo antialias e poi scalato.
typedef struct { int op; float a, b, c, d, e, f; } Prim;    // op: 1 disco, 2 anello, 3 segmento, 4 box, 5 triangolo, 9 togli disco, 10 togli box, 11 arco
#define P_DISC(x, y, r)          { 1, x, y, r, 0, 0, 0 }
#define P_RING(x, y, r, w)       { 2, x, y, r, w, 0, 0 }
#define P_SEG(x1, y1, x2, y2, w) { 3, x1, y1, x2, y2, w, 0 }
#define P_BOX(x, y, w, h, r)     { 4, x, y, w, h, r, 0 }
#define P_TRI(x1, y1, x2, y2, x3, y3) { 5, x1, y1, x2, y2, x3, y3 }
#define P_SUBD(x, y, r)          { 9, x, y, r, 0, 0, 0 }
#define P_SUBB(x, y, w, h, r)    { 10, x, y, w, h, r, 0 }
#define P_ARC(x, y, r, w, a0, a1) { 11, x, y, r, w, a0, a1 }
#define P_END { 0, 0, 0, 0, 0, 0, 0 }

static const Prim ICONS[IC_COUNT][12] = {
  [IC_SEARCH]  = { P_RING(0.42f, 0.42f, 0.27f, 0.085f), P_SEG(0.62f, 0.62f, 0.84f, 0.84f, 0.05f), P_END },
  [IC_GEAR]    = { P_RING(0.5f, 0.5f, 0.27f, 0.11f), P_SEG(0.5f, 0.14f, 0.5f, 0.86f, 0.07f), P_SEG(0.14f, 0.5f, 0.86f, 0.5f, 0.07f),
                   P_SEG(0.245f, 0.245f, 0.755f, 0.755f, 0.07f), P_SEG(0.755f, 0.245f, 0.245f, 0.755f, 0.07f), P_SUBD(0.5f, 0.5f, 0.13f), P_END },
  [IC_BELL]    = { P_DISC(0.5f, 0.45f, 0.25f), P_BOX(0.25f, 0.45f, 0.5f, 0.25f, 0.0f), P_BOX(0.17f, 0.66f, 0.66f, 0.08f, 0.04f), P_DISC(0.5f, 0.8f, 0.08f), P_DISC(0.5f, 0.17f, 0.05f), P_END },
  [IC_FRIENDS] = { P_DISC(0.36f, 0.34f, 0.14f), P_BOX(0.12f, 0.55f, 0.48f, 0.3f, 0.14f), P_DISC(0.68f, 0.38f, 0.12f), P_BOX(0.56f, 0.58f, 0.34f, 0.27f, 0.12f), P_END },
  [IC_CHAT]    = { P_BOX(0.12f, 0.18f, 0.76f, 0.52f, 0.16f), P_TRI(0.26f, 0.62f, 0.46f, 0.66f, 0.24f, 0.86f), P_END },
  [IC_PARTY]   = { P_ARC(0.5f, 0.52f, 0.32f, 0.07f, 3.3f, 6.13f), P_BOX(0.14f, 0.5f, 0.17f, 0.3f, 0.07f), P_BOX(0.69f, 0.5f, 0.17f, 0.3f, 0.07f), P_END },
  [IC_POWER]   = { P_ARC(0.5f, 0.54f, 0.3f, 0.075f, -1.0f, 4.14f), P_SEG(0.5f, 0.14f, 0.5f, 0.5f, 0.045f), P_END },
  [IC_PLUS]    = { P_SEG(0.5f, 0.18f, 0.5f, 0.82f, 0.055f), P_SEG(0.18f, 0.5f, 0.82f, 0.5f, 0.055f), P_END },
  [IC_CHECK]   = { P_SEG(0.2f, 0.52f, 0.42f, 0.74f, 0.06f), P_SEG(0.42f, 0.74f, 0.82f, 0.3f, 0.06f), P_END },
  [IC_CLOSE]   = { P_SEG(0.24f, 0.24f, 0.76f, 0.76f, 0.055f), P_SEG(0.76f, 0.24f, 0.24f, 0.76f, 0.055f), P_END },
  [IC_ADDUSER] = { P_DISC(0.4f, 0.32f, 0.15f), P_BOX(0.14f, 0.55f, 0.52f, 0.3f, 0.15f), P_SEG(0.8f, 0.36f, 0.8f, 0.64f, 0.045f), P_SEG(0.66f, 0.5f, 0.94f, 0.5f, 0.045f), P_END },
  [IC_GAMEPAD] = { P_BOX(0.1f, 0.3f, 0.8f, 0.4f, 0.2f), P_SUBB(0.2f, 0.46f, 0.18f, 0.06f, 0.02f), P_SUBB(0.26f, 0.4f, 0.06f, 0.18f, 0.02f), P_SUBD(0.66f, 0.44f, 0.045f), P_SUBD(0.76f, 0.54f, 0.045f), P_END },
  [IC_NEWS]    = { P_BOX(0.14f, 0.16f, 0.72f, 0.68f, 0.08f), P_SUBB(0.24f, 0.26f, 0.24f, 0.2f, 0.02f), P_SUBB(0.54f, 0.28f, 0.22f, 0.05f, 0.02f), P_SUBB(0.54f, 0.4f, 0.22f, 0.05f, 0.02f), P_SUBB(0.24f, 0.56f, 0.52f, 0.05f, 0.02f), P_SUBB(0.24f, 0.67f, 0.4f, 0.05f, 0.02f), P_END },
  [IC_MORE]    = { P_DISC(0.24f, 0.5f, 0.075f), P_DISC(0.5f, 0.5f, 0.075f), P_DISC(0.76f, 0.5f, 0.075f), P_END },
  [IC_MIC]     = { P_BOX(0.38f, 0.12f, 0.24f, 0.46f, 0.12f), P_ARC(0.5f, 0.45f, 0.24f, 0.05f, 0.0f, 3.14f), P_SEG(0.5f, 0.72f, 0.5f, 0.86f, 0.035f), P_SEG(0.36f, 0.86f, 0.64f, 0.86f, 0.035f), P_END },
  [IC_MICOFF]  = { P_BOX(0.38f, 0.12f, 0.24f, 0.46f, 0.12f), P_ARC(0.5f, 0.45f, 0.24f, 0.05f, 0.0f, 3.14f), P_SEG(0.5f, 0.72f, 0.5f, 0.86f, 0.035f), P_SEG(0.18f, 0.16f, 0.82f, 0.84f, 0.045f), P_END },
  [IC_PLAY]    = { P_TRI(0.3f, 0.18f, 0.3f, 0.82f, 0.84f, 0.5f), P_END },
  [IC_USER]    = { P_DISC(0.5f, 0.34f, 0.17f), P_BOX(0.2f, 0.58f, 0.6f, 0.3f, 0.15f), P_END },
  [IC_BTN_X]   = { P_SEG(0.24f, 0.24f, 0.76f, 0.76f, 0.07f), P_SEG(0.76f, 0.24f, 0.24f, 0.76f, 0.07f), P_END },
  [IC_BTN_O]   = { P_RING(0.5f, 0.5f, 0.31f, 0.1f), P_END },
  [IC_BTN_TRI] = { P_SEG(0.5f, 0.2f, 0.2f, 0.74f, 0.06f), P_SEG(0.2f, 0.74f, 0.8f, 0.74f, 0.06f), P_SEG(0.8f, 0.74f, 0.5f, 0.2f, 0.06f), P_END },
  [IC_BTN_SQ]  = { P_BOX(0.2f, 0.2f, 0.6f, 0.6f, 0.04f), P_SUBB(0.31f, 0.31f, 0.38f, 0.38f, 0.0f), P_END },
  [IC_BTN_OPT] = { P_SEG(0.2f, 0.3f, 0.8f, 0.3f, 0.05f), P_SEG(0.2f, 0.5f, 0.8f, 0.5f, 0.05f), P_SEG(0.2f, 0.7f, 0.8f, 0.7f, 0.05f), P_END },
  [IC_ARROW_R] = { P_SEG(0.36f, 0.2f, 0.66f, 0.5f, 0.06f), P_SEG(0.66f, 0.5f, 0.36f, 0.8f, 0.06f), P_END },
  [IC_SEND]    = { P_TRI(0.14f, 0.16f, 0.9f, 0.5f, 0.14f, 0.84f), P_SUBB(0.1f, 0.47f, 0.4f, 0.06f, 0.0f), P_END },
  [IC_EXIT]    = { P_BOX(0.14f, 0.14f, 0.42f, 0.72f, 0.06f), P_SUBB(0.22f, 0.22f, 0.26f, 0.56f, 0.02f), P_SEG(0.44f, 0.5f, 0.86f, 0.5f, 0.05f), P_SEG(0.7f, 0.34f, 0.86f, 0.5f, 0.05f), P_SEG(0.7f, 0.66f, 0.86f, 0.5f, 0.05f), P_END },
  [IC_CLOCK]   = { P_RING(0.5f, 0.5f, 0.36f, 0.08f), P_SEG(0.5f, 0.5f, 0.5f, 0.28f, 0.045f), P_SEG(0.5f, 0.5f, 0.66f, 0.58f, 0.045f), P_END },
  [IC_GLOBE]   = { P_RING(0.5f, 0.5f, 0.36f, 0.06f), P_SEG(0.16f, 0.5f, 0.84f, 0.5f, 0.03f), P_SEG(0.5f, 0.15f, 0.5f, 0.85f, 0.03f),
                   P_ARC(0.86f, 0.5f, 0.43f, 0.05f, 2.45f, 3.83f), P_ARC(0.14f, 0.5f, 0.43f, 0.05f, -0.69f, 0.69f), P_SEG(0.24f, 0.3f, 0.76f, 0.3f, 0.025f), P_SEG(0.24f, 0.7f, 0.76f, 0.7f, 0.025f), P_END },
  [IC_BACK]    = { P_SEG(0.64f, 0.2f, 0.34f, 0.5f, 0.06f), P_SEG(0.34f, 0.5f, 0.64f, 0.8f, 0.06f), P_END },
  [IC_FWD]     = { P_SEG(0.36f, 0.2f, 0.66f, 0.5f, 0.06f), P_SEG(0.66f, 0.5f, 0.36f, 0.8f, 0.06f), P_END },
  [IC_RELOAD]  = { P_ARC(0.5f, 0.5f, 0.3f, 0.07f, -1.2f, 4.0f), P_TRI(0.62f, 0.08f, 0.84f, 0.26f, 0.58f, 0.36f), P_END },
  [IC_STAR]    = { P_TRI(0.5f, 0.1f, 0.62f, 0.42f, 0.38f, 0.42f), P_TRI(0.1f, 0.4f, 0.9f, 0.4f, 0.5f, 0.66f), P_TRI(0.5f, 0.6f, 0.22f, 0.9f, 0.36f, 0.5f), P_TRI(0.5f, 0.6f, 0.78f, 0.9f, 0.64f, 0.5f), P_END },
  [IC_STORE]   = { P_BOX(0.2f, 0.38f, 0.6f, 0.5f, 0.07f), P_ARC(0.5f, 0.4f, 0.16f, 0.055f, 3.14159f, 6.28319f), P_SUBB(0.3f, 0.5f, 0.1f, 0.3f, 0.02f), P_END },
  [IC_DOWNLOAD] = { P_SEG(0.5f, 0.12f, 0.5f, 0.6f, 0.06f), P_SEG(0.3f, 0.42f, 0.5f, 0.64f, 0.06f), P_SEG(0.7f, 0.42f, 0.5f, 0.64f, 0.06f), P_SEG(0.2f, 0.82f, 0.8f, 0.82f, 0.05f), P_END },
  [IC_LIKE]    = { P_BOX(0.34f, 0.44f, 0.5f, 0.44f, 0.08f), P_BOX(0.46f, 0.12f, 0.2f, 0.4f, 0.1f), P_BOX(0.12f, 0.5f, 0.17f, 0.38f, 0.04f), P_END },
  [IC_DISLIKE] = { P_BOX(0.34f, 0.12f, 0.5f, 0.44f, 0.08f), P_BOX(0.46f, 0.48f, 0.2f, 0.4f, 0.1f), P_BOX(0.12f, 0.12f, 0.17f, 0.38f, 0.04f), P_END },
  [IC_MUSIC]   = { P_DISC(0.3f, 0.74f, 0.12f), P_DISC(0.72f, 0.66f, 0.12f), P_SEG(0.4f, 0.74f, 0.4f, 0.22f, 0.04f), P_SEG(0.82f, 0.66f, 0.82f, 0.14f, 0.04f), P_SEG(0.4f, 0.22f, 0.82f, 0.14f, 0.06f), P_END },
  [IC_PAUSE]   = { P_BOX(0.24f, 0.18f, 0.18f, 0.64f, 0.05f), P_BOX(0.58f, 0.18f, 0.18f, 0.64f, 0.05f), P_END },
  [IC_NEXT]    = { P_TRI(0.16f, 0.2f, 0.16f, 0.8f, 0.64f, 0.5f), P_BOX(0.68f, 0.2f, 0.13f, 0.6f, 0.03f), P_END },
  [IC_PREV]    = { P_TRI(0.84f, 0.2f, 0.84f, 0.8f, 0.36f, 0.5f), P_BOX(0.19f, 0.2f, 0.13f, 0.6f, 0.03f), P_END },
  [IC_SHUFFLE] = { P_SEG(0.12f, 0.3f, 0.36f, 0.3f, 0.045f), P_SEG(0.36f, 0.3f, 0.62f, 0.7f, 0.045f), P_SEG(0.62f, 0.7f, 0.8f, 0.7f, 0.045f),
                   P_SEG(0.12f, 0.7f, 0.36f, 0.7f, 0.045f), P_SEG(0.36f, 0.7f, 0.62f, 0.3f, 0.045f), P_SEG(0.62f, 0.3f, 0.8f, 0.3f, 0.045f),
                   P_TRI(0.76f, 0.18f, 0.92f, 0.3f, 0.76f, 0.42f), P_TRI(0.76f, 0.58f, 0.92f, 0.7f, 0.76f, 0.82f), P_END },
  [IC_REPEAT]  = { P_SEG(0.2f, 0.62f, 0.2f, 0.36f, 0.045f), P_SEG(0.2f, 0.36f, 0.74f, 0.36f, 0.045f), P_TRI(0.7f, 0.22f, 0.86f, 0.36f, 0.7f, 0.5f),
                   P_SEG(0.8f, 0.38f, 0.8f, 0.64f, 0.045f), P_SEG(0.8f, 0.64f, 0.26f, 0.64f, 0.045f), P_TRI(0.3f, 0.5f, 0.14f, 0.64f, 0.3f, 0.78f), P_END },
  [IC_RADIO]   = { P_BOX(0.12f, 0.36f, 0.76f, 0.5f, 0.08f), P_SUBD(0.35f, 0.61f, 0.12f), P_SEG(0.24f, 0.34f, 0.7f, 0.12f, 0.035f), P_SUBB(0.56f, 0.5f, 0.2f, 0.05f, 0.02f), P_SUBB(0.56f, 0.64f, 0.2f, 0.05f, 0.02f), P_END },
  [IC_FOLDER]  = { P_BOX(0.1f, 0.3f, 0.8f, 0.52f, 0.06f), P_BOX(0.1f, 0.2f, 0.32f, 0.16f, 0.05f), P_END },
  [IC_VOLUME]  = { P_BOX(0.12f, 0.38f, 0.16f, 0.24f, 0.02f), P_TRI(0.2f, 0.5f, 0.5f, 0.16f, 0.5f, 0.84f), P_ARC(0.5f, 0.5f, 0.16f, 0.045f, -0.9f, 0.9f), P_ARC(0.5f, 0.5f, 0.3f, 0.045f, -0.9f, 0.9f), P_END },
  [IC_ALBUM]   = { P_RING(0.5f, 0.5f, 0.3f, 0.12f), P_DISC(0.5f, 0.5f, 0.06f), P_END },
  // disco esterno: corpo con la spia e la fessura
  [IC_DRIVE]   = { P_BOX(0.1f, 0.3f, 0.8f, 0.4f, 0.08f), P_SUBD(0.74f, 0.5f, 0.06f), P_SUBB(0.2f, 0.47f, 0.36f, 0.06f, 0.02f), P_END },
  // pennello (Personalizza)
  [IC_BRUSH]   = { P_SEG(0.78f, 0.16f, 0.42f, 0.56f, 0.07f), P_DISC(0.32f, 0.68f, 0.14f), P_TRI(0.18f, 0.7f, 0.18f, 0.88f, 0.36f, 0.86f), P_END },
};

float sd_seg(float px, float py, float ax, float ay, float bx, float by) {
  float pax = px - ax, pay = py - ay, bax = bx - ax, bay = by - ay;
  float h = clampf((pax * bax + pay * bay) / (bax * bax + bay * bay + 1e-9f), 0, 1);
  return hypotf(pax - bax * h, pay - bay * h);
}
float sd_box(float px, float py, float x, float y, float w, float h, float r) {
  float cx = x + w / 2, cy = y + h / 2;
  float qx = fabsf(px - cx) - w / 2 + r, qy = fabsf(py - cy) - h / 2 + r;
  float ox = qx > 0 ? qx : 0, oy = qy > 0 ? qy : 0;
  return hypotf(ox, oy) + fminf(fmaxf(qx, qy), 0) - r;
}
float sd_tri(float px, float py, float x0, float y0, float x1, float y1, float x2, float y2) {
  float x[3] = { x0, x1, x2 }, y[3] = { y0, y1, y2 };
  float d = 1e9f; int inside = 1; float sgn0 = 0;
  for (int i = 0; i < 3; i++) {
    int j = (i + 1) % 3;
    d = fminf(d, sd_seg(px, py, x[i], y[i], x[j], y[j]));
    float cr = (x[j] - x[i]) * (py - y[i]) - (y[j] - y[i]) * (px - x[i]);
    if (i == 0) sgn0 = cr; else if ((cr > 0) != (sgn0 > 0)) inside = 0;
  }
  return inside ? -d : d;
}

static SDL_Texture *icon_tex[IC_COUNT];
static void build_icon(int id) {
  const int N = 128;
  SDL_Surface *s = SDL_CreateRGBSurfaceWithFormat(0, N, N, 32, SDL_PIXELFORMAT_ARGB8888);
  Uint32 *px = s->pixels; int pitch = s->pitch / 4;
  for (int y = 0; y < N; y++) for (int x = 0; x < N; x++) {
    float u = (x + 0.5f) / N, v = (y + 0.5f) / N, cov = 0;
    for (const Prim *p = ICONS[id]; p->op; p++) {
      float d;
      switch (p->op) {
        case 1: case 9: d = hypotf(u - p->a, v - p->b) - p->c; break;
        case 2: d = fabsf(hypotf(u - p->a, v - p->b) - (p->c - p->d / 2)) - p->d / 2; break;
        case 3: d = sd_seg(u, v, p->a, p->b, p->c, p->d) - p->e; break;
        case 4: case 10: d = sd_box(u, v, p->a, p->b, p->c, p->d, p->e); break;
        case 5: d = sd_tri(u, v, p->a, p->b, p->c, p->d, p->e, p->f); break;
        case 11: {
          float ang = atan2f(v - p->b, u - p->a);
          float a0 = p->e, a1 = p->f;
          while (ang < a0) ang += 6.2831853f;
          if (ang <= a1) d = fabsf(hypotf(u - p->a, v - p->b) - p->c) - p->d / 2;
          else {
            float ex0 = p->a + cosf(a0) * p->c, ey0 = p->b + sinf(a0) * p->c;
            float ex1 = p->a + cosf(a1) * p->c, ey1 = p->b + sinf(a1) * p->c;
            d = fminf(hypotf(u - ex0, v - ey0), hypotf(u - ex1, v - ey1)) - p->d / 2;
          }
          break;
        }
        default: d = 1;
      }
      float c = clampf(0.5f - d * N, 0, 1);
      if (p->op >= 9 && p->op <= 10) cov *= (1 - c); else cov = fmaxf(cov, c);
    }
    px[y * pitch + x] = ((Uint32)(cov * 255) << 24) | 0xFFFFFF;
  }
  icon_tex[id] = SDL_CreateTextureFromSurface(R, s);
  SDL_FreeSurface(s);
  SDL_SetTextureBlendMode(icon_tex[id], SDL_BLENDMODE_BLEND);
  SDL_SetTextureScaleMode(icon_tex[id], SDL_ScaleModeLinear);
}

void draw_icon(int id, int cx, int cy, int size, Col c, int alpha) {
  if (id < 0 || id >= IC_COUNT || alpha <= 0) return;
  if (!icon_tex[id]) build_icon(id);
  setc(icon_tex[id], c, alpha);
  SDL_Rect d = { cx - size / 2, cy - size / 2, size, size };
  SDL_RenderCopy(R, icon_tex[id], NULL, &d);
}

// ------------------------------------------------------------------ marchio --
// Il logo di Omega è un PNG incorporato (logo_png.c). Si preparano metà,
// quarti e ottavi con un filtro a box: si disegna dal livello più piccolo non
// inferiore alla dimensione chiesta, così resta nitido anche a 100 px.
extern const unsigned char omega_logo_png[];
extern const size_t omega_logo_png_len;
#define LOGO_MIPS 4
static SDL_Texture *logo_mip[LOGO_MIPS]; static int logo_size[LOGO_MIPS], logo_ready;

static SDL_Surface *half(SDL_Surface *s) {
  int w = s->w / 2, h = s->h / 2;
  SDL_Surface *d = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_ARGB8888);
  if (!d) return NULL;
  const Uint32 *sp = s->pixels; Uint32 *dp = d->pixels; int sw = s->pitch / 4, dw = d->pitch / 4;
  for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) {
    Uint32 q[4] = { sp[(2 * y) * sw + 2 * x], sp[(2 * y) * sw + 2 * x + 1], sp[(2 * y + 1) * sw + 2 * x], sp[(2 * y + 1) * sw + 2 * x + 1] };
    Uint32 A = 0, Rr = 0, G = 0, B = 0;
    for (int k = 0; k < 4; k++) { Uint32 a = q[k] >> 24; A += a; Rr += ((q[k] >> 16) & 255) * a; G += ((q[k] >> 8) & 255) * a; B += (q[k] & 255) * a; }
    // media pesata sull'alpha: niente aloni scuri sui bordi
    dp[y * dw + x] = A ? ((A / 4) << 24) | ((Rr / A) << 16) | ((G / A) << 8) | (B / A) : 0;
  }
  return d;
}

static void logo_init(void) {
  logo_ready = 1;
  SDL_RWops *rw = SDL_RWFromConstMem(omega_logo_png, (int)omega_logo_png_len);
  SDL_Surface *raw = rw ? IMG_Load_RW(rw, 1) : NULL;
  if (!raw) { omega_log("logo: %s", IMG_GetError()); return; }
  SDL_Surface *s = SDL_ConvertSurfaceFormat(raw, SDL_PIXELFORMAT_ARGB8888, 0);
  SDL_FreeSurface(raw);
  for (int i = 0; i < LOGO_MIPS && s; i++) {
    logo_mip[i] = SDL_CreateTextureFromSurface(R, s); logo_size[i] = s->w;
    if (logo_mip[i]) { SDL_SetTextureBlendMode(logo_mip[i], SDL_BLENDMODE_BLEND); SDL_SetTextureScaleMode(logo_mip[i], SDL_ScaleModeLinear); }
    SDL_Surface *n = i + 1 < LOGO_MIPS ? half(s) : NULL;
    SDL_FreeSurface(s); s = n;
  }
}

void draw_logo(int cx, int cy, int size, int alpha) {
  if (!logo_ready) logo_init();
  if (alpha <= 0 || size <= 0) return;
  int k = 0;
  while (k + 1 < LOGO_MIPS && logo_mip[k + 1] && logo_size[k + 1] >= size) k++;
  if (!logo_mip[k]) return;
  SDL_SetTextureColorMod(logo_mip[k], 255, 255, 255);
  draw_tex(logo_mip[k], cx - size / 2, cy - size / 2, size, size, alpha);
}

// --------------------------------------------------------------- animazioni --
float clampf(float v, float a, float b) { return v < a ? a : v > b ? b : v; }
float approach(float cur, float target, float speed) {
  float k = 1.0f - expf(-speed * pref_anim_k() * g_dt);   // Personalizza › Velocità delle animazioni
  float v = cur + (target - cur) * k;
  if (fabsf(target - v) < 0.0005f) v = target;
  return v;
}
float ease_out(float t) { t = clampf(t, 0, 1); float u = 1 - t; return 1 - u * u * u; }
float ease_in_out(float t) { t = clampf(t, 0, 1); return t < 0.5f ? 4 * t * t * t : 1 - powf(-2 * t + 2, 3) / 2; }

// ----------------------------------------------------------- sfondo animato --
#define BG_CACHE 3   // sfondi a schermo intero in memoria, 8 MB l'uno
typedef struct { char key[32]; SDL_Texture *t; Uint32 used; } BgSlot;
static BgSlot bgc[BG_CACHE];
static SDL_Texture *bg_cur, *bg_prev, *bg_default;
static float bg_fade = 1;
static char bg_want[32];

static void bg_switch(SDL_Texture *t) {
  if (t == bg_cur) return;
  bg_prev = bg_cur; bg_cur = t; bg_fade = 0;
}

static void bg_loaded(const char *key, SDL_Texture *t, SDL_Color avg, void *ud) {
  (void)avg; (void)ud;
  if (!t) return;
  int slot = 0;
  for (int i = 0; i < BG_CACHE; i++) {
    if (!bgc[i].t) { slot = i; break; }
    if (bgc[i].used < bgc[slot].used) slot = i;
  }
  if (bgc[slot].t) {
    if (bgc[slot].t == bg_cur || bgc[slot].t == bg_prev) {   // quella a schermo non si distrugge
      for (int i = 0; i < BG_CACHE; i++) if (bgc[i].t != bg_cur && bgc[i].t != bg_prev) { slot = i; break; }
    }
    if (bgc[slot].t && bgc[slot].t != bg_cur && bgc[slot].t != bg_prev) SDL_DestroyTexture(bgc[slot].t);
  }
  snprintf(bgc[slot].key, sizeof bgc[slot].key, "%s", key);
  bgc[slot].t = t; bgc[slot].used = g_frame;
  if (!strcmp(key, bg_want)) bg_switch(t);
}

// --------------------------------------------------------------------- temi --
typedef struct { const char *name; Col base, acc, acc2, particle; } Theme;
static const Theme THEMES[N_THEMES] = {
  { N_("Omega blu"), { 20, 70, 200, 255 },  { 0, 112, 243, 255 },  { 56, 160, 255, 255 }, { 120, 180, 255, 255 } },
  { N_("Notte"),     { 30, 30, 60, 255 },   { 90, 100, 255, 255 }, { 150, 160, 255, 255 }, { 170, 170, 230, 255 } },
  { N_("Aurora"),    { 0, 140, 120, 255 },  { 0, 170, 140, 255 },  { 60, 230, 190, 255 }, { 120, 255, 210, 255 } },
  { N_("Tramonto"),  { 200, 70, 40, 255 },  { 235, 90, 50, 255 },  { 255, 150, 90, 255 }, { 255, 190, 140, 255 } },
  { N_("Ametista"),  { 120, 40, 200, 255 }, { 140, 70, 240, 255 }, { 190, 130, 255, 255 }, { 210, 170, 255, 255 } },
  { N_("Carbone"),   { 60, 64, 72, 255 },   { 110, 120, 140, 255 }, { 180, 190, 210, 255 }, { 200, 205, 215, 255 } },
  { N_("Neon"),      { 120, 0, 160, 255 },  { 255, 40, 170, 255 }, { 0, 230, 255, 255 },   { 255, 90, 220, 255 } },
  { N_("Smeraldo"),  { 0, 110, 60, 255 },   { 20, 190, 100, 255 }, { 110, 240, 160, 255 }, { 150, 255, 190, 255 } },
  { N_("Rubino"),    { 150, 10, 40, 255 },  { 225, 30, 70, 255 },  { 255, 110, 130, 255 }, { 255, 150, 160, 255 } },
  { N_("Oro"),       { 150, 105, 20, 255 }, { 230, 170, 30, 255 }, { 255, 215, 110, 255 }, { 255, 225, 150, 255 } },
  { N_("Ghiaccio"),  { 70, 140, 190, 255 }, { 90, 190, 240, 255 }, { 190, 235, 255, 255 }, { 220, 245, 255, 255 } },
  { N_("Mezzanotte"),{ 8, 10, 18, 255 },    { 70, 110, 255, 255 }, { 140, 170, 255, 255 }, { 90, 110, 170, 255 } },
};
// colori d'accento scelti in Personalizza (0 = quello del tema)
static const Col ACCENTS[8][2] = {
  { { 0, 0, 0, 0 }, { 0, 0, 0, 0 } },
  { { 0, 140, 255, 255 }, { 90, 190, 255, 255 } },   // azzurro
  { { 140, 80, 255, 255 }, { 195, 150, 255, 255 } }, // viola
  { { 255, 60, 150, 255 }, { 255, 140, 200, 255 } }, // rosa
  { { 20, 190, 110, 255 }, { 110, 240, 170, 255 } }, // verde
  { { 255, 120, 30, 255 }, { 255, 180, 100, 255 } }, // arancio
  { { 230, 175, 30, 255 }, { 255, 220, 120, 255 } }, // oro
  { { 200, 205, 215, 255 }, { 255, 255, 255, 255 } }, // bianco
};
int g_theme = 0;
Col g_theme_base = { 20, 70, 200, 255 };

// velatura del tema: ora è dentro gli sfondi (bake_tint in async.c). Resta per
// chi la vuole sopra qualcos'altro.
void theme_tint(int alpha) {
  grad_v(0, 0, SCREEN_W, SCREEN_H * 45 / 100, g_theme_base, alpha * 42 / 100, g_theme_base, 0);
  grad_v(0, SCREEN_H * 70 / 100, SCREEN_W, SCREEN_H * 30 / 100, g_theme_base, 0, g_theme_base, alpha * 26 / 100);
}
static Col g_particle = { 120, 180, 255, 255 };
const char *theme_name(int i) { return _(THEMES[(i < 0 ? 0 : i) % N_THEMES].name); }

void theme_apply(int i, int save) {
  if (i < 0 || i >= N_THEMES) i = 0;
  g_theme = i; g_prefs.theme = i;
  const Theme *t = &THEMES[i];
  C_ACC = t->acc; C_ACC2 = t->acc2; g_particle = t->particle;
  if (g_prefs.accent > 0 && g_prefs.accent < 8) { C_ACC = ACCENTS[g_prefs.accent][0]; C_ACC2 = ACCENTS[g_prefs.accent][1]; }
  g_theme_base = t->base;
  C_PANEL = mix(RGB(20, 24, 36), t->base, 0.22f);
  SDL_Surface *s = NULL;
  if (g_prefs.bg_style == 2) {   // nero puro
    s = SDL_CreateRGBSurfaceWithFormat(0, SCREEN_W, SCREEN_H, 32, SDL_PIXELFORMAT_ARGB8888);
    if (s) SDL_FillRect(s, NULL, 0xFF000000u);
  } else {
    s = gen_ambient(t->base, SCREEN_W, SCREEN_H);
    if (s) { bake_tint(s, t->base); bake_dim(s); }
  }
  // gli sfondi dei giochi già in memoria hanno la velatura del tema vecchio: si ricaricano
  for (int k = 0; k < BG_CACHE; k++) if (bgc[k].t && bgc[k].t != bg_cur && bgc[k].t != bg_prev) { SDL_DestroyTexture(bgc[k].t); bgc[k].t = NULL; bgc[k].key[0] = 0; }
  if (s) {
    SDL_Texture *old = bg_default;
    bg_default = SDL_CreateTextureFromSurface(R, s); SDL_FreeSurface(s);
    if (bg_cur == old) bg_cur = bg_default;
    if (bg_prev == old) bg_prev = NULL;
    if (old) SDL_DestroyTexture(old);
  }
  if (save) {
    FILE *f = fopen(OMEGA_DIR "/theme.txt", "w");   // per le versioni di prima
    if (f) { fprintf(f, "%d\n", i); fclose(f); }
    g_prefs_rev++; prefs_save();
  }
}

void theme_load(void) {
  prefs_load();            // legge anche theme.txt delle versioni di prima
  theme_apply(g_prefs.theme, 0);
}

static void wall_request(void);
void bg_init(void) {
  theme_load();
  bg_cur = bg_default;
  if (g_prefs.bg_style == 3) wall_request();
}

// Immagine personale: /data/Omega/wallpaper.jpg (o .png), caricata come gli sfondi
static int wall_ok;
static void wall_loaded(const char *key, SDL_Texture *t, SDL_Color avg, void *ud) {
  (void)key; (void)avg; (void)ud;
  if (!t) { wall_ok = 0; return; }
  if (bg_default && bg_default != bg_cur && bg_default != bg_prev) SDL_DestroyTexture(bg_default);
  bg_default = t; wall_ok = 1;
  if (!bg_want[0]) bg_switch(bg_default);
}
static void wall_request(void) {
  const char *c[3] = { OMEGA_DIR "/wallpaper.jpg", OMEGA_DIR "/wallpaper.png", OMEGA_DIR "/wallpaper.jpeg" };
  for (int i = 0; i < 3; i++) if (access(c[i], 0) == 0) { load_req(LOAD_BG, "~wallpaper", c[i], SCREEN_W, SCREEN_H, 0, g_theme_base, wall_loaded, NULL); return; }
}

// Sfondo o oscuramento cambiati: si rifà lo sfondo del tema e si buttano quelli dei giochi
void bg_refresh(void) {
  for (int k = 0; k < BG_CACHE; k++) if (bgc[k].t && bgc[k].t != bg_cur && bgc[k].t != bg_prev) { SDL_DestroyTexture(bgc[k].t); bgc[k].t = NULL; bgc[k].key[0] = 0; }
  bg_want[0] = 0;
  theme_apply(g_theme, 0);
  bg_switch(bg_default);
  if (g_prefs.bg_style == 3) wall_request();
}

void bg_set_default(void) { bg_want[0] = 0; bg_switch(bg_default); }

void bg_set_game(const char *tid, const char *art, Col avg) {
  if (g_prefs.bg_style != 0) return;   // sfondo fisso: niente copertine
  if (!strcmp(bg_want, tid)) return;
  snprintf(bg_want, sizeof bg_want, "%s", tid);
  for (int i = 0; i < BG_CACHE; i++) if (bgc[i].t && !strcmp(bgc[i].key, tid)) { bgc[i].used = g_frame; bg_switch(bgc[i].t); return; }
  load_req(LOAD_BG, tid, art, SCREEN_W, SCREEN_H, 0, avg, bg_loaded, NULL);
}

void bg_draw(void) {
  bg_fade = approach(bg_fade, 1, 9.0f);
  if (bg_prev && bg_fade < 1) {
    SDL_SetTextureBlendMode(bg_prev, SDL_BLENDMODE_NONE);
    draw_tex(bg_prev, 0, 0, SCREEN_W, SCREEN_H, 255);
    if (bg_cur) { SDL_SetTextureBlendMode(bg_cur, SDL_BLENDMODE_BLEND); draw_tex(bg_cur, 0, 0, SCREEN_W, SCREEN_H, (int)(255 * bg_fade)); }
  } else if (bg_cur) {
    SDL_SetTextureBlendMode(bg_cur, SDL_BLENDMODE_NONE);
    draw_tex(bg_cur, 0, 0, SCREEN_W, SCREEN_H, 255);
    bg_prev = NULL;
  } else {
    fill_rect(0, 0, SCREEN_W, SCREEN_H, RGB(4, 8, 20), 255);
  }
}

// Particelle sullo sfondo (Personalizza › Particelle e Tipo di particelle):
// luci che salgono, stelle che brillano ferme, neve che scende, lucciole che
// vagano, bolle che salgono ondeggiando.
#define NP 96
static struct { float x, y, vy, vx, r, a, ph; } parts[NP];
static int parts_init, parts_style = -1;
int g_busy_motion;
int bg_fading(void) { return bg_prev && bg_fade < 1; }
static void part_reset(int i, int st, int anywhere) {
  parts[i].x = (float)(rand() % SCREEN_W);
  parts[i].y = anywhere ? (float)(rand() % SCREEN_H) : (st == 2 ? -40.0f : SCREEN_H + 40.0f);
  parts[i].ph = (float)(rand() % 628) / 100.0f;
  switch (st) {
    case 1: parts[i].vy = 0; parts[i].vx = 0; parts[i].r = 3 + (float)(rand() % 6); parts[i].a = 60 + (float)(rand() % 140); break;        // stelle
    case 2: parts[i].vy = -(20 + (float)(rand() % 40)); parts[i].vx = -10 + (float)(rand() % 20); parts[i].r = 4 + (float)(rand() % 9); parts[i].a = 90 + (float)(rand() % 90); break;   // neve (scende)
    case 3: parts[i].vy = 4 + (float)(rand() % 10); parts[i].vx = -14 + (float)(rand() % 28); parts[i].r = 8 + (float)(rand() % 10); parts[i].a = 60 + (float)(rand() % 120); break;   // lucciole
    case 4: parts[i].vy = 25 + (float)(rand() % 40); parts[i].vx = 0; parts[i].r = 10 + (float)(rand() % 26); parts[i].a = 40 + (float)(rand() % 60); break;   // bolle
    default: parts[i].vy = 8 + (float)(rand() % 30); parts[i].vx = -6 + (float)(rand() % 12); parts[i].r = 6 + (float)(rand() % 26); parts[i].a = 20 + (float)(rand() % 60); break;   // luci
  }
}
void particles_draw(int alpha) {
  static const int COUNT[4] = { 0, 20, 46, 96 };
  int n = COUNT[g_prefs.particles & 3], st = g_prefs.particle_style;
  if (!n || g_busy_motion > 0 || bg_fading()) return;
  if (!parts_init || parts_style != st) { for (int i = 0; i < NP; i++) part_reset(i, st, 1); parts_init = 1; parts_style = st; }
  static const Col SNOW = { 235, 242, 255, 255 }, FIRE = { 255, 220, 110, 255 };
  for (int i = 0; i < n; i++) {
    float tt = (float)g_time;
    parts[i].y -= parts[i].vy * g_dt; parts[i].x += parts[i].vx * g_dt;
    if (st == 3) { parts[i].vx += sinf(tt * 0.7f + parts[i].ph) * 6 * g_dt; parts[i].vy += cosf(tt * 0.5f + parts[i].ph) * 3 * g_dt; }
    if (st == 4) parts[i].x += sinf(tt * 1.5f + parts[i].ph) * 18 * g_dt;
    if (parts[i].y < -60 || parts[i].y > SCREEN_H + 60 || parts[i].x < -60 || parts[i].x > SCREEN_W + 60) part_reset(i, st, st == 1 || st == 3);
    float tw = st == 1 ? 0.35f + 0.65f * fabsf(sinf(tt * 1.8f + parts[i].ph)) : st == 3 ? 0.3f + 0.7f * fabsf(sinf(tt * 2.2f + parts[i].ph)) : 0.6f + 0.4f * sinf(tt * 1.3f + parts[i].ph);
    int a = (int)(parts[i].a * tw * alpha / 255);
    Col c = st == 2 ? SNOW : st == 3 ? FIRE : g_particle;
    if (st == 4) ring((int)parts[i].x, (int)parts[i].y, (int)parts[i].r, 2, c, a);
    else if (st == 2) fill_circle((int)parts[i].x, (int)parts[i].y, (int)(parts[i].r / 2), c, a);
    else glow((int)parts[i].x, (int)parts[i].y, (int)parts[i].r, c, a);
  }
}

void gfx_init(void) {
  SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "1");
  fonts_scan();
  logo_init();
  for (int i = 0; i < IC_COUNT; i++) build_icon(i);
  for (int i = 0; i < 16; i++) avatar_base(i);
  bg_init();
}
