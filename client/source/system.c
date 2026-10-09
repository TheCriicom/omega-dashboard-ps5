// Omega UI — Sistema: stato della console (temperature, ventola, memoria,
// archivi, rete, servizi homebrew, jailbreak) e gestore dei file.
// Le letture lente (spazio dei dischi, porte dei servizi) girano in un thread
// ogni 2 s; il disegno usa l'ultima fotografia.
#include "app.h"
#include <arpa/inet.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/mount.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/sysctl.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#define FAN_FILE OMEGA_DIR "/fan.txt"   // soglia scelta: la riapplica il demone all'avvio
#define FAN_MIN 55
#define FAN_MAX 85                      // quella di fabbrica è 91 °C

#ifdef PS5
#include <ps5/kernel.h>
typedef struct { size_t size; char str[0x1C]; uint32_t ver; } SwVer;
int sceNetCtlInit(void);
int sceNetCtlGetInfo(int code, void *info);
// Temperature, ventola, firmware e memoria: non tutte esistono nella libkernel
// che vede un'app (sceKernelGetCurrentFanDuty è solo in libkernel_sys), e una
// funzione assente chiamata direttamente fa chiudere Omega. Si cercano a runtime.
static void *ksym(const char *name) {
  static const char *libs[] = { "libkernel_sys.sprx", "libkernel.sprx", "libkernel_web.sprx" };
  for (int i = 0; i < 3; i++) {
    uint32_t h; if (kernel_dynlib_handle(-1, libs[i], &h)) continue;
    intptr_t p = kernel_dynlib_dlsym(-1, h, name);
    if (p) return (void *)p;
  }
  return NULL;
}
// Le firme di ventola, frequenza e modello vengono da progetti GPL-3 provati
// su console vere: drakmor/fan_target e Marice/ps5-exporter. La ventola prima
// si leggeva con due int e rovinava lo stack: vuole uint16_t e uint64_t.
static struct {
  int done;
  int (*cpu_t)(int *); int (*soc_t)(int, int *); int (*fan)(uint16_t *, uint64_t *);
  int (*sw)(SwVer *); int (*mem)(size_t *); long (*freq)(void); int (*model)(char *);
} K;
static void ksyms(void) {
  if (K.done) return;
  K.cpu_t = ksym("sceKernelGetCpuTemperature"); K.soc_t = ksym("sceKernelGetSocSensorTemperature");
  K.sw = ksym("sceKernelGetProsperoSystemSwVersion");
  K.fan = ksym("sceKernelGetCurrentFanDuty");
  K.mem = ksym("sceKernelAvailableFlexibleMemorySize");
  K.freq = ksym("sceKernelGetCpuFrequency"); K.model = ksym("sceKernelGetHwModelName");
  omega_log("sistema: temp %d sensori %d ventola %d firmware %d memoria %d frequenza %d modello %d", !!K.cpu_t, !!K.soc_t, !!K.fan, !!K.sw, !!K.mem, !!K.freq, !!K.model);
  K.done = 1;
}
#endif

// temperatura del processore per la barra della home (Personalizza), -1 se non si sa
int sys_cpu_temp(void) {
#ifdef PS5
  ksyms(); int t = -1;
  if (K.cpu_t && K.cpu_t(&t) == 0 && t > 0 && t < 130) return t;
#endif
  return -1;
}

// ------------------------------------------------------------ fotografia --
typedef struct { char label[64], path[96]; double total, free; } Disk;
typedef struct { const char *name; int port; int up; } Svc;
typedef struct {
  int cpu_t, soc_t, fan;           // -1 = non disponibile
  double mem_free;                 // MB, < 0 non disponibile
  char fw[32], kfw[16], ip[48], jb[96], model[64];
  int cpu_mhz; long uptime;         // 0 = non disponibile
  int ndisk; Disk disk[8];
  Svc svc[6];
  int fan_threshold;               // quella salvata da Omega, 0 = mai impostata
} SysInfo;

static SysInfo snap, work;
static SDL_mutex *snap_mx;
static SDL_atomic_t sys_running;
static int sys_sel; static float sys_anim;

static int port_open(int port) {
  int s = socket(AF_INET, SOCK_STREAM, 0); if (s < 0) return 0;
  fcntl(s, F_SETFL, fcntl(s, F_GETFL, 0) | O_NONBLOCK);
  struct sockaddr_in a; memset(&a, 0, sizeof a);
  a.sin_family = AF_INET; a.sin_port = htons((unsigned short)port); a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  int up = 0;
  if (connect(s, (struct sockaddr *)&a, sizeof a) == 0) up = 1;
  else if (errno == EINPROGRESS) {
    fd_set w; FD_ZERO(&w); FD_SET(s, &w);
    struct timeval tv = { 0, 250000 };
    if (select(s + 1, NULL, &w, NULL, &tv) == 1) { int e = 0; socklen_t l = sizeof e; getsockopt(s, SOL_SOCKET, SO_ERROR, &e, &l); up = e == 0; }
  }
  close(s);
  return up;
}

static void add_disk(SysInfo *si, const char *label, const char *path) {
  if (si->ndisk >= 8) return;
  struct statfs st;
  if (statfs(path, &st) != 0 || st.f_blocks == 0) return;
  Disk *d = &si->disk[si->ndisk++];
  snprintf(d->label, sizeof d->label, "%s", label); snprintf(d->path, sizeof d->path, "%s", path);
  d->total = (double)st.f_blocks * st.f_bsize; d->free = (double)st.f_bavail * st.f_bsize;
}

