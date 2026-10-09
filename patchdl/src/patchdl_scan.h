/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Knutwurst
 *
 * PatchDL is free software: you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free
 * Software Foundation, either version 3 of the License, or (at your option)
 * any later version. See the LICENSE file in the project root for details.
 */

#pragma once

#include <stddef.h>
#include <stdint.h>

typedef enum {
    PATCHDL_SOURCE_OFFICIAL    = 0,
    PATCHDL_SOURCE_EXTERNAL    = 1,
    PATCHDL_SOURCE_SHADOWMOUNT = 2,
    PATCHDL_SOURCE_UNKNOWN     = 3,
} patchdl_source_t;

typedef struct {
    char             title_id[32];
    char             content_id[64];
    char             name[128];
    char             installed_version[16];
    char             source_path[256];
    char             mount_from[256];
    char             version_file_uri[256]; /* from app.db, for version.xml */
    patchdl_source_t source_type;
    /* populated by background verxml fetch — guarded by websrv mutex */
    char             compatible_version[16];
    char             latest_version[16];
    char             latest_required_fw[16];
    char             patch_url[512];   /* manifest_url if present, otherwise pkg URL */
    char             delta_url[512];   /* DP.pkg bootstrap URL (always a PKG) */
    char             patch_title_id[16]; /* target title id from version.xml */
    char             patch_storage_title_id[16]; /* title id embedded in delta_url */
    int              verxml_done;
    int              enabled;          /* user policy, persisted in config.json */
    int              resumable;        /* a partial download is on disk */
    long long        partial_bytes;    /* size of that partial, for the UI */
} patchdl_title_t;

int         patchdl_scan(patchdl_title_t **titles_out, size_t *count_out);
void        patchdl_scan_free(patchdl_title_t *titles, size_t count);
const char *patchdl_source_str(patchdl_source_t src);

/* Diagnostic: malloc'd JSON dump of the mount table + scan-base directory
   listings. Caller frees. */
char       *patchdl_scan_debug_json(void);

/* Mark scan/debug as no longer safe to call (must be set after MHD worker
   threads come up — patchdl_scan performs a process-wide vnode swap that
   would race any concurrent thread). After this is set, both entry points
   return immediately. Call once during startup, after MHD_start_daemon. */
void        patchdl_scan_lock(void);
