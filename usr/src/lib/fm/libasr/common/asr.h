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

#ifndef _ASR_H
#define	_ASR_H

#ifdef	__cplusplus
extern "C"
{
#endif

#include <stdio.h>
#include <fm/libtopo.h>

#include "libasr.h"
#include "asr_buf.h"
#include "asr_err.h"

#define	ASR_DEFAULT_KEYLEN		1024
#define	ASR_ANONYMOUS_USER		"oracleanonreg@oracleanon.com"
#define	ASR_DEFAULT_PRODUCT_NAME	"Oracle Solaris Operating System"
#define	ASR_DEFAULT_HTTP_TIMEOUT	30

#define	ASR_ERROR(id) asr_error((id), "%s %d", __FILE__, __LINE__)

/* Functions that define capabilities of an ASR message transport */
typedef struct asr_transport {
	char *asr_tprt_name;
	int (*asr_register_client)(
		asr_handle_t *ah, const asr_regreq_t *req, nvlist_t *rsp);
	int (*asr_unregister_client)(asr_handle_t *ah);
	int (*asr_send_msg)(
		asr_handle_t *ah, const asr_message_t *msg, nvlist_t *rsp);
} asr_transport_t;

struct asr_handle {
	nvlist_t	*asr_cfg;	/* a list of ASR properties */
	char		*asr_cfg_name;  /* Cfg svc or file name */
	char		*asr_host_id;   /* Hostname to use in messages */
	FILE		*asr_log;	/* phone home service log */
	boolean_t	asr_debug;	/* Debugging flag */
	asr_transport_t *asr_tprt;	/* ASR Message Transport */
};

/* Client Registration Request structure */
struct asr_regreq {
	char *asr_user;			/* user registration name */
	char *asr_password;		/* user registration password */
};

/* ASR configuration data access functions */
extern char *asr_get_datadir(asr_handle_t *ah);

/* ASR data structure init functions */
extern int asr_reg_fill(
    asr_handle_t *ah, const asr_regreq_t *regreq, nvlist_t *rsp);

/* Common ASR Message functions */
extern int asr_msg_start(asr_handle_t *ah, asr_buf_t *buf);
extern int asr_msg_tstart(
    asr_handle_t *ah, asr_buf_t *buf, char *timebuf, size_t tlen);
extern int asr_msg_end(asr_buf_t *msg);
extern asr_message_t *asr_message_alloc(asr_buf_t *buf, asr_msgtype_t type);

/* FMA topology functions */
typedef struct
{
	asr_handle_t *asr_hdl;	/* ASR handle */
	topo_hdl_t *asr_topoh;	/* Topo handle */
	void *asr_data;		/* Callback data */
} asr_topo_enum_data_t;

extern int asr_topo_walk(asr_handle_t *ah, topo_walk_cb_t walker, void *);
extern char *asr_topo_fmri2str(topo_hdl_t *ah, nvlist_t *fmri);
extern char *asr_fmri_str_to_name(char *fmri);

extern void asr_log_err(asr_handle_t *ah);
extern void asr_log_errno(asr_handle_t *ah, asr_err_t err);

extern boolean_t asr_use_schema_2_1(asr_handle_t *ah);

#ifdef	__cplusplus
}
#endif

#endif	/* _ASR_H */
