/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Knutwurst
 *
 * PatchDL is free software: you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free
 * Software Foundation, either version 3 of the License, or (at your option)
 * any later version. See the LICENSE file in the project root for details.
 */

#include "patchdl_scan.h"
#include "patchdl_appdb.h"

#include <ps5/kernel.h>

#include <dirent.h>
#include <limits.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <unistd.h>

#define MAX_TITLES 512

static const char *SCAN_DIRS[] = {
    "/user/app",
    "/mnt/ext0/user/app",
    "/mnt/ext1/user/app",
    "/system_ex/app",
    NULL,
};

/* ---------- param.json -------------------------------------------------- */

static int
json_str(const char *json, const char *key, char *out, size_t out_sz) {
    char needle[80];
    const char *p;
    size_t len;

    if ((size_t)snprintf(needle, sizeof(needle), "\"%s\"", key) >= sizeof(needle))
        return -1;

    p = strstr(json, needle);
    if (!p) return -1;
    p += strlen(needle);
    while (*p == ' ' || *p == '\t' || *p == ':' || *p == '\n' || *p == '\r')
        p++;
    if (*p != '"') return -1;
    p++;
    for (len = 0; p[len] && p[len] != '"' && len < out_sz - 1; len++)
        ;
    memcpy(out, p, len);
    out[len] = '\0';
    return 0;
}

/* Extract the title name for the default language out of the nested
   localizedParameters block. PS5 param.json has no flat titleName key. */
static void
json_title_name(const char *json, char *out, size_t out_sz) {
    char        lang[16] = {0};
    const char *p;

    if (!json_str(json, "defaultLanguage", lang, sizeof(lang)) && lang[0]) {
        char anchor[24];
        if ((size_t)snprintf(anchor, sizeof(anchor), "\"%s\"", lang) <
            sizeof(anchor)) {
            p = strstr(json, anchor);
            if (p) {
                const char *tn = strstr(p, "\"titleName\"");
                if (tn && !json_str(tn, "titleName", out, out_sz))
                    return;
            }
        }
    }

    /* Fallback: first titleName anywhere in the file */
    json_str(json, "titleName", out, out_sz);
}

static int
try_param_json(const char *dir, patchdl_title_t *t) {
    char  path[PATH_MAX];
    FILE *fp;
    char  buf[8192];
    size_t n;

    snprintf(path, sizeof(path), "%s/sce_sys/param.json", dir);
    fp = fopen(path, "r");
    if (!fp) return -1;
    n = fread(buf, 1, sizeof(buf) - 1, fp);
    fclose(fp);
    if (!n) return -1;
    buf[n] = '\0';

    /* PS5 keys first, then PS4 fallbacks */
    if (json_str(buf, "titleId",   t->title_id, sizeof(t->title_id)) &&
        json_str(buf, "TITLE_ID",  t->title_id, sizeof(t->title_id)))
        return -1;

    json_str(buf, "contentId",      t->content_id,        sizeof(t->content_id));
    json_str(buf, "contentVersion", t->installed_version, sizeof(t->installed_version));
    if (!t->content_id[0])
        json_str(buf, "CONTENT_ID", t->content_id, sizeof(t->content_id));
    if (!t->installed_version[0])
        json_str(buf, "APP_VER",    t->installed_version, sizeof(t->installed_version));

    json_title_name(buf, t->name, sizeof(t->name));

    return 0;
}

/* ---------- param.sfo --------------------------------------------------- */

#pragma pack(push, 1)
typedef struct {
    uint32_t magic;             /* 0x46535000 = "\x00PSF" */
    uint32_t version;
    uint32_t key_table_start;
    uint32_t data_table_start;
    uint32_t num_entries;
} sfo_hdr_t;

typedef struct {
    uint16_t key_offset;
    uint16_t data_fmt;
    uint32_t data_len;
    uint32_t data_max_len;
    uint32_t data_offset;
} sfo_entry_t;
#pragma pack(pop)

#define SFO_MAGIC    0x46535000u
#define SFO_FMT_STR  0x0204u

