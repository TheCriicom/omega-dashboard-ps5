#!/usr/bin/env bash
# Compila il plugin di Omega per OnionHEN → ../bin/OMGA00001.elf
#   PS5_PAYLOAD_SDK=$HOME/ps5-payload-sdk ./build.sh
# Facoltativo: ONIONHEN_PLUGIN_SDK_SOURCE=<copia locale dell'SDK dei plugin>
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
[ -n "${PS5_PAYLOAD_SDK:-}" ] || { echo "PS5_PAYLOAD_SDK non impostata"; exit 1; }
if [ -z "${LLVM_CONFIG:-}" ]; then export LLVM_CONFIG="$(brew --prefix llvm@18)/bin/llvm-config"; fi
GEN="$HERE/../omega-ui-src/tools/i18n-gen.mjs"; [ -f "$GEN" ] || GEN="$HERE/../client/tools/i18n-gen.mjs"
if command -v node >/dev/null 2>&1 && [ -f "$GEN" ]; then node "$GEN" || { echo "traduzioni non valide"; exit 1; }; fi
rm -rf "${HERE:?}/build"
"$PS5_PAYLOAD_SDK/bin/prospero-cmake" -S "$HERE" -B "$HERE/build" ${ONIONHEN_PLUGIN_SDK_SOURCE:+-DONIONHEN_PLUGIN_SDK_SOURCE="$ONIONHEN_PLUGIN_SDK_SOURCE"}
cmake --build "$HERE/build"
ELF="$(find "$HERE/build" -name 'OMGA00001*.elf' | head -1)"
[ -n "$ELF" ] || { echo "build fallita"; exit 1; }
OUT="$HERE/../bin"; [ -d "$OUT" ] || OUT="$HERE/bin"; mkdir -p "$OUT"
cp "$ELF" "$OUT/OMGA00001.elf"
echo "FATTO: $OUT/OMGA00001.elf ($(shasum -a 256 "$OUT/OMGA00001.elf" | cut -d' ' -f1))"
