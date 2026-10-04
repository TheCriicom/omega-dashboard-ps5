<p align="center">
  <picture><source media="(prefers-color-scheme: dark)" srcset="docs/img/omega-mark.svg"><img src="docs/img/omega-mark-light.svg" width="96" alt="Omega"></picture>
</p>

<h1 align="center">Omega</h1>

<p align="center">
  Den öppna instrumentpanelen för PS5-konsoler med homebrew: spel, Store, musik, vänner och party på ett ställe.<br>
  Utvecklad av <a href="https://outlinedigital.it"><b>TheCriicom</b></a>.
</p>

<p align="center"><sub><a href="README.md">English</a> · <a href="README.it.md">Italiano</a> · <a href="README.ja.md">日本語</a> · <a href="README.fr.md">Français</a> · <a href="README.es.md">Español</a> · <a href="README.de.md">Deutsch</a> · <a href="README.nl.md">Nederlands</a> · <a href="README.pt-PT.md">Português (Portugal)</a> · <a href="README.pt-BR.md">Português (Brasil)</a> · <a href="README.ru.md">Русский</a> · <a href="README.ko.md">한국어</a> · <a href="README.zh-Hans.md">简体中文</a> · <a href="README.zh-Hant.md">繁體中文</a> · <a href="README.fi.md">Suomi</a> · <b>Svenska</b> · <a href="README.da.md">Dansk</a> · <a href="README.nb.md">Norsk bokmål</a> · <a href="README.pl.md">Polski</a> · <a href="README.tr.md">Türkçe</a> · <a href="README.cs.md">Čeština</a> · <a href="README.hu.md">Magyar</a> · <a href="README.el.md">Ελληνικά</a> · <a href="README.ro.md">Română</a> · <a href="README.th.md">ไทย</a> · <a href="README.vi.md">Tiếng Việt</a> · <a href="README.id.md">Bahasa Indonesia</a> · <a href="README.uk.md">Українська</a></sub></p>

<p align="center">
  <a href="https://play.omegasuite.it">Webbplats</a> ·
  <a href="https://play.omegasuite.it/installa">Installera</a> ·
  <a href="server/README.md">Driva en server</a> ·
  <a href="client/README.md">Utveckling</a>
</p>

![Omegas Home](docs/screenshots/home.png)

Omega är en instrumentpanel byggd för homebrew-användare, och din Home-skärm om du
vill ha den så (den frågar vid första start): installerade spel och homebrew ligger
på samma rad, Store installerar med en knapptryckning, musiken fortsätter spela
medan du spelar och dina vänner finns alltid ett tryck bort. Omega körs som
homebrew (SDL2, mjukvarurenderare) och kommunicerar med en server som vem som
helst kan driva.

## Vad som ingår

- **Home** med spel och homebrew tillsammans, dynamiska bakgrunder från
  omslagsbilderna och direktstart (spel via LncUtil, homebrew via websrv,
  ELF-payloads i bakgrunden).
- **Store** med homebrew med öppen källkod: sökning, kategorihyllor, röster,
  betyg och kommentarer. Den känner själv igen `.pkg`, `.zip` och `.elf` och
  installerar varje fil på rätt plats.
- **Mitt bibliotek**: dina egna spelsäkerhetskopior med nedladdningslänk och
  omslag, tillagda från konsolen eller från mobilen, eller importerade från en
  JSON-fil (en gång, eller länkad så att den hålls synkad). Lagras bara på
  konsolen.
- **Musik**, även under spel: internetradio (radio-browser), Navidrome och andra
  Subsonic-servrar, USB-filer, valfri ljudlänk. Uppspelningen körs i
  bakgrundsdaemonen (FFmpeg-bygge med enbart ljud), så den stannar inte när du
  startar ett spel.
- **Mobilfjärrkontroll**: daemonen serverar en webbsida på port 9095. Skanna
  QR-koden som visas på konsolen, skriv in PIN-koden och styr musiken, skicka
  ljudfiler och hantera Mitt bibliotek från valfri mobil, surfplatta eller
  dator med webbläsare.
- **HEN-identifiering**: vid start känner Omega igen OnionHEN, etaHEN, pldmgr
  eller ps5_autoloader, talar om vad som saknas och installerar och aktiverar,
  efter att du bekräftat, dess tjänster på rätt plats. På OnionHEN lägger den till
  en sida i spelmenyn (L2 + R3) med musikkontroller.
