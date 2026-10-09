/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Knutwurst
 *
 * PatchDL is free software: you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free
 * Software Foundation, either version 3 of the License, or (at your option)
 * any later version. See the LICENSE file in the project root for details.
 */

#include "patchdl_install.h"

#include <arpa/inet.h>
#include <ifaddrs.h>
#include <netinet/in.h>
#include <ps5/kernel.h>

#include <pthread.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>

/* AppInstUtil structs/signatures, reverse-engineered by the PS5 homebrew
   scene (notmaj0r/CheatRunner, ps5-payload-dev/websrv). Sizes per CheatRunner:
   CONTENTID=0x30, PLAYGOSCENARIOID=3, LANGUAGE=8, NUM_LANGUAGES=30, NUM_IDS=64.
   The trailing `unknown` padding makes the playgo struct safely large. */
#define AI_CONTENTID_SIZE        0x30
#define AI_PLAYGOSCENARIOID_SIZE 3
#define AI_LANGUAGE_SIZE         8
#define AI_NUM_LANGUAGES         30
#define AI_NUM_IDS               64

typedef struct {
    char content_id[AI_CONTENTID_SIZE];
    int  content_type;
    int  content_platform;
} ai_pkg_info_t;

typedef struct {
    const char *uri;
    const char *ex_uri;
    const char *playgo_scenario_id;
    const char *content_id;
    const char *content_name;
    const char *icon_url;
} ai_meta_info_t;

typedef struct {
    char languages[AI_NUM_LANGUAGES][AI_LANGUAGE_SIZE];
    char playgo_scenario_ids[AI_NUM_IDS][AI_PLAYGOSCENARIOID_SIZE];
    char content_ids[AI_NUM_IDS][AI_CONTENTID_SIZE];
    long unknown[810];
} ai_playgo_info_t;

typedef struct {
    int32_t error_code;
    int32_t version;
    char    description[512];
    char    type[9];
} ai_install_error_t;

typedef struct {
    char               status[16];
    char               src_type[8];
    uint32_t           remain_time;
    uint64_t           downloaded_size;
    uint64_t           initial_chunk_size;
    uint64_t           total_size;
    uint32_t           promote_progress;
    ai_install_error_t error_info;
    int32_t            local_copy_percent;
    bool               is_copy_only;
    char               _pad[2048]; /* safety margin — actual Sony struct may be larger */
} ai_install_status_t;

/* Padded buffers for Sony output writes whose actual size is reverse-engineered.
   The visible content fits in 0x30 (content_id) / 16 (title_id) bytes, but the
   firmware may NUL-pad or write more. Used as caller-side temporaries that are
   then copy_bounded()-d into the right-sized destination. */
#define AI_CONTENTID_OUT_SIZE    256
#define AI_TITLEID_OUT_SIZE      128

#define STATIC_ASSERT(c, n) typedef char static_assert_##n[(c) ? 1 : -1]
STATIC_ASSERT(sizeof(ai_pkg_info_t) == 0x38, pkg_info_size);
STATIC_ASSERT(sizeof(ai_meta_info_t) == (6 * sizeof(void *)), meta_info_size);
STATIC_ASSERT(sizeof(ai_playgo_info_t) == 0x2700, playgo_info_size);
STATIC_ASSERT(offsetof(ai_meta_info_t, uri) == 0, meta_uri_offset);
STATIC_ASSERT(offsetof(ai_meta_info_t, icon_url) == (5 * sizeof(void *)),
              meta_icon_offset);

/* Sysmodule IDs (from ps5-payload-dev/sdk crt/rtld_sprx.c). */
#define SYSMOD_IPMI           0x8000001d
#define SYSMOD_USERSERVICE    0x80000011
#define SYSMOD_SYSTEMSERVICE  0x80000010
#define SYSMOD_APPINSTUTIL    0x80000014

typedef int (*sysmod_load_fn)(unsigned int id);
typedef int (*ai_init_fn)(void);
typedef int (*ai_install_pkg_fn)(const char *path, ai_pkg_info_t *info);
typedef int (*ai_install_by_pkg_fn)(ai_meta_info_t *meta, ai_pkg_info_t *info,
                                    ai_playgo_info_t *playgo);
typedef int (*ai_title_from_pkg_fn)(const char *path, char *title_id, int *is_app);
typedef int (*ai_content_from_pkg_fn)(const char *path, char *content_id, int *is_app);
typedef int (*ai_get_status_fn)(char *content_id_out, ai_install_status_t *status);

