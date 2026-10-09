/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Knutwurst
 *
 * PatchDL is free software: you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free
 * Software Foundation, either version 3 of the License, or (at your option)
 * any later version. See the LICENSE file in the project root for details.
 */

#include "patchdl_proc.h"

#include <signal.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/syscall.h>
#include <sys/sysctl.h>

/* kinfo_proc field offsets for the FreeBSD 9-based PS5 kernel.
   Same layout used by ps5-payload-dev/ftpsrv (verified on fw 11.60):
   ki_pid at +72, ki_tdname (thread name) at +447, ki_structsize at +0. */
#define KINFO_OFF_STRUCTSIZE 0
#define KINFO_OFF_PID        72
#define KINFO_OFF_TDNAME     447

void
patchdl_proc_set_name(const char *name) {
    /* tid -1 = current thread */
    syscall(SYS_thr_set_name, -1, name);
}

static pid_t
find_pid(const char *name) {
    int      mib[4] = {1, 14, 8, 0}; /* CTL_KERN, KERN_PROC, KERN_PROC_ALL */
    pid_t    mypid = getpid();
    pid_t    pid = -1;
    size_t   buf_size;
    uint8_t *buf;

    if (sysctl(mib, 4, 0, &buf_size, 0, 0)) return -1;
    if (!(buf = malloc(buf_size)))          return -1;
    if (sysctl(mib, 4, buf, &buf_size, 0, 0)) { free(buf); return -1; }

    /* The loop guard guarantees the structsize/pid/tdname fields are inside the
       buffer before we read them, and the per-record check below keeps the name
       compare within the record (and thus the buffer). */
    for (uint8_t *ptr = buf; ptr + KINFO_OFF_TDNAME < buf + buf_size; ) {
        int    ki_structsize = *(int *)(ptr + KINFO_OFF_STRUCTSIZE);
        pid_t  ki_pid;
        char  *ki_tdname;
        size_t name_max;

        if (ki_structsize <= KINFO_OFF_TDNAME) break;   /* malformed/truncated */
        if (ptr + ki_structsize > buf + buf_size) break; /* record past buffer */

        ki_pid   = *(pid_t *)(ptr + KINFO_OFF_PID);
        ki_tdname = (char *)(ptr + KINFO_OFF_TDNAME);
        name_max  = (size_t)(ptr + ki_structsize - (uint8_t *)ki_tdname);

        if (!strncmp(name, ki_tdname, name_max) && ki_pid != mypid)
            pid = ki_pid;

        ptr += ki_structsize;
    }

    free(buf);
    return pid;
}

int
patchdl_proc_kill_others(const char *name) {
    int   killed = 0;
    pid_t pid;

    /* Bound the loop: at startup we expect 0-1 stale instance. A pathological
       proc table (or kill returning success but the process not exiting) would
       otherwise stall startup for sleep(1) × N. */
    while (killed < 8 && (pid = find_pid(name)) > 0) {
        if (kill(pid, SIGKILL))
            break;
        killed++;
        sleep(1); /* let the port + process slot free up */
    }
    return killed;
}
