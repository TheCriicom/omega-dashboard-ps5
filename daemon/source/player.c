// Omega — lettore musicale del demone. Due thread:
//  · decode: FFmpeg apre il brano (file, http(s), HLS), decodifica e converte a
//    48 kHz s16 stereo dentro un anello di ~3 s;
//  · out: prende blocchi da 256 frame, applica il volume e li manda all'uscita
//    (sceAudioOutOutput è bloccante e dà il ritmo).
// Comandi e stato passano da un mutex; chi comanda non aspetta mai la rete.
#include "player.h"
#include "json.h"
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/opt.h>
#include <libavutil/channel_layout.h>
#include <libswresample/swresample.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define SR 48000
#define BLOCK 256
#define RING_FRAMES (SR * 3)

void player_log(const char *fmt, ...);   // definita dal demone

// ------------------------------------------------------------- uscita audio --
#ifdef PS5
int sceAudioOutInit(void);
int sceAudioOutOpen(int userId, int type, int index, unsigned len, unsigned freq, unsigned param);
int sceAudioOutOutput(int handle, const void *ptr);
int sceAudioOutClose(int handle);
static int aout = -1;
static int aout_open(void) {
  static int inited;
  if (!inited) { sceAudioOutInit(); inited = 1; }
  if (aout < 0) aout = sceAudioOutOpen(255, 0 /* MAIN */, 0, BLOCK, SR, 1 /* s16 stereo */);
  return aout >= 0;
}
static void aout_write(const int16_t *pcm) { if (aout >= 0) sceAudioOutOutput(aout, pcm); }
static void aout_close(void) { if (aout >= 0) { sceAudioOutOutput(aout, NULL); sceAudioOutClose(aout); aout = -1; } }
#else
#include <SDL2/SDL.h>
static SDL_AudioDeviceID aout;
static int aout_open(void) {
  if (aout) return 1;
  if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) return 0;
  SDL_AudioSpec w; SDL_zero(w); w.freq = SR; w.format = AUDIO_S16SYS; w.channels = 2; w.samples = BLOCK;
  aout = SDL_OpenAudioDevice(NULL, 0, &w, NULL, 0);
  if (aout) SDL_PauseAudioDevice(aout, 0);
  return aout != 0;
}
// come sceAudioOutOutput: si blocca finché in coda c'è più di qualche blocco
static void aout_write(const int16_t *pcm) {
  while (SDL_GetQueuedAudioSize(aout) > BLOCK * 4 * 4) SDL_Delay(2);
  SDL_QueueAudio(aout, pcm, BLOCK * 4);
}
static void aout_close(void) { if (aout) { SDL_CloseAudioDevice(aout); aout = 0; } }
#endif

// --------------------------------------------------------------------- stato --
static pthread_mutex_t mx = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t cv = PTHREAD_COND_INITIALIZER;

static PlayerItem *items;            // coda
static int count, cur = -1;
static int order[PLAYER_MAX_ITEMS];  // ordine di riproduzione (mescolato o no)
static int shuffle, repeat, volume = 70;
static int state = PL_STOPPED;
static char err[160];
static unsigned seq;                 // cresce a ogni cambiamento: la UI interroga a buon mercato

// richieste al thread di decodifica
static int want_load;                // apri il brano cur
static double want_seek = -1;
static int gen;                      // generazione del brano: invalida i dati vecchi nell'anello

// brano in corso (scritti dal decode thread)
static double dur, base_pos;         // base_pos = posizione del primo campione dopo apertura/seek
static int live;
static char stream_title[256], meta_title[256], meta_artist[256], meta_album[256];
static unsigned char *cover_buf; static size_t cover_len; static char cover_mime[24];

// anello pcm s16 stereo
static int16_t ring[RING_FRAMES * 2];
static size_t r_head, r_tail;        // in frame, crescono sempre
static int ring_gen;
static int eof_pending;              // decodifica finita: quando l'anello si svuota si passa al prossimo
static uint64_t played;              // frame usciti dal brano corrente

