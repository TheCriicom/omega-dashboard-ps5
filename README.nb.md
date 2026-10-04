<p align="center">
  <picture><source media="(prefers-color-scheme: dark)" srcset="docs/img/omega-mark.svg"><img src="docs/img/omega-mark-light.svg" width="96" alt="Omega"></picture>
</p>

<h1 align="center">Omega</h1>

<p align="center">
  Dashbordet med åpen kildekode for PS5-konsoller med homebrew: spill, Store, musikk, venner og party samlet på ett sted.<br>
  Utviklet av <a href="https://outlinedigital.it"><b>TheCriicom</b></a>.
</p>

<p align="center"><sub><a href="README.md">English</a> · <a href="README.it.md">Italiano</a> · <a href="README.ja.md">日本語</a> · <a href="README.fr.md">Français</a> · <a href="README.es.md">Español</a> · <a href="README.de.md">Deutsch</a> · <a href="README.nl.md">Nederlands</a> · <a href="README.pt-PT.md">Português (Portugal)</a> · <a href="README.pt-BR.md">Português (Brasil)</a> · <a href="README.ru.md">Русский</a> · <a href="README.ko.md">한국어</a> · <a href="README.zh-Hans.md">简体中文</a> · <a href="README.zh-Hant.md">繁體中文</a> · <a href="README.fi.md">Suomi</a> · <a href="README.sv.md">Svenska</a> · <a href="README.da.md">Dansk</a> · <b>Norsk bokmål</b> · <a href="README.pl.md">Polski</a> · <a href="README.tr.md">Türkçe</a> · <a href="README.cs.md">Čeština</a> · <a href="README.hu.md">Magyar</a> · <a href="README.el.md">Ελληνικά</a> · <a href="README.ro.md">Română</a> · <a href="README.th.md">ไทย</a> · <a href="README.vi.md">Tiếng Việt</a> · <a href="README.id.md">Bahasa Indonesia</a> · <a href="README.uk.md">Українська</a></sub></p>

<p align="center">
  <a href="https://play.omegasuite.it">Nettsted</a> ·
  <a href="https://play.omegasuite.it/installa">Installer</a> ·
  <a href="server/README.md">Drift en server</a> ·
  <a href="client/README.md">Utvikling</a>
</p>

![Omegas Home](docs/screenshots/home.png)

Omega er et dashbord laget for homebrew-brukere, og Home-skjermen din hvis du
ønsker det (det spør ved første oppstart): installerte spill og homebrew står i
samme rad, Store installerer med ett knappetrykk, musikken fortsetter å spille
mens du spiller, og vennene dine er alltid bare ett trykk unna. Omega kjører som
homebrew (SDL2, programvarerenderer) og kommuniserer med en server som hvem
som helst kan drifte.

## Hva som er inkludert

- **Home** med spill og homebrew samlet, dynamiske bakgrunner fra
  coverbildene og direkte oppstart (spill via LncUtil, homebrew via websrv,
  ELF-payloads i bakgrunnen).
- **Store** med homebrew med åpen kildekode: søk, kategorihyller, stemmer,
  vurderinger og kommentarer. Den gjenkjenner selv `.pkg`, `.zip` og `.elf` og
  installerer hver fil på riktig sted.
- **Mitt bibliotek**: egne spillkopier med nedlastingslenke og cover, lagt til
  fra konsollen eller fra telefonen, eller importert fra en JSON-fil (én gang,
  eller lenket slik at den holdes synkronisert). Lagres bare på konsollen.
- **Musikk**, også under spill: nettradio (radio-browser), Navidrome og andre
  Subsonic-servere, USB-filer, en hvilken som helst lydlenke. Avspillingen kjører
  i bakgrunnsdaemonen (FFmpeg-bygg kun med lyd), så den stopper ikke når du
  starter et spill.
- **Telefonfjernkontroll**: daemonen serverer en nettside på port 9095. Skann
  QR-koden som vises på konsollen, skriv inn PIN-koden og styr musikken, send
  lydfiler og administrer Mitt bibliotek fra en hvilken som helst telefon,
  nettbrett eller PC-nettleser.
- **HEN-gjenkjenning**: ved oppstart gjenkjenner Omega OnionHEN, etaHEN, pldmgr
  eller ps5_autoloader, forteller hva som mangler og installerer og aktiverer,
  etter at du har bekreftet, tjenestene på riktig sted. På OnionHEN legger den til
  en side i spillmenyen (L2 + R3) med musikkontroller.
