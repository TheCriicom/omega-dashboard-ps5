// omega_redirect — voce del party quando la UI non è in primo piano.
//
// La UI si chiude per lasciare memoria al gioco: da lì in poi la voce la porta
// avanti il servizio, che resta acceso. Quando la UI torna in primo piano
// (ui-active aggiornato da meno di UI_VOICE_S secondi) il servizio la lascia a
// lei, così il microfono lo apre un processo solo.
//
// Quattro thread per sessione, più il gestore:
//  · mic: legge il microfono (libSceAudioIn caricata a runtime) a 16 kHz mono;
//  · tx:  riconosce il parlato a frame da 20 ms, li codifica in Opus e manda
//         tutto quello che è pronto in un POST /party/voice sulla STESSA
//         connessione keep-alive (una connessione nuova costa 250-400 ms, più
//         del ritmo dei pacchetti: la voce accumulava secondi di ritardo);
//  · rx:  long-poll GET /party/voice, sempre sulla stessa connessione, decodifica
//         in un buffer per persona;
//  · out: mescola le persone, porta a 48 kHz e suona con sceAudioOut.
// Il formato dei pacchetti è quello della UI (voice.c): [u16 len][opus] per frame.
#include <fcntl.h>
#include <math.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <opus/opus.h>

#include "ctl.h"
#include "json.h"
#include "voice.h"

void player_log(const char *fmt, ...);
#define lg player_log

#define VSR        16000
#define FRAME      320                  // 20 ms
#define OSR        48000
#define OGRAIN     256                  // campioni per sceAudioOutOutput
#define MIC_GRAIN  256
#define MIC_RING   (VSR * 2)
#define MAXSPK     8
#define SPK_RING   (VSR * 2)            // 2 s per persona
#define PREBUF_MIN (VSR * 2 / 25)       // 80 ms accumulati prima di suonare...
#define PREBUF_MAX (VSR * 2 / 5)        // ...che crescono fino a 400 ms se la rete singhiozza
#define MAXQ       (VSR * 6 / 10)       // oltre 600 ms in coda si salta avanti...
#define CATCHUP    (VSR * 2 / 10)       // ...fino a lasciarne 200 ms
#define VAD_RMS    300
#define PREROLL    3                    // frame tenuti da parte: l'attacco delle parole non si taglia
#define HANG       15                   // frame di coda dopo il parlato (300 ms)
#define MAX_BATCH  10                   // al massimo 200 ms per POST
#define MAX_BACKLOG 25                  // oltre 500 ms non spediti si butta il più vecchio
#define UI_VOICE_S 5                    // la UI aggiorna ui-active ogni 2 s
#define PARTY_EVERY_S 4

enum { CODEC_OPUS = 1, CODEC_ADPCM = 2 };

// Protezione dai crash: la voce dal servizio non è mai stata provata su tutte le
// console. Prima dei passi delicati si scrive dove si è; se il servizio muore lì,
// al riavvio si salta quel passo: niente microfono dopo un crash nell'aprirlo,
// niente voce nel servizio dopo due crash con la voce accesa. Musica e notifiche
// non devono pagare per la voce. Si azzera con una versione nuova del servizio.
#ifndef GUARD_FILE
#define GUARD_FILE "/data/Omega/voice-guard.txt"   // "<stato> <crash> <build>"
#endif
static int guard_no_mic, guard_off, guard_crashes;
static volatile int guard_phase;                    // 0 niente, 1 apre il microfono, 2 voce accesa
static const char BUILD_ID[] = __DATE__ " " __TIME__;
static void guard_write(const char *state) {
  FILE *f = fopen(GUARD_FILE, "w");
  if (f) { fprintf(f, "%s %d %s\n", state, guard_crashes, BUILD_ID); fclose(f); }
}
// Dal gestore dei crash (main.c): solo open/write, niente stdio.
void voice_on_crash(void) {
  if (!guard_phase) return;
  char b[128]; const char *st = guard_phase == 1 ? "miccrash" : "runcrash";
  int n = snprintf(b, sizeof b, "%s %d %s\n", st, guard_crashes, BUILD_ID);
  int fd = open(GUARD_FILE, O_WRONLY | O_CREAT | O_TRUNC, 0666);
  if (fd >= 0) { if (write(fd, b, (size_t)n) < 0) {} close(fd); }
}
static void guard_load(void) {
  FILE *f = fopen(GUARD_FILE, "r"); if (!f) return;
  char st[16] = "", build[64] = ""; int n = 0;
  int got = fscanf(f, "%15s %d %63[^\n]", st, &n, build);
  fclose(f);
  if (got < 3 || strcmp(build, BUILD_ID)) { remove(GUARD_FILE); return; }   // servizio nuovo: si riprova tutto
  guard_crashes = n;
  // una console spenta a metà party non lascia "crash": conta solo il gestore dei crash
  if (!strcmp(st, "nomic")) guard_no_mic = 1;
  if (!strcmp(st, "miccrash")) { guard_no_mic = 1; lg("voce: il servizio si era fermato aprendo il microfono: da ora solo ascolto"); }
  if (!strcmp(st, "runcrash")) { guard_crashes++; lg("voce: il servizio si era fermato con la voce accesa (%d)", guard_crashes); }
  if (!strcmp(st, "off") || guard_crashes >= 2) { guard_off = 1; lg("voce: disattivata nel servizio dopo %d arresti", guard_crashes); }
}

