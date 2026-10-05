// Omega UI — Impostazioni › Personalizza.
// Pannello a destra: a sinistra resta la home, che cambia dal vivo mentre si
// scorrono le opzioni (la scena congelata qui è spenta, main.c). Colonna delle
// categorie, poi le opzioni della categoria; ←/→ cambia il valore subito,
// X lo fa avanzare, △ riporta la categoria ai valori iniziali.
#include "app.h"
#include <math.h>
#include <stdlib.h>

static int cat, sel, zone;             // zone 0 categorie, 1 opzioni
static float cat_anim, sel_anim, scroll, scroll_t;
static float flash;                    // lampo sul valore appena cambiato

static const char *const CATS[] = { N_("Stili rapidi"), N_("Aspetto"), N_("Home"), N_("Orologio e barra"), N_("Movimento"),
                                     N_("Suoni"), N_("Notifiche"), N_("Salvaschermo"), N_("Prestazioni") };
static const int CAT_IC[] = { IC_STAR, IC_BRUSH, IC_GAMEPAD, IC_CLOCK, IC_RELOAD, IC_MUSIC, IC_BELL, IC_PLAY, IC_GEAR };
#define NCATS (int)(sizeof CATS / sizeof *CATS)

// opzioni della categoria c (c >= 1: PC_* = c - 1)
static int rows(int c, const PrefDef **out, int max) {
  int n, k = 0; const PrefDef *t = prefs_table(&n);
  for (int i = 0; i < n && k < max; i++) if (t[i].cat == c - 1) out[k++] = &t[i];
  return k;
}
static int nrows(void) { const PrefDef *r[40]; return cat == 0 ? presets_count() : rows(cat, r, 40); }

void custom_open(void) { cat = 1; sel = 0; zone = 0; scroll = scroll_t = 0; ov_push(OV_CUSTOM); }

// colore di anteprima per tema e accento
static Col swatch(const PrefDef *d, int v) {
  static const Col TH[N_THEMES] = { {0,112,243,255}, {90,100,255,255}, {0,170,140,255}, {235,90,50,255}, {140,70,240,255}, {110,120,140,255},
                                    {255,40,170,255}, {20,190,100,255}, {225,30,70,255}, {230,170,30,255}, {90,190,240,255}, {70,110,255,255} };
  static const Col AC[8] = { {0,0,0,0}, {0,140,255,255}, {140,80,255,255}, {255,60,150,255}, {20,190,110,255}, {255,120,30,255}, {230,175,30,255}, {220,225,235,255} };
  if (!strcmp(d->key, "theme")) return TH[v % N_THEMES];
  if (!strcmp(d->key, "accent")) return v ? AC[v % 8] : C_ACC;
  return (Col){ 0, 0, 0, 0 };
}

