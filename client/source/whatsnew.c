// Omega UI — "Novità": al primo avvio dopo un aggiornamento mostra cosa è
// cambiato. La versione già vista sta in OMEGA_DIR/version-seen.txt; chi
// installa da zero (nessun update.json, nessuna versione vista) non la vede.
// L'elenco delle novità si aggiorna a ogni rilascio, insieme a OMEGA_VERSION.
#include "app.h"
#include <stdlib.h>
#include <sys/stat.h>

#define SEEN_FILE OMEGA_DIR "/version-seen.txt"

// Novità di questa versione: testi in italiano esatto dentro N_(), tradotti
// quando si disegnano (i18n/*.json).
static const char *const ITEMS[] = {
  N_("Nuovo: App mobile. Inquadra il codice dalla tessera nella home e hai Omega sul telefono e sul PC: chat, party, amici online, community, classifiche, trofei e Store, anche a console spenta."),
  N_("Dall'App mobile, con la console accesa sulla stessa rete, carichi i giochi dal PC e gestisci La mia libreria e il JSON: i file vanno dritti alla console, non passano dal server."),
  N_("\xC2\xABInstalla sulla PS5\xC2\xBB dallo Store del telefono: la console lo scarica e lo installa da sola, con la barra nella home."),
  N_("Notifiche come vuoi tu: per ogni tipo scegli tutti, solo preferiti o nessuno, silenzia un amico, imposta gli orari di silenzio e cosa vedere mentre giochi (Impostazioni \xE2\x80\xBA Account \xE2\x80\xBA Notifiche)."),
  N_("\xC2\xABUn amico \xC3\xA8 online\xC2\xBB e \xC2\xABUn amico inizia a giocare\xC2\xBB ora arrivano solo dagli amici preferiti: niente pi\xC3\xB9 notifiche continue."),
  N_("Archivio e spostamenti: sposta o copia i giochi tra memoria interna e dischi esterni, e i .pkg su un disco. Dalle opzioni di un gioco: \xC2\xABSposta su un altro disco\xC2\xBB."),
};
#define NITEMS (int)(sizeof ITEMS / sizeof *ITEMS)

static int checked;

static void mark_seen(void) {
  FILE *f = fopen(SEEN_FILE, "w");
  if (f) { fprintf(f, "%s\n", OMEGA_VERSION); fclose(f); }
}

// Nel ciclo principale: una volta, sulla home, quando non c'è altro aperto.
void whatsnew_tick(void) {
  if (checked || g_scene != SC_HOME || ov_depth() > 0) return;
  checked = 1;
  char *seen = file_read(SEEN_FILE, 64, NULL);
  struct stat st;
  int updated = seen ? strncmp(seen, OMEGA_VERSION, strlen(OMEGA_VERSION)) != 0
                     : stat(OMEGA_DIR "/update.json", &st) == 0;   // c'era una versione precedente
  free(seen);
  mark_seen();
  if (updated) { omega_log("novità della versione %s", OMEGA_VERSION); ov_push(OV_WHATSNEW); }
}

void whatsnew_draw(float t) {
  fill_rect(0, 0, SCREEN_W, SCREEN_H, RGB(2, 4, 10), (int)(170 * t));
  int w = 1240, h = 900, x = SCREEN_W / 2 - w / 2, y = SCREEN_H / 2 - h / 2 + (int)((1 - ease_out(t)) * 60);
  int a = (int)(255 * t);
  shadow_rrect(x, y, w, h, 34, 50, a * 70 / 100);
  fill_rrect(x, y, w, h, 34, C_PANEL, a);
  stroke_rrect(x, y, w, h, 34, 1, RGB(80, 92, 125), a / 2);
  glow(x + 110, y + 110, 120, RGB(40, 120, 255), a * 40 / 100);
  draw_logo(x + 110, y + 110, 90, a);
  draw_text(font(W_LIGHT, 50), _("Novità"), x + 190, y + 62, C_WHITE, a, AL_L);
  char v[96]; snprintf(v, sizeof v, _("Versione %s"), OMEGA_VERSION);
  draw_text(font(W_REG, 24), v, x + 192, y + 128, C_DIM, a, AL_L);
  int ly = y + 210;
  for (int i = 0; i < NITEMS && ly < y + h - 130; i++) {
    fill_circle(x + 82, ly + 18, 6, C_ACC2, a);
    int n = draw_text_wrap(font(W_REG, 27), _(ITEMS[i]), x + 108, ly, w - 170, 3, 37, C_TXT, a);
    ly += (n > 0 ? n : 1) * 37 + 24;
  }
  const int ic[] = { IC_BTN_X };
  const char *lb[] = { _("Ho capito") };
  hints(ic, lb, 1, a);
}

void whatsnew_input(int b) { if (b == B_X || b == B_O) ov_pop(); }