static void gather(SysInfo *si) {
  memset(si, 0, sizeof *si);
  si->cpu_t = si->soc_t = si->fan = -1; si->mem_free = -1;
  snprintf(si->fw, sizeof si->fw, "\xE2\x80\x94"); snprintf(si->ip, sizeof si->ip, "\xE2\x80\x94");
#ifdef PS5
  int v;
  ksyms();
  if (K.cpu_t && K.cpu_t(&v) == 0 && v > 0 && v < 130) si->cpu_t = v;
  // il SoC ha fino a 16 sensori: si mostra il più caldo
  for (int k = 0; K.soc_t && k < 16; k++) if (K.soc_t(k, &v) == 0 && v > 0 && v < 130 && v > si->soc_t) si->soc_t = v;
  uint16_t duty = 0xffff; uint64_t chassis = 0;
  if (K.fan && K.fan(&duty, &chassis) == 0 && duty <= 1024) si->fan = (duty * 100 + 512) / 1024;
  if (K.freq) { long hz = K.freq(); if (hz > 100000000L && hz < 10000000000L) si->cpu_mhz = (int)(hz / 1000000); }
  char model[128]; memset(model, 0, sizeof model);
  if (K.model && K.model(model) == 0 && model[0]) snprintf(si->model, sizeof si->model, "%.63s", model);
  // firmware vero del kernel: quello di sistema può essere falsato dal jailbreak per non chiedere aggiornamenti
  uint32_t kv = kernel_get_fw_version();
  if (kv) snprintf(si->kfw, sizeof si->kfw, "%x.%02x", (kv >> 24) & 0xff, (kv >> 16) & 0xff);
  struct timeval boot; size_t bsz = sizeof boot; int mib[2] = { CTL_KERN, KERN_BOOTTIME };
  if (sysctl(mib, 2, &boot, &bsz, NULL, 0) == 0 && boot.tv_sec > 0) si->uptime = (long)(time(NULL) - boot.tv_sec);
  SwVer sw; memset(&sw, 0, sizeof sw); sw.size = sizeof sw;
  if (K.sw && K.sw(&sw) == 0 && sw.str[0]) { snprintf(si->fw, sizeof si->fw, "%.27s", sw.str); char *sp = strchr(si->fw, ' '); if (sp) *sp = 0; }
  size_t mf = 0; if (K.mem && K.mem(&mf) == 0) si->mem_free = mf / 1048576.0;
  static int netctl; if (!netctl) netctl = sceNetCtlInit() >= 0 ? 1 : -1;
  char info[256]; memset(info, 0, sizeof info);
  if (sceNetCtlGetInfo(14 /* IP_ADDRESS */, info) == 0 && info[0]) snprintf(si->ip, sizeof si->ip, "%.15s", info);
  add_disk(si, _("Archivio della console"), "/user");
  add_disk(si, _("Archivio M.2"), "/mnt/ext1");
  add_disk(si, _("Archivio esteso USB"), "/mnt/ext0");
  for (int u = 0; u < 4; u++) { char p[16], l[64]; snprintf(p, sizeof p, "/mnt/usb%d", u); snprintf(l, sizeof l, _("Chiavetta USB %d"), u + 1); add_disk(si, l, p); }
#else
  add_disk(si, _("Archivio della console"), OMEGA_DIR);
  add_disk(si, _("Chiavetta USB 1"), OMEGA_SYSROOT "/mnt/usb0");
#endif
  // jailbreak e caricatori (hen.c)
  snprintf(si->jb, sizeof si->jb, "%s", hen_name());
  static const struct { const char *n; int p; } S5[6] = { { "websrv", 8080 }, { "FTP", 2121 }, { "Payload loader", 9021 }, { "klog", 3232 }, { N_("Lettore musicale"), 9095 }, { "etaHEN RPC", 8000 } };
  for (int i = 0; i < 6; i++) { si->svc[i].name = S5[i].n; si->svc[i].port = S5[i].p; si->svc[i].up = port_open(S5[i].p); }
  char *f = file_read(FAN_FILE, 64, NULL); if (f) { si->fan_threshold = atoi(f); free(f); }
}

static int sys_thread(void *arg) {
  (void)arg;
  while (SDL_AtomicGet(&sys_running)) {
    gather(&work);
    SDL_LockMutex(snap_mx); snap = work; SDL_UnlockMutex(snap_mx);
    for (int i = 0; i < 20 && SDL_AtomicGet(&sys_running); i++) SDL_Delay(100);
  }
  return 0;
}

