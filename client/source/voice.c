// Omega UI — voce nel party. Con un party attivo partono due thread:
//  · tx: legge il microfono a 16 kHz mono, riconosce il parlato a frame da 20 ms
//    e manda tutto quello che è pronto (POST /party/voice) sulla stessa
//    connessione keep-alive: una connessione nuova a pacchetto costava più del
//    ritmo dei pacchetti e la voce accumulava secondi di ritardo;
//  · rx: long-poll di GET /party/voice (stessa connessione), decodifica i
//    pacchetti degli altri in un buffer per persona; la callback audio
//    (audio.c) li mescola.
// Quando la UI non è in primo piano (o si chiude per un gioco) la voce passa al
// servizio omega_redirect, che resta acceso (omega-redirect-src/source/voice.c).
// Codec Opus 16 kb/s su PS5 (libopus dell'SDK), IMA ADPCM sul desktop. Il server
// inoltra i pacchetti senza decodificarli. Senza microfono si resta in ascolto.
#include "app.h"
#include <math.h>
#include <stdlib.h>

#ifdef PS5
#define HAVE_OPUS 1
#include <opus/opus.h>
#endif

#define VSR      16000
#define FRAME    320                 // 20 ms
#define NFRAMES  10                  // al massimo 200 ms per pacchetto
#define CHUNK    (FRAME * NFRAMES)
#define MAX_BACKLOG 25               // oltre 500 ms non spediti si butta il più vecchio
#define HANG     15                  // frame di coda dopo il parlato (300 ms)
#define PREROLL  3                   // frame tenuti da parte per l'attacco delle parole
#define MAXSPK   8
#define JIT      (VSR * 2)           // 2 s di buffer per persona
#define PREBUF_MIN (VSR * 2 / 25)    // 80 ms accumulati prima di suonare...
#define PREBUF_MAX (VSR * 2 / 5)     // ...fino a 400 ms se la rete singhiozza
#define MAXQ     (VSR * 6 / 10)      // oltre 600 ms in coda si salta avanti
#define CATCHUP  (VSR * 2 / 10)
#define MIC_RING (VSR * 2)
#define VAD_RMS  300                 // soglia di energia per "sta parlando"
#define OUT_SR   48000

enum { CODEC_OPUS = 1, CODEC_ADPCM = 2 };

// ---------------------------------------------------------------- IMA ADPCM --
static const int STEP[89] = {
  7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66, 73, 80, 88, 97,
  107, 118, 130, 143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796,
  876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871,
  5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623,
  27086, 29794, 32767 };
static const int IDX[16] = { -1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8 };

static int adpcm_step(int nib, int *pred, int *idx) {
  int st = STEP[*idx], d = st >> 3;
  if (nib & 4) d += st;
  if (nib & 2) d += st >> 1;
  if (nib & 1) d += st >> 2;
  *pred += (nib & 8) ? -d : d;
  if (*pred > 32767) *pred = 32767; else if (*pred < -32768) *pred = -32768;
  *idx += IDX[nib]; if (*idx < 0) *idx = 0; else if (*idx > 88) *idx = 88;
  return *pred;
}
// blocco: [i16 pred][u8 idx][u8 0] + n/2 byte
static int adpcm_encode(const int16_t *pcm, int n, uint8_t *out) {
  int pred = pcm[0], idx = 0;
  out[0] = (uint8_t)(pred & 0xff); out[1] = (uint8_t)((pred >> 8) & 0xff); out[2] = 0; out[3] = 0;
  for (int i = 0; i < n; i++) {
    int diff = pcm[i] - pred, nib = 0, st = STEP[idx];
    if (diff < 0) { nib = 8; diff = -diff; }
    if (diff >= st) { nib |= 4; diff -= st; }
    if (diff >= st >> 1) { nib |= 2; diff -= st >> 1; }
    if (diff >= st >> 2) nib |= 1;
    adpcm_step(nib, &pred, &idx);
    if (i & 1) out[4 + i / 2] |= (uint8_t)(nib << 4); else out[4 + i / 2] = (uint8_t)nib;
  }
  return 4 + n / 2;
}
static int adpcm_decode(const uint8_t *in, int len, int16_t *pcm, int max) {
  if (len < 4) return 0;
  int pred = (int16_t)(in[0] | in[1] << 8), idx = in[2] > 88 ? 88 : in[2], n = (len - 4) * 2;
  if (n > max) n = max;
  for (int i = 0; i < n; i++) pcm[i] = (int16_t)adpcm_step((in[4 + i / 2] >> ((i & 1) * 4)) & 15, &pred, &idx);
  return n;
}