// ------------------------------------------------------------- dipendenze --
static const char *base_url;            // OMEGA_BASE_URL, da voice_start
static int (*get_tmpl)(void);
static int (*read_session)(char *json, size_t n);
static int (*ui_active_age)(void);      // secondi dall'ultimo ui-active, -1 se manca
static void (*notify)(const char *msg);
static const char *(*lang_code)(void);
const char *i18n_tr(const char *msgid) __attribute__((format_arg(1)));
#define _(s) i18n_tr(s)

// ------------------------------------------------------------------ SceHttp --
int sceHttpCreateConnectionWithURL(int tmpl, const char *url, int keepAlive);
int sceHttpCreateRequestWithURL(int conn, int method, const char *url, uint64_t clen);
int sceHttpAddRequestHeader(int id, const char *name, const char *value, uint32_t mode);
int sceHttpSendRequest(int req, const void *data, size_t size);
int sceHttpGetStatusCode(int req, int *status);
int sceHttpReadData(int req, void *data, size_t size);
int sceHttpDeleteRequest(int req);
int sceHttpDeleteConnection(int conn);
int sceHttpSetRecvTimeOut(int id, uint32_t usec);

// Una connessione che resta aperta: le richieste dello stesso thread la riusano.
typedef struct { int conn; char host[200]; } Keep;
static void keep_close(Keep *k) { if (k->conn >= 0) sceHttpDeleteConnection(k->conn); k->conn = -1; }

// method 0 GET, 1 POST. Ritorna lo stato HTTP o <0; *out (malloc) solo per le GET.
static int keep_req(Keep *k, int method, const char *url, const char *token, const void *body, size_t blen,
                    const char *ctype, unsigned char **out, size_t *olen, size_t max) {
  if (out) { *out = NULL; *olen = 0; }
  int tmpl = get_tmpl(); if (tmpl < 0) return -1;
  for (int attempt = 0; attempt < 2; attempt++) {
    if (k->conn < 0) { k->conn = sceHttpCreateConnectionWithURL(tmpl, url, 1); if (k->conn < 0) return k->conn; }
    int req = sceHttpCreateRequestWithURL(k->conn, method, url, (uint64_t)blen);
    if (req < 0) { keep_close(k); continue; }
    sceHttpSetRecvTimeOut(req, 6 * 1000 * 1000);
    char auth[760]; snprintf(auth, sizeof auth, "Bearer %s", token);
    sceHttpAddRequestHeader(req, "Authorization", auth, 1);
    if (ctype) sceHttpAddRequestHeader(req, "Content-Type", ctype, 1);
    sceHttpAddRequestHeader(req, "Accept-Language", lang_code(), 1);
    int status = -1;
    int rc = sceHttpSendRequest(req, body, blen);
    if (rc >= 0) {
      sceHttpGetStatusCode(req, &status);
      size_t cap = 4096, total = 0; unsigned char *buf = out ? malloc(cap + 1) : NULL; int r;   // +1 per lo zero finale
      unsigned char sink[512];
      for (;;) {
        if (buf && total == cap) {
          if (cap >= max) break;
          unsigned char *nb = realloc(buf, cap * 2 + 1); if (!nb) break; buf = nb; cap *= 2;
        }
        r = buf ? sceHttpReadData(req, buf + total, cap - total) : sceHttpReadData(req, sink, sizeof sink);
        if (r <= 0) break;
        if (buf) total += (size_t)r;
      }
      if (buf) buf[total] = 0;
      if (out) { *out = buf; *olen = total; }
    }
    sceHttpDeleteRequest(req);
    if (rc >= 0) return status;
    keep_close(k);            // connessione caduta: se ne apre una nuova, una volta
  }
  return -1;
}

