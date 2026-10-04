// Omega UI — installazione degli homebrew dello Store, secondo il tipo di file:
//  · .zip con <Nome>/homebrew.js o eboot.elf (formato websrv) → /data/homebrew;
//  · .zip con homebrew.js in radice → /data/homebrew/<Nome>;
//  · .elf → Payload Manager, con il .elf.json accanto;
//  · .zip di un'app con Title ID → /user/app/<TID> e registrazione del titolo;
//  · .pkg → sceAppInstUtilInstallByPackage: la console lo scarica da sé;
//  · cartella di un gioco (caricata dal telefono o dal PC, file:///data/...) →
//    /user/app/<TID> e registrazione del titolo.
// I file caricati in La mia libreria (url file://) si usano dove sono, senza scaricarli.
// Le prime tre scrivono solo file; pkg e registrazione dei titoli richiedono il
// privilegio ShellCore, e senza si indica un ripiego (ItemzFlow).
// Sul desktop si lavora nel finto filesystem OMEGA_SYSROOT (serve minizip).
#include "app.h"
#include <fcntl.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <dirent.h>

#if defined(PS5) || defined(OMEGA_HAVE_MINIZIP)
#define HAVE_ZIP 1
#include <minizip/unzip.h>
#endif

#ifdef PS5
#include <ps5/kernel.h>
// layout come nel riferimento dell'SDK
typedef struct { const char *uri, *ex_uri, *playgo_scenario_id, *content_id, *content_name, *icon_url; } pkg_metadata_t;
typedef struct { char content_id[48]; int type; int platform; } pkg_info_t;
typedef struct { char lang[8][30]; char scenario_ids[3][64]; char content_ids[64]; long unknown[810]; } playgo_info_t;
int sceAppInstUtilInitialize(void);
int sceAppInstUtilInstallByPackage(const pkg_metadata_t *, pkg_info_t *, playgo_info_t *);
int sceAppInstUtilAppInstallAll(void *);
int sceAppInstUtilAppUnInstall(const char *);
#endif
#define APP_DIR   OMEGA_SYSROOT "/user/app"
#define ZIP_CHUNK (256 * 1024)
#define CANCELLED (-102)       // omega_url_download: interrotto con *cancel

// -------------------------------------------------------------------- stato --
enum { KIND_AUTO = 0, KIND_PKG = 1, KIND_ZIP = 2, KIND_ELF = 3, KIND_FOLDER = 4 };

static SDL_atomic_t g_state;          // 0 fermo, 1 in corso, 2 finito
static volatile long g_done, g_total; // byte scaricati / totali
static volatile int g_cancel;
static char g_label[96], g_phase[128], g_result[512];
static volatile int g_dl;           // fase di scaricamento: avanzamento in MB
static int g_result_err, g_installed_title;
static Uint32 g_started;

int install_busy(void) { return SDL_AtomicGet(&g_state) == 1; }

int store_uninstall(const char *title_id) {
#ifdef PS5
  if (!title_id || !title_id[0]) return -1;
  sceAppInstUtilInitialize();
  return sceAppInstUtilAppUnInstall(title_id);
#else
  (void)title_id; return 0;
#endif
}

// ------------------------------------------------------------------ utilità --
// crea le cartelle che contengono path (mkdir -p della directory)
static void mkparents(const char *path) {
  char tmp[700]; snprintf(tmp, sizeof tmp, "%s", path);
  for (char *p = tmp + 1; *p; p++) if (*p == '/') { *p = 0; mkdir(tmp, 0755); *p = '/'; }
}
static void mkdirs(const char *dir) { char t[700]; snprintf(t, sizeof t, "%s/", dir); mkparents(t); }

// nome di cartella sicuro da un titolo: lettere, cifre, - e _
static void safe_name(const char *src, char *out, size_t n) {
  size_t o = 0;
  for (const unsigned char *p = (const unsigned char *)src; *p && o + 1 < n; p++)
    if ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9') || *p == '-' || *p == '_') out[o++] = (char)*p;
  out[o] = 0;
  if (!o) snprintf(out, n, "Homebrew");
}

