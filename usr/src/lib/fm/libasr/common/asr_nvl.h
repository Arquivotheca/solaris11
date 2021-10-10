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
 * Copyright (c) 2011, Oracle and/or its affiliates. All rights reserved.
 */

#ifndef _ASR_NVL_H
#define	_ASR_NVL_H

#ifdef	__cplusplus
extern "C"
{
#endif

#include <sys/nvpair.h>
#include <stdio.h>
#include <time.h>

#include "asr_buf.h"

extern nvlist_t *asr_nvl_alloc();
extern int asr_nvl_cp_str(nvlist_t *, nvlist_t *, const char *);
extern int asr_nvl_cp_strd(nvlist_t *, nvlist_t *, const char *, char *);
extern nvlist_t *asr_nvl_dup(nvlist_t *);
extern char *asr_nvl_str(nvlist_t *, const char *);
extern char *asr_nvl_strd(nvlist_t *, const char *, char *);

extern int asr_nvl_merge(nvlist_t *, nvlist_t *);
extern void asr_nvl_free(nvlist_t *);

extern int asr_nvl_add_str(nvlist_t *, const char *, const char *);
extern int asr_nvl_add_strf(nvlist_t *, const char *, const char *, ...);
extern int asr_nvl_rm_str(nvlist_t *, const char *);

extern int asr_nvl_logf(FILE *, time_t *, nvlist_t *);

extern int asr_nvl_print_properties(FILE *, nvlist_t *);
extern int asr_nvl_read_properties(FILE *, nvlist_t *);

extern void asr_nvl_print_json(FILE *, nvlist_t *);
extern void asr_nvl_print_perl(FILE *, nvlist_t *);

extern void asr_nvl_tostringi(asr_buf_t *, nvlist_t *, int, char, char *);
extern asr_buf_t *asr_nvl_tostring(nvlist_t *);
extern asr_buf_t *asr_nvl_toperl(nvlist_t *);

#ifdef	__cplusplus
}
#endif

#endif	/* _ASR_NVL_H */