// ------------------------------------------------------------------- ventola --
// Temperatura obiettivo del controllo automatico della ventola (di fabbrica
// 91 °C): più bassa = la ventola accelera prima. Si legge la configurazione
// intera (28 byte), si cambia solo il byte 5 e si verifica, come fa
// drakmor/fan_target (GPL-3); prima si mandavano 10 byte quasi tutti a zero.
// Giochi e sistema a volte la rimettono a 91: il demone la ripristina.
#define FAN_GET 0xC01C8F08UL
#define FAN_SET 0xC01C8F07UL
static int fan_apply(int t) {
  if (t < FAN_MIN) t = FAN_MIN;
  if (t > FAN_MAX) t = FAN_MAX;
#ifdef PS5
  int fd = open("/dev/icc_fan", 0x10002, 0);
  if (fd < 0) fd = open("/dev/icc_fan", O_RDONLY, 0);
  if (fd < 0) return -1;
  uint8_t cfg[28], chk[28]; memset(cfg, 0, sizeof cfg); memset(chk, 0, sizeof chk);
  int rc = ioctl(fd, FAN_GET, cfg);
  if (rc == 0) { cfg[5] = (uint8_t)t; rc = ioctl(fd, FAN_SET, cfg); }
  if (rc == 0 && ioctl(fd, FAN_GET, chk) == 0 && chk[5] != (uint8_t)t) rc = -1;
  close(fd);
  if (rc != 0) return -2;
#endif
  FILE *f = fopen(FAN_FILE, "w"); if (f) { fprintf(f, "%d\n", t); fclose(f); }
  return 0;
}
static void fan_pick(int idx, void *ud) {
  (void)ud; if (idx < 0) return;
  if (idx == 7) { unlink(FAN_FILE); set_msg(_("Soglia della ventola: quella di sistema al prossimo riavvio"), 0); return; }
  int t = FAN_MIN + idx * 5;
  int rc = fan_apply(t);
  char m[120];
  if (rc == 0) snprintf(m, sizeof m, _("Ventola: accelera sopra %d \xC2\xB0""C"), t);
  else snprintf(m, sizeof m, "%s", _("Impossibile impostare la ventola su questa console"));
  set_msg(m, rc != 0);
}
static void fan_menu(void) {
  static char l[8][64]; static const char *it[8];
  for (int i = 0; i < 7; i++) {
    int t = FAN_MIN + i * 5;
    snprintf(l[i], sizeof l[i], t <= 65 ? _("%d \xC2\xB0""C \xE2\x80\x94 pi\xC3\xB9 fresca, pi\xC3\xB9 rumorosa") : t >= 75 ? _("%d \xC2\xB0""C \xE2\x80\x94 pi\xC3\xB9 silenziosa") : _("%d \xC2\xB0""C"), t);
    it[i] = l[i];
  }
  snprintf(l[7], sizeof l[7], "%s", _("Usa quella di sistema"));
  it[7] = l[7];
  menu_open(_("Soglia della ventola"), it, 8, fan_pick, NULL);
}

// ---------------------------------------------------------------- pannello --
enum { SY_FILES, SY_PKGS, SY_REMOTE, SY_HEN, SY_FAN, SY_RESTART_DAEMON, SY_DIAG, SY_N };
static char remote_pin[8];
static void remote_done(int st, JVal *j, const char *raw, void *ud) { (void)raw; (void)ud; if (st == 200) jcpy(remote_pin, sizeof remote_pin, j, "pin"); else remote_pin[0] = 0; }
void files_open(const char *start);

void system_open(void) {
  if (!snap_mx) snap_mx = SDL_CreateMutex();
  gather(&snap);   // la prima volta subito, poi il thread
  if (!SDL_AtomicGet(&sys_running)) {
    SDL_AtomicSet(&sys_running, 1);
    SDL_Thread *t = SDL_CreateThread(sys_thread, "sysinfo", NULL);
    if (t) SDL_DetachThread(t); else SDL_AtomicSet(&sys_running, 0);
  }
  sys_sel = 0;
  net_req(HTTP_GET, "http://127.0.0.1:9095/v1/remote", NULL, remote_done, NULL);
  ov_push(OV_SYSTEM);
}

void system_shutdown(void) { SDL_AtomicSet(&sys_running, 0); SDL_Delay(snap_mx ? 120 : 0); }

static Col temp_col(int t) { return t < 0 ? C_DIM : t >= 85 ? C_ERR : t >= 75 ? C_WARN : C_OK; }

static void stat_card(int x, int y, int w, int h, int icon, const char *label, const char *value, Col vc, int a) {
  fill_rrect(x, y, w, h, 22, RGB(255, 255, 255), a * 6 / 100);
  draw_icon(icon, x + 46, y + h / 2, 34, C_DIM, a);
  draw_text_fit(font(W_REG, 22), label, x + 86, y + 22, w - 100, C_DIM, a, AL_L);
  draw_text_fit(font(W_MED, 34), value, x + 86, y + 54, w - 100, vc, a, AL_L);
}

static void fmt_size(double b, char *out, size_t n) {
  if (b >= 1e12) snprintf(out, n, _("%.2f TB"), b / 1e12);
  else if (b >= 1e9) snprintf(out, n, _("%.1f GB"), b / 1e9);
  else snprintf(out, n, _("%.0f MB"), b / 1e6);
}