static ai_init_fn             ai_initialize;
static ai_install_pkg_fn      ai_install_pkg;
static ai_install_by_pkg_fn   ai_install_by_package;
static ai_title_from_pkg_fn   ai_title_from_pkg;
static ai_content_from_pkg_fn ai_content_from_pkg;
static ai_get_status_fn       ai_get_status;

/* Resolve + initialize the AppInstUtil backend WITHOUT linking the sce libs
   (that makes the ELF unloadable by the elfldr) and WITHOUT raw
   sceKernelLoadStartModule (that wedged the process). Instead use the proper
   sysmodule loader — the same path the SDK's rtld_sprx takes — then resolve
   symbols via the kernel dynlib helpers. Runs in a detached thread; the HTTP
   handler reports the stage and never blocks.
   stage: 0 idle, 1 resolve loader, 2 load modules, 3 resolve symbols,
   4 initialize, 5 ready, negative = failure at that step.
   Stored as _Atomic so the worker's release-store and the request handlers'
   acquire-loads pair properly — the function pointers they read after
   stage==5 must not be reordered ahead of the stage check. */
static _Atomic int     g_stage;
static _Atomic int     g_err;
static pthread_mutex_t g_mtx = PTHREAD_MUTEX_INITIALIZER;
static char            g_probe_json[2048]; /* filled by the backend thread */
static char            g_last_content_id[AI_CONTENTID_SIZE];
static char            g_last_target_title_id[32];
static char            g_last_method[32];
static int             g_last_start_rc;

static intptr_t
dynsym(const char *module, const char *sym) {
    uint32_t h = 0;
    if (kernel_dynlib_handle(-1, module, &h) < 0)
        return 0;
    return kernel_dynlib_dlsym(-1, h, sym);
}

static void
local_ip(char *out, size_t n) {
    struct ifaddrs *ifa = NULL, *p;

    out[0] = '\0';
    if (getifaddrs(&ifa))
        return;

    for (p = ifa; p; p = p->ifa_next) {
        char ip[INET_ADDRSTRLEN];
        struct sockaddr_in *s;

        if (!p->ifa_addr || p->ifa_addr->sa_family != AF_INET)
            continue;
        s = (struct sockaddr_in *)p->ifa_addr;
        if (!inet_ntop(AF_INET, &s->sin_addr, ip, sizeof(ip)))
            continue;
        if (strcmp(ip, "127.0.0.1") && strncmp(ip, "0.", 2)) {
            strncpy(out, ip, n - 1);
            out[n - 1] = '\0';
            break;
        }
    }
    freeifaddrs(ifa);
}

static void
copy_bounded(char *dst, size_t dst_sz, const char *src, size_t src_sz) {
    size_t n;
    if (!dst || !dst_sz) return;
    dst[0] = '\0';
    if (!src || !src_sz) return;
    for (n = 0; n + 1 < dst_sz && n < src_sz && src[n]; n++)
        dst[n] = src[n];
    dst[n] = '\0';
}

/* Conservative whitelist for ids/filenames that we extract from a local path
   and inject into URIs/log lines passed to AppInstUtil. Rejects CRLF, '/',
   '\\', NUL, control chars, anything that could change URI semantics. */
static int
install_id_safe(const char *s) {
    if (!s || !s[0]) return 0;
    for (const char *p = s; *p; p++) {
        if (!((*p >= 'A' && *p <= 'Z') ||
              (*p >= 'a' && *p <= 'z') ||
              (*p >= '0' && *p <= '9') ||
              *p == '_' || *p == '-' || *p == '.'))
            return 0;
    }
    return 1;
}

/* Exact match for PS4/PS5 title ids (9 chars: 4 letters + 5 digits). A bare
   strncmp(...,9) would also match longer ids that share a 9-char prefix and
   could collide PPSA12345 with PPSA12345EVIL. */
static int
title_id_eq9(const char *a, const char *b) {
    if (!a || !b) return 0;
    if (strnlen(a, 16) != 9 || strnlen(b, 16) != 9) return 0;
    return strncmp(a, b, 9) == 0;
}

static void
remember_install(const char *target_title_id, const char *method,
                 const ai_pkg_info_t *pkg, const char *fallback_content_id,
                 int rc) {
    char cid[AI_CONTENTID_SIZE] = {0};

    if (pkg)
        copy_bounded(cid, sizeof(cid), pkg->content_id, sizeof(pkg->content_id));
    if (!cid[0] && fallback_content_id)
        copy_bounded(cid, sizeof(cid), fallback_content_id, strlen(fallback_content_id));

    pthread_mutex_lock(&g_mtx);
    snprintf(g_last_content_id, sizeof(g_last_content_id), "%s", cid);
    snprintf(g_last_target_title_id, sizeof(g_last_target_title_id), "%s",
             target_title_id ? target_title_id : "");
    snprintf(g_last_method, sizeof(g_last_method), "%s", method ? method : "");
    g_last_start_rc = rc;
    pthread_mutex_unlock(&g_mtx);
}