- **Systemverktøy**: temperaturer, viftegrense, lagring og filbehandler.
- **Community**: en vegg med innlegg, likerklikk og kommentarer; gruppechatter;
  personer du kanskje kjenner; spilletid og en toppliste blant vennene.
- **Party** med chat og tale (Opus), spillinvitasjoner og egendefinert status
  (pålogget, borte, ikke forstyrr, usynlig).
- **Game Base**: venner, forespørsler og meldinger.
- **Personvern**: blokkering, rapportering, hvem som kan sende deg meldinger,
  dataeksport og sletting av konto.
- **Temaer**, generert stemningsmusikk og en lesevennlig nettleser.
- **27 språk**: appen følger automatisk konsollens språk (og språket kan endres
  i Innstillinger).
- **Velg din server**: legg til adressen til en hvilken som helst Omega-server i
  Innstillinger; den offisielle serveren er alltid tilgjengelig.

| | |
|---|---|
| ![Store](docs/screenshots/store.png) | ![Community](docs/screenshots/community.png) |
| ![Party](docs/screenshots/party.png) | ![Spilletid](docs/screenshots/stats.png) |
| ![Telefonfjernkontroll på en PC](docs/screenshots/remote-music.png) | ![Mitt bibliotek fra en PC](docs/screenshots/remote-library.png) |

## Struktur i repositoriet

| Mappe | |
|---|---|
| [`client/`](client) | konsollappen (C, SDL2) og en skrivebordsversjon for å utvikle den på en Mac |
| [`daemon/`](daemon) | bakgrunns-payload: musikkspiller, nettside for telefonfjernkontrollen, Mitt bibliotek, varsler mens du spiller og (bare hvis du velger det) gjenåpning av Omega når du går tilbake til Home |
| [`onionhen-plugin/`](onionhen-plugin) | OnionHEN-plugin: en Omega-side i spillmenyen med musikkontroller |
| [`server/`](server) | Node.js-API, proxy, modereringspanel og Docker Compose for å drifte en server |
| [`docs/`](docs) | arkitektur og bilder |

## Installere Omega på konsollen

Du trenger en jailbreaket PS5 med støtte for homebrew: en HEN som OnionHEN eller
etaHEN, eller en oppstarter som [websrv](https://github.com/ps5-payload-dev/websrv) med en payload-laster.
Det enkleste er å åpne [play.omegasuite.it/installa](https://play.omegasuite.it/installa)
i konsollens nettleser. Alternativt kan du laste ned pakken fra nettstedet og
kopiere `data/` til konsollen via FTP.

## Drifte en server

```sh
git clone https://github.com/CristianLaporta/omega-dashboard-ps5.git
cd omega-dashboard-ps5/server
./scripts/install.sh
```

Skriptet oppretter `.env` med tilfeldige hemmeligheter og starter containerne.
Den fullstendige veiledningen, med automatisk HTTPS via Caddy, finner du i
[`server/README.md`](server/README.md). Gå deretter til følgende på konsollen:
**Innstillinger → Server → Legg til en server**.

## Utvikling

- App: [`client/README.md`](client/README.md) — PS5-bygg med
  [ps5-payload-sdk](https://github.com/ps5-payload-dev/sdk) og et skrivebordsbygg
  med SDL2 fra Homebrew, styrt av en kommandofil slik at grensesnittet kan testes
  uten konsoll. Oversettelsene ligger i `client/i18n/` (én JSON per språk).
- Server: [`server/README.md`](server/README.md) — Node.js ≥ 20 og PostgreSQL,
  ende-til-ende-tester med `npm test`.
- Arkitektur: [`docs/architecture.md`](docs/architecture.md).

Bidrag er velkomne: se [CONTRIBUTING.md](CONTRIBUTING.md).

## Opphavsperson

Omega utvikles av **TheCriicom** — [outlinedigital.it](https://outlinedigital.it).

## Lisens

Copyright © 2026 TheCriicom og Omegas bidragsytere.
Omega er fri programvare: [GNU GPL v3 eller nyere](LICENSE). Komponenter fra
tredjeparter og lisensene deres er oppført i
[`client/THIRD-PARTY-NOTICES.md`](client/THIRD-PARTY-NOTICES.md).

Omega er et uavhengig prosjekt og er ikke tilknyttet, godkjent eller
sponset av Sony Interactive Entertainment. «PlayStation» og «PS5» er
varemerker som tilhører sine respektive eiere. Omega inneholder ikke og
distribuerer ikke spill.
