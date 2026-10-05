// Omega UI — audio tutto sintetizzato, senza file: musica generativa (accordi
// lenti e campanelle rade), effetti sonori e voce del party. Un solo
// dispositivo SDL a 48 kHz stereo; la callback mescola tutto.
#include "app.h"
#include <math.h>
#include <stdlib.h>

#define SR 48000
#define MAXV 24
#define TAU 6.28318530718f
#define AUDIO_FILE OMEGA_DIR "/audio.txt"

typedef struct { int on; float freq, freq_end, phase, amp, decay, t, dur, pan; int wave; } Voice;   // wave 0 sine, 1 triangolo
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
  v->on = 1; v->freq = f; v->freq_end = f_end; v->phase = 0; v->amp = amp; v->dur = dur; v->decay = decay; v->t = 0; v->pan = pan; v->wave = wave;
}

static void voice_sample(Voice *v, float *l, float *r) {
  if (!v->on) return;
  if (v->t < 0) { v->t += 1.0f / SR; return; }   // partenza ritardata (arpeggi)
  float k = v->t / v->dur;
  if (k >= 1) { v->on = 0; return; }
  float f = v->freq + (v->freq_end - v->freq) * k;
  float env = expf(-v->t * v->decay) * (v->t < 0.004f ? v->t / 0.004f : 1.0f);   // attacco breve anti-click
  float s = osc(v->wave, v->phase) * v->amp * env;
  v->phase += TAU * f / SR; if (v->phase > TAU) v->phase -= TAU;
  v->t += 1.0f / SR;
  *l += s * (1 - v->pan); *r += s * (1 + v->pan);
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
      for (int b = 0; b < 8; b++) voice_sample(&bells[b], &pl, &pr);
      // passa-basso morbido
      lp_l += (pl - lp_l) * 0.12f; lp_r += (pr - lp_r) * 0.12f;
      l += lp_l * music_vol * duck; r += lp_r * music_vol * duck;
      mtime += 1.0 / SR;
    }
    if (sfx_on) {
      float sl = 0, sr = 0;
      for (int v = 0; v < MAXV; v++) voice_sample(&sfx[v], &sl, &sr);
      l += sl * sfx_vol; r += sr * sfx_vol;
    }
    if (ext) { l += ml[i] * duck; r += mr[i] * duck; }
    if (talking) { l += vbuf[i]; r += vbuf[i]; }
    if (l > 1) l = 1; if (l < -1) l = -1; if (r > 1) r = 1; if (r < -1) r = -1;
    out[i * 2] = (Sint16)(l * 30000); out[i * 2 + 1] = (Sint16)(r * 30000);
  }
}

static Voice *free_voice(void) {
  for (int v = 0; v < MAXV; v++) if (!sfx[v].on) return &sfx[v];
  return &sfx[0];
}

// Personalizza › Suoni dell'interfaccia: classici, morbidi, retrò 8 bit, cristallo, nessuno.
// Lo stesso suono cambia altezza, durata e forma d'onda (2 = quadra).
static void sfx_pack_tweak(const Voice *before) {
  for (int i = 0; i < MAXV; i++) {
    Voice *x = &sfx[i];
    // solo le voci appena avviate da questa chiamata
    if (!x->on || !memcmp(x, &before[i], sizeof *x)) continue;
    switch (g_prefs.sfx_pack) {
      case 1: x->freq *= 0.7f; x->freq_end *= 0.7f; x->amp *= 0.8f; x->decay *= 0.6f; x->dur *= 1.4f; x->wave = 0; break;   // morbidi
      case 2: x->wave = 2; x->amp *= 0.55f; x->dur *= 0.8f; x->decay *= 1.2f; break;                                        // retrò
      case 3: x->freq *= 1.5f; x->freq_end *= 1.5f; x->wave = 0; x->decay *= 0.5f; x->dur *= 1.8f; x->amp *= 0.7f; break;    // cristallo
    }
  }
}

void sfx_play(int kind) {
  if (!dev || !sfx_on || g_prefs.sfx_pack == 4) return;
  SDL_LockAudioDevice(dev);
  static Voice before[MAXV]; memcpy(before, sfx, sizeof sfx);
  switch (kind) {
    case SFX_MOVE:   start_voice(free_voice(), 1900, 1700, 0.10f, 0.06f, 70, 0, 0); break;
    case SFX_SELECT: start_voice(free_voice(), 880, 880, 0.16f, 0.35f, 9, -0.1f, 0);
                     { Voice *v = free_voice(); start_voice(v, 1318.5f, 1318.5f, 0.12f, 0.45f, 8, 0.1f, 0); v->t = -0.06f; } break;
    case SFX_BACK:   start_voice(free_voice(), 660, 440, 0.12f, 0.18f, 14, 0, 1); break;
    case SFX_NOTIFY: { float n[3] = { 1046.5f, 1318.5f, 1568.0f };
                       for (int i = 0; i < 3; i++) { Voice *v = free_voice(); start_voice(v, n[i], n[i], 0.12f, 0.9f, 4.5f, -0.3f + 0.3f * i, 0); v->t = -0.09f * i; } } break;
    case SFX_LAUNCH: start_voice(free_voice(), 220, 880, 0.16f, 0.9f, 2.5f, 0, 1);
                     { Voice *v = free_voice(); start_voice(v, 440, 1760, 0.08f, 0.9f, 2.5f, 0, 0); } break;
    case SFX_OPEN:   start_voice(free_voice(), 520, 780, 0.09f, 0.16f, 16, 0, 0); break;
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
