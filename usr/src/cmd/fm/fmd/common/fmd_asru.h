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
 * Copyright (c) 2004, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#ifndef	_FMD_ASRU_H
#define	_FMD_ASRU_H

#include <sys/types.h>
#include <pthread.h>

#ifdef	__cplusplus
extern "C" {
#endif

#include <fmd_api.h>
#include <fmd_log.h>
#include <fmd_list.h>
#include <fmd_topo.h>

typedef struct fmd_asru_link {
	fmd_list_t al_list;		/* linked list next/prev pointers */
	struct fmd_asru_link *al_asru_next;	/* next link on hash chain */
	struct fmd_asru_link *al_case_next;	/* next link on hash chain */
	struct fmd_asru_link *al_fru_next;	/* next link on hash chain */
	struct fmd_asru_link *al_label_next;	/* next link on hash chain */
	struct fmd_asru_link *al_rsrc_next;	/* next link on hash chain */
	char *al_uuid;			/* uuid for asru cache entry (ro) */
	fmd_log_t *al_log;		/* persistent event log */
	char *al_asru_name;		/* string form of asru fmri (ro) */
	char *al_fru_name;		/* string form of fru fmri (ro) */
	char *al_rsrc_name;		/* string form of resource fmri (ro) */
	char *al_label;			/* label */
	char *al_case_uuid;		/* case uuid */
	fmd_case_t *al_case;		/* case associated with last change */
	nvlist_t *al_event;		/* event associated with last change */
	uint_t al_flags;		/* flags (see below) */
	uint8_t al_reason;		/* repair reason (see below) */
	pthread_mutex_t al_lock;	/* lock protecting remaining members */
	char *al_root;			/* directory for cache entry (ro) */
} fmd_asru_link_t;

#define	FMD_ASRU_FAULTY		0x01	/* asru has been diagnosed as faulty */
#define	FMD_ASRU_UNUSABLE	0x02	/* asru can not be used at present */
#define	FMD_ASRU_INVISIBLE	0x10	/* asru is not visibly administered */
#define	FMD_ASRU_PRESENT	0x40	/* asru present at last R$ update */
#define	FMD_ASRU_DEGRADED	0x80	/* asru service is degraded */
#define	FMD_ASRU_PROXY		0x100	/* asru on proxy */
#define	FMD_ASRU_PROXY_WITH_ASRU 0x200	/* asru accessible locally on proxy */
#define	FMD_ASRU_PROXY_PRESENCE	0x400	/* use presence state sent from diag */
#define	FMD_ASRU_PROXY_RDONLY	0x800	/* proxy over readonly transport */

/*
 * Note the following are defined in order of increasing precedence and
 * this should not be changed
 */
#define	FMD_ASRU_REMOVED	0	/* asru removed */
#define	FMD_ASRU_ACQUITTED	1	/* asru acquitted */
#define	FMD_ASRU_REPAIRED	2	/* asru repaired */
#define	FMD_ASRU_REPLACED	3	/* asru replaced */

#define	FMD_ASRU_STATE	(FMD_ASRU_FAULTY | FMD_ASRU_UNUSABLE)

#define	FMD_ASRU_AL_HASH_NAME(a, off) \
	*(char **)((uint8_t *)a + off)
#define	FMD_ASRU_AL_HASH_NEXT(a, off) \
	*(fmd_asru_link_t **)((uint8_t *)a + off)
#define	FMD_ASRU_AL_HASH_NEXTP(a, off) \
	(fmd_asru_link_t **)((uint8_t *)a + off)

typedef struct fmd_asru_hash {
	pthread_mutex_t ah_lock;	/* lock protecting hash contents */
	fmd_asru_link_t **ah_asru_hash;	/* hash bucket array for asrus */
	fmd_asru_link_t **ah_case_hash;	/* hash bucket array for frus */
	fmd_asru_link_t **ah_fru_hash;	/* hash bucket array for cases */
	fmd_asru_link_t **ah_label_hash;	/* label hash bucket array */
	fmd_asru_link_t **ah_rsrc_hash;	/* hash bucket array for rsrcs */
	uint_t ah_hashlen;		/* length of hash bucket array */
	char *ah_dirpath;		/* path of hash's log file directory */
	uint64_t ah_lifetime;		/* max lifetime of log if not present */
	uint_t ah_al_count;		/* count of number of entries in hash */
	int ah_error;			/* error from opening asru log */
	fmd_topo_t *ah_topo;		/* topo handle */
	uint_t ah_refs;			/* reference count */
	pthread_cond_t ah_cv;		/* condition variable for dr update */
} fmd_asru_hash_t;

extern fmd_asru_hash_t *fmd_asru_hash_create(const char *, const char *);
extern void fmd_asru_hash_destroy(fmd_asru_hash_t *);
extern void fmd_asru_hash_refresh(fmd_asru_hash_t *);
extern void fmd_asru_hash_replay(fmd_asru_hash_t *);

extern void fmd_asru_al_hash_apply(fmd_asru_hash_t *,
    void (*)(fmd_asru_link_t *, void *), void *);
extern void fmd_asru_hash_apply_by_asru(fmd_asru_hash_t *, const char *,
    void (*)(fmd_asru_link_t *, void *), void *, boolean_t);
extern void fmd_asru_hash_apply_by_label(fmd_asru_hash_t *, const char *,
    void (*)(fmd_asru_link_t *, void *), void *);
extern void fmd_asru_hash_apply_by_fru(fmd_asru_hash_t *, const char *,
    void (*)(fmd_asru_link_t *, void *), void *, boolean_t);
extern void fmd_asru_hash_apply_by_rsrc(fmd_asru_hash_t *, const char *,
    void (*)(fmd_asru_link_t *, void *), void *, boolean_t);
extern void fmd_asru_hash_apply_by_case(fmd_asru_hash_t *, fmd_case_t *,
    void (*)(fmd_asru_link_t *, void *), void *);

extern fmd_asru_link_t *fmd_asru_hash_create_entry(fmd_asru_hash_t *,
    fmd_case_t *, nvlist_t *);
extern void fmd_asru_hash_delete_case(fmd_asru_hash_t *, fmd_case_t *);

extern void fmd_asru_clear_aged_rsrcs();

/*
 * flags used in fara_bywhat field in fmd_asru_rep_arg_t
 */
#define	FARA_ALL	0
#define	FARA_BY_CASE	1
#define	FARA_BY_ASRU	2
#define	FARA_BY_FRU	3
#define	FARA_BY_RSRC	4
#define	FARA_BY_LABEL	5

/*
 * Return values for fmd_asru_repaired. May return "ok" or "not replaced".
 * If no fault is found we will get default value of "not found".
 */
#define	FARA_OK 0
#define	FARA_ERR_RSRCNOTF 1
#define	FARA_ERR_RSRCNOTR 2

/*
 * The following structures are used to pass arguments to the corresponding
 * function when walking the resource cache by case etc.
 */
typedef struct {
	uint8_t fara_reason;	/* repaired, acquit, replaced, removed */
	uint8_t fara_bywhat;	/* whether doing a walk by case, asru, etc */
	int *fara_rval;		/* for return success or failure */
	char *fara_uuid;	/* uuid can be passed in for comparison */
} fmd_asru_rep_arg_t;
extern void fmd_asru_repaired(fmd_asru_link_t *, void *);
extern void fmd_asru_resolved(fmd_asru_link_t *, void *);
extern void fmd_asru_isolated(fmd_asru_link_t *, void *);
extern void fmd_asru_flush(fmd_asru_link_t *, void *);

typedef struct {
	int	*faus_countp;
	int	faus_maxcount;
	uint8_t *faus_ba;		/* received status for each suspect */
	uint8_t *faus_proxy_asru;	/* asru on proxy for each suspect? */
	uint8_t *faus_diag_asru;	/* asru on diag for each suspect? */
	boolean_t	faus_is_proxy;	/* are we on the proxy side? */
	int	faus_repaired;		/* were any suspects repaired */
} fmd_asru_update_status_t;
extern void fmd_asru_update_status(fmd_asru_link_t *alp, void *arg);

typedef struct {
	int	*fasp_countp;
	int	fasp_maxcount;
	uint8_t *fasp_proxy_asru;	/* asru on proxy for each suspect? */
	int	fasp_proxy_external;	/* is this an external transport? */
	int	fasp_proxy_rdonly;	/* is this a rdonly transport? */
} fmd_asru_set_on_proxy_t;
extern void fmd_asru_set_on_proxy(fmd_asru_link_t *alp, void *arg);

extern void fmd_asru_update_containees(fmd_asru_link_t *alp, void *arg);

typedef struct {
	int	*facs_countp;
	int	facs_maxcount;
} fmd_asru_close_status_t;
extern void fmd_asru_close_status(fmd_asru_link_t *alp, void *arg);

extern int fmd_asru_setflags(fmd_asru_link_t *, uint_t);
extern int fmd_asru_clrflags(fmd_asru_link_t *, uint_t, uint8_t);
extern void fmd_asru_log_resolved(fmd_asru_link_t *, void *);
extern int fmd_asru_al_getstate(fmd_asru_link_t *);
extern void fmd_asru_check_if_aged(fmd_asru_link_t *, void *);
extern void fmd_asru_handle_dr(fmd_asru_link_t *, void *);
extern void fmd_asru_set_serial(topo_hdl_t *, nvlist_t *, nvlist_t *);
void fmd_asru_most_recent(fmd_asru_link_t *, void *);
extern void fmd_asru_update_fault(nvlist_t *, char **, char **, char **,
    char **);

#ifdef	__cplusplus
}
#endif

#endif	/* _FMD_ASRU_H */
