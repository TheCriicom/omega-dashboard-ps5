<p align="center">
  <picture><source media="(prefers-color-scheme: dark)" srcset="docs/img/omega-mark.svg"><img src="docs/img/omega-mark-light.svg" width="96" alt="Omega"></picture>
</p>

<h1 align="center">Omega</h1>

<p align="center">
  La dashboard open source per PS5 con homebrew: giochi, Store, musica, amici e party in un unico posto.<br>
  Sviluppata da <a href="https://outlinedigital.it"><b>TheCriicom</b></a>.
</p>

<p align="center"><sub><a href="README.md">English</a> · <b>Italiano</b> · <a href="README.ja.md">日本語</a> · <a href="README.fr.md">Français</a> · <a href="README.es.md">Español</a> · <a href="README.de.md">Deutsch</a> · <a href="README.nl.md">Nederlands</a> · <a href="README.pt-PT.md">Português (Portugal)</a> · <a href="README.pt-BR.md">Português (Brasil)</a> · <a href="README.ru.md">Русский</a> · <a href="README.ko.md">한국어</a> · <a href="README.zh-Hans.md">简体中文</a> · <a href="README.zh-Hant.md">繁體中文</a> · <a href="README.fi.md">Suomi</a> · <a href="README.sv.md">Svenska</a> · <a href="README.da.md">Dansk</a> · <a href="README.nb.md">Norsk bokmål</a> · <a href="README.pl.md">Polski</a> · <a href="README.tr.md">Türkçe</a> · <a href="README.cs.md">Čeština</a> · <a href="README.hu.md">Magyar</a> · <a href="README.el.md">Ελληνικά</a> · <a href="README.ro.md">Română</a> · <a href="README.th.md">ไทย</a> · <a href="README.vi.md">Tiếng Việt</a> · <a href="README.id.md">Bahasa Indonesia</a> · <a href="README.uk.md">Українська</a></sub></p>

<p align="center">
  <a href="https://play.omegasuite.it">Sito</a> ·
  <a href="https://play.omegasuite.it/installa">Installa</a> ·
  <a href="server/README.it.md">Ospita un server</a> ·
  <a href="client/README.md">Sviluppo</a>
</p>

![Home di Omega](docs/screenshots/home.png)

Omega è una dashboard pensata per chi usa homebrew, e diventa la tua Home se
vuoi (te lo chiede al primo avvio): i giochi installati e gli homebrew stanno
nella stessa fila, lo Store installa con un tasto, la musica continua mentre
giochi e gli amici sono sempre a portata di mano. Gira come homebrew (SDL2,
renderer software) e parla con un server che chiunque può ospitare.

## Cosa c'è dentro

- **Home** con giochi e homebrew insieme, sfondi dinamici dall'artwork, avvio
  diretto (giochi tramite LncUtil, homebrew tramite websrv, payload ELF in
  background).
- **Store** di homebrew open source: ricerca, scaffali per categoria, voti,
  valutazioni e commenti. Riconosce da solo `.pkg`, `.zip` ed `.elf` e li
  installa nel posto giusto.
- **La mia libreria**: i backup dei tuoi giochi con link di download e copertina,
  aggiunti dalla console o dal telefono, oppure importati da un file JSON
  (una volta sola, o collegato in modo che resti sincronizzato). Salvata solo
  sulla console.
- **Musica**, anche durante i giochi: radio internet (radio-browser), Navidrome e
  altri server Subsonic, file su USB, qualsiasi link audio. La riproduzione gira
  nel daemon in background (build solo audio di FFmpeg), quindi non si ferma
  quando avvii un gioco.
- **Telecomando dal telefono**: il daemon serve una pagina web sulla porta 9095.
  Inquadra il QR code mostrato sulla console, digita il PIN e controlla la
  musica, invia file audio e gestisci La mia libreria da qualsiasi browser di
  telefono, tablet o PC.
- **Rilevamento HEN**: all'avvio Omega riconosce OnionHEN, etaHEN, pldmgr o
  ps5_autoloader, ti dice cosa manca e, dopo la tua conferma, installa e abilita
  i suoi servizi nel posto giusto. Su OnionHEN aggiunge al menu in gioco
  (L2 + R3) una pagina con i controlli della musica.