// -------------------------------------------------------------------- stato --
typedef struct {
  char oid[32];
  int16_t ring[JIT]; int head, fill, playing;
  int prebuf; Uint32 calm_since;     // margine adattivo
  Uint32 last;                       // ultimo pacchetto ricevuto
  float pos;                         // posizione frazionaria (16k → 48k)
#ifdef HAVE_OPUS
  OpusDecoder *dec;
#endif
} Speaker;

static Speaker spk[MAXSPK];
static SDL_atomic_t run_gen;         // cambia a ogni avvio/arresto: i thread vecchi escono
static SDL_atomic_t live_threads;    // thread della voce ancora in giro (per la chiusura dell'app)
static int state;                    // 0 spenta, 1 attiva, 2 solo ascolto
static Uint32 me_spoke;

// microfono: anello riempito dal thread di cattura
static int16_t mic[MIC_RING]; static int mic_w, mic_r; static SDL_mutex *mic_mx;

static void mic_push(const int16_t *p, int n) {
  SDL_LockMutex(mic_mx);
  for (int i = 0; i < n; i++) { mic[mic_w] = p[i]; mic_w = (mic_w + 1) % MIC_RING; if (mic_w == mic_r) mic_r = (mic_r + 1) % MIC_RING; }
  SDL_UnlockMutex(mic_mx);
}
static int mic_avail(void) {
  SDL_LockMutex(mic_mx); int have = (mic_w - mic_r + MIC_RING) % MIC_RING; SDL_UnlockMutex(mic_mx);
  return have;
}
static int mic_take(int16_t *p, int n) {
  SDL_LockMutex(mic_mx);
  int have = (mic_w - mic_r + MIC_RING) % MIC_RING;
  if (have < n) { SDL_UnlockMutex(mic_mx); return 0; }
  for (int i = 0; i < n; i++) { p[i] = mic[mic_r]; mic_r = (mic_r + 1) % MIC_RING; }
  SDL_UnlockMutex(mic_mx);
  return 1;
}

// ---------------------------------------------------------------- microfono --
#ifdef PS5
// libSceAudioIn non è tra le librerie dell'SDK: si carica a runtime.
#define MIC_BLOCK 256                // campioni per lettura di sceAudioInInput
int sceKernelLoadStartModule(const char *path, size_t argc, const void *argv, uint32_t flags, void *opt, int *res);
intptr_t kernel_dynlib_dlsym(int pid, uint32_t handle, const char *sym);
int getpid(void);
int sceUserServiceGetForegroundUser(uint32_t *uid);
typedef int (*in_open_t)(int, unsigned, unsigned, unsigned, unsigned, unsigned);
typedef int (*in_input_t)(int, void *);
typedef int (*in_close_t)(int);
static in_open_t in_open; static in_input_t in_input; static in_close_t in_close;

static int load_audioin(void) {
  if (in_open) return 1;
  int res = 0;
  int h = sceKernelLoadStartModule("/system/common/lib/libSceAudioIn.sprx", 0, NULL, 0, NULL, &res);
  if (h < 0) { omega_log("voce: libSceAudioIn non caricata 0x%x", h); return 0; }
  in_open = (in_open_t)kernel_dynlib_dlsym(getpid(), (uint32_t)h, "sceAudioInOpen");
  in_input = (in_input_t)kernel_dynlib_dlsym(getpid(), (uint32_t)h, "sceAudioInInput");
  in_close = (in_close_t)kernel_dynlib_dlsym(getpid(), (uint32_t)h, "sceAudioInClose");
  return in_open && in_input && in_close;
}