void system_draw(float t) {
  int a = (int)(255 * t);
  fill_rect(0, 0, SCREEN_W, SCREEN_H, mix(RGB(10, 12, 20), g_theme_base, 0.12f), t > 0.98f ? 255 : a);
  SysInfo si; SDL_LockMutex(snap_mx); si = snap; SDL_UnlockMutex(snap_mx);
  int x0 = 110;
  draw_icon(IC_GEAR, x0 + 22, 74, 46, C_ACC2, a);
  draw_text(font(W_LIGHT, 38), _("Sistema"), x0 + 62, 52, C_WHITE, a, AL_L);

  // riga di schede: temperature, ventola, memoria, firmware, IP
  char v[5][64];
  if (si.cpu_t >= 0 && si.cpu_mhz) snprintf(v[0], sizeof v[0], "%d \xC2\xB0""C \xC2\xB7 %.1f GHz", si.cpu_t, si.cpu_mhz / 1000.0);
  else if (si.cpu_t >= 0) snprintf(v[0], sizeof v[0], "%d \xC2\xB0""C", si.cpu_t); else snprintf(v[0], sizeof v[0], "\xE2\x80\x94");
  if (si.soc_t >= 0) snprintf(v[1], sizeof v[1], "%d \xC2\xB0""C", si.soc_t); else snprintf(v[1], sizeof v[1], "\xE2\x80\x94");
  if (si.fan >= 0) snprintf(v[2], sizeof v[2], "%d%%", si.fan); else snprintf(v[2], sizeof v[2], "\xE2\x80\x94");
  if (si.mem_free >= 0) snprintf(v[3], sizeof v[3], _("%.0f MB liberi"), si.mem_free); else snprintf(v[3], sizeof v[3], "\xE2\x80\x94");
  int cw = 322, ch = 120, gap = 18, y = 150;
  stat_card(x0, y, cw, ch, IC_POWER, _("Processore"), v[0], temp_col(si.cpu_t), a);
  stat_card(x0 + (cw + gap), y, cw, ch, IC_GAMEPAD, _("SoC"), v[1], temp_col(si.soc_t), a);
  stat_card(x0 + 2 * (cw + gap), y, cw, ch, IC_RELOAD, _("Ventola"), v[2], C_TXT, a);
  stat_card(x0 + 3 * (cw + gap), y, cw, ch, IC_MORE, _("Memoria"), v[3], C_TXT, a);
  stat_card(x0 + 4 * (cw + gap), y, cw, ch, IC_GLOBE, _("Indirizzo IP"), si.ip, C_TXT, a);

  // colonna sinistra: archivi
  y = 310;
  draw_text(font(W_MED, 28), _("Archivi"), x0, y, C_TXT, a, AL_L);
  y += 50;
  for (int i = 0; i < si.ndisk; i++) {
    Disk *d = &si.disk[i];
    double used = d->total - d->free; float k = d->total > 0 ? (float)(used / d->total) : 0;
    char fr[32], tt[32], line[128]; fmt_size(d->free, fr, sizeof fr); fmt_size(d->total, tt, sizeof tt);
    snprintf(line, sizeof line, _("%s liberi di %s"), fr, tt);
    draw_text_fit(font(W_MED, 24), d->label, x0, y, 500, C_TXT, a, AL_L);
    draw_text(font(W_REG, 22), line, x0 + 820, y + 2, C_DIM, a, AL_R);
    fill_rrect(x0, y + 38, 820, 12, 6, RGB(255, 255, 255), a * 12 / 100);
    fill_rrect(x0, y + 38, (int)(820 * k), 12, 6, k > 0.9f ? C_ERR : k > 0.75f ? C_WARN : C_ACC, a);
    y += 82;
  }
  if (!si.ndisk) draw_text(font(W_REG, 24), _("Nessun archivio leggibile"), x0, y, C_FAINT, a, AL_L);

  // colonna destra: console, servizi, azioni
  int rx = 1040, ry = 310;
  draw_text(font(W_MED, 28), _("Console"), rx, ry, C_TXT, a, AL_L);
  char l1[200];
  if (si.kfw[0] && strncmp(si.fw, si.kfw, strlen(si.kfw))) snprintf(l1, sizeof l1, _("Firmware %s (kernel %s)"), si.fw, si.kfw);
  else snprintf(l1, sizeof l1, _("Firmware %s"), si.fw);
  if (si.model[0]) { size_t L = strlen(l1); snprintf(l1 + L, sizeof l1 - L, "  \xC2\xB7  %s", si.model); }
  draw_text_fit(font(W_REG, 24), l1, rx, ry + 48, 760, C_DIM, a, AL_L);
  snprintf(l1, sizeof l1, _("Jailbreak: %s"), si.jb);
  if (si.uptime > 0) { size_t L = strlen(l1); long h = si.uptime / 3600, m = si.uptime / 60 % 60;
    snprintf(l1 + L, sizeof l1 - L, h ? _("  \xC2\xB7  acceso da %ld h %ld min") : _("  \xC2\xB7  acceso da %ld min"), h ? h : m, m); }
  draw_text_fit(font(W_REG, 24), l1, rx, ry + 82, 760, C_DIM, a, AL_L);
  ry += 140;
  draw_text(font(W_MED, 28), _("Servizi"), rx, ry, C_TXT, a, AL_L);
  ry += 48;
  for (int i = 0; i < 6; i++) {
    int cx = rx + (i % 2) * 380, cy = ry + (i / 2) * 42;
    fill_circle(cx + 9, cy + 15, 7, si.svc[i].up ? C_OK : RGB(90, 96, 112), a);
    char s[64]; snprintf(s, sizeof s, "%s  :%d", _(si.svc[i].name), si.svc[i].port);
    draw_text(font(W_REG, 23), s, cx + 28, cy, si.svc[i].up ? C_TXT : C_FAINT, a, AL_L);
  }
  ry += 3 * 42 + 14;
  static const char *acts[SY_N] = { N_("Gestore dei file"), N_("Installa PKG e cartelle dei giochi"), N_("Telecomando in Wi-Fi (avanzato)"), N_("Componenti per il jailbreak"), N_("Soglia della ventola"), N_("Riavvia il lettore musicale"), N_("Invia diagnostica allo sviluppatore") };
  static const int aic[SY_N] = { IC_FOLDER, IC_BOX, IC_CLOUD, IC_DOWNLOAD, IC_RELOAD, IC_MUSIC, IC_CHAT };
  sys_anim = approach(sys_anim, (float)sys_sel, 20.0f);
  for (int i = 0; i < SY_N; i++) {
    int yy = ry + i * 50;   // 7 voci sopra la barra dei comandi
    float fa = clampf(1 - fabsf(sys_anim - i), 0, 1);
    fill_rrect(rx, yy, 760, 44, 16, mix(RGB(34, 40, 56), C_WHITE, fa), a);
    Col fg = mix(C_TXT, RGB(12, 14, 22), fa);
    draw_icon(aic[i], rx + 40, yy + 22, 26, fg, a);
    char lab[128]; snprintf(lab, sizeof lab, "%s", _(acts[i]));
    if (i == SY_REMOTE && remote_pin[0]) { size_t l = strlen(lab); snprintf(lab + l, sizeof lab - l, " \xC2\xB7 PIN %s", remote_pin); }
    if (i == SY_FAN && si.fan_threshold) { size_t l = strlen(lab); snprintf(lab + l, sizeof lab - l, " \xC2\xB7 %d \xC2\xB0""C", si.fan_threshold); }
    draw_text_fit(font(W_MED, 24), lab, rx + 80, yy + 8, 660, fg, a, AL_L);
  }
  if (remote_pin[0] && strcmp(si.ip, "\xE2\x80\x94")) {
    char m[200]; snprintf(m, sizeof m, _("Per tutto il resto c'è l'App mobile: %s/app/"), omega_base()); (void)si;
    draw_text_fit(font(W_REG, 22), m, rx, ry + SY_N * 50 + 2, 760, C_DIM, a, AL_L);
  }
  const int ic[] = { IC_BTN_X, IC_BTN_O };
  const char *lb[] = { _("Scegli"), _("Indietro") };
  hints(ic, lb, 2, a);
}

