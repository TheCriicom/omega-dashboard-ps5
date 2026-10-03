<p align="center">
  <img src="docs/img/mark.svg" width="88" alt="">
</p>

<h1 align="center">Omega</h1>

<p align="center">
  La dashboard open source per PS5 con homebrew: giochi, Store, amici e party in un'unica Home.
</p>

<p align="center">
  <a href="https://play.omegasuite.it">Sito</a> ·
  <a href="https://play.omegasuite.it/installa">Installa</a> ·
  <a href="server/README.md">Ospita un server</a> ·
  <a href="client/README.md">Sviluppo</a>
</p>

![Home di Omega](docs/screenshots/home.png)

Omega sostituisce la Home della console con un'interfaccia pensata per chi usa
homebrew: i giochi installati e gli homebrew stanno nella stessa fila, lo Store
installa con un tasto, gli amici sono sempre a portata di mano. Gira come
homebrew (SDL2, renderer software) e parla con un server che chiunque può
ospitare.

## Cosa c'è dentro

- **Home** con giochi e homebrew insieme, sfondi dinamici dall'artwork, avvio
  diretto (giochi tramite LncUtil, homebrew tramite websrv, payload ELF in
  background).
- **Store** di homebrew open source: ricerca, scaffali per categoria, voti,
  valutazioni e commenti. Riconosce da solo `.pkg`, `.zip` ed `.elf` e li
  installa nel posto giusto.
- **Libreria personale** da un file JSON con i backup dei propri giochi.
- **Community**: bacheca con post, mi piace e commenti; gruppi di chat;
  persone che potresti conoscere; tempo di gioco e classifica degli amici.
- **Party** con chat e voce (Opus), inviti a giocare, stato personalizzato
  (online, assente, non disturbare, invisibile).
- **Game Base**: amici, richieste e messaggi.
- **Privacy**: blocchi, segnalazioni, chi può scriverti, esportazione dei dati,
  eliminazione dell'account.
- **Temi**, musica d'atmosfera generata, browser di lettura.
- **Server a scelta**: dalle Impostazioni si aggiunge l'indirizzo di qualunque
  server Omega; quello ufficiale resta sempre disponibile.

| | |
|---|---|
| ![Store](docs/screenshots/store.png) | ![Community](docs/screenshots/community.png) |
| ![Party](docs/screenshots/party.png) | ![Tempo di gioco](docs/screenshots/stats.png) |

## Struttura

| Cartella | |
|---|---|
| [`client/`](client) | l'app per la console (C, SDL2) e una build desktop per svilupparla sul Mac |
| [`daemon/`](daemon) | payload di supporto: riapre Omega quando si torna alla Home e porta le notifiche durante il gioco |
| [`server/`](server) | API Node.js, proxy, pannello di moderazione e Docker Compose per ospitare un server |
| [`docs/`](docs) | architettura e immagini |

## Installare Omega sulla console

Serve una PS5 con jailbreak e supporto agli homebrew (un launcher come
[websrv](https://github.com/ps5-payload-dev/websrv) e un loader di payload).
La strada più semplice è la pagina [play.omegasuite.it/installa](https://play.omegasuite.it/installa),
da aprire con il browser della console. In alternativa si scarica il pacchetto
dal sito e si copia `data/` sulla console via FTP.

## Ospitare un server

```sh
git clone https://github.com/CristianLaporta/omega-dashboard-ps5.git
cd omega-dashboard-ps5/server
./scripts/install.sh
```

Lo script prepara il `.env` con segreti casuali e avvia i container. La guida
completa, con HTTPS automatico tramite Caddy, è in [`server/README.md`](server/README.md).
Poi, sulla console: **Impostazioni → Server → Aggiungi un server**.

## Sviluppare

- App: [`client/README.md`](client/README.md) — build per PS5 con
  [ps5-payload-sdk](https://github.com/ps5-payload-dev/sdk) e build desktop con
  SDL2 di Homebrew, comandabile da file per provare l'interfaccia senza console.
- Server: [`server/README.md`](server/README.md) — Node.js ≥ 20 e PostgreSQL,
  test end-to-end con `npm test`.
- Architettura: [`docs/architettura.md`](docs/architettura.md).

Le proposte sono benvenute: leggi [CONTRIBUTING.md](CONTRIBUTING.md).

## Licenza

Omega è software libero: [GNU GPL v3 o successiva](LICENSE). I componenti di
terze parti e le loro licenze sono elencati in
[`client/THIRD-PARTY-NOTICES.md`](client/THIRD-PARTY-NOTICES.md).

Omega è un progetto indipendente e non è affiliato, approvato o sponsorizzato
da Sony Interactive Entertainment. "PlayStation" e "PS5" sono marchi dei
rispettivi titolari. Omega non contiene né distribuisce giochi.
