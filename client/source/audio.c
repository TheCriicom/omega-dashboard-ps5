// Omega UI — audio tutto sintetizzato, senza file: musica generativa (accordi
// lenti e campanelle rade), effetti sonori (campane FM, aria filtrata e un
// piccolo riverbero) e voce del party. Un solo
// dispositivo SDL a 48 kHz stereo; la callback mescola tutto.
#include "app.h"
#include <math.h>
#include <stdlib.h>

#define SR 48000
#define MAXV 40
#define TAU 6.28318530718f
#define AUDIO_FILE OMEGA_DIR "/audio.txt"

// wave: 0 sinusoide, 1 triangolo, 2 quadra (retrò), 3 campana FM, 4 aria (rumore filtrato)
enum { W_SINE, W_TRI, W_SQUARE, W_BELL, W_AIR };
typedef struct {
  int on, wave;
  float freq, freq_end, glide, phase, amp, decay, t, dur, pan;
  float attack, send;                    // attacco (s); quota mandata al riverbero degli effetti
  float mratio, mindex, mdecay, mphase;  // campana FM: rapporto, indice e sua caduta (brillantezza dell'attacco)
  float q, bp_lo, bp_band;               // aria: filtro passa-banda a variabile di stato
} Voice;
static Voice sfx[MAXV];
static SDL_AudioDeviceID dev;
static int music_on = 1, sfx_on = 1; static float music_vol = 0.55f, sfx_vol = 0.8f;
static int paused;
static volatile int ext_music;            // il lettore musicale del demone sta suonando

// -------------------------------------------------------- musica generativa --
// Personalizza › Musica di sottofondo: quattro atmosfere (accordi in Hz) e "spenta"
static const float MOODS[4][4][4] = {
  { { 130.81f, 196.00f, 246.94f, 293.66f }, { 110.00f, 164.81f, 196.00f, 246.94f },     // serena: Cmaj9, Am9,
    { 87.31f, 130.81f, 164.81f, 246.94f }, { 98.00f, 146.83f, 164.81f, 220.00f } },     // Fmaj7#11, G6/9
  { { 110.00f, 130.81f, 164.81f, 196.00f }, { 87.31f, 110.00f, 130.81f, 164.81f },      // notturna: Am7, Fmaj7,
    { 98.00f, 116.54f, 146.83f, 174.61f }, { 82.41f, 123.47f, 146.83f, 196.00f } },     // Gm7, Em7
  { { 65.41f, 98.00f, 146.83f, 196.00f }, { 73.42f, 110.00f, 164.81f, 220.00f },       // spaziale: quinte vuote,
    { 58.27f, 87.31f, 130.81f, 174.61f }, { 61.74f, 92.50f, 138.59f, 185.00f } },       // basse e larghe
  { { 146.83f, 185.00f, 220.00f, 277.18f }, { 123.47f, 155.56f, 185.00f, 246.94f },     // calda: Dmaj7, Bm7,
    { 110.00f, 138.59f, 164.81f, 207.65f }, { 130.81f, 164.81f, 196.00f, 246.94f } },   // Amaj7, Cmaj7
};
#define CHORDS MOODS[g_prefs.music_mood % 4]
static double mtime;                     // secondi di musica
static float pad_phase[4][2];
static float lp_l, lp_r;
static Voice bells[8];
static double next_bell;
static unsigned rng = 12345;
static float frand(void) { rng = rng * 1664525u + 1013904223u; return (rng >> 8) / 16777216.0f; }

static float osc(int wave, float ph) {
  if (wave == 1) { float x = ph / TAU; return 4.0f * fabsf(x - floorf(x + 0.5f)) - 1.0f; }
  if (wave == 2) return ph < TAU / 2 ? 0.8f : -0.8f;   // quadra (suoni retrò)
  return sinf(ph);
}

static void start_voice(Voice *v, float f, float f_end, float amp, float dur, float decay, float pan, int wave) {
  memset(v, 0, sizeof *v);
  v->on = 1; v->freq = f; v->freq_end = f_end; v->amp = amp; v->dur = dur; v->decay = decay; v->pan = pan; v->wave = wave;
  v->attack = 0.004f;                                        // attacco breve anti-click
  v->glide = (f > 0 && f_end > 0) ? logf(f_end / f) : 0;     // glissando esponenziale: si sente "naturale"
}

