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
 * Copyright (c) 2011, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#include <sys/mac_cpu_impl.h>
#include <sys/pool.h>
#include <sys/pool_pset.h>
#include <sys/cpupart.h>
#include <sys/aggr.h>

#define	CPU_STRATEGY_OPTIMAL		0
#define	CPU_STRATEGY_SUBOPTIMAL		1
#define	CPU_STRATEGY_SUBOPTIMAL_2	2

#define	SUBOPTIMAL_STRATEGY(x)	\
	(x == CPU_STRATEGY_SUBOPTIMAL || x == CPU_STRATEGY_SUBOPTIMAL_2)

static int mac_cpu_get_aggr_port_count(mac_group_t *);

boolean_t mac_cpu_binding_on = B_TRUE;
boolean_t mac_cpu_socket_binding = B_TRUE;

/*
 * Re-targeting is allowed only for exclusive group or for primary.
 */
#define	RETARGETABLE_CLIENT(group, mcip)				\
	(((group) != NULL) &&						\
	    (((group)->mrg_state == MAC_GROUP_STATE_RESERVED) ||	\
	    mac_is_primary_client(mcip)))


void
mac_cpu_set_effective(mac_client_impl_t *mcip)
{
	mac_numa_group_t	*mac_numa;
	mac_resource_props_t	*mcip_mrp = MCIP_EFFECTIVE_PROPS(mcip);
	processorid_t		*cpus, cpuid;
	size_t			i, n, cnt, cpu_count, ARRAY_SZ;

	ARRAY_SZ = sizeof (mcip_mrp->mrp_cpu);
	cpus = kmem_zalloc(ARRAY_SZ, KM_SLEEP);
	mcip_mrp->mrp_ncpus = 0;
	for (i = 0; i < mcip->mci_numa_grp_cnt; i++) {
		mac_numa = &mcip->mci_numa_grp[i];
		numaio_get_effective_cpus(mac_numa->mn_grp,
		    cpus, ARRAY_SZ, &cpu_count);
		/*
		 * Store the CPUs in effective mrp; Make sure
		 * not to have duplicate entries.
		 */
		for (cnt = 0; cnt < cpu_count; cnt++) {
			cpuid = cpus[cnt];
			for (n = 0; n < mcip_mrp->mrp_ncpus; n++) {
				if (mcip_mrp->mrp_cpu[n] == cpuid)
					break;
			}

			if (n == mcip_mrp->mrp_ncpus) {
				mcip_mrp->mrp_cpu[n] = cpuid;
				mcip_mrp->mrp_ncpus++;
			}

			if (mcip_mrp->mrp_ncpus == ARRAY_SZ)
				break;
		}
	}
	kmem_free(cpus, ARRAY_SZ);
}

static void
mac_cpu_group_map(mac_client_impl_t *mcip)
{
	int			i;
	mac_numa_group_t	*mac_numa;

	for (i = 0; i < mcip->mci_numa_grp_cnt; i++) {
		mac_numa = &mcip->mci_numa_grp[i];
		numaio_group_map(mac_numa->mn_grp);
	}
}

static void
mac_cpu_apply_constraint(mac_client_impl_t *mcip)
{
	mac_resource_props_t	*mrp;
	mac_numa_group_t	*mac_numa;
	int			i, ret_val;
	numaio_constraint_t	*constraint;

	mrp = MCIP_RESOURCE_PROPS(mcip);
	/* set up constraint.  */
	if (mrp->mrp_mask & (MRP_POOL|MRP_CPUS_USERSPEC)) {
		constraint = numaio_constraint_create();
		if (mrp->mrp_mask & MRP_CPUS_USERSPEC) {
			ret_val = numaio_constraint_set_cpus(constraint,
			    (processorid_t *)mrp->mrp_cpu, mrp->mrp_ncpus);
		} else {
			if ((strcmp(mrp->mrp_pool, "pool_default") == 0) ||
			    (strcmp(mrp->mrp_pool, "") == 0)) {
				goto no_constraint;
			}

			ret_val = numaio_constraint_set_pool(constraint,
			    mrp->mrp_pool, 0);
		}

		if (ret_val == 0) {
			for (i = 0; i < mcip->mci_numa_grp_cnt; i++) {
				mac_numa = &mcip->mci_numa_grp[i];
				numaio_constraint_apply_group(constraint,
				    mac_numa->mn_grp,
				    NUMAIO_FLAG_BIND_DEFER);
			}
			mcip->mci_numa_constraint = constraint;
		} else {
			numaio_constraint_destroy(constraint);
		}
	}
no_constraint:
	for (i = 0; i < mcip->mci_numa_grp_cnt; i++) {
		mac_numa = &mcip->mci_numa_grp[i];
		ASSERT(mac_numa->mn_devinfo_obj == NULL);
		mac_numa->mn_devinfo_obj =
		    numaio_object_create_dev_info(mac_numa->mn_devinfo,
		    mcip->mci_name, 0);
		numaio_group_add_object(mac_numa->mn_grp,
		    mac_numa->mn_devinfo_obj, NUMAIO_FLAG_BIND_DEFER);
	}
}