/* Resolve (dlsym, never call) a list of candidate patch-install symbols and
   record which exist. Runs inside the backend thread, where the AppInstUtil
   module is already loaded — the same proven-safe context as the normal symbol
   resolution. No sysmod_load and no calls, so it is side-effect free. */
static void
fill_probe(void) {
    static const char *ai_syms[] = {
        "sceAppInstUtilInitialize",
        "sceAppInstUtilAppInstallPkg",
        "sceAppInstUtilAppInstallTitleDir",     /* takes an explicit title id */
        "sceAppInstUtilInstallByPackage",
        "sceAppInstUtilInstallByPackageEx",
        "sceAppInstUtilGetTitleIdFromPkg",
        "sceAppInstUtilGetContentIdFromPkg",
        "sceAppInstUtilGetInstallStatus",
        "sceAppInstUtilAppExist",
        "sceAppInstUtilAppGetInstallStatus",
        "sceAppInstUtilAppInstallStatus",
        "sceAppInstUtilAppUnInstall",
        "sceAppInstUtilGetMetaInfoFromPkg",
        "sceAppInstUtilUpdateTitleByTitleId",
        "sceAppInstUtilInstallByChunk",
        NULL
    };
    static const char *bgft_syms[] = {
        "sceBgftInitialize",
        "sceBgftServiceIntInit",
        "sceBgftServiceDownloadRegisterTask",
        "sceBgftServiceDownloadRegisterTaskByStorage",
        "sceBgftServiceDownloadRegisterTaskByStorageEx",
        "sceBgftServiceIntDownloadRegisterTaskByStorageEx",
        "sceBgftServiceDownloadStartTask",
        "sceBgftServiceDownloadGetProgress",
        "sceBgftServiceInstallPackage",
        NULL
    };
    uint32_t h = 0;
    int ai_loaded   = (kernel_dynlib_handle(-1, "libSceAppInstUtil.sprx", &h) >= 0);
    int bgft_loaded = (kernel_dynlib_handle(-1, "libSceBgft.sprx", &h) >= 0);
    char   tmp[sizeof(g_probe_json)];
    size_t n = 0, sz = sizeof(tmp);
    char  *out = tmp;
    int    first = 1;

    n += snprintf(out + n, sz - n,
                  "{\"appinstutil_loaded\":%s,\"bgft_loaded\":%s,\"symbols\":{",
                  ai_loaded ? "true" : "false", bgft_loaded ? "true" : "false");
    for (int i = 0; ai_syms[i] && n < sz - 80; i++) {
        intptr_t a = dynsym("libSceAppInstUtil.sprx", ai_syms[i]);
        n += snprintf(out + n, sz - n, "%s\"%s\":%s",
                      first ? "" : ",", ai_syms[i], a ? "true" : "false");
        first = 0;
    }
    for (int i = 0; bgft_syms[i] && n < sz - 80; i++) {
        intptr_t a = bgft_loaded ? dynsym("libSceBgft.sprx", bgft_syms[i]) : 0;
        n += snprintf(out + n, sz - n, "%s\"%s\":%s",
                      first ? "" : ",", bgft_syms[i], a ? "true" : "false");
        first = 0;
    }
    snprintf(out + n, sz - n, "}}");

    /* Publish atomically: the getter runs on an MHD worker thread and reads
       g_probe_json under the same lock, so it never sees a half-built buffer. */
    pthread_mutex_lock(&g_mtx);
    memcpy(g_probe_json, tmp, sizeof(g_probe_json));
    pthread_mutex_unlock(&g_mtx);
}