static const char *state_file;
static player_track_cb on_track;

static void bump(void) { seq++; pthread_cond_broadcast(&cv); }

// ------------------------------------------------------------ salvataggio --
static void jesc(FILE *f, const char *s) {
  fputc('"', f);
  for (; *s; s++) {
    unsigned char c = (unsigned char)*s;
    if (c == '"' || c == '\\') { fputc('\\', f); fputc(c, f); }
    else if (c < 0x20) fprintf(f, "\\u%04x", c);
    else fputc(c, f);
  }
  fputc('"', f);
}
static void item_json(FILE *f, const PlayerItem *it) {
  fputs("{\"url\":", f); jesc(f, it->url);
  fputs(",\"title\":", f); jesc(f, it->title);
  fputs(",\"artist\":", f); jesc(f, it->artist);
  fputs(",\"album\":", f); jesc(f, it->album);
  fputs(",\"cover\":", f); jesc(f, it->cover);
  fputs(",\"source\":", f); jesc(f, it->source);
  fputs(",\"id\":", f); jesc(f, it->id);
  fprintf(f, ",\"dur\":%.1f}", it->dur);
}

static void save(void) {
  if (!state_file) return;
  char tmp[300]; snprintf(tmp, sizeof tmp, "%s.tmp", state_file);
  FILE *f = fopen(tmp, "w"); if (!f) return;
  fprintf(f, "{\"volume\":%d,\"shuffle\":%d,\"repeat\":%d,\"index\":%d,\"items\":[", volume, shuffle, repeat, cur);
  for (int i = 0; i < count; i++) { if (i) fputc(',', f); item_json(f, &items[i]); }
  fputs("]}\n", f); fclose(f);
  rename(tmp, state_file);
}

static void item_from(PlayerItem *it, JVal *o) {
  memset(it, 0, sizeof *it);
  jcpy(it->url, sizeof it->url, o, "url");
  jcpy(it->title, sizeof it->title, o, "title");
  jcpy(it->artist, sizeof it->artist, o, "artist");
  jcpy(it->album, sizeof it->album, o, "album");
  jcpy(it->cover, sizeof it->cover, o, "cover");
  jcpy(it->source, sizeof it->source, o, "source");
  jcpy(it->id, sizeof it->id, o, "id");
  it->dur = jnum(o, "dur", 0);
}

// ----------------------------------------------------------------- ordine --
static unsigned rnd_state = 0x9e3779b9u;
static unsigned rnd(void) { rnd_state ^= rnd_state << 13; rnd_state ^= rnd_state >> 17; rnd_state ^= rnd_state << 5; return rnd_state; }

// ricostruisce l'ordine tenendo il brano corrente al suo posto
static void reorder(void) {
  for (int i = 0; i < count; i++) order[i] = i;
  if (!shuffle || count < 2) return;
  for (int i = count - 1; i > 0; i--) { int j = (int)(rnd() % (unsigned)(i + 1)); int t = order[i]; order[i] = order[j]; order[j] = t; }
  if (cur >= 0) for (int i = 0; i < count; i++) if (order[i] == cur) { order[i] = order[0]; order[0] = cur; break; }
}
static int pos_of(int idx) { for (int i = 0; i < count; i++) if (order[i] == idx) return i; return -1; }

// ------------------------------------------------------------- caricamento --
static void reset_track_locked(void) {
  gen++; r_head = r_tail = 0; ring_gen = gen; eof_pending = 0; played = 0;
  dur = 0; base_pos = 0; live = 0;
  stream_title[0] = meta_title[0] = meta_artist[0] = meta_album[0] = 0;
  free(cover_buf); cover_buf = NULL; cover_len = 0;
}

