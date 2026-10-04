<p align="center">
  <picture><source media="(prefers-color-scheme: dark)" srcset="docs/img/omega-mark.svg"><img src="docs/img/omega-mark-light.svg" width="96" alt="Omega"></picture>
</p>

<h1 align="center">Omega</h1>

<p align="center">
  Nyílt forráskódú vezérlőpult homebrew-képes PS5 konzolokhoz: játékok, Store, zene, barátok és party egy helyen.<br>
  Fejlesztő: <a href="https://outlinedigital.it"><b>TheCriicom</b></a>.
</p>

<p align="center"><sub><a href="README.md">English</a> · <a href="README.it.md">Italiano</a> · <a href="README.ja.md">日本語</a> · <a href="README.fr.md">Français</a> · <a href="README.es.md">Español</a> · <a href="README.de.md">Deutsch</a> · <a href="README.nl.md">Nederlands</a> · <a href="README.pt-PT.md">Português (Portugal)</a> · <a href="README.pt-BR.md">Português (Brasil)</a> · <a href="README.ru.md">Русский</a> · <a href="README.ko.md">한국어</a> · <a href="README.zh-Hans.md">简体中文</a> · <a href="README.zh-Hant.md">繁體中文</a> · <a href="README.fi.md">Suomi</a> · <a href="README.sv.md">Svenska</a> · <a href="README.da.md">Dansk</a> · <a href="README.nb.md">Norsk bokmål</a> · <a href="README.pl.md">Polski</a> · <a href="README.tr.md">Türkçe</a> · <a href="README.cs.md">Čeština</a> · <b>Magyar</b> · <a href="README.el.md">Ελληνικά</a> · <a href="README.ro.md">Română</a> · <a href="README.th.md">ไทย</a> · <a href="README.vi.md">Tiếng Việt</a> · <a href="README.id.md">Bahasa Indonesia</a> · <a href="README.uk.md">Українська</a></sub></p>

<p align="center">
  <a href="https://play.omegasuite.it">Weboldal</a> ·
  <a href="https://play.omegasuite.it/installa">Telepítés</a> ·
  <a href="server/README.md">Saját szerver</a> ·
  <a href="client/README.md">Fejlesztés</a>
</p>

![Az Omega Home képernyője](docs/screenshots/home.png)

Az Omega egy homebrew-felhasználóknak készült vezérlőpult, és ha szeretnéd, a
Home képernyőd is (az első indításkor rákérdez): a telepített játékok és a
homebrew-k egy sorban vannak, a Store egyetlen gombnyomással telepít, a zene
játék közben is szól, a barátaid pedig mindig karnyújtásnyira vannak.
Homebrew-ként fut (SDL2, szoftveres renderelés), és egy olyan szerverrel
kommunikál, amelyet bárki üzemeltethet.

## Mit tartalmaz

- **Home** a játékokkal és homebrew-kkal együtt, a borítóképekből készülő
  dinamikus hátterekkel és közvetlen indítással (játékok LncUtil-lal,
  homebrew-k websrv-vel, ELF payloadok a háttérben).
- Nyílt forráskódú homebrew-k **Store**-ja: keresés, kategóriapolcok,
  szavazatok, értékelések és hozzászólások. Magától felismeri a `.pkg`, `.zip`
  és `.elf` fájlokat, és mindegyiket a megfelelő helyre telepíti.
- **Saját könyvtár**: a saját játékaid biztonsági mentései letöltési
  hivatkozással és borítóval, a konzolról vagy a telefonodról hozzáadva, illetve
  JSON-fájlból importálva (egyszer, vagy összekapcsolva, hogy szinkronban
  maradjon). Csak a konzolon tárolódik.
- **Zene**, játék közben is: internetes rádió (radio-browser), Navidrome és más
  Subsonic-szerverek, USB-s fájlok, bármilyen hanglink. A lejátszás a háttérben
  futó daemonban zajlik (csak hangot tartalmazó FFmpeg-build), ezért nem áll le,
  amikor elindítasz egy játékot.
- **Telefonos távirányító**: a daemon egy weboldalt szolgál ki a 9095-ös porton.
  Olvasd be a konzolon megjelenő QR-kódot, írd be a PIN-kódot, és irányítsd a
  zenét, küldj hangfájlokat, és kezeld a Saját könyvtárat bármely telefon,
  táblagép vagy PC böngészőjéből.
- **HEN-felismerés**: indításkor az Omega felismeri az OnionHEN-t, az etaHEN-t,
  a pldmgr-t vagy a ps5_autoloadert, megmondja, mi hiányzik, és a jóváhagyásod
  után a megfelelő helyre telepíti és engedélyezi a szolgáltatásait. OnionHEN-en
  egy zenevezérlős oldalt ad a játékon belüli menühöz (L2 + R3).
- **Rendszereszközök**: hőmérsékletek, ventilátorküszöb, tárhely és
  fájlkezelő.
