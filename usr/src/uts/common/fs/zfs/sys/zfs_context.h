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
 * Copyright (c) 2005, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#ifndef _SYS_ZFS_CONTEXT_H
#define	_SYS_ZFS_CONTEXT_H

#ifdef	__cplusplus
extern "C" {
#endif

#include <sys/note.h>
#include <sys/types.h>
#include <sys/t_lock.h>
#include <sys/atomic.h>
#include <sys/sysmacros.h>
#include <sys/bitmap.h>
#include <sys/cmn_err.h>
#include <sys/kmem.h>
#include <sys/taskq.h>
#include <sys/buf.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/cpuvar.h>
#include <sys/kobj.h>
#include <sys/conf.h>
#include <sys/disp.h>
#include <sys/debug.h>
#include <sys/random.h>
#include <sys/byteorder.h>
#include <sys/systm.h>
#include <sys/list.h>
#include <sys/uio.h>
#include <sys/dirent.h>
#include <sys/time.h>
#include <vm/seg_kmem.h>
#include <sys/zone.h>
#include <sys/uio.h>
#include <sys/zfs_debug.h>
#include <sys/sysevent.h>
#include <sys/sysevent/eventdefs.h>
#include <sys/sysevent/dev.h>
#include <sys/fm/util.h>
#include <sys/sunddi.h>

#define	CPU_SEQID	(CPU->cpu_seqid)

#define	RW_REENTRANT	RW_DEFAULT

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_ZFS_CONTEXT_H */
