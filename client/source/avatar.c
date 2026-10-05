// Omega UI — avatar:
//  · personalizzati: foto o breve video caricati dall'utente (fotogrammi JPEG
//    dal server), animati quando l'avatar è grande;
//  · illustrati: 32 personaggi vettoriali (SDF a colori) rasterizzati una
//    volta a 256 px;
//  · colore e iniziale (gfx.c) come ripiego.
#include "app.h"
#include <math.h>
#include <stdlib.h>

#define MEDIA_REG 160
#define MEDIA_TEX 200

// ----------------------------------------------- avatar personalizzati noti --
typedef struct { char oid[32]; char media[20]; int frames; Uint32 used; } MediaReg;
static MediaReg reg[MEDIA_REG];

void media_note(const char *oid, const char *media, int frames) {
  if (!oid || !oid[0]) return;
  MediaReg *slot = NULL, *old = &reg[0];
  for (int i = 0; i < MEDIA_REG; i++) {
    if (reg[i].oid[0] && !strcasecmp(reg[i].oid, oid)) { slot = &reg[i]; break; }
    if (!reg[i].oid[0] && !slot) slot = &reg[i];
    if (reg[i].used < old->used) old = &reg[i];
  }
  if (!slot) slot = old;
  snprintf(slot->oid, sizeof slot->oid, "%s", oid);
  snprintf(slot->media, sizeof slot->media, "%s", media && frames > 0 ? media : "");
  slot->frames = media && media[0] ? frames : 0;
  slot->used = g_frame;
}

void media_note_json(const char *oid, JVal *o) {
  if (!o || !jget(o, "avatar_media")) return;      // campo assente: dato sconosciuto, non si tocca
  media_note(oid, jstr(o, "avatar_media", ""), (int)jnum(o, "avatar_frames", 0));
}

const char *media_of(const char *oid, int *frames) {
  if (!oid || !oid[0]) return NULL;
  for (int i = 0; i < MEDIA_REG; i++) if (reg[i].oid[0] && !strcasecmp(reg[i].oid, oid)) {
    reg[i].used = g_frame;
    if (frames) *frames = reg[i].frames;
    return reg[i].media[0] ? reg[i].media : NULL;
  }
  return NULL;
}

// ------------------------------------------------------ fotogrammi in cache --
typedef struct { char key[48]; SDL_Texture *t; int state; Uint32 used; } MTex;   // state: 1 in arrivo, 2 pronta, 3 assente
static MTex mt[MEDIA_TEX];

static void on_mtex(const char *key, SDL_Texture *t, SDL_Color avg, void *ud) {
  (void)avg; (void)ud;
  for (int i = 0; i < MEDIA_TEX; i++) if (mt[i].state == 1 && !strcmp(mt[i].key, key)) { mt[i].t = t; mt[i].state = t ? 2 : 3; return; }
  if (t) SDL_DestroyTexture(t);
}

static MTex *mtex(const char *media, int n, int tw, int th, int radius) {
  char key[48]; snprintf(key, sizeof key, "%s:%d:%dx%d:%d", media, n, tw, th, radius);
  MTex *victim = NULL;
  for (int i = 0; i < MEDIA_TEX; i++) {
    if (mt[i].state && !strcmp(mt[i].key, key)) { mt[i].used = g_frame; return &mt[i]; }
    if (mt[i].state == 1) continue;
    if (!victim || !mt[i].state || (victim->state && mt[i].used < victim->used)) victim = &mt[i];
  }
  if (!victim) return NULL;
  if (victim->t) SDL_DestroyTexture(victim->t);
  memset(victim, 0, sizeof *victim);
  snprintf(victim->key, sizeof victim->key, "%s", key);
  victim->state = 1; victim->used = g_frame;
  char path[96]; snprintf(path, sizeof path, OMEGA_API "/media/%s/%d", media, n);
  load_req(LOAD_URL, key, path, tw, th, radius, RGB(0, 0, 0), on_mtex, NULL);
  return victim;
}

// Disegna il fotogramma corrente (10 al secondo); 0 se non c'è ancora niente di pronto.
int draw_media_frames(const char *media, int frames, int x, int y, int w, int h, int tw, int th, int radius, int animate, int alpha) {
  if (!media || !media[0] || frames <= 0) return 0;
  int n = animate && frames > 1 ? 1 + ((int)(g_time * 10.0) % frames) : 1;
  MTex *m = mtex(media, n, tw, th, radius);
  if (!(m && m->state == 2)) {
    m = mtex(media, 1, tw, th, radius);           // nel frattempo il primo fotogramma
    if (!(m && m->state == 2)) return 0;
  }
  draw_tex(m->t, x, y, w, h, alpha);
  return 1;
}

// -------------------------------------------------------- avatar illustrati --
// Primitive colorate in coordinate 0..1, come le icone di gfx.c.
enum { A_END, A_DISC, A_ELL, A_SEG, A_BOX, A_TRI };
typedef struct { int op; float a, b, c, d, e, f; Uint32 col; } APrim;
#define C(hex) 0x##hex
#define DISC(x, y, r, c)           { A_DISC, x, y, r, 0, 0, 0, C(c) }
#define ELL(x, y, rx, ry, c)       { A_ELL, x, y, rx, ry, 0, 0, C(c) }
#define SEG(x1, y1, x2, y2, w, c)  { A_SEG, x1, y1, x2, y2, w, 0, C(c) }
#define BOX(x, y, w, h, r, c)      { A_BOX, x, y, w, h, r, 0, C(c) }
#define TRI(a, b, c2, d, e, f, c)  { A_TRI, a, b, c2, d, e, f, C(c) }
#define AEND { A_END, 0, 0, 0, 0, 0, 0, 0 }

typedef struct { Uint32 bg1, bg2; APrim p[24]; } AvatarArt;

