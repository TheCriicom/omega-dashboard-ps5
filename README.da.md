<p align="center">
  <picture><source media="(prefers-color-scheme: dark)" srcset="docs/img/omega-mark.svg"><img src="docs/img/omega-mark-light.svg" width="96" alt="Omega"></picture>
</p>

<h1 align="center">Omega</h1>

<p align="center">
  Open source-dashboardet til PS5-konsoller med homebrew: spil, Store, musik, venner og party samlet ét sted.<br>
  Udviklet af <a href="https://outlinedigital.it"><b>TheCriicom</b></a>.
</p>

<p align="center"><sub><a href="README.md">English</a> · <a href="README.it.md">Italiano</a> · <a href="README.ja.md">日本語</a> · <a href="README.fr.md">Français</a> · <a href="README.es.md">Español</a> · <a href="README.de.md">Deutsch</a> · <a href="README.nl.md">Nederlands</a> · <a href="README.pt-PT.md">Português (Portugal)</a> · <a href="README.pt-BR.md">Português (Brasil)</a> · <a href="README.ru.md">Русский</a> · <a href="README.ko.md">한국어</a> · <a href="README.zh-Hans.md">简体中文</a> · <a href="README.zh-Hant.md">繁體中文</a> · <a href="README.fi.md">Suomi</a> · <a href="README.sv.md">Svenska</a> · <b>Dansk</b> · <a href="README.nb.md">Norsk bokmål</a> · <a href="README.pl.md">Polski</a> · <a href="README.tr.md">Türkçe</a> · <a href="README.cs.md">Čeština</a> · <a href="README.hu.md">Magyar</a> · <a href="README.el.md">Ελληνικά</a> · <a href="README.ro.md">Română</a> · <a href="README.th.md">ไทย</a> · <a href="README.vi.md">Tiếng Việt</a> · <a href="README.id.md">Bahasa Indonesia</a> · <a href="README.uk.md">Українська</a></sub></p>

<p align="center">
  <a href="https://play.omegasuite.it">Websted</a> ·
  <a href="https://play.omegasuite.it/installa">Installér</a> ·
  <a href="server/README.md">Kør en server</a> ·
  <a href="client/README.md">Udvikling</a>
</p>

![Omegas Home](docs/screenshots/home.png)

Omega er et dashboard bygget til homebrew-brugere, og din Home-skærm, hvis du
vil have det sådan (det spørger ved første start): installerede spil og homebrew
står på samme række, Store installerer med ét tryk, musikken bliver ved med at
spille, mens du spiller, og dine venner er altid kun et tryk væk. Omega kører som
homebrew (SDL2, softwarerenderer) og taler med en server, som alle kan køre.

## Hvad er der i

- **Home** med spil og homebrew samlet, dynamiske baggrunde ud fra
  coverbillederne og direkte start (spil via LncUtil, homebrew via websrv,
  ELF-payloads i baggrunden).
- **Store** med open source-homebrew: søgning, kategorihylder, stemmer,
  bedømmelser og kommentarer. Den genkender selv `.pkg`, `.zip` og `.elf` og
  installerer hver fil det rigtige sted.
- **Mit bibliotek**: dine egne spilkopier med et downloadlink og et cover,
  tilføjet fra konsollen eller fra telefonen eller importeret fra en JSON-fil
  (én gang eller linket, så det holdes synkroniseret). Gemmes kun på konsollen.
- **Musik**, også under spil: internetradio (radio-browser), Navidrome og andre
  Subsonic-servere, USB-filer, ethvert lydlink. Afspilningen kører i
  baggrundsdaemonen (FFmpeg-build kun med lyd), så den stopper ikke, når du
  starter et spil.
- **Telefonfjernbetjening**: daemonen serverer en webside på port 9095. Scan
  QR-koden på konsollen, indtast PIN-koden, og styr musikken, send lydfiler og
  administrer Mit bibliotek fra en hvilken som helst telefon, tablet eller
  pc-browser.
- **HEN-genkendelse**: ved start genkender Omega OnionHEN, etaHEN, pldmgr eller
  ps5_autoloader, fortæller, hvad der mangler, og installerer og aktiverer, når
  du bekræfter, dens tjenester det rigtige sted. På OnionHEN tilføjer den en side
  i spilmenuen (L2 + R3) med musikkontroller.
