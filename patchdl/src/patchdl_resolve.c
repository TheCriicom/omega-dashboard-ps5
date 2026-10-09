/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Knutwurst
 *
 * PatchDL is free software: you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free
 * Software Foundation, either version 3 of the License, or (at your option)
 * any later version. See the LICENSE file in the project root for details.
 */

#include "patchdl_resolve.h"

#include <stdio.h>
#include <string.h>

#define DB_PATH "/data/patchdl/db/ps5_xml.tsv"

static int
lookup_tsv(const char *title_id, char *url_out, size_t url_sz) {
    FILE *fp;
    char  line[1024];

    fp = fopen(DB_PATH, "r");
    if (!fp) return -1;

    while (fgets(line, sizeof(line), fp)) {
        char *tab = strchr(line, '\t');
        if (!tab) continue;
        *tab = '\0';
        if (strcmp(line, title_id)) continue;

        char *url = tab + 1;
        size_t len = strlen(url);
        while (len > 0 && (url[len - 1] == '\n' || url[len - 1] == '\r'))
            url[--len] = '\0';
        if (len == 0 || len >= url_sz) {
            fclose(fp);
            return -1;
        }
        /* Defense in depth: the TSV file lives under /data/patchdl, writable
           by anyone with /data access. patchdl_http_get also enforces
           host_allowed, but rejecting non-https / non-Sony schemes here means
           a poisoned line can't even reach the network layer. */
        if (strncmp(url, "https://", 8) != 0) {
            fclose(fp);
            return -1;
        }
        memcpy(url_out, url, len + 1);
        fclose(fp);
        return 0;
    }
    fclose(fp);
    return -1;
}

int
patchdl_resolve_url(const char *title_id, char *url_out, size_t url_sz) {
    if (!title_id) return -1;

    /* Only PS5 native title IDs (PPSA/PPSE/PPSH/PPSJ/PPSC) */
    if (strncmp(title_id, "PPSA", 4) &&
        strncmp(title_id, "PPSE", 4) &&
        strncmp(title_id, "PPSH", 4) &&
        strncmp(title_id, "PPSJ", 4) &&
        strncmp(title_id, "PPSC", 4))
        return -1;

    return lookup_tsv(title_id, url_out, url_sz);
}