static void *
backend_init_thread(void *arg) {
    (void)arg;
    sysmod_load_fn sysmod_load;

    g_stage = 1;
    sysmod_load = (sysmod_load_fn)dynsym("libSceSysmodule.sprx",
                                         "sceSysmoduleLoadModuleInternal");
    if (!sysmod_load) { g_stage = -1; return NULL; }

    g_stage = 2;
    /* Dependencies first, then AppInstUtil. The sysmodule loader pulls in
       further deps and is the non-wedging path. */
    sysmod_load(SYSMOD_IPMI);
    sysmod_load(SYSMOD_USERSERVICE);
    sysmod_load(SYSMOD_SYSTEMSERVICE);
    g_err = sysmod_load(SYSMOD_APPINSTUTIL);
    if (g_err) { g_stage = -2; return NULL; }

    g_stage = 3;
    ai_initialize         = (ai_init_fn)dynsym("libSceAppInstUtil.sprx",
                                               "sceAppInstUtilInitialize");
    ai_install_pkg        = (ai_install_pkg_fn)dynsym("libSceAppInstUtil.sprx",
                                                      "sceAppInstUtilAppInstallPkg");
    ai_install_by_package = (ai_install_by_pkg_fn)dynsym("libSceAppInstUtil.sprx",
                                                         "sceAppInstUtilInstallByPackage");
    ai_title_from_pkg     = (ai_title_from_pkg_fn)dynsym("libSceAppInstUtil.sprx",
                                                         "sceAppInstUtilGetTitleIdFromPkg");
    ai_content_from_pkg   = (ai_content_from_pkg_fn)dynsym("libSceAppInstUtil.sprx",
                                                          "sceAppInstUtilGetContentIdFromPkg");
    ai_get_status         = (ai_get_status_fn)dynsym("libSceAppInstUtil.sprx",
                                                     "sceAppInstUtilGetInstallStatus");

    /* Read-only feasibility probe — module is loaded, safe context. */
    fill_probe();

    if (!ai_initialize || !ai_install_pkg || !ai_install_by_package) {
        g_stage = -3;
        return NULL;
    }

    g_stage = 4;
    g_err = ai_initialize();
    if (g_err) { g_stage = -4; return NULL; }

    g_stage = 5;
    return NULL;
}

static void
backend_start(void) {
    pthread_mutex_lock(&g_mtx);
    if (g_stage == 0) {
        pthread_t      tid;
        pthread_attr_t attr;
        g_stage = 1;
        pthread_attr_init(&attr);
        pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
        pthread_create(&tid, &attr, backend_init_thread, NULL);
        pthread_attr_destroy(&attr);
    }
    pthread_mutex_unlock(&g_mtx);
}

static const char *
stage_str(int s) {
    switch (s) {
    case 0:  return "idle";
    case 1:  return "resolving sysmodule loader";
    case 2:  return "loading AppInstUtil module";
    case 3:  return "resolving AppInstUtil symbols";
    case 4:  return "initializing service";
    case 5:  return "ready";
    case -1: return "sysmodule loader unavailable";
    case -2: return "AppInstUtil module load failed";
    case -3: return "AppInstUtil symbol resolution failed";
    case -4: return "AppInstUtil init failed";
    default: return "unknown";
    }
}

int
patchdl_install_backend_check(char *msg, size_t msg_sz) {
    int s;
    backend_start();
    s = g_stage;
    snprintf(msg, msg_sz, "%s (rc=0x%08x)", stage_str(s), (unsigned)g_err);
    return (s == 5) ? 0 : -1;
}

/* Public getter: trigger the backend (which fills the probe in its own thread)
   and return the cached result. Runs from the MHD worker thread, so it only
   reads the cached string — it never loads modules or resolves symbols here. */
int
patchdl_install_api_probe(char *out, size_t out_sz) {
    int ready;

    backend_start();
    pthread_mutex_lock(&g_mtx);
    ready = (g_probe_json[0] != '\0');
    if (ready)
        snprintf(out, out_sz, "%s", g_probe_json);
    pthread_mutex_unlock(&g_mtx);
    if (ready)
        return 0;

    snprintf(out, out_sz,
             "{\"pending\":true,\"stage\":\"%s\"}", stage_str(g_stage));
    return -1;
}

/* Read-only: report the .pkg's embedded content id + title id (and whether it
   is a full app vs a patch). No install, no side effects. 0 if anything read. */
