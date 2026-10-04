<p align="center">
  <picture><source media="(prefers-color-scheme: dark)" srcset="docs/img/omega-mark.svg"><img src="docs/img/omega-mark-light.svg" width="96" alt="Omega"></picture>
</p>

<h1 align="center">Omega</h1>

<p align="center">
  Het opensource-dashboard voor PS5-consoles met homebrew: games, Store, muziek, vrienden en party op één plek.<br>
  Ontwikkeld door <a href="https://outlinedigital.it"><b>TheCriicom</b></a>.
</p>

<p align="center"><sub><a href="README.md">English</a> · <a href="README.it.md">Italiano</a> · <a href="README.ja.md">日本語</a> · <a href="README.fr.md">Français</a> · <a href="README.es.md">Español</a> · <a href="README.de.md">Deutsch</a> · <b>Nederlands</b> · <a href="README.pt-PT.md">Português (Portugal)</a> · <a href="README.pt-BR.md">Português (Brasil)</a> · <a href="README.ru.md">Русский</a> · <a href="README.ko.md">한국어</a> · <a href="README.zh-Hans.md">简体中文</a> · <a href="README.zh-Hant.md">繁體中文</a> · <a href="README.fi.md">Suomi</a> · <a href="README.sv.md">Svenska</a> · <a href="README.da.md">Dansk</a> · <a href="README.nb.md">Norsk bokmål</a> · <a href="README.pl.md">Polski</a> · <a href="README.tr.md">Türkçe</a> · <a href="README.cs.md">Čeština</a> · <a href="README.hu.md">Magyar</a> · <a href="README.el.md">Ελληνικά</a> · <a href="README.ro.md">Română</a> · <a href="README.th.md">ไทย</a> · <a href="README.vi.md">Tiếng Việt</a> · <a href="README.id.md">Bahasa Indonesia</a> · <a href="README.uk.md">Українська</a></sub></p>

<p align="center">
  <a href="https://play.omegasuite.it">Website</a> ·
  <a href="https://play.omegasuite.it/installa">Installeren</a> ·
  <a href="server/README.md">Een server hosten</a> ·
  <a href="client/README.md">Ontwikkeling</a>
</p>

![Home van Omega](docs/screenshots/home.png)

Omega is een dashboard voor homebrew-gebruikers en, als je wilt, je Home (bij de
eerste start wordt dat gevraagd): geïnstalleerde games en homebrew staan in
dezelfde rij, de Store installeert met één knop, de muziek blijft doorspelen
terwijl je gamet en je vrienden zijn altijd binnen handbereik. Omega draait als
homebrew (SDL2, software-renderer) en praat met een server die iedereen zelf kan
hosten.

## Wat zit erin

- **Home** met games en homebrew samen, dynamische achtergronden op basis van
  de artwork en direct starten (games via LncUtil, homebrew via websrv,
  ELF-payloads op de achtergrond).
- **Store** met opensource-homebrew: zoeken, schappen per categorie, stemmen,
  beoordelingen en reacties. Herkent zelf `.pkg`, `.zip` en `.elf` en
  installeert elk bestand op de juiste plek.
- **Mijn bibliotheek**: je eigen game-back-ups met een downloadlink en een
  cover, toegevoegd vanaf de console of vanaf je telefoon, of geïmporteerd uit
  een JSON-bestand (eenmalig, of gekoppeld zodat het gesynchroniseerd blijft).
  Wordt alleen op de console opgeslagen.
- **Muziek**, ook tijdens het gamen: internetradio (radio-browser), Navidrome en
  andere Subsonic-servers, USB-bestanden, elke audiolink. Het afspelen gebeurt in
  de daemon op de achtergrond (FFmpeg-build alleen voor audio), dus het stopt
  niet als je een game start.
- **Telefoonbediening**: de daemon serveert een webpagina op poort 9095. Scan de
  QR-code op de console, typ de PIN en bedien de muziek, stuur audiobestanden
  door en beheer Mijn bibliotheek vanuit de browser van elke telefoon, tablet of
  pc.
- **HEN-detectie**: bij het opstarten herkent Omega OnionHEN, etaHEN, pldmgr of
  ps5_autoloader, vertelt wat er ontbreekt en installeert en activeert na jouw
  bevestiging de bijbehorende services op de juiste plek. Op OnionHEN voegt het
  een pagina met muziekbediening toe aan het menu in de game (L2 + R3).
