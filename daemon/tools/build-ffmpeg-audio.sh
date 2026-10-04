#!/usr/bin/env bash
# FFmpeg ridotto al solo audio per il lettore del demone (player.c).
# Il pacchetto FFmpeg dell'SDK contiene tutti i codec, anche video: il demone
# passerebbe da ~0,2 a ~30 MB. Questa build tiene decoder audio, contenitori,
# radio HLS e https (OpenSSL dell'SDK) e resta sotto i 3 MB. Niente assembly
# x86 (servirebbe nasm): per decodificare audio la CPU della console avanza.
#
#   PS5_PAYLOAD_SDK=$HOME/ps5-payload-sdk ./tools/build-ffmpeg-audio.sh
#
# Risultato in ffmpeg-audio/{include,lib}: CMakeLists.txt lo usa se c'è,
# altrimenti ripiega sul FFmpeg completo dell'SDK.
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
VER=7.0.1
OUT="$HERE/ffmpeg-audio"
WORK="${TMPDIR:-/tmp}/omega-ffmpeg-$VER"
[ -n "${PS5_PAYLOAD_SDK:-}" ] || { echo "PS5_PAYLOAD_SDK non impostata"; exit 1; }
if [ -z "${LLVM_CONFIG:-}" ]; then export LLVM_CONFIG="$(brew --prefix llvm@18)/bin/llvm-config"; fi
# shellcheck disable=SC1091
source "$PS5_PAYLOAD_SDK/toolchain/prospero.sh"

mkdir -p "$WORK"; cd "$WORK"
[ -f "ffmpeg-$VER.tar.xz" ] || curl -fsSLO "https://ffmpeg.org/releases/ffmpeg-$VER.tar.xz"
rm -rf "ffmpeg-$VER"; tar xf "ffmpeg-$VER.tar.xz"; cd "ffmpeg-$VER"

DEC=mp3,mp3float,mp2,mp2float,aac,aac_latm,flac,vorbis,opus,alac,wavpack,ape,wmav1,wmav2,pcm_s16le,pcm_s16be,pcm_s24le,pcm_s24be,pcm_s32le,pcm_f32le,pcm_f64le,pcm_u8,pcm_alaw,pcm_mulaw
DEMUX=mp3,aac,flac,ogg,mov,matroska,wav,w64,aiff,caf,wv,ape,asf,hls,mpegts,latm
PROTO=file,http,https,tls,tcp,hls,crypto,httpproxy

./configure \
  --prefix="$OUT" --enable-cross-compile \
  --cross-prefix="$PS5_PAYLOAD_SDK/bin/prospero-" \
  --enable-static --disable-shared --arch=x86_64 --target-os=freebsd \
  --disable-everything --disable-programs --disable-doc --disable-debug --disable-x86asm \
  --disable-avdevice --disable-avfilter --disable-swscale --disable-postproc \
  --enable-avformat --enable-avcodec --enable-swresample --enable-network \
  --enable-decoder="$DEC" --enable-demuxer="$DEMUX" --enable-protocol="$PROTO" \
  --enable-parser=mpegaudio,aac,aac_latm,flac,vorbis,opus \
  --enable-openssl --enable-version3 --enable-zlib --disable-bzlib --disable-iconv --disable-lzma --disable-sdl2 --disable-xlib \
  --extra-cflags="-I$PS5_SYSROOT$PS5_HBROOT/include" --extra-ldflags="-L$PS5_SYSROOT$PS5_HBROOT/lib" \
  --cc="$CC" --cxx="$CXX" --nm="$NM" --strip="$STRIP" --ar="$AR" --ranlib="$RANLIB" --pkg-config="$PKG_CONFIG"
make -j"$(sysctl -n hw.ncpu 2>/dev/null || echo 4)"
rm -rf "$OUT"; make install DESTDIR= >/dev/null   # prospero.sh imposta DESTDIR sulla sysroot
echo "FATTO: $OUT ($(du -sh "$OUT/lib" | cut -f1))"
