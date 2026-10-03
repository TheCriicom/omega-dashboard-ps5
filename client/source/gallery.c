// Omega UI — galleria: catture della console (/user/av_contents) e file su
// chiavetta USB. Il file scelto si carica sul server come avatar o copertina,
// in un thread a parte con l'avanzamento a schermo.
#include "app.h"
#include <dirent.h>
#include <fcntl.h>
#include <math.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

#define MAX_ITEMS 200
#define VIDEO_HEAD (12u * 1024 * 1024)   // dei video bastano i primi secondi; WebM/MP4 si leggono anche troncati
#define UPLOAD_MAX (96u * 1024 * 1024)
typedef struct { char path[300]; char ext[8]; int video; char tid[16]; char date[24]; long size; char sort[32]; } GItem;
static GItem items[MAX_ITEMS]; static int nitems, gsel;
static float ganim, gscroll;
static int gkind;                       // 0 avatar, 1 copertina

// ---------------------------------------------------------------- scansione --
static const char *ext_of(const char *name) { const char *d = strrchr(name, '.'); return d ? d + 1 : ""; }
static int supported(const char *ext, int *video) {
  char e[8]; size_t i = 0; for (; ext[i] && i < 7; i++) e[i] = (char)(ext[i] | 0x20); e[i] = 0;
  *video = !strcmp(e, "webm") || !strcmp(e, "mp4") || !strcmp(e, "mov") || !strcmp(e, "mkv");
  return *video || !strcmp(e, "jxr") || !strcmp(e, "jpg") || !strcmp(e, "jpeg") || !strcmp(e, "png") || !strcmp(e, "webp");
}

static void add_file(const char *path, const char *name, const char *tid) {
  if (nitems >= MAX_ITEMS) return;
  int video; const char *ext = ext_of(name);
  if (!supported(ext, &video)) return;
  if (strstr(name, ".jxr.jxr") || strstr(name, ".webm.jxr")) return;     // miniature di sistema
  struct stat st; if (stat(path, &st) != 0 || st.st_size < 64) return;
  GItem *g = &items[nitems++]; memset(g, 0, sizeof *g);
  snprintf(g->path, sizeof g->path, "%s", path);
  snprintf(g->ext, sizeof g->ext, "%s", ext);
  for (char *c = g->ext; *c; c++) *c = (char)(*c | 0x20);
  g->video = video; g->size = (long)st.st_size;
  snprintf(g->tid, sizeof g->tid, "%s", tid ? tid : "");
  // nome tipo 20261002_170728_xxx → data leggibile
  int Y, M, D, h, m;
  if (sscanf(name, "%4d%2d%2d_%2d%2d", &Y, &M, &D, &h, &m) == 5) {
    static const char *MM[12] = { "gen", "feb", "mar", "apr", "mag", "giu", "lug", "ago", "set", "ott", "nov", "dic" };
    snprintf(g->date, sizeof g->date, "%d %s %d, %02d:%02d", D, MM[(M - 1) % 12], Y, h, m);
    snprintf(g->sort, sizeof g->sort, "%04d%02d%02d%02d%02d", Y, M, D, h, m);
  } else {
    snprintf(g->date, sizeof g->date, "%s", name);
    snprintf(g->sort, sizeof g->sort, "%010ld", (long)st.st_mtime);
  }
}

// scende nelle cartelle fino a depth livelli; tid = nome della cartella che sembra un title id
static void walk(const char *dir, int depth, const char *tid) {
  DIR *dp = opendir(dir); if (!dp) return;
  struct dirent *e;
  while ((e = readdir(dp)) && nitems < MAX_ITEMS) {
    if (e->d_name[0] == '.') continue;
    char p[300]; snprintf(p, sizeof p, "%s/%s", dir, e->d_name);
    struct stat st; if (stat(p, &st) != 0) continue;
    if (S_ISDIR(st.st_mode)) {
      if (depth > 0) {
        int is_tid = strlen(e->d_name) == 9 && e->d_name[0] >= 'A' && e->d_name[0] <= 'Z' && e->d_name[4] >= '0' && e->d_name[4] <= '9' && strncmp(e->d_name, "NPXS", 4);
        walk(p, depth - 1, is_tid ? e->d_name : tid);
      }
    } else add_file(p, e->d_name, tid);
  }
  closedir(dp);
}

static void scan(void) {
  nitems = 0;
#ifdef PS5
  walk("/user/av_contents/photo", 4, NULL);
  walk("/user/av_contents/video", 4, NULL);
  for (int u = 0; u < 4; u++) { char d[32]; snprintf(d, sizeof d, "/mnt/usb%d", u); walk(d, 1, NULL); }
#else
  walk(OMEGA_DIR "/gallery", 3, NULL);
#endif
  // più recenti prima
  for (int a = 0; a < nitems; a++) for (int b = a + 1; b < nitems; b++)
    if (strcmp(items[b].sort, items[a].sort) > 0) { GItem t = items[a]; items[a] = items[b]; items[b] = t; }
}