// ultimo segmento del percorso dell'URL, senza query
static void url_basename(const char *url, char *out, size_t n) {
  const char *q = strpbrk(url, "?#"); size_t L = q ? (size_t)(q - url) : strlen(url);
  const char *s = url + L; while (s > url && s[-1] != '/') s--;
  size_t k = (size_t)(url + L - s); if (k >= n) k = n - 1;
  memcpy(out, s, k); out[k] = 0;
  for (char *c = out; *c; c++) if (*c == '/' || *c == '\\') *c = '_';
  if (!out[0]) snprintf(out, n, "payload.elf");
}

static int ends_with(const char *s, const char *suf) { size_t a = strlen(s), b = strlen(suf); return a >= b && !strcasecmp(s + a - b, suf); }

static void json_str_field(FILE *f, const char *key, const char *val, int last) {
  char esc[700]; json_escape(esc, sizeof esc, val ? val : "");
  fprintf(f, "  \"%s\": \"%s\"%s\n", key, esc, last ? "" : ",");
}

// ---------------------------------------------------------------------- zip --
#ifdef HAVE_ZIP
// percorsi assoluti e ".." si saltano (zip-slip)
static int unsafe_entry(const char *name) {
  if (!name[0] || name[0] == '/' || name[0] == '\\') return 1;
  return strstr(name, "..") != NULL;
}

typedef struct { char top[256]; int hb_top, hb_root, files; } ZipInfo;

// cartella comune di primo livello e posizione di homebrew.js / eboot.elf
static int zip_scan(const char *zip, ZipInfo *zi) {
  memset(zi, 0, sizeof *zi);
  unzFile uf = unzOpen64(zip); if (!uf) return -1;
  int first = 1, common = 1;
  if (unzGoToFirstFile(uf) == UNZ_OK) do {
    unz_file_info64 fi; char nm[512];
    if (unzGetCurrentFileInfo64(uf, &fi, nm, sizeof nm, NULL, 0, NULL, 0) != UNZ_OK) continue;
    zi->files++;
    const char *slash = strchr(nm, '/');
    if (!slash) common = 0;
    else if (first) { snprintf(zi->top, sizeof zi->top, "%.*s", (int)(slash - nm) + 1, nm); first = 0; }
    else if (strncmp(nm, zi->top, strlen(zi->top))) common = 0;
    if (!strcmp(nm, "homebrew.js") || !strcmp(nm, "eboot.elf")) zi->hb_root = 1;
    if (slash && strchr(slash + 1, '/') == NULL && (!strcmp(slash + 1, "homebrew.js") || !strcmp(slash + 1, "eboot.elf"))) zi->hb_top = 1;
  } while (unzGoToNextFile(uf) == UNZ_OK);
  unzClose(uf);
  if (!common) { zi->top[0] = 0; zi->hb_top = 0; }
  return zi->files ? 0 : -1;
}

// estrae in dest togliendo il prefisso strip dai nomi
static int unzip_to(const char *zip, const char *dest, const char *strip) {
  unzFile uf = unzOpen64(zip); if (!uf) return -1;
  size_t striplen = strip ? strlen(strip) : 0;
  int rc = 0, files = 0;
  char *buf = malloc(ZIP_CHUNK); if (!buf) { unzClose(uf); return -1; }
  if (unzGoToFirstFile(uf) == UNZ_OK) do {
    unz_file_info64 fi; char nm[512];
    if (unzGetCurrentFileInfo64(uf, &fi, nm, sizeof nm, NULL, 0, NULL, 0) != UNZ_OK) { rc = -1; break; }
    if (unsafe_entry(nm)) continue;
    const char *rel = (striplen && !strncmp(nm, strip, striplen)) ? nm + striplen : nm;
    if (!rel[0]) continue;
    char out[900]; snprintf(out, sizeof out, "%s/%s", dest, rel);
    size_t L = strlen(out);
    if (L && out[L - 1] == '/') { mkparents(out); continue; }
    mkparents(out);
    if (unzOpenCurrentFile(uf) != UNZ_OK) { rc = -1; break; }
    FILE *f = fopen(out, "wb");
    if (!f) { unzCloseCurrentFile(uf); rc = -1; break; }
    int k;
    while ((k = unzReadCurrentFile(uf, buf, ZIP_CHUNK)) > 0) if (fwrite(buf, 1, (size_t)k, f) != (size_t)k) { k = -1; break; }
    fclose(f); unzCloseCurrentFile(uf);
    if (k < 0) { rc = -1; break; }
    // gli eseguibili devono restare tali (anche i file senza estensione)
    if (ends_with(out, ".elf") || ends_with(out, ".so") || !strchr(rel, '.')) chmod(out, 0755);
    files++;
    snprintf(g_phase, sizeof g_phase, _("Estrazione (%d file)"), files);
  } while (unzGoToNextFile(uf) == UNZ_OK);
  free(buf); unzClose(uf);
  return rc ? rc : (files ? 0 : -1);
}
#endif