// ----------------------------------------------------------------- stato --
typedef struct {
  char oid[40];
  int16_t ring[SPK_RING]; int head, fill, playing;
  int prebuf;                         // buffer di partenza, adattivo
  int underruns; time_t calm_since;   // vuoti mentre parlava, per alzare o abbassare prebuf
  double pos;
  time_t last;
  OpusDecoder *dec;
} Speaker;

static pthread_mutex_t mx = PTHREAD_MUTEX_INITIALIZER;
static Speaker *spk;                         // MAXSPK, allocati una volta
static int16_t mic[MIC_RING]; static int mic_w, mic_r;
static volatile int gen;                     // cambia a ogni avvio/arresto: i thread vecchi escono
static volatile int live;                    // thread della sessione ancora vivi
static volatile int running;                 // sessione attiva
static volatile int mic_ok;                  // 1 microfono aperto
static volatile int mic_silent_s;            // secondi di fila con il microfono a zero
static volatile int muted;                   // muto (dal server o dal comando locale)
static volatile int me_speaking;
static volatile int left;                    // uscito dal party con il comando: niente riavvio fino al prossimo controllo
static char me[40], party_name[80], url_base[200], tok[700];
static int nmembers;
static time_t last_party_ok;

static void party_mute_remote(int on);

// ---------------------------------------------------------------- microfono --
int sceKernelLoadStartModule(const char *path, size_t argc, const void *argv, uint32_t flags, void *opt, int *res);
intptr_t kernel_dynlib_dlsym(int pid, uint32_t handle, const char *sym);
int getpid(void);
typedef int (*in_open_t)(int, int, int, unsigned, unsigned, unsigned);
typedef int (*in_input_t)(int, void *);
typedef int (*in_close_t)(int);
typedef int (*in_silent_t)(int);
static in_open_t in_open; static in_input_t in_input; static in_close_t in_close; static in_silent_t in_silent;
static int mic_handle = -1;

static int load_audioin(void) {
  if (in_open) return 1;
  int res = 0;
  int h = sceKernelLoadStartModule("/system/common/lib/libSceAudioIn.sprx", 0, NULL, 0, NULL, &res);
  if (h < 0) { lg("voce: libSceAudioIn non caricata 0x%x", h); return 0; }
  in_open = (in_open_t)kernel_dynlib_dlsym(getpid(), (uint32_t)h, "sceAudioInOpen");
  in_input = (in_input_t)kernel_dynlib_dlsym(getpid(), (uint32_t)h, "sceAudioInInput");
  in_close = (in_close_t)kernel_dynlib_dlsym(getpid(), (uint32_t)h, "sceAudioInClose");
  in_silent = (in_silent_t)kernel_dynlib_dlsym(getpid(), (uint32_t)h, "sceAudioInGetSilentState");
  if (!(in_open && in_input && in_close)) { in_open = NULL; lg("voce: simboli di libSceAudioIn mancanti"); return 0; }
  return 1;
}

// utente in primo piano, da libSceUserService caricata a runtime (se manca il
// servizio deve partire lo stesso: musica e notifiche non ne dipendono)
int foreground_user(void) {
  static int (*get_fg)(int *); static int tried;
  if (!tried) {
    tried = 1; int res = 0;
    int h = sceKernelLoadStartModule("/system/common/lib/libSceUserService.sprx", 0, NULL, 0, NULL, &res);
    if (h >= 0) {
      int (*init)(void *) = (int (*)(void *))kernel_dynlib_dlsym(getpid(), (uint32_t)h, "sceUserServiceInitialize");
      get_fg = (int (*)(int *))kernel_dynlib_dlsym(getpid(), (uint32_t)h, "sceUserServiceGetForegroundUser");
      if (init) init(NULL);
    } else lg("voce: libSceUserService non caricata 0x%x", h);
  }
  int uid = -1;
  if (get_fg) get_fg(&uid);
  return uid;
}