// -------------------------------------------------------------- caricamento --
static SDL_atomic_t up_state;           // 0 fermo, 1 in corso, 2 finito
static char up_result[512]; static int up_status; static char up_label[80];
static Uint32 up_started;
typedef struct { char path[300]; char ext[8]; int kind; } UpJob;

static int upload_thread(void *arg) {
  UpJob *j = arg;
  up_status = -1; up_result[0] = 0;
  int fd = open(j->path, O_RDONLY);
  if (fd >= 0) {
    struct stat st; fstat(fd, &st);
    size_t len = (size_t)st.st_size;
    int video; if (supported(j->ext, &video) && video && len > VIDEO_HEAD) len = VIDEO_HEAD;
    if (len > UPLOAD_MAX) { up_status = 413; }
    else {
      unsigned char *buf = malloc(len);
      size_t got = 0; ssize_t r;
      while (buf && got < len && (r = read(fd, buf + got, len - got)) > 0) got += (size_t)r;
      if (buf && got == len) {
        char path[160]; snprintf(path, sizeof path, OMEGA_API "/media/upload?kind=%s&ext=%s", j->kind ? "cover" : "avatar", j->ext);
        up_status = omega_http_upload(path, g_token, buf, len, up_result, sizeof up_result);
      }
      free(buf);
    }
    close(fd);
  }
  free(j);
  SDL_AtomicSet(&up_state, 2);
  return 0;
}

static void start_upload(const GItem *g, int kind) {
  if (SDL_AtomicGet(&up_state) == 1) return;
  UpJob *j = calloc(1, sizeof *j);
  snprintf(j->path, sizeof j->path, "%s", g->path); snprintf(j->ext, sizeof j->ext, "%s", g->ext); j->kind = kind;
  snprintf(up_label, sizeof up_label, "%s", kind ? "Nuova copertina" : "Nuovo avatar");
  SDL_AtomicSet(&up_state, 1); up_started = SDL_GetTicks();
  SDL_Thread *t = SDL_CreateThread(upload_thread, "upload", j);
  if (t) SDL_DetachThread(t); else { free(j); SDL_AtomicSet(&up_state, 0); set_msg("Impossibile avviare il caricamento", 1); }
}

// dal ciclo principale: chiude il caricamento quando il thread ha finito
void gallery_tick(void) {
  if (SDL_AtomicGet(&up_state) != 2) return;
  SDL_AtomicSet(&up_state, 0);
  if (up_status == 201) {
    set_msg(gkind ? "Copertina aggiornata" : "Avatar aggiornato", 0);
    social_sync_now();
    if (PR.loaded) profile_open(PR.oid);
  } else {
    JVal *j = json_parse(up_result);
    const char *e = jstr(j, "error", "");
    set_msg(up_status == 413 || !strcmp(e, "file_too_large") ? "File troppo grande (massimo 96 MB)" :
            !strcmp(e, "too_many_uploads") ? "Troppi caricamenti: riprova tra qualche minuto" :
            !strcmp(e, "conversion_failed") ? "Il server non è riuscito a convertire il file" :
            up_status < 0 ? "Caricamento non riuscito (rete)" : "Caricamento non riuscito", 1);
    json_free(j);
  }
}

// disegnata sopra tutto mentre il caricamento è in corso
void upload_overlay(void) {
  if (SDL_AtomicGet(&up_state) != 1) return;
  int w = 560, h = 120, x = SCREEN_W - w - 40, y = SCREEN_H - h - 110;
  shadow_rrect(x, y, w, h, 24, 22, 150);
  fill_rrect(x, y, w, h, 24, C_PANEL, 245);
  draw_spinner(x + 60, y + h / 2, 20, 255);
  draw_text(font(W_MED, 28), up_label, x + 110, y + 26, C_TXT, 255, AL_L);
  char m[80]; Uint32 s = (SDL_GetTicks() - up_started) / 1000;
  snprintf(m, sizeof m, "Caricamento e conversione... %us", s);
  draw_text(font(W_REG, 22), m, x + 110, y + 68, C_DIM, 255, AL_L);
}

// ----------------------------------------------------------------- pannello --
void gallery_open(int kind) {
  gkind = kind; gsel = 0; gscroll = 0;
  scan();
  ov_push(OV_GALLERY);
}

static const char *game_name(const char *tid) {
  for (int i = 0; i < napps; i++) if (!strcmp(apps[i].tid, tid)) return apps[i].name;
  return tid[0] ? tid : "Chiavetta USB";
}

