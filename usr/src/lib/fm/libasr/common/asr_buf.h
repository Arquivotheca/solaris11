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

#ifndef _ASR_BUF_H
#define	_ASR_BUF_H

#ifdef	__cplusplus
extern "C"
{
#endif

#include <stdio.h>
#include <sys/types.h>
#include <sys/nvpair.h>
#include <sys/varargs.h>

#define	ASRB_GROW_LEN 32

typedef struct asr_buf {
	char *asrb_data;
	size_t asrb_length;
	size_t asrb_allocated;
	int asrb_error;
} asr_buf_t;

extern asr_buf_t *asr_buf_alloc(size_t size);
extern void asr_buf_free(asr_buf_t *abp);
extern char *asr_buf_free_struct(asr_buf_t *abp);
extern void asr_buf_reset(asr_buf_t *abp);

extern char *asr_buf_data(asr_buf_t *abp);
extern size_t asr_buf_size(asr_buf_t *abp);

/*
 * Text formatting functions
 */
extern int asr_buf_append(asr_buf_t *abp, const char *format, ...);
extern int asr_buf_vappend(asr_buf_t *abp, const char *format, va_list va);
extern int asr_buf_append_buf(asr_buf_t *abp, const asr_buf_t *buf);
extern int asr_buf_append_str(asr_buf_t *abp, const char *value);
extern int asr_buf_append_raw(asr_buf_t *abp, const void *data, size_t size);
extern int asr_buf_append_char(asr_buf_t *abp, const char value);
extern void asr_buf_trim(asr_buf_t *abp);
extern int asr_buf_terminate(asr_buf_t *abp);

/*
 * XML formating functions
 */
extern int asr_buf_append_xml_elem(
    asr_buf_t *abp, unsigned int pad, const char *name);
extern int asr_buf_append_xml_end(
    asr_buf_t *abp, unsigned int pad, const char *name);
extern int asr_buf_append_xml_val(asr_buf_t *abp, const char *value);
extern int asr_buf_append_xml_token(asr_buf_t *abp, const char *token);
extern int asr_buf_append_xml_nv(asr_buf_t *abp, unsigned int pad,
    const char *name, const char *value);
extern int asr_buf_append_xml_nnv(asr_buf_t *abp, unsigned int pad,
    const char *name, const char *value, int n);
extern int asr_buf_append_xml_nb(asr_buf_t *abp, unsigned int pad,
    const char *name, const boolean_t value);
extern int asr_buf_append_xml_nvtoken(asr_buf_t *abp, unsigned int pad,
    const char *name, const char *value);
extern int asr_buf_append_xml_anv(
    asr_buf_t *abp, unsigned int pad, const char *aname, const char *avalue,
    const char *name, const char *value);
extern int asr_buf_append_xml_ai(
    asr_buf_t *bp, int pad, const char *name, const char *value);
extern int asr_buf_append_pad(asr_buf_t *abp, unsigned int pad);

extern int asr_buf_readln(asr_buf_t *abp, FILE *file);

#ifdef	__cplusplus
}
#endif

#endif	/* _ASR_BUF_H */
