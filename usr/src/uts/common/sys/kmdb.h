/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */
/*
 * Copyright (c) 2004, Oracle and/or its affiliates. All rights reserved.
 */

#ifndef _KMDB_H
#define	_KMDB_H

#include <sys/modctl.h>

#ifdef __cplusplus
extern "C" {
#endif

#define	KMDB_IOC		(0xDB << 16)
#define	KMDB_IOC_START		(KMDB_IOC|1)	/* mdb: load debugger */
#define	KMDB_IOC_STOP		(KMDB_IOC|2)	/* mdb: unload debugger */

#define	KMDB_F_AUTO_ENTRY	0x1	/* auto enter debugger after mdb -K */
#define	KMDB_F_TRAP_NOSWITCH	0x2	/* don't use debugger's IDT/TBA */
#define	KMDB_F_DRV_DEBUG	0x4	/* enable driver's debug messages */

struct bootops;

extern int kctl_attach(dev_info_t *);
extern int kctl_detach(void);
extern int kctl_get_state(void);
extern int kctl_modload_activate(size_t, const char *, uint_t);
extern int kctl_deactivate(void);

typedef int kctl_boot_activate_f(struct bootops *, void *, size_t,
    const char **);

#ifdef __cplusplus
}
#endif

#endif /* _KMDB_H */