static mac_numa_group_t *
mac_cpu_get_group(mac_client_impl_t *mcip, mac_ring_t *mac_ring)
{
	mac_impl_t		*lmip;
	mac_numa_group_t	*mac_numa;
	int			i;

	ASSERT(mcip->mci_numa_grp_cnt > 0);

	if (mcip->mci_numa_grp_cnt == 1)
		return (&mcip->mci_numa_grp[0]);

	ASSERT(mcip->mci_state_flags & MCIS_IS_AGGR);
	lmip =
	    (mac_impl_t *)mac_ring_get_lower_mip((mac_ring_handle_t)mac_ring);
	for (i = 0; i < mcip->mci_numa_grp_cnt; i++) {
		mac_numa = &mcip->mci_numa_grp[i];
		if (mac_numa->mn_devinfo == lmip->mi_dip)
			break;
	}

	return (mac_numa);
}

/*
 * If direct DLD_CAPAB_POLL is not enabled, then rf_squeue_obj will be NULL.
 */
static void
mac_cpu_rx_fanout_init(numaio_group_t *grp, mac_rx_fanout_t *rf)
{
	numaio_group_add_object(grp, rf->rf_worker_obj, NUMAIO_FLAG_BIND_DEFER);
	if (rf->rf_squeue_obj != NULL) {
		numaio_group_add_object(grp, rf->rf_squeue_obj,
		    NUMAIO_FLAG_BIND_DEFER);
		numaio_set_affinity(rf->rf_worker_obj, rf->rf_squeue_obj,
		    NUMAIO_AFF_STRENGTH_CPU, NUMAIO_FLAG_BIND_DEFER);
	}
}

static void
mac_cpu_ring_init(numaio_group_t *grp, mac_ring_t *ring)
{
	/*
	 * Clear numaio group reference if the object has one.
	 * This would be the case where a mac group is owned
	 * by a non-primay client but now has become shared
	 * and the client that made it shared is the primary.
	 */
	numaio_clear_group_reference(ring->mr_worker_obj);
	numaio_group_add_object(grp, ring->mr_worker_obj,
	    NUMAIO_FLAG_BIND_DEFER);
	if (ring->mr_intr_obj != NULL) {
		numaio_clear_group_reference(ring->mr_intr_obj);
		numaio_group_add_object(grp, ring->mr_intr_obj,
		    NUMAIO_FLAG_BIND_DEFER);
		numaio_set_affinity(ring->mr_worker_obj, ring->mr_intr_obj,
		    NUMAIO_AFF_STRENGTH_CPU, NUMAIO_FLAG_BIND_DEFER);
	}
}