static void load_locked(int idx, int play) {
  cur = idx;
  reset_track_locked();
  err[0] = 0;
  if (idx < 0 || idx >= count) { state = PL_STOPPED; want_load = 0; bump(); return; }
  want_load = 1; want_seek = -1;
  state = play ? PL_LOADING : PL_PAUSED;
  bump();
}

// passa al successivo nell'ordine; 0 se la coda è finita
static int advance_locked(int dir, int user) {
  if (!count) return 0;
  if (repeat == PL_REPEAT_ONE && !user) { load_locked(cur, 1); return 1; }
  int p = pos_of(cur) + dir;
  if (p >= count) { if (repeat == PL_REPEAT_ALL || user) { if (shuffle) reorder(); p = 0; } else return 0; }
  if (p < 0) p = repeat == PL_REPEAT_ALL ? count - 1 : 0;
  load_locked(order[p], 1);
  return 1;
}

// ---------------------------------------------------------- thread decode --
static int interrupt_cb(void *opaque) {
  int my = *(int *)opaque;
  return my != gen;     // brano cambiato o fermato: interrompi letture e connessioni
}

static void copy_tag(char *dst, size_t n, AVDictionary *md, const char *key) {
  AVDictionaryEntry *e = av_dict_get(md, key, NULL, 0);
  if (e && e->value) snprintf(dst, n, "%s", e->value);
}

// "StreamTitle='Artista - Titolo';StreamUrl='...';"
static void parse_icy(const char *pkt) {
  const char *p = strstr(pkt, "StreamTitle='"); if (!p) return;
  p += 13; const char *e = strstr(p, "';"); if (!e) e = p + strlen(p);
  size_t n = (size_t)(e - p); if (n >= sizeof stream_title) n = sizeof stream_title - 1;
  if (n == strlen(stream_title) && !strncmp(stream_title, p, n)) return;
  memcpy(stream_title, p, n); stream_title[n] = 0;
  bump();
  if (on_track && stream_title[0]) on_track(stream_title, "");
}

