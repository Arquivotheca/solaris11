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

#ifndef	_FRU_H
#define	_FRU_H

#ifdef	__cplusplus
extern "C" {
#endif

#define	TOPO_EV_FMRI		"fmri"
#define	TOPO_EV_FMRISTR		"fmristr"
#define	TOPO_EV_TYPE		"type"

typedef enum {
	FRU_DISK,
	FRU_FAN,
	FRU_PSU,
	FRU_CONTROLLER
} fru_type_t;

#define	FRU_TIMER_INITIAL	((void *)(uintptr_t)1)
#define	FRU_TIMER_POLL		((void *)(uintptr_t)2)

typedef struct fru_chassis {
	char *frc_id;			/* chassis ID */
	struct fru_chassis *frc_next;	/* next chassis in list */
	nvlist_t *frc_fmri;		/* chassis FMRI */
	char *frc_fmristr;		/* string form of chassis FMRI */
	boolean_t frc_present;		/* current present state */
	boolean_t frc_last_present;	/* last known present state */
	id_t frc_timer;			/* for tracking chassis addition */
	boolean_t frc_adding;		/* recently added chassis */
} fru_chassis_t;

typedef struct fru {
	struct fru *fru_chain;		/* next fru on hash chain */
	struct fru *fru_next;		/* next logical fru */
	struct fru *fru_prev;		/* previous logical fru */
	fru_type_t fru_type;		/* type of FRU */
	char *fru_fmristr;		/* string form of FRU fmri (ro) */
	nvlist_t *fru_fmri;		/* nvlist form of FRU fmri (ro) */
	nvlist_t *fru_ctl;		/* fmri of control (bay) node */
	fru_chassis_t *fru_chassis;	/* containing chassis */
	boolean_t fru_present;		/* current present state */
	boolean_t fru_last_present;	/* last known present state */
	boolean_t fru_valid;		/* fru state is valid */
	boolean_t fru_faulted;		/* known to be faulted */
} fru_t;

typedef struct fru_hash {
	fmd_hdl_t *fh_hdl;		/* associated fmd handle */
	fru_t **fh_hash;		/* hash bucket array for frus */
	fru_t *fh_list;			/* linked list of frus in hash */
	uint_t fh_hashlen;		/* length of hash bucket array */
	uint_t fh_count;		/* count of number of entries in hash */
	fru_chassis_t *fh_chassis;	/* chassis list */
	topo_hdl_t *fh_scanhdl;		/* last scan handle */
	boolean_t fh_initial;		/* initial scan */
} fru_hash_t;

#ifdef	__cplusplus
}
#endif

#endif	/* _FRU_H */
