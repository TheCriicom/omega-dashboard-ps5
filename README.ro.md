<p align="center">
  <picture><source media="(prefers-color-scheme: dark)" srcset="docs/img/omega-mark.svg"><img src="docs/img/omega-mark-light.svg" width="96" alt="Omega"></picture>
</p>

<h1 align="center">Omega</h1>

<p align="center">
  Dashboard-ul open source pentru console PS5 cu homebrew: jocuri, Store, muzică, prieteni și party într-un singur loc.<br>
  Dezvoltat de <a href="https://outlinedigital.it"><b>TheCriicom</b></a>.
</p>

<p align="center"><sub><a href="README.md">English</a> · <a href="README.it.md">Italiano</a> · <a href="README.ja.md">日本語</a> · <a href="README.fr.md">Français</a> · <a href="README.es.md">Español</a> · <a href="README.de.md">Deutsch</a> · <a href="README.nl.md">Nederlands</a> · <a href="README.pt-PT.md">Português (Portugal)</a> · <a href="README.pt-BR.md">Português (Brasil)</a> · <a href="README.ru.md">Русский</a> · <a href="README.ko.md">한국어</a> · <a href="README.zh-Hans.md">简体中文</a> · <a href="README.zh-Hant.md">繁體中文</a> · <a href="README.fi.md">Suomi</a> · <a href="README.sv.md">Svenska</a> · <a href="README.da.md">Dansk</a> · <a href="README.nb.md">Norsk bokmål</a> · <a href="README.pl.md">Polski</a> · <a href="README.tr.md">Türkçe</a> · <a href="README.cs.md">Čeština</a> · <a href="README.hu.md">Magyar</a> · <a href="README.el.md">Ελληνικά</a> · <b>Română</b> · <a href="README.th.md">ไทย</a> · <a href="README.vi.md">Tiếng Việt</a> · <a href="README.id.md">Bahasa Indonesia</a> · <a href="README.uk.md">Українська</a></sub></p>

<p align="center">
  <a href="https://play.omegasuite.it">Site web</a> ·
  <a href="https://play.omegasuite.it/installa">Instalare</a> ·
  <a href="server/README.md">Găzduiește un server</a> ·
  <a href="client/README.md">Dezvoltare</a>
</p>

![Home în Omega](docs/screenshots/home.png)

Omega este un dashboard gândit pentru utilizatorii de homebrew și, dacă vrei,
Home-ul tău (te întreabă la prima pornire): jocurile instalate și homebrew-urile
stau pe același rând, Store-ul instalează cu un singur buton, muzica merge mai
departe cât timp joci, iar prietenii sunt mereu la o apăsare distanță. Rulează
ca homebrew (SDL2, randare software) și comunică cu un server pe care îl poate
găzdui oricine.

## Ce conține

- **Home** cu jocuri și homebrew împreună, fundaluri dinamice generate din
  artwork, lansare directă (jocurile prin LncUtil, homebrew-urile prin websrv,
  payload-urile ELF în fundal).
- **Store** de homebrew open source: căutare, rafturi pe categorii, voturi,
  evaluări și comentarii. Recunoaște singur fișierele `.pkg`, `.zip` și `.elf`
  și instalează fiecare fișier la locul potrivit.
- **Biblioteca mea**: copiile de siguranță ale propriilor jocuri, cu link de
  descărcare și copertă, adăugate de pe consolă sau de pe telefon ori importate
  dintr-un fișier JSON (o dată sau legat, ca să rămână sincronizat). Stocată
  doar pe consolă.
- **Muzică**, inclusiv în timpul jocurilor: radio pe internet (radio-browser),
  Navidrome și alte servere Subsonic, fișiere de pe USB, orice link audio.
  Redarea rulează în daemonul din fundal (build FFmpeg doar pentru audio), așa
  că nu se oprește când pornești un joc.
- **Telecomandă pe telefon**: daemonul servește o pagină web pe portul 9095.
  Scanează codul QR afișat pe consolă, introdu PIN-ul și controlează muzica,
  trimite fișiere audio și gestionează Biblioteca mea din browserul de pe orice
  telefon, tabletă sau PC.
- **Detectare HEN**: la pornire, Omega recunoaște OnionHEN, etaHEN, pldmgr sau
  ps5_autoloader, îți spune ce lipsește și, după ce confirmi, instalează și
  activează serviciile acestora la locul potrivit. Pe OnionHEN adaugă în meniul din joc
  (L2 + R3) o pagină cu comenzi pentru muzică.
