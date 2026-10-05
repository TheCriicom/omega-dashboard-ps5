# Omega per OnionHEN

Plugin per OnionHEN (id `OMGA00001`): aggiunge una
pagina "Omega" al Toolbox, che si apre anche durante il gioco con la scorciatoia
del Toolbox (Omega propone L2 + R3). Mostra il brano in corso con riproduci/pausa,
avanti, indietro e volume, quanti amici sono online e, se sei in un party, il
party vocale: chi parla, microfono acceso/spento ed «Esci dal party».

Non parla con il server: dati e comandi passano dal demone `omega_redirect`
su `127.0.0.1:9095`. La pagina di OnionHEN non si ridisegna mentre è aperta,
quindi si aggiorna riaprendola.

La UI lo installa da sola in `/data/OnionHEN/plugins/OMGA00001.elf` (con il file
`.auto_start`) quando trova OnionHEN e l'utente conferma.

## Compilare

```sh
PS5_PAYLOAD_SDK=$HOME/ps5-payload-sdk ./build.sh
```

L'SDK dei plugin di OnionHEN viene scaricato da CMake a un commit fisso; per
usarne una copia locale imposta `ONIONHEN_PLUGIN_SDK_SOURCE`. Il risultato è
`bin/OMGA00001.elf`.

## Crediti

Omega è sviluppato da **TheCriicom** ([outlinedigital.it](https://outlinedigital.it)).
Licenza GPL-3.0-or-later.
