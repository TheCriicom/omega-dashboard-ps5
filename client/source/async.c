// Omega UI — lavoro in background, perché la UI non si blocchi mai:
//  · coda di rete: due worker eseguono le richieste e il parsing JSON; le
//    callback girano sul thread principale (netq_pump);
//  · caricatore di immagini: un worker decodifica in superfici; le texture le
//    crea il thread principale (il renderer non è thread-safe).
#include "app.h"
#include <SDL_image.h>
#include <stdlib.h>
#include <string.h>

// ------------------------------------------------------------- coda di rete --
typedef struct NetJob {
  int method; char path[512]; char *body; char token[700];
  int gen; NetCb cb; void *ud;
  int status; char *raw; JVal *json;
  struct NetJob *next;
} NetJob;

static SDL_mutex *nq_mx; static SDL_cond *nq_cv;
static NetJob *nq_head, *nq_tail;    // in attesa
static NetJob *nd_head, *nd_tail;    // completate
int g_net_gen = 1;

static int net_worker(void *arg) {
  (void)arg;
  char *buf = malloc(NET_BIG);
  for (;;) {
    SDL_LockMutex(nq_mx);
    while (!nq_head) SDL_CondWait(nq_cv, nq_mx);
    NetJob *j = nq_head; nq_head = j->next; if (!nq_head) nq_tail = NULL;
    SDL_UnlockMutex(nq_mx);

    buf[0] = 0;
    j->status = omega_http(j->method, j->path, j->token[0] ? j->token : NULL, j->body, buf, NET_BIG);
    if (j->status < 0 || j->status >= 400) omega_log("net %d %s -> %d", j->method, j->path, j->status);
    j->raw = strdup(buf);
    j->json = json_parse(buf);
    j->next = NULL;

    SDL_LockMutex(nq_mx);
    if (nd_tail) nd_tail->next = j; else nd_head = j;
    nd_tail = j;
    SDL_UnlockMutex(nq_mx);
  }
  return 0;
}

void netq_init(void) {
  nq_mx = SDL_CreateMutex(); nq_cv = SDL_CreateCond();
  SDL_CreateThread(net_worker, "net0", NULL);
  SDL_CreateThread(net_worker, "net1", NULL);
}

void net_req(int method, const char *path, const char *body, NetCb cb, void *ud) {
  NetJob *j = calloc(1, sizeof *j);
  if (!j) return;
  j->method = method; snprintf(j->path, sizeof j->path, "%s", path);
  j->body = body ? strdup(body) : NULL;
  snprintf(j->token, sizeof j->token, "%s", g_token);
  j->gen = g_net_gen; j->cb = cb; j->ud = ud;
  SDL_LockMutex(nq_mx);
  if (nq_tail) nq_tail->next = j; else nq_head = j;
  nq_tail = j;
  SDL_CondSignal(nq_cv);
  SDL_UnlockMutex(nq_mx);
}

void netq_pump(void) {
  for (;;) {
    SDL_LockMutex(nq_mx);
    NetJob *j = nd_head;
    if (j) { nd_head = j->next; if (!nd_head) nd_tail = NULL; }
    SDL_UnlockMutex(nq_mx);
    if (!j) break;
    // le risposte di una sessione chiusa (logout) si scartano
    if (j->cb && j->gen == g_net_gen) j->cb(j->status, j->json, j->raw ? j->raw : "", j->ud);
    json_free(j->json); free(j->raw); free(j->body); free(j);
  }
}

// ------------------------------------------------------ caricatore immagini --
typedef struct LoadJob {
  int kind; char key[48]; char path[256]; int w, h, radius;
  SDL_Surface *out; SDL_Color avg;
  LoadCb cb; void *ud;
  struct LoadJob *next;
} LoadJob;

#define FIT_MAX_H   860
#define REMOTE_MAX  (4 * 1024 * 1024)
#define PUMP_PER_FRAME 4     // texture create per fotogramma: oltre si vedono scatti

static SDL_mutex *lq_mx; static SDL_cond *lq_cv;
static LoadJob *lq_head, *lq_tail, *ld_head, *ld_tail;