static int
mac_cpu_binding_strategy(mac_client_impl_t *mcip, mac_group_t *tx_group,
    mac_group_t *rx_group)
{
	mac_numa_group_t	*mac_numa;
	numaio_lgrps_cpus_t	numa_lgrp_cpu;
	int			i, total_cpu_cnt, per_lgrp_cpu_cnt;
	int			rx_thread_cnt, tx_thread_cnt;
	int			nports, port_rx_thread_cnt, port_tx_thread_cnt;

	mac_numa = &mcip->mci_numa_grp[0];
	numaio_group_get_lgrp_cpu_count(mac_numa->mn_grp, &numa_lgrp_cpu);
	if (numa_lgrp_cpu.nlc_nlgrps > 0) {
		/*
		 * save the first lgrp_cpucnt as per_lgrp_cpu_cnt.
		 * If this count is not the same in all the
		 * remaining lgroups, then this gets changed to 0.
		 */
		per_lgrp_cpu_cnt = numa_lgrp_cpu.nlc_lgrp[0].lgc_lgrp_cpucnt;
		total_cpu_cnt = 0;
		for (i = 0; i < numa_lgrp_cpu.nlc_nlgrps; i++) {
			total_cpu_cnt +=
			    numa_lgrp_cpu.nlc_lgrp[i].lgc_lgrp_cpucnt;
			if (numa_lgrp_cpu.nlc_lgrp[i].lgc_lgrp_cpucnt !=
			    per_lgrp_cpu_cnt) {
				per_lgrp_cpu_cnt = 0;
			}
		}
	} else {
		/*
		 * if numaio_group_get_lgrp_cpu_count() does not return
		 * any lgroups, then set total_cpu_cnt and
		 * per_lgrp_cpu_cnt to ncpus.
		 */
		total_cpu_cnt = ncpus;
		per_lgrp_cpu_cnt = ncpus;
	}

	rx_thread_cnt = rx_group ? rx_group->mrg_cur_count : 0;
	tx_thread_cnt = tx_group ? tx_group->mrg_cur_count : 0;

	/*
	 * If there are uneven number of CPUs across the lgroups,
	 * then the bindings get distributed across lgroups. In
	 * such a case, if we have enough CPUs, then we choose
	 * OPTIMAL strategy.
	 */
	if (per_lgrp_cpu_cnt == 0) {
		if (rx_thread_cnt+mcip->mci_rx_fanout_cnt <= total_cpu_cnt &&
		    tx_thread_cnt <= total_cpu_cnt) {
			return (CPU_STRATEGY_OPTIMAL);
		}
	} else {
		if ((rx_thread_cnt + mcip->mci_rx_fanout_cnt) <=
		    per_lgrp_cpu_cnt && tx_thread_cnt <= per_lgrp_cpu_cnt) {
			return (CPU_STRATEGY_OPTIMAL);
		}
		/*
		 * This is the G5 case. If you have 8 Rx rings, 8 fanout
		 * threads, and 8 Tx rings, then put all of them onto one
		 * socket.
		 */
		if (rx_thread_cnt == mcip->mci_rx_fanout_cnt &&
		    rx_thread_cnt <= per_lgrp_cpu_cnt &&
		    tx_thread_cnt <= per_lgrp_cpu_cnt &&
		    mac_cpu_socket_binding) {
			return (CPU_STRATEGY_SUBOPTIMAL_2);
		}
		/*
		 * G5 optimization for aggr: If the number of RX and TX
		 * rings on a port is equal to or less than the number
		 * of CPUs in an lgroup, then having both RX rings and
		 * TX rings in the same lgroup is ideal. For that,
		 * CPU_STRATEGY_SUBOPTIMAL_2 should be returned.
		 */
		if (mcip->mci_state_flags & MCIS_IS_AGGR &&
		    tx_group != NULL && rx_group != NULL &&
		    mcip->mci_numa_grp_cnt > 1 && mac_cpu_socket_binding) {
			nports = mac_cpu_get_aggr_port_count(tx_group);
			/*
			 * It is possible for each port in the aggregation
			 * to have a different number of RX/TX rings. If
			 * such is the case, the strategy returned may not
			 * be optimal.
			 */
			port_rx_thread_cnt = rx_group->mrg_cur_count/nports;
			port_tx_thread_cnt = tx_group->mrg_cur_count/nports;
			if (rx_thread_cnt == mcip->mci_rx_fanout_cnt &&
			    port_rx_thread_cnt <= per_lgrp_cpu_cnt &&
			    port_tx_thread_cnt <= per_lgrp_cpu_cnt) {
				return (CPU_STRATEGY_SUBOPTIMAL_2);
			}
		}
	}

	return (CPU_STRATEGY_SUBOPTIMAL);
}

/*
 * Get an RX ring from an RX group that is part of the passed mac_numa_group.
 */