static int mic_open(void) {
  if (!load_audioin()) return 0;
  int uid = foreground_user();
  // prima l'utente vero (il microfono è legato all'utente del controller), poi il sistema;
  // tipo 0 = chat vocale, 1 = generico
  int users[2] = { uid, 255 }; int types[2] = { 0, 1 };
  for (int u = 0; u < 2; u++) for (int t = 0; t < 2; t++) {
    if (users[u] < 0) continue;
    int h = in_open(users[u], types[t], 0, MIC_GRAIN, VSR, 0 /* S16 mono */);
    if (h >= 0) { mic_handle = h; lg("voce: microfono aperto (utente 0x%x, tipo %d)", users[u], types[t]); return 1; }
    lg("voce: microfono utente 0x%x tipo %d -> 0x%x", users[u], types[t], h);
  }
  return 0;
}

static void mic_push(const int16_t *p, int n) {
  pthread_mutex_lock(&mx);
  for (int i = 0; i < n; i++) { mic[mic_w] = p[i]; mic_w = (mic_w + 1) % MIC_RING; if (mic_w == mic_r) mic_r = (mic_r + 1) % MIC_RING; }
  pthread_mutex_unlock(&mx);
}
static int mic_avail(void) { pthread_mutex_lock(&mx); int n = (mic_w - mic_r + MIC_RING) % MIC_RING; pthread_mutex_unlock(&mx); return n; }
static void mic_take(int16_t *p, int n) {
  pthread_mutex_lock(&mx);
  for (int i = 0; i < n; i++) { p[i] = mic[mic_r]; mic_r = (mic_r + 1) % MIC_RING; }
  pthread_mutex_unlock(&mx);
}

static void *mic_thread(void *arg) {
  int g = (int)(intptr_t)arg;
  int16_t buf[MIC_GRAIN]; int zero_blocks = 0, last_mask = -1;
  while (gen == g) {
    int r = in_input(mic_handle, buf);
    if (r < 0) { lg("voce: lettura microfono 0x%x", r); break; }
    int peak = 0; for (int i = 0; i < MIC_GRAIN; i++) { int v = buf[i] < 0 ? -buf[i] : buf[i]; if (v > peak) peak = v; }
    // tutto a zero per secondi: microfono spento, muto col tasto, o il gioco ha la priorità
    if (peak == 0) zero_blocks++; else zero_blocks = 0;
    mic_silent_s = zero_blocks * MIC_GRAIN / VSR;
    if (in_silent && (zero_blocks == 0 || zero_blocks % 625 == 1)) {
      int m = in_silent(mic_handle);
      if (m != last_mask) { lg("voce: stato del microfono 0x%x (picco %d)", m, peak); last_mask = m; }
    }
    mic_push(buf, MIC_GRAIN);
  }
  in_close(mic_handle); mic_handle = -1; mic_ok = 0;
  __sync_fetch_and_sub(&live, 1);
  return NULL;
}