#ifdef PS5
// Registra un titolo già estratto su disco. sceAppInstUtilAppInstallTitleDir non
// è esportata dall'SDK: si risolve per NID, come fa ftpsrv.
static int reg_app(const char *title_id, const char *dir) {
  int (*AppInstallTitleDir)(const char *, const char *, void *) = 0;
  uint32_t handle;
  if (!kernel_dynlib_handle(-1, "libSceAppInstUtil.sprx", &handle))
    AppInstallTitleDir = (void *)kernel_dynlib_resolve(-1, handle, "Wudg3Xe3heE");
  if (AppInstallTitleDir) return AppInstallTitleDir(title_id, dir, 0);
  return sceAppInstUtilAppInstallAll(0);
}
#endif

// ------------------------------------------------------ riconoscimento tipo --
// file già sulla console (caricato da La mia libreria): file:///data/...
static const char *local_path(const char *url) { return !strncmp(url, "file://", 7) ? url + 7 : NULL; }

static int detect_kind(int kind, const char *url) {
  if (kind == KIND_PKG || kind == KIND_ZIP || kind == KIND_ELF || kind == KIND_FOLDER) return kind;
  if (local_path(url)) { struct stat st; if (stat(local_path(url), &st) == 0 && S_ISDIR(st.st_mode)) return KIND_FOLDER; }
  const char *q = strpbrk(url, "?#"); size_t L = q ? (size_t)(q - url) : strlen(url);
  if (L >= 4 && !strncasecmp(url + L - 4, ".pkg", 4)) return KIND_PKG;
  if (L >= 4 && !strncasecmp(url + L - 4, ".zip", 4)) return KIND_ZIP;
  if (L >= 4 && !strncasecmp(url + L - 4, ".elf", 4)) return KIND_ELF;
  unsigned char m[8] = { 0 };
  long got = -1;
  if (local_path(url)) { int fd = open(local_path(url), O_RDONLY); if (fd >= 0) { got = read(fd, m, sizeof m); close(fd); } }
  else got = omega_url_peek(url, m, sizeof m);
  if (got >= 4) {
    if (m[0] == 'P' && m[1] == 'K' && m[2] == 3 && m[3] == 4) return KIND_ZIP;
    if (m[0] == 0x7F && m[1] == 'E' && m[2] == 'L' && m[3] == 'F') return KIND_ELF;
    if (m[0] == 0x7F && m[1] == 'C' && m[2] == 'N' && m[3] == 'T') return KIND_PKG;   // pkg PS4/PS5
  }
  return KIND_PKG;
}

// ------------------------------------------------------------------- lavori --
static int copy_file(const char *src, const char *dst) {
  int in = open(src, O_RDONLY); if (in < 0) return -1;
  int out = open(dst, O_WRONLY | O_CREAT | O_TRUNC, 0666); if (out < 0) { close(in); return -1; }
  struct stat st; if (fstat(in, &st) == 0) g_total = (long)st.st_size;
  static char buf[ZIP_CHUNK]; ssize_t k; int rc = 0;
  while ((k = read(in, buf, sizeof buf)) > 0) {
    if (g_cancel || write(out, buf, (size_t)k) != k) { rc = -1; break; }
    g_done += (long)k;
  }
  if (k < 0) rc = -1;
  close(in); close(out);
  if (rc) unlink(dst);
  return rc;
}
// cartella intera (quando non si può spostare: un'altra partizione)
static int copy_tree(const char *src, const char *dst) {
  struct stat st; if (stat(src, &st) != 0) return -1;
  if (!S_ISDIR(st.st_mode)) return copy_file(src, dst);
  mkdir(dst, 0777);
  DIR *d = opendir(src); if (!d) return -1;
  struct dirent *e; int rc = 0;
  while (!rc && (e = readdir(d))) {
    if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
    if (g_cancel) { rc = -1; break; }
    char a[1024], b[1024]; snprintf(a, sizeof a, "%s/%s", src, e->d_name); snprintf(b, sizeof b, "%s/%s", dst, e->d_name);
    rc = copy_tree(a, b);
  }
  closedir(d);
  return rc;
}

