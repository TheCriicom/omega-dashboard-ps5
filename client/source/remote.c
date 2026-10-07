// Omega UI — Telecomando dal telefono: come collegarsi, con un codice QR da
// inquadrare, l'indirizzo e il PIN. La pagina la serve il demone (porta 9095).
#include "app.h"
#include "qrcodegen.h"
#include <arpa/inet.h>
#include <stdlib.h>
#ifndef PS5
#include <ifaddrs.h>
#include <netinet/in.h>
#endif

#define CTL "http://127.0.0.1:9095"
#ifdef PS5
int sceNetCtlInit(void);
int sceNetCtlGetInfo(int code, void *info);
#endif

static char pin[8], ip[48], url[96];
static int state;                    // 0 in attesa, 1 pronto, -1 il demone non risponde
static SDL_Texture *qr; static char qr_for[96];

// indirizzo della console nella rete di casa
int console_ip(char *out, size_t n) {
  out[0] = 0;
#ifdef PS5
  static int inited; if (!inited) inited = sceNetCtlInit() >= 0 ? 1 : -1;
  char info[256]; memset(info, 0, sizeof info);
  if (sceNetCtlGetInfo(14 /* IP_ADDRESS */, info) == 0 && info[0]) snprintf(out, n, "%.15s", info);
#else
  struct ifaddrs *ifs, *i;
  if (getifaddrs(&ifs) == 0) {
    for (i = ifs; i; i = i->ifa_next)
      if (i->ifa_addr && i->ifa_addr->sa_family == AF_INET && strcmp(i->ifa_name, "lo0")) {
        inet_ntop(AF_INET, &((struct sockaddr_in *)i->ifa_addr)->sin_addr, out, (socklen_t)n);
        if (strncmp(out, "127.", 4)) break;
      }
    freeifaddrs(ifs);
  }
#endif
  return out[0] ? 0 : -1;
}

static void build_qr(void) {
  if (qr && !strcmp(qr_for, url)) return;
  if (qr) { SDL_DestroyTexture(qr); qr = NULL; }
  static uint8_t tmp[qrcodegen_BUFFER_LEN_MAX], code[qrcodegen_BUFFER_LEN_MAX];
  if (!qrcodegen_encodeText(url, tmp, code, qrcodegen_Ecc_MEDIUM, qrcodegen_VERSION_MIN, qrcodegen_VERSION_MAX, qrcodegen_Mask_AUTO, true)) return;
  int size = qrcodegen_getSize(code), border = 3, total = size + border * 2;
  SDL_Surface *s = SDL_CreateRGBSurfaceWithFormat(0, total, total, 32, SDL_PIXELFORMAT_ARGB8888);
  if (!s) return;
  Uint32 *px = s->pixels; int pitch = s->pitch / 4;
  for (int y = 0; y < total; y++) for (int x = 0; x < total; x++) {
    int on = x >= border && y >= border && x < border + size && y < border + size && qrcodegen_getModule(code, x - border, y - border);
    px[y * pitch + x] = on ? 0xFF0C0E14 : 0xFFFFFFFF;
  }
  qr = SDL_CreateTextureFromSurface(R, s);
  SDL_FreeSurface(s);
  if (qr) SDL_SetTextureScaleMode(qr, SDL_ScaleModeNearest);   // moduli netti, niente sfocatura
  snprintf(qr_for, sizeof qr_for, "%s", url);
}

static void on_remote(int st, JVal *j, const char *raw, void *ud) {
  (void)raw; (void)ud;
  if (st == 200) { jcpy(pin, sizeof pin, j, "pin"); state = pin[0] ? 1 : -1; }
  else state = -1;
}

void remote_open(void) {
  state = 0; pin[0] = 0;
  console_ip(ip, sizeof ip);
  snprintf(url, sizeof url, "http://%s:9095", ip[0] ? ip : "IP");
  net_req(HTTP_GET, CTL "/v1/remote", NULL, on_remote, NULL);
  ov_push(OV_REMOTE);
}

static void step(int n, const char *text, int x, int y, int w, int a) {
  fill_circle(x + 22, y + 22, 22, C_ACC, a);
  char nb[4]; snprintf(nb, sizeof nb, "%d", n);
  draw_text(font(W_BOLD, 24), nb, x + 22, y + 22 - TTF_FontHeight(font(W_BOLD, 24)) / 2, C_WHITE, a, AL_C);
  draw_text_wrap(font(W_REG, 27), text, x + 64, y + 4, w - 64, 2, 36, C_TXT, a);
}