static SDL_Surface *to_argb(SDL_Surface *s) {
  if (!s) return NULL;
  SDL_Surface *c = SDL_ConvertSurfaceFormat(s, SDL_PIXELFORMAT_ARGB8888, 0);
  SDL_FreeSurface(s);
  return c;
}

// Scala con filtro bilineare (solo superfici: si può fare fuori dal thread principale).
static SDL_Surface *scaled(SDL_Surface *s, int w, int h) {
  if (!s) return NULL;
  if (s->w == w && s->h == h) return s;
  SDL_Surface *d = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_ARGB8888);
  if (!d) { SDL_FreeSurface(s); return NULL; }
  if (SDL_SoftStretchLinear(s, NULL, d, NULL) != 0) SDL_BlitScaled(s, NULL, d, NULL);
  SDL_FreeSurface(s);
  return d;
}

// Angoli arrotondati con bordo morbido; calcola anche il colore medio.
static void round_corners(SDL_Surface *s, int r, SDL_Color *avg) {
  SDL_LockSurface(s);
  Uint32 *px = s->pixels; int pitch = s->pitch / 4;
  unsigned long long sr = 0, sg = 0, sb = 0, n = 0;
  for (int y = 0; y < s->h; y++) for (int x = 0; x < s->w; x++) {
    Uint32 p = px[y * pitch + x];
    int a = (p >> 24) & 255;
    if ((x & 3) == 0 && (y & 3) == 0 && a > 128) { sr += (p >> 16) & 255; sg += (p >> 8) & 255; sb += p & 255; n++; }
    if (r <= 0) continue;
    float ccx = x < r ? (float)r : (x >= s->w - r ? (float)(s->w - r) : -1.0f);
    float ccy = y < r ? (float)r : (y >= s->h - r ? (float)(s->h - r) : -1.0f);
    if (ccx < 0 || ccy < 0) continue;
    float dx = (float)x + 0.5f - ccx, dy = (float)y + 0.5f - ccy;
    float d = SDL_sqrtf(dx * dx + dy * dy) - (float)r;
    float cov = 0.5f - d; if (cov < 0) cov = 0; if (cov > 1) cov = 1;
    a = (int)(a * cov);
    px[y * pitch + x] = (p & 0x00FFFFFF) | ((Uint32)a << 24);
  }
  SDL_UnlockSurface(s);
  if (avg) {
    if (n) { avg->r = (Uint8)(sr / n); avg->g = (Uint8)(sg / n); avg->b = (Uint8)(sb / n); }
    else { avg->r = 40; avg->g = 60; avg->b = 120; }
    avg->a = 255;
  }
}

// Solo gli angoli superiori (immagine in cima a una scheda).
static void round_top(SDL_Surface *s, int r) {
  if (r <= 0) return;
  SDL_LockSurface(s);
  Uint32 *px = s->pixels; int pitch = s->pitch / 4;
  for (int y = 0; y < r && y < s->h; y++) for (int x = 0; x < s->w; x++) {
    float ccx = x < r ? (float)r : (x >= s->w - r ? (float)(s->w - r) : -1.0f);
    if (ccx < 0) continue;
    float dx = x + 0.5f - ccx, dy = y + 0.5f - r;
    float cov = clampf((float)r + 0.5f - SDL_sqrtf(dx * dx + dy * dy), 0, 1);
    Uint32 p = px[y * pitch + x];
    px[y * pitch + x] = (p & 0x00FFFFFF) | ((Uint32)(((p >> 24) & 255) * cov) << 24);
  }
  SDL_UnlockSurface(s);
}