static mac_ring_t *
mac_get_mac_numa_rx_ring(mac_client_impl_t *mcip, mac_group_t *rx_group,
    mac_numa_group_t *mac_numa)
{
	mac_ring_t *ring;

	for (ring = rx_group->mrg_rings; ring != NULL; ring = ring->mr_next) {
		if (mac_cpu_get_group(mcip, ring) == mac_numa)
			return (ring);
	}

	return (NULL);
}

static void
mac_cpu_binding_setup(mac_client_impl_t *mcip)
{
	flow_entry_t		*flent = mcip->mci_flent;
	boolean_t		tx_retarget, rx_retarget;
	mac_group_t		*tx_group, *rx_group;
	int			i, cnt, subset_sz, strategy;
	mac_numa_group_t	*mac_numa, *prev_mac_numa;
	mac_ring_t		*ring, *prev_ring, *rx_ring;
	mac_rx_fanout_t		*subset, *rf;
	numaio_aff_strength_t	fanout_strength = NUMAIO_AFF_STRENGTH_SOCKET;

	tx_group = (mac_group_t *)flent->fe_tx_ring_group;
	tx_retarget = RETARGETABLE_CLIENT(tx_group, mcip);

	rx_group = (mac_group_t *)flent->fe_rx_ring_group;
	rx_retarget = RETARGETABLE_CLIENT(rx_group, mcip);

	strategy = mac_cpu_binding_strategy(mcip, tx_retarget ?
	    tx_group : NULL, rx_retarget ? rx_group : NULL);

	if (rx_group != NULL) {
		subset_sz = mcip->mci_rx_fanout_cnt_per_ring;
		if (SUBOPTIMAL_STRATEGY(strategy) && subset_sz == 1)
			fanout_strength = NUMAIO_AFF_STRENGTH_CPU;

		prev_ring = NULL;
		prev_mac_numa = NULL;
		for (ring = rx_group->mrg_rings;
		    ring != NULL; ring = ring->mr_next) {
			mac_numa = mac_cpu_get_group(mcip, ring);
			if (rx_retarget)
				mac_cpu_ring_init(mac_numa->mn_grp, ring);

			subset =
			    &mcip->mci_rx_fanout[ring->mr_gindex * subset_sz];
			for (cnt = 0; cnt < subset_sz; cnt++) {
				rf = &subset[cnt];
				mac_cpu_rx_fanout_init(mac_numa->mn_grp, rf);
				/*
				 * If retargetable client, then set the
				 * affinity between ring and rf fanout
				 * threads.
				 */
				if (rx_retarget) {
					numaio_set_affinity(ring->mr_worker_obj,
					    rf->rf_worker_obj,
					    fanout_strength,
					    NUMAIO_FLAG_BIND_DEFER);
				}
			}
			/*
			 * Set socket affinity between each of the rings
			 * so that they all lie in the same socket. We
			 * do this if the present mac_numa is the same
			 * previous. Note that multiple mac_numas are
			 * present only for aggr case.
			 * It is important to note that while an RX/TX
			 * group contain pseudo rings from different
			 * aggr ports, the rings of each port are
			 * placed next to eachother followed by rings of
			 * another port and so on.
			 */
			if (prev_mac_numa == mac_numa) {
				numaio_set_affinity(ring->mr_worker_obj,
				    prev_ring->mr_worker_obj,
				    NUMAIO_AFF_STRENGTH_SOCKET,
				    NUMAIO_FLAG_BIND_DEFER);
			} else {
				prev_mac_numa = mac_numa;
			}
			prev_ring = ring;
		}
	} else {
		ASSERT(mcip->mci_numa_grp_cnt == 1);
		mac_numa = &mcip->mci_numa_grp[0];
		for (i = 0; i < mcip->mci_rx_fanout_cnt; i++) {
			rf = &mcip->mci_rx_fanout[i];
			mac_cpu_rx_fanout_init(mac_numa->mn_grp, rf);
		}
	}

	if (tx_retarget) {
		prev_ring = NULL;
		prev_mac_numa = NULL;
		for (ring = tx_group->mrg_rings;
		    ring != NULL; ring = ring->mr_next) {
			mac_numa = mac_cpu_get_group(mcip, ring);
			mac_cpu_ring_init(mac_numa->mn_grp, ring);
			/*
			 * Set socket affinity between each of the rings
			 * so that they all lie in the same socket.
			 */
			if (prev_mac_numa == mac_numa) {
				numaio_set_affinity(ring->mr_worker_obj,
				    prev_ring->mr_worker_obj,
				    NUMAIO_AFF_STRENGTH_SOCKET,
				    NUMAIO_FLAG_BIND_DEFER);
			} else {
				prev_mac_numa = mac_numa;
				if (strategy == CPU_STRATEGY_SUBOPTIMAL_2 &&
				    rx_retarget) {
					/*
					 * For CPU_STRATEGY_SUBOPTIMAL_2, we
					 * want both RX and TX rings to lie
					 * on the same socket. So set a
					 * socket affinity between an RX and
					 * TX ring that belong to the same
					 * mac_numa.
					 */
					rx_ring = mac_get_mac_numa_rx_ring(mcip,
					    rx_group, mac_numa);
					numaio_set_affinity(ring->mr_worker_obj,
					    rx_ring->mr_worker_obj,
					    NUMAIO_AFF_STRENGTH_SOCKET,
					    NUMAIO_FLAG_BIND_DEFER);
				}
			}
			prev_ring = ring;
		}
	}
}

