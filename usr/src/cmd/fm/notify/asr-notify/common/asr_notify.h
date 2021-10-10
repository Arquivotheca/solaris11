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

#ifndef _ASR_NOTIFY_H
#define	_ASR_NOTIFY_H

#ifdef	__cplusplus
extern "C" {
#endif

#include <fm/libasr.h>
#include <sys/nvpair.h>

#include "asr_dts.h"
#include "asr_scrk.h"

/* Configuration properties */
#define	PH_DEFAULT_INTERVAL	300L
#define	PH_DEFAULT_MAX_MSGS	50L
#define	PH_PROP_MAX_MSGS	"config/ph_max_msgs"
#define	PH_SVC_NAME		"system/fm/asr-notify"
#define	PH_FMRI			"svc:/system/fm/asr-notify:default"
#define	PH_DEFAULT_ROOT		"/"
#define	PH_DEFAULT_DATADIR	"var/fm/asr/"
#define	PH_MSGDIR		"msgs/"
#define	PH_AUDIT_MSG		"audit.xml"
#define	PH_HEARTBEAT_MSG	"heartbeat.xml"
#define	PH_FAULT_MSG		"fault.xml"
#define	PH_DEFAULT_TRANSPORT	"DTS"
#define	PH_UID			60002

/* Phone home result codes */
#define	PH_FAILURE	-1
#define	PH_OK		0

/* Functions */
extern int ph_main(int argc, char ** argv);
extern int ph_tprt_init(asr_handle_t *asrh);
extern int ph_save_reg(asr_handle_t *asrh);
extern int ph_read_reg(asr_handle_t *asrh);

#ifdef	__cplusplus
}
#endif

#endif	/* _ASR_NOTIFY_H */
