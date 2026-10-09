#!/bin/sh
set -eu

ROOT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)

if [ -z "${PS5_PAYLOAD_SDK:-}" ]; then
  LOCAL_SDK="$ROOT_DIR/.toolchains/ps5-payload-sdk/ps5-payload-sdk"
  if [ -d "$LOCAL_SDK" ]; then
    PS5_PAYLOAD_SDK="$LOCAL_SDK"
    export PS5_PAYLOAD_SDK
  else
    echo "PS5_PAYLOAD_SDK is unset and no local SDK exists at $LOCAL_SDK" >&2
    exit 1
  fi
fi

if [ -z "${LLVM_CONFIG:-}" ] && [ -x /opt/homebrew/opt/llvm@18/bin/llvm-config ]; then
  LLVM_CONFIG=/opt/homebrew/opt/llvm@18/bin/llvm-config
  export LLVM_CONFIG
fi

if [ -n "${LLVM_CONFIG:-}" ]; then
  LLVM_BIN=$(dirname -- "$LLVM_CONFIG")
  PATH="$PS5_PAYLOAD_SDK/bin:$LLVM_BIN:$PATH"
else
  PATH="$PS5_PAYLOAD_SDK/bin:$PATH"
fi
export PATH

# Enable the libcurl/OpenSSL network path automatically when those static
# libs have been installed into the SDK sysroot (the pacbrew homebrew prefix).
if [ -z "${CURL_DIR:-}" ]; then
  HBREW="$PS5_PAYLOAD_SDK/target/user/homebrew"
  if [ -f "$HBREW/include/curl/curl.h" ] && [ -f "$HBREW/lib/libcurl.a" ]; then
    CURL_DIR="$HBREW"
    export CURL_DIR
  fi
fi

exec make -C "$ROOT_DIR" "$@"