static void restart_done(int st, JVal *j, const char *raw, void *ud) {
  (void)j; (void)raw; (void)ud;
  set_msg(st == 200 ? _("Lettore musicale fermato e coda svuotata") : _("Il lettore musicale non risponde"), st != 200);
}

// ------------------------------------------------------------ diagnostica --
// I registri restano sulla console: con l'OK dell'utente li si manda al server
// (/api/v1/diag/report), così si capisce perché musica o voce non vanno.
static void diag_esc(char *out, size_t *o, size_t cap, const char *s) {
  for (; *s && *o + 8 < cap; s++) {
    unsigned char c = (unsigned char)*s;
    if (c == '"' || c == '\\') { out[(*o)++] = '\\'; out[(*o)++] = (char)c; }
    else if (c == '\n') { out[(*o)++] = '\\'; out[(*o)++] = 'n'; }
    else if (c < 0x20) { *o += (size_t)snprintf(out + *o, cap - *o, "\\u%04x", c); }
    else out[(*o)++] = (char)c;
  }
}
static void diag_add(char *out, size_t *o, size_t cap, const char *name, const char *path, size_t tail) {
  size_t len = 0; char *t = file_read(path, 4 * 1024 * 1024, &len);
  if (!t) return;
  const char *from = len > tail ? t + len - tail : t;
  *o += (size_t)snprintf(out + *o, cap - *o, "%s\"%s\":\"", out[*o - 1] == '{' ? "" : ",", name);
  diag_esc(out, o, cap, from);
  if (*o + 2 < cap) out[(*o)++] = '"';
  free(t);
}
static void diag_done(int st, JVal *j, const char *raw, void *ud) {
  (void)j; (void)raw; (void)ud;
  set_msg(st == 201 ? _("Diagnostica inviata: grazie!") : _("Invio della diagnostica non riuscito"), st != 201);
}
static void diag_send(int idx, void *ud) {
  (void)idx; (void)ud;
  size_t cap = 900 * 1024, o = 0; char *b = malloc(cap); if (!b) return;
  o += (size_t)snprintf(b, cap, "{\"app_version\":\"%s\",\"logs\":{", OMEGA_VERSION);
  diag_add(b, &o, cap, "ui", OMEGA_LOG, 160 * 1024);
  diag_add(b, &o, cap, "service", OMEGA_DIR "/omega-redirect.log", 300 * 1024);
  diag_add(b, &o, cap, "onion", OMEGA_DIR "/omega-onion.log", 30 * 1024);
  diag_add(b, &o, cap, "voice_guard", OMEGA_DIR "/voice-guard.txt", 1024);
  diag_add(b, &o, cap, "hen_setup", OMEGA_DIR "/hen-setup.txt", 1024);
  if (o + 3 < cap) { b[o++] = '}'; b[o++] = '}'; b[o] = 0; } else { free(b); return; }
  net_req(HTTP_POST, OMEGA_API "/diag/report", b, diag_done, NULL);
  free(b);
  set_msg(_("Invio della diagnostica..."), 0);
}

void system_input(int b) {
  if (b == B_O) { SDL_AtomicSet(&sys_running, 0); ov_pop(); return; }
  if (b == B_UP && sys_sel > 0) { sys_sel--; sfx_play(SFX_MOVE); }
  else if (b == B_DOWN && sys_sel < SY_N - 1) { sys_sel++; sfx_play(SFX_MOVE); }
  else if (b == B_X) {
    if (sys_sel == SY_FILES) files_open(NULL);
    else if (sys_sel == SY_PKGS) pkgs_open();
    else if (sys_sel == SY_FAN) fan_menu();
    else if (sys_sel == SY_REMOTE) remote_open();
    else if (sys_sel == SY_HEN) hen_check(2);
    else if (sys_sel == SY_DIAG) confirm_open(_("Mandare allo sviluppatore i registri di Omega? Contengono i nomi di giochi, brani e radio usati, non le password."), _("Invia"), diag_send, NULL);
    else net_req(HTTP_POST, "http://127.0.0.1:9095/v1/cmd", "{\"cmd\":\"clear\"}", restart_done, NULL);
  }
}