// send: dove va la quota per il riverbero (NULL = niente riverbero, es. la musica)
static void voice_sample(Voice *v, float *l, float *r, float *send) {
  if (!v->on) return;
  if (v->t < 0) { v->t += 1.0f / SR; return; }   // partenza ritardata (arpeggi)
  float k = v->t / v->dur;
  if (k >= 1) { v->on = 0; return; }
  float f = v->freq * expf(v->glide * k);
  float env = v->t < v->attack ? sinf(1.5707963f * v->t / v->attack) : expf(-(v->t - v->attack) * v->decay);
  if (k > 0.9f) env *= (1 - k) * 10;             // coda sfumata: niente click alla fine
  float s;
  if (v->wave == W_BELL) {
    float m = sinf(v->mphase) * v->mindex * expf(-v->t * v->mdecay);
    s = sinf(v->phase + m);
    v->mphase += TAU * f * v->mratio / SR; if (v->mphase > TAU) v->mphase -= TAU;
  } else if (v->wave == W_AIR) {
    float g = 2 * sinf(3.14159265f * fminf(f, SR / 6.0f) / SR);   // filtro SVF (Chamberlin)
    float n = frand() * 2 - 1;
    v->bp_lo += g * v->bp_band;
    float hi = n - v->bp_lo - v->bp_band / v->q;
    v->bp_band += g * hi;
    s = v->bp_band;
  } else s = osc(v->wave, v->phase);
  s *= v->amp * env;
  v->phase += TAU * f / SR; if (v->phase > TAU) v->phase -= TAU;
  v->t += 1.0f / SR;
  float sl = s * (1 - v->pan), sr = s * (1 + v->pan);
  *l += sl; *r += sr;
  if (send) { send[0] += sl * v->send; send[1] += sr * v->send; }
}

// --------------------------------------------- riverbero degli effetti --
// Piccola stanza (Freeverb ridotto: 4 comb + 2 all-pass per canale): dà agli
// effetti la coda ariosa delle interfacce delle console, senza impastarli.
#define NCOMB 4
#define NAP 2
static const int COMB_LEN[NCOMB] = { 1213, 1361, 1493, 1621 }, AP_LEN[NAP] = { 241, 607 };
#define SPREAD 23
static float comb_buf[2][NCOMB][1621 + SPREAD], ap_buf[2][NAP][607 + SPREAD], comb_lp[2][NCOMB];
static int comb_i[2][NCOMB], ap_i[2][NAP];
static float reverb_ch(int ch, float in) {
  float out = 0;
  for (int c = 0; c < NCOMB; c++) {
    int len = COMB_LEN[c] + ch * SPREAD;
    float y = comb_buf[ch][c][comb_i[ch][c]];
    comb_lp[ch][c] = y * 0.62f + comb_lp[ch][c] * 0.38f + 1e-18f;      // smorzamento degli acuti
    comb_buf[ch][c][comb_i[ch][c]] = in + comb_lp[ch][c] * 0.80f;
    if (++comb_i[ch][c] >= len) comb_i[ch][c] = 0;
    out += y;
  }
  for (int a = 0; a < NAP; a++) {
    int len = AP_LEN[a] + ch * SPREAD;
    float b = ap_buf[ch][a][ap_i[ch][a]];
    ap_buf[ch][a][ap_i[ch][a]] = out + b * 0.5f;
    out = b - out;
    if (++ap_i[ch][a] >= len) ap_i[ch][a] = 0;
  }
  return out * 0.25f;
}

