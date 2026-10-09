/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Knutwurst
 *
 * PatchDL is free software: you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free
 * Software Foundation, either version 3 of the License, or (at your option)
 * any later version. See the LICENSE file in the project root for details.
 */

#pragma once

/* Set this process's (main thread) name as shown in the PS5 process list. */
void patchdl_proc_set_name(const char *name);

/* SIGKILL any other process whose name matches `name` (excluding self),
   so a freshly launched payload replaces a stale instance cleanly.
   Returns the number of processes killed. */
int patchdl_proc_kill_others(const char *name);