- **Systemverktyg**: temperaturer, fantröskel, lagring och en filhanterare.
- **Community**: en vägg med inlägg, gilla-markeringar och kommentarer;
  gruppchattar; personer du kanske känner; speltid och en topplista bland vännerna.
- **Party** med chatt och röst (Opus), spelinbjudningar och anpassad status
  (online, borta, stör ej, osynlig).
- **Game Base**: vänner, förfrågningar och meddelanden.
- **Integritet**: blockering, anmälan, vem som får skicka meddelanden till dig,
  dataexport och radering av konto.
- **Teman**, genererad stämningsmusik och en läsvänlig webbläsare.
- **27 språk**: appen följer automatiskt konsolens språk (och kan ändras i
  Inställningar).
- **Välj din server**: lägg till adressen till valfri Omega-server i
  Inställningar; den officiella servern finns alltid kvar.

| | |
|---|---|
| ![Store](docs/screenshots/store.png) | ![Community](docs/screenshots/community.png) |
| ![Party](docs/screenshots/party.png) | ![Speltid](docs/screenshots/stats.png) |
| ![Mobilfjärrkontroll på en dator](docs/screenshots/remote-music.png) | ![Mitt bibliotek från en dator](docs/screenshots/remote-library.png) |

## Repots struktur

| Mapp | |
|---|---|
| [`client/`](client) | konsolappen (C, SDL2) och en skrivbordsversion för att utveckla den på en Mac |
| [`daemon/`](daemon) | bakgrunds-payload: musikspelare, webbsida för mobilfjärrkontrollen, Mitt bibliotek, notiser under spel och (bara om du väljer det) att öppna Omega igen när du går tillbaka till Home |
| [`onionhen-plugin/`](onionhen-plugin) | OnionHEN-plugin: en Omega-sida i spelmenyn med musikkontroller |
| [`server/`](server) | Node.js-API, proxy, modereringspanel och Docker Compose för att driva en server |
| [`docs/`](docs) | arkitektur och bilder |

## Installera Omega på konsolen

Du behöver en jailbreakad PS5 med stöd för homebrew: en HEN som OnionHEN eller
etaHEN, eller en startare som [websrv](https://github.com/ps5-payload-dev/websrv) med en payload-laddare.
Enklast är att öppna [play.omegasuite.it/installa](https://play.omegasuite.it/installa)
i konsolens webbläsare. Du kan också ladda ner paketet från webbplatsen och
kopiera `data/` till konsolen via FTP.

## Driva en server

```sh
git clone https://github.com/TheCriicom/omega-dashboard-ps5.git
cd omega-dashboard-ps5/server
./scripts/install.sh
```

Skriptet skapar `.env` med slumpmässiga hemligheter och startar containrarna.
Den fullständiga guiden, med automatisk HTTPS via Caddy, finns i
[`server/README.md`](server/README.md). Gå sedan till följande på konsolen:
**Inställningar → Server → Lägg till en server**.

## Utveckling

- App: [`client/README.md`](client/README.md) — PS5-bygge med
  [ps5-payload-sdk](https://github.com/ps5-payload-dev/sdk) och ett skrivbordsbygge
  med SDL2 från Homebrew, som styrs av en kommandofil så att gränssnittet kan
  testas utan konsol. Översättningarna finns i `client/i18n/` (en JSON per språk).
- Server: [`server/README.md`](server/README.md) — Node.js ≥ 20 och PostgreSQL,
  end-to-end-tester med `npm test`.
- Arkitektur: [`docs/architecture.md`](docs/architecture.md).

Bidrag är välkomna: se [CONTRIBUTING.md](CONTRIBUTING.md).

## Upphovsperson

Omega utvecklas av **TheCriicom** — [outlinedigital.it](https://outlinedigital.it).

## Licens

Copyright © 2026 TheCriicom och Omegas bidragsgivare.
Omega är fri programvara: [GNU GPL v3 eller senare](LICENSE). Komponenter från
tredje part och deras licenser listas i
[`client/THIRD-PARTY-NOTICES.md`](client/THIRD-PARTY-NOTICES.md).

Omega är ett oberoende projekt och är inte anslutet till, godkänt eller
sponsrat av Sony Interactive Entertainment. "PlayStation" och "PS5" är
varumärken som tillhör respektive ägare. Omega innehåller inte och
distribuerar inte spel.