static void *decode_thread(void *arg) {
  (void)arg;
  for (;;) {
    pthread_mutex_lock(&mx);
    while (!want_load) pthread_cond_wait(&cv, &mx);
    want_load = 0;
    int my = gen;
    PlayerItem it = items[cur];
    int start_paused = state == PL_PAUSED;
    pthread_mutex_unlock(&mx);

    AVFormatContext *fc = avformat_alloc_context();
    fc->interrupt_callback.callback = interrupt_cb;
    fc->interrupt_callback.opaque = &my;
    AVDictionary *opt = NULL;
    av_dict_set(&opt, "user_agent", "Omega/1.0 (PS5 homebrew; +https://play.omegasuite.it)", 0);
    av_dict_set(&opt, "icy", "1", 0);
    av_dict_set(&opt, "reconnect", "1", 0);
    av_dict_set(&opt, "reconnect_streamed", "1", 0);
    av_dict_set(&opt, "reconnect_delay_max", "8", 0);
    av_dict_set(&opt, "rw_timeout", "15000000", 0);
    av_dict_set(&opt, "tls_verify", "0", 0);
    const char *why = NULL; char ebuf[128];
    AVCodecContext *cc = NULL; SwrContext *sw = NULL; AVPacket *pkt = NULL; AVFrame *fr = NULL;
    int si = -1;
    int rc = avformat_open_input(&fc, it.url, NULL, &opt);
    av_dict_free(&opt);
    if (rc < 0) { av_strerror(rc, ebuf, sizeof ebuf); why = ebuf; fc = NULL; goto done; }
    if (avformat_find_stream_info(fc, NULL) < 0) { why = "formato non riconosciuto"; goto done; }
    const AVCodec *codec = NULL;
    si = av_find_best_stream(fc, AVMEDIA_TYPE_AUDIO, -1, -1, &codec, 0);
    if (si < 0 || !codec) { why = "nessuna traccia audio"; goto done; }
    cc = avcodec_alloc_context3(codec);
    avcodec_parameters_to_context(cc, fc->streams[si]->codecpar);
    if (avcodec_open2(cc, codec, NULL) < 0) { why = "decoder non disponibile"; goto done; }
    AVChannelLayout out_l = AV_CHANNEL_LAYOUT_STEREO;
    if (swr_alloc_set_opts2(&sw, &out_l, AV_SAMPLE_FMT_S16, SR, &cc->ch_layout, cc->sample_fmt, cc->sample_rate, 0, NULL) < 0 || swr_init(sw) < 0) { why = "conversione audio"; goto done; }

    pthread_mutex_lock(&mx);
    if (my == gen) {
      dur = fc->duration > 0 ? fc->duration / (double)AV_TIME_BASE : it.dur;
      live = !(fc->pb && (fc->pb->seekable & AVIO_SEEKABLE_NORMAL)) || dur <= 0;
      copy_tag(meta_title, sizeof meta_title, fc->metadata, "title");
      copy_tag(meta_artist, sizeof meta_artist, fc->metadata, "artist");
      copy_tag(meta_album, sizeof meta_album, fc->metadata, "album");
      // file: i tag sono più belli del nome del file, anche nella coda
      if (!strcmp(items[cur].source, "file")) {
        if (meta_title[0]) snprintf(items[cur].title, sizeof items[cur].title, "%s", meta_title);
        if (meta_artist[0]) snprintf(items[cur].artist, sizeof items[cur].artist, "%s", meta_artist);
        if (meta_album[0]) snprintf(items[cur].album, sizeof items[cur].album, "%s", meta_album);
        if (dur > 0) items[cur].dur = dur;
      }
      // copertina incorporata (ID3 APIC, FLAC PICTURE, mp4 covr)
      for (unsigned s = 0; s < fc->nb_streams; s++) {
        AVStream *st = fc->streams[s];
        if ((st->disposition & AV_DISPOSITION_ATTACHED_PIC) && st->attached_pic.size > 0 && st->attached_pic.size < 4 * 1024 * 1024) {
          cover_buf = malloc((size_t)st->attached_pic.size);
          if (cover_buf) { memcpy(cover_buf, st->attached_pic.data, (size_t)st->attached_pic.size); cover_len = (size_t)st->attached_pic.size; }
          snprintf(cover_mime, sizeof cover_mime, "%s", st->codecpar->codec_id == AV_CODEC_ID_PNG ? "image/png" : "image/jpeg");
          break;
        }
      }
      if (!start_paused) state = PL_PLAYING;
      bump();
      if (on_track) {
        const char *t = meta_title[0] ? meta_title : it.title;
        const char *a = meta_artist[0] ? meta_artist : it.artist;
        on_track(t, a);
      }
    }
    pthread_mutex_unlock(&mx);
    player_log("lettore: aperto %s (%s, %d Hz, %s)", it.title[0] ? it.title : it.url, codec->name, cc->sample_rate, live ? "diretta" : "file");

    pkt = av_packet_alloc(); fr = av_frame_alloc();
    int16_t conv[8192 * 2];
    for (;;) {
      // seek richiesto?
      pthread_mutex_lock(&mx);
      if (my != gen) { pthread_mutex_unlock(&mx); break; }
      double sk = want_seek; want_seek = -1;
      if (sk >= 0 && !live) {
        r_head = r_tail = 0; played = 0; base_pos = sk; eof_pending = 0;
        pthread_mutex_unlock(&mx);
        av_seek_frame(fc, -1, (int64_t)(sk * AV_TIME_BASE), AVSEEK_FLAG_BACKWARD);
        avcodec_flush_buffers(cc);
        swr_init(sw);
        pthread_mutex_lock(&mx);
        bump();
      }
      pthread_mutex_unlock(&mx);

      int r = av_read_frame(fc, pkt);
      if (r == AVERROR(EAGAIN)) { usleep(10000); continue; }
      if (r < 0) {
        if (r != AVERROR_EOF && my == gen) { av_strerror(r, ebuf, sizeof ebuf); player_log("lettore: lettura interrotta: %s", ebuf); }
        break;
      }
      // titoli delle radio (ICY) aggiornati durante la riproduzione
      if (fc->pb) {
        uint8_t *icy = NULL;
        if (av_opt_get(fc->pb, "icy_metadata_packet", AV_OPT_SEARCH_CHILDREN, &icy) >= 0 && icy) {
          if (*icy) { pthread_mutex_lock(&mx); if (my == gen) parse_icy((const char *)icy); pthread_mutex_unlock(&mx); }
          av_free(icy);
        }
      }
      // radio Ogg/FLAC: il titolo arriva come metadati del flusso
      AVStream *ast = fc->streams[si];
      if ((ast->event_flags & AVSTREAM_EVENT_FLAG_METADATA_UPDATED) || (fc->event_flags & AVFMT_EVENT_FLAG_METADATA_UPDATED)) {
        ast->event_flags &= ~AVSTREAM_EVENT_FLAG_METADATA_UPDATED; fc->event_flags &= ~AVFMT_EVENT_FLAG_METADATA_UPDATED;
        char t[256] = "", a[256] = "", pk[600];
        copy_tag(t, sizeof t, ast->metadata, "title"); if (!t[0]) copy_tag(t, sizeof t, fc->metadata, "StreamTitle");
        copy_tag(a, sizeof a, ast->metadata, "artist");
        if (t[0] && live) {
          snprintf(pk, sizeof pk, "StreamTitle='%s%s%s';", a, a[0] ? " - " : "", t);
          pthread_mutex_lock(&mx); if (my == gen) parse_icy(pk); pthread_mutex_unlock(&mx);
        }
      }
      if (pkt->stream_index != si) { av_packet_unref(pkt); continue; }
      if (avcodec_send_packet(cc, pkt) < 0) { av_packet_unref(pkt); continue; }
      av_packet_unref(pkt);
      while (avcodec_receive_frame(cc, fr) == 0) {
        int maxo = (int)(sizeof conv / 4);
        int n = swr_convert(sw, (uint8_t **)(uint8_t *[]){ (uint8_t *)conv }, maxo, (const uint8_t **)fr->extended_data, fr->nb_samples);
        av_frame_unref(fr);
        if (n <= 0) continue;
        // nell'anello, aspettando che ci sia posto
        int off = 0;
        pthread_mutex_lock(&mx);
        while (off < n && my == gen && want_seek < 0) {
          size_t room = RING_FRAMES - (r_head - r_tail);
          if (!room) { pthread_cond_wait(&cv, &mx); continue; }
          size_t k = (size_t)(n - off); if (k > room) k = room;
          for (size_t i = 0; i < k; i++) {
            size_t at = (r_head + i) % RING_FRAMES;
            ring[at * 2] = conv[(off + i) * 2]; ring[at * 2 + 1] = conv[(off + i) * 2 + 1];
          }
          r_head += k; off += (int)k;
          pthread_cond_broadcast(&cv);
        }
        int stop = my != gen;
        pthread_mutex_unlock(&mx);
        if (stop) break;
      }
    }
  done:
    if (why) player_log("lettore: errore su %s: %s", it.url, why);
    av_packet_free(&pkt); av_frame_free(&fr);
    swr_free(&sw); avcodec_free_context(&cc);
    if (fc) avformat_close_input(&fc);

    pthread_mutex_lock(&mx);
    if (my == gen) {
      if (why) {
        snprintf(err, sizeof err, "%s", why);
        state = PL_ERROR; bump();
        // un brano rotto non ferma la coda: si passa al prossimo dopo un attimo
        pthread_mutex_unlock(&mx);
        sleep(2);
        pthread_mutex_lock(&mx);
        if (my == gen && state == PL_ERROR && !advance_locked(1, 0)) { state = PL_STOPPED; bump(); }
      } else {
        eof_pending = 1; bump();
      }
    }
    pthread_mutex_unlock(&mx);
  }
  return NULL;
}