static int
mac_cpu_get_aggr_port_count(mac_group_t *group)
{
	mac_impl_t	**lmip_array;
	mac_impl_t	*lmip;
	int		i, lmip_cnt;
	mac_ring_t	*ring;
	mac_handle_t	mh;

	lmip_array = (mac_impl_t **)kmem_zalloc(sizeof (mac_impl_t *) *
	    MAX_RINGS_PER_GROUP, KM_SLEEP);

	lmip_cnt = 0;
	for (ring = group->mrg_rings; ring != NULL; ring = ring->mr_next) {
		mh = mac_ring_get_lower_mip((mac_ring_handle_t)ring);
		lmip = (mac_impl_t *)mh;
		for (i = 0; i < lmip_cnt; i++) {
			if (lmip_array[i] == lmip)
				break;
		}

		if (i == lmip_cnt) {
			lmip_array[lmip_cnt] = lmip;
			lmip_cnt++;
		}
	}

	kmem_free(lmip_array, sizeof (mac_impl_t *) * MAX_RINGS_PER_GROUP);
	return (lmip_cnt);
}

void
mac_cpu_group_init(mac_client_impl_t *mcip)
{
	int group_count = 1;

	/*
	 * We can have at most AGGR_MAX_PORTS NUMA groups as
	 * aggr uses one numa group per port. Other type of
	 * links use just one numa group per link.
	 */
	if (mcip->mci_state_flags & MCIS_IS_AGGR)
		group_count = AGGR_MAX_PORTS;
	mcip->mci_numa_grp =
	    (mac_numa_group_t *)kmem_zalloc(sizeof (mac_numa_group_t) *
	    group_count, KM_SLEEP);
	mcip->mci_numa_grp_cnt = 0;
	mcip->mci_numa_constraint = NULL;
}

void
mac_cpu_group_fini(mac_client_impl_t *mcip)
{
	int group_count = 1;

	ASSERT(mcip->mci_numa_grp_cnt == 0);
	if (mcip->mci_state_flags & MCIS_IS_AGGR)
		group_count = AGGR_MAX_PORTS;
	kmem_free(mcip->mci_numa_grp,
	    sizeof (mac_numa_group_t) * group_count);
	mcip->mci_numa_grp = NULL;
}

