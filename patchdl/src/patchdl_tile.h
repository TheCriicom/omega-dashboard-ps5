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

/* Install / refresh the PatchDL home-screen tile.
 *
 * Writes param.json + icon0.png into /user/app/<TITLE_ID>/sce_sys/ and asks
 * the on-console installer to register the directory as an app. The tile's
 * deeplinkUri opens the on-console browser at http://127.0.0.1:12880/, i.e.
 * PatchDL's own UI — only useful while the ELF is running.
 *
 * No package, no code signing — files only; the installer reads the
 * directory directly.
 *
 * stat-guard: the asset bytes are diffed against the on-disk copy first;
 * if nothing changed the install call isn't made at all (avoids a costly
 * re-register every payload start). Returns 0 on success or when nothing
 * needed doing.
 */
int patchdl_tile_install_if_needed(char *msg, size_t msg_sz);