static int download(const InstallReq *j, const char *dest) {
  mkparents(dest);
  if (local_path(j->url)) {   // già sulla console: basta copiarlo
    snprintf(g_phase, sizeof g_phase, "%s", _("Copia sulla console")); g_dl = 1;
    if (copy_file(local_path(j->url), dest) == 0) return 0;
    g_dl = 0;
    snprintf(g_result, sizeof g_result, "%s", g_cancel ? _("Installazione annullata") : _("Copia non riuscita"));
    return g_cancel ? -2 : -1;
  }
  snprintf(g_phase, sizeof g_phase, "%s", _("Scaricamento")); g_dl = 1;
  int st = omega_url_download(j->url, dest, &g_done, &g_total, &g_cancel);
  if (st == 200) return 0;
  unlink(dest);
  g_dl = 0;
  if (st == CANCELLED) snprintf(g_result, sizeof g_result, "%s", _("Installazione annullata"));
  else snprintf(g_result, sizeof g_result, _("Download non riuscito (%d)"), st);
  return st == CANCELLED ? -2 : -1;
}

static int do_pkg(const InstallReq *j) {
#ifdef PS5
  if (sceAppInstUtilInitialize()) { snprintf(g_result, sizeof g_result, "%s", _("AppInst non disponibile (privilegio mancante). Ripiego: installa il pkg con ItemzFlow.")); return -1; }
  const char *uri = local_path(j->url) ? local_path(j->url) : j->url;   // un pkg caricato si installa dal disco
  pkg_metadata_t meta = { .uri = uri, .ex_uri = "", .playgo_scenario_id = "", .content_id = "", .content_name = j->name[0] ? j->name : "", .icon_url = "" };
  pkg_info_t info; memset(&info, 0, sizeof info);
  playgo_info_t pg; memset(&pg, 0, sizeof pg);
  int rc = sceAppInstUtilInstallByPackage(&meta, &info, &pg);
  if (rc) { snprintf(g_result, sizeof g_result, _("Installazione pkg non riuscita (0x%08X). Ripiego: ItemzFlow."), (unsigned)rc); return -1; }
  snprintf(g_result, sizeof g_result, "%s", _("Installazione avviata: comparirà nella Home della console"));
  return 0;
#else
  for (int i = 0; i <= 100 && !g_cancel; i += 5) { g_total = 100; g_done = i; SDL_Delay(30); }
  snprintf(g_result, sizeof g_result, "(desktop) pkg: sulla console va ad AppInstUtil — %s", j->name);
  return 0;
#endif
}

