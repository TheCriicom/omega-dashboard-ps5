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

/*
 * Install a local PKG (a downloaded patch) via Sony's AppInstUtil service.
 * `local_path` is the on-disk path under /data/patchdl/... The install runs
 * asynchronously in the system installer queue once started; this returns 0
 * when the install was accepted, non-zero (the SCE error code) otherwise, and
 * writes a short human-readable status into `msg`.
 *
 * This MODIFIES the installed game. The caller must gate it to genuine
 * (official) titles and obtain explicit user intent before calling.
 */
/* `expected_title_id` is the title id of the installed game the patch is for.
   `storage_title_id` is the title id embedded in the delta_url storage path.
   `target_content_id` is the installed game's content id from app.db; it is
   retained for diagnostics and status fallback. Normal same-title installs
   deliberately pass an empty MetaInfo.content_id, matching etaHEN's native DPI
   path and letting AppInstUtil bind the package to its signed metadata. */
int patchdl_install_local_pkg(const char *local_path,
                              const char *expected_title_id,
                              const char *storage_title_id,
                              const char *target_content_id,
                              char *msg, size_t msg_sz);

/* Verify the AppInstUtil backend can be loaded + resolved + initialized,
   WITHOUT performing any install. Returns 0 if ready. Safe to call. */
int patchdl_install_backend_check(char *msg, size_t msg_sz);

/* Read-only feasibility probe: resolve (dlsym, never call) a list of candidate
   AppInstUtil/Bgft patch-install symbols and report which exist on this
   firmware. Writes a JSON object into `out`. No install, no side effects. */
int patchdl_install_api_probe(char *out, size_t out_sz);

/* Read-only: report the .pkg's embedded content id + title id (and whether it
   is a full app vs a patch, via *is_app). No install. 0 if anything was read. */
int patchdl_install_pkg_meta(const char *local_path, char *content_id, size_t cid_sz,
                             char *title_id, size_t tid_sz, int *is_app,
                             char *msg, size_t msg_sz);

/* Read-only: report the last AppInstUtil install task PatchDL started, using
   sceAppInstUtilGetInstallStatus when present. No install, no mutation. */
int patchdl_install_status_json(char *out, size_t out_sz);

/* Raw dump of g_last_* tracking state (no AppInstUtil call). For diagnosis. */
void patchdl_install_debug_state(char *out, size_t out_sz);

/* Install a package from a remote URI (http:// or file://) directly, without
 * requiring a local copy. Used for shared-master delta packages where the
 * version.xml targets a different title id than the CDN storage path.
 * `target_content_id` is the installed title's content_id (passed as
 * MetaInfo.content_id so AppInstUtil binds the install to the right title).
 */
int patchdl_install_by_uri(const char *uri,
                            const char *target_title_id,
                            const char *target_content_id,
                            char *msg, size_t msg_sz);

/* Directly call sceAppInstUtilAppInstallPkg for a local file. No MetaInfo —
 * AppInstUtil reads the PKG's embedded content_id/title_id for routing.
 * Use when InstallByPackage is unavailable (privilege). */
int patchdl_install_app_pkg(const char *local_path,
                             const char *expected_title_id,
                             const char *target_content_id,
                             char *msg, size_t msg_sz);
