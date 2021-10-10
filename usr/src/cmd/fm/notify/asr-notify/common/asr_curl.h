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

#ifndef _ASR_CURL_H
#define	_ASR_CURL_H

#ifdef	__cplusplus
extern "C" {
#endif

#include <curl/curl.h>
#include <fm/libasr.h>
#include "asr_buf.h"

typedef struct asr_curl_req {
	asr_handle_t		*ac_asrh;
	const char		*ac_url;
	const asr_regreq_t	*ac_regreq;
} asr_curl_req_t;

extern void asr_curl_init_request(asr_curl_req_t *creq,
    asr_handle_t *ah, const char *url, const asr_regreq_t *regreq);

extern int asr_curl_hdr_add(struct curl_slist **in_out_hdrs, char *header);
extern int asr_curl_hdr_append(struct curl_slist **hdrs, char *fmt, ...);
extern struct curl_slist *asr_curl_headers(nvlist_t *hdr);

extern int asr_curl_get(
    asr_curl_req_t *creq, struct curl_slist *hdrs,
    asr_buf_t *out_buf, long *out_resp, nvlist_t *out_hdr);
extern int asr_curl_request(asr_curl_req_t *creq,
    const char *request, struct curl_slist *hdrs,
    asr_buf_t *out_buf, long *out_resp, nvlist_t *out_hdr);
extern int asr_curl_post_data(asr_curl_req_t *creq,
    struct curl_slist *hdrs, char *data, size_t len,
    asr_buf_t *out_buf, long *out_resp, nvlist_t *out_hdr);
extern int asr_curl_post_form(asr_curl_req_t *creq,
    nvlist_t *hdr, nvlist_t *post,
    asr_buf_t *out_bp, long *out_resp, nvlist_t *out_hdr);
extern int asr_curl_post_multi(asr_curl_req_t *creq,
    nvlist_t *hdr, nvlist_t *post,
    asr_buf_t *out_bp, long *out_resp, nvlist_t *out_hdr);
extern int asr_curl_put_data(asr_curl_req_t *creq,
    struct curl_slist *hdrs, char *data, size_t len,
    asr_buf_t *out_buf, long *out_resp, nvlist_t *out_hdr);

#ifdef	__cplusplus
}
#endif

#endif	/* _ASR_CURL_H */
