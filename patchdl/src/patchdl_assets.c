/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Knutwurst
 *
 * PatchDL is free software: you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free
 * Software Foundation, either version 3 of the License, or (at your option)
 * any later version. See the LICENSE file in the project root for details.
 */

#include "patchdl_assets.h"

#include <stdlib.h>
#include <string.h>

static patchdl_asset_t* asset_head;

void
patchdl_asset_register(const char* path, const void* data, size_t size,
                       const char* mime) {
  patchdl_asset_t* asset = calloc(1, sizeof(*asset));

  if(!asset) {
    return;
  }

  asset->path = path;
  asset->data = data;
  asset->size = size;
  asset->mime = mime;
  asset->next = asset_head;
  asset_head = asset;
}

const patchdl_asset_t*
patchdl_asset_find(const char* path) {
  const patchdl_asset_t* asset = asset_head;

  if(!path || !strcmp(path, "/")) {
    path = "/index.html";
  }

  while(asset) {
    if(!strcmp(asset->path, path)) {
      return asset;
    }
    asset = asset->next;
  }

  return 0;
}
