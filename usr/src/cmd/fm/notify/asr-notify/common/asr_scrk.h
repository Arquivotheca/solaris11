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

#ifndef _ASR_SCRK_H
#define	_ASR_SCRK_H

#ifdef	__cplusplus
extern "C"
{
#endif

#include <sys/nvpair.h>
#include "asr.h"

#define	ASR_SCRK_VERSION		"1.1.1"
#define	ASR_SCRK_OFFERING_CLASS		"STG_MESSAGE_2_0"
#define	ASR_SCRK_DESCRIPTION		"Storage Group Message Contents"

#define	ASR_SCRK_MSG_VERSION		"VERSION"
#define	ASR_SCRK_MSG_OFFERING_CLASS	"OFFERING_CLASS"
#define	ASR_SCRK_MSG_DESCRIPTION	"TELEMETRY_DESCRIPTION"
#define	ASR_SCRK_MSG_PRODUCT		"PRODUCT_ID"
#define	ASR_SCRK_MSG_CLIENT_REG		"SC_CLIENT_REG_ID"
#define	ASR_SCRK_MSG_SIGNATURE		"MSG_SIGNATURE"
#define	ASR_SCRK_MSG_DATA		"TELEMETRY_DATA"

#define	ASR_SCRK_MSG_SOA_ID		"SOA_ID"
#define	ASR_SCRK_MSG_SOA_PW		"SOA_PW"
#define	ASR_SCRK_MSG_ASSET_ID		"ASSET_ID"
#define	ASR_SCRK_MSG_PUBLIC_KEY		"PUBLIC_KEY"

#define	ASR_SCRK_HTTP_STATUS_OK		200
#define	ASR_SCRK_CRS_OK			"0"
#define	ASR_SCRK_CRS_ERRCODE_BADAUTH	"4"

/* SCRK Properties */
#define	ASR_PROP_SCRK_URL	ASR_PROP_DEST_URL
#define	ASR_PROP_SCRK_BETA_URL	ASR_PROP_BETA_URL

#define	ASR_SCRK_REG_PATH	"/SCRK/ClientRegistrationV1_1_0"
#define	ASR_SCRK_MSG_PATH	"/ServiceInformation/ServiceInformation"

#define	ASR_SCRK_ORCL_URL	"https://asr-services.oracle.com"
#define	ASR_SCRK_SUN_URL	"https://cns-services.sun.com"
#define	ASR_SCRK_URL		ASR_SCRK_ORCL_URL

#define	ASR_SCRK_BETA1_URL	"https://cns-services-bt.us.oracle.com"
#define	ASR_SCRK_BETA2_URL	"https://cns-services-uat.us.oracle.com"
#define	ASR_SCRK_BETA3_URL	"https://cns-services-stage.us.oracle.com"
#define	ASR_SCRK_BETA_URL	ASR_SCRK_BETA1_URL

extern int asr_scrk_init(asr_handle_t *ah);
extern int asr_scrk_register_client(
    asr_handle_t *, const asr_regreq_t *, nvlist_t *);
extern int asr_scrk_unregister_client(asr_handle_t *);
extern int asr_scrk_send_msg(
    asr_handle_t *, const asr_message_t *, nvlist_t *);

#ifdef	__cplusplus
}
#endif

#endif	/* _ASR_SCRK_H */