static int mic_handle = -1;
static int mic_open(void) {
  if (!load_audioin()) return 0;
  uint32_t uid = 0; sceUserServiceGetForegroundUser(&uid);
  // utente in primo piano o 255 (sistema), porta di tipo 0 o 1: vince la prima che si apre
  int users[2] = { (int)uid, 255 }; unsigned types[2] = { 0, 1 };
  for (int u = 0; u < 2; u++) for (int t = 0; t < 2; t++) {
    int h = in_open(users[u], types[t], 0, MIC_BLOCK, VSR, 0 /* S16 mono */);
    if (h >= 0) { mic_handle = h; omega_log("voce: microfono aperto (user 0x%x tipo %u)", users[u], types[t]); return 1; }
    omega_log("voce: microfono user 0x%x tipo %u -> 0x%x", users[u], types[t], h);
  }
  return 0;
}
static int cap_thread(void *arg) {
  int gen = (int)(intptr_t)arg; static int16_t buf[MIC_BLOCK];
  while (SDL_AtomicGet(&run_gen) == gen) {
    int r = in_input(mic_handle, buf);
    if (r < 0) { omega_log("voce: lettura microfono 0x%x", r); break; }
    mic_push(buf, MIC_BLOCK);
  }
  in_close(mic_handle); mic_handle = -1;
  SDL_AtomicAdd(&live_threads, -1);
  return 0;
}
static void mic_close(void) {}
#else
static SDL_AudioDeviceID cap_dev;
static void cap_cb(void *ud, Uint8 *stream, int len) { (void)ud; mic_push((const int16_t *)stream, len / 2); }
static int mic_open(void) {
  SDL_AudioSpec want, have; SDL_zero(want);
  want.freq = VSR; want.format = AUDIO_S16SYS; want.channels = 1; want.samples = 512; want.callback = cap_cb;
  cap_dev = SDL_OpenAudioDevice(NULL, 1, &want, &have, 0);
  if (!cap_dev) { omega_log("voce: nessun microfono (%s)", SDL_GetError()); return 0; }
  SDL_PauseAudioDevice(cap_dev, 0);
  return 1;
}
static void mic_close(void) { if (cap_dev) { SDL_CloseAudioDevice(cap_dev); cap_dev = 0; } }
#endif

// -------------------------------------------------------------------- invio --
static int i_am_muted(void) {
  for (int i = 0; i < S.party.nmembers; i++) if (!strcasecmp(S.party.members[i].oid, S.me)) return S.party.members[i].muted;
  return 0;
}

