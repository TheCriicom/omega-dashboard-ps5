PS5_HOST ?= ps5
PS5_PORT ?= 9021
PATCHDL_HTTP_PORT ?= 12880
ETAHEN_DEPS_DIR ?= vendor/etahen

ifdef PS5_PAYLOAD_SDK
    include $(PS5_PAYLOAD_SDK)/toolchain/prospero.mk
else
    $(error PS5_PAYLOAD_SDK is undefined)
endif

PYTHON ?= python3
BIN := patchdl-ps5.elf

SRCS := src/main.c \
        src/patchdl_assets.c \
        src/patchdl_websrv.c \
        src/patchdl_proc.c \
        src/patchdl_fw.c \
        src/patchdl_scan.c \
        src/patchdl_appdb.c \
        src/patchdl_net.c \
        src/patchdl_resolve.c \
        src/patchdl_verxml.c \
        src/patchdl_install.c \
        src/patchdl_notify.c \
        src/patchdl_tile.c

# patchdl_tile.c uses .incbin to embed param.json + icon0.png; touching the
# assets must trigger a rebuild.
TILE_ASSETS := assets/param.json assets/icon0.png

WEB_ASSETS := web/index.html web/styles.css web/app.js
GEN_SRCS := $(patsubst web/%,gen/web/%.c,$(WEB_ASSETS))

SQLITE_DIR := vendor/sqlite
SQLITE_OBJ := $(SQLITE_DIR)/sqlite3.o
SQLITE_CFLAGS := -O2 -w \
        -DSQLITE_THREADSAFE=0 \
        -DSQLITE_OMIT_LOAD_EXTENSION \
        -DSQLITE_OMIT_WAL \
        -DSQLITE_DEFAULT_MEMSTATUS=0 \
        -DSQLITE_OMIT_DEPRECATED

CFLAGS := -g -O2 -Wall -Werror -Isrc -I"$(ETAHEN_DEPS_DIR)/include" -I"$(SQLITE_DIR)" -DPATCHDL_HTTP_PORT=$(PATCHDL_HTTP_PORT)
LDADD := -L"$(ETAHEN_DEPS_DIR)/lib" -lmicrohttpd -lkernel_sys -lpthread

ifdef CURL_DIR
    CFLAGS += -DPATCHDL_HAVE_CURL -I"$(CURL_DIR)/include"
    LDADD  += -L"$(CURL_DIR)/lib" -lcurl -lpsl -lssl -lcrypto -lzstd -lz
endif

all: $(BIN)

gen/web:
	mkdir -p gen/web

gen/web/%.c: web/% scripts/gen_asset_module.py | gen/web
	$(PYTHON) scripts/gen_asset_module.py --path $* $< > $@

$(SQLITE_OBJ): $(SQLITE_DIR)/sqlite3.c
	$(CC) $(SQLITE_CFLAGS) -c -o $@ $<

$(BIN): $(SRCS) $(GEN_SRCS) $(SQLITE_OBJ) $(TILE_ASSETS)
	$(CC) $(CFLAGS) -o $@ $(SRCS) $(GEN_SRCS) $(SQLITE_OBJ) $(LDADD)

test: $(BIN)
	$(PS5_DEPLOY) -h $(PS5_HOST) -p $(PS5_PORT) $^

clean:
	rm -rf $(BIN) gen $(SQLITE_OBJ)

.PHONY: all clean test
