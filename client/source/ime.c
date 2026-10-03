// Omega UI — tastiera di sistema PS5 (sceImeDialog). Adattato dal sample
// offact di John Törnblom (GPL).
#include "omega.h"
#include <SDL.h>
#include <stdlib.h>
#include <wchar.h>

typedef enum { IME_NONE, IME_RUNNING, IME_FINISHED } SceImeDialogStatus;
typedef int (*SceImeTextFilter)(wchar_t*, uint32_t*, const wchar_t*, uint32_t);

typedef struct SceImeDialogParam {
  int userId;
  int type;
  uint64_t supportedLanguages;
  int enterLabel;
  int inputMethod;
  SceImeTextFilter filter;
  uint32_t option;
  uint32_t maxTextLength;
  wchar_t *inputTextBuffer;
  float posx, posy;
  int halign, valign;
  const wchar_t *placeholder;
  const wchar_t *title;
  int8_t reserved[16];
} SceImeDialogParam;

typedef struct SceImeDialogResult { int outcome; int8_t reserved[12]; } SceImeDialogResult;

int sceUserServiceGetForegroundUser(int*);
int sceImeDialogInit(const SceImeDialogParam*, void*);
int sceImeDialogGetResult(SceImeDialogResult*);
int sceImeDialogTerm(void);
SceImeDialogStatus sceImeDialogGetStatus(void);

#define SCE_IME_TYPE_BASIC_LATIN 1
#define SCE_IME_HALIGN_CENTER 1
#define SCE_IME_VALIGN_CENTER 1
#define IME_TITLE_LEN  0x80
#define IME_TEXT_LEN   0x200
#define IME_TIMEOUT_MS (10u * 60 * 1000)

static wchar_t g_title[IME_TITLE_LEN];
static wchar_t g_text[IME_TEXT_LEN];

int ime_input(const char *title, const char *initial, int is_password,
              char *out, size_t outlen, void (*render_frame)(void)) {
  (void)is_password;
  SceImeDialogParam p; memset(&p, 0, sizeof p);
  memset(g_title, 0, sizeof g_title); memset(g_text, 0, sizeof g_text);
  if (title) mbstowcs(g_title, title, IME_TITLE_LEN - 1);
  if (initial && *initial) mbstowcs(g_text, initial, IME_TEXT_LEN - 1);

  p.type = SCE_IME_TYPE_BASIC_LATIN;
  p.supportedLanguages = 0;
  p.inputTextBuffer = g_text;
  p.maxTextLength = IME_TEXT_LEN - 1;
  p.halign = SCE_IME_HALIGN_CENTER;
  p.valign = SCE_IME_VALIGN_CENTER;
  p.posx = SCREEN_W / 2; p.posy = SCREEN_H / 2;
  p.title = g_title;
  if (sceUserServiceGetForegroundUser(&p.userId) != 0) return 0;
  int rc = sceImeDialogInit(&p, NULL);
  if (rc != 0) {
    // una tastiera rimasta aperta blocca la nuova: si chiude e si riprova
    omega_log("sceImeDialogInit 0x%x: riprovo", rc);
    sceImeDialogTerm();
    rc = sceImeDialogInit(&p, NULL);
    if (rc != 0) { omega_log("sceImeDialogInit fail 0x%x", rc); return 0; }
  }

  // Si esce con qualsiasi stato diverso da "in corso", anche un codice di
  // errore: aspettando solo FINISHED l'app può restare bloccata per sempre.
  Uint32 t0 = SDL_GetTicks(), last_draw = 0;
  for (;;) {
    SDL_Event e; while (SDL_PollEvent(&e)) {}   // l'input è della tastiera
    int st = (int)sceImeDialogGetStatus();
    if (st != IME_RUNNING) { if (st != IME_FINISHED) omega_log("ime: stato %d", st); break; }
    if (SDL_GetTicks() - t0 > IME_TIMEOUT_MS) { omega_log("ime: timeout"); break; }
    // sfondo ridisegnato di rado, per non togliere tempo all'IME
    if (render_frame && SDL_GetTicks() - last_draw > 100) { render_frame(); last_draw = SDL_GetTicks(); }
    SDL_Delay(16);
  }
  SceImeDialogResult r; memset(&r, 0, sizeof r);
  sceImeDialogGetResult(&r);
  sceImeDialogTerm();
  if (r.outcome != 0) return 0;   // annullata
  wcstombs(out, g_text, outlen);
  out[outlen - 1] = '\0';
  return 1;
}
