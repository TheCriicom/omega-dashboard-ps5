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

typedef struct {
    char  *data;
    size_t size;
    size_t cap;
    size_t max;     /* 0 = unbounded (legacy). Otherwise write_cb fails past this. */
} patchdl_buf_t;

patchdl_buf_t *patchdl_buf_new(void);
void           patchdl_buf_free(patchdl_buf_t *b);

/* Call once, single-threaded, before any concurrent download worker starts /
   after they have all joined. curl's global/OpenSSL init is otherwise lazy and
   races across threads. */
void patchdl_net_global_init(void);
void patchdl_net_global_cleanup(void);

int patchdl_http_get(const char *url, patchdl_buf_t *out);

/* ---- parallel piece download (used by the connection pool) ------------- */

/* One piece of a split manifest package. `url` is heap-allocated. */
typedef struct {
    char     *url;
    long long offset;     /* byte offset of this piece in the assembled file */
    long long size;       /* exact length of this piece */
    char      hash[80];   /* manifest SHA-256 hex, or "" */
} patchdl_piece_t;

typedef struct {
    patchdl_piece_t *pieces;
    int              count;
    long long        total;   /* assembled file size = sum of piece sizes */
} patchdl_manifest_t;

/* Fetch + parse a Sony JSON manifest into a validated, contiguously-tiled
   piece list. Returns 0 on success (caller frees with patchdl_manifest_free),
   -1 on fetch/parse/tiling failure. */
int  patchdl_fetch_manifest(const char *manifest_url, patchdl_manifest_t *out);
void patchdl_manifest_free(patchdl_manifest_t *m);

/* Live state shared with one in-flight piece download. The worker owns these;
   the curl callbacks read `abort` (set elsewhere) and publish progress into
   `bytes_slot` (single-writer per worker slot). */
typedef struct {
    volatile long long *bytes_slot;  /* bytes written so far for this piece */
    volatile int       *abort;       /* non-zero -> stop this transfer */
} patchdl_piece_ctx_t;

/* Download one whole piece and pwrite it into `fd` at `file_offset`. Concurrent
   non-overlapping pieces of the same fd are safe. Returns 0 on success (and
   fdatasyncs fd), -1 on network/IO/abort, -2 on a SHA-256 mismatch.
   `curl_rc_out`/`http_code_out` (either may be NULL) receive the last libcurl
   CURLcode + HTTP status for failure diagnosis. */
int patchdl_http_download_piece(const char *url, int fd,
                                long long file_offset, long long file_size,
                                const char *expected_sha256_or_null,
                                patchdl_piece_ctx_t *ctx,
                                int *curl_rc_out, long *http_code_out);

/* Read-only: SHA-256 a [offset, offset+size) region of fd into out_hex
   (caller provides >= 65 bytes). Returns 0 on success. */
int patchdl_sha256_fd_region(int fd, long long offset, long long size,
                             char *out_hex);

/* Progress callback. Return non-zero to ABORT the in-flight download (used to
   cancel large patch downloads); return 0 to continue. */
typedef int (*patchdl_download_progress_cb)(void *ctx,
                                            long long downloaded,
                                            long long total);

/* Stream a URL to a file on disk (for large PKG downloads). Returns 0 on
   success and writes the byte count to *bytes_out. */
int patchdl_http_download(const char *url, const char *dest_path,
                          long long *bytes_out);
int patchdl_http_download_progress(const char *url, const char *dest_path,
                                   long long *bytes_out,
                                   patchdl_download_progress_cb cb, void *ctx);

/* Download a Sony JSON package manifest by concatenating every entry in
   "pieces" into one installable PKG. When `verify` is non-zero each piece is
   checked against its manifest SHA-256 (a mismatch returns -2). When `resume`
   is non-zero an existing partial at dest_path is kept: fully-downloaded pieces
   are skipped and only the remainder is fetched (survives a reboot). On any
   failure the partial is left in place for a later resume. */
int patchdl_http_download_manifest(const char *manifest_url, const char *dest_path,
                                   long long *bytes_out);
int patchdl_http_download_manifest_progress(const char *manifest_url,
                                            const char *dest_path,
                                            long long *bytes_out,
                                            patchdl_download_progress_cb cb,
                                            void *ctx, int verify, int resume);

/* Diagnostic: run the GET pipeline for `url` and write a JSON report
   (dns result/ip, curl code, http status, bytes) into `out_json`. */
void patchdl_net_diag(const char *url, char *out_json, size_t sz);
