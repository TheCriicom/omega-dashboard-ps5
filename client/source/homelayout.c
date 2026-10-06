// Omega UI — disposizione della home: app nascoste e cartelle.
//
// Tutto sta sulla console, in OMEGA_DIR/home-layout.txt, una riga per voce:
//   hide <tid>              app tolta dalla home (si riattiva in Impostazioni)
//   folder <fid> <nome>     cartella (fid = "FD" + numero)
//   in <tid> <fid>          app dentro una cartella
// Nascondere un'app la toglie dalla sua cartella e viceversa: una voce sola
// per app. Le righe di app non più installate restano: se l'app torna, torna
// dov'era.
//
// layout_apply() riordina apps[]: in testa le tessere della fila (Community,
// cartelle non vuote, app visibili), in coda le app dentro le cartelle e
// quelle nascoste. nrow è il numero di tessere della fila; il resto del
// programma continua a vedere tutte le app installate fino a napps.
#include "app.h"
#include <math.h>
#include <stdlib.h>

#define LAYOUT_FILE OMEGA_DIR "/home-layout.txt"
#define MAX_FOLDERS 32
#define MAX_ITEMS 512

int nrow;

typedef struct { char fid[16]; char name[64]; } Folder;
typedef struct { char tid[16]; char fid[16]; } Item;      // fid vuoto = nascosta
static Folder folders[MAX_FOLDERS]; static int nfolders;
static Item items[MAX_ITEMS]; static int nitems;
static int loaded;

// ------------------------------------------------------------------- file --
static void load(void) {
  if (loaded) return;
  loaded = 1;
  FILE *f = fopen(LAYOUT_FILE, "r");
  if (!f) return;
  char line[160];
  while (fgets(line, sizeof line, f)) {
    line[strcspn(line, "\r\n")] = 0;
    char a[16] = "", b[16] = "";
    if (!strncmp(line, "hide ", 5) && nitems < MAX_ITEMS) {
      snprintf(items[nitems].tid, sizeof items[0].tid, "%s", line + 5); items[nitems].fid[0] = 0; nitems++;
    } else if (!strncmp(line, "folder ", 7) && nfolders < MAX_FOLDERS && sscanf(line + 7, "%15s", a) == 1) {
      const char *name = line + 7 + strlen(a); while (*name == ' ') name++;
      snprintf(folders[nfolders].fid, sizeof folders[0].fid, "%s", a);
      snprintf(folders[nfolders].name, sizeof folders[0].name, "%s", *name ? name : a);
      nfolders++;
    } else if (!strncmp(line, "in ", 3) && nitems < MAX_ITEMS && sscanf(line + 3, "%15s %15s", a, b) == 2) {
      snprintf(items[nitems].tid, sizeof items[0].tid, "%s", a); snprintf(items[nitems].fid, sizeof items[0].fid, "%s", b); nitems++;
    }
  }
  fclose(f);
}

static void save(void) {
  FILE *f = fopen(LAYOUT_FILE ".tmp", "w");
  if (!f) { omega_log("home-layout: scrittura non riuscita"); return; }
  for (int i = 0; i < nfolders; i++) fprintf(f, "folder %s %s\n", folders[i].fid, folders[i].name);
  for (int i = 0; i < nitems; i++) {
    if (items[i].fid[0]) fprintf(f, "in %s %s\n", items[i].tid, items[i].fid);
    else fprintf(f, "hide %s\n", items[i].tid);
  }
  fclose(f);
  rename(LAYOUT_FILE ".tmp", LAYOUT_FILE);
}

static Item *item_of(const char *tid) {
  for (int i = 0; i < nitems; i++) if (!strcmp(items[i].tid, tid)) return &items[i];
  return NULL;
}
static Folder *folder_of_id(const char *fid) {
  for (int i = 0; i < nfolders; i++) if (!strcmp(folders[i].fid, fid)) return &folders[i];
  return NULL;
}
static void item_drop(const char *tid) {
  for (int i = 0; i < nitems; i++) if (!strcmp(items[i].tid, tid)) { items[i] = items[--nitems]; return; }
}
static void item_set(const char *tid, const char *fid) {
  Item *it = item_of(tid);
  if (!it) { if (nitems >= MAX_ITEMS) return; it = &items[nitems++]; snprintf(it->tid, sizeof it->tid, "%s", tid); }
  snprintf(it->fid, sizeof it->fid, "%s", fid ? fid : "");
}

