// GENERATO da omega-ui-src/tools/i18n-gen.mjs a partire da omega-ui-src/i18n/*.json:
// non modificare a mano (rigenerare con `node tools/i18n-gen.mjs`).
// Testi di Omega Installer (notifiche di sistema) e scelta della lingua. Autonomo:
// main.c dichiara i18n_tr, i18n_init e i18n_code e passa a i18n_init il valore
// di sceSystemServiceParamGetInt(1 /* lingua */) o -1 se non disponibile.
#include <stdio.h>
#include <string.h>

#define I18N_LANG_FILE "/data/Omega/lang.txt"   // scelta fatta nella UI (Impostazioni → Lingua)
#define NMSG 7
#define NTR 26

static const char *const MSGID[NMSG] = {
  "Omega %s installato. Avvio in corso...",
  "Omega è installato: aprilo da websrv (Homebrew)",
  "Omega: download in corso...",
  "Omega: firma dell'aggiornamento non valida, installazione annullata",
  "Omega: installazione non riuscita (vedi %s)",
  "Omega: preparazione dell'installazione...",
  "Omega: server non raggiungibile, riprova più tardi",
};
static const char *const CODE[NTR] = { "en", "ja", "fr", "es", "de", "nl", "pt-PT", "pt-BR", "ru", "ko", "zh-Hans", "zh-Hant", "fi", "sv", "da", "nb", "pl", "tr", "cs", "hu", "el", "ro", "th", "vi", "id", "uk" };
static const char *const TR[NTR][NMSG] = {
  { // en
    "Omega %s installed. Starting...",
    "Omega is installed: open it from websrv (Homebrew)",
    "Omega: downloading...",
    "Omega: invalid update signature, installation canceled",
    "Omega: installation failed (see %s)",
    "Omega: preparing installation...",
    "Omega: server unreachable, try again later",
  },
  { // ja
    "Omega %sをインストールしました。起動中...",
    "Omegaをインストールしました：websrv（Homebrew）から開いてください",
    "Omega：ダウンロード中...",
    "Omega：アップデートの署名が無効なため、インストールを中止しました",
    "Omega：インストールできませんでした（%sを参照）",
    "Omega：インストールの準備中...",
    "Omega：サーバーに接続できません。しばらくしてからもう一度お試しください",
  },
  { // fr
    "Omega %s installé. Lancement...",
    "Omega est installé : ouvrez-le depuis websrv (Homebrew)",
    "Omega : téléchargement...",
    "Omega : signature de mise à jour non valide, installation annulée",
    "Omega : échec de l'installation (voir %s)",
    "Omega : préparation de l'installation...",
    "Omega : serveur injoignable, réessayez plus tard",
  },
  { // es
    "Omega %s instalado. Iniciando...",
    "Omega está instalado: ábrelo desde websrv (Homebrew)",
    "Omega: descargando...",
    "Omega: firma de la actualización no válida, instalación cancelada",
    "Omega: error en la instalación (consulta %s)",
    "Omega: preparando la instalación...",
    "Omega: no se puede acceder al servidor, inténtalo más tarde",
  },
  { // de
    "Omega %s installiert. Wird gestartet ...",
    "Omega ist installiert: Öffne es über websrv (Homebrew)",
    "Omega: Download läuft ...",
    "Omega: Ungültige Update-Signatur, Installation abgebrochen",
    "Omega: Installation fehlgeschlagen (siehe %s)",
    "Omega: Installation wird vorbereitet ...",
    "Omega: Server nicht erreichbar, versuche es später erneut",
  },
  { // nl
    "Omega %s geïnstalleerd. Wordt gestart...",
    "Omega is geïnstalleerd: open het via websrv (Homebrew)",
    "Omega: downloaden...",
    "Omega: ongeldige updatehandtekening, installatie geannuleerd",
    "Omega: installatie mislukt (zie %s)",
    "Omega: installatie voorbereiden...",
    "Omega: server onbereikbaar, probeer het later opnieuw",
  },
  { // pt-PT
    "Omega %s instalado. A iniciar...",
    "O Omega está instalado: abre-o a partir do websrv (Homebrew)",
    "Omega: a transferir...",
    "Omega: assinatura da atualização inválida, instalação cancelada",
    "Omega: falha na instalação (ver %s)",
    "Omega: a preparar a instalação...",
    "Omega: servidor inacessível, tenta novamente mais tarde",
  },
  { // pt-BR
    "Omega %s instalado. Iniciando...",
    "O Omega está instalado: abra-o pelo websrv (Homebrew)",
    "Omega: baixando...",
    "Omega: assinatura da atualização inválida, instalação cancelada",
    "Omega: falha na instalação (veja %s)",
    "Omega: preparando a instalação...",
    "Omega: servidor inacessível, tente novamente mais tarde",
  },
  { // ru
    "Omega %s: установлено. Запуск...",
    "Omega: установлено, откройте через websrv (Homebrew)",
    "Omega: загрузка...",
    "Omega: недействительная подпись обновления, установка отменена",
    "Omega: ошибка установки (см. %s)",
    "Omega: подготовка к установке...",
    "Omega: сервер недоступен, повторите позже",
  },
  { // ko
    "Omega %s 설치됨. 시작하는 중...",
    "Omega가 설치되었습니다: websrv(홈브루)에서 여세요",
    "Omega: 다운로드 중...",
    "Omega: 업데이트 서명이 잘못되어 설치가 취소되었습니다",
    "Omega: 설치 실패 (%s 참조)",
    "Omega: 설치 준비 중...",
    "Omega: 서버에 연결할 수 없습니다. 나중에 다시 시도하세요",
  },
  { // zh-Hans
    "Omega %s 已安装。正在启动...",
    "Omega 已安装：请从 websrv（Homebrew）打开",
    "Omega：正在下载...",
    "Omega：更新签名无效，已取消安装",
    "Omega：安装失败（请参阅 %s）",
    "Omega：正在准备安装...",
    "Omega：无法连接服务器，请稍后再试",
  },
  { // zh-Hant
    "Omega %s 已安裝。正在啟動...",
    "Omega 已安裝：請從 websrv（Homebrew）開啟",
    "Omega：正在下載...",
    "Omega：更新簽章無效，已取消安裝",
    "Omega：安裝失敗（請參閱 %s）",
    "Omega：正在準備安裝...",
    "Omega：無法連線到伺服器，請稍後再試",
  },
  { // fi
    "Omega %s asennettu. Käynnistetään...",
    "Omega on asennettu: avaa se websrv-sovelluksesta (Homebrew)",
    "Omega: ladataan...",
    "Omega: päivityksen allekirjoitus virheellinen, asennus peruttu",
    "Omega: asennus epäonnistui (katso %s)",
    "Omega: valmistellaan asennusta...",
    "Omega: palvelimeen ei saada yhteyttä, yritä myöhemmin uudelleen",
  },
  { // sv
    "Omega %s installerat. Startar...",
    "Omega är installerat: öppna det från websrv (Homebrew)",
    "Omega: laddar ner...",
    "Omega: ogiltig uppdateringssignatur, installationen avbröts",
    "Omega: installationen misslyckades (se %s)",
    "Omega: förbereder installationen...",
    "Omega: servern kan inte nås, försök igen senare",
  },
  { // da
    "Omega %s installeret. Starter...",
    "Omega er installeret: åbn det fra websrv (Homebrew)",
    "Omega: downloader...",
    "Omega: ugyldig opdateringssignatur, installation annulleret",
    "Omega: installation mislykkedes (se %s)",
    "Omega: forbereder installation...",
    "Omega: serveren kan ikke nås, prøv igen senere",
  },
  { // nb
    "Omega %s installert. Starter ...",
    "Omega er installert: åpne det fra websrv (Homebrew)",
    "Omega: laster ned ...",
    "Omega: ugyldig oppdateringssignatur, installasjonen ble avbrutt",
    "Omega: installasjonen mislyktes (se %s)",
    "Omega: forbereder installasjonen ...",
    "Omega: serveren kan ikke nås, prøv igjen senere",
  },
  { // pl
    "Zainstalowano Omega %s. Uruchamianie...",
    "Omega jest zainstalowana: otwórz ją z websrv (Homebrew)",
    "Omega: pobieranie...",
    "Omega: nieprawidłowy podpis aktualizacji, anulowano instalację",
    "Omega: instalacja nieudana (zob. %s)",
    "Omega: przygotowywanie instalacji...",
    "Omega: serwer nieosiągalny, spróbuj ponownie później",
  },
  { // tr
    "Omega %s yüklendi. Başlatılıyor...",
    "Omega yüklendi: websrv'den (Homebrew) aç",
    "Omega: indiriliyor...",
    "Omega: güncelleme imzası geçersiz, kurulum iptal edildi",
    "Omega: kurulum başarısız (bkz. %s)",
    "Omega: kurulum hazırlanıyor...",
    "Omega: sunucuya ulaşılamıyor, daha sonra tekrar dene",
  },
  { // cs
    "Omega %s nainstalována. Spouštění...",
    "Omega je nainstalována: otevřete ji z websrv (Homebrew)",
    "Omega: stahování...",
    "Omega: neplatný podpis aktualizace, instalace zrušena",
    "Omega: instalace se nezdařila (viz %s)",
    "Omega: příprava instalace...",
    "Omega: server je nedostupný, zkuste to později",
  },
  { // hu
    "Omega %s telepítve. Indítás...",
    "Az Omega telepítve: nyisd meg a websrv-ből (Homebrew)",
    "Omega: letöltés...",
    "Omega: érvénytelen frissítési aláírás, telepítés megszakítva",
    "Omega: sikertelen telepítés (lásd: %s)",
    "Omega: telepítés előkészítése...",
    "Omega: a szerver nem elérhető, próbáld újra később",
  },
  { // el
    "Το Omega %s εγκαταστάθηκε. Εκκίνηση...",
    "Το Omega εγκαταστάθηκε: ανοίξτε το από το websrv (Homebrew)",
    "Omega: λήψη...",
    "Omega: μη έγκυρη υπογραφή ενημέρωσης, η εγκατάσταση ακυρώθηκε",
    "Omega: η εγκατάσταση απέτυχε (δείτε %s)",
    "Omega: προετοιμασία εγκατάστασης...",
    "Omega: ο διακομιστής δεν είναι προσβάσιμος, δοκιμάστε ξανά αργότερα",
  },
  { // ro
    "Omega %s instalat. Se pornește...",
    "Omega este instalat: deschide-l din websrv (Homebrew)",
    "Omega: se descarcă...",
    "Omega: semnătura actualizării este nevalidă, instalare anulată",
    "Omega: instalare eșuată (vezi %s)",
    "Omega: se pregătește instalarea...",
    "Omega: server inaccesibil, încearcă mai târziu",
  },
  { // th
    "ติดตั้ง Omega %s แล้ว กำลังเริ่ม...",
    "ติดตั้ง Omega แล้ว: เปิดได้จาก websrv (Homebrew)",
    "Omega: กำลังดาวน์โหลด...",
    "Omega: ลายเซ็นของอัปเดตไม่ถูกต้อง ยกเลิกการติดตั้งแล้ว",
    "Omega: ติดตั้งไม่สำเร็จ (ดู %s)",
    "Omega: กำลังเตรียมการติดตั้ง...",
    "Omega: เชื่อมต่อเซิร์ฟเวอร์ไม่ได้ ลองใหม่ภายหลัง",
  },
  { // vi
    "Đã cài đặt Omega %s. Đang khởi chạy...",
    "Đã cài đặt Omega: hãy mở từ websrv (Homebrew)",
    "Omega: đang tải xuống...",
    "Omega: chữ ký bản cập nhật không hợp lệ, đã hủy cài đặt",
    "Omega: cài đặt thất bại (xem %s)",
    "Omega: đang chuẩn bị cài đặt...",
    "Omega: không kết nối được máy chủ, hãy thử lại sau",
  },
  { // id
    "Omega %s terinstal. Memulai...",
    "Omega terinstal: buka dari websrv (Homebrew)",
    "Omega: mengunduh...",
    "Omega: tanda tangan pembaruan tidak valid, instalasi dibatalkan",
    "Omega: instalasi gagal (lihat %s)",
    "Omega: menyiapkan instalasi...",
    "Omega: server tidak dapat dijangkau, coba lagi nanti",
  },
  { // uk
    "Omega %s встановлено. Запуск...",
    "Omega встановлено: відкрийте з websrv (Homebrew)",
    "Omega: завантаження...",
    "Omega: недійсний підпис оновлення, встановлення скасовано",
    "Omega: не вдалося встановити (див. %s)",
    "Omega: підготовка до встановлення...",
    "Omega: сервер недоступний, спробуйте пізніше",
  },
};
static const char *const LANGS[] = { "it", "en", "ja", "fr", "es", "de", "nl", "pt-PT", "pt-BR", "ru", "ko", "zh-Hans", "zh-Hant", "fi", "sv", "da", "nb", "pl", "tr", "cs", "hu", "el", "ro", "th", "vi", "id", "uk" };
// valore di SCE_SYSTEM_SERVICE_PARAM_ID_LANG → codice (21 = arabo: inglese)
static const char *const PS5_LANG[] = {
  "ja", "en", "fr", "es", "de", "it", "nl", "pt-PT", "ru", "ko", "zh-Hant", "zh-Hans", "fi", "sv", "da", "nb",
  "pl", "pt-BR", "en", "tr", "es", "en", "fr", "cs", "hu", "el", "ro", "th", "vi", "id", "uk",
};