// Sfondo del gioco, scurito a sinistra e in basso per la leggibilità.
static void bake_vignette(SDL_Surface *s) {
  SDL_LockSurface(s);
  Uint32 *px = s->pixels; int pitch = s->pitch / 4;
  for (int y = 0; y < s->h; y++) {
    float fy = (float)y / s->h;
    float vb = fy < 0.35f ? 0.0f : (fy - 0.35f) / 0.65f;      // basso
    float vt = fy < 0.18f ? (0.18f - fy) / 0.18f * 0.5f : 0;  // fascia superiore (barra)
    for (int x = 0; x < s->w; x++) {
      float fx = (float)x / s->w;
      float vl = fx < 0.6f ? (0.6f - fx) / 0.6f : 0;          // sinistra
      float dark = 0.25f + 0.45f * vl * vl + 0.65f * vb * vb + vt;
      if (dark > 0.94f) dark = 0.94f;
      float k = 1.0f - dark;
      Uint32 p = px[y * pitch + x];
      int r = (int)(((p >> 16) & 255) * k), g = (int)(((p >> 8) & 255) * k), b = (int)((p & 255) * k);
      // leggera dominante blu notte, come la home console
      b += (int)(12 * dark); g += (int)(4 * dark);
      if (b > 255) b = 255;
      px[y * pitch + x] = 0xFF000000u | ((Uint32)r << 16) | ((Uint32)g << 8) | (Uint32)b;
    }
  }
  SDL_UnlockSurface(s);
}

// Sfondo generato: sfumatura dal colore medio della copertina + aloni di luce.
SDL_Surface *gen_ambient(SDL_Color base, int w, int h) {
  SDL_Surface *s = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_ARGB8888);
  if (!s) return NULL;
  SDL_LockSurface(s);
  Uint32 *px = s->pixels; int pitch = s->pitch / 4;
  float br = base.r / 255.0f, bg = base.g / 255.0f, bb = base.b / 255.0f;
  // satura e scurisci il colore base
  float mx = br > bg ? (br > bb ? br : bb) : (bg > bb ? bg : bb);
  if (mx < 0.05f) mx = 0.05f;
  br /= mx; bg /= mx; bb /= mx;
  for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) {
    float fx = (float)x / w, fy = (float)y / h;
    float d1 = (fx - 0.78f) * (fx - 0.78f) * 1.6f + (fy - 0.22f) * (fy - 0.22f) * 2.4f;
    float d2 = (fx - 0.25f) * (fx - 0.25f) * 2.0f + (fy - 0.95f) * (fy - 0.95f) * 3.0f;
    float l1 = 0.55f / (1.0f + d1 * 9.0f), l2 = 0.22f / (1.0f + d2 * 10.0f);
    float v = 0.05f + l1 + l2 * 0.6f;
    float wave = 0.025f * SDL_sinf(fx * 7.0f + fy * 3.0f) * (1.0f - fy);
    v += wave;
    int r = (int)(255 * (v * br * 0.85f + 0.012f)), g = (int)(255 * (v * bg * 0.85f + 0.02f)), b = (int)(255 * (v * bb * 0.95f + 0.05f));
    if (r > 255) r = 255; if (g > 255) g = 255; if (b > 255) b = 255;
    if (r < 0) r = 0; if (g < 0) g = 0; if (b < 0) b = 0;
    px[y * pitch + x] = 0xFF000000u | ((Uint32)r << 16) | ((Uint32)g << 8) | (Uint32)b;
  }
  SDL_UnlockSurface(s);
  return s;
}