// ------------------------------------------------------------ thread out --
static void *out_thread(void *arg) {
  (void)arg;
  int16_t blk[BLOCK * 2];
  float gain = 0;            // volume effettivo, segue quello chiesto senza scatti
  int idle = 0, open = 0;
  for (;;) {
    pthread_mutex_lock(&mx);
    int playing = state == PL_PLAYING;
    size_t avail = r_head - r_tail;
    int got = 0;
    if (playing && ring_gen == gen && avail >= BLOCK) {
      for (int i = 0; i < BLOCK; i++) {
        size_t at = (r_tail + (size_t)i) % RING_FRAMES;
        blk[i * 2] = ring[at * 2]; blk[i * 2 + 1] = ring[at * 2 + 1];
      }
      r_tail += BLOCK; played += BLOCK; got = 1;
      pthread_cond_broadcast(&cv);
    } else if (playing && eof_pending && avail < BLOCK) {
      eof_pending = 0;
      if (!advance_locked(1, 0)) { state = PL_STOPPED; bump(); }
    }
    float target = volume / 100.0f; target *= target;     // curva più naturale all'orecchio
    pthread_mutex_unlock(&mx);

    if (!got) {
      // niente da suonare: dopo 3 s si chiude l'uscita, così non resta occupata
      if (open && ++idle > (SR / BLOCK) * 3) { aout_close(); open = 0; }
      if (!open) { usleep(20000); continue; }
      memset(blk, 0, sizeof blk);
    } else {
      idle = 0;
      if (!open) open = aout_open();
      if (!open) { usleep(100000); continue; }
    }
    for (int i = 0; i < BLOCK * 2; i++) {
      gain += (target - gain) * 0.002f;
      int v = (int)(blk[i] * gain);
      blk[i] = (int16_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : v);
    }
    aout_write(blk);
  }
  return NULL;
}

