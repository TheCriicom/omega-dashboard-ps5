# Omega Installer

Payload che installa (o aggiorna) Omega sulla console. Si manda con qualsiasi
caricatore di ELF: la pagina [play.omegasuite.it/installa](https://play.omegasuite.it/installa)
aperta nel browser della PS5 (websrv, `/elfldr`), oppure `nc <ip> 9021 < omega-installer.elf`.

Scarica il manifest degli aggiornamenti, ne verifica la firma Ed25519 e lo
SHA-256 di ogni file, poi mette `OmegaUI.elf`, `homebrew.js` e l'icona in
`/data/homebrew/OmegaUI/` e i payload (`omega_redirect.elf`, `OMGA00001.elf`,
`patchdl.elf`) in `/data/homebrew/OmegaUI/payloads/`. Il servizio in
background non si attiva qui: al primo avvio Omega riconosce il caricatore
(OnionHEN, etaHEN, Payload Manager...) e lo configura dopo la conferma.

Scarica prima in https con SceHttp; se non riesce riprova in http semplice
con un socket suo (il server serve `/updates` e `/download` anche così), dato
che l'integrità la garantiscono firma e SHA-256. Ogni passo finisce in
`/data/Omega/omega-installer.log`, che fa parte della diagnostica.

## Compilare

```sh
export LLVM_CONFIG=$(brew --prefix llvm@18)/bin/llvm-config
$PS5_PAYLOAD_SDK/bin/prospero-cmake -S installer -B installer/build
cmake --build installer/build
```

Il risultato è `bin/omega-installer.elf`. Le traduzioni (`source/i18n_data.c`)
le genera `client/tools/i18n-gen.mjs` dagli stessi cataloghi della UI.

## Crediti

Omega è sviluppato da **TheCriicom** ([outlinedigital.it](https://outlinedigital.it)).
Licenza GPL-3.0-or-later. Firma e verifica con [Monocypher](https://monocypher.org)
(CC0 / BSD-2-Clause).
