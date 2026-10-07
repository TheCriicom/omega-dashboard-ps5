// Omega UI — App mobile e telecomando, due schede:
//  · App mobile: codice QR della web app (<server>/app). Ci si entra con
//    l'account Omega e funziona anche a console spenta: amici, chat, party,
//    community, classifiche, trofei, Store, notifiche. Le funzioni che toccano
//    la console (caricare giochi, libreria) la aprono direttamente in Wi-Fi;
//  · Telecomando in Wi-Fi: la pagina del demone (porta 9095), con QR e PIN.
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

static char pin[8], ip[48], url[96], app_url[160];
static int rmode;                    // 0 App mobile, 1 telecomando in Wi-Fi
static int state;                    // 0 in attesa, 1 pronto, -1 il demone non risponde
static SDL_Texture *qr; static char qr_for[200];

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

static void build_qr(const char *text) {
  if (qr && !strcmp(qr_for, text)) return;
  if (qr) { SDL_DestroyTexture(qr); qr = NULL; }
  static uint8_t tmp[qrcodegen_BUFFER_LEN_MAX], code[qrcodegen_BUFFER_LEN_MAX];
  if (!qrcodegen_encodeText(text, tmp, code, qrcodegen_Ecc_MEDIUM, qrcodegen_VERSION_MIN, qrcodegen_VERSION_MAX, qrcodegen_Mask_AUTO, true)) return;
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
  snprintf(qr_for, sizeof qr_for, "%s", text);
}

static void on_remote(int st, JVal *j, const char *raw, void *ud) {
  (void)raw; (void)ud;
  if (st == 200) { jcpy(pin, sizeof pin, j, "pin"); state = pin[0] ? 1 : -1; }
  else state = -1;
}

void mobile_open(void) { remote_open(); rmode = 0; }

// Riquadro "App mobile" da mettere ovunque si parla del telefono o del PC:
// codice QR, indirizzo della web app in grande e una riga di spiegazione.
static SDL_Texture *card_qr; static char card_for[200];
void mobile_link_card(int x, int y, int w, int a) {
  char u[160]; snprintf(u, sizeof u, "%s/app/", omega_base());
  if (!card_qr || strcmp(card_for, u)) {
    if (card_qr) { SDL_DestroyTexture(card_qr); card_qr = NULL; }
    static uint8_t tmp[qrcodegen_BUFFER_LEN_MAX], code[qrcodegen_BUFFER_LEN_MAX];
    if (qrcodegen_encodeText(u, tmp, code, qrcodegen_Ecc_MEDIUM, qrcodegen_VERSION_MIN, qrcodegen_VERSION_MAX, qrcodegen_Mask_AUTO, true)) {
      int size = qrcodegen_getSize(code), border = 2, total = size + border * 2;
      SDL_Surface *s = SDL_CreateRGBSurfaceWithFormat(0, total, total, 32, SDL_PIXELFORMAT_ARGB8888);
      if (s) {
        Uint32 *px = s->pixels; int pitch = s->pitch / 4;
        for (int yy = 0; yy < total; yy++) for (int xx = 0; xx < total; xx++)
          px[yy * pitch + xx] = (xx >= border && yy >= border && xx < border + size && yy < border + size && qrcodegen_getModule(code, xx - border, yy - border)) ? 0xFF0C0E14 : 0xFFFFFFFF;
        card_qr = SDL_CreateTextureFromSurface(R, s); SDL_FreeSurface(s);
        if (card_qr) SDL_SetTextureScaleMode(card_qr, SDL_ScaleModeNearest);
      }
    }
    snprintf(card_for, sizeof card_for, "%s", u);
  }
  int h = 150;
  shadow_rrect(x, y, w, h, 26, 24, a * 50 / 100);
  fill_rrect(x, y, w, h, 26, RGB(40, 30, 90), a);
  grad_h(x + 20, y, w - 40, h, RGB(110, 60, 210), a * 70 / 100, RGB(30, 110, 220), a * 70 / 100);
  glow(x + w - 120, y + 40, 160, C_WHITE, a * 10 / 100);
  int qs = h - 30;
  fill_rrect(x + 15, y + 15, qs, qs, 14, C_WHITE, a);
  if (card_qr) draw_tex(card_qr, x + 21, y + 21, qs - 12, qs - 12, a);
  int tx = x + qs + 40, tw = w - qs - 60;
  draw_icon(IC_CHAT, tx + 14, y + 34, 26, C_WHITE, a);
  draw_text(font(W_BOLD, 24), _("APP MOBILE"), tx + 38, y + 22, RGB(230, 225, 255), a, AL_L);
  draw_text_fit(font(W_MED, 34), u, tx, y + 54, tw, C_WHITE, a, AL_L);
  draw_text_fit(font(W_REG, 20), _("Inquadra il codice o scrivi l'indirizzo nel browser del telefono o del PC, e accedi con il tuo account"), tx, y + 106, tw, RGB(225, 228, 245), a, AL_L);
}

