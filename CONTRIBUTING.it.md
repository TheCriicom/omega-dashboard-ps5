<sub>[English](CONTRIBUTING.md) · **Italiano**</sub>

# Contribuire a Omega

Grazie per l'interesse. Qualche indicazione per lavorare bene insieme.

## Prima di iniziare

- Per un bug apri una issue con i passi per riprodurlo, la versione di Omega
  (Impostazioni) e, se c'entra la console, le righe utili di
  `/data/Omega/omega-ui.log`.
- Per una funzione nuova apri prima una issue: ne parliamo e si evita lavoro
  buttato.

## Codice

- **App** (`client/`): C17, stile compatto come il codice esistente, commenti in
  italiano e solo dove spiegano il perché. Prova le modifiche con la build
  desktop (`client/desktop/build-desktop.sh`) e, se tocchi chiamate di sistema,
  anche sulla console.
- **Server** (`server/api/`): Node.js ≥ 20, CommonJS, nessuna dipendenza nuova
  senza un buon motivo. Ogni rotta nuova va in `config/endpoints.json`; ogni
  modifica allo schema è una nuova migrazione (quelle esistenti non si toccano).
- I testi visibili all'utente si scrivono in italiano nel codice, dentro `_()`; le traduzioni
  stanno in `client/i18n/<lingua>.json`. Dopo aver aggiunto testi lancia
  `node client/tools/i18n-extract.mjs` e mantieni identici gli specificatori printf in ogni
  traduzione. Nuove lingue sono benvenute.
- `npm test` in `server/api` deve passare (vedi `server/README.it.md`).

## Pull request

- Una PR per argomento, con una descrizione di cosa cambia e perché.
- Includi screenshot se cambia l'interfaccia (la build desktop ha il comando
  `shot` nel file comandi).
- Contribuendo accetti che il tuo codice sia distribuito con licenza
  GPL-3.0-or-later.

## Cosa non accettiamo

Codice per aggirare protezioni dei giochi a fini di pirateria, distribuzione di
giochi o software commerciale, raccolta di credenziali o dati non necessari.
