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
 * Copyright (c) 2008, 2010, Oracle and/or its affiliates. All rights reserved.
 */

#ifndef	_FMD_AGENT_H
#define	_FMD_AGENT_H

#include <inttypes.h>
#include <libnvpair.h>
#include <umem.h>
#include <sys/types.h>
#include <sys/processor.h>


#ifdef	__cplusplus
extern "C" {
#endif

/*
 * libfmd_agent Interfaces
 *
 * Note: The contents of this file are private to the implementation of the
 * Solaris system and FMD subsystem and are subject to change at any time
 * without notice.  Applications and drivers using these interfaces will fail
 * to run on future releases.  These interfaces should not be used for any
 * purpose until they are publicly documented for use outside of Sun.
 */

#define	FMD_AGENT_VERSION		1

#define	FMD_AGENT_RETIRE_DONE		0	/* synchronous success */
#define	FMD_AGENT_RETIRE_ASYNC		1	/* asynchronous complete */
#define	FMD_AGENT_RETIRE_FAIL		2	/* failure */

typedef struct fmd_agent_hdl fmd_agent_hdl_t;

extern fmd_agent_hdl_t *fmd_agent_open(int);
extern void fmd_agent_close(fmd_agent_hdl_t *);
extern int fmd_agent_errno(fmd_agent_hdl_t *);
extern const char *fmd_agent_errmsg(fmd_agent_hdl_t *);
extern const char *fmd_agent_strerr(int);

extern int fmd_agent_page_retire(fmd_agent_hdl_t *, nvlist_t *);
extern int fmd_agent_page_unretire(fmd_agent_hdl_t *, nvlist_t *);
extern int fmd_agent_page_isretired(fmd_agent_hdl_t *, nvlist_t *);

#ifdef __x86
extern int fmd_agent_physcpu_info(fmd_agent_hdl_t *, nvlist_t ***cpusp,
    uint_t *ncpu);
extern int fmd_agent_cpu_retire(fmd_agent_hdl_t *, int, int, int);
extern int fmd_agent_cpu_unretire(fmd_agent_hdl_t *, int, int, int);
extern int fmd_agent_cpu_isretired(fmd_agent_hdl_t *, int, int, int);
#endif /* __x86 */

#ifdef __sparc
extern int fmd_agent_cache_retire(fmd_agent_hdl_t *, uint32_t, nvlist_t *);
extern int fmd_agent_cache_unretire(fmd_agent_hdl_t *, uint32_t,
    nvlist_t *);
extern int fmd_agent_cache_isretired(fmd_agent_hdl_t *, uint32_t,
    nvlist_t *);
#endif /* __sparc */

#ifdef	__cplusplus
}
#endif

#endif	/* _FMD_AGENT_H */