// ============================================================ gestore file ==
// Si naviga ovunque; copiare, eliminare, rinominare e creare cartelle si può
// solo dove non si rischia di rompere la console.
#define FM_MAX 1500
typedef struct { char name[256]; int dir; long long size; } FEnt;
static FEnt *fe; static int nfe, fm_sel; static float fm_anim, fm_scroll;
static char fm_path[640];
static char clip_path[640]; static int clip_cut;
static SDL_atomic_t copy_state;            // 0 fermo, 1 in corso, 2 finito
static volatile long long copy_done, copy_total; static volatile int copy_err; static char copy_label[300];

static int writable(const char *p) {
  static const char *ok[] = { "/data/", "/mnt/usb", "/mnt/ext", "/user/av_contents/" };
  size_t rl = strlen(OMEGA_SYSROOT);
  if (strncmp(p, OMEGA_SYSROOT, rl)) return 0;
  char q[700]; snprintf(q, sizeof q, "%s/", p + rl);     // con la barra finale: "/data" diventa "/data/"
  for (size_t i = 0; i < sizeof ok / sizeof *ok; i++) if (!strncmp(q, ok[i], strlen(ok[i]))) return 1;
  return 0;
}

static int fe_cmp(const void *a, const void *b) {
  const FEnt *x = a, *y = b;
  if (x->dir != y->dir) return y->dir - x->dir;
  return strcasecmp(x->name, y->name);
}
static void fm_load(void) {
  if (!fe) fe = calloc(FM_MAX, sizeof *fe);
  nfe = 0;
  DIR *d = opendir(fm_path);
  if (d) {
    struct dirent *e;
    while ((e = readdir(d)) && nfe < FM_MAX) {
      if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
      char p[900]; snprintf(p, sizeof p, "%s/%s", fm_path, e->d_name);
      struct stat st; if (lstat(p, &st) != 0) continue;
      FEnt *f = &fe[nfe++]; snprintf(f->name, sizeof f->name, "%s", e->d_name);
      f->dir = S_ISDIR(st.st_mode); f->size = f->dir ? -1 : (long long)st.st_size;
    }
    closedir(d);
  }
  qsort(fe, (size_t)nfe, sizeof *fe, fe_cmp);
  if (fm_sel >= nfe) fm_sel = nfe ? nfe - 1 : 0;
}

void files_open(const char *start) {
  snprintf(fm_path, sizeof fm_path, "%s", start && *start ? start : OMEGA_SYSROOT "/data");
  fm_sel = 0; fm_scroll = 0;
  fm_load();
  ov_push(OV_FILES);
}

// copia ricorsiva (in un thread), poi eventuale eliminazione dell'originale
static int copy_file(const char *src, const char *dst) {
  int in = open(src, O_RDONLY); if (in < 0) return -1;
  int out = open(dst, O_WRONLY | O_CREAT | O_TRUNC, 0777); if (out < 0) { close(in); return -2; }
  char *buf = malloc(1 << 20); ssize_t r; int rc = 0;
  while (buf && (r = read(in, buf, 1 << 20)) > 0) {
    if (write(out, buf, (size_t)r) != r) { rc = -3; break; }
    copy_done += r;
  }
  if (r < 0) rc = -4;
  free(buf); close(in); close(out);
  return rc;
}
static long long tree_size(const char *p) {
  struct stat st; if (lstat(p, &st) != 0) return 0;
  if (!S_ISDIR(st.st_mode)) return st.st_size;
  long long t = 0; DIR *d = opendir(p); if (!d) return 0;
  struct dirent *e;
  while ((e = readdir(d))) { if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue; char q[900]; snprintf(q, sizeof q, "%s/%s", p, e->d_name); t += tree_size(q); }
  closedir(d); return t;
}
static int copy_tree(const char *src, const char *dst) {
  struct stat st; if (lstat(src, &st) != 0) return -1;
  if (!S_ISDIR(st.st_mode)) return copy_file(src, dst);
  if (mkdir(dst, 0777) != 0 && errno != EEXIST) return -5;
  DIR *d = opendir(src); if (!d) return -6;
  struct dirent *e; int rc = 0;
  while (!rc && (e = readdir(d))) {
    if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
    char a[900], b[900]; snprintf(a, sizeof a, "%s/%s", src, e->d_name); snprintf(b, sizeof b, "%s/%s", dst, e->d_name);
    rc = copy_tree(a, b);
  }
  closedir(d); return rc;
}
static int remove_tree(const char *p) {
  struct stat st; if (lstat(p, &st) != 0) return -1;
  if (!S_ISDIR(st.st_mode)) return unlink(p);
  DIR *d = opendir(p); if (!d) return -1;
  struct dirent *e; int rc = 0;
  while ((e = readdir(d))) { if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue; char q[900]; snprintf(q, sizeof q, "%s/%s", p, e->d_name); if (remove_tree(q)) rc = -1; }
  closedir(d);
  return rmdir(p) || rc;
}

typedef struct { char src[640], dst[900]; int cut; } CopyJob;
static int copy_thread(void *arg) {
  CopyJob *j = arg;
  copy_total = tree_size(j->src); copy_done = 0;
  // spostare nello stesso disco è un rename
  if (j->cut && rename(j->src, j->dst) == 0) copy_err = 0;
  else {
    copy_err = copy_tree(j->src, j->dst);
    if (!copy_err && j->cut) remove_tree(j->src);
  }
  free(j);
  SDL_AtomicSet(&copy_state, 2);
  return 0;
}

