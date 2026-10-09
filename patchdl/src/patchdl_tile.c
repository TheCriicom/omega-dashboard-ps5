/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Knutwurst
 *
 * PatchDL is free software: you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free
 * Software Foundation, either version 3 of the License, or (at your option)
 * any later version. See the LICENSE file in the project root for details.
 */

#include "patchdl_tile.h"
#include "patchdl_install.h"

#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <ps5/kernel.h>

/* Embed the tile assets into .rodata directly via .incbin — no codegen step,
   no Python helper, no second translation unit. */
#define INCASSET(name, file)                                                  \
    __asm__(".section .rodata\n"                                              \
            ".global " #name "\n"                                             \
            ".global " #name "_end\n"                                         \
            ".global " #name "_size\n"                                        \
            ".align 16\n" #name ":\n"                                         \
            ".incbin \"" file "\"\n" #name "_end:\n" #name "_size:\n"         \
            ".quad " #name "_end - " #name "\n"                               \
            ".previous\n");                                                   \
    extern const uint8_t name[];                                              \
    extern const size_t  name##_size;

INCASSET(tile_param_json, "assets/param.json");
INCASSET(tile_icon0_png,  "assets/icon0.png");

#define TILE_TITLE_ID "PTDL00001"

/* Forward decls — we resolve sceAppInstUtilInitialize/AppInstallTitleDir at
   runtime via the kernel dynlib helpers so the ELF stays loader-friendly. */
typedef int (*ai_init_fn)(void);
typedef int (*ai_install_dir_fn)(const char *title_id, const char *parent_dir,
                                  void *opts);

static intptr_t
dynsym_by_name(const char *sym) {
    uint32_t h = 0;
    if (kernel_dynlib_handle(-1, "libSceAppInstUtil.sprx", &h) < 0) return 0;
    return kernel_dynlib_dlsym(-1, h, sym);
}

static intptr_t
dynsym_by_nid(const char *nid) {
    uint32_t h = 0;
    if (kernel_dynlib_handle(-1, "libSceAppInstUtil.sprx", &h) < 0) return 0;
    return kernel_dynlib_resolve(-1, h, nid);
}

static int
write_all(const char *path, const uint8_t *data, size_t size) {
    int     fd;
    ssize_t w;
    size_t  off = 0;

    /* O_NOFOLLOW + 0600: don't follow a symlink at the destination, and don't
       create the file with the libc default 0666 mode. Same pattern as the
       net layer's fopen_safe. */
    fd = open(path, O_WRONLY | O_CREAT | O_TRUNC | O_NOFOLLOW | O_CLOEXEC, 0600);
    if (fd < 0) return -1;
    while (off < size) {
        w = write(fd, data + off, size - off);
        if (w < 0) {
            if (errno == EINTR) continue;
            close(fd);
            return -1;
        }
        off += (size_t)w;
    }
    close(fd);
    return 0;
}

static int
file_matches(const char *path, const uint8_t *expected, size_t expected_size) {
    struct stat st;
    uint8_t    *buf;
    int         fd;
    ssize_t     n;
    int         match = 0;

    if (stat(path, &st) != 0) return 0;
    if ((size_t)st.st_size != expected_size) return 0;

    fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return 0;

    buf = malloc(expected_size);
    if (!buf) { close(fd); return 0; }

    n = read(fd, buf, expected_size);
    close(fd);
    if (n == (ssize_t)expected_size && memcmp(buf, expected, expected_size) == 0)
        match = 1;

    free(buf);
    return match;
}