// -------------------------------------------------------------------- invio --
static void *tx_thread(void *arg) {
  int g = (int)(intptr_t)arg;
  Keep k = { -1, "" };
  int err = 0;
  OpusEncoder *enc = opus_encoder_create(VSR, 1, OPUS_APPLICATION_VOIP, &err);
  if (enc) {
    opus_encoder_ctl(enc, OPUS_SET_BITRATE(20000));
    opus_encoder_ctl(enc, OPUS_SET_INBAND_FEC(1));
    opus_encoder_ctl(enc, OPUS_SET_PACKET_LOSS_PERC(5));
    opus_encoder_ctl(enc, OPUS_SET_SIGNAL(OPUS_SIGNAL_VOICE));
  } else lg("voce: encoder Opus non creato (%d)", err);
  static uint8_t pkt[8192]; int16_t pcm[FRAME];
  static int16_t pre[PREROLL][FRAME]; int npre = 0;   // ultimi frame silenziosi, per l'attacco
  uint32_t seq = (uint32_t)time(NULL) << 8; int hang = 0, fails = 0;
  char url[300], ptoken[700];
  while (gen == g && enc) {
    int have = mic_avail() / FRAME;
    if (have < 2) { usleep(10 * 1000); continue; }          // si aspetta almeno 40 ms
    // rete lenta: si butta il più vecchio, meglio un buco che secondi di ritardo
    while (have > MAX_BACKLOG) { mic_take(pcm, FRAME); have--; }
    int len = 0, frames = 0;
    for (int f = 0; f < have && f < MAX_BATCH; f++) {
      mic_take(pcm, FRAME);
      double e = 0; for (int i = 0; i < FRAME; i++) e += (double)pcm[i] * pcm[i];
      int onset = 0;
      if (sqrt(e / FRAME) > VAD_RMS) { onset = hang == 0; hang = HANG; }
      else if (hang > 0) hang--;
      else {   // silenzio: lo si tiene da parte nel caso stia per cominciare una parola
        if (npre == PREROLL) { memmove(pre[0], pre[1], sizeof pre[0] * (PREROLL - 1)); npre--; }
        memcpy(pre[npre++], pcm, sizeof pcm);
        continue;
      }
      if (muted) { npre = 0; continue; }
      for (int q = 0; q <= (onset ? npre : 0); q++) {
        const int16_t *src = (onset && q < npre) ? pre[q] : pcm;
        if (len + 2 + 400 > (int)sizeof pkt) break;
        int n = opus_encode(enc, src, FRAME, pkt + len + 2, (opus_int32)(sizeof pkt - len - 2));
        if (n < 0) n = 0;
        pkt[len] = (uint8_t)(n & 0xff); pkt[len + 1] = (uint8_t)(n >> 8);
        len += 2 + n; frames++;
      }
      if (onset) npre = 0;
    }
    me_speaking = frames > 0;
    if (!frames) continue;
    pthread_mutex_lock(&mx); snprintf(ptoken, sizeof ptoken, "%s", tok);
    snprintf(url, sizeof url, "%s/api/v1/party/voice?codec=opus&seq=%u", url_base, seq++); pthread_mutex_unlock(&mx);
    int st = keep_req(&k, 1, url, ptoken, pkt, (size_t)len, "application/octet-stream", NULL, NULL, 0);
    if (st == 204) fails = 0;
    else {
      if (fails++ < 3 || fails % 50 == 0) lg("voce: invio -> %d", st);
      if (st == 403) usleep(500 * 1000);                  // fuori dal party o muto: lo dirà il gestore
      else if (st < 0) usleep(200 * 1000);
    }
  }
  if (enc) opus_encoder_destroy(enc);
  keep_close(&k);
  me_speaking = 0;
  __sync_fetch_and_sub(&live, 1);
  return NULL;
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
  snprintf(s->oid, sizeof s->oid, "%s", oid);
  s->head = s->fill = s->playing = 0; s->pos = 0; s->prebuf = PREBUF_MIN * 3 / 2; s->calm_since = time(NULL);
  if (s->dec) opus_decoder_ctl(s->dec, OPUS_RESET_STATE);
  return s;
}

static void feed(Speaker *s, const int16_t *p, int n) {
  for (int i = 0; i < n; i++) {
    if (s->fill >= SPK_RING) { s->head = (s->head + 1) % SPK_RING; s->fill--; }
    s->ring[(s->head + s->fill) % SPK_RING] = p[i]; s->fill++;
  }
  // troppo in ritardo (rete che ha consegnato tutto insieme): si salta avanti
  if (s->fill > MAXQ + s->prebuf) { int drop = s->fill - CATCHUP - s->prebuf; s->head = (s->head + drop) % SPK_RING; s->fill -= drop; }
  s->last = time(NULL);
}

static void play_chunk(const char *oid, int codec, const uint8_t *d, int len) {
  static int16_t pcm[FRAME * MAX_BATCH * 2];
  if (codec != CODEC_OPUS) return;        // l'ADPCM lo manda solo la build desktop
  pthread_mutex_lock(&mx);
  Speaker *s = speaker(oid);
  if (!s->dec) { int err; s->dec = opus_decoder_create(VSR, 1, &err); }
  int n = 0;
  for (int off = 0; s->dec && off + 2 <= len && n + FRAME <= (int)(sizeof pcm / sizeof *pcm);) {
    int fl = d[off] | d[off + 1] << 8; off += 2;
    if (off + fl > len) break;
    int k = opus_decode(s->dec, fl ? d + off : NULL, fl, pcm + n, FRAME, 0);
    if (k > 0) n += k;
    off += fl;
  }
  if (n > 0) feed(s, pcm, n);
  pthread_mutex_unlock(&mx);
}

static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
static uint32_t rd32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }

static void *rx_thread(void *arg) {
  int g = (int)(intptr_t)arg;
  Keep k = { -1, "" };
  uint32_t cursor = 0; char url[300], ptoken[700];
  while (gen == g) {
    pthread_mutex_lock(&mx); snprintf(ptoken, sizeof ptoken, "%s", tok);
    snprintf(url, sizeof url, "%s/api/v1/party/voice?after=%u&wait=1", url_base, cursor); pthread_mutex_unlock(&mx);
    unsigned char *b = NULL; size_t len = 0;
    time_t t0 = time(NULL);
    int st = keep_req(&k, 0, url, ptoken, NULL, 0, NULL, &b, &len, 256 * 1024);
    if (st != 200 || len < 10 || memcmp(b, "OVC1", 4)) {
      free(b);
      usleep(st == 403 ? 2000 * 1000 : time(NULL) == t0 ? 400 * 1000 : 100 * 1000);
      continue;
    }
    cursor = rd32(b + 4);
    int count = rd16(b + 8); size_t o = 10;
    for (int i = 0; i < count && o < len && gen == g; i++) {
      int ol = b[o++]; if (o + (size_t)ol + 7 > len) break;
      char oid[40]; snprintf(oid, sizeof oid, "%.*s", ol > 39 ? 39 : ol, (const char *)b + o); o += (size_t)ol;
      int codec = b[o++]; o += 4;
      int dl = rd16(b + o); o += 2;
      if (o + (size_t)dl > len) break;
      play_chunk(oid, codec, b + o, dl);
      o += (size_t)dl;
    }
    free(b);
  }
  keep_close(&k);
  __sync_fetch_and_sub(&live, 1);
  return NULL;
}

// ----------------------------------------------------------------- uscita --
int sceAudioOutInit(void);
int sceAudioOutOpen(int userId, int type, int index, unsigned len, unsigned freq, unsigned param);
int sceAudioOutOutput(int handle, const void *ptr);
int sceAudioOutClose(int handle);

static void *out_thread(void *arg) {
  int g = (int)(intptr_t)arg;
  sceAudioOutInit();
  // porta MAIN come la musica (sappiamo che suona anche dentro i giochi), mono S16
  int h = sceAudioOutOpen(255, 0, 0, OGRAIN, OSR, 0);
  if (h < 0) { lg("voce: uscita audio non aperta 0x%x", h); __sync_fetch_and_sub(&live, 1); return NULL; }
  static int16_t out[OGRAIN]; float mixf[OGRAIN];
  while (gen == g) {
    int any = 0;
    for (int i = 0; i < OGRAIN; i++) mixf[i] = 0;
    pthread_mutex_lock(&mx);
    for (int k = 0; k < MAXSPK; k++) {
      Speaker *s = &spk[k];
      if (!s->oid[0]) continue;
      if (!s->playing) {
        if (s->fill < s->prebuf) continue;
        s->playing = 1;
      }
      for (int i = 0; i < OGRAIN; i++) {
        if (s->fill < 2) {
          s->playing = 0;
          // vuoto a metà frase (pacchetto appena arrivato): la rete singhiozza, più margine
          if (time(NULL) - s->last <= 1) {
            s->underruns++; s->calm_since = time(NULL);
            if (s->prebuf < PREBUF_MAX) s->prebuf += VSR / 25;   // +40 ms
            if (s->underruns <= 20) lg("voce: vuoto nell'audio di %s, margine %d ms", s->oid, s->prebuf * 1000 / VSR);
          }
          break;
        }
        float a = s->ring[s->head] / 32768.0f, b = s->ring[(s->head + 1) % SPK_RING] / 32768.0f;
        mixf[i] += a + (b - a) * (float)s->pos;
        s->pos += (double)VSR / OSR;
        if (s->pos >= 1) { s->pos -= 1; s->head = (s->head + 1) % SPK_RING; s->fill--; }
      }
      // 20 s senza vuoti: si torna a stringere di 20 ms
      if (s->prebuf > PREBUF_MIN && time(NULL) - s->calm_since >= 20) { s->prebuf -= VSR / 50; s->calm_since = time(NULL); }
      any = 1;
    }
    pthread_mutex_unlock(&mx);
    for (int i = 0; i < OGRAIN; i++) {
      float x = any ? mixf[i] / (1 + fabsf(mixf[i])) * 1.6f : 0;   // limitatore morbido
      if (x > 1) x = 1; else if (x < -1) x = -1;
      out[i] = (int16_t)(x * 30000);
    }
    sceAudioOutOutput(h, out);   // bloccante: dà il ritmo (5,3 ms)
  }
  sceAudioOutOutput(h, NULL); sceAudioOutClose(h);
  __sync_fetch_and_sub(&live, 1);
  return NULL;
}

