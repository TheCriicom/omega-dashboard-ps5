<p align="center">
  <picture><source media="(prefers-color-scheme: dark)" srcset="docs/img/omega-mark.svg"><img src="docs/img/omega-mark-light.svg" width="96" alt="Omega"></picture>
</p>

<h1 align="center">Omega</h1>

<p align="center">
  Das Open-Source-Dashboard für Homebrew-fähige PS5-Konsolen: Spiele, Store, Musik, Freunde und Party an einem Ort.<br>
  Entwickelt von <a href="https://outlinedigital.it"><b>TheCriicom</b></a>.
</p>

<p align="center"><sub><a href="README.md">English</a> · <a href="README.it.md">Italiano</a> · <a href="README.ja.md">日本語</a> · <a href="README.fr.md">Français</a> · <a href="README.es.md">Español</a> · <b>Deutsch</b> · <a href="README.nl.md">Nederlands</a> · <a href="README.pt-PT.md">Português (Portugal)</a> · <a href="README.pt-BR.md">Português (Brasil)</a> · <a href="README.ru.md">Русский</a> · <a href="README.ko.md">한국어</a> · <a href="README.zh-Hans.md">简体中文</a> · <a href="README.zh-Hant.md">繁體中文</a> · <a href="README.fi.md">Suomi</a> · <a href="README.sv.md">Svenska</a> · <a href="README.da.md">Dansk</a> · <a href="README.nb.md">Norsk bokmål</a> · <a href="README.pl.md">Polski</a> · <a href="README.tr.md">Türkçe</a> · <a href="README.cs.md">Čeština</a> · <a href="README.hu.md">Magyar</a> · <a href="README.el.md">Ελληνικά</a> · <a href="README.ro.md">Română</a> · <a href="README.th.md">ไทย</a> · <a href="README.vi.md">Tiếng Việt</a> · <a href="README.id.md">Bahasa Indonesia</a> · <a href="README.uk.md">Українська</a></sub></p>

<p align="center">
  <a href="https://play.omegasuite.it">Website</a> ·
  <a href="https://play.omegasuite.it/installa">Installieren</a> ·
  <a href="server/README.md">Server hosten</a> ·
  <a href="client/README.md">Entwicklung</a>
</p>

![Omega-Home](docs/screenshots/home.png)

Omega ist ein Dashboard für Homebrew-Nutzer und, wenn du willst, deine Home
(beim ersten Start wirst du gefragt): Installierte Spiele und Homebrew stehen in
derselben Reihe, der Store installiert mit einem Tastendruck, die Musik läuft
beim Spielen weiter, und deine Freunde sind immer nur einen Klick entfernt.
Omega läuft als Homebrew (SDL2, Software-Renderer) und kommuniziert mit einem
Server, den jeder selbst hosten kann.

## Funktionen

- **Home** mit Spielen und Homebrew zusammen, dynamischen Hintergründen aus dem
  Artwork und Direktstart (Spiele über LncUtil, Homebrew über websrv,
  ELF-Payloads im Hintergrund).
- **Store** für Open-Source-Homebrew: Suche, Regale nach Kategorie, Votes,
  Bewertungen und Kommentare. Er erkennt `.pkg`, `.zip` und `.elf` selbstständig
  und installiert jede Datei am richtigen Ort.
- **Meine Bibliothek**: deine eigenen Spiele-Backups mit Download-Link und
  Cover, hinzugefügt von der Konsole oder vom Smartphone, oder aus einer
  JSON-Datei importiert (einmalig oder verknüpft, sodass sie synchron bleibt).
  Wird nur auf der Konsole gespeichert.
- **Musik**, auch während des Spielens: Internetradio (radio-browser), Navidrome
  und andere Subsonic-Server, USB-Dateien, beliebige Audio-Links. Die Wiedergabe
  läuft im Hintergrund-Daemon (FFmpeg-Build nur für Audio) und stoppt deshalb
  nicht, wenn du ein Spiel startest.
- **Smartphone-Fernbedienung**: Der Daemon stellt eine Webseite auf Port 9095
  bereit. Scanne den auf der Konsole angezeigten QR-Code, gib die PIN ein und
  steuere die Musik, sende Audiodateien und verwalte Meine Bibliothek in jedem
  Browser auf Smartphone, Tablet oder PC.
- **HEN-Erkennung**: Beim Start erkennt Omega OnionHEN, etaHEN, pldmgr oder
  ps5_autoloader, sagt dir, was fehlt, und installiert und aktiviert nach
  deiner Bestätigung dessen Dienste am richtigen Ort. Unter OnionHEN fügt es
  dem Spielmenü (L2 + R3) eine Seite mit Musiksteuerung hinzu.
- **Systemwerkzeuge**: Temperaturen, Lüfterschwelle, Speicher und ein
  Dateimanager.