// ------------------------------------------------------------------ comandi --
void player_init(const char *file) {
  state_file = file;
  items = calloc(PLAYER_MAX_ITEMS, sizeof *items);
  rnd_state ^= (unsigned)time(NULL);
  if (file) {
    FILE *f = fopen(file, "r");
    if (f) {
      fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
      char *buf = n > 0 && n < 8 * 1024 * 1024 ? malloc((size_t)n + 1) : NULL;
      if (buf && fread(buf, 1, (size_t)n, f) == (size_t)n) {
        buf[n] = 0;
        JVal *j = json_parse(buf);
        if (j) {
          volume = (int)jnum(j, "volume", 70); shuffle = (int)jnum(j, "shuffle", 0); repeat = (int)jnum(j, "repeat", 0);
          JFOR(o, jget(j, "items")) { if (count >= PLAYER_MAX_ITEMS) break; item_from(&items[count++], o); }
          int idx = (int)jnum(j, "index", -1);
          cur = idx >= 0 && idx < count ? idx : (count ? 0 : -1);
          json_free(j);
        }
      }
      free(buf); fclose(f);
    }
  }
  reorder();
  avformat_network_init();
  av_log_set_level(AV_LOG_ERROR);
  pthread_t a, b;
  pthread_create(&a, NULL, decode_thread, NULL);
  pthread_create(&b, NULL, out_thread, NULL);
  player_log("lettore pronto: %d brani in coda", count);
}

void player_enqueue(const PlayerItem *in, int n, int mode, int start, int play) {
  pthread_mutex_lock(&mx);
  if (mode == 0) { count = 0; cur = -1; }
  int at = mode == 2 && cur >= 0 ? cur + 1 : count;
  if (n > PLAYER_MAX_ITEMS - count) n = PLAYER_MAX_ITEMS - count;
  if (n > 0) {
    memmove(&items[at + n], &items[at], (size_t)(count - at) * sizeof *items);
    memcpy(&items[at], in, (size_t)n * sizeof *items);
    if (cur >= at) cur += n;
    count += n;
  }
  reorder();
  if (mode == 0 && count) load_locked(start >= 0 && start < count ? start : 0, play);
  else if (play && n > 0) load_locked(at, 1);
  else if (cur < 0 && count) { cur = 0; bump(); }
  else bump();
  save();
  pthread_mutex_unlock(&mx);
}

