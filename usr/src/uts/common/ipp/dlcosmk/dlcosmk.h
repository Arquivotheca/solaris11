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
 * Copyright (c) 2002, Oracle and/or its affiliates. All rights reserved.
 */

#ifndef _IPP_DLCOSMK_DLCOSMK_H
#define	_IPP_DLCOSMK_DLCOSMK_H

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * Header file for DL CoS marker -- marks 802.1d user priority field on
 * VLAN devices
 */

#define	DLCOSMK_NEXT_ACTION_NAME	"dlcosmk.next_action"	/* string */
#define	DLCOSMK_COS			"dlcosmk.cos" 		/* uint8 */
#define	DLCOSMK_BAND			"dlcosmk.bband" 	/* uint8 */
#define	DLCOSMK_PRI			"dlcosmk.dlpri" 	/* uint8 */

#ifdef	__cplusplus
}
#endif

#endif /* _IPP_DLCOSMK_DLCOSMK_H */