static int tx_thread(void *arg) {
  int gen = (int)(intptr_t)arg;
  static int16_t pcm[FRAME], pre[PREROLL][FRAME]; static uint8_t pkt[8192]; char path[96];
  uint32_t seq = 0; int hang = 0, npre = 0, fails = 0;
  OmegaKeep k = OMEGA_KEEP_INIT;
#ifdef HAVE_OPUS
  int err = 0; OpusEncoder *enc = opus_encoder_create(VSR, 1, OPUS_APPLICATION_VOIP, &err);
  if (enc) {
    opus_encoder_ctl(enc, OPUS_SET_BITRATE(20000)); opus_encoder_ctl(enc, OPUS_SET_INBAND_FEC(1));
    opus_encoder_ctl(enc, OPUS_SET_PACKET_LOSS_PERC(5)); opus_encoder_ctl(enc, OPUS_SET_SIGNAL(OPUS_SIGNAL_VOICE));
  }
#endif
  while (SDL_AtomicGet(&run_gen) == gen) {
    int have = mic_avail() / FRAME;
    if (have < 2) { SDL_Delay(10); continue; }             // si aspetta almeno 40 ms
    while (have > MAX_BACKLOG) { mic_take(pcm, FRAME); have--; }   // rete lenta: meglio un buco che secondi di ritardo
    int len = 0, frames = 0, adpcm_n = 0; const char *codec = "adpcm";
    static int16_t raw[CHUNK];                              // ADPCM: un blocco solo con tutti i frame
    for (int f = 0; f < have && f < NFRAMES; f++) {
      mic_take(pcm, FRAME);
      double e = 0; for (int i = 0; i < FRAME; i++) e += (double)pcm[i] * pcm[i];
      int onset = 0;
      if (sqrt(e / FRAME) > VAD_RMS) { onset = hang == 0; hang = HANG; }
      else if (hang > 0) hang--;
      else {   // silenzio: tenuto da parte nel caso stia per cominciare una parola
        if (npre == PREROLL) { memmove(pre[0], pre[1], sizeof pre[0] * (PREROLL - 1)); npre--; }
        memcpy(pre[npre++], pcm, sizeof pcm);
        continue;
      }
      if (i_am_muted() || !g_token[0]) { npre = 0; continue; }
      for (int q = 0; q <= (onset ? npre : 0); q++) {
        const int16_t *src = (onset && q < npre) ? pre[q] : pcm;
#ifdef HAVE_OPUS
        if (enc) {
          codec = "opus";
          if (len + 2 + 400 > (int)sizeof pkt) break;
          int n = opus_encode(enc, src, FRAME, pkt + len + 2, (opus_int32)(sizeof pkt - len - 2));
          if (n < 0) n = 0;
          pkt[len] = (uint8_t)(n & 0xff); pkt[len + 1] = (uint8_t)(n >> 8);
          len += 2 + n; frames++;
          continue;
        }
#endif
        if (adpcm_n + FRAME <= CHUNK) { memcpy(raw + adpcm_n, src, FRAME * sizeof *src); adpcm_n += FRAME; frames++; }
      }
      if (onset) npre = 0;
    }
    if (!frames) continue;
    if (!strcmp(codec, "adpcm")) len = adpcm_encode(raw, adpcm_n, pkt);
    me_spoke = SDL_GetTicks();
    snprintf(path, sizeof path, OMEGA_API "/party/voice?codec=%s&seq=%u", codec, seq++);
    int st = omega_keep_req(&k, 1, path, g_token, pkt, (size_t)len, "application/octet-stream", NULL, NULL, 0);
    if (st == 204) fails = 0;
    else {
      if (fails++ < 3) omega_log("voce: invio -> %d", st);
      if (st == 403) SDL_Delay(500); else if (st < 0) SDL_Delay(200);
    }
  }
#ifdef HAVE_OPUS
  if (enc) opus_encoder_destroy(enc);
#endif
  omega_keep_close(&k);
  SDL_AtomicAdd(&live_threads, -1);
  return 0;
}

// ---------------------------------------------------------------- ricezione --
static Speaker *speaker(const char *oid) {
  Speaker *free_s = NULL, *oldest = &spk[0];
  for (int i = 0; i < MAXSPK; i++) {
    if (!strcmp(spk[i].oid, oid)) return &spk[i];
    if (!spk[i].oid[0] && !free_s) free_s = &spk[i];
    if (spk[i].last < oldest->last) oldest = &spk[i];
  }
  Speaker *s = free_s ? free_s : oldest;
  audio_lock(1);
  snprintf(s->oid, sizeof s->oid, "%s", oid); s->head = s->fill = s->playing = 0; s->pos = 0;
  s->prebuf = PREBUF_MIN * 3 / 2; s->calm_since = SDL_GetTicks();
  audio_lock(0);
  return s;
}

static void feed(Speaker *s, const int16_t *p, int n) {
  audio_lock(1);
  for (int i = 0; i < n; i++) {
    if (s->fill >= JIT) { s->head = (s->head + 1) % JIT; s->fill--; }      // troppo in ritardo: si scarta il più vecchio
    s->ring[(s->head + s->fill) % JIT] = p[i]; s->fill++;
  }
  // troppo in ritardo (la rete ha consegnato tutto insieme): si salta avanti
  if (s->fill > MAXQ + s->prebuf) { int drop = s->fill - CATCHUP - s->prebuf; s->head = (s->head + drop) % JIT; s->fill -= drop; }
  s->last = SDL_GetTicks();
  audio_lock(0);
}

static void play_chunk(const char *oid, int codec, const uint8_t *d, int len) {
  static int16_t pcm[CHUNK * 2];
  Speaker *s = speaker(oid);
  int n = 0;
  if (codec == CODEC_ADPCM) n = adpcm_decode(d, len, pcm, CHUNK * 2);
#ifdef HAVE_OPUS
  else if (codec == CODEC_OPUS) {
    if (!s->dec) { int err; s->dec = opus_decoder_create(VSR, 1, &err); }
    for (int off = 0; s->dec && off + 2 <= len && n + FRAME <= CHUNK * 2;) {
      int fl = d[off] | d[off + 1] << 8; off += 2;
      if (off + fl > len) break;
      int k = opus_decode(s->dec, fl ? d + off : NULL, fl, pcm + n, FRAME, 0);
      if (k > 0) n += k;
      off += fl;
    }
  }
#endif
  if (n > 0) feed(s, pcm, n);
}