static void callback(void *ud, Uint8 *stream, int len) {
  (void)ud;
  Sint16 *out = (Sint16 *)stream;
  int frames = len / 4;
  // voce del party (48 kHz mono, già mescolata): quando qualcuno parla la musica si abbassa
  static float vbuf[4096];
  int talking = frames <= 4096 && voice_fill(vbuf, frames);
  // musica del servizio (pcmlink.c): già col suo volume
  static float ml[4096], mr[4096];
  int ext = 0;
  if (frames <= 4096) { memset(ml, 0, sizeof(float) * (size_t)frames); memset(mr, 0, sizeof(float) * (size_t)frames); ext = pcmlink_mix(ml, mr, frames); }
  static float duck = 1;
  for (int i = 0; i < frames; i++) {
    float l = 0, r = 0;
    duck += ((talking ? 0.25f : 1.0f) - duck) * 0.0005f;
    if (music_on && !paused && !ext_music && g_prefs.music_mood != 4) {
      // accordo corrente con dissolvenza incrociata di 2 s ogni 9 s
      double cyc = g_prefs.music_mood == 2 ? 14.0 : g_prefs.music_mood == 1 ? 11.0 : 9.0;   // la spaziale va più piano
      int ci = (int)(mtime / cyc) % 4, cn = (ci + 1) % 4;
      float pos = (float)fmod(mtime, cyc);
      float x = pos > cyc - 2.0f ? (pos - (float)(cyc - 2.0)) / 2.0f : 0.0f;
      float pl = 0, pr = 0;
      for (int n = 0; n < 4; n++) {
        float f = CHORDS[ci][n] * (1 - x) + CHORDS[cn][n] * x;
        for (int d = 0; d < 2; d++) {       // due voci leggermente stonate = chorus
          float det = d ? 1.003f : 0.997f;
          pad_phase[n][d] += TAU * f * det / SR; if (pad_phase[n][d] > TAU) pad_phase[n][d] -= TAU;
          float s = sinf(pad_phase[n][d]) * 0.7f + sinf(pad_phase[n][d] * 2.0f) * 0.12f;
          if (d) pr += s; else pl += s;
        }
      }
      // respiro lento del volume
      float breath = 0.75f + 0.25f * sinf((float)mtime * 0.21f);
      pl *= 0.045f * breath; pr *= 0.045f * breath;
      // campanelle rade dall'accordo (due ottave sopra)
      if (mtime >= next_bell) {
        for (int b = 0; b < 8; b++) if (!bells[b].on) {
          float f = CHORDS[ci][(int)(frand() * 4) % 4] * (frand() < 0.5f ? 4.0f : 8.0f);
          start_voice(&bells[b], f, f, 0.05f, 3.5f, 1.4f, frand() * 1.2f - 0.6f, 0);
          break;
        }
        next_bell = mtime + 1.2 + frand() * 2.6;
      }
      for (int b = 0; b < 8; b++) voice_sample(&bells[b], &pl, &pr, NULL);
      // passa-basso morbido
      lp_l += (pl - lp_l) * 0.12f; lp_r += (pr - lp_r) * 0.12f;
      l += lp_l * music_vol * duck; r += lp_r * music_vol * duck;
      mtime += 1.0 / SR;
    }
    if (sfx_on) {
      float sl = 0, sr = 0, send[2] = { 0, 0 };
      for (int v = 0; v < MAXV; v++) voice_sample(&sfx[v], &sl, &sr, send);
      sl += reverb_ch(0, send[0]); sr += reverb_ch(1, send[1]);
      l += sl * sfx_vol; r += sr * sfx_vol;
    }
    if (ext) { l += ml[i] * duck; r += mr[i] * duck; }
    if (talking) { l += vbuf[i]; r += vbuf[i]; }
    if (l > 1) l = 1; if (l < -1) l = -1; if (r > 1) r = 1; if (r < -1) r = -1;
    out[i * 2] = (Sint16)(l * 30000); out[i * 2 + 1] = (Sint16)(r * 30000);
  }
}

// Voce libera; se sono tutte occupate si ruba quella più vicina alla fine.
static Voice *free_voice(void) {
  int best = 0; float bk = -1;
  for (int v = 0; v < MAXV; v++) {
    if (!sfx[v].on) return &sfx[v];
    float k = sfx[v].t / sfx[v].dur; if (k > bk) { bk = k; best = v; }
  }
  return &sfx[best];
}

