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
 * Copyright (c) 2010, Oracle and/or its affiliates. All rights reserved.
 */

#ifndef	_PROBLEM_H
#define	_PROBLEM_H

#ifdef	__cplusplus
extern "C" {
#endif

#include <sys/types.h>
#include <libuutil.h>
#include <libnvpair.h>

typedef struct sunFmProblem_data {
	int		d_valid;
	uu_avl_node_t	d_uuid_avl;
	const char	*d_aci_uuid;
	const char	*d_aci_code;
	const char	*d_aci_url;
	const char	*d_diag_engine;
	struct timeval	d_diag_time;
	ulong_t		d_nsuspects;
	nvlist_t	**d_suspects;
	nvlist_t	*d_aci_event;
	uint8_t		*d_statuses;
	const char	*d_severity;
	boolean_t	d_injected;
} sunFmProblem_data_t;

typedef struct sunFmProblem_update_ctx {
	const char	*uc_host;
	uint32_t	uc_prog;
	int		uc_version;
	const char	*uc_index;
	uint32_t	uc_type;
} sunFmProblem_update_ctx_t;

typedef nvlist_t sunFmFaultEvent_data_t;
typedef uint8_t sunFmFaultStatus_data_t;

int sunFmProblemTable_init(void);
int sunFmFaultEventTable_init(void);

#define	CASE_STATE_CLOSED	"Closed"
#define	CASE_STATE_SOLVED	"Solved"
#define	CASE_STATE_REPAIRED	"Repaired"
#define	CASE_STATE_RESOLVED	"Resolved"
#define	CASE_STATE_CLOSE_WAIT	"Close-Wait"

#ifdef	__cplusplus
}
#endif

#endif	/* _PROBLEM_H */