static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
static uint32_t rd32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }

static int rx_thread(void *arg) {
  int gen = (int)(intptr_t)arg;
  uint32_t cursor = 0; char path[96];
  OmegaKeep k = OMEGA_KEEP_INIT;
  while (SDL_AtomicGet(&run_gen) == gen) {
    snprintf(path, sizeof path, OMEGA_API "/party/voice?after=%u&wait=1", cursor);
    unsigned char *b = NULL; size_t len = 0;
    Uint32 t0 = SDL_GetTicks();
    int st = omega_keep_req(&k, 0, path, g_token[0] ? g_token : NULL, NULL, 0, NULL, &b, &len, 256 * 1024);
    // il server tiene aperta la richiesta fino a 1,5 s: se risponde subito e a
    // vuoto si aspetta un attimo, per non aprire connessioni a raffica
    if (SDL_GetTicks() - t0 < 100 && (st != 200 || len < 12)) SDL_Delay(300);
    // pacchetto: "OVC1", cursore u32, conteggio u16, poi per ogni voce
    // [len u8][online_id][codec u8][seq u32][dati u16][dati]
    if (st != 200 || len < 10 || memcmp(b, "OVC1", 4)) { free(b); SDL_Delay(st == 403 ? 2000 : 500); continue; }
    cursor = rd32(b + 4);
    int count = rd16(b + 8); size_t o = 10;
    for (int i = 0; i < count && o < len; i++) {
      int ol = b[o++]; if (o + (size_t)ol + 7 > len) break;
      char oid[40]; snprintf(oid, sizeof oid, "%.*s", ol > 39 ? 39 : ol, (const char *)b + o); o += (size_t)ol;
      int codec = b[o++]; o += 4;                       // seq ignorato: l'ordine lo dà il cursore
      int dl = rd16(b + o); o += 2;
      if (o + (size_t)dl > len) break;
      if (SDL_AtomicGet(&run_gen) == gen) play_chunk(oid, codec, b + o, dl);
      o += (size_t)dl;
    }
    free(b);
  }
  omega_keep_close(&k);
  SDL_AtomicAdd(&live_threads, -1);
  return 0;
}

// ----------------------------------------------------- mix (callback audio) --
int voice_fill(float *out, int frames) {
  if (!state) return 0;
  int any = 0;
  for (int i = 0; i < frames; i++) out[i] = 0;
  for (int k = 0; k < MAXSPK; k++) {
    Speaker *s = &spk[k];
    if (!s->oid[0]) continue;
    if (!s->playing) { if (s->fill < s->prebuf) continue; s->playing = 1; }
    for (int i = 0; i < frames; i++) {
      if (s->fill < 2) {
        s->playing = 0;
        // vuoto a metà frase: la rete singhiozza, si alza il margine di 40 ms
        if (SDL_GetTicks() - s->last < 1000) { s->calm_since = SDL_GetTicks(); if (s->prebuf < PREBUF_MAX) s->prebuf += VSR / 25; }
        break;
      }
      // da 16 a 48 kHz: interpolazione lineare
      float a = s->ring[s->head] / 32768.0f, b = s->ring[(s->head + 1) % JIT] / 32768.0f;
      out[i] += (a + (b - a) * s->pos) * 0.9f;
      s->pos += (float)VSR / OUT_SR;
      if (s->pos >= 1) { s->pos -= 1; s->head = (s->head + 1) % JIT; s->fill--; }
    }
    if (s->prebuf > PREBUF_MIN && SDL_GetTicks() - s->calm_since > 20000) { s->prebuf -= VSR / 50; s->calm_since = SDL_GetTicks(); }
    any = 1;
  }
  if (any) for (int i = 0; i < frames; i++) { float x = out[i]; out[i] = x / (1 + fabsf(x)) * 1.4f; }   // limitatore morbido
  return any;
}

