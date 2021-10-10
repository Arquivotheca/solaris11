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

#ifndef	_FMD_PROTOCOL_H
#define	_FMD_PROTOCOL_H

#include <sys/fm/protocol.h>
#include <libnvpair.h>
#include <stdarg.h>

#ifdef	__cplusplus
extern "C" {
#endif

#define	FMD_RSRC_CLASS		FM_RSRC_CLASS ".fm."
#define	FMD_CTL_CLASS		FMD_RSRC_CLASS "fmd."
#define	SYSEVENT_RSRC_CLASS	FM_RSRC_CLASS ".sysevent."

#define	FMD_RSRC_CLASS_LEN	(sizeof (FMD_RSRC_CLASS) - 1)
#define	FMD_CTL_CLASS_LEN	(sizeof (FMD_CTL_CLASS) - 1)
#define	SYSEVENT_RSRC_CLASS_LEN	(sizeof (SYSEVENT_RSRC_CLASS) - 1)

#define	FMD_CTL_ADDHRT		FMD_CTL_CLASS "clock.addhrtime"
#define	FMD_CTL_ADDHRT_VERS1	1
#define	FMD_CTL_ADDHRT_DELTA	"delta"

/*
 * The FMD_FLT_* events still use defect.sunos.* for now: a future registry
 * putback should define all fmd events and convert these to defect.fm.fmd.*
 */
#define	FMD_FLT_NOSUB		"defect.sunos.fmd.nosub"
#define	FMD_FLT_NODC		"defect.sunos.fmd.nodiagcode"
#define	FMD_FLT_MOD		"defect.sunos.fmd.module"
#define	FMD_FLT_CONF		"defect.sunos.fmd.config"

#define	FMD_ERR_CLASS		"ereport.fm.fmd."
#define	FMD_ERR_CLASS_LEN	(sizeof (FMD_ERR_CLASS) - 1)

#define	FMD_ERR_MOD_MSG		"msg"
#define	FMD_ERR_MOD_ERRNO	"errno"
#define	FMD_ERR_MOD_ERRCLASS	"errclass"

struct fmd_module;			/* see <fmd_module.h> */

extern nvlist_t *fmd_protocol_authority(void);
extern nvlist_t *fmd_protocol_fmri_module(struct fmd_module *);
extern nvlist_t *fmd_protocol_fault(const char *,
    uint8_t, nvlist_t *, nvlist_t *, nvlist_t *, const char *);
extern nvlist_t *fmd_protocol_list(const char *, nvlist_t *,
    const char *, const char *, uint_t, nvlist_t **, uint8_t *, int,
    struct timeval *, int, uint32_t, boolean_t, const char *);
extern nvlist_t *fmd_protocol_rsrc_asru(const char *,
    const char *, const char *, boolean_t, boolean_t, boolean_t, nvlist_t *,
    struct timeval *m, boolean_t, boolean_t, boolean_t, boolean_t, nvlist_t *,
    boolean_t, const char *);
extern nvlist_t *fmd_protocol_fmderror(int, const char *, va_list);
extern nvlist_t *fmd_protocol_moderror(struct fmd_module *, int, const char *);
extern nvlist_t *fmd_protocol_xprt_ctl(struct fmd_module *,
    const char *, uint8_t);
extern nvlist_t *fmd_protocol_xprt_sub(struct fmd_module *,
    const char *, uint8_t, const char *);
extern nvlist_t *fmd_protocol_xprt_uuclose(struct fmd_module *,
    const char *, uint8_t, const char *);
extern nvlist_t *fmd_protocol_xprt_uuresolved(struct fmd_module *,
    const char *, uint8_t, const char *);
extern nvlist_t *fmd_protocol_xprt_updated(struct fmd_module *,
    const char *, uint8_t, const char *, uint8_t *, uint8_t *, uint_t);

#ifdef	__cplusplus
}
#endif

#endif	/* _FMD_PROTOCOL_H */