void files_tick(void) {
  if (SDL_AtomicGet(&copy_state) != 2) return;
  SDL_AtomicSet(&copy_state, 0);
  set_msg(copy_err ? _("Copia non riuscita (spazio o permessi)") : _("Copia completata"), copy_err != 0);
  if (ov_top() == OV_FILES) fm_load();
}
void files_overlay(void) {
  if (SDL_AtomicGet(&copy_state) != 1) return;
  int w = 640, h = 130, x = SCREEN_W - w - 40, y = SCREEN_H - h - 110;
  shadow_rrect(x, y, w, h, 24, 22, 150);
  fill_rrect(x, y, w, h, 24, C_PANEL, 245);
  draw_spinner(x + 60, y + h / 2, 20, 255);
  draw_text_fit(font(W_MED, 26), copy_label, x + 110, y + 24, w - 140, C_TXT, 255, AL_L);
  float k = copy_total > 0 ? (float)copy_done / (float)copy_total : 0;
  fill_rrect(x + 110, y + 76, w - 150, 10, 5, RGB(255, 255, 255), 40);
  fill_rrect(x + 110, y + 76, (int)((w - 150) * clampf(k, 0, 1)), 10, 5, C_ACC, 255);
}

static void fm_paste(void) {
  if (!clip_path[0]) return;
  if (!writable(fm_path)) { set_msg(_("Qui non si possono incollare file"), 1); return; }
  if (SDL_AtomicGet(&copy_state) == 1) { set_msg(_("C'è già una copia in corso"), 1); return; }
  const char *base = strrchr(clip_path, '/'); base = base ? base + 1 : clip_path;
  CopyJob *j = calloc(1, sizeof *j);
  snprintf(j->src, sizeof j->src, "%s", clip_path); snprintf(j->dst, sizeof j->dst, "%s/%s", fm_path, base); j->cut = clip_cut;
  if (!strcmp(j->src, j->dst) || !strncmp(j->dst, j->src, strlen(j->src)) ) { free(j); set_msg(_("Scegli un'altra cartella"), 1); return; }
  snprintf(copy_label, sizeof copy_label, clip_cut ? _("Sposto %s") : _("Copio %s"), base);
  SDL_AtomicSet(&copy_state, 1);
  SDL_Thread *t = SDL_CreateThread(copy_thread, "copy", j);
  if (t) SDL_DetachThread(t); else { free(j); SDL_AtomicSet(&copy_state, 0); }
  if (clip_cut) clip_path[0] = 0;
}

static char fm_target[900];
static void fm_delete_yes(int idx, void *ud) {
  (void)ud; if (idx != 0) return;
  set_msg(remove_tree(fm_target) == 0 ? _("Eliminato") : _("Eliminazione non riuscita"), 0);
  fm_load();
}
static void fm_opts(int idx, void *ud) {
  (void)ud;
  FEnt *f = nfe ? &fe[fm_sel] : NULL;
  int can = writable(fm_path);
  switch (idx) {
    case 0: if (f) { snprintf(fm_target, sizeof fm_target, "%s/%s", fm_path, f->name); snprintf(clip_path, sizeof clip_path, "%s", fm_target); clip_cut = 0; set_msg(_("Copiato: scegli la cartella e premi \xE2\x96\xB3"), 0); } break;
    case 1: if (f && can) { snprintf(clip_path, sizeof clip_path, "%s/%s", fm_path, f->name); clip_cut = 1; set_msg(_("Da spostare: scegli la cartella e premi \xE2\x96\xB3"), 0); } else set_msg(_("Qui non si possono spostare file"), 1); break;
    case 2:
      if (!f || !can) { set_msg(_("Qui non si possono rinominare file"), 1); break; }
      { char nn[256]; snprintf(nn, sizeof nn, "%s", f->name);
        if (edit_text(_("Nuovo nome"), nn, sizeof nn, 0) && nn[0] && !strchr(nn, '/')) {
          char a[900], b[900]; snprintf(a, sizeof a, "%s/%s", fm_path, f->name); snprintf(b, sizeof b, "%s/%s", fm_path, nn);
          set_msg(rename(a, b) == 0 ? _("Rinominato") : _("Impossibile rinominare"), 0); fm_load();
        } }
      break;
    case 3:
      if (!can) { set_msg(_("Qui non si possono creare cartelle"), 1); break; }
      { char nn[256] = ""; if (edit_text(_("Nome della cartella"), nn, sizeof nn, 0) && nn[0] && !strchr(nn, '/')) { char p[900]; snprintf(p, sizeof p, "%s/%s", fm_path, nn); set_msg(mkdir(p, 0777) == 0 ? _("Cartella creata") : _("Impossibile creare la cartella"), 0); fm_load(); } }
      break;
    case 4:
      if (!f || !can) { set_msg(_("Qui non si possono eliminare file"), 1); break; }
      snprintf(fm_target, sizeof fm_target, "%s/%s", fm_path, f->name);
      confirm_open(_("Eliminare definitivamente questo elemento?"), _("Elimina"), fm_delete_yes, NULL);
      break;
  }
}

