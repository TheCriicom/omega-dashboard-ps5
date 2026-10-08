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
  N_("Nuovo: Il tuo riepilogo, come il Wrap-Up di PlayStation. La tua settimana, il tuo mese o il tuo anno di gioco raccontati a schede: tempo giocato, il gioco preferito, il tuo ritmo, i record, i trofei e gli amici con cui hai giocato."),
  N_("Alla fine scopri il tuo profilo di giocatore (Nottambulo, Maratoneta, Esploratore...) e lo condividi sulla bacheca. Lo trovi in Community \xE2\x80\xBA Tempo di gioco con Triangolo, e ogni lunedì ti avvisa quando quello della settimana è pronto."),
  N_("Nella web app: Profilo \xE2\x80\xBA Il tuo riepilogo, con l'immagine da condividere dove vuoi."),
  N_("Nuovo: Salvataggi online, come su PS Plus. Dopo ogni partita i salvataggi cambiati vanno online da soli e li ripristini su questa o su un'altra console (Impostazioni \xE2\x80\xBA Home e giochi, oppure dal menu del gioco)."),
  N_("Sono cifrati sulla console con una chiave protetta dalla tua parola d'ordine: il server riceve solo dati illeggibili e, se qualcuno li toccasse, la console se ne accorge e non li ripristina."),
  N_("Prima di ogni ripristino Omega tiene una copia dei salvataggi della console: \xC2\xAB" "Annulla l'ultimo ripristino\xC2\xBB li rimette com'erano."),
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
