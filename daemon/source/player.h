// Omega — lettore musicale del demone: suona anche mentre si gioca.
// FFmpeg apre file, flussi http(s) e radio HLS; l'uscita è a 48 kHz stereo
// (sceAudioOut sulla console, SDL nella build desktop).
#pragma once
#include <stddef.h>

#define PLAYER_MAX_ITEMS 1000

typedef struct {
  char url[1024];
  char title[256], artist[256], album[256];
  char cover[1024];      // immagine (URL); vuoto = copertina dentro il file, se c'è
  char source[32];       // "radio", "subsonic", "jellyfin", "file"... solo per la UI
  char id[128];          // id nella sorgente, solo per la UI
  double dur;            // secondi, 0 = sconosciuta o diretta
} PlayerItem;

enum { PL_STOPPED, PL_LOADING, PL_PLAYING, PL_PAUSED, PL_ERROR };
enum { PL_REPEAT_OFF, PL_REPEAT_ALL, PL_REPEAT_ONE };

void player_init(const char *state_file);   // carica coda e volume salvati (non riparte da solo)

// coda: mode 0 = sostituisci, 1 = in fondo, 2 = dopo il brano corrente
void player_enqueue(const PlayerItem *items, int n, int mode, int start, int play);
void player_play(void);
void player_pause(void);
void player_toggle(void);
void player_stop(void);
void player_next(void);
void player_prev(void);
void player_jump(int index);
void player_remove(int index);
void player_clear(void);
void player_seek(double sec);
void player_volume(int v);         // 0-100
void player_shuffle(int on);
void player_repeat(int mode);

// stato in JSON per il server di controllo; restituisce i byte scritti
size_t player_state_json(char *out, size_t n);
size_t player_queue_json(char *out, size_t n);
// copertina incorporata nel file corrente (jpeg/png); 0 se non c'è
size_t player_cover(unsigned char *out, size_t n, const char **mime);

// chiamata dal demone a ogni cambio di brano (per la notifica durante i giochi)
typedef void (*player_track_cb)(const char *title, const char *artist);
void player_on_track(player_track_cb cb);
int  player_is_playing(void);
// power.c: 1 = riposo (pausa, audio e porta 9096 chiusi, stato su disco), 0 = di nuovo acceso (resta in pausa)
void player_power(int sleeping);
