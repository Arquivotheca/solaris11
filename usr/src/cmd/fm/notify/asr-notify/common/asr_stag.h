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

#ifndef _ASR_STAG_H
#define	_ASR_STAG_H

#ifdef	__cplusplus
extern "C"
{
#endif

#include "asr_buf.h"

#define	ASR_STAG_URL_PROP		"config/scrk_reg_url"
#define	ASR_STAG_BETA_URL_PROP		"config/scrk_beta_reg_url"

#define	ASR_STAG_DEFAULT_ORCL_URL	"https://inv-cs.oracle.com"
#define	ASR_STAG_DEFAULT_SUN_URL	"https://inventory.sun.com"
#define	ASR_STAG_DEFAULT_URL		ASR_STAG_DEFAULT_ORCL_URL

#define	ASR_STAG_BETA1_URL		"https://inv-cs-bt.us.oracle.com"
#define	ASR_STAG_BETA2_URL		"https://inv-cs-uat.us.oracle.com"
#define	ASR_STAG_BETA3_URL		"https://inv-cs-stage.us.oracle.com"
#define	ASR_STAG_DEFAULT_BETA_URL	ASR_STAG_BETA1_URL

#define	ASR_STAG_REG_PATH		"/SCRK/ClientRegistrationV1_1_0"
#define	ASR_STAG_HTTP_STATUS_OK	200

/* Default Service Tag Values */
#define	ASR_STAG_PRODUCT_NAME	"Oracle Solaris 11"
#define	ASR_STAG_PRODUCT_ID	"Solaris_11"
#define	ASR_STAG_PRODUCT_VERSION	"11"
#define	ASR_STAG_PRODUCT_VENDOR	"Oracle"
#define	ASR_STAG_PRODUCT_URN	"urn:uuid:6df19e63-7ef5-11db-a4bd-080020a9ed93"

/* Service Tag creation */
extern int asr_stag_agent_urn(char *serial, asr_buf_t *urn);
extern char *asr_stag_inst_urn(char *id);
extern int asr_stag_create_agent(char *serial, char *agent_urn, asr_buf_t *out);
extern int asr_stag_create(
    char *inst_urn, char *agent_urn, char *product_urn,
    char *userid, char *domain_id, char *domain_name, asr_buf_t *out);

extern int asr_stag_parse_domains(asr_buf_t *xml, nvlist_t *domains);

/* Inventory Web service */
extern int asr_stag_get_domains(
    asr_handle_t *ah, nvlist_t *reg, nvlist_t *domains);
extern int asr_stag_register(
    asr_handle_t *ah, nvlist_t *reg, char *type, char *urn, asr_buf_t *xml);
extern int asr_stag_check_reg(
    asr_handle_t *ah, nvlist_t *reg, char *type, char *urn);


#define	ASR_STAG_REG_TYPE_AGENT		"agent"
#define	ASR_STAG_REG_TYPE_SVCTAG	"svctag"

#ifdef	__cplusplus
}
#endif

#endif	/* _ASR_STAG_H */