- **Unelte de sistem**: temperaturi, prag al ventilatorului, stocare și manager
  de fișiere.
- **Community**: un perete cu postări, aprecieri și comentarii; chaturi de grup;
  persoane pe care s-ar putea să le cunoști; timp de joc și un clasament al
  prietenilor.
- **Party** cu chat și voce (Opus), invitații la joc, status personalizat
  (online, plecat, nu deranja, invizibil).
- **Game Base**: prieteni, cereri și mesaje.
- **Confidențialitate**: blocare, raportare, cine îți poate trimite mesaje,
  exportul datelor, ștergerea contului.
- **Teme**, muzică ambientală generată, un browser pentru citit.
- **27 de limbi**: aplicația urmează automat limba consolei (și poate fi
  schimbată din Setări).
- **Alege-ți serverul**: adaugă în Setări adresa oricărui server Omega; cel
  oficial rămâne mereu disponibil.

| | |
|---|---|
| ![Store](docs/screenshots/store.png) | ![Community](docs/screenshots/community.png) |
| ![Party](docs/screenshots/party.png) | ![Timp de joc](docs/screenshots/stats.png) |
| ![Telecomanda de pe telefon, pe PC](docs/screenshots/remote-music.png) | ![Biblioteca mea de pe PC](docs/screenshots/remote-library.png) |

## Structura depozitului

| Folder | |
|---|---|
| [`client/`](client) | aplicația pentru consolă (C, SDL2) și o versiune desktop pentru dezvoltare pe Mac |
| [`daemon/`](daemon) | payload în fundal: player de muzică, pagina web a telecomenzii de pe telefon, Biblioteca mea, notificări în timpul jocului și (doar dacă alegi tu) redeschiderea Omega când revii în Home |
| [`onionhen-plugin/`](onionhen-plugin) | plugin OnionHEN: o pagină Omega în meniul din joc, cu comenzi pentru muzică |
| [`server/`](server) | API Node.js, proxy, panou de moderare și Docker Compose pentru găzduirea unui server |
| [`docs/`](docs) | arhitectură și imagini |

## Instalarea Omega pe consolă

Ai nevoie de un PS5 cu jailbreak și suport pentru homebrew: un HEN precum OnionHEN
sau etaHEN, ori un launcher precum [websrv](https://github.com/ps5-payload-dev/websrv) cu un loader de payload-uri.
Cea mai simplă cale este [play.omegasuite.it/installa](https://play.omegasuite.it/installa),
deschisă în browserul consolei. Alternativ, descarcă pachetul de pe site și
copiază `data/` pe consolă prin FTP.

## Găzduirea unui server

```sh
git clone https://github.com/TheCriicom/omega-dashboard-ps5.git
cd omega-dashboard-ps5/server
./scripts/install.sh
```

Scriptul creează `.env` cu secrete aleatorii și pornește containerele. Ghidul
complet, cu HTTPS automat prin Caddy, se află în
[`server/README.md`](server/README.md). Apoi, pe consolă:
**Setări → Server → Adaugă un server**.

## Dezvoltare

- Aplicația: [`client/README.md`](client/README.md) — build pentru PS5 cu
  [ps5-payload-sdk](https://github.com/ps5-payload-dev/sdk) și un build desktop
  cu SDL2 din Homebrew, controlat printr-un fișier de comenzi pentru a testa
  interfața fără consolă. Traducerile se află în `client/i18n/` (câte un JSON
  pentru fiecare limbă).
- Server: [`server/README.md`](server/README.md) — Node.js ≥ 20 și PostgreSQL,
  teste end-to-end cu `npm test`.
- Arhitectură: [`docs/architecture.md`](docs/architecture.md).

Contribuțiile sunt binevenite: vezi [CONTRIBUTING.md](CONTRIBUTING.md).

## Autor

Omega este dezvoltat de **TheCriicom** — [outlinedigital.it](https://outlinedigital.it).

## Licență

Copyright © 2026 TheCriicom și contribuitorii Omega.
Omega este software liber: [GNU GPL v3 sau ulterioară](LICENSE). Componentele
terțe și licențele lor sunt enumerate în
[`client/THIRD-PARTY-NOTICES.md`](client/THIRD-PARTY-NOTICES.md).

Omega este un proiect independent și nu este afiliat cu Sony Interactive
Entertainment, nici aprobat sau sponsorizat de aceasta. „PlayStation” și „PS5”
sunt mărci comerciale ale deținătorilor lor. Omega nu conține și nu
distribuie jocuri.