// ---------------------------------------------------------------- consulta --
int layout_is_hidden(const char *tid) { load(); Item *it = item_of(tid); return it && !it->fid[0]; }
const char *layout_folder_of(const char *tid) {
  load(); Item *it = item_of(tid);
  return it && it->fid[0] && folder_of_id(it->fid) ? it->fid : NULL;
}
int layout_folder_count(const char *fid) {
  int n = 0;
  for (int i = nrow; i < napps; i++) { const char *f = layout_folder_of(apps[i].tid); if (f && !strcmp(f, fid)) n++; }
  return n;
}
// i-esima app di una cartella (indice in apps[]), -1 se non c'è
int layout_folder_app(const char *fid, int k) {
  for (int i = nrow; i < napps; i++) { const char *f = layout_folder_of(apps[i].tid); if (f && !strcmp(f, fid) && k-- == 0) return i; }
  return -1;
}
const char *layout_folder_name(const char *fid) { load(); Folder *f = folder_of_id(fid); return f ? f->name : ""; }

// ------------------------------------------------------------------ riordino --
void layout_apply(void) {
  load();
  static AppEntry tmp[MAX_APPS];
  int n = 0;
  // 1) Community e cartelle con almeno un'app installata
  for (int i = 0; i < napps; i++) if (apps[i].builtin == 1) tmp[n++] = apps[i];
  for (int f = 0; f < nfolders && n < MAX_APPS; f++) {
    int has = 0;
    for (int i = 0; i < napps && !has; i++) { const char *fi = apps[i].builtin ? NULL : layout_folder_of(apps[i].tid); has = fi && !strcmp(fi, folders[f].fid); }
    if (!has) continue;
    AppEntry *a = &tmp[n++]; memset(a, 0, sizeof *a);
    a->builtin = 2; a->tex_state = 2; a->avg = g_theme_base;
    snprintf(a->tid, sizeof a->tid, "%s", folders[f].fid);
    snprintf(a->name, sizeof a->name, "%s", folders[f].name);
  }
  // 2) app visibili
  for (int i = 0; i < napps && n < MAX_APPS; i++)
    if (!apps[i].builtin && !layout_is_hidden(apps[i].tid) && !layout_folder_of(apps[i].tid)) tmp[n++] = apps[i];
  int row = n;
  // 3) app nelle cartelle, poi quelle nascoste
  for (int i = 0; i < napps && n < MAX_APPS; i++) if (!apps[i].builtin && layout_folder_of(apps[i].tid)) tmp[n++] = apps[i];
  for (int i = 0; i < napps && n < MAX_APPS; i++) if (!apps[i].builtin && layout_is_hidden(apps[i].tid)) tmp[n++] = apps[i];
  memcpy(apps, tmp, sizeof(AppEntry) * (size_t)n);
  napps = n; nrow = row;
}

// --------------------------------------------------------------- modifiche --
void layout_hide(const char *tid, int hide) {
  load();
  if (hide) item_set(tid, NULL); else item_drop(tid);
  save(); home_relayout(tid);
}

static void move_to(const char *tid, const char *fid) {
  if (fid) item_set(tid, fid); else item_drop(tid);
  save();
}

// Rinomina o crea: chiede il nome con la tastiera. Ritorna 1 se confermato.
static int ask_name(const char *title, char *name, size_t n) {
  return edit_text(title, name, n, 0) && name[strspn(name, " ")];
}

static char pick_tid[16];
static void folder_pick(int idx, void *ud) {
  (void)ud;
  char name[64] = "";
  const char *fid = NULL;
  if (idx >= 0 && idx < nfolders) fid = folders[idx].fid;
  else if (idx == nfolders) {
    if (nfolders >= MAX_FOLDERS) { set_msg(_("Hai raggiunto il numero massimo di cartelle"), 1); return; }
    if (!ask_name(_("Nome della nuova cartella"), name, sizeof name)) return;
    int num = 1;
    for (;; num++) { char t[16]; snprintf(t, sizeof t, "FD%04d", num); if (!folder_of_id(t)) break; }
    Folder *f = &folders[nfolders++];
    snprintf(f->fid, sizeof f->fid, "FD%04d", num);
    snprintf(f->name, sizeof f->name, "%s", name);
    fid = f->fid;
  } else return;
  move_to(pick_tid, fid);
  char m[160]; snprintf(m, sizeof m, _("Spostato in %s"), layout_folder_name(fid));
  set_msg(m, 0);
  home_relayout(NULL);
}

// Menu "Sposta in una cartella" per un'app: cartelle esistenti + nuova.
void layout_move_menu(const char *tid) {
  load();
  static const char *list[MAX_FOLDERS + 1];
  snprintf(pick_tid, sizeof pick_tid, "%s", tid);
  const char *cur = layout_folder_of(tid);
  int n = 0;
  for (int i = 0; i < nfolders; i++) list[n++] = folders[i].name;
  list[n++] = _("Nuova cartella...");
  menu_open(_("Sposta in una cartella"), list, n, folder_pick, NULL);
  if (cur) for (int i = 0; i < nfolders; i++) if (!strcmp(folders[i].fid, cur)) menu_select(i);
}