// --------------------------------------------------------------- controllo --
static void session_stop(const char *why) {
  if (!running) return;
  __sync_fetch_and_add(&gen, 1);
  running = 0;
  for (int i = 0; i < 300 && live > 0; i++) usleep(10 * 1000);   // il microfono si chiude nel suo thread
  pthread_mutex_lock(&mx);
  for (int k = 0; k < MAXSPK; k++) { spk[k].oid[0] = 0; spk[k].fill = spk[k].playing = 0; }
  pthread_mutex_unlock(&mx);
  me_speaking = 0;
  guard_phase = 0;
  lg("voce: spenta (%s)%s", why, live > 0 ? ", thread ancora attivi" : "");
}

static int spawn_n(void *(*fn)(void *), int g) {
  __sync_fetch_and_add(&live, 1);
  if (omega_thread(fn, (void *)(intptr_t)g) != 0) { __sync_fetch_and_sub(&live, 1); return 0; }
  return 1;
}

static void session_start(void) {
  if (running || live > 0) return;
  int g = __sync_add_and_fetch(&gen, 1);
  pthread_mutex_lock(&mx); mic_w = mic_r = 0; pthread_mutex_unlock(&mx);
  mic_silent_s = 0;
  if (!guard_no_mic) { guard_phase = 1; mic_ok = mic_open(); }
  else mic_ok = 0;
  guard_phase = 2;
  if (mic_ok && !spawn_n(mic_thread, g)) { in_close(mic_handle); mic_handle = -1; mic_ok = 0; }
  if (mic_ok) spawn_n(tx_thread, g);
  spawn_n(rx_thread, g);
  spawn_n(out_thread, g);
  running = 1;
  lg("voce: attiva nel servizio (%s)", mic_ok ? "microfono acceso" : "solo ascolto");
  // niente notifica "voce attiva": in background la PS5 di solito non fa sentire
  // l'audio del servizio, e prometterlo sarebbe falso. Il microfono, se si apre,
  // fa sentire te agli altri.
  if (mic_ok) { char msg[300]; snprintf(msg, sizeof msg, _("Omega \xC2\xB7 party %s: il tuo microfono resta collegato"), party_name); notify(msg); }
}

// GET /party: in un party? come si chiama, quanti siamo, sono muto?
static int party_poll(const char *token) {
  Keep k = { -1, "" };
  char url[300]; snprintf(url, sizeof url, "%s/api/v1/party", url_base);
  unsigned char *b = NULL; size_t len = 0;
  int st = keep_req(&k, 0, url, token, NULL, 0, NULL, &b, &len, 128 * 1024);
  keep_close(&k);
  if (st != 200 || !b) { free(b); return st == 401 ? 0 : -1; }   // -1 = non si sa (rete)
  JVal *j = json_parse((const char *)b); free(b);
  if (!j) return -1;
  JVal *p = jget(j, "party");
  int in = p && p->t == J_OBJ;
  if (in) {
    pthread_mutex_lock(&mx);
    jcpy(party_name, sizeof party_name, p, "name");
    nmembers = jlen(jget(p, "members"));
    JFOR(m, jget(p, "members")) if (!strcasecmp(jstr(m, "online_id", ""), me)) muted = jbool(m, "muted");
    pthread_mutex_unlock(&mx);
  }
  json_free(j);
  return in;
}

static void *manager(void *arg) {
  (void)arg;
  time_t last_poll = 0; int in_party = 0;
  for (;;) {
    usleep(500 * 1000);
    int age = ui_active_age();
    int ui_has_it = age >= 0 && age < UI_VOICE_S;
    char sess[2048] = "", token[700] = "", server[200] = "";
    int has_session = read_session(sess, sizeof sess) > 0;
    if (has_session) {
      JVal *j = json_parse(sess);
      if (j) { jcpy(token, sizeof token, j, "token"); jcpy(server, sizeof server, j, "server"); pthread_mutex_lock(&mx); jcpy(me, sizeof me, j, "online_id"); pthread_mutex_unlock(&mx); json_free(j); }
      has_session = token[0] != 0;
    }
    if (!has_session || ui_has_it) {
      if (running) session_stop(ui_has_it ? "la UI è in primo piano" : "nessuna sessione");
      in_party = 0; last_poll = 0;
      continue;
    }
    pthread_mutex_lock(&mx);
    snprintf(tok, sizeof tok, "%s", token);
    // il server della sessione ("omega" = quello predefinito)
    snprintf(url_base, sizeof url_base, "%s", (!server[0] || !strcmp(server, "omega")) ? base_url : server);
    pthread_mutex_unlock(&mx);
    time_t now = time(NULL);
    if (left) { left = 0; in_party = 0; last_poll = now; }
    if (now - last_poll >= PARTY_EVERY_S) {
      last_poll = now;
      int r = party_poll(token);
      if (r >= 0) { in_party = r; last_party_ok = now; }
      else if (now - last_party_ok > 60) in_party = 0;   // rete giù da un minuto
    }
    if (in_party && !running && !guard_off) session_start();
    else if (!in_party && running) session_stop("non sei più in un party");
  }
  return NULL;
}