void remote_draw(float t) {
  int a = (int)(255 * t);
  Col bg = mix(RGB(10, 12, 20), g_theme_base, 0.12f);
  fill_rect(0, 0, SCREEN_W, SCREEN_H, bg, t > 0.98f ? 255 : a);
  glow(380, 520, 520, C_ACC, a * 14 / 100);
  draw_icon(IC_GLOBE, 132, 74, 44, C_ACC2, a);
  draw_text(font(W_LIGHT, 38), _("Telecomando dal telefono o dal PC"), 172, 52, C_WHITE, a, AL_L);

  // codice QR
  int qs = 500, qx = 130, qy = 190;
  fill_rrect(qx - 20, qy - 20, qs + 40, qs + 40, 30, C_WHITE, a);
  if (state == 1 && ip[0]) { build_qr(); if (qr) draw_tex(qr, qx, qy, qs, qs, a); }
  else if (state == 0) draw_spinner(qx + qs / 2, qy + qs / 2, 26, a);
  else draw_icon(IC_CLOSE, qx + qs / 2, qy + qs / 2, 120, RGB(180, 186, 200), a);
  draw_text(font(W_MED, 34), url, qx + qs / 2, qy + qs + 46, C_WHITE, a, AL_C);

  int x = 760, w = SCREEN_W - x - 110, y = 190;
  if (state == -1) {
    draw_text_wrap(font(W_REG, 30), _("Il servizio di Omega non risponde, quindi il telecomando non è attivo. Aggiorna Omega o riavvia la console."), x, y, w, 4, 44, C_DIM, a);
  } else {
    step(1, _("Collega il telefono o il PC alla stessa rete Wi-Fi della console."), x, y, w, a);
    step(2, _("Inquadra il codice con la fotocamera, oppure apri l'indirizzo qui a sinistra nel browser."), x, y + 100, w, a);
    char p3[160]; snprintf(p3, sizeof p3, _("Scrivi il PIN quando te lo chiede:"));
    step(3, p3, x, y + 200, w, a);
    // PIN grande, cifre spaziate
    char big[16] = ""; for (int i = 0; pin[i] && i < 6; i++) { size_t L = strlen(big); snprintf(big + L, sizeof big - L, "%c%s", pin[i], i < 5 ? " " : ""); }
    fill_rrect(x + 64, y + 260, 420, 100, 22, RGB(255, 255, 255), a * 8 / 100);
    draw_text(font(W_BOLD, 58), state == 1 ? big : "\xE2\x80\xA6", x + 64 + 210, y + 270, C_WHITE, a, AL_C);

    draw_text(font(W_MED, 28), _("Dal telefono puoi"), x, y + 410, C_TXT, a, AL_L);
    // cosa si fa dal telefono o dal PC: prima i giochi, il motivo per cui lo si apre più spesso
    const int cic[5] = { IC_CLOUD, IC_BOX, IC_IDEA, IC_MUSIC, IC_GAMEPAD };
    const char *can[5] = { _("caricare giochi dal PC alla console: .pkg, .zip, .elf o cartelle intere, anche trascinandoli"),
                           _("installarli subito: compaiono in home con l'icona e la barra che avanza"),
                           _("importare un JSON di giochi, collegarlo da un link o scaricare l'esempio da modificare"),
                           _("comandare la musica, anche mentre giochi, e mandare file audio"),
                           _("vedere il gioco in corso, la temperatura e il party vocale") };
    for (int i = 0; i < 5; i++) {
      draw_icon(cic[i], x + 16, y + 464 + i * 42 + 14, 26, i < 3 ? C_ACC2 : C_OK, a);
      draw_text_fit(font(W_REG, 24), can[i], x + 48, y + 462 + i * 42, w - 48, i < 3 ? C_TXT : C_DIM, a, AL_L);
    }
    draw_text_wrap(font(W_REG, 21), _("Il PIN abbina il telefono una volta sola. Con un nuovo PIN i telefoni già abbinati vanno abbinati di nuovo."), x, y + 690, w, 2, 30, C_FAINT, a);
  }
  int ic[2] = { IC_BTN_X, IC_BTN_O }; const char *lb[2] = { _("Nuovo PIN"), _("Indietro") };
  hints(ic, lb, 2, a);
}

static void reset_yes(int idx, void *ud) {
  (void)ud; if (idx != 0) return;
  state = 0;
  net_req(HTTP_POST, CTL "/v1/pair/reset", "{}", on_remote, NULL);
}

void remote_input(int b) {
  if (b == B_O) { ov_pop(); return; }
  if (b == B_X && state == 1) confirm_open(_("Creare un nuovo PIN? I telefoni già abbinati smetteranno di funzionare."), _("Nuovo PIN"), reset_yes, NULL);
}