void layout_unfold(const char *tid) { load(); move_to(tid, NULL); home_relayout(tid); }

static char edit_fid[16];
static void folder_delete_yes(int idx, void *ud) {
  (void)idx; (void)ud;
  for (int i = nitems - 1; i >= 0; i--) if (!strcmp(items[i].fid, edit_fid)) items[i] = items[--nitems];
  for (int i = 0; i < nfolders; i++) if (!strcmp(folders[i].fid, edit_fid)) { folders[i] = folders[--nfolders]; break; }
  save();
  if (ov_top() == OV_FOLDER) ov_pop();
  set_msg(_("Cartella eliminata: le app sono tornate in home"), 0);
  home_relayout(NULL);
}
static void folder_opt_pick(int idx, void *ud) {
  (void)ud;
  Folder *f = folder_of_id(edit_fid);
  if (!f) return;
  if (idx == 0) {
    char name[64]; snprintf(name, sizeof name, "%s", f->name);
    if (!ask_name(_("Nome della cartella"), name, sizeof name)) return;
    snprintf(f->name, sizeof f->name, "%s", name);
    save(); home_relayout(NULL);
  } else if (idx == 1) {
    confirm_open(_("Eliminare la cartella? Le app che contiene tornano in home, non vengono disinstallate."), _("Elimina"), folder_delete_yes, NULL);
  }
}
void layout_folder_menu(const char *fid) {
  load();
  snprintf(edit_fid, sizeof edit_fid, "%s", fid);
  const char *it[] = { _("Rinomina cartella"), _("Elimina cartella") };
  menu_open(layout_folder_name(fid), it, 2, folder_opt_pick, NULL);
}

// ------------------------------------------------- app nascoste (Impostazioni) --
static int hidden_idx[MAX_APPS]; static int nhidden;
static void hidden_pick(int idx, void *ud) {
  (void)ud;
  if (idx < 0 || idx >= nhidden) return;
  char m[160]; snprintf(m, sizeof m, _("%s è di nuovo in home"), apps[hidden_idx[idx]].name);
  char tid[16]; snprintf(tid, sizeof tid, "%s", apps[hidden_idx[idx]].tid);
  layout_hide(tid, 0);
  set_msg(m, 0);
  if (layout_hidden_count()) hidden_menu();       // il menu resta aperto finché ce ne sono
}
int layout_hidden_count(void) {
  int n = 0; for (int i = nrow; i < napps; i++) if (layout_is_hidden(apps[i].tid)) n++; return n;
}
void hidden_menu(void) {
  static const char *list[32];
  nhidden = 0;
  for (int i = nrow; i < napps && nhidden < 32; i++) if (layout_is_hidden(apps[i].tid)) { hidden_idx[nhidden] = i; list[nhidden++] = apps[i].name; }
  if (!nhidden) { set_msg(_("Nessuna app nascosta: le nascondi dalle opzioni di un gioco in home"), 0); return; }
  menu_open(_("App nascoste (X per rimetterle in home)"), list, nhidden, hidden_pick, NULL);
}

// ------------------------------------------------------------ vista cartella --
#define FV_COLS 6
#define FV_TILE 200
static char fv_fid[16]; static int fv_sel; static float fv_anim, fv_scroll;

void folder_open(const char *fid) {
  snprintf(fv_fid, sizeof fv_fid, "%s", fid);
  fv_sel = 0; fv_anim = 0; fv_scroll = 0;
  if (ov_top() != OV_FOLDER) ov_push(OV_FOLDER);
}

