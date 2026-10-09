/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Knutwurst
 *
 * PatchDL is free software: you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free
 * Software Foundation, either version 3 of the License, or (at your option)
 * any later version. See the LICENSE file in the project root for details.
 */

#include "patchdl_appdb.h"

#include <sqlite3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define APP_DB_URI \
    "file:/system_data/priv/mms/app.db?immutable=1"

#define APPDB_MAX 1024

/* Extract a string value for `key` from a flat PS5 AppInfoJson blob.
   Trailing whitespace (Sony pads VERSION_FILE_URI) is stripped. */
static void
json_str(const char *json, const char *key, char *out, size_t out_sz) {
    char        needle[48];
    const char *p;
    size_t      len;

    out[0] = '\0';
    if ((size_t)snprintf(needle, sizeof(needle), "\"%s\"", key) >= sizeof(needle))
        return;

    p = strstr(json, needle);
    if (!p) return;
    p += strlen(needle);
    while (*p == ' ' || *p == '\t' || *p == ':' || *p == '\n' || *p == '\r')
        p++;
    if (*p != '"') return;
    p++;
    /* Copy with minimal JSON unescaping — AppInfoJson escapes URL slashes
       as "\/", so VERSION_FILE_URI arrives as "https:\/\/..." otherwise. */
    len = 0;
    while (*p && *p != '"' && len < out_sz - 1) {
        if (*p == '\\' && p[1]) {
            p++;
            switch (*p) {
            case 'n': out[len++] = '\n'; break;
            case 't': out[len++] = '\t'; break;
            case 'r': out[len++] = '\r'; break;
            default:  out[len++] = *p;   break; /* \/  \\  \"  -> literal */
            }
            p++;
            continue;
        }
        out[len++] = *p++;
    }
    out[len] = '\0';
    while (len > 0 && (out[len - 1] == ' ' || out[len - 1] == '\t'))
        out[--len] = '\0';
}

int
patchdl_appdb_load(patchdl_appinfo_t **out, size_t *count) {
    sqlite3      *db = NULL;
    sqlite3_stmt *stmt = NULL;
    patchdl_appinfo_t *arr;
    size_t        n = 0;

    *out = NULL;
    *count = 0;

    /* FULLMUTEX: today only the startup thread calls this, but future code
       paths (a manual rescan triggered from the HTTP thread) would otherwise
       race the SQLite handle. Cost is one mutex per call. */
    if (sqlite3_open_v2(APP_DB_URI, &db,
                        SQLITE_OPEN_READONLY | SQLITE_OPEN_URI |
                        SQLITE_OPEN_FULLMUTEX, NULL) != SQLITE_OK) {
        if (db) sqlite3_close(db);
        return -1;
    }

    if (sqlite3_prepare_v2(db,
            "SELECT titleId, titleName, contentId, AppInfoJson "
            "FROM tbl_contentinfo;", -1, &stmt, NULL) != SQLITE_OK) {
        sqlite3_close(db);
        return -1;
    }

    arr = calloc(APPDB_MAX, sizeof(*arr));
    if (!arr) {
        sqlite3_finalize(stmt);
        sqlite3_close(db);
        return -1;
    }

    while (sqlite3_step(stmt) == SQLITE_ROW && n < APPDB_MAX) {
        patchdl_appinfo_t *e = &arr[n];
        const char *tid  = (const char *)sqlite3_column_text(stmt, 0);
        const char *name = (const char *)sqlite3_column_text(stmt, 1);
        const char *cid  = (const char *)sqlite3_column_text(stmt, 2);
        const char *json = (const char *)sqlite3_column_text(stmt, 3);

        if (!tid) continue;
        strncpy(e->title_id, tid, sizeof(e->title_id) - 1);
        if (name) strncpy(e->title_name, name, sizeof(e->title_name) - 1);
        if (cid)  strncpy(e->content_id, cid, sizeof(e->content_id) - 1);
        if (json) {
            json_str(json, "CONTENT_VERSION", e->content_version,
                     sizeof(e->content_version));
            json_str(json, "VERSION_FILE_URI", e->version_file_uri,
                     sizeof(e->version_file_uri));
        }
        n++;
    }

    sqlite3_finalize(stmt);
    sqlite3_close(db);

    *out = arr;
    *count = n;
    return 0;
}

void
patchdl_appdb_free(patchdl_appinfo_t *arr) {
    free(arr);
}

const patchdl_appinfo_t *
patchdl_appdb_find(const patchdl_appinfo_t *arr, size_t count,
                   const char *title_id) {
    for (size_t i = 0; i < count; i++)
        if (!strcmp(arr[i].title_id, title_id))
            return &arr[i];
    return NULL;
}