// ---------------------------------------------------------------- controllo --
static void stop(void) {
  if (!state) return;
  SDL_AtomicAdd(&run_gen, 1);
  mic_close();
  audio_lock(1);
  for (int k = 0; k < MAXSPK; k++) spk[k].oid[0] = 0, spk[k].fill = 0, spk[k].playing = 0;
  audio_lock(0);
  state = 0;
  omega_log("voce: spenta");
}

static void spawn(SDL_ThreadFunction fn, const char *name, int gen) {
  SDL_AtomicAdd(&live_threads, 1);
  SDL_Thread *t = SDL_CreateThread(fn, name, (void *)(intptr_t)gen);
  if (t) SDL_DetachThread(t); else SDL_AtomicAdd(&live_threads, -1);
}

static void start(void) {
  if (!mic_mx) mic_mx = SDL_CreateMutex();
  mic_w = mic_r = 0;
  int gen = SDL_AtomicAdd(&run_gen, 1) + 1;
  int has_mic = mic_open();
  // staccati: escono da soli al cambio di generazione; live_threads li conta
#ifdef PS5
  if (has_mic) spawn(cap_thread, "mic", gen);
#endif
  if (has_mic) spawn(tx_thread, "voice-tx", gen);
  spawn(rx_thread, "voice-rx", gen);
  state = has_mic ? 1 : 2;
  omega_log("voce: %s", has_mic ? "attiva" : "solo ascolto (microfono non disponibile)");
}

// La voce nella UI solo in primo piano e da almeno 2 s: prima il servizio deve
// accorgersi che la UI è tornata e chiudere il suo microfono. Se il microfono
// non si apre si riprova ogni 5 s (fino a 6 volte): può essere ancora suo.
void voice_tick(void) {
  static Uint32 started_at; static int mic_retries;
  Uint32 now = SDL_GetTicks();
  int want = S.party.active && g_token[0] && g_scene == SC_HOME && g_ui_fg && now - g_ui_fg_since > 2000;
  if (want && !state) { start(); started_at = now; if (state == 1) mic_retries = 0; }
  else if (!want && state) { stop(); mic_retries = 0; }
  else if (state == 2 && mic_retries < 6 && now - started_at > 5000) { mic_retries++; stop(); start(); started_at = now; }
}

// Alla chiusura dell'app: ferma la voce e aspetta (al massimo 3 s) che i thread
// escano, prima che SDL e la rete vengano chiusi sotto i loro piedi.
void voice_shutdown(void) {
  stop();
  Uint32 t0 = SDL_GetTicks();
  while (SDL_AtomicGet(&live_threads) > 0 && SDL_GetTicks() - t0 < 3000) SDL_Delay(20);
  if (SDL_AtomicGet(&live_threads) > 0) omega_log("voce: %d thread ancora attivi alla chiusura", SDL_AtomicGet(&live_threads));
}

int voice_state(void) { return state; }

int voice_speaking(const char *oid) {
  Uint32 now = SDL_GetTicks();
  if (!strcasecmp(oid, S.me)) return state == 1 && now - me_spoke < 500;
  for (int k = 0; k < MAXSPK; k++) if (!strcasecmp(spk[k].oid, oid) && (spk[k].playing || now - spk[k].last < 400)) return 1;
  for (int i = 0; i < S.party.nmembers; i++) if (!strcasecmp(S.party.members[i].oid, oid)) return S.party.members[i].talking;
  return 0;
}

// Comando di debug "micprobe": apre il microfono, legge 2 s e scrive il picco nel log.
void mic_probe(void) {
  if (!mic_mx) mic_mx = SDL_CreateMutex();
  omega_log("mic: prova");
  if (!mic_open()) { omega_log("mic: nessuna combinazione accettata"); return; }
#ifdef PS5
  static int16_t buf[MIC_BLOCK]; int peak = 0, blocks = 0;
  for (int i = 0; i < 2 * VSR / MIC_BLOCK; i++) {
    if (in_input(mic_handle, buf) < 0) break;
    blocks++; for (int k = 0; k < MIC_BLOCK; k++) { int v = abs(buf[k]); if (v > peak) peak = v; }
  }
  in_close(mic_handle); mic_handle = -1;
  omega_log("mic: %d blocchi, picco %d", blocks, peak);
#else
  SDL_Delay(2000); mic_close(); omega_log("mic: desktop ok");
#endif
}
