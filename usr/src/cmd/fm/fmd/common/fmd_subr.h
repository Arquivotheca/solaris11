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
 * Copyright (c) 2004, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#ifndef	_FMD_SUBR_H
#define	_FMD_SUBR_H

#include <pthread.h>
#include <synch.h>
#include <stdarg.h>

#ifdef	__cplusplus
extern "C" {
#endif

#ifdef DEBUG
extern int fmd_assert(const char *, const char *, int);
#define	ASSERT(x)	((void)((x) || fmd_assert(#x, __FILE__, __LINE__)))
#else
#define	ASSERT(x)
#endif

extern void fmd_vpanic(const char *, va_list);
extern void fmd_panic(const char *, ...);

extern void fmd_verror(int, const char *, va_list);
extern void fmd_error(int, const char *, ...);

#define	FMD_DBG_HELP	0x00001	/* display list of debugging modes and exit */
#define	FMD_DBG_ERR	0x00002	/* enable error handling debug messages */
#define	FMD_DBG_MOD	0x00004	/* enable module subsystem debug messages */
#define	FMD_DBG_DISP	0x00008	/* enable dispq subsystem debug messages */
#define	FMD_DBG_XPRT	0x00010	/* enable transport subsystem debug messages */
#define	FMD_DBG_EVT	0x00020	/* enable event subsystem debug messages */
#define	FMD_DBG_LOG	0x00040	/* enable log subsystem debug messages */
#define	FMD_DBG_TMR	0x00080	/* enable timer subsystem debug messages */
#define	FMD_DBG_FMRI	0x00100	/* enable fmri subsystem debug messages */
#define	FMD_DBG_ASRU	0x00200	/* enable asru subsystem debug messages */
#define	FMD_DBG_CASE	0x00400	/* enable case subsystem debug messages */
#define	FMD_DBG_CKPT	0x00800	/* enable checkpoint debug messages */
#define	FMD_DBG_RPC	0x01000	/* enable rpc service debug messages */
#define	FMD_DBG_TRACE	0x02000	/* display matching TRACE() calls */
#define	FMD_DBG_STARTUP	0x04000	/* enable fmd startup messages */
#define	FMD_DBG_TOPO	0x08000	/* enable topo debug messages */
#define	FMD_DBG_TSTAMP	0x10000	/* enable timestamps in debug messages */
#define	FMD_DBG_ALL	0x1dffe	/* enable all modes except for HELP, TRACE */

/*
 * These debug settings are used to control and debug topo snapshot features.
 * They are enabled with the the "debug.topo" configuration option.
 */
#define	FMD_DBG_TOPO_NO_SNAP_LOAD	0x00000001	/* see decsr in fmd.c */
#define	FMD_DBG_TOPO_NO_SNAP_SAVE	0x00000002
#define	FMD_DBG_TOPO_NO_SNAP_USE	0x00000004
#define	FMD_DBG_TOPO_NO_SNAP_SWITCH	0x00000008
#define	FMD_DBG_TOPO_NO_STARTUP_DELAY	0x00000010
#define	FMD_DBG_TOPO_NO_UPDATE_DELAY	0x00000020
#define	FMD_DBG_TOPO_NO_DUMP_CLEAR	0x00000040
#define	FMD_DBG_TOPO_UPDATE_LOAD	0x00000080

#define	FMD_DEBUG_TOPO_NO_SNAP_LOAD \
	(fmd.d_fmd_debug_topo & FMD_DBG_TOPO_NO_SNAP_LOAD)
#define	FMD_DEBUG_TOPO_NO_SNAP_SAVE \
	(fmd.d_fmd_debug_topo & FMD_DBG_TOPO_NO_SNAP_SAVE)
#define	FMD_DEBUG_TOPO_NO_SNAP_USE \
	(fmd.d_fmd_debug_topo & FMD_DBG_TOPO_NO_SNAP_USE)
#define	FMD_DEBUG_TOPO_NO_SNAP_SWITCH \
	(fmd.d_fmd_debug_topo & FMD_DBG_TOPO_NO_SNAP_SWITCH)
#define	FMD_DEBUG_TOPO_NO_STARTUP_DELAY \
	(fmd.d_fmd_debug_topo & FMD_DBG_TOPO_NO_STARTUP_DELAY)
#define	FMD_DEBUG_TOPO_NO_UPDATE_DELAY \
	(fmd.d_fmd_debug_topo & FMD_DBG_TOPO_NO_UPDATE_DELAY)
#define	FMD_DEBUG_TOPO_UPDATE_LOAD \
	(fmd.d_fmd_debug_topo & FMD_DBG_TOPO_UPDATE_LOAD)
#define	FMD_DEBUG_TOPO_NO_DUMP_CLEAR \
	(fmd.d_fmd_debug_topo & FMD_DBG_TOPO_NO_DUMP_CLEAR)

extern void fmd_vdprintf(int, const char *, va_list);
extern void fmd_dprintf(int, const char *, ...);

extern void fmd_trace_cpp(void *, const char *, int);
extern void *fmd_trace(uint_t, const char *, ...);

#ifdef DEBUG
#define	TRACE(args)	{ fmd_trace_cpp(fmd_trace args, __FILE__, __LINE__); }
#else
#define	TRACE(args)
#endif

extern const char *fmd_ea_strerror(int);
extern uint64_t fmd_ena(void);
extern uint32_t fmd_ntz32(uint32_t);

#ifdef	__cplusplus
}
#endif

#endif	/* _FMD_SUBR_H */