int
patchdl_install_pkg_meta(const char *local_path, char *content_id, size_t cid_sz,
                         char *title_id, size_t tid_sz, int *is_app,
                         char *msg, size_t msg_sz) {
    char        sdk_path[1024];
    /* Padded output buffers — Sony's GetContentIdFromPkg / GetTitleIdFromPkg
       take no length hint; firmware may NUL-pad more than the visible id. */
    char        cid[AI_CONTENTID_OUT_SIZE] = {0};
    char        tid[AI_TITLEID_OUT_SIZE]   = {0};
    int         app_c = 0, app_t = 0, ok = 0;
    struct stat st;

    if (content_id && cid_sz) content_id[0] = '\0';
    if (title_id && tid_sz)   title_id[0]   = '\0';
    if (is_app) *is_app = 0;

    if (!local_path || !local_path[0] || stat(local_path, &st) != 0) {
        snprintf(msg, msg_sz, "package not on disk");
        return -1;
    }
    backend_start();
    if (g_stage != 5) {
        snprintf(msg, msg_sz, "install backend not ready: %s", stage_str(g_stage));
        return -1;
    }
    if (!strncmp(local_path, "/data/", 6))
        snprintf(sdk_path, sizeof sdk_path, "/user%s", local_path);
    else
        snprintf(sdk_path, sizeof sdk_path, "%s", local_path);

    if (ai_content_from_pkg &&
        ai_content_from_pkg(sdk_path, cid, &app_c) == 0 && cid[0]) {
        copy_bounded(content_id, cid_sz, cid, sizeof(cid));
        if (is_app) *is_app = app_c;
        ok = 1;
    }
    if (ai_title_from_pkg &&
        ai_title_from_pkg(sdk_path, tid, &app_t) == 0 && tid[0]) {
        copy_bounded(title_id, tid_sz, tid, sizeof(tid));
        ok = 1;
    }
    snprintf(msg, msg_sz, ok ? "ok" : "could not read pkg metadata");
    return ok ? 0 : -1;
}

void
patchdl_install_debug_state(char *out, size_t out_sz) {
    char cid[AI_CONTENTID_SIZE];
    char tid[32], method[32];
    int  start_rc;
    int  stage;

    pthread_mutex_lock(&g_mtx);
    memcpy(cid, g_last_content_id, sizeof(cid));
    snprintf(tid,    sizeof(tid),    "%s", g_last_target_title_id);
    snprintf(method, sizeof(method), "%s", g_last_method);
    start_rc = g_last_start_rc;
    stage    = g_stage;
    pthread_mutex_unlock(&g_mtx);

    snprintf(out, out_sz,
             "{\"stage\":%d,\"cid_len\":%d,\"cid_hex\":\"%02x%02x%02x%02x\","
             "\"content_id\":\"%s\",\"target_title_id\":\"%s\","
             "\"method\":\"%s\",\"start_rc\":%d}",
             stage,
             (int)strnlen(cid, sizeof(cid)),
             (unsigned char)cid[0], (unsigned char)cid[1],
             (unsigned char)cid[2], (unsigned char)cid[3],
             cid, tid, method, start_rc);
}

int
patchdl_install_status_json(char *out, size_t out_sz) {
    char cid[AI_CONTENTID_SIZE];
    char tid[32];
    char method[32];
    int  start_rc;
    /* ai_install_status_t carries a 2 KB safety pad; live on the heap so
       a frequently-polled /api/installstatus doesn't keep committing pages
       on every MHD worker's stack. */
    ai_install_status_t *st = NULL;
    char status[17], src_type[9];
    int  rc;
    int  progress = 0;
    int  terminal = 0;

    if (!out || !out_sz)
        return -1;

    backend_start();

    pthread_mutex_lock(&g_mtx);
    snprintf(cid, sizeof(cid), "%s", g_last_content_id);
    snprintf(tid, sizeof(tid), "%s", g_last_target_title_id);
    snprintf(method, sizeof(method), "%s", g_last_method);
    start_rc = g_last_start_rc;
    pthread_mutex_unlock(&g_mtx);

    if (!cid[0]) {
        snprintf(out, out_sz, "{\"active\":false}");
        return -1;
    }
    if (g_stage != 5) {
        snprintf(out, out_sz,
                 "{\"active\":true,\"content_id\":\"%s\",\"target_title_id\":\"%s\","
                 "\"method\":\"%s\",\"start_rc\":%d,\"status\":\"backend_not_ready\","
                 "\"stage\":\"%s\"}",
                 cid, tid, method, start_rc, stage_str(g_stage));
        return -1;
    }
    if (!ai_get_status) {
        snprintf(out, out_sz,
                 "{\"active\":true,\"content_id\":\"%s\",\"target_title_id\":\"%s\","
                 "\"method\":\"%s\",\"start_rc\":%d,\"status\":\"unavailable\","
                 "\"message\":\"sceAppInstUtilGetInstallStatus not exported\"}",
                 cid, tid, method, start_rc);
        return -1;
    }

    st = calloc(1, sizeof(*st));
    if (!st) {
        snprintf(out, out_sz, "{\"error\":\"oom\"}");
        return -1;
    }

    /* sceAppInstUtilGetInstallStatus(char *content_id_out, status_t *status):
       first arg is an OUTPUT buffer that receives the current install's content_id.
       Do NOT pass `cid` there — it would be overwritten. The visible id fits in
       0x30 bytes but Sony's NUL-pad length is unknown; use a padded buffer. */
    {
        char ai_cid_out[AI_CONTENTID_OUT_SIZE] = {0};
        rc = ai_get_status(ai_cid_out, st);
        (void)ai_cid_out; /* returned content_id for future use */
    }
    copy_bounded(status, sizeof(status), st->status, sizeof(st->status));
    copy_bounded(src_type, sizeof(src_type), st->src_type, sizeof(st->src_type));
    if (st->total_size > 0)
        progress = (int)((st->downloaded_size * 100) / st->total_size);
    if (progress < 0) progress = 0;
    if (progress > 100) progress = 100;
    terminal = (!strcmp(status, "playable") ||
                !strcmp(status, "error") ||
                !strcmp(status, "none"));

    snprintf(out, out_sz,
             "{\"active\":true,\"terminal\":%s,\"content_id\":\"%s\","
             "\"target_title_id\":\"%s\",\"method\":\"%s\",\"start_rc\":%d,"
             "\"rc\":%d,\"status\":\"%s\",\"src_type\":\"%s\","
             "\"progress\":%d,\"downloaded_size\":%llu,\"total_size\":%llu,"
             "\"promote_progress\":%u,\"error_code\":%d}",
             terminal ? "true" : "false", cid, tid, method, start_rc, rc,
             status, src_type, progress,
             (unsigned long long)st->downloaded_size,
             (unsigned long long)st->total_size,
             (unsigned)st->promote_progress,
             (int)st->error_info.error_code);
    free(st);
    return rc;
}