static int load_worker(void *arg) {
  (void)arg;
  for (;;) {
    SDL_LockMutex(lq_mx);
    while (!lq_head) SDL_CondWait(lq_cv, lq_mx);
    LoadJob *j = lq_head; lq_head = j->next; if (!lq_head) lq_tail = NULL;
    SDL_UnlockMutex(lq_mx);

    SDL_Surface *s = NULL;
    if (j->kind == LOAD_ICON) {
      s = scaled(to_argb(IMG_Load(j->path)), j->w, j->h);
      if (s) round_corners(s, j->radius, &j->avg);
    } else if (j->kind == LOAD_URL) {
      // immagine dal server, ritagliata in modalità "cover" a w×h
      unsigned char *data = NULL; size_t len = 0;
      int st = omega_http_bin(j->path, g_token[0] ? g_token : NULL, &data, &len, REMOTE_MAX);
      if (st == 200 && data && len) {
        SDL_Surface *src = to_argb(IMG_Load_RW(SDL_RWFromConstMem(data, (int)len), 1));
        if (src && j->h <= 0) {
          // h <= 0: "fit" entro la larghezza j->w, proporzioni intatte (immagini del browser)
          int tw = src->w > j->w ? j->w : (src->w * 3 / 2 < j->w ? src->w * 3 / 2 : j->w);
          int th = src->h * tw / (src->w ? src->w : 1);
          if (th > FIT_MAX_H) { tw = tw * FIT_MAX_H / th; th = FIT_MAX_H; }
          if (tw < 1) tw = 1;
          if (th < 1) th = 1;
          s = SDL_CreateRGBSurfaceWithFormat(0, tw, th, 32, SDL_PIXELFORMAT_ARGB8888);
          if (s && SDL_SoftStretchLinear(src, NULL, s, NULL) != 0) SDL_BlitScaled(src, NULL, s, NULL);
          SDL_FreeSurface(src);
          if (s) round_corners(s, j->radius, NULL);
        } else if (src) {
          float sc = (float)j->w / src->w; if ((float)j->h / src->h > sc) sc = (float)j->h / src->h;
          int cw = (int)(j->w / sc), ch = (int)(j->h / sc);
          SDL_Rect crop = { (src->w - cw) / 2, (src->h - ch) / 3, cw, ch };
          s = SDL_CreateRGBSurfaceWithFormat(0, j->w, j->h, 32, SDL_PIXELFORMAT_ARGB8888);
          if (s && SDL_SoftStretchLinear(src, &crop, s, NULL) != 0) SDL_BlitScaled(src, &crop, s, NULL);
          SDL_FreeSurface(src);
          // raggio > 0: tutti gli angoli; < 0: solo quelli in alto
          if (s) { if (j->radius > 0) round_corners(s, j->radius, NULL); else round_top(s, -j->radius); }
        }
      }
      free(data);
    } else if (j->kind == LOAD_BG) {
      if (j->path[0]) s = scaled(to_argb(IMG_Load(j->path)), j->w, j->h);
      if (!s) s = gen_ambient(j->avg, j->w, j->h);
      if (s) bake_vignette(s);
    }
    j->out = s;
    j->next = NULL;
    SDL_LockMutex(lq_mx);
    if (ld_tail) ld_tail->next = j; else ld_head = j;
    ld_tail = j;
    SDL_UnlockMutex(lq_mx);
  }
  return 0;
}

void loader_init(void) {
  lq_mx = SDL_CreateMutex(); lq_cv = SDL_CreateCond();
  SDL_CreateThread(load_worker, "loader", NULL);
}

void load_req(int kind, const char *key, const char *path, int w, int h, int radius, SDL_Color avg, LoadCb cb, void *ud) {
  LoadJob *j = calloc(1, sizeof *j);
  if (!j) return;
  j->kind = kind; snprintf(j->key, sizeof j->key, "%s", key ? key : "");
  snprintf(j->path, sizeof j->path, "%s", path ? path : "");
  j->w = w; j->h = h; j->radius = radius; j->avg = avg; j->cb = cb; j->ud = ud;
  SDL_LockMutex(lq_mx);
  if (lq_tail) lq_tail->next = j; else lq_head = j;
  lq_tail = j;
  SDL_CondSignal(lq_cv);
  SDL_UnlockMutex(lq_mx);
}

void loader_pump(void) {
  for (int k = 0; k < PUMP_PER_FRAME; k++) {
    SDL_LockMutex(lq_mx);
    LoadJob *j = ld_head;
    if (j) { ld_head = j->next; if (!ld_head) ld_tail = NULL; }
    SDL_UnlockMutex(lq_mx);
    if (!j) break;
    SDL_Texture *t = NULL;
    if (j->out) { t = SDL_CreateTextureFromSurface(R, j->out); SDL_FreeSurface(j->out); }
    if (t && (j->kind == LOAD_ICON || j->kind == LOAD_URL)) { SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND); SDL_SetTextureScaleMode(t, SDL_ScaleModeLinear); }
    if (j->cb) j->cb(j->key, t, j->avg, j->ud); else if (t) SDL_DestroyTexture(t);
    free(j);
  }
}