- **Systemværktøjer**: temperaturer, blæsertærskel, lagerplads og en filhåndtering.
- **Community**: en væg med opslag, likes og kommentarer; gruppechats;
  personer, du måske kender; spilletid og en rangliste blandt dine venner.
- **Party** med chat og stemme (Opus), spilinvitationer og brugerdefineret status
  (online, væk, forstyr ikke, usynlig).
- **Game Base**: venner, anmodninger og beskeder.
- **Privatliv**: blokering, rapportering, hvem der må skrive til dig,
  dataeksport og sletning af konto.
- **Temaer**, genereret stemningsmusik og en læsevenlig browser.
- **27 sprog**: appen følger automatisk konsollens sprog (og det kan ændres i
  Indstillinger).
- **Vælg din server**: tilføj adressen på en hvilken som helst Omega-server i
  Indstillinger; den officielle server er altid tilgængelig.

| | |
|---|---|
| ![Store](docs/screenshots/store.png) | ![Community](docs/screenshots/community.png) |
| ![Party](docs/screenshots/party.png) | ![Spilletid](docs/screenshots/stats.png) |
| ![Telefonfjernbetjening på en pc](docs/screenshots/remote-music.png) | ![Mit bibliotek fra en pc](docs/screenshots/remote-library.png) |

## Repositoryets struktur

| Mappe | |
|---|---|
| [`client/`](client) | konsolappen (C, SDL2) og en desktopversion til at udvikle den på en Mac |
| [`daemon/`](daemon) | baggrunds-payload: musikafspiller, webside til telefonfjernbetjeningen, Mit bibliotek, notifikationer under spil og (kun hvis du vælger det) genåbning af Omega, når du går tilbage til Home |
| [`onionhen-plugin/`](onionhen-plugin) | OnionHEN-plugin: en Omega-side i spilmenuen med musikkontroller |
| [`server/`](server) | Node.js-API, proxy, moderationspanel og Docker Compose til at køre en server |
| [`docs/`](docs) | arkitektur og billeder |

## Installér Omega på konsollen

Du skal bruge en jailbroken PS5 med understøttelse af homebrew: en HEN som
OnionHEN eller etaHEN eller en launcher som [websrv](https://github.com/ps5-payload-dev/websrv) med en payload-loader.
Det nemmeste er at åbne [play.omegasuite.it/installa](https://play.omegasuite.it/installa)
i konsollens browser. Alternativt kan du hente pakken fra webstedet og kopiere
`data/` til konsollen via FTP.

## Kør en server

```sh
git clone https://github.com/CristianLaporta/omega-dashboard-ps5.git
cd omega-dashboard-ps5/server
./scripts/install.sh
```

Scriptet opretter `.env` med tilfældige hemmeligheder og starter containerne.
Den komplette vejledning, med automatisk HTTPS via Caddy, findes i
[`server/README.md`](server/README.md). Gå derefter til følgende på konsollen:
**Indstillinger → Server → Tilføj en server**.

## Udvikling

- App: [`client/README.md`](client/README.md) — PS5-build med
  [ps5-payload-sdk](https://github.com/ps5-payload-dev/sdk) og et desktopbuild
  med SDL2 fra Homebrew, der styres af en kommandofil, så brugerfladen kan testes
  uden konsol. Oversættelserne ligger i `client/i18n/` (én JSON pr. sprog).
- Server: [`server/README.md`](server/README.md) — Node.js ≥ 20 og PostgreSQL,
  end-to-end-tests med `npm test`.
- Arkitektur: [`docs/architecture.md`](docs/architecture.md).

Bidrag er velkomne: se [CONTRIBUTING.md](CONTRIBUTING.md).

## Ophavsperson

Omega udvikles af **TheCriicom** — [outlinedigital.it](https://outlinedigital.it).

## Licens

Copyright © 2026 TheCriicom og Omegas bidragydere.
Omega er fri software: [GNU GPL v3 eller nyere](LICENSE). Komponenter fra
tredjepart og deres licenser er angivet i
[`client/THIRD-PARTY-NOTICES.md`](client/THIRD-PARTY-NOTICES.md).

Omega er et uafhængigt projekt og er ikke tilknyttet, godkendt eller
sponsoreret af Sony Interactive Entertainment. "PlayStation" og "PS5" er
varemærker tilhørende deres respektive ejere. Omega indeholder og
distribuerer ikke spil.