static int
sfo_get(const uint8_t *buf, size_t bufsz, const char *key,
        char *out, size_t out_sz) {
    const sfo_hdr_t   *h;
    const sfo_entry_t *entries;
    uint32_t i;

    if (bufsz < sizeof(*h)) return -1;
    h = (const sfo_hdr_t *)buf;
    if (h->magic != SFO_MAGIC || h->num_entries > 256) return -1;

    entries = (const sfo_entry_t *)(buf + sizeof(*h));

    /* The entry table itself must fit in the bytes we actually read; a crafted
       param.sfo (from a shadow-mounted game dir) could otherwise drive
       entries[i] past the buffer. */
    if (sizeof(*h) + (size_t)h->num_entries * sizeof(sfo_entry_t) > bufsz)
        return -1;

    for (i = 0; i < h->num_entries; i++) {
        size_t key_off = h->key_table_start  + entries[i].key_offset;
        size_t val_off = h->data_table_start + entries[i].data_offset;

        if (key_off >= bufsz || val_off >= bufsz) continue;

        const char *k    = (const char *)(buf + key_off);
        size_t      kmax = bufsz - key_off;
        /* Require the key to be NUL-terminated within the buffer before the
           strcmp, otherwise it would read past the end. */
        if (strnlen(k, kmax) == kmax) continue;
        if (strcmp(k, key)) continue;
        if (entries[i].data_fmt != SFO_FMT_STR) return -1;

        size_t len = entries[i].data_len;
        if (val_off + len > bufsz) return -1;
        if (len >= out_sz) len = out_sz - 1;
        memcpy(out, buf + val_off, len);
        out[len] = '\0';
        return 0;
    }
    return -1;
}

static int
try_param_sfo(const char *dir, patchdl_title_t *t) {
    char     path[PATH_MAX];
    FILE    *fp;
    uint8_t  buf[8192];
    size_t   n;
    struct stat st;

    snprintf(path, sizeof(path), "%s/sce_sys/param.sfo", dir);
    fp = fopen(path, "rb");
    if (!fp) return -1;

    if (fstat(fileno(fp), &st) || (size_t)st.st_size > sizeof(buf)) {
        fclose(fp);
        return -1;
    }
    n = fread(buf, 1, (size_t)st.st_size, fp);
    fclose(fp);
    if (n != (size_t)st.st_size) return -1;

    if (sfo_get(buf, n, "TITLE_ID", t->title_id, sizeof(t->title_id)))
        return -1;

    sfo_get(buf, n, "CONTENT_ID", t->content_id,        sizeof(t->content_id));
    sfo_get(buf, n, "APP_VER",    t->installed_version,  sizeof(t->installed_version));
    sfo_get(buf, n, "TITLE",      t->name,               sizeof(t->name));
    if (!t->name[0])
        sfo_get(buf, n, "TITLE_00", t->name, sizeof(t->name));
    return 0;
}

/* ---------- install-type classification (file-based) -------------------- */

static int
path_exists(const char *dir, const char *rel) {
    char p[PATH_MAX];
    struct stat st;
    snprintf(p, sizeof(p), "%s/%s", dir, rel);
    return stat(p, &st) == 0;
}

/* Read up to sz-1 bytes of dir/rel into buf (NUL-terminated). 0 on success. */
static int
read_small_file(const char *dir, const char *rel, char *buf, size_t sz) {
    char   p[PATH_MAX];
    FILE  *fp;
    size_t n;

    snprintf(p, sizeof(p), "%s/%s", dir, rel);
    fp = fopen(p, "rb");
    if (!fp) return -1;
    n = fread(buf, 1, sz - 1, fp);
    fclose(fp);
    buf[n] = '\0';
    return 0;
}

/* Keep only actual games. Skip PS5 system apps (NPXS), the Payload Manager
   fake app (PLDM) and ELF-loader homebrew (PSPS). */
static int
is_game_title(const char *title_id) {
    if (!strncmp(title_id, "NPXS", 4)) return 0;
    if (!strncmp(title_id, "PLDM", 4)) return 0;
    if (!strncmp(title_id, "PSPS", 4)) return 0;
    return 1;
}