void custom_draw(float t) {
  int a = (int)(255 * t);
  float e = ease_out(t);
  int w = 1140, x = SCREEN_W - (int)(w * e);
  // a sinistra la home resta visibile (anteprima dal vivo): solo un'ombra
  grad_h(x - 80, 0, 80, SCREEN_H, C_BLACK, 0, C_BLACK, (int)(150 * t));
  fill_rect(x, 0, w, SCREEN_H, RGB(14, 17, 27), t > 0.98f ? 255 : (int)(250 * t));
  fill_rect(x, 0, 2, SCREEN_H, C_ACC, (int)(120 * t));
  // titolo
  fill_circle(x + 84, 78, 30, C_ACC, a);
  draw_icon(IC_BRUSH, x + 84, 78, 34, C_WHITE, a);
  draw_text(font(W_LIGHT, 46), _("Personalizza"), x + 132, 48, C_WHITE, a, AL_L);
  draw_text_fit(font(W_REG, 22), _("Ogni cambio si vede subito sulla home qui a sinistra"), x + 134, 104, w - 200, C_DIM, a, AL_L);

  // colonna delle categorie
  cat_anim = approach(cat_anim, (float)cat, 18.0f);
  int cx = x + 44, cw = 330, cy0 = 170;
  for (int i = 0; i < NCATS; i++) {
    int yy = cy0 + i * 70;
    float fa = clampf(1 - fabsf(cat_anim - i), 0, 1);
    int on = i == cat;
    if (fa > 0.01f) fill_rrect(cx, yy, cw, 58, 18, zone == 0 ? C_WHITE : C_ACC, (int)(a * (zone == 0 ? 1.0f : 0.35f) * fa));
    Col fg = zone == 0 && on ? RGB(12, 14, 22) : on ? C_WHITE : C_TXT;
    draw_icon(CAT_IC[i], cx + 34, yy + 29, 26, fg, a);
    draw_text_fit(font(on ? W_MED : W_REG, 25), _(CATS[i]), cx + 66, yy + 14, cw - 80, fg, a, AL_L);
  }

  // opzioni
  int ox = cx + cw + 36, ow = x + w - 50 - ox, oy0 = 170;
  sel_anim = approach(sel_anim, (float)sel, 18.0f);
  flash = approach(flash, 0, 5.0f);
  const PrefDef *r[40]; int n = cat == 0 ? presets_count() : rows(cat, r, 40);
  int rh = cat == 0 ? 112 : 96;
  // scorrimento: la riga scelta resta visibile
  int view_h = SCREEN_H - oy0 - 210;
  float want = sel * rh - view_h / 2 + rh / 2; if (want < 0) want = 0;
  float maxs = n * rh - view_h; if (maxs < 0) maxs = 0; if (want > maxs) want = maxs;
  scroll_t = zone == 1 ? want : 0; scroll = approach(scroll, scroll_t, 14.0f);
  SDL_Rect clip = { ox - 20, oy0 - 10, ow + 40, view_h + 20 };
  SDL_RenderSetClipRect(R, &clip);
  for (int i = 0; i < n; i++) {
    int yy = oy0 + i * rh - (int)scroll;
    if (yy < oy0 - rh || yy > oy0 + view_h) continue;
    float fa = zone == 1 ? clampf(1 - fabsf(sel_anim - i), 0, 1) : 0;
    fill_rrect(ox, yy, ow, rh - 14, 20, RGB(26, 30, 46), a);
    if (fa > 0.01f) {
      fill_rrect(ox, yy, ow, rh - 14, 20, C_WHITE, (int)(a * 0.10f * fa));
      stroke_rrect(ox - 4, yy - 4, ow + 8, rh - 6, 24, 3, C_ACC2, (int)(a * fa));
    }
    if (cat == 0) {   // stili rapidi
      draw_text(font(W_MED, 28), preset_name(i), ox + 28, yy + 16, C_WHITE, a, AL_L);
      draw_text_wrap(font(W_REG, 21), preset_desc(i), ox + 28, yy + 54, ow - 230, 2, 26, C_DIM, a);
      draw_text(font(W_MED, 22), _("Applica"), ox + ow - 30, yy + 34, i == sel && zone == 1 ? C_ACC2 : C_FAINT, a, AL_R);
      continue;
    }
    const PrefDef *d = r[i];
    int v = *d->var, nv = pref_nvals(d);
    draw_text_fit(font(W_MED, 26), _(d->name), ox + 28, yy + 14, ow / 2 - 30, C_WHITE, a, AL_L);
    // valore: ‹ valore ›, con i pallini delle posizioni sotto
    const char *val = _(d->vals[v]);
    TTF_Font *vf = font(W_MED, 25);
    int vw = text_w(vf, val), vx = ox + ow - 60;
    Col sw = swatch(d, v);
    float fl = i == sel && zone == 1 ? flash : 0;
    if (fl > 0.02f) fill_rrect(vx - vw - 56, yy + 8, vw + 90, 44, 22, C_ACC, (int)(a * 0.35f * fl));
    draw_text(vf, val, vx, yy + 14, C_TXT, a, AL_R);
    if (sw.a) { fill_circle(vx - vw - 26, yy + 29, 12, sw, a); ring(vx - vw - 26, yy + 29, 13, 2, C_WHITE, a / 2); }
    if (i == sel && zone == 1) {
      draw_text(vf, "\xE2\x80\xB9", vx - vw - (sw.a ? 54 : 24), yy + 12, C_ACC2, a, AL_R);
      draw_text(vf, "\xE2\x80\xBA", vx + 18, yy + 12, C_ACC2, a, AL_L);
    }
    int dots = nv > 8 ? 8 : nv, dx = vx - dots * 16 + 8;
    for (int k = 0; k < dots; k++) fill_circle(dx + k * 16, yy + 62, k == (v * dots / nv) ? 5 : 3, k == (v * dots / nv) ? C_ACC2 : RGB(70, 78, 100), a);
    draw_text_fit(font(W_REG, 20), _(d->desc), ox + 28, yy + 52, ow / 2 + 40, C_FAINT, a, AL_L);
  }
  SDL_RenderSetClipRect(R, NULL);

  // spiegazione dell'opzione scelta, in basso
  if (zone == 1 && cat > 0 && sel < n) {
    int by = SCREEN_H - 200;
    fill_rrect(ox, by, ow, 76, 18, RGB(22, 26, 40), a);
    draw_icon(IC_MORE, ox + 34, by + 38, 24, C_ACC2, a);
    draw_text_wrap(font(W_REG, 22), _(r[sel]->desc), ox + 64, by + 12, ow - 90, 2, 28, C_TXT, a);
  }
  const int ic[] = { IC_BTN_X, IC_BTN_TRI, IC_BTN_O };
  const char *lb[] = { zone == 0 ? _("Apri") : cat == 0 ? _("Applica") : _("Cambia"), _("Ripristina"), _("Indietro") };
  hints(ic, lb, 3, a);
}