void files_draw(float t) {
  int a = (int)(255 * t);
  fill_rect(0, 0, SCREEN_W, SCREEN_H, mix(RGB(10, 12, 20), g_theme_base, 0.12f), t > 0.98f ? 255 : a);
  int x0 = 110, w = SCREEN_W - 220;
  draw_icon(IC_FOLDER, x0 + 22, 74, 44, C_ACC2, a);
  draw_text(font(W_LIGHT, 38), _("File"), x0 + 62, 52, C_WHITE, a, AL_L);
  const char *shown = fm_path + strlen(OMEGA_SYSROOT);
  draw_text_fit(font(W_REG, 26), shown[0] ? shown : "/", x0 + 200, 62, w - 520, C_DIM, a, AL_L);
  if (!writable(fm_path)) draw_text(font(W_MED, 22), _("Sola lettura"), x0 + w, 66, C_WARN, a, AL_R);
  int top = 150, rh = 70, bottom = SCREEN_H - 150, lh = bottom - top;
  fm_anim = approach(fm_anim, (float)fm_sel, 20.0f);
  float target = fm_sel * rh + rh > lh ? (float)(fm_sel * rh + rh - lh + rh) : 0;
  fm_scroll = approach(fm_scroll, target, 14.0f);
  SDL_Rect clip = { 0, top - 6, SCREEN_W, lh + 6 }; SDL_RenderSetClipRect(R, &clip);
  for (int i = 0; i < nfe; i++) {
    int y = top + i * rh - (int)fm_scroll;
    if (y + rh < top - 10 || y > bottom) continue;
    float fa = clampf(1 - fabsf(fm_anim - i), 0, 1);
    if (fa > 0.01f) { fill_rrect(x0, y, w, rh - 8, 14, C_WHITE, (int)(a * 0.10f * fa)); stroke_rrect(x0 - 3, y - 3, w + 6, rh - 2, 17, 3, C_WHITE, (int)(a * fa)); }
    draw_icon(fe[i].dir ? IC_FOLDER : IC_NEWS, x0 + 40, y + (rh - 8) / 2, 30, fe[i].dir ? C_ACC2 : C_DIM, a);
    draw_text_fit(font(W_REG, 26), fe[i].name, x0 + 80, y + 14, w - 340, C_TXT, a, AL_L);
    if (!fe[i].dir) { char s[32]; fmt_size((double)fe[i].size, s, sizeof s); if (fe[i].size < 1000000) snprintf(s, sizeof s, _("%lld KB"), (fe[i].size + 1023) / 1024); draw_text(font(W_REG, 22), s, x0 + w - 30, y + 18, C_FAINT, a, AL_R); }
  }
  if (!nfe) draw_text(font(W_REG, 26), _("Cartella vuota o non leggibile"), SCREEN_W / 2, top + 80, C_FAINT, a, AL_C);
  SDL_RenderSetClipRect(R, NULL);
  int ic[5]; const char *lb[5]; int n = 0;
  ic[n] = IC_BTN_X; lb[n++] = nfe && fe[fm_sel].dir ? _("Apri") : _("Seleziona");
  ic[n] = IC_BTN_SQ; lb[n++] = _("Opzioni");
  if (clip_path[0]) { ic[n] = IC_BTN_TRI; lb[n++] = clip_cut ? _("Sposta qui") : _("Incolla qui"); }
  ic[n] = IC_BTN_O; lb[n++] = _("Su di un livello");
  hints(ic, lb, n, a);
}

void files_input(int b) {
  if (b == B_O) {
    char *sl = strrchr(fm_path, '/');
    if (!sl || (int)(sl - fm_path) < (int)strlen(OMEGA_SYSROOT) || fm_path[strlen(OMEGA_SYSROOT)] == 0) { ov_pop(); return; }
    char was[256]; snprintf(was, sizeof was, "%s", sl + 1);
    if (sl == fm_path + strlen(OMEGA_SYSROOT)) sl[1] = 0; else *sl = 0;
    fm_load();
    fm_sel = 0; for (int i = 0; i < nfe; i++) if (!strcmp(fe[i].name, was)) { fm_sel = i; break; }
    sfx_play(SFX_BACK);
    return;
  }
  if (b == B_UP && fm_sel > 0) { fm_sel--; sfx_play(SFX_MOVE); }
  else if (b == B_DOWN && fm_sel < nfe - 1) { fm_sel++; sfx_play(SFX_MOVE); }
  else if (b == B_L1) { fm_sel = fm_sel > 10 ? fm_sel - 10 : 0; }
  else if (b == B_R1) { fm_sel = fm_sel + 10 < nfe ? fm_sel + 10 : (nfe ? nfe - 1 : 0); }
  else if (b == B_X && nfe) {
    if (fe[fm_sel].dir) {
      size_t l = strlen(fm_path);
      snprintf(fm_path + l, sizeof fm_path - l, "%s%s", fm_path[l - 1] == '/' ? "" : "/", fe[fm_sel].name);
      fm_sel = 0; fm_scroll = 0; fm_load(); sfx_play(SFX_OPEN);
    } else {
      size_t L = strlen(fe[fm_sel].name);
      if (L > 4 && !strcasecmp(fe[fm_sel].name + L - 4, ".pkg")) {
        // un .pkg: si installa da qui, con la tessera che avanza nella home
        InstallReq r; memset(&r, 0, sizeof r); r.kind = 1;
        snprintf(r.url, sizeof r.url, "file://%s/%s", fm_path, fe[fm_sel].name);
        char t[96] = ""; char path[900]; snprintf(path, sizeof path, "%s/%s", fm_path, fe[fm_sel].name);
        char ic[320]; snprintf(ic, sizeof ic, OMEGA_DIR "/dl/icons/%08x.png", fnv1a(path));
        pkg_info(path, t, sizeof t, NULL, 0, ic);
        snprintf(r.name, sizeof r.name, "%.*s", (int)(t[0] ? strlen(t) : L - 4), t[0] ? t : fe[fm_sel].name);
        if (access(ic, 0) == 0) snprintf(r.icon, sizeof r.icon, "%s", ic);
        install_begin(&r);
      } else fm_opts(0, NULL);
    }
  }
  else if (b == B_TRI) fm_paste();
  else if (b == B_SQ) {
    const char *it[5] = { _("Copia"), _("Sposta"), _("Rinomina"), _("Nuova cartella"), _("Elimina") };
    menu_open(nfe ? fe[fm_sel].name : _("File"), it, 5, fm_opts, NULL);
  }
}