// Mattoncini dei suoni. delay in secondi; send = quanto riverbero.
static Voice *bell(float f, float amp, float dur, float decay, float pan, float delay, float ratio, float index, float send) {
  Voice *v = free_voice();
  start_voice(v, f, f, amp, dur, decay, pan, W_BELL);
  v->mratio = ratio; v->mindex = index; v->mdecay = decay * 3; v->send = send; v->t = -delay;
  return v;
}
static Voice *tone(float f, float f_end, float amp, float dur, float decay, float pan, float delay, float send) {
  Voice *v = free_voice();
  start_voice(v, f, f_end, amp, dur, decay, pan, W_SINE);
  v->send = send; v->t = -delay;
  return v;
}
static Voice *air(float f, float f_end, float q, float amp, float dur, float attack, float decay, float send) {
  Voice *v = free_voice();
  start_voice(v, f, f_end, amp, dur, decay, 0, W_AIR);
  v->q = q; v->attack = attack; v->send = send;
  return v;
}

// Personalizza › Suoni dell'interfaccia: classici, morbidi, retrò 8 bit, cristallo, nessuno.
// Lo stesso suono cambia altezza, brillantezza, riverbero e forma d'onda.
static void sfx_pack_tweak(const Voice *before) {
  for (int i = 0; i < MAXV; i++) {
    Voice *x = &sfx[i];
    // solo le voci appena avviate da questa chiamata
    if (!x->on || !memcmp(x, &before[i], sizeof *x)) continue;
    switch (g_prefs.sfx_pack) {
      case 1:   // morbidi: più bassi, rotondi, lenti
        x->freq *= 0.7f; x->freq_end *= 0.7f; x->mindex *= 0.35f; x->decay *= 0.7f; x->dur *= 1.3f; x->amp *= 0.85f;
        if (x->wave == W_AIR) x->amp *= 0.6f;
        break;
      case 2:   // retrò: onda quadra, secchi, niente aria né riverbero
        if (x->wave == W_AIR) { x->on = 0; break; }
        x->wave = W_SQUARE; x->amp *= 0.65f; x->dur *= 0.8f; x->decay *= 1.3f; x->send = 0;
        break;
      case 3:   // cristallo: un'ottava abbondante sopra, code lunghe
        x->freq *= 1.5f; x->freq_end *= 1.5f; x->decay *= 0.55f; x->dur *= 1.7f; x->amp *= 0.7f; x->send *= 1.6f;
        if (x->wave == W_BELL) x->mratio = 3.5f;
        break;
    }
  }
}