static void reset_category(void) {
  const PrefDef *r[40]; int n = rows(cat, r, 40);
  for (int i = 0; i < n; i++) if (*r[i]->var != r[i]->def) pref_set(r[i], r[i]->def);
  set_msg(_("Categoria riportata ai valori iniziali"), 0);
}

void custom_input(int b) {
  int n = nrows();
  if (zone == 0) {
    if (b == B_O) { ov_pop(); return; }
    if (b == B_UP && cat > 0) { cat--; sfx_play(SFX_MOVE); }
    else if (b == B_DOWN && cat < NCATS - 1) { cat++; sfx_play(SFX_MOVE); }
    else if (b == B_RIGHT || b == B_X) { zone = 1; sel = 0; sfx_play(SFX_SELECT); }
    else if (b == B_TRI && cat > 0) reset_category();
    return;
  }
  if (b == B_O || (b == B_LEFT && cat == 0)) { zone = 0; sfx_play(SFX_BACK); return; }
  if (b == B_UP && sel > 0) { sel--; sfx_play(SFX_MOVE); return; }
  if (b == B_DOWN && sel < n - 1) { sel++; sfx_play(SFX_MOVE); return; }
  if (b == B_TRI && cat > 0) { reset_category(); return; }
  if (cat == 0) {
    if (b == B_X) { preset_apply(sel); char m[160]; snprintf(m, sizeof m, _("Stile «%s» applicato"), preset_name(sel)); set_msg(m, 0); sfx_play(SFX_SELECT); }
    return;
  }
  const PrefDef *r[40]; rows(cat, r, 40);
  const PrefDef *d = r[sel];
  if (b == B_LEFT) pref_set(d, *d->var - 1);
  else if (b == B_RIGHT || b == B_X) pref_set(d, *d->var + 1);
  else return;
  flash = 1;
  sfx_play(SFX_MOVE);   // con il pacchetto di suoni appena scelto: si sente com'è
}