- **Community**: üzenőfal bejegyzésekkel, kedvelésekkel és hozzászólásokkal;
  csoportos csevegések; ismerősök, akiket talán ismersz; játékidő és baráti
  ranglista.
- **Party** csevegéssel és hanggal (Opus), játékmeghívókkal és egyéni
  állapottal (online, távol, ne zavarj, láthatatlan).
- **Game Base**: barátok, kérések és üzenetek.
- **Adatvédelem**: letiltás, jelentés, ki írhat neked, adatexportálás,
  fiók törlése.
- **Témák**, generált hangulatzene, olvasásra szánt böngésző.
- **27 nyelv**: az alkalmazás automatikusan a konzol nyelvét használja (a
  Beállításokban módosítható).
- **Választható szerver**: a Beállításokban bármely Omega-szerver címét
  hozzáadhatod; a hivatalos szerver mindig elérhető marad.

| | |
|---|---|
| ![Store](docs/screenshots/store.png) | ![Community](docs/screenshots/community.png) |
| ![Party](docs/screenshots/party.png) | ![Játékidő](docs/screenshots/stats.png) |
| ![Telefonos távirányító PC-n](docs/screenshots/remote-music.png) | ![Saját könyvtár PC-ről](docs/screenshots/remote-library.png) |

## A tároló felépítése

| Mappa | |
|---|---|
| [`client/`](client) | a konzolos alkalmazás (C, SDL2) és egy asztali változat a Macen történő fejlesztéshez |
| [`daemon/`](daemon) | háttérben futó payload: zenelejátszó, a telefonos távirányító weboldala, Saját könyvtár, értesítések játék közben, és (csak ha ezt választod) az Omega újranyitása, amikor visszatérsz a Home képernyőre |
| [`onionhen-plugin/`](onionhen-plugin) | OnionHEN-plugin: egy Omega-oldal a játékon belüli menüben zenevezérlőkkel |
| [`server/`](server) | Node.js API, proxy, moderációs panel és Docker Compose a szerver üzemeltetéséhez |
| [`docs/`](docs) | architektúra és képek |

## Az Omega telepítése a konzolra

Jailbreakelt, homebrew-támogatással rendelkező PS5 szükséges: egy HEN, például az
OnionHEN vagy az etaHEN, vagy egy indító, például a [websrv](https://github.com/ps5-payload-dev/websrv)
payload-betöltővel.
A legegyszerűbb, ha a konzol böngészőjében megnyitod a
[play.omegasuite.it/installa](https://play.omegasuite.it/installa) oldalt.
Másik lehetőségként töltsd le a csomagot a weboldalról, és másold a `data/`
mappát FTP-n keresztül a konzolra.

## Szerver üzemeltetése

```sh
git clone https://github.com/CristianLaporta/omega-dashboard-ps5.git
cd omega-dashboard-ps5/server
./scripts/install.sh
```

A szkript véletlenszerű titkos kulcsokkal létrehozza a `.env` fájlt, és elindítja
a konténereket. A teljes útmutató – a Caddyn keresztüli automatikus HTTPS-sel –
a [`server/README.md`](server/README.md) fájlban található. Ezután a konzolon:
**Beállítások → Szerver → Szerver hozzáadása**.

## Fejlesztés

- Alkalmazás: [`client/README.md`](client/README.md) – PS5-ös build a
  [ps5-payload-sdk](https://github.com/ps5-payload-dev/sdk) segítségével, valamint
  asztali build a Homebrew SDL2-jével, amely egy parancsfájllal vezérelhető, így a
  felület konzol nélkül is tesztelhető. A fordítások a `client/i18n/` mappában
  vannak (nyelvenként egy JSON).
- Szerver: [`server/README.md`](server/README.md) – Node.js ≥ 20 és PostgreSQL,
  végponttól végpontig tartó tesztek az `npm test` paranccsal.
- Architektúra: [`docs/architecture.md`](docs/architecture.md).

A közreműködést szívesen fogadjuk: lásd a [CONTRIBUTING.md](CONTRIBUTING.md) fájlt.

## Szerző

Az Omegát a **TheCriicom** fejleszti — [outlinedigital.it](https://outlinedigital.it).

## Licenc

Copyright © 2026 TheCriicom és az Omega közreműködői.
Az Omega szabad szoftver: [GNU GPL v3 vagy újabb](LICENSE). A harmadik féltől
származó összetevők és licenceik a
[`client/THIRD-PARTY-NOTICES.md`](client/THIRD-PARTY-NOTICES.md) fájlban
találhatók.

Az Omega független projekt, nem áll kapcsolatban a Sony Interactive
Entertainmenttel, és nem annak jóváhagyásával vagy szponzorálásával készül. A „PlayStation” és a „PS5” a megfelelő tulajdonosaik védjegyei.
Az Omega nem tartalmaz és nem terjeszt játékokat.
