// Omega UI — prima configurazione, dalla home, una domanda alla volta:
//  1. Omega come Home della console? (il demone la riapre all'avvio e quando
//     si torna alla Home di sistema). Non è obbligatorio: senza, Omega è
//     un'app come le altre e il demone pensa solo a musica e notifiche.
//  2. componenti per il jailbreak rilevato (hen.c): se manca qualcosa si
//     elenca e si installa solo dopo l'OK.
#include "app.h"
#include <stdlib.h>
#include <unistd.h>

#define HOME_MODE_FILE OMEGA_DIR "/home-mode.txt"   // "1" Omega come Home, "0" solo app (lo legge il demone)

static int step;                    // 0 domanda sulla Home, 1 componenti, 2 finito
static Uint32 asked_at;

static int mode_cache = -2;         // -2 = da leggere
int home_mode(void) {
  if (mode_cache == -2) {
    char *s = file_read(HOME_MODE_FILE, 16, NULL);
    mode_cache = s ? (s[0] == '1' ? 1 : 0) : -1;   // -1 = non ancora scelto
    free(s);
  }
  return mode_cache;
}
void home_mode_set(int on) {
  FILE *f = fopen(HOME_MODE_FILE, "w");
  if (f) { fputs(on ? "1\n" : "0\n", f); fclose(f); }
  mode_cache = on ? 1 : 0;
}

void home_mode_menu(void) { hen_ask_home(); }

// dal ciclo principale, solo in home e senza altri pannelli aperti
void setup_tick(void) {
  if (g_scene != SC_HOME || ov_depth() > 0 || install_busy()) return;
  Uint32 now = SDL_GetTicks();
  if (asked_at && now - asked_at < 1500) return;     // un attimo tra una domanda e l'altra
  if (step == 0) {
    if (home_mode() < 0) { asked_at = now; hen_ask_home(); }   // dopo la risposta home_mode() >= 0
    step = 1;
    return;
  }
  if (step == 1) {
    asked_at = now;
    step = 2;
    hen_check(1);   // propone l'installazione dei componenti mancanti
  }
}