void player_play(void) {
  pthread_mutex_lock(&mx);
  if (state == PL_PAUSED && cur >= 0 && (r_head != r_tail || want_load)) {
    if (want_load) state = PL_LOADING;
    else state = PL_PLAYING;
    // la diretta in pausa è vecchia: si riparte dal vivo
    if (live) load_locked(cur, 1);
    bump();
  } else if (state != PL_PLAYING && state != PL_LOADING && cur >= 0) load_locked(cur, 1);
  else if (cur < 0 && count) load_locked(0, 1);
  pthread_mutex_unlock(&mx);
}
void player_pause(void) {
  pthread_mutex_lock(&mx);
  if (state == PL_PLAYING || state == PL_LOADING) { state = PL_PAUSED; bump(); }
  pthread_mutex_unlock(&mx);
}
void player_toggle(void) {
  pthread_mutex_lock(&mx); int p = state == PL_PLAYING || state == PL_LOADING; pthread_mutex_unlock(&mx);
  if (p) player_pause(); else player_play();
}
void player_stop(void) {
  pthread_mutex_lock(&mx); reset_track_locked(); want_load = 0; state = PL_STOPPED; bump(); pthread_mutex_unlock(&mx);
}
void player_next(void) { pthread_mutex_lock(&mx); if (!advance_locked(1, 1)) { reset_track_locked(); state = PL_STOPPED; bump(); } save(); pthread_mutex_unlock(&mx); }
void player_prev(void) {
  pthread_mutex_lock(&mx);
  double pos = base_pos + played / (double)SR;
  if (pos > 4 && !live) { want_seek = 0; bump(); }   // come i lettori veri: prima torna all'inizio
  else advance_locked(-1, 1);
  save();
  pthread_mutex_unlock(&mx);
}
void player_jump(int i) { pthread_mutex_lock(&mx); if (i >= 0 && i < count) { load_locked(i, 1); save(); } pthread_mutex_unlock(&mx); }
void player_remove(int i) {
  pthread_mutex_lock(&mx);
  if (i >= 0 && i < count) {
    int was_cur = i == cur;
    memmove(&items[i], &items[i + 1], (size_t)(count - i - 1) * sizeof *items);
    count--;
    if (i < cur) cur--;
    reorder();
    if (was_cur) { if (cur >= count) cur = count - 1; if (cur >= 0 && state == PL_PLAYING) load_locked(cur, 1); else { reset_track_locked(); state = PL_STOPPED; } }
    bump(); save();
  }
  pthread_mutex_unlock(&mx);
}
void player_clear(void) { pthread_mutex_lock(&mx); reset_track_locked(); count = 0; cur = -1; want_load = 0; state = PL_STOPPED; bump(); save(); pthread_mutex_unlock(&mx); }
void player_seek(double s) { pthread_mutex_lock(&mx); if (!live && s >= 0) { want_seek = dur > 0 && s > dur ? dur : s; bump(); } pthread_mutex_unlock(&mx); }
void player_volume(int v) { pthread_mutex_lock(&mx); volume = v < 0 ? 0 : v > 100 ? 100 : v; bump(); save(); pthread_mutex_unlock(&mx); }
void player_shuffle(int on) { pthread_mutex_lock(&mx); shuffle = !!on; reorder(); bump(); save(); pthread_mutex_unlock(&mx); }
void player_repeat(int m) { pthread_mutex_lock(&mx); repeat = m < 0 || m > 2 ? 0 : m; bump(); save(); pthread_mutex_unlock(&mx); }
void player_on_track(player_track_cb cb) { on_track = cb; }
int player_is_playing(void) { pthread_mutex_lock(&mx); int p = state == PL_PLAYING; pthread_mutex_unlock(&mx); return p; }