void voice_start(const char *base, int (*tmpl_fn)(void), int (*session_fn)(char *, size_t),
                 int (*ui_age_fn)(void), void (*notify_fn)(const char *), const char *(*lang_fn)(void)) {
  base_url = base; get_tmpl = tmpl_fn; read_session = session_fn; ui_active_age = ui_age_fn; notify = notify_fn; lang_code = lang_fn;
  guard_load();
  if (guard_off) guard_write("off");
  else if (guard_no_mic) guard_write("nomic");
  else if (guard_crashes) guard_write("ok");
  spk = calloc(MAXSPK, sizeof *spk);
  if (!spk) { lg("voce: memoria insufficiente"); return; }
  if (omega_thread(manager, NULL) != 0) lg("voce: gestore non avviato");
}

// ------------------------------------------------- comandi e stato (ctl.c) --
static void party_mute_remote(int on) {
  char token[700], url[300], body[32];
  pthread_mutex_lock(&mx); snprintf(token, sizeof token, "%s", tok); snprintf(url, sizeof url, "%s/api/v1/party/mute", url_base); pthread_mutex_unlock(&mx);
  if (!token[0] || !url_base[0]) return;
  snprintf(body, sizeof body, "{\"muted\":%s}", on ? "true" : "false");
  Keep k = { -1, "" };
  int st = keep_req(&k, 1, url, token, body, strlen(body), "application/json", NULL, NULL, 0);
  keep_close(&k);
  lg("voce: muto %s -> %d", on ? "acceso" : "spento", st);
}

int voice_command(const char *cmd) {
  if (!strcmp(cmd, "mute") || !strcmp(cmd, "unmute") || !strcmp(cmd, "toggle")) {
    int on = !strcmp(cmd, "toggle") ? !muted : !strcmp(cmd, "mute");
    muted = on;
    party_mute_remote(on);
    notify(on ? _("Omega \xC2\xB7 microfono spento") : _("Omega \xC2\xB7 microfono acceso"));
    return 1;
  }
  if (!strcmp(cmd, "leave")) {
    char token[700], url[300];
    pthread_mutex_lock(&mx); snprintf(token, sizeof token, "%s", tok); snprintf(url, sizeof url, "%s/api/v1/party/leave", url_base); pthread_mutex_unlock(&mx);
    if (!token[0]) return 0;
    Keep k = { -1, "" };
    int st = keep_req(&k, 1, url, token, "{}", 2, "application/json", NULL, NULL, 0);
    keep_close(&k);
    lg("voce: uscita dal party -> %d", st);
    if (st == 200) { left = 1; session_stop("uscito dal party"); notify(_("Omega \xC2\xB7 sei uscito dal party")); }
    return st == 200;
  }
  return 0;
}

void voice_state_json(char *out, size_t n) {
  char esc[200], talk[400] = "";
  pthread_mutex_lock(&mx);
  json_escape(esc, sizeof esc, party_name);
  size_t tl = 0; time_t now = time(NULL);
  for (int k = 0; k < MAXSPK && spk; k++) if (spk[k].oid[0] && (spk[k].playing || now - spk[k].last <= 1)) {
    char e[90]; json_escape(e, sizeof e, spk[k].oid);
    tl += (size_t)snprintf(talk + tl, sizeof talk - tl, "%s\"%s\"", tl ? "," : "", e);
    if (tl >= sizeof talk) { tl = sizeof talk - 1; break; }
  }
  const char *mic_s = !running ? "off" : !mic_ok ? "none" : muted ? "muted" : mic_silent_s >= 3 ? "silent" : "ok";
  snprintf(out, n, "{\"active\":%s,\"in_party\":%s,\"party\":\"%s\",\"members\":%d,\"mic\":\"%s\",\"muted\":%s,\"speaking\":%s,\"talking\":[%s]}",
           running ? "true" : "false", running ? "true" : "false", running ? esc : "", running ? nmembers : 0, mic_s,
           muted ? "true" : "false", me_speaking ? "true" : "false", talk);
  pthread_mutex_unlock(&mx);
}