int
patchdl_tile_install_if_needed(char *msg, size_t msg_sz) {
    char base_dir[128];
    char sce_sys_dir[160];
    char param_path[192];
    char icon_path[192];

    ai_init_fn        ai_initialize    = NULL;
    ai_install_dir_fn ai_install_title = NULL;
    int               rc;

    snprintf(base_dir,    sizeof base_dir,    "/user/app/%s",                  TILE_TITLE_ID);
    snprintf(sce_sys_dir, sizeof sce_sys_dir, "/user/app/%s/sce_sys",          TILE_TITLE_ID);
    snprintf(param_path,  sizeof param_path,  "/user/app/%s/sce_sys/param.json", TILE_TITLE_ID);
    snprintf(icon_path,   sizeof icon_path,   "/user/app/%s/sce_sys/icon0.png",  TILE_TITLE_ID);

    /* stat-guard: if everything on disk already matches, do nothing. Re-running
       the install API every payload start would be wasted work and burns the
       only safe path through Sony's installer state machine. */
    {
        struct stat st;
        if (stat(base_dir, &st) == 0 &&
            file_matches(param_path, tile_param_json, tile_param_json_size) &&
            file_matches(icon_path,  tile_icon0_png,  tile_icon0_png_size)) {
            snprintf(msg, msg_sz, "tile already installed and up to date");
            return 0;
        }
    }

    /* AppInstUtil is loaded lazily by the install backend thread. Poke it +
       poll briefly so libSceAppInstUtil.sprx is mapped before we try to
       resolve symbols out of it. Bounded to a few seconds so a stuck init
       doesn't wedge an MHD worker forever. */
    {
        char    ready_msg[128];
        int     ready = -1;
        for (int i = 0; i < 60 && ready != 0; i++) {
            ready = patchdl_install_backend_check(ready_msg, sizeof ready_msg);
            if (ready != 0) usleep(250 * 1000);
        }
        if (ready != 0) {
            snprintf(msg, msg_sz,
                     "install backend not ready: %s", ready_msg);
            return -1;
        }
    }

    /* Resolve the install API now so we can fail fast before touching disk.
       The symbol export is Sony-private, so the NID resolver is the more
       reliable lookup — try NID first, fall back to the by-name dlsym. */
    ai_install_title = (ai_install_dir_fn)dynsym_by_nid("Wudg3Xe3heE");
    if (!ai_install_title)
        ai_install_title = (ai_install_dir_fn)dynsym_by_name(
            "sceAppInstUtilAppInstallTitleDir");
    if (!ai_install_title) {
        snprintf(msg, msg_sz, "sceAppInstUtilAppInstallTitleDir not resolved");
        return -1;
    }

    ai_initialize = (ai_init_fn)dynsym_by_name("sceAppInstUtilInitialize");
    if (ai_initialize) {
        rc = ai_initialize();
        /* SCE_OK == 0; a non-zero rc here usually means "already initialised"
           in this process, which is fine. We only bail on a clearly fatal
           code (anything that isn't already the success case). */
        if (rc != 0 && rc != 0x80B21161 /* ALREADY_INITIALIZED */) {
            snprintf(msg, msg_sz,
                     "sceAppInstUtilInitialize failed 0x%08x", (unsigned)rc);
            return -1;
        }
    }

    if (mkdir(base_dir, 0755) && errno != EEXIST) {
        snprintf(msg, msg_sz, "mkdir %s failed errno=%d", base_dir, errno);
        return -1;
    }
    if (mkdir(sce_sys_dir, 0755) && errno != EEXIST) {
        snprintf(msg, msg_sz, "mkdir %s failed errno=%d", sce_sys_dir, errno);
        return -1;
    }

    if (write_all(param_path, tile_param_json, tile_param_json_size)) {
        snprintf(msg, msg_sz, "write param.json failed errno=%d", errno);
        return -1;
    }
    if (write_all(icon_path, tile_icon0_png, tile_icon0_png_size)) {
        snprintf(msg, msg_sz, "write icon0.png failed errno=%d", errno);
        return -1;
    }

    rc = ai_install_title(TILE_TITLE_ID, "/user/app/", NULL);
    if (rc != 0) {
        snprintf(msg, msg_sz,
                 "AppInstallTitleDir(%s) returned 0x%08x",
                 TILE_TITLE_ID, (unsigned)rc);
        return -1;
    }

    snprintf(msg, msg_sz, "tile installed (%s)", TILE_TITLE_ID);
    return 0;
}