static const AvatarArt ART[AV_ART_COUNT] = {
  // 0 astronauta
  { C(1b2a6b), C(3d7bff), { BOX(0.24f, 0.74f, 0.52f, 0.36f, 0.14f, e8ecf4), DISC(0.5f, 0.47f, 0.27f, f4f6fb), ELL(0.5f, 0.46f, 0.19f, 0.13f, 14203f),
    ELL(0.43f, 0.41f, 0.06f, 0.03f, 7fb2ff), DISC(0.31f, 0.47f, 0.04f, c9d2e6), DISC(0.69f, 0.47f, 0.04f, c9d2e6), BOX(0.44f, 0.82f, 0.12f, 0.06f, 0.02f, ff7a3d), AEND } },
  // 1 robot
  { C(ff7a3d), C(ffb347), { SEG(0.5f, 0.24f, 0.5f, 0.12f, 0.018f, 5a6274), DISC(0.5f, 0.11f, 0.045f, ff3d5a), BOX(0.27f, 0.74f, 0.46f, 0.34f, 0.08f, 8c95a8),
    BOX(0.23f, 0.24f, 0.54f, 0.46f, 0.1f, c8d0dc), BOX(0.3f, 0.33f, 0.4f, 0.2f, 0.06f, 2b3140), DISC(0.4f, 0.43f, 0.055f, 4fe3ff), DISC(0.6f, 0.43f, 0.055f, 4fe3ff),
    BOX(0.38f, 0.58f, 0.24f, 0.04f, 0.02f, 5a6274), BOX(0.17f, 0.4f, 0.07f, 0.14f, 0.03f, 8c95a8), BOX(0.76f, 0.4f, 0.07f, 0.14f, 0.03f, 8c95a8), AEND } },
  // 2 gatto
  { C(8b4dff), C(ff6fb5), { TRI(0.26f, 0.42f, 0.3f, 0.14f, 0.46f, 0.32f, ff9f43), TRI(0.74f, 0.42f, 0.7f, 0.14f, 0.54f, 0.32f, ff9f43),
    TRI(0.3f, 0.36f, 0.32f, 0.21f, 0.41f, 0.3f, ffd2a8), TRI(0.7f, 0.36f, 0.68f, 0.21f, 0.59f, 0.3f, ffd2a8), BOX(0.26f, 0.76f, 0.48f, 0.34f, 0.16f, ff9f43),
    DISC(0.5f, 0.53f, 0.26f, ffad5c), ELL(0.5f, 0.64f, 0.13f, 0.09f, fff1e0), ELL(0.4f, 0.5f, 0.05f, 0.065f, 2ecc71), ELL(0.6f, 0.5f, 0.05f, 0.065f, 2ecc71),
    ELL(0.4f, 0.5f, 0.018f, 0.05f, 102010), ELL(0.6f, 0.5f, 0.018f, 0.05f, 102010), TRI(0.47f, 0.6f, 0.53f, 0.6f, 0.5f, 0.635f, ff6f91),
    SEG(0.3f, 0.62f, 0.16f, 0.6f, 0.006f, 5a3a20), SEG(0.7f, 0.62f, 0.84f, 0.6f, 0.006f, 5a3a20), AEND } },
  // 3 panda
  { C(16a085), C(7bed9f), { DISC(0.29f, 0.3f, 0.09f, 1c1c24), DISC(0.71f, 0.3f, 0.09f, 1c1c24), BOX(0.25f, 0.76f, 0.5f, 0.34f, 0.16f, 1c1c24),
    DISC(0.5f, 0.52f, 0.27f, f7f7f7), ELL(0.39f, 0.5f, 0.07f, 0.09f, 1c1c24), ELL(0.61f, 0.5f, 0.07f, 0.09f, 1c1c24), DISC(0.4f, 0.49f, 0.025f, ffffff),
    DISC(0.6f, 0.49f, 0.025f, ffffff), ELL(0.5f, 0.62f, 0.05f, 0.035f, 1c1c24), SEG(0.5f, 0.65f, 0.5f, 0.69f, 0.008f, 1c1c24), AEND } },
  // 4 volpe
  { C(0f8b8d), C(46d9c4), { TRI(0.22f, 0.44f, 0.26f, 0.12f, 0.44f, 0.3f, ff6b2c), TRI(0.78f, 0.44f, 0.74f, 0.12f, 0.56f, 0.3f, ff6b2c),
    TRI(0.27f, 0.36f, 0.29f, 0.2f, 0.38f, 0.3f, 3a1f12), TRI(0.73f, 0.36f, 0.71f, 0.2f, 0.62f, 0.3f, 3a1f12), BOX(0.27f, 0.76f, 0.46f, 0.34f, 0.16f, ff6b2c),
    DISC(0.5f, 0.5f, 0.25f, ff7a3d), TRI(0.26f, 0.52f, 0.74f, 0.52f, 0.5f, 0.78f, fff3e6), DISC(0.5f, 0.71f, 0.035f, 2a1a10),
    ELL(0.4f, 0.48f, 0.03f, 0.045f, 2a1a10), ELL(0.6f, 0.48f, 0.03f, 0.045f, 2a1a10), AEND } },
  // 5 alieno
  { C(3b1d7a), C(9b59ff), { SEG(0.4f, 0.26f, 0.32f, 0.1f, 0.012f, 7bed7b), SEG(0.6f, 0.26f, 0.68f, 0.1f, 0.012f, 7bed7b), DISC(0.32f, 0.1f, 0.035f, d6ff6b),
    DISC(0.68f, 0.1f, 0.035f, d6ff6b), BOX(0.32f, 0.76f, 0.36f, 0.34f, 0.15f, 5fd35f), ELL(0.5f, 0.48f, 0.23f, 0.28f, 7bed7b),
    ELL(0.4f, 0.47f, 0.075f, 0.1f, 101820), ELL(0.6f, 0.47f, 0.075f, 0.1f, 101820), DISC(0.38f, 0.43f, 0.02f, ffffff), DISC(0.58f, 0.43f, 0.02f, ffffff),
    SEG(0.45f, 0.64f, 0.55f, 0.64f, 0.01f, 2e6b2e), AEND } },
  // 6 ninja
  { C(b0172f), C(ff4757), { BOX(0.26f, 0.76f, 0.48f, 0.34f, 0.16f, 22252e), DISC(0.5f, 0.5f, 0.27f, 2a2e38), BOX(0.24f, 0.42f, 0.52f, 0.15f, 0.06f, f2c9a0),
    ELL(0.41f, 0.49f, 0.045f, 0.03f, 20140c), ELL(0.59f, 0.49f, 0.045f, 0.03f, 20140c), BOX(0.24f, 0.31f, 0.52f, 0.07f, 0.03f, e8283c),
    SEG(0.74f, 0.34f, 0.88f, 0.26f, 0.025f, e8283c), SEG(0.74f, 0.35f, 0.86f, 0.42f, 0.025f, e8283c), AEND } },
  // 7 fantasma
  { C(141e46), C(3a4f9c), { BOX(0.27f, 0.36f, 0.46f, 0.42f, 0.04f, f4f7ff), DISC(0.5f, 0.38f, 0.23f, f4f7ff), DISC(0.32f, 0.78f, 0.06f, f4f7ff),
    DISC(0.44f, 0.8f, 0.06f, f4f7ff), DISC(0.56f, 0.78f, 0.06f, f4f7ff), DISC(0.68f, 0.8f, 0.06f, f4f7ff), ELL(0.42f, 0.4f, 0.04f, 0.06f, 1b2040),
    ELL(0.58f, 0.4f, 0.04f, 0.06f, 1b2040), ELL(0.5f, 0.55f, 0.04f, 0.05f, 1b2040), ELL(0.36f, 0.5f, 0.035f, 0.02f, ffb3c6), ELL(0.64f, 0.5f, 0.035f, 0.02f, ffb3c6), AEND } },
  // 8 pinguino
  { C(5ab0ff), C(c6ecff), { ELL(0.5f, 0.6f, 0.26f, 0.34f, 1d2433), ELL(0.5f, 0.66f, 0.17f, 0.25f, ffffff), DISC(0.42f, 0.42f, 0.055f, ffffff),
    DISC(0.58f, 0.42f, 0.055f, ffffff), DISC(0.43f, 0.43f, 0.025f, 101010), DISC(0.57f, 0.43f, 0.025f, 101010), TRI(0.44f, 0.5f, 0.56f, 0.5f, 0.5f, 0.58f, ffa21f),
    ELL(0.33f, 0.88f, 0.08f, 0.04f, ffa21f), ELL(0.67f, 0.88f, 0.08f, 0.04f, ffa21f), AEND } },
  // 9 gufo
  { C(4a2c17), C(a0652f), { TRI(0.24f, 0.32f, 0.3f, 0.12f, 0.4f, 0.28f, 6b4226), TRI(0.76f, 0.32f, 0.7f, 0.12f, 0.6f, 0.28f, 6b4226),
    ELL(0.5f, 0.58f, 0.3f, 0.34f, 8a5a33), ELL(0.5f, 0.7f, 0.17f, 0.2f, d9b38c), DISC(0.38f, 0.44f, 0.11f, fdf6e3), DISC(0.62f, 0.44f, 0.11f, fdf6e3),
    DISC(0.38f, 0.44f, 0.065f, ffc93c), DISC(0.62f, 0.44f, 0.065f, ffc93c), DISC(0.38f, 0.44f, 0.035f, 1a1208), DISC(0.62f, 0.44f, 0.035f, 1a1208),
    TRI(0.46f, 0.52f, 0.54f, 0.52f, 0.5f, 0.6f, ff9f1c), AEND } },
  // 10 teschio
  { C(1a0a0a), C(8e1b1b), { DISC(0.5f, 0.44f, 0.26f, f1ece0), BOX(0.36f, 0.58f, 0.28f, 0.2f, 0.06f, f1ece0), ELL(0.41f, 0.46f, 0.065f, 0.075f, 1a0a0a),
    ELL(0.59f, 0.46f, 0.065f, 0.075f, 1a0a0a), TRI(0.47f, 0.6f, 0.53f, 0.6f, 0.5f, 0.54f, 1a0a0a), SEG(0.42f, 0.7f, 0.42f, 0.77f, 0.012f, 1a0a0a),
    SEG(0.5f, 0.7f, 0.5f, 0.77f, 0.012f, 1a0a0a), SEG(0.58f, 0.7f, 0.58f, 0.77f, 0.012f, 1a0a0a), DISC(0.41f, 0.46f, 0.02f, ff3d3d), DISC(0.59f, 0.46f, 0.02f, ff3d3d), AEND } },
  // 11 cavaliere
  { C(2c3e50), C(7f8c8d), { TRI(0.5f, 0.2f, 0.56f, 0.06f, 0.66f, 0.12f, e74c3c), TRI(0.5f, 0.2f, 0.62f, 0.02f, 0.72f, 0.2f, ff6b5b),
    BOX(0.3f, 0.2f, 0.4f, 0.5f, 0.18f, c0c7cf), BOX(0.34f, 0.4f, 0.32f, 0.05f, 0.02f, 1c232b), SEG(0.5f, 0.47f, 0.5f, 0.62f, 0.012f, 8a939e),
    BOX(0.26f, 0.72f, 0.48f, 0.36f, 0.12f, 9aa3ad), BOX(0.47f, 0.74f, 0.06f, 0.3f, 0.02f, f1c40f), AEND } },
  // 12 rana
  { C(c9d100), C(f5ff6b), { DISC(0.37f, 0.32f, 0.1f, 4cbb4c), DISC(0.63f, 0.32f, 0.1f, 4cbb4c), ELL(0.5f, 0.56f, 0.3f, 0.24f, 4cbb4c),
    DISC(0.37f, 0.31f, 0.065f, ffffff), DISC(0.63f, 0.31f, 0.065f, ffffff), DISC(0.38f, 0.32f, 0.03f, 101010), DISC(0.62f, 0.32f, 0.03f, 101010),
    SEG(0.36f, 0.6f, 0.64f, 0.6f, 0.012f, 1f5c1f), ELL(0.3f, 0.56f, 0.04f, 0.025f, ff8fab), ELL(0.7f, 0.56f, 0.04f, 0.025f, ff8fab),
    ELL(0.5f, 0.86f, 0.2f, 0.08f, 3fa83f), AEND } },
  // 13 orso
  { C(6d4c41), C(c08457), { DISC(0.3f, 0.3f, 0.09f, 7b4f2c), DISC(0.7f, 0.3f, 0.09f, 7b4f2c), DISC(0.3f, 0.3f, 0.045f, d9a066),
    DISC(0.7f, 0.3f, 0.045f, d9a066), BOX(0.26f, 0.76f, 0.48f, 0.34f, 0.16f, 7b4f2c), DISC(0.5f, 0.52f, 0.26f, 8d5b34), ELL(0.5f, 0.62f, 0.11f, 0.08f, e0b384),
    ELL(0.5f, 0.59f, 0.045f, 0.03f, 2a1a10), DISC(0.41f, 0.47f, 0.025f, 1a0f08), DISC(0.59f, 0.47f, 0.025f, 1a0f08), AEND } },
  // 14 mascotte controller
  { C(0050c8), C(00a2ff), { BOX(0.14f, 0.36f, 0.72f, 0.32f, 0.16f, f5f7fb), ELL(0.27f, 0.66f, 0.12f, 0.12f, f5f7fb), ELL(0.73f, 0.66f, 0.12f, 0.12f, f5f7fb),
    BOX(0.24f, 0.47f, 0.12f, 0.04f, 0.01f, 3b4252), BOX(0.28f, 0.43f, 0.04f, 0.12f, 0.01f, 3b4252), DISC(0.66f, 0.44f, 0.028f, 2ed573),
    DISC(0.72f, 0.5f, 0.028f, ff4757), DISC(0.6f, 0.5f, 0.028f, ffa502), DISC(0.66f, 0.56f, 0.028f, 1e90ff), DISC(0.42f, 0.6f, 0.045f, 3b4252),
    DISC(0.58f, 0.6f, 0.045f, 3b4252), BOX(0.44f, 0.4f, 0.12f, 0.06f, 0.02f, cfd6e2), AEND } },
  // 15 fiamma
  { C(3a0d0d), C(b3261e), { TRI(0.5f, 0.1f, 0.24f, 0.7f, 0.76f, 0.7f, ff6b1a), DISC(0.5f, 0.66f, 0.26f, ff6b1a), TRI(0.34f, 0.28f, 0.26f, 0.62f, 0.46f, 0.6f, ff8c2a),
    TRI(0.68f, 0.3f, 0.56f, 0.6f, 0.76f, 0.62f, ff8c2a), TRI(0.5f, 0.32f, 0.34f, 0.76f, 0.66f, 0.76f, ffc21a), DISC(0.5f, 0.74f, 0.15f, ffc21a),
    DISC(0.5f, 0.78f, 0.08f, fff3b0), DISC(0.44f, 0.62f, 0.022f, 3a0d0d), DISC(0.56f, 0.62f, 0.022f, 3a0d0d), AEND } },
  // 16 drago
  { C(1d0b3a), C(6c2bd9), { TRI(0.33f, 0.31f, 0.25f, 0.1f, 0.42f, 0.26f, f1e3c2), TRI(0.67f, 0.31f, 0.75f, 0.1f, 0.58f, 0.26f, f1e3c2),
    BOX(0.31f, 0.7f, 0.38f, 0.4f, 0.14f, 1e8449), TRI(0.43f, 0.29f, 0.5f, 0.15f, 0.57f, 0.29f, f39c12), ELL(0.5f, 0.47f, 0.24f, 0.21f, 27ae60),
    ELL(0.5f, 0.62f, 0.17f, 0.11f, 58d68d), DISC(0.45f, 0.6f, 0.017f, 145a32), DISC(0.55f, 0.6f, 0.017f, 145a32),
    ELL(0.4f, 0.43f, 0.055f, 0.06f, fff3a0), ELL(0.6f, 0.43f, 0.055f, 0.06f, fff3a0), ELL(0.4f, 0.43f, 0.014f, 0.045f, 1a1a1a), ELL(0.6f, 0.43f, 0.014f, 0.045f, 1a1a1a),
    SEG(0.43f, 0.8f, 0.57f, 0.8f, 0.012f, f7dc6f), SEG(0.44f, 0.88f, 0.56f, 0.88f, 0.012f, f7dc6f), SEG(0.44f, 0.68f, 0.56f, 0.68f, 0.008f, 145a32), AEND } },
  // 17 unicorno
  { C(8e7dff), C(ff9ff3), { DISC(0.34f, 0.4f, 0.11f, ff6b9d), DISC(0.3f, 0.55f, 0.1f, feca57), DISC(0.33f, 0.7f, 0.09f, 48dbfb), DISC(0.4f, 0.82f, 0.08f, a29bfe),
    BOX(0.38f, 0.6f, 0.28f, 0.5f, 0.12f, ffffff), TRI(0.43f, 0.37f, 0.46f, 0.21f, 0.52f, 0.34f, ffffff), ELL(0.55f, 0.48f, 0.2f, 0.17f, ffffff),
    TRI(0.5f, 0.33f, 0.6f, 0.31f, 0.63f, 0.07f, ffd700), SEG(0.53f, 0.27f, 0.6f, 0.24f, 0.006f, e1a800), SEG(0.55f, 0.19f, 0.61f, 0.17f, 0.006f, e1a800),
    ELL(0.66f, 0.56f, 0.11f, 0.09f, ffe4f2), DISC(0.7f, 0.55f, 0.012f, d98cb3), ELL(0.53f, 0.45f, 0.03f, 0.04f, 2d1b4e), DISC(0.54f, 0.435f, 0.011f, ffffff),
    ELL(0.58f, 0.54f, 0.032f, 0.018f, ffb3d9), AEND } },
  // 18 squalo
  { C(0a3d62), C(3c9ad6), { TRI(0.5f, 0.06f, 0.37f, 0.33f, 0.63f, 0.31f, 5d7b93), ELL(0.5f, 0.56f, 0.33f, 0.28f, 7f98ab), ELL(0.5f, 0.7f, 0.25f, 0.15f, eef4f7),
    BOX(0.33f, 0.63f, 0.34f, 0.09f, 0.045f, 3b0f17), TRI(0.36f, 0.63f, 0.41f, 0.63f, 0.385f, 0.68f, ffffff), TRI(0.43f, 0.63f, 0.48f, 0.63f, 0.455f, 0.68f, ffffff),
    TRI(0.52f, 0.63f, 0.57f, 0.63f, 0.545f, 0.68f, ffffff), TRI(0.59f, 0.63f, 0.64f, 0.63f, 0.615f, 0.68f, ffffff),
    DISC(0.37f, 0.47f, 0.038f, 101820), DISC(0.63f, 0.47f, 0.038f, 101820), DISC(0.38f, 0.46f, 0.011f, ffffff), DISC(0.64f, 0.46f, 0.011f, ffffff),
    SEG(0.23f, 0.5f, 0.25f, 0.6f, 0.008f, 4e6a80), SEG(0.77f, 0.5f, 0.75f, 0.6f, 0.008f, 4e6a80), DISC(0.78f, 0.24f, 0.025f, a9d8f5), DISC(0.84f, 0.32f, 0.016f, a9d8f5), AEND } },
  // 19 polpo
  { C(0f6e6e), C(48c9b0), { ELL(0.29f, 0.78f, 0.06f, 0.15f, e84393), ELL(0.41f, 0.83f, 0.06f, 0.15f, e84393), ELL(0.59f, 0.83f, 0.06f, 0.15f, e84393),
    ELL(0.71f, 0.78f, 0.06f, 0.15f, e84393), ELL(0.5f, 0.44f, 0.27f, 0.28f, fd79a8), DISC(0.38f, 0.28f, 0.03f, e84393), DISC(0.6f, 0.25f, 0.024f, e84393),
    DISC(0.68f, 0.37f, 0.02f, e84393), DISC(0.41f, 0.5f, 0.065f, ffffff), DISC(0.59f, 0.5f, 0.065f, ffffff), DISC(0.42f, 0.51f, 0.032f, 2d3436),
    DISC(0.6f, 0.51f, 0.032f, 2d3436), DISC(0.43f, 0.495f, 0.01f, ffffff), DISC(0.61f, 0.495f, 0.01f, ffffff), ELL(0.5f, 0.63f, 0.045f, 0.025f, 8e2a5b),
    ELL(0.31f, 0.58f, 0.04f, 0.022f, ffb3d1), ELL(0.69f, 0.58f, 0.04f, 0.022f, ffb3d1), AEND } },
  // 20 coniglio
  { C(fab1a0), C(ffeaa7), { ELL(0.4f, 0.22f, 0.065f, 0.18f, f5f6fa), ELL(0.6f, 0.22f, 0.065f, 0.18f, f5f6fa), ELL(0.4f, 0.23f, 0.032f, 0.13f, ffb8c6),
    ELL(0.6f, 0.23f, 0.032f, 0.13f, ffb8c6), BOX(0.3f, 0.76f, 0.4f, 0.34f, 0.16f, e8e8f0), DISC(0.5f, 0.53f, 0.23f, ffffff),
    DISC(0.42f, 0.5f, 0.03f, 2d3436), DISC(0.58f, 0.5f, 0.03f, 2d3436), DISC(0.43f, 0.49f, 0.01f, ffffff), DISC(0.59f, 0.49f, 0.01f, ffffff),
    TRI(0.475f, 0.58f, 0.525f, 0.58f, 0.5f, 0.615f, ff7aa2), SEG(0.5f, 0.61f, 0.5f, 0.65f, 0.006f, b07a8a), SEG(0.5f, 0.65f, 0.46f, 0.67f, 0.006f, b07a8a),
    SEG(0.5f, 0.65f, 0.54f, 0.67f, 0.006f, b07a8a), ELL(0.36f, 0.59f, 0.04f, 0.022f, ffc6d3), ELL(0.64f, 0.59f, 0.04f, 0.022f, ffc6d3),
    DISC(0.66f, 0.84f, 0.06f, ff9f43), TRI(0.63f, 0.8f, 0.66f, 0.72f, 0.69f, 0.8f, 2ecc71), AEND } },
  // 21 tigre
  { C(0b6623), C(6ab04c), { DISC(0.29f, 0.3f, 0.085f, e67e22), DISC(0.71f, 0.3f, 0.085f, e67e22), DISC(0.29f, 0.3f, 0.042f, fdebd0), DISC(0.71f, 0.3f, 0.042f, fdebd0),
    DISC(0.5f, 0.53f, 0.27f, f39c12), ELL(0.5f, 0.65f, 0.15f, 0.11f, fdf2e9), SEG(0.5f, 0.27f, 0.5f, 0.37f, 0.015f, 1e1e1e), SEG(0.43f, 0.29f, 0.45f, 0.36f, 0.01f, 1e1e1e),
    SEG(0.57f, 0.29f, 0.55f, 0.36f, 0.01f, 1e1e1e), SEG(0.24f, 0.5f, 0.33f, 0.52f, 0.011f, 1e1e1e), SEG(0.25f, 0.58f, 0.33f, 0.57f, 0.011f, 1e1e1e),
    SEG(0.76f, 0.5f, 0.67f, 0.52f, 0.011f, 1e1e1e), SEG(0.75f, 0.58f, 0.67f, 0.57f, 0.011f, 1e1e1e), ELL(0.4f, 0.46f, 0.048f, 0.035f, f9e79f),
    ELL(0.6f, 0.46f, 0.048f, 0.035f, f9e79f), ELL(0.4f, 0.46f, 0.012f, 0.03f, 111111), ELL(0.6f, 0.46f, 0.012f, 0.03f, 111111),
    TRI(0.46f, 0.59f, 0.54f, 0.59f, 0.5f, 0.64f, e17055), SEG(0.5f, 0.64f, 0.5f, 0.68f, 0.008f, 5d4037), AEND } },
  // 22 lupo
  { C(0c2461), C(4a69bd), { DISC(0.76f, 0.24f, 0.08f, fdfde0), TRI(0.28f, 0.38f, 0.31f, 0.11f, 0.45f, 0.3f, 7f8c8d), TRI(0.72f, 0.38f, 0.69f, 0.11f, 0.55f, 0.3f, 7f8c8d),
    TRI(0.31f, 0.33f, 0.32f, 0.18f, 0.41f, 0.3f, 3d3d3d), TRI(0.69f, 0.33f, 0.68f, 0.18f, 0.59f, 0.3f, 3d3d3d), BOX(0.3f, 0.74f, 0.4f, 0.36f, 0.14f, 6c7a89),
    ELL(0.5f, 0.52f, 0.25f, 0.24f, 95a5a6), TRI(0.45f, 0.33f, 0.55f, 0.33f, 0.5f, 0.47f, 6c7a89), TRI(0.36f, 0.53f, 0.64f, 0.53f, 0.5f, 0.79f, ecf0f1),
    DISC(0.5f, 0.72f, 0.036f, 2d3436), ELL(0.41f, 0.47f, 0.042f, 0.026f, f6e58d), ELL(0.59f, 0.47f, 0.042f, 0.026f, f6e58d),
    DISC(0.41f, 0.47f, 0.013f, 111111), DISC(0.59f, 0.47f, 0.013f, 111111), AEND } },
  // 23 zombie
  { C(2c003e), C(4b6584), { BOX(0.3f, 0.78f, 0.4f, 0.32f, 0.1f, 6c5ce7), TRI(0.4f, 0.78f, 0.46f, 0.78f, 0.43f, 0.85f, 4b6584), BOX(0.28f, 0.23f, 0.44f, 0.52f, 0.14f, 8bc34a),
    BOX(0.28f, 0.21f, 0.44f, 0.1f, 0.06f, 3e2c1c), TRI(0.3f, 0.3f, 0.37f, 0.3f, 0.32f, 0.39f, 3e2c1c), TRI(0.58f, 0.3f, 0.66f, 0.3f, 0.63f, 0.37f, 3e2c1c),
    DISC(0.4f, 0.47f, 0.065f, fdfefe), DISC(0.41f, 0.48f, 0.02f, c0392b), SEG(0.55f, 0.43f, 0.65f, 0.51f, 0.013f, 1b2a10), SEG(0.65f, 0.43f, 0.55f, 0.51f, 0.013f, 1b2a10),
    BOX(0.4f, 0.6f, 0.21f, 0.08f, 0.02f, 3b1f1f), BOX(0.44f, 0.6f, 0.04f, 0.03f, 0.005f, f5f5dc), BOX(0.53f, 0.6f, 0.04f, 0.03f, 0.005f, f5f5dc),
    SEG(0.31f, 0.37f, 0.4f, 0.33f, 0.006f, 2c3e2c), SEG(0.34f, 0.32f, 0.35f, 0.38f, 0.005f, 2c3e2c), SEG(0.37f, 0.31f, 0.38f, 0.37f, 0.005f, 2c3e2c),
    ELL(0.6f, 0.68f, 0.03f, 0.02f, 6e9a3a), AEND } },
  // 24 pirata
  { C(1b4f72), C(2e86c1), { BOX(0.3f, 0.78f, 0.4f, 0.32f, 0.12f, fdfefe), SEG(0.34f, 0.84f, 0.66f, 0.84f, 0.014f, c0392b), SEG(0.34f, 0.93f, 0.66f, 0.93f, 0.014f, c0392b),
    DISC(0.5f, 0.54f, 0.22f, f0c08a), ELL(0.5f, 0.7f, 0.18f, 0.1f, 6d3b1e), SEG(0.45f, 0.65f, 0.55f, 0.65f, 0.009f, 3b1d0e),
    BOX(0.28f, 0.29f, 0.44f, 0.16f, 0.08f, c0392b), TRI(0.69f, 0.36f, 0.83f, 0.29f, 0.8f, 0.45f, a93226), DISC(0.38f, 0.35f, 0.016f, ffffff),
    DISC(0.5f, 0.33f, 0.016f, ffffff), DISC(0.62f, 0.35f, 0.016f, ffffff), SEG(0.3f, 0.41f, 0.68f, 0.53f, 0.009f, 1b1b1b), DISC(0.6f, 0.5f, 0.058f, 1b1b1b),
    DISC(0.42f, 0.5f, 0.026f, 1b1b1b), DISC(0.29f, 0.62f, 0.022f, f1c40f), ELL(0.5f, 0.57f, 0.03f, 0.025f, e0a878), AEND } },
  // 25 mago
  { C(1b1464), C(5f27cd), { TRI(0.5f, 0.03f, 0.28f, 0.38f, 0.72f, 0.38f, 3c40c6), DISC(0.46f, 0.22f, 0.022f, ffd32a), DISC(0.57f, 0.3f, 0.016f, ffd32a),
    DISC(0.52f, 0.13f, 0.012f, ffd32a), DISC(0.5f, 0.53f, 0.17f, f6d5b3), TRI(0.32f, 0.57f, 0.68f, 0.57f, 0.5f, 0.96f, ecf0f1), ELL(0.5f, 0.62f, 0.18f, 0.08f, ecf0f1),
    ELL(0.45f, 0.59f, 0.065f, 0.026f, ffffff), ELL(0.55f, 0.59f, 0.065f, 0.026f, ffffff), ELL(0.5f, 0.54f, 0.026f, 0.032f, eab38f),
    DISC(0.44f, 0.49f, 0.019f, 2d3436), DISC(0.56f, 0.49f, 0.019f, 2d3436), SEG(0.4f, 0.45f, 0.47f, 0.44f, 0.009f, ffffff), SEG(0.53f, 0.44f, 0.6f, 0.45f, 0.009f, ffffff),
    ELL(0.5f, 0.38f, 0.29f, 0.05f, 30336b), AEND } },
  // 26 gamer con le cuffie
  { C(3c40c6), C(ff3f34), { BOX(0.3f, 0.78f, 0.4f, 0.32f, 0.12f, 0be881), DISC(0.5f, 0.52f, 0.21f, ffd3a5), ELL(0.5f, 0.35f, 0.2f, 0.09f, 6f3a1f),
    TRI(0.32f, 0.38f, 0.42f, 0.3f, 0.36f, 0.45f, 6f3a1f), SEG(0.27f, 0.5f, 0.3f, 0.27f, 0.024f, 2d3436), SEG(0.3f, 0.27f, 0.5f, 0.17f, 0.024f, 2d3436),
    SEG(0.5f, 0.17f, 0.7f, 0.27f, 0.024f, 2d3436), SEG(0.7f, 0.27f, 0.73f, 0.5f, 0.024f, 2d3436), BOX(0.2f, 0.43f, 0.11f, 0.19f, 0.045f, ff3f34),
    BOX(0.69f, 0.43f, 0.11f, 0.19f, 0.045f, ff3f34), SEG(0.27f, 0.6f, 0.4f, 0.7f, 0.01f, 2d3436), DISC(0.41f, 0.7f, 0.024f, 2d3436),
    ELL(0.43f, 0.5f, 0.022f, 0.03f, 2d3436), ELL(0.57f, 0.5f, 0.022f, 0.03f, 2d3436), SEG(0.47f, 0.61f, 0.55f, 0.6f, 0.01f, a0522d),
    SEG(0.38f, 0.86f, 0.62f, 0.86f, 0.01f, 05c46b), AEND } },
  // 27 hacker
  { C(000000), C(0b3d0b), { SEG(0.18f, 0.2f, 0.18f, 0.27f, 0.008f, 0a7a3a), SEG(0.82f, 0.62f, 0.82f, 0.7f, 0.008f, 0a7a3a), DISC(0.84f, 0.3f, 0.01f, 0a7a3a),
    DISC(0.17f, 0.66f, 0.01f, 0a7a3a), BOX(0.22f, 0.22f, 0.56f, 0.82f, 0.24f, 1e272e), ELL(0.5f, 0.52f, 0.17f, 0.2f, 0a0a0a),
    BOX(0.36f, 0.45f, 0.12f, 0.065f, 0.02f, 00ff88), BOX(0.52f, 0.45f, 0.12f, 0.065f, 0.02f, 00ff88), SEG(0.48f, 0.48f, 0.52f, 0.48f, 0.008f, 00ff88),
    SEG(0.46f, 0.62f, 0.54f, 0.62f, 0.006f, 00aa55), SEG(0.44f, 0.74f, 0.44f, 0.87f, 0.006f, bdc3c7), SEG(0.56f, 0.74f, 0.56f, 0.87f, 0.006f, bdc3c7),
    DISC(0.44f, 0.88f, 0.012f, bdc3c7), DISC(0.56f, 0.88f, 0.012f, bdc3c7), AEND } },
  // 28 maschera oni
  { C(1a0000), C(c0392b), { TRI(0.31f, 0.31f, 0.23f, 0.08f, 0.39f, 0.26f, f5f5dc), TRI(0.69f, 0.31f, 0.77f, 0.08f, 0.61f, 0.26f, f5f5dc),
    ELL(0.5f, 0.53f, 0.27f, 0.29f, e74c3c), TRI(0.32f, 0.37f, 0.47f, 0.42f, 0.33f, 0.44f, 1a0000), TRI(0.68f, 0.37f, 0.53f, 0.42f, 0.67f, 0.44f, 1a0000),
    ELL(0.4f, 0.48f, 0.052f, 0.032f, ffd32a), ELL(0.6f, 0.48f, 0.052f, 0.032f, ffd32a), DISC(0.4f, 0.48f, 0.016f, 1a0000), DISC(0.6f, 0.48f, 0.016f, 1a0000),
    ELL(0.5f, 0.57f, 0.045f, 0.03f, b03a2e), BOX(0.35f, 0.63f, 0.3f, 0.11f, 0.045f, 1a0000), TRI(0.37f, 0.63f, 0.43f, 0.63f, 0.4f, 0.71f, ffffff),
    TRI(0.57f, 0.63f, 0.63f, 0.63f, 0.6f, 0.71f, ffffff), TRI(0.44f, 0.74f, 0.5f, 0.74f, 0.47f, 0.69f, ffffff), TRI(0.5f, 0.74f, 0.56f, 0.74f, 0.53f, 0.69f, ffffff),
    SEG(0.27f, 0.6f, 0.31f, 0.7f, 0.008f, a93226), SEG(0.73f, 0.6f, 0.69f, 0.7f, 0.008f, a93226), AEND } },
  // 29 disco volante
  { C(0c0032), C(3d1a78), { DISC(0.24f, 0.26f, 0.011f, ffffff), DISC(0.76f, 0.2f, 0.009f, ffffff), DISC(0.82f, 0.62f, 0.011f, ffffff), DISC(0.18f, 0.66f, 0.009f, ffffff),
    DISC(0.66f, 0.12f, 0.007f, ffffff), TRI(0.5f, 0.55f, 0.29f, 0.94f, 0.71f, 0.94f, 5e8f72), TRI(0.5f, 0.58f, 0.37f, 0.94f, 0.63f, 0.94f, 8fd9a8),
    DISC(0.5f, 0.44f, 0.14f, a5f3fc), DISC(0.5f, 0.47f, 0.06f, 7bed7b), ELL(0.48f, 0.46f, 0.012f, 0.018f, 101010), ELL(0.52f, 0.46f, 0.012f, 0.018f, 101010),
    SEG(0.48f, 0.41f, 0.46f, 0.36f, 0.006f, 7bed7b), SEG(0.52f, 0.41f, 0.54f, 0.36f, 0.006f, 7bed7b), ELL(0.5f, 0.55f, 0.31f, 0.085f, 7f8c8d),
    ELL(0.5f, 0.52f, 0.27f, 0.055f, bdc3c7), DISC(0.31f, 0.57f, 0.021f, ffd32a), DISC(0.43f, 0.6f, 0.021f, ff4757), DISC(0.57f, 0.6f, 0.021f, ffd32a),
    DISC(0.69f, 0.57f, 0.021f, ff4757), DISC(0.42f, 0.4f, 0.025f, e0fbff), AEND } },
  // 30 slime
  { C(6c5ce7), C(a29bfe), { ELL(0.5f, 0.86f, 0.27f, 0.035f, 4834a6), ELL(0.5f, 0.67f, 0.31f, 0.21f, 3ddc97), DISC(0.5f, 0.5f, 0.22f, 3ddc97),
    TRI(0.46f, 0.3f, 0.54f, 0.3f, 0.53f, 0.2f, 3ddc97), ELL(0.27f, 0.82f, 0.045f, 0.06f, 3ddc97), ELL(0.73f, 0.84f, 0.035f, 0.05f, 3ddc97),
    ELL(0.39f, 0.42f, 0.055f, 0.03f, c8fff0), DISC(0.3f, 0.6f, 0.014f, c8fff0), ELL(0.42f, 0.57f, 0.036f, 0.052f, 1e272e), ELL(0.58f, 0.57f, 0.036f, 0.052f, 1e272e),
    DISC(0.43f, 0.555f, 0.013f, ffffff), DISC(0.59f, 0.555f, 0.013f, ffffff), ELL(0.5f, 0.68f, 0.055f, 0.035f, 1e272e), ELL(0.5f, 0.7f, 0.03f, 0.015f, ff7675),
    ELL(0.34f, 0.64f, 0.03f, 0.016f, 7ef0c0), ELL(0.66f, 0.64f, 0.03f, 0.016f, 7ef0c0), AEND } },
  // 31 cyborg
  { C(2f3542), C(57606f), { BOX(0.26f, 0.82f, 0.48f, 0.28f, 0.1f, 1e2530), BOX(0.4f, 0.72f, 0.2f, 0.14f, 0.02f, 747d8c), DISC(0.5f, 0.5f, 0.25f, a4b0be),
    ELL(0.42f, 0.5f, 0.17f, 0.25f, f0c8a0), ELL(0.42f, 0.29f, 0.17f, 0.06f, 2d1e14), SEG(0.59f, 0.3f, 0.59f, 0.72f, 0.007f, 57606f),
    DISC(0.62f, 0.46f, 0.05f, 2f3542), DISC(0.62f, 0.46f, 0.028f, ff4757), DISC(0.615f, 0.455f, 0.009f, ffd0d5), DISC(0.4f, 0.46f, 0.026f, 2d3436),
    DISC(0.41f, 0.45f, 0.009f, ffffff), DISC(0.7f, 0.35f, 0.012f, 57606f), DISC(0.72f, 0.6f, 0.012f, 57606f), SEG(0.42f, 0.62f, 0.56f, 0.62f, 0.008f, 8b4a3a),
    SEG(0.62f, 0.66f, 0.7f, 0.66f, 0.006f, 57606f), SEG(0.34f, 0.88f, 0.66f, 0.88f, 0.008f, 00d2d3), AEND } },
};