static void
mac_cpu_create_groups(mac_client_impl_t *mcip)
{
	mac_handle_t		mh;
	mac_impl_t		*lmip;
	dev_info_t		*last_dip;
	int			gcnt;
	mac_numa_group_t	*mac_numa;
	char			name[MAXNAMELEN];
	flow_entry_t		*flent;
	mac_group_t		*tx_group, *rx_group;
	mac_ring_t		*pring;

	ASSERT(mcip->mci_numa_grp_cnt == 0);
	if (mcip->mci_state_flags & MCIS_IS_AGGR) {
		flent = mcip->mci_flent;
		tx_group = (mac_group_t *)flent->fe_tx_ring_group;
		rx_group = (mac_group_t *)flent->fe_rx_ring_group;
		/*
		 * The Tx rings of each port is exported as a pseudo
		 * Tx ring in the aggr's Tx mac_group. Even for NICs
		 * that don't have any Tx rings, a pseudo Tx ring is
		 * created and exported to Tx mac group. Thus all
		 * lower macs (mip) have a representation via
		 * atleast one Tx ring in the aggr's Tx mac group.
		 * Counting the distinct lower mips for each of the
		 * Tx ring will give the number of ports in the aggr.
		 *
		 * Unlike Tx rings, if a NIC does not have any Rx
		 * ring, then there won't be a corresponding pseudo
		 * Rx ring in the aggr's Rx group. Thus walking the
		 * Rx rings and adding up all the distinct lower mips
		 * won't necessarily give the correct count of number
		 * of ports.
		 */
		/*
		 * Compute the number of ports based on RX and TX
		 * rings in the aggregation. They should match.
		 * Otherwise just create one numa group.
		 */
		if (tx_group == NULL || rx_group == NULL)
			goto one_group;

		if (mac_cpu_get_aggr_port_count(tx_group) !=
		    mac_cpu_get_aggr_port_count(rx_group)) {
			goto one_group;
		}
		/*
		 * Associate a numaio_group for each port of the
		 * aggregation. To find the number of ports, we
		 * go through all the Tx rings and create a
		 * numaio_group for every distinct dev_info
		 * pointer.
		 */
		last_dip = NULL;
		for (pring = tx_group->mrg_rings;
		    pring != NULL; pring = pring->mr_next) {
			mh = mac_ring_get_lower_mip((mac_ring_handle_t)pring);
			lmip = (mac_impl_t *)mh;
			if (lmip->mi_dip == last_dip)
				continue;

			for (gcnt = 0; gcnt < mcip->mci_numa_grp_cnt; gcnt++) {
				mac_numa = &mcip->mci_numa_grp[gcnt];
				if (mac_numa->mn_devinfo == lmip->mi_dip)
					break;
			}

			if (gcnt == mcip->mci_numa_grp_cnt) {
				mac_numa = &mcip->mci_numa_grp[gcnt];
				ASSERT(mac_numa->mn_grp == NULL);
				ASSERT(mac_numa->mn_devinfo == NULL);
				(void) snprintf(name, MAXNAMELEN,
				    "%s-%s", mcip->mci_name, lmip->mi_name);
				mac_numa->mn_grp = numaio_group_create(name);
				mac_numa->mn_devinfo = lmip->mi_dip;
				mcip->mci_numa_grp_cnt++;
				last_dip = lmip->mi_dip;
			}
		}
	} else {
one_group:
		mac_numa = &mcip->mci_numa_grp[0];
		mac_numa->mn_grp = numaio_group_create(mcip->mci_name);
		mcip->mci_numa_grp_cnt = 1;
		mac_numa->mn_devinfo = mcip->mci_mip->mi_dip;
	}
}

void
mac_cpu_setup(mac_client_impl_t *mcip)
{
	if (!mac_cpu_binding_on)
		return;

	if (mcip->mci_numa_grp_cnt != 0)
		mac_cpu_teardown(mcip);

	/*
	 * No need to do CPU setup for Aggr port. Do it
	 * only if the client is an aggr (MCIS_IS_AGGR).
	 */
	if (mcip->mci_state_flags & MCIS_IS_AGGR_PORT)
		return;

	mac_cpu_create_groups(mcip);
	mac_cpu_apply_constraint(mcip);
	mac_cpu_binding_setup(mcip);
	mac_cpu_group_map(mcip);
}

void
mac_cpu_pool_setup(mac_client_impl_t *mcip)
{
	cpupart_t		*cpupart;
	boolean_t		use_default = B_FALSE;
	mac_resource_props_t	*mrp = MCIP_RESOURCE_PROPS(mcip);
	mac_resource_props_t	*emrp = MCIP_EFFECTIVE_PROPS(mcip);

	pool_lock();
	cpupart = mac_pset_find(mrp, &use_default);
	mac_set_pool_effective(use_default, cpupart, mrp, emrp);
	pool_unlock();
	mac_cpu_setup(mcip);
}

