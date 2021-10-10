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

#ifndef	_SYS_BALANCE_H
#define	_SYS_BALANCE_H

#include <sys/types.h>
#include <sys/ddi.h>
#include <sys/sunddi.h>
#include <sys/list.h>
#include <sys/sunldi.h>
#include <sys/ddi_intr.h>
#include <sys/ddi_intr_impl.h>

#ifdef	__cplusplus
extern "C" {
#endif

#ifdef	_KERNEL

/*
 * Misc defines for I/O load balancer
 */
#define	BAL_DEFWIN		60	/* keep a window of 60 sec.. */
#define	BAL_DEF_INTERVAL	10	/* gather stats every 10 sec. */
#define	BAL_NDELTAS (BAL_DEFWIN / BAL_DEF_INTERVAL)
#define	BAL_INTRD_STRATEGY	0	/* balance evenly among all cpus */
#define	BAL_NUMA_STRATEGY	1	/* balance using NUMA framework info */
#define	BAL_LOAD_BASED	0	/* rebalance due to unbalanced I/O load */
#define	BAL_CPU_OFFLINE	1	/* rebalance due to cpu going offline */
#define	BAL_CPU_ONLINE	2	/* rebalance due to cpu going on line */

/*
 * Structures for capturing per-cpu interrupt load stats/deltas
 * We keep 60 seconds worth of deltas.
 */

/*
 * Info for accessing the pci intr nexus for each ivec
 */
typedef struct pci_nexus_info {
	list_node_t	next; 	/* next nexus in list */
	char		nexus_name[MAXPATHLEN]; /* nexus name */
	ldi_handle_t	lh; 	/* nexus ldi handle */
	ldi_ident_t	li;	/* nexus ldi ident */
} nexusinfo_t;

/*
 * There is a chain of these one per ivec hanging off each cpu info struct
 */
typedef struct ivecdat {
	list_node_t	next;		/* next ivec in chain */
	ddi_intr_handle_impl_t *ih;	/* interrupt handle */
	dev_info_t	*dip;		/* dip for device */
	dev_info_t	*pdip;		/* dip for devices pci control nexus */
	struct ivecdat *istack;		/* stack link for DFS use */
	uint8_t		ingoal;		/* set if searching ingoal subtree */
	uint8_t		inbest;		/* set if ivec is part of best match */
	uint8_t		inocnt;		/* no. of vectors in fixed/msi group */
	uint8_t		numa_managed;	/* set if ivec managed by numa fmwrk */
	uint8_t		do_without;	/* flag to DFS the without subtree */
	int		inum;		/* interrupt no. */
	uint32_t	cookie;		/* irq/mondo for this vector */
	processorid_t	ocpu;		/* cpu ivec is bound to */
	processorid_t	ncpu;		/* cpu to move ivec to */
	uint64_t	*ticksp;	/* device service ticks pointer */
	hrtime_t	lastsnap;	/* last snap of ivec int time */
	hrtime_t	cursnap;	/* current snap of ivec int time */
	hrtime_t	inttime[BAL_NDELTAS];	/* nsec spent in int */
	hrtime_t	tot_inttime;	/* sum of nsec in int for all deltas */
	hrtime_t	loadsum;	/* sum of totals for rest of slist */
	uint64_t	goalarg;	/* goal arg for DFS of ivecs */
	nexusinfo_t	*nexus;		/* control nexus for this ivec */
} ivecdat_t;

typedef struct cpu_loadinf {
	list_node_t	next;		/* next cpu in chain */
	list_node_t	noffline;	/* next cpu in offlined chain */
	processorid_t	cpuid;		/* cpu id */
	list_t		ivecs;		/* interrupt vectors on this cpu */
	int		offline;	/* offline flag, prot. by cpu_lock */
	int		nvec;		/* no. int vectors on this cpu */
	hrtime_t	lastload;	/* last snapshot of cpu load */
	hrtime_t	curload;	/* current snapshot of cpu load */
	hrtime_t	tload[BAL_NDELTAS]; /* total cpu load in nsec */
	hrtime_t	cload;		/* sum of delta loads */
	hrtime_t	cinttime;	/* sum of tot_inttimes for all ivecs */
	hrtime_t	bigintr;	/* biggest ivec time for this cpu */
	int		intrload;	/* interrupt load (cinttime / cload) */
	int		nextid;		/* next cpuid present */
} cpu_loadinf_t;

extern void bal_numa_managed_int(ddi_intr_handle_t, processorid_t);
extern void bal_numa_managed_clear(ddi_intr_handle_t, processorid_t);
extern void bal_register_int(dev_info_t *, dev_info_t *,
    ddi_intr_handle_impl_t *, uint64_t *, int, uint32_t);
extern void bal_remove_int(ddi_intr_handle_impl_t *, uint64_t *);
extern int io_balancer_control(int);
#endif	/* _KERNEL */

#ifdef	__cplusplus
}
#endif

#endif /* _SYS_BALANCE_H */
