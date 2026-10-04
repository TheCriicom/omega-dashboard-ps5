#!/usr/bin/env bash
# Compila la Omega UI (SDL2) → ELF per il Payload Manager.
#   PS5_PAYLOAD_SDK=$HOME/ps5-payload-sdk ./build.sh
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
[ -n "${PS5_PAYLOAD_SDK:-}" ] || { echo "PS5_PAYLOAD_SDK non impostata"; exit 1; }
if [ -z "${LLVM_CONFIG:-}" ]; then export LLVM_CONFIG="$(brew --prefix llvm@18)/bin/llvm-config"; fi
# tabelle delle traduzioni (source/i18n_data.c è già nel sorgente: Node serve solo per rigenerarle)
if command -v node >/dev/null 2>&1; then node "$HERE/tools/i18n-gen.mjs" || { echo "traduzioni non valide"; exit 1; }; fi
rm -rf "${HERE:?}/build"
"$PS5_PAYLOAD_SDK/bin/prospero-cmake" -S "$HERE" -B "$HERE/build"
cmake --build "$HERE/build"
ELF="$HERE/../bin/omega_ui.elf"
[ -f "$ELF" ] && echo "FATTO: $ELF ($(shasum -a 256 "$ELF" | cut -d' ' -f1))" || { echo "build fallita"; exit 1; }