// .elf → nella cartella del caricatore rilevato (hen.c): OnionHEN / etaHEN 2
// in una cartella piatta, altrimenti Payload Manager con il suo .elf.json.
// L'avvio automatico non si attiva da solo: lo decide l'utente nel caricatore.
static int do_elf(const InstallReq *j) {
  char name[64], file[160], dir[400], path[600], tmp[620], side[640], sha[65];
  safe_name(j->name, name, sizeof name);
  // il nome vero del file: l'URL finale di GitHub è un link firmato senza nome
  if (j->filename[0]) url_basename(j->filename, file, sizeof file); else url_basename(j->url, file, sizeof file);
  if (!ends_with(file, ".elf")) { size_t L = strlen(file); snprintf(file + L, sizeof file - L, ".elf"); }
  char flat[300];
  if (hen_payload_dir(flat, sizeof flat)) {
    snprintf(path, sizeof path, "%s/%s", flat, file);
    snprintf(tmp, sizeof tmp, "%s.part", path);
    mkdirs(flat);
    int rc = download(j, tmp); if (rc) return rc;
    if (rename(tmp, path) != 0) { unlink(tmp); snprintf(g_result, sizeof g_result, "%s", _("Impossibile salvare il payload")); return -1; }
    chmod(path, 0755);
    snprintf(side, sizeof side, "%s.omega.json", path);   // solo il nome da mostrare in home
    FILE *f = fopen(side, "w"); if (f) { fprintf(f, "{\n"); json_str_field(f, "name", j->name, 1); fprintf(f, "}\n"); fclose(f); }
    snprintf(g_result, sizeof g_result, _("%s installato per %s: lo trovi in home e tra i payload del caricatore"), j->name, hen_name());
    return 0;
  }
  snprintf(dir, sizeof dir, OMEGA_PLD_ROOT "/%s", name);
  snprintf(path, sizeof path, "%s/%s", dir, file);
  snprintf(tmp, sizeof tmp, "%s.part", path);
  mkdirs(dir);
  int rc = download(j, tmp); if (rc) return rc;
  if (rename(tmp, path) != 0) { unlink(tmp); snprintf(g_result, sizeof g_result, "%s", _("Impossibile salvare il payload")); return -1; }
  chmod(path, 0755);
  snprintf(g_phase, sizeof g_phase, "%s", _("Verifica"));
  if (file_sha256(path, sha)) sha[0] = 0;
  snprintf(side, sizeof side, "%s.json", path);
  FILE *f = fopen(side, "w");
  if (f) {
    char when[32]; time_t t = time(NULL); strftime(when, sizeof when, "%Y-%m-%dT%H:%M:%SZ", gmtime(&t));
    fprintf(f, "{\n");
    json_str_field(f, "name", j->name, 0);
    json_str_field(f, "filename", file, 0);
    json_str_field(f, "url", j->url, 0);
    json_str_field(f, "source", j->source[0] ? j->source : j->url, 0);
    json_str_field(f, "description", j->desc, 0);
    json_str_field(f, "version", j->version, 0);
    json_str_field(f, "checksum", sha, 0);
    json_str_field(f, "category", j->category[0] ? j->category : "Utilities", 0);
    json_str_field(f, "downloaded_at", when, 0);
    json_str_field(f, "install_source", "Omega Store", 1);
    fprintf(f, "}\n");
    fclose(f);
  }
  if (ends_with(file, "-install.elf"))
    snprintf(g_result, sizeof g_result, _("%s è in Payload Manager: avvialo una volta per completare l'installazione"), j->name);
  else
    snprintf(g_result, sizeof g_result, _("%s installato in Payload Manager"), j->name);
  return 0;
}