/* ---------- directory scanner ------------------------------------------- */

/* Authoritative shadowmount test: ShadowMountPlus routes its images through
   /mnt/shadowmnt (a pfs from /dev/lvdN there, then a nullfs onto the app dir),
   so a title whose mount table references /mnt/shadowmnt is a shadowmount. The
   on-disk mount.lnk marker is unreliable across reboots/remounts; the live
   mount table is not. */
static int
mount_is_shadow(const char *title_id, const struct statfs *mounts, int nmounts) {
    for (int i = 0; i < nmounts; i++) {
        const char *from = mounts[i].f_mntfromname;
        const char *on   = mounts[i].f_mntonname;
        if ((strstr(from, "/mnt/shadowmnt") || strstr(on, "/mnt/shadowmnt")) &&
            (strstr(from, title_id) || strstr(on, title_id)))
            return 1;
    }
    return 0;
}

/*
 * Classify each /user/app/<TID> (verified on fw 11.60):
 *   - mount table references /mnt/shadowmnt for the title -> ShadowMountPlus
 *     mount (authoritative; mount.lnk is only a fallback hint)
 *   - app.pkg (no shadow mount) -> genuine install; app.json with CDN piece
 *     URLs means a not-downloaded preinstall stub, local URLs a real install
 *   - app.json with "fake":true -> homebrew fake (skip)
 */
static int
scan_one(const char *base, const char *name, patchdl_title_t *t,
         const struct statfs *mounts, int nmounts) {
    char dir[PATH_MAX];
    char appjson[4096];
    int  has_mountlnk, has_app_pkg, has_paramjson, is_shadow, is_fake = 0, is_cdn = 0;

    snprintf(dir, sizeof(dir), "%s/%s", base, name);
    memset(t, 0, sizeof(*t));
    strncpy(t->source_path, dir, sizeof(t->source_path) - 1);
    strncpy(t->title_id, name, sizeof(t->title_id) - 1);

    if (!is_game_title(t->title_id))
        return -1;

    is_shadow     = mount_is_shadow(t->title_id, mounts, nmounts);
    has_mountlnk  = path_exists(dir, "mount.lnk") ||
                    path_exists(dir, "mount_img.lnk");
    has_app_pkg   = path_exists(dir, "app.pkg");
    has_paramjson = path_exists(dir, "sce_sys/param.json");

    if (!read_small_file(dir, "app.json", appjson, sizeof(appjson))) {
        if (strstr(appjson, "\"fake\"") && strstr(appjson, "true"))
            is_fake = 1;
        if (strstr(appjson, "http"))
            is_cdn = 1; /* piece URLs point at the Sony CDN -> preinstall */
    }
    if (is_fake)
        return -1;

    /* app.pkg is the on-disk package of a genuine install; ShadowMountPlus
       titles never have it (they have mounted/leftover sce_sys content). So
       app.pkg is the reliable genuine-vs-shadow discriminator — independent of
       whether the shadow image is currently mounted. */
    if (has_app_pkg) {
        t->source_type = is_cdn ? PATCHDL_SOURCE_UNKNOWN  /* CDN pkg = preinstall */
                                : PATCHDL_SOURCE_OFFICIAL;
    } else if (is_shadow || has_mountlnk || has_paramjson ||
               path_exists(dir, "sce_sys/param.sfo")) {
        if (try_param_json(dir, t)) /* metadata from the mounted sce_sys */
            try_param_sfo(dir, t);
        t->source_type = PATCHDL_SOURCE_SHADOWMOUNT;
    } else {
        return -1; /* empty / leftover directory */
    }

    if (!t->title_id[0])
        strncpy(t->title_id, name, sizeof(t->title_id) - 1);
    if (!t->name[0])
        strncpy(t->name, t->title_id, sizeof(t->name) - 1);

    return 0;
}

static int
already_seen(const patchdl_title_t *arr, size_t cnt, const char *title_id) {
    for (size_t i = 0; i < cnt; i++)
        if (!strcmp(arr[i].title_id, title_id))
            return 1;
    return 0;
}

