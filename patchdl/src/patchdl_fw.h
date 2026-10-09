/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Knutwurst
 *
 * PatchDL is free software: you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free
 * Software Foundation, either version 3 of the License, or (at your option)
 * any later version. See the LICENSE file in the project root for details.
 */

#pragma once

#include <stdint.h>

typedef struct {
    char     str[0x1c]; /* e.g. "11.60" */
    uint32_t bin;       /* e.g. 0x11600000 */
} patchdl_fw_t;

int patchdl_fw_get(patchdl_fw_t *out);