int
patchdl_install_local_pkg(const char *local_path, const char *expected_title_id,
                          const char *storage_title_id,
                          const char *target_content_id,
                          char *msg, size_t msg_sz) {
    char        sdk_path[1024];
    char        pkg_tid[48] = {0};
    struct stat st;
    int         rc;
    int         pkg_tid_mismatch = 0;

    if (!local_path || !local_path[0]) {
        snprintf(msg, msg_sz, "no package path");
        return -1;
    }
    if (stat(local_path, &st) != 0) {
        snprintf(msg, msg_sz, "package not downloaded");
        return -1;
    }

    backend_start();
    if (g_stage != 5) {
        snprintf(msg, msg_sz, "install backend not ready: %s", stage_str(g_stage));
        return -1;
    }

    /* Sony's installer sees the user partition as /user/data, not /data, and
       PATH-ALLOWLISTS the URI passed to InstallByPackage: /user/data/ and
       /mnt/usb are accepted, but a bare /data/... path is REJECTED with
       0x80B2116F (empirically confirmed by the ps5upload project). So feed the
       /user/data view of the file to both InstallByPackage and AppInstallPkg. */
    if (!strncmp(local_path, "/data/", 6))
        snprintf(sdk_path, sizeof(sdk_path), "/user%s", local_path);
    else
        snprintf(sdk_path, sizeof(sdk_path), "%s", local_path);

    /* Diagnostic guard: Sony sometimes stores one patch byte stream under a
       master title id while the version.xml targets a regional title id. That
       is valid only when the caller supplies target metadata, so do not feed
       such packages to the raw AppInstallPkg path. */
    if (storage_title_id && storage_title_id[0] &&
        expected_title_id && expected_title_id[0] &&
        !title_id_eq9(storage_title_id, expected_title_id)) {
        pkg_tid_mismatch = 1;
        strncpy(pkg_tid, storage_title_id, sizeof(pkg_tid) - 1);
        pkg_tid[sizeof(pkg_tid) - 1] = '\0';
    }
    if (ai_title_from_pkg && expected_title_id && expected_title_id[0]) {
        /* Sony's GetTitleIdFromPkg writes into the output buffer with no length
           hint — pad generously and copy the safe portion into pkg_tid. */
        char tid_out[AI_TITLEID_OUT_SIZE] = {0};
        int  is_app = 0;
        if (ai_title_from_pkg(sdk_path, tid_out, &is_app) == 0 && tid_out[0]) {
            if (!title_id_eq9(tid_out, expected_title_id))
                pkg_tid_mismatch = 1;
            copy_bounded(pkg_tid, sizeof(pkg_tid), tid_out, sizeof(tid_out));
        }
    }
    if (pkg_tid_mismatch && (!target_content_id || !target_content_id[0])) {
        snprintf(msg, msg_sz,
                 "refused: package metadata is %.12s, target is %.12s",
                 pkg_tid, expected_title_id);
        return -1;
    }
    /* Preferred path: etaHEN's DPI uses InstallByPackage with the installed
       game's content_id in MetaInfo so AppInstUtil binds the install to the
       right title slot. For shared-master cross-region packages the pkg bytes
       carry a different title id than the installed game; passing content_id
       is what etaHEN does to route the install correctly. */
    {
        char              file_uri[1100];
        char              http_loop_uri[1200] = {0};
        char              http_lan_uri[1200] = {0};
        const char       *uris[4];
        ai_meta_info_t    meta    = {0};
        ai_pkg_info_t     pkg     = {0};
        /* playgo is 0x2700 bytes — too big for the MHD worker stack alongside
           uris, meta, pkg, resp buffers, and Sony's own frame use. */
        ai_playgo_info_t *playgo  = calloc(1, sizeof(*playgo));
        int               rc2     = -1;
        const char       *title_dir;
        const char       *file_base;

        if (!playgo) {
            snprintf(msg, msg_sz, "out of memory");
            return -1;
        }

        snprintf(file_uri, sizeof(file_uri), "file://%s", sdk_path);
        title_dir = strstr(local_path, "/data/patchdl/");
        file_base = strrchr(local_path, '/');
        if (title_dir && file_base && file_base > title_dir + strlen("/data/patchdl/")) {
            char title_id[32] = {0};
            const char *t = title_dir + strlen("/data/patchdl/");
            size_t tlen = (size_t)(file_base - t);
            if (tlen > 0 && tlen < sizeof(title_id)) {
                char ip[INET_ADDRSTRLEN] = {0};
                memcpy(title_id, t, tlen);
                /* CRLF/path-injection guard: anything we splice into the loop /
                   LAN URI lands inside Sony's HTTP request line. Reject ids
                   or filenames carrying delimiters or control chars. */
                if (install_id_safe(title_id) && install_id_safe(file_base + 1)) {
                    snprintf(http_loop_uri, sizeof(http_loop_uri),
                             "http://127.0.0.1:%d/api/pkg/%s/%s",
                             PATCHDL_HTTP_PORT, title_id, file_base + 1);
                    local_ip(ip, sizeof(ip));
                    if (ip[0])
                        snprintf(http_lan_uri, sizeof(http_lan_uri),
                                 "http://%s:%d/api/pkg/%s/%s",
                                 ip, PATCHDL_HTTP_PORT, title_id, file_base + 1);
                }
            }
        }
        uris[0]                 = sdk_path;      /* /user/data/... — the allowlisted path */
        uris[1]                 = file_uri;       /* file:///user/data/... */
        uris[2]                 = http_loop_uri[0] ? http_loop_uri : NULL;
        uris[3]                 = http_lan_uri[0] ? http_lan_uri : NULL;
        meta.ex_uri             = "";
        meta.playgo_scenario_id = "";
        /* For cross-region shared-master packages pass the installed game's
           content_id so AppInstUtil binds the download to the right title. */
        meta.content_id         = (pkg_tid_mismatch &&
                                    target_content_id && target_content_id[0])
                                   ? target_content_id : "";
        meta.content_name       = "PatchDL";
        meta.icon_url           = "";

        {
            char        tries[260] = {0};
            const char *labels[4]  = { "userdata", "file", "loop", "lan" };
            for (int i = 0; i < 4; i++) {
                if (!uris[i]) continue;
                memset(&pkg, 0, sizeof(pkg));
                memset(playgo, 0, sizeof(*playgo));
                meta.uri = uris[i];
                rc2 = ai_install_by_package(&meta, &pkg, playgo);
                {
                    size_t l = strlen(tries);
                    snprintf(tries + l, sizeof(tries) - l, "%s%s=0x%08x",
                             l ? "," : "", labels[i], (unsigned)rc2);
                }
                if (rc2 == 0) {
                    remember_install(expected_title_id, "InstallByPackage",
                                     &pkg, target_content_id, rc2);
                    snprintf(msg, msg_sz, "install started (InstallByPackage, content %.47s)",
                             pkg.content_id[0] ? pkg.content_id :
                             (target_content_id ? target_content_id : ""));
                    free(playgo);
                    return 0;
                }
            }
            rc = rc2;
        }
        free(playgo);
    }

    /* AppInstallPkg: simpler API, no MetaInfo content_id override. Tried for
       all packages including cross-region, since it may have different
       privilege requirements than InstallByPackage. For shared-master packages
       it will bind to the pkg's own embedded title, not the expected_title_id,
       so treat success with caution; also report the complete error set. */
    {
        char          ibp_tries[260] = {0};
        ai_pkg_info_t pkg = {0};
        int rc2;

        /* stash the InstallByPackage diagnostic if available */
        if (pkg_tid_mismatch)
            snprintf(ibp_tries, sizeof(ibp_tries),
                     "ibp=0x%08x(pkg %.12s->%.12s)",
                     (unsigned)rc, pkg_tid,
                     expected_title_id ? expected_title_id : "");

        rc2 = ai_install_pkg(sdk_path, &pkg);
        if (rc2 == 0) {
            remember_install(expected_title_id, "AppInstallPkg",
                             &pkg, target_content_id, rc2);
            snprintf(msg, msg_sz, "install started (AppInstallPkg, content %.47s%s%s)",
                     pkg.content_id[0] ? pkg.content_id :
                     (target_content_id ? target_content_id : ""),
                     ibp_tries[0] ? " " : "", ibp_tries);
            return 0;
        }
        if (pkg_tid_mismatch)
            snprintf(msg, msg_sz,
                     "install rejected (pkg %.12s->%.12s ibp=0x%08x aip=0x%08x)",
                     pkg_tid, expected_title_id ? expected_title_id : "",
                     (unsigned)rc, (unsigned)rc2);
        else
            snprintf(msg, msg_sz,
                     "install rejected (InstallByPackage=0x%08x, AppInstallPkg=0x%08x)",
                     (unsigned)rc, (unsigned)rc2);
        return rc2 ? rc2 : (rc ? rc : -1);
    }
}