static void
scan_base(const char *base, patchdl_title_t *arr, size_t *cnt, size_t cap,
          const struct statfs *mounts, int nmounts) {
    DIR           *d;
    struct dirent *de;

    d = opendir(base);
    if (!d) return;

    while ((de = readdir(d))) {
        if (de->d_name[0] == '.') continue;
        if (*cnt >= cap) break;
        if (scan_one(base, de->d_name, &arr[*cnt], mounts, nmounts) != 0)
            continue;
        if (already_seen(arr, *cnt, arr[*cnt].title_id))
            continue; /* dedupe a title already found in an earlier base */
        (*cnt)++;
    }
    closedir(d);
}

/* ---------- public API -------------------------------------------------- */

const char *
patchdl_source_str(patchdl_source_t src) {
    switch (src) {
    case PATCHDL_SOURCE_OFFICIAL:    return "official";
    case PATCHDL_SOURCE_EXTERNAL:    return "external";
    case PATCHDL_SOURCE_SHADOWMOUNT: return "shadowmount";
    default:                          return "unknown";
    }
}

/* Overlay app.db metadata (real title name, installed version, version.xml
   URL) onto the scanned titles. Genuine installs have no readable param.json,
   so this is the only source of their name/version. */
static void
merge_appdb(patchdl_title_t *arr, size_t cnt) {
    patchdl_appinfo_t *info = NULL;
    size_t n = 0;

    if (patchdl_appdb_load(&info, &n))
        return;

    for (size_t i = 0; i < cnt; i++) {
        const patchdl_appinfo_t *a = patchdl_appdb_find(info, n, arr[i].title_id);
        if (!a) continue;
        if (a->title_name[0])
            strncpy(arr[i].name, a->title_name, sizeof(arr[i].name) - 1);
        if (a->content_version[0])
            strncpy(arr[i].installed_version, a->content_version,
                    sizeof(arr[i].installed_version) - 1);
        if (a->content_id[0] && !arr[i].content_id[0])
            strncpy(arr[i].content_id, a->content_id,
                    sizeof(arr[i].content_id) - 1);
        if (a->version_file_uri[0])
            strncpy(arr[i].version_file_uri, a->version_file_uri,
                    sizeof(arr[i].version_file_uri) - 1);
    }

    patchdl_appdb_free(info);
}

/* See patchdl_scan_lock — both vnode-swap entry points return -1 / NULL once
   this is set so a late rescan call can't race the running MHD threads. */
static _Atomic int g_scan_locked = 0;

void
patchdl_scan_lock(void) {
    atomic_store(&g_scan_locked, 1);
}

int
patchdl_scan(patchdl_title_t **titles_out, size_t *count_out) {
    patchdl_title_t *arr;
    size_t cnt = 0;
    pid_t pid = getpid();
    intptr_t saved_root = 0, root_vnode;
    int using_vswap = 0;

    struct statfs *mounts = NULL;
    int            nmounts;

    if (atomic_load(&g_scan_locked)) return -1;

    arr = calloc(MAX_TITLES, sizeof(*arr));
    if (!arr) return -1;

    /* Elevate so privileged paths (the app.db under /system_data) are
       readable; same authid ftpsrv uses. No-op without kernel R/W. */
    kernel_set_ucred_authid(pid, 0x4801000000000013L);

    /* Global mount table for shadowmount detection. getmntinfo's buffer is
       libc-managed — must NOT be freed. */
    nmounts = getmntinfo(&mounts, MNT_NOWAIT);
    if (nmounts < 0) { nmounts = 0; mounts = NULL; }

    root_vnode = kernel_get_root_vnode();
    if (root_vnode) {
        saved_root = kernel_get_proc_rootdir(pid);
        /* Only swap if we captured the current root, so we can always restore
           it. Leaving the process rooted at the system root would make later
           absolute-path writes land in the wrong place. */
        if (saved_root) {
            kernel_set_proc_rootdir(pid, root_vnode);
            using_vswap = 1;
        }
    }

    for (int i = 0; SCAN_DIRS[i]; i++)
        scan_base(SCAN_DIRS[i], arr, &cnt, MAX_TITLES, mounts, nmounts);

    merge_appdb(arr, cnt);

    if (using_vswap)
        kernel_set_proc_rootdir(pid, saved_root);

    *titles_out = arr;
    *count_out  = cnt;
    return 0;
}