- **Strumenti di sistema**: temperature, soglia della ventola, spazio di
  archiviazione e un file manager.
- **Community**: bacheca con post, mi piace e commenti; gruppi di chat;
  persone che potresti conoscere; tempo di gioco e classifica degli amici.
- **Party** con chat e voce (Opus), inviti a giocare, stato personalizzato
  (online, assente, non disturbare, invisibile).
- **Game Base**: amici, richieste e messaggi.
- **Privacy**: blocchi, segnalazioni, chi può scriverti, esportazione dei dati,
  eliminazione dell'account.
- **Temi**, musica d'atmosfera generata, browser di lettura.
- **27 lingue**: l'app usa automaticamente la lingua della console (si può cambiare dalle Impostazioni).
- **Server a scelta**: dalle Impostazioni si aggiunge l'indirizzo di qualunque
  server Omega; quello ufficiale resta sempre disponibile.

| | |
|---|---|
| ![Store](docs/screenshots/store.png) | ![Community](docs/screenshots/community.png) |
| ![Party](docs/screenshots/party.png) | ![Tempo di gioco](docs/screenshots/stats.png) |
| ![Telecomando dal telefono su PC](docs/screenshots/remote-music.png) | ![La mia libreria da PC](docs/screenshots/remote-library.png) |

## Struttura

| Cartella | |
|---|---|
| [`client/`](client) | l'app per la console (C, SDL2) e una build desktop per svilupparla sul Mac |
| [`daemon/`](daemon) | payload in background: player musicale, pagina web del telecomando, La mia libreria, notifiche durante il gioco e (solo se lo scegli) la riapertura di Omega quando si torna alla Home |
| [`onionhen-plugin/`](onionhen-plugin) | plugin per OnionHEN: una pagina di Omega nel menu in gioco con i controlli della musica |
| [`server/`](server) | API Node.js, proxy, pannello di moderazione e Docker Compose per ospitare un server |
| [`docs/`](docs) | architettura e immagini |

## Installare Omega sulla console

Serve una PS5 con jailbreak e supporto agli homebrew: un HEN come OnionHEN o
etaHEN, oppure un launcher come [websrv](https://github.com/ps5-payload-dev/websrv)
con un loader di payload.
La strada più semplice è la pagina [play.omegasuite.it/installa](https://play.omegasuite.it/installa),
da aprire con il browser della console. In alternativa si scarica il pacchetto
dal sito e si copia `data/` sulla console via FTP.

## Ospitare un server

```sh
git clone https://github.com/TheCriicom/omega-dashboard-ps5.git
cd omega-dashboard-ps5/server
./scripts/install.sh
```

Lo script prepara il `.env` con segreti casuali e avvia i container. La guida
completa, con HTTPS automatico tramite Caddy, è in [`server/README.it.md`](server/README.it.md).
Poi, sulla console: **Impostazioni → Server → Aggiungi un server**.

## Sviluppare

- App: [`client/README.md`](client/README.md) — build per PS5 con
  [ps5-payload-sdk](https://github.com/ps5-payload-dev/sdk) e build desktop con
  SDL2 di Homebrew, comandabile da file per provare l'interfaccia senza console.
  Le traduzioni sono in `client/i18n/` (un JSON per lingua).
- Server: [`server/README.it.md`](server/README.it.md) — Node.js ≥ 20 e PostgreSQL,
  test end-to-end con `npm test`.
- Architettura: [`docs/architettura.md`](docs/architettura.md).

Le proposte sono benvenute: leggi [CONTRIBUTING.md](CONTRIBUTING.md).

## Autore

Omega è sviluppato da **TheCriicom** — [outlinedigital.it](https://outlinedigital.it).

## Licenza

Copyright © 2026 TheCriicom e i contributori di Omega.
Omega è software libero: [GNU GPL v3 o successiva](LICENSE). I componenti di
terze parti e le loro licenze sono elencati in
[`client/THIRD-PARTY-NOTICES.md`](client/THIRD-PARTY-NOTICES.md).

Omega è un progetto indipendente e non è affiliato, approvato o sponsorizzato
da Sony Interactive Entertainment. "PlayStation" e "PS5" sono marchi dei
rispettivi titolari. Omega non contiene né distribuisce giochi.
