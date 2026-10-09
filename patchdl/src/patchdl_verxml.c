/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Knutwurst
 *
 * PatchDL is free software: you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free
 * Software Foundation, either version 3 of the License, or (at your option)
 * any later version. See the LICENSE file in the project root for details.
 */

#include "patchdl_verxml.h"
#include "patchdl_net.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Convert packed fw bin to "11.60" string */
static void
fw_bin_to_str(uint32_t bin, char *out, size_t sz) {
    /* Packed firmware: hex digits read as decimal (0x10200006 -> "10.20"),
       same scheme as patchdl_fw. */
    snprintf(out, sz, "%x.%02x",
             (bin >> 24) & 0xff,
             (bin >> 16) & 0xff);
}

static uint32_t
parse_hex(const char *s) {
    uint32_t v = 0;
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X'))
        sscanf(s + 2, "%x", &v);
    else
        sscanf(s, "%u", &v);
    return v;
}

/* Extract attribute value from within a <package ...> tag span.
   tag_start points to '<', tag_end points past '>'. */
static int
attr_val(const char *tag_start, const char *tag_end, const char *attr,
         char *out, size_t out_sz) {
    char needle[64];
    const char *p;
    size_t len;

    if ((size_t)snprintf(needle, sizeof(needle), " %s=\"", attr) >= sizeof(needle))
        return -1;

    p = tag_start;
    while (p < tag_end) {
        p = strstr(p, needle);
        if (!p || p >= tag_end) return -1;
        p += strlen(needle);
        for (len = 0; p + len < tag_end && p[len] != '"' && len < out_sz - 1; len++)
            ;
        /* The value must actually end on its closing quote inside the tag —
           otherwise we hit tag_end or the buffer limit and would return a
           silently truncated URL/title as if it were valid. */
        if (p + len >= tag_end || p[len] != '"')
            return -1;
        memcpy(out, p, len);
        out[len] = '\0';
        return 0;
    }
    return -1;
}

/* version strings are zero-padded (e.g. "01.041.000") — strcmp orders correctly */
static int
ver_gt(const char *a, const char *b) {
    return strcmp(a, b) > 0;
}

/* Extract the first PS4/PS5 title id token ([A-Z]{4}[0-9]{5}, e.g. PPSA03099)
   from a string such as nptitleid, manifest_url, or delta_url. */
static void
extract_title_id(const char *s, char *out, size_t sz) {
    size_t len;
    out[0] = '\0';
    if (sz < 10 || !s) return;
    len = strlen(s);
    if (len < 9) return;
    /* `len - 9` is the last position where a 9-char id can still fit; this
       avoids reading p[8] past the NUL terminator. */
    for (size_t i = 0; i <= len - 9; i++) {
        const char *p = s + i;
        int ok = 1;
        for (int k = 0; k < 4 && ok; k++)
            if (p[k] < 'A' || p[k] > 'Z') ok = 0;
        for (int k = 4; k < 9 && ok; k++)
            if (p[k] < '0' || p[k] > '9') ok = 0;
        if (ok) {
            memcpy(out, p, 9);
            out[9] = '\0';
            return;
        }
    }
}

static void
parse_root_title_id(const char *xml, char *out, size_t sz) {
    const char *p;
    const char *tag_end;
    char nptitleid[64] = {0};

    out[0] = '\0';
    if (!xml || sz < 10) return;

    p = strstr(xml, "<title_patch");
    if (!p) return;
    tag_end = strchr(p, '>');
    if (!tag_end) return;
    tag_end++;

    if (!attr_val(p, tag_end, "nptitleid", nptitleid, sizeof(nptitleid)))
        extract_title_id(nptitleid, out, sz);
}

static void
parse_packages(const char *xml, uint32_t fw_bin, patchdl_verinfo_t *out) {
    const char *p = xml;
    char root_title[16] = {0};

    parse_root_title_id(xml, root_title, sizeof(root_title));

    while ((p = strstr(p, "<package "))) {
        const char *tag_end = strchr(p, '>');
        if (!tag_end) break;
        tag_end++;

        char ver[16]  = {0};
        char sver[16] = {0};
        char durl[512] = {0};
        char murl[512] = {0};

        /* PS5 version.xml uses content_ver; PS4 uses version. */
        if (attr_val(p, tag_end, "content_ver", ver, sizeof(ver)))
            attr_val(p, tag_end, "version", ver, sizeof(ver));
        attr_val(p, tag_end, "system_ver", sver, sizeof(sver));
        attr_val(p, tag_end, "delta_url", durl, sizeof(durl));
        attr_val(p, tag_end, "manifest_url", murl, sizeof(murl));

        if (ver[0] && sver[0]) {
            uint32_t pkg_sver = parse_hex(sver);

            /* Track latest overall */
            if (!out->latest_version[0] || ver_gt(ver, out->latest_version)) {
                strncpy(out->latest_version, ver, sizeof(out->latest_version) - 1);
                fw_bin_to_str(pkg_sver, out->latest_required_fw,
                              sizeof(out->latest_required_fw));
            }

            /* Track latest compatible + its installable patch source. PS5
               updates expose a small delta_url (DP.pkg) plus a manifest_url
               containing the actual split package pieces. Feeding the DP
               bootstrap directly can make the system download the full patch
               under the storage/master title id, so prefer the target-title
               manifest whenever present. */
            if (pkg_sver <= fw_bin) {
                if (!out->compatible_version[0] ||
                    ver_gt(ver, out->compatible_version)) {
                    strncpy(out->compatible_version, ver,
                            sizeof(out->compatible_version) - 1);
                    strncpy(out->compatible_url, murl[0] ? murl : durl,
                            sizeof(out->compatible_url) - 1);
                    if (durl[0])
                        strncpy(out->delta_url, durl, sizeof(out->delta_url) - 1);
                    extract_title_id(durl, out->compatible_storage_title,
                                     sizeof(out->compatible_storage_title));
                    if (root_title[0]) {
                        strncpy(out->compatible_title, root_title,
                                sizeof(out->compatible_title) - 1);
                    } else {
                        extract_title_id(murl, out->compatible_title,
                                         sizeof(out->compatible_title));
                        if (!out->compatible_title[0])
                            extract_title_id(durl, out->compatible_title,
                                             sizeof(out->compatible_title));
                    }
                }
            }
        }

        p = tag_end;
    }
}

int
patchdl_verxml_query(const char *url, uint32_t fw_bin, patchdl_verinfo_t *out) {
    patchdl_buf_t buf;

    if (!url || !out) return -1;
    memset(out, 0, sizeof(*out));

    /* version.xml is a few KB in practice; cap so a misbehaving CDN can't
       slurp unbounded RAM into the buffer. */
    memset(&buf, 0, sizeof(buf));
    buf.max = 4 * 1024 * 1024;
    if (patchdl_http_get(url, &buf)) return -1;
    if (!buf.data || !buf.size) { free(buf.data); return -1; }

    parse_packages(buf.data, fw_bin, out);
    free(buf.data);

    return (out->latest_version[0]) ? 0 : -1;
}
