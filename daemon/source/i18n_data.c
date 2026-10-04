// GENERATO da omega-ui-src/tools/i18n-gen.mjs a partire da omega-ui-src/i18n/*.json:
// non modificare a mano (rigenerare con `node tools/i18n-gen.mjs`).
// Testi di omega_redirect (notifiche di sistema) e scelta della lingua. Autonomo:
// main.c dichiara i18n_tr, i18n_init e i18n_code e passa a i18n_init il valore
// di sceSystemServiceParamGetInt(1 /* lingua */) o -1 se non disponibile.
#include <stdio.h>
#include <string.h>

#define I18N_LANG_FILE "/data/Omega/lang.txt"   // scelta fatta nella UI (Impostazioni → Lingua)
#define NMSG 5
#define NTR 26

static const char *const MSGID[NMSG] = {
  "Omega · %s",
  "Omega · %s: %s",
  "Omega: %s è in La mia libreria, pronto da installare",
  "Omega: %s è tra gli homebrew",
  "Omega: ricevo i giochi dal telefono o dal PC (%d%%)",
};
static const char *const CODE[NTR] = { "en", "ja", "fr", "es", "de", "nl", "pt-PT", "pt-BR", "ru", "ko", "zh-Hans", "zh-Hant", "fi", "sv", "da", "nb", "pl", "tr", "cs", "hu", "el", "ro", "th", "vi", "id", "uk" };
static const char *const TR[NTR][NMSG] = {
  { // en
    0,
    0,
    "Omega: %s is in My library, ready to install",
    "Omega: %s is now with your homebrew",
    "Omega: receiving games from your phone or PC (%d%%)",
  },
  { // ja
    0,
    "Omega · %s：%s",
    "Omega：%s がマイライブラリーに追加されました。インストールできます",
    "Omega：%s を自作ソフトに追加しました",
    "Omega：スマホまたはPCからゲームを受信中（%d%%）",
  },
  { // fr
    0,
    "Omega · %s : %s",
    "Omega : %s est dans Ma bibliothèque, prêt à installer",
    "Omega : %s est parmi tes homebrew",
    "Omega : réception des jeux depuis le téléphone ou le PC (%d%%)",
  },
  { // es
    0,
    0,
    "Omega: %s está en Mi biblioteca, listo para instalar",
    "Omega: %s ya está entre tus homebrew",
    "Omega: recibiendo juegos del móvil o el PC (%d%%)",
  },
  { // de
    0,
    0,
    "Omega: %s ist in Meiner Bibliothek, bereit zur Installation",
    "Omega: %s ist jetzt bei deinen Homebrews",
    "Omega: Spiele vom Handy oder PC werden empfangen (%d%%)",
  },
  { // nl
    0,
    0,
    "Omega: %s staat in Mijn bibliotheek, klaar om te installeren",
    "Omega: %s staat nu bij je homebrew",
    "Omega: games ontvangen van telefoon of pc (%d%%)",
  },
  { // pt-PT
    0,
    0,
    "Omega: %s está n'A minha biblioteca, pronto a instalar",
    "Omega: %s está agora nos teus homebrew",
    "Omega: a receber jogos do telemóvel ou do PC (%d%%)",
  },
  { // pt-BR
    0,
    0,
    "Omega: %s está na Minha biblioteca, pronto para instalar",
    "Omega: %s agora está nos seus homebrews",
    "Omega: recebendo jogos do celular ou do PC (%d%%)",
  },
  { // ru
    0,
    0,
    "Omega: %s в «Моей библиотеке», можно устанавливать",
    "Omega: %s теперь среди ваших homebrew",
    "Omega: получаю игры с телефона или ПК (%d%%)",
  },
  { // ko
    0,
    0,
    "Omega: %s이(가) 내 라이브러리에 있습니다. 설치할 수 있습니다",
    "Omega: %s이(가) 홈브루에 추가되었습니다",
    "Omega: 휴대폰이나 PC에서 게임을 받는 중 (%d%%)",
  },
  { // zh-Hans
    0,
    "Omega · %s：%s",
    "Omega：%s 已在我的游戏库中，可以安装",
    "Omega：%s 已加入你的自制软件",
    "Omega：正在从手机或电脑接收游戏（%d%%）",
  },
  { // zh-Hant
    0,
    "Omega · %s：%s",
    "Omega：%s 已在我的遊戲庫中，可以安裝",
    "Omega：%s 已加入你的自製軟體",
    "Omega：正在從手機或電腦接收遊戲（%d%%）",
  },
  { // fi
    0,
    0,
    "Omega: %s on Omassa kirjastossa, valmiina asennettavaksi",
    "Omega: %s on nyt homebrew-sovelluksissasi",
    "Omega: vastaanotetaan pelejä puhelimesta tai PC:ltä (%d%%)",
  },
  { // sv
    0,
    0,
    "Omega: %s finns i Mitt bibliotek, redo att installeras",
    "Omega: %s finns nu bland dina homebrew",
    "Omega: tar emot spel från mobilen eller datorn (%d%%)",
  },
  { // da
    0,
    0,
    "Omega: %s er i Mit bibliotek, klar til at blive installeret",
    "Omega: %s er nu blandt dine homebrew",
    "Omega: modtager spil fra telefonen eller pc'en (%d%%)",
  },
  { // nb
    0,
    0,
    "Omega: %s er i Mitt bibliotek, klar til å installeres",
    "Omega: %s er nå blant dine homebrew",
    "Omega: tar imot spill fra telefonen eller PC-en (%d%%)",
  },
  { // pl
    0,
    0,
    "Omega: %s jest w Mojej bibliotece, gotowe do instalacji",
    "Omega: %s jest już wśród twoich homebrew",
    "Omega: odbieram gry z telefonu lub komputera (%d%%)",
  },
  { // tr
    0,
    0,
    "Omega: %s Kitaplığım'da, kurulmaya hazır",
    "Omega: %s artık homebrew'larında",
    "Omega: telefondan veya bilgisayardan oyunlar alınıyor (%d%%)",
  },
  { // cs
    0,
    0,
    "Omega: %s je v Mé knihovně, připraveno k instalaci",
    "Omega: %s je mezi tvými homebrew",
    "Omega: přijímám hry z telefonu nebo PC (%d %%)",
  },
  { // hu
    0,
    0,
    "Omega: %s a Saját könyvtáramban van, telepíthető",
    "Omega: %s most a homebrew-id között van",
    "Omega: játékok fogadása telefonról vagy PC-ről (%d%%)",
  },
  { // el
    0,
    0,
    "Omega: το %s είναι στη Βιβλιοθήκη μου, έτοιμο για εγκατάσταση",
    "Omega: το %s είναι πλέον στα homebrew σου",
    "Omega: λήψη παιχνιδιών από κινητό ή PC (%d%%)",
  },
  { // ro
    0,
    0,
    "Omega: %s e în Biblioteca mea, gata de instalat",
    "Omega: %s e acum printre homebrew-urile tale",
    "Omega: primesc jocuri de pe telefon sau PC (%d%%)",
  },
  { // th
    0,
    0,
    "Omega: %s อยู่ในคลังของฉันแล้ว พร้อมติดตั้ง",
    "Omega: %s อยู่ในโฮมบรูของคุณแล้ว",
    "Omega: กำลังรับเกมจากโทรศัพท์หรือ PC (%d%%)",
  },
  { // vi
    0,
    0,
    "Omega: %s đã có trong Thư viện của tôi, sẵn sàng cài đặt",
    "Omega: %s đã có trong homebrew của bạn",
    "Omega: đang nhận trò chơi từ điện thoại hoặc PC (%d%%)",
  },
  { // id
    0,
    0,
    "Omega: %s ada di Pustaka saya, siap dipasang",
    "Omega: %s kini ada di homebrew-mu",
    "Omega: menerima game dari ponsel atau PC (%d%%)",
  },
  { // uk
    0,
    0,
    "Omega: %s у «Моїй бібліотеці», можна встановлювати",
    "Omega: %s тепер серед ваших homebrew",
    "Omega: отримую ігри з телефона або ПК (%d%%)",
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