- **Systeemhulpmiddelen**: temperaturen, ventilatordrempel, opslag en een
  bestandsbeheerder.
- **Community**: een prikbord met berichten, likes en reacties; groepschats;
  mensen die je misschien kent; speeltijd en een ranglijst met vrienden.
- **Party** met chat en spraak (Opus), game-uitnodigingen en een eigen status
  (online, afwezig, niet storen, onzichtbaar).
- **Game Base**: vrienden, verzoeken en berichten.
- **Privacy**: blokkeren, melden, bepalen wie je berichten mag sturen,
  gegevensexport, account verwijderen.
- **Thema's**, gegenereerde sfeermuziek en een leesbrowser.
- **27 talen**: de app volgt automatisch de taal van de console (aan te passen
  in Instellingen).
- **Kies je server**: voeg in Instellingen het adres van een willekeurige
  Omega-server toe; de officiële server blijft altijd beschikbaar.

| | |
|---|---|
| ![Store](docs/screenshots/store.png) | ![Community](docs/screenshots/community.png) |
| ![Party](docs/screenshots/party.png) | ![Speeltijd](docs/screenshots/stats.png) |
| ![Telefoonbediening op een pc](docs/screenshots/remote-music.png) | ![Mijn bibliotheek vanaf een pc](docs/screenshots/remote-library.png) |

## Opbouw van de repository

| Map | |
|---|---|
| [`client/`](client) | de console-app (C, SDL2) en een desktopbuild om hem op een Mac te ontwikkelen |
| [`daemon/`](daemon) | payload op de achtergrond: muziekspeler, webpagina van de telefoonbediening, Mijn bibliotheek, meldingen tijdens het gamen en (alleen als je dat kiest) Omega opnieuw openen als je teruggaat naar de Home |
| [`onionhen-plugin/`](onionhen-plugin) | OnionHEN-plugin: een Omega-pagina in het menu in de game met muziekbediening |
| [`server/`](server) | Node.js-API, proxy, moderatiepaneel en Docker Compose om een server te hosten |
| [`docs/`](docs) | architectuur en afbeeldingen |

## Omega op de console installeren

Je hebt een gejailbreakte PS5 met homebrew-ondersteuning nodig: een HEN
zoals OnionHEN of etaHEN, of een launcher zoals [websrv](https://github.com/ps5-payload-dev/websrv)
met een payload-loader. De makkelijkste manier is
[play.omegasuite.it/installa](https://play.omegasuite.it/installa) openen in de
browser van de console. Je kunt ook het pakket van de website downloaden en
`data/` via FTP naar de console kopiëren.

## Een server hosten

```sh
git clone https://github.com/CristianLaporta/omega-dashboard-ps5.git
cd omega-dashboard-ps5/server
./scripts/install.sh
```

Het script maakt `.env` aan met willekeurige secrets en start de containers.
De volledige handleiding, met automatische HTTPS via Caddy, staat in
[`server/README.md`](server/README.md). Daarna op de console:
**Instellingen → Server → Een server toevoegen**.

## Ontwikkeling

- App: [`client/README.md`](client/README.md) — PS5-build met
  [ps5-payload-sdk](https://github.com/ps5-payload-dev/sdk) en een desktopbuild
  met SDL2 uit Homebrew, aan te sturen via een commandobestand om de interface
  zonder console te testen. De vertalingen staan in `client/i18n/` (één JSON
  per taal).
- Server: [`server/README.md`](server/README.md) — Node.js ≥ 20 en PostgreSQL,
  end-to-endtests met `npm test`.
- Architectuur: [`docs/architecture.md`](docs/architecture.md).

Bijdragen zijn welkom: zie [CONTRIBUTING.md](CONTRIBUTING.md).

## Auteur

Omega wordt ontwikkeld door **TheCriicom** — [outlinedigital.it](https://outlinedigital.it).

## Licentie

Copyright © 2026 TheCriicom en de bijdragers aan Omega.
Omega is vrije software: [GNU GPL v3 of later](LICENSE). Componenten van derden
en hun licenties staan in
[`client/THIRD-PARTY-NOTICES.md`](client/THIRD-PARTY-NOTICES.md).

Omega is een onafhankelijk project en is niet verbonden aan, goedgekeurd door
of gesponsord door Sony Interactive Entertainment. "PlayStation" en "PS5" zijn
handelsmerken van hun respectieve eigenaars. Omega bevat en verspreidt geen
games.