void
patchdl_scan_free(patchdl_title_t *titles, size_t count) {
    (void)count;
    free(titles);
}

/* ---------- diagnostic dump --------------------------------------------- */

static char *
dbg_append(char *buf, size_t *len, size_t *cap, const char *s) {
    size_t sl = strlen(s);
    if (*len + sl + 1 > *cap) {
        size_t nc = (*cap + sl + 1) * 2;
        char *p = realloc(buf, nc);
        if (!p) return buf;
        buf = p;
        *cap = nc;
    }
    memcpy(buf + *len, s, sl + 1);
    *len += sl;
    return buf;
}

char *
patchdl_scan_debug_json(void) {
    char  *buf = NULL;
    size_t len = 0, cap = 0;
    char   tmp[2048];
    pid_t  pid = getpid();
    intptr_t saved_root = 0, root_vnode;

    if (atomic_load(&g_scan_locked)) return NULL;
    int    using_vswap = 0;
    struct statfs *mounts = NULL;
    int    nmounts;

    /* getmntinfo returns the global mount table — no root-vnode swap needed.
       Must NOT free the returned buffer (libc-managed). */
    nmounts = getmntinfo(&mounts, MNT_NOWAIT);
    if (nmounts < 0) { nmounts = 0; mounts = NULL; }

    buf = dbg_append(buf, &len, &cap, "{\"mounts\":[");
    for (int i = 0; i < nmounts; i++) {
        snprintf(tmp, sizeof(tmp),
                 "%s{\"from\":\"%s\",\"on\":\"%s\",\"type\":\"%s\",\"flags\":\"0x%lx\"}",
                 i ? "," : "",
                 mounts[i].f_mntfromname, mounts[i].f_mntonname,
                 mounts[i].f_fstypename, (unsigned long)mounts[i].f_flags);
        buf = dbg_append(buf, &len, &cap, tmp);
    }

    buf = dbg_append(buf, &len, &cap, "],\"bases\":[");

    root_vnode = kernel_get_root_vnode();
    if (root_vnode) {
        saved_root = kernel_get_proc_rootdir(pid);
        /* Only swap if we can restore it afterwards (see patchdl_scan). */
        if (saved_root) {
            kernel_set_proc_rootdir(pid, root_vnode);
            using_vswap = 1;
        }
    }

    for (int i = 0; SCAN_DIRS[i]; i++) {
        DIR *d = opendir(SCAN_DIRS[i]);
        snprintf(tmp, sizeof(tmp), "%s{\"base\":\"%s\",\"exists\":%s,\"entries\":[",
                 i ? "," : "", SCAN_DIRS[i], d ? "true" : "false");
        buf = dbg_append(buf, &len, &cap, tmp);
        if (d) {
            struct dirent *de;
            int first = 1;
            while ((de = readdir(d))) {
                char dir[PATH_MAX];
                if (de->d_name[0] == '.') continue;
                snprintf(dir, sizeof(dir), "%s/%s", SCAN_DIRS[i], de->d_name);
                snprintf(tmp, sizeof(tmp),
                         "%s{\"name\":\"%s\",\"pj\":%s,\"pkg\":%s,\"lnk\":%s}",
                         first ? "" : ",", de->d_name,
                         path_exists(dir, "sce_sys/param.json") ? "true" : "false",
                         path_exists(dir, "app.pkg") ? "true" : "false",
                         (path_exists(dir, "mount.lnk") ||
                          path_exists(dir, "mount_img.lnk")) ? "true" : "false");
                buf = dbg_append(buf, &len, &cap, tmp);
                first = 0;
            }
            closedir(d);
        }
        buf = dbg_append(buf, &len, &cap, "]}");
    }

    if (using_vswap)
        kernel_set_proc_rootdir(pid, saved_root);

    buf = dbg_append(buf, &len, &cap, "]}");
    return buf;
}
