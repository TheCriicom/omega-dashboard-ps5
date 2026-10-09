/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Knutwurst
 *
 * PatchDL is free software: you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free
 * Software Foundation, either version 3 of the License, or (at your option)
 * any later version. See the LICENSE file in the project root for details.
 */

#include "patchdl_notify.h"
#include "patchdl_version.h"

#include <arpa/inet.h>
#include <ifaddrs.h>
#include <netinet/in.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>

/* Standard PS5/PS4 on-screen notification (same layout ftpsrv uses). */
typedef struct {
    char useless1[45];
    char message[3075];
} notify_request_t;

extern int sceKernelSendNotificationRequest(int, notify_request_t *,
                                            unsigned long, int);

static void
notify(const char *fmt, ...) {
    notify_request_t req;
    va_list          ap;

    memset(&req, 0, sizeof(req));
    va_start(ap, fmt);
    vsnprintf(req.message, sizeof(req.message), fmt, ap);
    va_end(ap);

    sceKernelSendNotificationRequest(0, &req, sizeof(req), 0);
    puts(req.message);
}

/* First non-loopback IPv4 address of the console. */
static void
local_ip(char *out, size_t n) {
    struct ifaddrs *ifa = NULL, *p;

    out[0] = '\0';
    if (getifaddrs(&ifa))
        return;

    for (p = ifa; p; p = p->ifa_next) {
        char ip[INET_ADDRSTRLEN];
        struct sockaddr_in *s;

        if (!p->ifa_addr || p->ifa_addr->sa_family != AF_INET)
            continue;
        s = (struct sockaddr_in *)p->ifa_addr;
        if (!inet_ntop(AF_INET, &s->sin_addr, ip, sizeof(ip)))
            continue;
        if (strcmp(ip, "127.0.0.1") && strncmp(ip, "0.", 2)) {
            strncpy(out, ip, n - 1);
            out[n - 1] = '\0';
            break;
        }
    }
    freeifaddrs(ifa);
}

void
patchdl_notify_startup(unsigned short port) {
    char ip[INET_ADDRSTRLEN] = {0};

    local_ip(ip, sizeof(ip));
    if (ip[0])
        notify("PatchDL %s by Knutwurst\nhttp://%s:%u/", PATCHDL_VERSION, ip, port);
    else
        notify("PatchDL %s by Knutwurst\nWeb UI on port %u", PATCHDL_VERSION, port);
}