- **Community**: eine Pinnwand mit Beiträgen, Likes und Kommentaren;
  Gruppenchats; Personen, die du kennen könntest; Spielzeit und eine
  Freunde-Rangliste.
- **Party** mit Chat und Sprache (Opus), Spieleinladungen und eigenem Status
  (online, abwesend, nicht stören, unsichtbar).
- **Game Base**: Freunde, Anfragen und Nachrichten.
- **Privatsphäre**: Blockieren, Melden, wer dir schreiben darf, Datenexport,
  Kontolöschung.
- **Themes**, generierte Ambient-Musik, ein Lese-Browser.
- **27 Sprachen**: Die App übernimmt automatisch die Sprache der Konsole (in den
  Einstellungen änderbar).
- **Freie Serverwahl**: Füge in den Einstellungen die Adresse eines beliebigen
  Omega-Servers hinzu; der offizielle Server bleibt immer verfügbar.

| | |
|---|---|
| ![Store](docs/screenshots/store.png) | ![Community](docs/screenshots/community.png) |
| ![Party](docs/screenshots/party.png) | ![Spielzeit](docs/screenshots/stats.png) |
| ![Smartphone-Fernbedienung am PC](docs/screenshots/remote-music.png) | ![Meine Bibliothek am PC](docs/screenshots/remote-library.png) |

## Aufbau des Repositorys

| Ordner | |
|---|---|
| [`client/`](client) | die Konsolen-App (C, SDL2) und ein Desktop-Build, um sie auf dem Mac zu entwickeln |
| [`daemon/`](daemon) | Hintergrund-Payload: Musikplayer, Webseite der Fernbedienung, Meine Bibliothek, Benachrichtigungen während des Spielens und (nur wenn du es wählst) das erneute Öffnen von Omega, wenn du zur Home zurückkehrst |
| [`onionhen-plugin/`](onionhen-plugin) | OnionHEN-Plugin: eine Omega-Seite im Spielmenü mit Musiksteuerung |
| [`server/`](server) | Node.js-API, Proxy, Moderationspanel und Docker Compose zum Hosten eines Servers |
| [`docs/`](docs) | Architektur und Bilder |

## Omega auf der Konsole installieren

Du brauchst eine PS5 mit Jailbreak und Homebrew-Unterstützung: ein HEN
wie OnionHEN oder etaHEN, oder einen Launcher wie [websrv](https://github.com/ps5-payload-dev/websrv)
mit einem Payload-Loader. Am einfachsten öffnest du
[play.omegasuite.it/installa](https://play.omegasuite.it/installa) im Browser
der Konsole. Alternativ lädst du das Paket von der Website herunter und
kopierst `data/` per FTP auf die Konsole.

## Einen Server hosten

```sh
git clone https://github.com/CristianLaporta/omega-dashboard-ps5.git
cd omega-dashboard-ps5/server
./scripts/install.sh
```

Das Skript erstellt die `.env` mit zufälligen Secrets und startet die
Container. Die vollständige Anleitung, inklusive automatischem HTTPS über
Caddy, steht in [`server/README.md`](server/README.md). Danach auf der Konsole:
**Einstellungen → Server → Server hinzufügen**.

## Entwicklung

- App: [`client/README.md`](client/README.md) — PS5-Build mit
  [ps5-payload-sdk](https://github.com/ps5-payload-dev/sdk) und ein Desktop-Build
  mit SDL2 aus Homebrew, steuerbar über eine Befehlsdatei, um die Oberfläche
  ohne Konsole zu testen. Die Übersetzungen liegen in `client/i18n/` (ein JSON
  pro Sprache).
- Server: [`server/README.md`](server/README.md) — Node.js ≥ 20 und PostgreSQL,
  End-to-End-Tests mit `npm test`.
- Architektur: [`docs/architecture.md`](docs/architecture.md).

Beiträge sind willkommen: siehe [CONTRIBUTING.md](CONTRIBUTING.md).

## Autor

Omega wird von **TheCriicom** entwickelt — [outlinedigital.it](https://outlinedigital.it).

## Lizenz

Copyright © 2026 TheCriicom und die Omega-Mitwirkenden.
Omega ist freie Software: [GNU GPL v3 oder später](LICENSE). Komponenten von
Drittanbietern und ihre Lizenzen sind in
[`client/THIRD-PARTY-NOTICES.md`](client/THIRD-PARTY-NOTICES.md) aufgeführt.

Omega ist ein unabhängiges Projekt und steht in keiner Verbindung zu Sony
Interactive Entertainment; es wird von Sony Interactive Entertainment weder
unterstützt noch gesponsert. „PlayStation“ und „PS5“ sind Marken ihrer
jeweiligen Inhaber. Omega enthält und verbreitet keine Spiele.