static int do_zip(const InstallReq *j) {
#ifdef HAVE_ZIP
  char zip[1100]; const char *lp = local_path(j->url);
  int rc = 0;
  if (lp) snprintf(zip, sizeof zip, "%s", lp);
  else { snprintf(zip, sizeof zip, OMEGA_DIR "/dl/omega-install.zip"); rc = download(j, zip); if (rc) return rc; }
  snprintf(g_phase, sizeof g_phase, "%s", _("Analisi"));
  ZipInfo zi;
  if (zip_scan(zip, &zi)) { if (!lp) unlink(zip); snprintf(g_result, sizeof g_result, "%s", _("Lo zip è vuoto o danneggiato")); return -1; }
  if (zi.hb_top || zi.hb_root) {
    char dest[400], folder[256];
    if (zi.hb_top) { snprintf(folder, sizeof folder, "%.*s", (int)strlen(zi.top) - 1, zi.top); snprintf(dest, sizeof dest, "%s", OMEGA_HB_ROOT); }
    else {
      // homebrew.js in radice: la cartella prende il nome dello zip, come nelle
      // istruzioni di websrv (YQuake2.zip → /data/homebrew/YQuake2), perché
      // alcuni script cercano i propri file per percorso. Nomi con versioni
      // ("pnes-7.1_ps5") o vuoti → il titolo dello Store.
      char zn[160]; url_basename(j->filename[0] ? j->filename : j->url, zn, sizeof zn);
      size_t L = strlen(zn); if (ends_with(zn, ".zip")) zn[L - 4] = 0;
      if (zn[0] && !strchr(zn, '.') && !strchr(zn, '%')) safe_name(zn, folder, sizeof folder);
      else safe_name(j->name, folder, sizeof folder);
      snprintf(dest, sizeof dest, OMEGA_HB_ROOT "/%s", folder);
    }
    mkdirs(dest);
    rc = unzip_to(zip, dest, "");
    if (!lp) unlink(zip);
    if (rc) { snprintf(g_result, sizeof g_result, "%s", _("Estrazione dello zip non riuscita")); return -1; }
    omega_log("install: homebrew %s → %s/%s", j->name, OMEGA_HB_ROOT, folder);
    snprintf(g_result, sizeof g_result, _("%s installato: lo trovi nella Home di Omega"), j->name);
    g_installed_title = 1;
    return 0;
  }
  if (!j->title_id[0]) { if (!lp) unlink(zip); snprintf(g_result, sizeof g_result, "%s", _("Zip non riconosciuto: manca homebrew.js/eboot.elf e non c'è un Title ID")); return -1; }
  char dest[200]; snprintf(dest, sizeof dest, APP_DIR "/%s", j->title_id);
# ifdef PS5
  sceAppInstUtilInitialize();
  sceAppInstUtilAppUnInstall(j->title_id);     // reinstallazione
# endif
  mkdirs(dest);
  rc = unzip_to(zip, dest, zi.top);
  if (!lp) unlink(zip);
  if (rc) { snprintf(g_result, sizeof g_result, "%s", _("Estrazione dello zip non riuscita")); return -1; }
# ifdef PS5
  snprintf(g_phase, sizeof g_phase, "%s", _("Registrazione"));
  rc = reg_app(j->title_id, "/user/app/");
  if (rc) { snprintf(g_result, sizeof g_result, _("Registrazione non riuscita (0x%08X). Privilegio ShellCore mancante?"), (unsigned)rc); return -1; }
  g_installed_title = 1;
# endif
  snprintf(g_result, sizeof g_result, _("%s installato"), j->name[0] ? j->name : j->title_id);
  return 0;
#else
  (void)j;
  for (int i = 0; i <= 100 && !g_cancel; i += 4) { g_total = 100; g_done = i; SDL_Delay(25); }
  snprintf(g_result, sizeof g_result, "(desktop senza minizip) installazione zip simulata");
  return 0;
#endif
}

// cartella di un gioco caricata in La mia libreria → /user/app/<TID> e registrazione
static int do_folder(const InstallReq *j) {
  const char *src = local_path(j->url);
  if (!src || strlen(j->title_id) != 9) { snprintf(g_result, sizeof g_result, "%s", _("Cartella non valida: manca il Title ID")); return -1; }
  char dest[200]; snprintf(dest, sizeof dest, APP_DIR "/%s", j->title_id);
# ifdef PS5
  sceAppInstUtilInitialize();
  sceAppInstUtilAppUnInstall(j->title_id);     // reinstallazione
# endif
  snprintf(g_phase, sizeof g_phase, "%s", _("Copia sulla console"));
  mkdirs(APP_DIR);
  // stessa partizione: si sposta e basta; altrimenti si copia
  if (rename(src, dest) != 0 && copy_tree(src, dest) != 0) {
    snprintf(g_result, sizeof g_result, "%s", g_cancel ? _("Installazione annullata") : _("Copia non riuscita"));
    return g_cancel ? -2 : -1;
  }
# ifdef PS5
  snprintf(g_phase, sizeof g_phase, "%s", _("Registrazione"));
  int rc = reg_app(j->title_id, "/user/app/");
  if (rc) { snprintf(g_result, sizeof g_result, _("Registrazione non riuscita (0x%08X). Privilegio ShellCore mancante?"), (unsigned)rc); return -1; }
  g_installed_title = 1;
# endif
  omega_log("install: cartella %s → %s", src, dest);
  snprintf(g_result, sizeof g_result, _("%s installato"), j->name[0] ? j->name : j->title_id);
  return 0;
}