void sfx_play(int kind) {
  if (!dev || !sfx_on || g_prefs.sfx_pack == 4) return;
  SDL_LockAudioDevice(dev);
  static Voice before[MAXV]; memcpy(before, sfx, sizeof sfx);
  float hum = 1 + (frand() - 0.5f) * 0.03f;   // ±1,5% a ogni colpo: gli spostamenti ripetuti non suonano a mitraglia
  switch (kind) {
    case SFX_MOVE:     // tic morbido e corto, con un filo d'aria
      tone(2350 * hum, 2050 * hum, 0.055f, 0.07f, 60, 0, 0, 0.18f);
      tone(1175 * hum, 1100 * hum, 0.030f, 0.05f, 75, 0, 0, 0.10f);
      air(6500, 5200, 2.5f, 0.06f, 0.03f, 0.001f, 160, 0.10f);
      break;
    case SFX_SELECT:   // conferma: due note di campana in quinta, brillanti all'attacco
      bell(1318.5f, 0.11f, 0.75f, 7.0f, -0.08f, 0, 2.0f, 1.3f, 0.32f);
      bell(1975.5f, 0.07f, 0.70f, 8.0f, 0.08f, 0.045f, 2.0f, 0.9f, 0.36f);
      tone(659.3f, 659.3f, 0.045f, 0.25f, 16, 0, 0, 0.10f);
      air(4200, 7000, 1.6f, 0.035f, 0.08f, 0.004f, 45, 0.15f);
      break;
    case SFX_BACK:     // indietro: due note che scendono, più scure e corte
      bell(1174.7f, 0.085f, 0.40f, 11.0f, 0.06f, 0, 2.0f, 0.8f, 0.25f);
      bell(880.0f, 0.075f, 0.45f, 10.0f, -0.06f, 0.05f, 2.0f, 0.6f, 0.28f);
      air(3800, 1800, 1.4f, 0.03f, 0.09f, 0.004f, 40, 0.10f);
      break;
    case SFX_OPEN:     // apertura: un piccolo soffio che sale e un tono che sboccia
      air(1600, 5200, 1.3f, 0.05f, 0.22f, 0.035f, 14, 0.30f);
      tone(740, 1108.7f, 0.055f, 0.16f, 17, 0, 0.01f, 0.30f);
      bell(1480, 0.035f, 0.30f, 12.0f, 0, 0.06f, 2.0f, 0.5f, 0.35f);
      break;
    case SFX_NOTIFY: { // notifica: arpeggio di campane (Emaj9) con un fondo caldo
      static const float n[4] = { 1318.5f, 1661.2f, 1975.5f, 2489.0f };
      for (int i = 0; i < 4; i++) bell(n[i], 0.085f - 0.012f * i, 1.7f, 3.4f, -0.3f + 0.2f * i, 0.075f * i, 2.0f, 1.1f - 0.15f * i, 0.5f);
      Voice *p = tone(659.3f, 659.3f, 0.05f, 1.2f, 3.0f, 0, 0, 0.3f); p->attack = 0.03f;
      break; }
    case SFX_LAUNCH: { // avvio: soffio che cresce e accordo che si apre, poi un rintocco
      air(350, 6500, 1.1f, 0.07f, 1.0f, 0.45f, 5.0f, 0.45f);
      static const float c[4] = { 440.0f, 659.3f, 880.0f, 1108.7f };
      for (int i = 0; i < 4; i++) { Voice *v = tone(c[i] * 0.995f, c[i], 0.045f, 1.7f, 2.4f, -0.24f + 0.16f * i, 0, 0.55f); v->attack = 0.35f; }
      bell(1760, 0.08f, 1.4f, 3.8f, 0, 0.38f, 2.0f, 1.4f, 0.6f);
      break; }
  }
  if (g_prefs.sfx_pack) sfx_pack_tweak(before);
  SDL_UnlockAudioDevice(dev);
}

static void audio_save(void) {
  FILE *f = fopen(AUDIO_FILE, "w");
  if (f) { fprintf(f, "%d %d %.2f %.2f\n", music_on, sfx_on, music_vol, sfx_vol); fclose(f); }
}

void audio_init(void) {
  FILE *f = fopen(AUDIO_FILE, "r");
  if (f) { if (fscanf(f, "%d %d %f %f", &music_on, &sfx_on, &music_vol, &sfx_vol) != 4) { music_on = sfx_on = 1; music_vol = 0.55f; sfx_vol = 0.8f; } fclose(f); }
  if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) { omega_log("audio: %s", SDL_GetError()); return; }
  SDL_AudioSpec want, have; SDL_zero(want);
  want.freq = SR; want.format = AUDIO_S16SYS; want.channels = 2; want.samples = 1024; want.callback = callback;
  dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
  if (!dev) { omega_log("audio: apertura fallita: %s", SDL_GetError()); return; }
  if (have.freq != SR || have.channels != 2 || have.format != AUDIO_S16SYS) omega_log("audio: formato %d Hz %d ch fmt 0x%x", have.freq, have.channels, have.format);
  next_bell = 2.0;
  SDL_PauseAudioDevice(dev, 0);
  pcmlink_init();
  omega_log("audio pronto");
}

void audio_pause(int p) { paused = p; }
void audio_external_music(int on) { ext_music = on; }
void audio_lock(int on) { if (dev) { if (on) SDL_LockAudioDevice(dev); else SDL_UnlockAudioDevice(dev); } }
int  audio_music_on(void) { return music_on; }
int  audio_sfx_on(void) { return sfx_on; }
int  audio_music_level(void) { return (int)(music_vol * 100 + 0.5f); }
void audio_set(int music, int effects, int level) {
  if (dev) SDL_LockAudioDevice(dev);
  if (music >= 0) music_on = music;
  if (effects >= 0) sfx_on = effects;
  if (level >= 0) music_vol = level / 100.0f;
  if (dev) SDL_UnlockAudioDevice(dev);
  audio_save();
}
