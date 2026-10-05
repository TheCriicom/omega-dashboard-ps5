// Omega UI — la musica del servizio suona qui. Sulla PS5 il servizio
// omega_redirect è un payload in background e non ha una sessione audio: la
// sua uscita resta muta. La UI invece è un'app ("bigapp") e il suo audio si
// sente. Quindi, mentre la UI è in primo piano, si collega a 127.0.0.1:9096 e
// riceve i blocchi già decodificati e col volume applicato (48 kHz, s16
// stereo); la callback audio (audio.c) li mescola. Il ritmo lo dà la lettura:
// quando l'anello è pieno non si legge, il servizio aspetta.
#include "app.h"
#include <errno.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#define PCM_PORT  9096
#define RING      (48000 / 4)        // 250 ms di margine
#define START_AT  (48000 / 12)       // si comincia a suonare con ~80 ms in coda

static int16_t pring[RING * 2];
static volatile int r_head, r_tail;  // in frame, modulo RING (un solo produttore, un solo consumatore)
static SDL_atomic_t linked, playing_now;

static int fill(void) { return (r_head - r_tail + RING) % RING; }

static int pcm_thread(void *arg) {
  (void)arg;
  static int16_t buf[1024 * 2];
  for (;;) {
    // collegati solo in primo piano: fuori, la UI può essere sospesa
    if (!g_ui_fg) { SDL_Delay(500); continue; }
    int s = socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) { SDL_Delay(2000); continue; }
    struct sockaddr_in a; memset(&a, 0, sizeof a);
    a.sin_family = AF_INET; a.sin_port = htons(PCM_PORT); a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (connect(s, (struct sockaddr *)&a, sizeof a) != 0) { close(s); SDL_Delay(3000); continue; }
    struct timeval tv = { 1, 0 }; setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    int small = 16 * 1024; setsockopt(s, SOL_SOCKET, SO_RCVBUF, &small, sizeof small);
    SDL_AtomicSet(&linked, 1);
    omega_log("musica: collegata all'uscita della UI");
    int partial = 0;                 // byte di un frame rimasti a metà
    unsigned char *pb = (unsigned char *)buf;
    for (;;) {
      if (!g_ui_fg) break;
      int room = RING - 1 - fill();
      if (room < 256) { SDL_Delay(5); continue; }        // pieno: il servizio aspetta
      int want = room < 1024 ? room : 1024;
      ssize_t k = recv(s, pb + partial, (size_t)want * 4 - (size_t)partial, 0);
      if (k == 0) break;
      if (k < 0) { if (errno == EAGAIN || errno == EWOULDBLOCK) { SDL_AtomicSet(&playing_now, 0); continue; } break; }
      int bytes = partial + (int)k, frames = bytes / 4;
      int h = r_head;
      for (int i = 0; i < frames; i++) { pring[h * 2] = buf[i * 2]; pring[h * 2 + 1] = buf[i * 2 + 1]; h = (h + 1) % RING; }
      r_head = h;
      partial = bytes - frames * 4;
      if (partial) memmove(pb, pb + frames * 4, (size_t)partial);
    }
    close(s);
    SDL_AtomicSet(&linked, 0); SDL_AtomicSet(&playing_now, 0);
    r_tail = r_head;
    omega_log("musica: uscita della UI scollegata");
    SDL_Delay(300);
  }
  return 0;
}

void pcmlink_init(void) { SDL_CreateThread(pcm_thread, "pcm", NULL); }

// Dalla callback audio: aggiunge fino a frames campioni stereo (float) a l/r.
// 1 se ha suonato qualcosa.
int pcmlink_mix(float *l, float *r, int frames) {
  int f = fill();
  if (!SDL_AtomicGet(&playing_now)) { if (f < START_AT) return 0; SDL_AtomicSet(&playing_now, 1); }
  int t = r_tail, n = f < frames ? f : frames;
  for (int i = 0; i < n; i++) { l[i] += pring[t * 2] / 32768.0f; r[i] += pring[t * 2 + 1] / 32768.0f; t = (t + 1) % RING; }
  r_tail = t;
  if (n < frames) SDL_AtomicSet(&playing_now, 0);         // vuoto: si riaspetta un po' di margine
  return n > 0;
}

int pcmlink_active(void) { return SDL_AtomicGet(&linked); }