void folder_draw(float t) {
  int a = (int)(255 * t);
  int n = layout_folder_count(fv_fid);
  if (!n) { ov_pop(); return; }                    // svuotata (app tolte o disinstallate)
  if (fv_sel >= n) fv_sel = n - 1;
  // pannello centrato, alto quanto le righe che servono (al massimo tre visibili)
  int gap = 40, step = FV_TILE + gap, cols = n < FV_COLS ? n : FV_COLS;
  int gw = cols * step - gap, rowh = FV_TILE + 74;
  int rows = (n + FV_COLS - 1) / FV_COLS, vis = rows < 3 ? rows : 3;
  int pw = gw + 160 < 900 ? 900 : gw + 160, ph = 170 + vis * rowh + 30;
  int px = SCREEN_W / 2 - pw / 2, py = (SCREEN_H - 90) / 2 - ph / 2 + (int)((1 - ease_out(t)) * 40);
  fill_rect(0, 0, SCREEN_W, SCREEN_H, RGB(4, 6, 12), a * 72 / 100);
  shadow_rrect(px, py, pw, ph, 34, 46, a * 70 / 100);
  fill_rrect(px, py, pw, ph, 34, mix(C_PANEL, RGB(10, 12, 20), 0.2f), a);
  draw_icon(IC_FOLDER, px + 74, py + 78, 46, C_ACC2, a);
  draw_text_fit(font(W_LIGHT, 50), layout_folder_name(fv_fid), px + 118, py + 46, pw - 360, C_WHITE, a, AL_L);
  char cnt[48]; snprintf(cnt, sizeof cnt, _("%d app"), n);
  draw_text(font(W_REG, 26), cnt, px + pw - 60, py + 62, C_DIM, a, AL_R);
  int top = py + 160, view = vis * rowh, x0 = SCREEN_W / 2 - gw / 2;
  float want = (float)((fv_sel / FV_COLS) * rowh);
  if (want - fv_scroll > view - rowh) fv_scroll = approach(fv_scroll, want - (view - rowh), 12.0f);
  else if (want < fv_scroll) fv_scroll = approach(fv_scroll, want, 12.0f);
  fv_anim = approach(fv_anim, (float)fv_sel, 18.0f);
  float pulse = 0.5f + 0.5f * sinf((float)g_time * 3.2f);
  SDL_Rect clip = { px, top - 16, pw, view + 16 };
  SDL_RenderSetClipRect(R, &clip);
  for (int k = 0; k < n; k++) {
    int i = layout_folder_app(fv_fid, k);
    if (i < 0) break;
    AppEntry *ap = &apps[i];
    int x = x0 + (k % FV_COLS) * step, y = top + (k / FV_COLS) * rowh - (int)fv_scroll;
    if (y + rowh < top - 20 || y > top + view) continue;
    int foc = k == fv_sel, s = foc ? FV_TILE + 12 : FV_TILE, o = (FV_TILE - s) / 2;
    if (foc) shadow_rrect(x + o, y + o, s, s, 26, 22, a * 70 / 100);
    if (ap->tex) draw_tex(ap->tex, x + o, y + o, s, s, a);
    else { fill_rrect(x + o, y + o, s, s, 26, RGB(30, 38, 60), a); draw_icon(IC_GAMEPAD, x + FV_TILE / 2, y + FV_TILE / 2, 70, C_DIM, a); }
    if (foc) focus_ring(x + o, y + o, s, s, 26, pulse, a);
    draw_text_fit(font(foc ? W_MED : W_REG, 22), ap->name, x + FV_TILE / 2, y + FV_TILE + 16, FV_TILE + 30, foc ? C_TXT : C_DIM, a, AL_C);
  }
  SDL_RenderSetClipRect(R, NULL);
  int ic[4] = { IC_BTN_X, IC_BTN_TRI, IC_BTN_SQ, IC_BTN_O };
  const char *lb[4] = { _("Gioca"), _("Opzioni dell'app"), _("Opzioni della cartella"), _("Chiudi") };
  hints(ic, lb, 4, a);
}

static void fv_app_pick(int idx, void *ud) {
  (void)ud;
  int i = layout_folder_app(fv_fid, fv_sel);
  if (i < 0) return;
  char tid[16]; snprintf(tid, sizeof tid, "%s", apps[i].tid);
  if (idx == 0) layout_unfold(tid);
  else if (idx == 1) layout_move_menu(tid);
  else if (idx == 2) { char m[160]; snprintf(m, sizeof m, _("%s nascosta: la rimetti in Impostazioni › App nascoste"), apps[i].name); layout_hide(tid, 1); set_msg(m, 0); }
}

void folder_input(int b) {
  int n = layout_folder_count(fv_fid);
  if (b == B_O) { ov_pop(); return; }
  if (!n) return;
  if (b == B_LEFT && fv_sel % FV_COLS > 0) fv_sel--;
  else if (b == B_RIGHT && fv_sel % FV_COLS < FV_COLS - 1 && fv_sel < n - 1) fv_sel++;
  else if (b == B_UP && fv_sel >= FV_COLS) fv_sel -= FV_COLS;
  else if (b == B_DOWN && fv_sel + FV_COLS < n) fv_sel += FV_COLS;
  else if (b == B_DOWN && fv_sel / FV_COLS < (n - 1) / FV_COLS) fv_sel = n - 1;
  else if (b == B_X) { int i = layout_folder_app(fv_fid, fv_sel); if (i >= 0) { ov_clear(); launch_app(i); } }
  else if (b == B_TRI) {
    int i = layout_folder_app(fv_fid, fv_sel);
    const char *it[] = { _("Togli dalla cartella"), _("Sposta in un'altra cartella"), _("Nascondi dalla home") };
    if (i >= 0) menu_open(apps[i].name, it, 3, fv_app_pick, NULL);
  } else if (b == B_SQ) layout_folder_menu(fv_fid);
}
