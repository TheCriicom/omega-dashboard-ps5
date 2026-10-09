/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Knutwurst
 *
 * PatchDL is free software: you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free
 * Software Foundation, either version 3 of the License, or (at your option)
 * any later version. See the LICENSE file in the project root for details.
 */

#pragma once

/* Send an on-screen PS5 notification at startup showing the web UI URL
   (with the console's LAN IP when resolvable). */
void patchdl_notify_startup(unsigned short port);