static void
mac_cpu_destroy_groups(mac_client_impl_t *mcip)
{
	int			i;
	mac_numa_group_t	*mac_numa;
	numaio_constraint_t	*constraint;

	ASSERT(mcip->mci_numa_grp_cnt != 0);
	constraint = mcip->mci_numa_constraint;
	for (i = 0; i < mcip->mci_numa_grp_cnt; i++) {
		mac_numa = &mcip->mci_numa_grp[i];
		if (mac_numa->mn_devinfo_obj != NULL)
			numaio_object_destroy(mac_numa->mn_devinfo_obj);
		mac_numa->mn_devinfo_obj = NULL;
		mac_numa->mn_devinfo = NULL;
		if (constraint != NULL) {
			numaio_constraint_clear_group(constraint,
			    mac_numa->mn_grp, NUMAIO_FLAG_BIND_DEFER);
		}
		numaio_group_destroy(mac_numa->mn_grp);
		mac_numa->mn_grp = NULL;
	}
	mcip->mci_numa_grp_cnt = 0;
	if (constraint != NULL) {
		numaio_constraint_destroy(constraint);
		mcip->mci_numa_constraint = NULL;
	}
}

static void
mac_cpu_binding_teardown(mac_client_impl_t *mcip)
{
	int			i;
	mac_numa_group_t	*mac_numa;

	/*
	 * mci_numa_grp_cnt will be greater than 1 for
	 * aggr with each count representing a port.
	 */
	for (i = 0; i < mcip->mci_numa_grp_cnt; i++) {
		mac_numa = &mcip->mci_numa_grp[i];
		numaio_group_remove_all_objects(mac_numa->mn_grp);
	}
}

void
mac_cpu_teardown(mac_client_impl_t *mcip)
{
	if (mcip->mci_numa_grp_cnt == 0)
		return;
	mac_cpu_binding_teardown(mcip);
	mac_cpu_destroy_groups(mcip);
}

static void
mac_cpu_sq_affinity_add(mac_rx_fanout_t *rf)
{
	numaio_group_t	*grp;

	grp = numaio_object_get_group(rf->rf_worker_obj);
	if (grp != NULL) {
		numaio_group_add_object(grp, rf->rf_squeue_obj,
		    NUMAIO_FLAG_BIND_DEFER);
		numaio_set_affinity(rf->rf_worker_obj, rf->rf_squeue_obj,
		    NUMAIO_AFF_STRENGTH_CPU, 0);
	}
}

static void
mac_cpu_sq_affinity_remove(mac_rx_fanout_t *rf)
{
	numaio_clear_affinity(rf->rf_worker_obj, rf->rf_squeue_obj, 0);
	numaio_clear_group_reference(rf->rf_squeue_obj);
}

static void
mac_cpu_intr_modify(mac_client_impl_t *mcip, mac_ring_t *ring)
{
	mac_numa_group_t	*mac_numa;

	mac_numa = mac_cpu_get_group(mcip, ring);
	if (ring->mr_intr_obj != NULL) {
		numaio_group_add_object(mac_numa->mn_grp, ring->mr_intr_obj,
		    NUMAIO_FLAG_BIND_DEFER);
		numaio_set_affinity(ring->mr_worker_obj, ring->mr_intr_obj,
		    NUMAIO_AFF_STRENGTH_CPU, 0);
	}
}

static void
mac_cpu_ring_remove(mac_ring_t *ring)
{
	if (ring->mr_intr_obj != NULL)
		numaio_clear_group_reference(ring->mr_intr_obj);
	numaio_clear_group_reference(ring->mr_worker_obj);
}

void
mac_cpu_modify(mac_client_impl_t *mcip, mac_cpu_state_t state, void *arg)
{
	switch (state) {
	case MAC_CPU_SQUEUE_ADD:
		mac_cpu_sq_affinity_add(arg);
		break;
	case MAC_CPU_SQUEUE_REMOVE:
		mac_cpu_sq_affinity_remove(arg);
		break;
	case MAC_CPU_INTR:
		mac_cpu_intr_modify(mcip, arg);
		break;
	case MAC_CPU_RING_REMOVE:
		mac_cpu_ring_remove(arg);
		break;
	}
}