int
patchdl_install_by_uri(const char *uri, const char *target_title_id,
                       const char *target_content_id,
                       char *msg, size_t msg_sz) {
    ai_meta_info_t    meta   = {0};
    ai_pkg_info_t     pkg    = {0};
    ai_playgo_info_t *playgo;
    int rc;

    if (!uri || !uri[0]) {
        snprintf(msg, msg_sz, "no uri");
        return -1;
    }
    backend_start();
    if (g_stage != 5) {
        snprintf(msg, msg_sz, "install backend not ready: %s", stage_str(g_stage));
        return -1;
    }
    playgo = calloc(1, sizeof(*playgo));
    if (!playgo) { snprintf(msg, msg_sz, "out of memory"); return -1; }

    meta.uri                = uri;
    meta.ex_uri             = "";
    meta.playgo_scenario_id = "";
    meta.content_id         = target_content_id ? target_content_id : "";
    meta.content_name       = "PatchDL";
    meta.icon_url           = "";

    rc = ai_install_by_package(&meta, &pkg, playgo);
    remember_install(target_title_id, "InstallByURI", &pkg, target_content_id, rc);
    free(playgo);

    if (rc == 0) {
        snprintf(msg, msg_sz, "install started (InstallByPackage/uri, content %.47s)",
                 pkg.content_id[0] ? pkg.content_id :
                 (target_content_id ? target_content_id : ""));
    } else {
        snprintf(msg, msg_sz, "install rejected rc=0x%08x", (unsigned)rc);
    }
    return rc;
}