static int cur = -1;                 // riga di TR, -1 = msgid (italiano)
static const char *cur_code = "en";

const char *i18n_tr(const char *s) __attribute__((format_arg(1)));
const char *i18n_tr(const char *s) {
  if (cur < 0 || !s) return s;
  for (int i = 0; i < NMSG; i++) if (!strcmp(MSGID[i], s)) return TR[cur][i] ? TR[cur][i] : s;
  return s;
}

const char *i18n_code(void) { return cur_code; }

void i18n_init(int sys_lang) {
  char buf[16] = ""; const char *code = NULL;
  FILE *f = fopen(I18N_LANG_FILE, "r");
  if (f) { if (!fgets(buf, sizeof buf, f)) buf[0] = 0; fclose(f); buf[strcspn(buf, "\r\n \t")] = 0; }
  for (unsigned i = 0; buf[0] && i < sizeof LANGS / sizeof LANGS[0]; i++) if (!strcmp(LANGS[i], buf)) code = LANGS[i];
  if (!code) code = sys_lang >= 0 && sys_lang < (int)(sizeof PS5_LANG / sizeof PS5_LANG[0]) ? PS5_LANG[sys_lang] : "en";
  cur_code = code; cur = -1;
  for (int i = 0; i < NTR; i++) if (!strcmp(CODE[i], code)) cur = i;
}