static SDL_Texture *art_tex[AV_ART_COUNT];
static SDL_Texture *art_avatar(int idx) {
  if (idx < 0 || idx >= AV_ART_COUNT) return NULL;
  if (art_tex[idx]) return art_tex[idx];
  const int N = 256; const AvatarArt *A = &ART[idx];
  SDL_Surface *s = SDL_CreateRGBSurfaceWithFormat(0, N, N, 32, SDL_PIXELFORMAT_ARGB8888);
  if (!s) return NULL;
  Uint32 *px = s->pixels; int pitch = s->pitch / 4;
  float r1 = (A->bg1 >> 16) & 255, g1 = (A->bg1 >> 8) & 255, b1 = A->bg1 & 255;
  float r2 = (A->bg2 >> 16) & 255, g2 = (A->bg2 >> 8) & 255, b2 = A->bg2 & 255;
  for (int y = 0; y < N; y++) for (int x = 0; x < N; x++) {
    float u = (x + 0.5f) / N, v = (y + 0.5f) / N;
    float circ = clampf((0.5f - hypotf(u - 0.5f, v - 0.5f)) * N + 0.5f, 0, 1);
    if (circ <= 0) { px[y * pitch + x] = 0; continue; }
    // sfondo: sfumatura diagonale + alone di luce
    float t = clampf((u + v) * 0.5f, 0, 1);
    float hl = 1.0f - clampf(hypotf(u - 0.32f, v - 0.25f) / 0.8f, 0, 1);
    float R_ = r2 + (r1 - r2) * t + 50 * hl * hl, G_ = g2 + (g1 - g2) * t + 50 * hl * hl, B_ = b2 + (b1 - b2) * t + 50 * hl * hl;
    for (const APrim *p = A->p; p->op; p++) {
      float d;
      switch (p->op) {
        case A_DISC: d = hypotf(u - p->a, v - p->b) - p->c; break;
        case A_ELL: { float k = fminf(p->c, p->d); d = (hypotf((u - p->a) / p->c, (v - p->b) / p->d) - 1.0f) * k; break; }
        case A_SEG: d = sd_seg(u, v, p->a, p->b, p->c, p->d) - p->e; break;
        case A_BOX: d = sd_box(u, v, p->a, p->b, p->c, p->d, p->e); break;
        case A_TRI: d = sd_tri(u, v, p->a, p->b, p->c, p->d, p->e, p->f); break;
        default: d = 1;
      }
      float cov = clampf(0.5f - d * N, 0, 1);
      if (cov <= 0) continue;
      float pr = (p->col >> 16) & 255, pg = (p->col >> 8) & 255, pb = p->col & 255;
      // leggera ombreggiatura dall'alto, per dare volume
      float shade = 1.0f + 0.12f * (0.5f - v);
      R_ = R_ + (pr * shade - R_) * cov; G_ = G_ + (pg * shade - G_) * cov; B_ = B_ + (pb * shade - B_) * cov;
    }
    int R = (int)clampf(R_, 0, 255), G = (int)clampf(G_, 0, 255), B = (int)clampf(B_, 0, 255);
    px[y * pitch + x] = ((Uint32)(circ * 255) << 24) | ((Uint32)R << 16) | ((Uint32)G << 8) | (Uint32)B;
  }
  art_tex[idx] = SDL_CreateTextureFromSurface(R, s);
  SDL_FreeSurface(s);
  if (art_tex[idx]) { SDL_SetTextureBlendMode(art_tex[idx], SDL_BLENDMODE_BLEND); SDL_SetTextureScaleMode(art_tex[idx], SDL_ScaleModeLinear); }
  return art_tex[idx];
}

void draw_avatar(const char *oid, int avatar, int cx, int cy, int size, int alpha) {
  int frames = 0;
  const char *m = media_of(oid, &frames);
  if (m && draw_media_frames(m, frames, cx - size / 2, cy - size / 2, size, size, 256, 256, 128, size >= 140, alpha)) return;
  if (avatar >= AV_ART_FIRST && avatar < AV_ART_FIRST + AV_ART_COUNT) {
    SDL_Texture *t = art_avatar(avatar - AV_ART_FIRST);
    if (t) { SDL_SetTextureColorMod(t, 255, 255, 255); draw_tex(t, cx - size / 2, cy - size / 2, size, size, alpha); return; }
  }
  draw_avatar_color(oid, avatar & 15, cx, cy, size, alpha);
}