// ------------------------------------------------------------------- JSON --
static size_t jput(char *o, size_t n, size_t at, const char *s) {
  for (; *s && at + 7 < n; s++) {
    unsigned char c = (unsigned char)*s;
    if (c == '"' || c == '\\') { o[at++] = '\\'; o[at++] = (char)c; }
    else if (c < 0x20) at += (size_t)snprintf(o + at, n - at, "\\u%04x", c);
    else o[at++] = (char)c;
  }
  return at;
}
#define PUT(s) do { at = jput(out, n, at, (s)); } while (0)
#define RAW(...) do { if (at < n) at += (size_t)snprintf(out + at, n - at, __VA_ARGS__); if (at > n) at = n; } while (0)

size_t player_state_json(char *out, size_t n) {
  static const char *names[] = { "stopped", "loading", "playing", "paused", "error" };
  size_t at = 0;
  pthread_mutex_lock(&mx);
  const PlayerItem *it = cur >= 0 && cur < count ? &items[cur] : NULL;
  double pos = base_pos + played / (double)SR;
  RAW("{\"seq\":%u,\"state\":\"%s\",\"index\":%d,\"count\":%d,\"pos\":%.1f,\"dur\":%.1f,\"live\":%s,\"volume\":%d,\"shuffle\":%s,\"repeat\":%d,\"buffer\":%.1f,\"has_cover\":%s",
      seq, names[state], cur, count, pos, dur, live ? "true" : "false", volume, shuffle ? "true" : "false", repeat,
      (r_head - r_tail) / (double)SR, cover_len ? "true" : "false");
  RAW(",\"title\":\""); PUT(it ? (meta_title[0] ? meta_title : it->title) : ""); RAW("\"");
  RAW(",\"artist\":\""); PUT(it ? (meta_artist[0] ? meta_artist : it->artist) : ""); RAW("\"");
  RAW(",\"album\":\""); PUT(it ? (meta_album[0] ? meta_album : it->album) : ""); RAW("\"");
  RAW(",\"cover\":\""); PUT(it ? it->cover : ""); RAW("\"");
  RAW(",\"source\":\""); PUT(it ? it->source : ""); RAW("\"");
  RAW(",\"id\":\""); PUT(it ? it->id : ""); RAW("\"");
  RAW(",\"stream_title\":\""); PUT(stream_title); RAW("\"");
  RAW(",\"error\":\""); PUT(err); RAW("\"}");
  pthread_mutex_unlock(&mx);
  return at < n ? at : n - 1;
}

size_t player_queue_json(char *out, size_t n) {
  size_t at = 0;
  pthread_mutex_lock(&mx);
  RAW("{\"index\":%d,\"items\":[", cur);
  for (int i = 0; i < count && at + 2048 < n; i++) {
    const PlayerItem *it = &items[i];
    RAW("%s{\"title\":\"", i ? "," : ""); PUT(it->title);
    RAW("\",\"artist\":\""); PUT(it->artist);
    RAW("\",\"album\":\""); PUT(it->album);
    RAW("\",\"cover\":\""); PUT(it->cover);
    RAW("\",\"source\":\""); PUT(it->source);
    RAW("\",\"id\":\""); PUT(it->id);
    RAW("\",\"dur\":%.1f}", it->dur);
  }
  RAW("]}");
  pthread_mutex_unlock(&mx);
  return at < n ? at : n - 1;
}

size_t player_cover(unsigned char *out, size_t n, const char **mime) {
  pthread_mutex_lock(&mx);
  size_t k = cover_len <= n ? cover_len : 0;
  if (k) { memcpy(out, cover_buf, k); *mime = cover_mime; }
  pthread_mutex_unlock(&mx);
  return k;
}