void gallery_draw(float t) {
  int a = (int)(255 * t);
  fill_rect(0, 0, SCREEN_W, SCREEN_H, RGB(2, 4, 10), (int)(150 * t));
  int w = 1200, h = 920, x = SCREEN_W / 2 - w / 2, y = SCREEN_H / 2 - h / 2 + (int)((1 - ease_out(t)) * 50);
  shadow_rrect(x, y, w, h, 34, 50, a * 70 / 100);
  fill_rrect(x, y, w, h, 34, C_PANEL, a);
  draw_text(font(W_LIGHT, 46), gkind ? "Scegli la copertina" : "Scegli il tuo avatar", x + 60, y + 44, C_WHITE, a, AL_L);
  draw_text(font(W_REG, 24), "Foto e video catturati con la console o presenti su chiavetta USB. Dei video si usano i primi 3 secondi.", x + 62, y + 110, C_DIM, a, AL_L);
  int ly = y + 170, rh = 100, lh = h - 210;
  int total = nitems + 1;   // la prima riga ripristina il predefinito
  if (gsel >= total) gsel = total - 1;
  ganim = approach(ganim, (float)gsel, 20.0f);
  float target = gsel * rh + rh > lh ? (float)(gsel * rh + rh - lh + rh / 2) : 0;
  gscroll = approach(gscroll, target, 14.0f);
  SDL_Rect clip = { x, ly - 6, w, lh }; SDL_RenderSetClipRect(R, &clip);
  for (int i = 0; i < total; i++) {
    int ry = ly + i * rh - (int)gscroll;
    if (ry + rh < ly - 10 || ry > ly + lh) continue;
    float fa = clampf(1 - fabsf(ganim - i), 0, 1);
    if (fa > 0.01f) { fill_rrect(x + 50, ry, w - 100, rh - 12, 18, C_WHITE, (int)(a * 0.11f * fa)); stroke_rrect(x + 47, ry - 3, w - 94, rh - 6, 21, 3, C_WHITE, (int)(a * fa)); }
    int cx = x + 110, cy = ry + (rh - 12) / 2;
    if (i == 0) {
      fill_circle(cx, cy, 32, RGB(60, 66, 86), a); draw_icon(IC_RELOAD, cx, cy, 32, C_TXT, a);
      draw_text(font(W_MED, 28), gkind ? "Usa la copertina predefinita" : "Usa un avatar Omega (illustrato o colore)", x + 170, ry + 26, C_TXT, a, AL_L);
      continue;
    }
    GItem *g = &items[i - 1];
    fill_circle(cx, cy, 32, g->video ? RGB(140, 60, 255) : C_ACC, a);
    draw_icon(g->video ? IC_PLAY : IC_STAR, cx + (g->video ? 2 : 0), cy, 30, C_WHITE, a);
    char l1[160]; snprintf(l1, sizeof l1, "%s \xC2\xB7 %s", g->video ? "Video" : "Foto", game_name(g->tid));
    draw_text_fit(font(W_MED, 27), l1, x + 170, ry + 14, w - 420, C_TXT, a, AL_L);
    draw_text(font(W_REG, 22), g->date, x + 170, ry + 52, C_DIM, a, AL_L);
    char sz[32]; snprintf(sz, sizeof sz, "%.1f MB", g->size / 1048576.0);
    draw_text(font(W_REG, 22), sz, x + w - 80, ry + 32, C_FAINT, a, AL_R);
  }
  if (!nitems) draw_text(font(W_REG, 26), "Nessuna cattura trovata. Usa il tasto Create per fare uno screenshot o un video.", x + w / 2, ly + 180, C_FAINT, a, AL_C);
  SDL_RenderSetClipRect(R, NULL);
  const int ic[] = { IC_BTN_X, IC_BTN_O };
  const char *lb[] = { "Usa", "Indietro" };
  hints(ic, lb, 2, a);
}

static void clear_done(int st, JVal *j, const char *raw, void *ud) {
  (void)j; (void)raw;
  set_msg(st == 200 ? ((intptr_t)ud ? "Copertina predefinita ripristinata" : "Avatar personalizzato rimosso") : "Operazione non riuscita", st != 200);
  social_sync_now();
  if (PR.loaded) profile_open(PR.oid);
}

void gallery_input(int b) {
  int total = nitems + 1;
  if (b == B_O) { ov_pop(); return; }
  if (b == B_UP && gsel > 0) gsel--;
  else if (b == B_DOWN && gsel < total - 1) gsel++;
  else if (b == B_X) {
    if (gsel == 0) {
      char path[64]; snprintf(path, sizeof path, OMEGA_API "/media/clear?kind=%s", gkind ? "cover" : "avatar");
      net_req(HTTP_POST, path, "{}", clear_done, (void *)(intptr_t)gkind);
      if (!gkind) media_note(S.me, "", 0);
      ov_pop();
      return;
    }
    if (SDL_AtomicGet(&up_state) == 1) { set_msg("C'è già un caricamento in corso", 1); return; }
    start_upload(&items[gsel - 1], gkind);
    ov_pop();
  }
}
