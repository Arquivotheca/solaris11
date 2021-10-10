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
#ifndef _ASR_DTS_H
#define	_ASR_DTS_H

#ifdef	__cplusplus
extern "C" {
#endif

#include <sys/nvpair.h>

#include "asr.h"

#define	ASR_DTS_TRANSPORT	"DTS"
#define	ASR_DEFAULT_DTS_URL	"https://transport.oracle.com"

#define	DTS_HEADER_CASE_NUMBER		"X-Sun-case-number"
#define	DTS_HEADER_MESSAGE_LENGTH	"X-Sun-message-length"
#define	DTS_HEADER_USERNAME		"X-Sun-SOA-username"

#define	DTS_HEADER_MESSAGE_ID		"X-Sun-message-id"
#define	DTS_HEADER_MESSAGE_LENGTH	"X-Sun-message-length"
#define	DTS_HEADER_RESUME_FROM		"X-Sun-resume-from"
#define	DTS_HEADER_PRIORITY		"X-Sun-message-priority"
#define	DTS_HEADER_TIME			"X-Sun-DTS-time"
#define	DTS_HEADER_SIGNED		"Sun-DTS-signed"
#define	DTS_HEADER_AUTH			"Sun-SOA-auth"

#define	DTS_PUBLIC_KEY_PARAMETER	"X-Sun-pub-key"
#define	DTS_CLIENT_REGID_HEADER		"X-Sun-client-reg-id"
#define	DTS_STAG_URN_PARAMETER		"X-Sun-service-tag-urn"
#define	DTS_STAG_XML_PARAMETER		"X-Sun-service-tag-xml"
#define	DTS_STAG_AGENT_URN_PARAMETER	"X-Sun-service-tag-agent-urn"
#define	DTS_STAG_AGENT_XML_PARAMETER	"X-Sun-service-tag-agent-xml"
#define	DTS_PRODUCT			"X-Oracle-Product"
#define	DTS_EVENT_TYPE			"X-Oracle-Event-Type"
#define	DTS_TELEMETRY_SOURCE		"X-Oracle-Telemetry-Source"

#define	DTS_REGISTRY			"registry"

/* Configuration properties for DTS */
#define	ASR_DTS_PROP_VIRTUAL_PATH	ASR_PROP(config/dts_virtual_path)
#define	ASR_DTS_PROP_QUEUE		ASR_PROP(config/dts_queue)
#define	ASR_DTS_PROP_ACTIVATION_QUEUE	ASR_PROP(config/dts_activation_queue)
#define	ASR_DTS_PROP_AUDIT_QUEUE	ASR_PROP(config/dts_audit_queue)
#define	ASR_DTS_PROP_HEARTBEAT_QUEUE	ASR_PROP(config/dts_heartbeat_queue)
#define	ASR_DTS_PROP_EVENT_QUEUE	ASR_PROP(config/dts_event_queue)
#define	ASR_DTS_PROP_ADMIN_QUEUE	ASR_PROP(config/dts_admin_queue)

/* Default values for above configuration properties */
#define	ASR_DTS_VIRTUAL_PATH		"/v1"
#define	ASR_DTS_ACTIVATION_QUEUE	"asr-activation"
#define	ASR_DTS_AUDIT_QUEUE		"product"
#define	ASR_DTS_HEARTBEAT_QUEUE		"asr-heartbeat"
#define	ASR_DTS_EVENT_QUEUE		"asr-messages"
#define	ASR_DTS_ADMIN_QUEUE		"asr-admin"
#define	ASR_DTS_QUEUE			"product"

#define	ASR_DTS_APP_NAME		"asr-notify 1.0.0"

/* Maximum number of bytes in message that are signed. */
#define	ASR_DTS_MAX_SIGN	100000
#define	ASR_DTS_MAX_MSG_SIZE	(10*1024*1024) /* 10 MB Max message size */

#define	ASR_DTS_RSP_OK		200	/* OK */
#define	ASR_DTS_RSP_CREATED	201	/* Created new message on queue */
#define	ASR_DTS_RSP_ACCEPTED	202	/* Message accepted */
#define	ASR_DTS_RSP_NO_CONTENT	204	/* No msgs in queue */

/* No reties when 4xx errors are received */
#define	ASR_DTS_RSP_BAD_REQUEST	400	/* Returns desc of error */
#define	ASR_DTS_RSP_UNAUTHORIZED	401 /* Msg not signed properly */
#define	ASR_DTS_RSP_NOT_FOUND	404	/* bad queue name */
#define	ASR_DTS_RSP_CONFLICT	409	/* rcved data size != content-length */
#define	ASR_DTS_RSP_LENGTH_REQUIRED	411 /* missing content-length header */

/* content-length exceeds limit for this queue */
#define	ASR_DTS_RSP_REQUEST_ENTITY_TOO_LARGE	413

/*
 * Returns error desc or exception from server side.
 * Connection shouldn't be reused.
 */
#define	ASR_DTS_RSP_INTERNAL_SERVER_ERROR	500

/* server is under maintenance or overloaded */
#define	ASR_DTS_RSP_SERVICE_UNAVAILABLE		503

/* Standard transport methods */
extern int asr_dts_register_client(
    asr_handle_t *ah, const asr_regreq_t *req, nvlist_t *rsp);
extern int asr_dts_unregister_client(asr_handle_t *ah);
extern int asr_dts_send_msg(
    asr_handle_t *ah, const asr_message_t *msg, nvlist_t *rsp);

/* Additional DTS functions */
extern int asr_dts_init(asr_handle_t *ah);
extern long asr_dts_authenticate(asr_handle_t *ah, char **name, char **id);
extern long asr_dts_pop(asr_handle_t *ah, char *name, char *filter,
    char **out_buf, nvlist_t **out_hdr);
extern long asr_dts_read(asr_handle_t *ah, char *name, char *id,
    char **out_buf, nvlist_t **out_hdr);
extern long asr_dts_poll(asr_handle_t *ah, char *queue, char *filter);
extern long asr_dts_add_msg(asr_handle_t *ah, nvlist_t *hdrs,
    char *name, char *data, size_t len, nvlist_t **out_hdr);
extern long asr_dts_create_msg(asr_handle_t *ah, nvlist_t *hdrs,
    char *name, size_t len, nvlist_t **out_hdr);
extern long asr_dts_delete_msg(asr_handle_t *ah, char *name, char *id);
extern long asr_dts_add_payload(asr_handle_t *ah, char *name, char *id,
    int resume_from, char *data, size_t len, nvlist_t **out_hdr);
extern const char *asr_dts_resp_msg(long resp);

#ifdef	__cplusplus
}
#endif

#endif	/* _ASR_DTS_H */