/* Direct AppInstallPkg call for a local path — bypasses MetaInfo, lets
   AppInstUtil read the PKG's own embedded metadata to determine the target. */
int
patchdl_install_app_pkg(const char *local_path,
                        const char *expected_title_id,
                        const char *target_content_id,
                        char *msg, size_t msg_sz) {
    char          sdk_path[1024];
    ai_pkg_info_t pkg = {0};
    struct stat   st;
    int           rc;

    if (!local_path || !local_path[0]) { snprintf(msg, msg_sz, "no path"); return -1; }
    if (stat(local_path, &st) != 0)    { snprintf(msg, msg_sz, "package not downloaded"); return -1; }

    backend_start();
    if (g_stage != 5) {
        snprintf(msg, msg_sz, "install backend not ready: %s", stage_str(g_stage));
        return -1;
    }

    if (!strncmp(local_path, "/data/", 6))
        snprintf(sdk_path, sizeof sdk_path, "/user%s", local_path);
    else
        snprintf(sdk_path, sizeof sdk_path, "%s", local_path);

    rc = ai_install_pkg(sdk_path, &pkg);
    remember_install(expected_title_id, "AppInstallPkg/direct", &pkg, target_content_id, rc);

    if (rc == 0)
        snprintf(msg, msg_sz, "install started (AppInstallPkg, content %.47s)",
                 pkg.content_id[0] ? pkg.content_id :
                 (target_content_id ? target_content_id : ""));
    else
        snprintf(msg, msg_sz, "install rejected rc=0x%08x", (unsigned)rc);
    return rc;
}