static int install_thread(void *arg) {
  InstallReq *j = arg;
  g_done = g_total = 0; g_cancel = 0; g_dl = 0; g_result[0] = 0; g_result_err = 0; g_installed_title = 0;
  snprintf(g_phase, sizeof g_phase, "%s", _("Preparazione"));
  mkdir(OMEGA_DIR, 0777);
  int kind = detect_kind(j->kind, j->url);
  omega_log("install: %s kind=%d %s", j->name, kind, j->url);
  int rc = kind == KIND_ZIP ? do_zip(j) : kind == KIND_ELF ? do_elf(j) : kind == KIND_FOLDER ? do_folder(j) : do_pkg(j);
  g_result_err = rc == -1;
  omega_log("install: %s -> %s", j->name, g_result);
  free(j);
  SDL_AtomicSet(&g_state, 2);
  return 0;
}

void install_begin(const InstallReq *r) {
  if (SDL_AtomicGet(&g_state) == 1) { set_msg(_("C'è già un'installazione in corso"), 1); return; }
  if (!r || !r->url[0]) { set_msg(_("Link di download mancante"), 1); return; }
  InstallReq *j = malloc(sizeof *j);
  if (!j) { set_msg(_("Memoria insufficiente"), 1); return; }
  *j = *r;
  snprintf(g_label, sizeof g_label, "%s", r->name[0] ? r->name : _("Installazione"));
  g_started = SDL_GetTicks();
  SDL_AtomicSet(&g_state, 1);
  SDL_Thread *t = SDL_CreateThread(install_thread, "install", j);
  if (t) SDL_DetachThread(t);
  else { free(j); SDL_AtomicSet(&g_state, 0); set_msg(_("Impossibile avviare l'installazione"), 1); }
}

// dal ciclo principale: chiude l'installazione quando il thread ha finito
void install_tick(void) {
  if (SDL_AtomicGet(&g_state) != 2) return;
  SDL_AtomicSet(&g_state, 0);
  set_msg(g_result, g_result_err);
  if (g_installed_title) {
    toast(IC_DOWNLOAD, NULL, 0, g_label, _("Installato: ora è nella tua Home"));
    scan_apps();
  } else if (!g_result_err) {
    toast(IC_DOWNLOAD, NULL, 0, g_label, g_result);
  }
}

void install_overlay(void) {
  if (SDL_AtomicGet(&g_state) != 1) return;
  int w = 600, h = 132, x = SCREEN_W - w - 40, y = SCREEN_H - h - 110;
  shadow_rrect(x, y, w, h, 24, 22, 150);
  fill_rrect(x, y, w, h, 24, RGB(28, 32, 46), 245);
  draw_icon(IC_DOWNLOAD, x + 60, y + h / 2, 44, C_ACC2, 255);
  draw_text_fit(font(W_MED, 28), g_label, x + 110, y + 24, w - 150, C_TXT, 255, AL_L);
  long done = g_done, total = g_total;
  char sub[256];
  if (total > 0 && done >= 0 && g_dl) {
    snprintf(sub, sizeof sub, _("%s  ·  %.1f / %.1f MB"), g_phase, done / 1048576.0, total / 1048576.0);
  } else {
    Uint32 s = (SDL_GetTicks() - g_started) / 1000;
    snprintf(sub, sizeof sub, "%s...  %us", g_phase[0] ? g_phase : _("In corso"), s);
  }
  draw_text_fit(font(W_REG, 22), sub, x + 110, y + 62, w - 150, C_DIM, 255, AL_L);
  int bx = x + 110, bw = w - 150, by = y + h - 30;
  fill_rrect(bx, by, bw, 8, 4, RGB(255, 255, 255), 30);
  if (total > 0 && g_dl) fill_rrect(bx, by, (int)(bw * clampf((float)done / (float)total, 0, 1)), 8, 4, C_ACC2, 230);
  else { int iw = bw / 3, ix = (int)(fmodf((float)g_time * 400, (float)(bw + iw)) - iw); fill_rrect(bx + (ix < 0 ? 0 : ix), by, ix < 0 ? iw + ix : iw, 8, 4, C_ACC2, 230); }
}
