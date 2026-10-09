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

typedef struct patchdl_asset {
  const char* path;
  const void* data;
  size_t size;
  const char* mime;
  struct patchdl_asset* next;
} patchdl_asset_t;

void patchdl_asset_register(const char* path, const void* data, size_t size,
                            const char* mime);
const patchdl_asset_t* patchdl_asset_find(const char* path);