void remote_open(void) {
  rmode = 1;
  snprintf(app_url, sizeof app_url, "%s/app/", omega_base());
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

static void tabs(int a) {
  if (!rmode) return;   // l'App mobile non ha schede: il telecomando in Wi-Fi si apre da Sistema
  const char *n[2] = { _("App mobile"), _("Telecomando in Wi-Fi") };
  int x = 700, y = 46;
  for (int i = 0; i < 2; i++) {
    TTF_Font *f = font(rmode == i ? W_BOLD : W_MED, 28);
    int w = text_w(f, n[i]) + 52;
    if (rmode == i) fill_rrect(x, y, w, 54, 27, C_WHITE, a * 16 / 100);
    draw_text(f, n[i], x + w / 2, y + 27 - TTF_FontHeight(f) / 2, rmode == i ? C_WHITE : C_DIM, a, AL_C);
    x += w + 12;
  }
  draw_text(font(W_REG, 20), "L1 / R1", x + 10, y + 16, C_FAINT, a, AL_L);
}

static void mobile_draw(int a) {
  int qs = 440, qx = 150, qy = 200;
  glow(qx + qs / 2, qy + qs / 2, qs, RGB(160, 90, 255), a * 22 / 100);
  fill_rrect(qx - 26, qy - 26, qs + 52, qs + 52, 34, RGB(120, 70, 230), a);
  fill_rrect(qx - 20, qy - 20, qs + 40, qs + 40, 30, C_WHITE, a);
  build_qr(app_url); if (qr) draw_tex(qr, qx, qy, qs, qs, a);
  { TTF_Font *f = font(W_MED, 30); int uw = text_w(f, app_url) + 60; if (uw > qs + 180) uw = qs + 180;
    int ux = qx + qs / 2 - uw / 2, uy = qy + qs + 36;
    fill_rrect(ux, uy, uw, 58, 29, RGB(120, 70, 230), a);
    draw_text_fit(f, app_url, qx + qs / 2, uy + 29 - TTF_FontHeight(f) / 2, uw - 40, C_WHITE, a, AL_C); }
  draw_text_wrap_al(font(W_REG, 22), _("Inquadra con la fotocamera, accedi con il tuo account Omega e aggiungila alla schermata Home: sembra un'app."), qx + qs / 2, qy + qs + 112, qs + 60, 3, 30, C_DIM, a, AL_C);
  int x = 760, w = SCREEN_W - x - 110, y = 180;
  draw_text(font(W_LIGHT, 44), _("Omega sul telefono e sul PC"), x, y, C_WHITE, a, AL_L);
  draw_text_wrap(font(W_REG, 25), _("Anche a console spenta: la web app parla con il server di Omega, come la dash."), x, y + 64, w, 2, 34, C_DIM, a);
  const struct { int ic; const char *t; } F[] = {
    { IC_FRIENDS, N_("Chi è online, a cosa gioca, richieste di amicizia e profili") },
    { IC_CHAT, N_("Chat con gli amici e party con la chat di gruppo") },
    { IC_TROPHY, N_("Community, classifiche, record e trofei di tutti") },
    { IC_STORE, N_("Store completo, lista dei desideri e «Installa sulla PS5»") },
    { IC_BELL, N_("Notifiche, e da chi riceverle") },
    { IC_CLOUD, N_("Con la console accesa e sulla stessa rete: carica i giochi dal PC e gestisci La mia libreria e il JSON") } };
  for (int i = 0; i < 6; i++) {
    int yy = y + 150 + i * 70;
    fill_circle(x + 26, yy + 22, 24, mix(C_ACC, C_WHITE, 0.1f), a);
    draw_icon(F[i].ic, x + 26, yy + 22, 24, C_WHITE, a);
    draw_text_wrap(font(W_REG, 24), _(F[i].t), x + 68, yy + 6, w - 70, 2, 30, i == 5 ? C_ACC2 : C_TXT, a);
  }
  draw_text_wrap(font(W_REG, 21), _("I file dei giochi non passano mai dal server: il telefono li manda direttamente alla console sulla tua rete di casa."), x, y + 600, w, 2, 30, C_FAINT, a);
}

void remote_draw(float t) {
  int a = (int)(255 * t);
  Col bg = mix(RGB(10, 12, 20), g_theme_base, 0.12f);
  fill_rect(0, 0, SCREEN_W, SCREEN_H, bg, t > 0.98f ? 255 : a);
  glow(380, 520, 520, rmode ? C_ACC : RGB(196, 91, 255), a * 14 / 100);
  draw_icon(rmode ? IC_GLOBE : IC_CHAT, 132, 74, 44, C_ACC2, a);
  draw_text(font(W_LIGHT, 38), rmode ? _("Telecomando in Wi-Fi") : _("App mobile"), 172, 52, C_WHITE, a, AL_L);
  tabs(a);
  if (!rmode) {
    mobile_draw(a);
    int ic[1] = { IC_BTN_O }; const char *lb[1] = { _("Indietro") };
    hints(ic, lb, 1, a);
    return;
  }

  // codice QR
  int qs = 500, qx = 130, qy = 190;
  fill_rrect(qx - 20, qy - 20, qs + 40, qs + 40, 30, C_WHITE, a);
  if (state == 1 && ip[0]) { build_qr(url); if (qr) draw_tex(qr, qx, qy, qs, qs, a); }
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
  if (!rmode) return;
  if (b == B_X && state == 1) confirm_open(_("Creare un nuovo PIN? I telefoni già abbinati smetteranno di funzionare."), _("Nuovo PIN"), reset_yes, NULL);
}
