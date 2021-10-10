/*
 * Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"

/*
 * This file contains memtest CMP core parking/unparking specific code.
 */

#include <sys/memtestio.h>
#include <sys/memtestio_u.h>
#include <sys/memtestio_chp.h>
#include <sys/memtestio_jg.h>
#include <sys/memtest_u.h>
#include <sys/memtest_ch.h>
#include <sys/memtest_chp.h>
#include <sys/memtest_jg.h>
#include <sys/memtest_pn.h>
#include <sys/memtest_asm.h>
#include <sys/memtest_cmp.h>
#include <sys/cmpregs.h>
#include <sys/pghw.h>

/* number of tries to park/unpark core */
#define	MAX_PARK_TRIES  10

/*
 * The following two routines to quiesce and unquiesce sibling cores
 * in a CMP system are Panther (US4+) specific.
 *
 * One of the L2 or L3 caches are put into split mode so that accesses
 * from one core will not pollute the other cores cache space.
 *
 * Note that the other core is still parked for a short period while
 * the actual cache split takes place.
 */
int
memtest_cmp_quiesce(mdata_t *mdatap)
{
	ioc_t		*iocp = mdatap->m_iocp;
	int		core_parked = 0;
	char		*fname = "memtest_cmp_quiesce";

	if (CPU_SHARED_CACHE(mdatap->m_cip) && CPU_ISPANTHER(mdatap->m_cip)) {
		if ((memtest_get_sibling(mdatap)) != NULL) {
			if (memtest_park_core(mdatap) != 0) {
				DPRINTF(0, "%s: core park failed! Aborting "
				    "injection\n", fname);
				return (-1);
			} else {
				core_parked = 1;
			}
		}

		/*
		 * Split either the L2 or L3 cache based on the command.
		 */
		pn_split_cache(ERR_LEVEL_L2(iocp->ioc_command) ? 0 : 1);

		/*
		 * Unpark the sibling core.
		 */
		if (core_parked == 1) {
			if (memtest_unpark_core(mdatap) != 0) {
				DPRINTF(0, "%s: core unpark failed!\n", fname);
				return (-1);
			}
		}
	}

	return (0);
}

int
memtest_cmp_unquiesce(mdata_t *mdatap)
{
	ioc_t		*iocp = mdatap->m_iocp;
	int		core_parked = 0;
	char		*fname = "memtest_cmp_unquiesce";

	if (CPU_SHARED_CACHE(mdatap->m_cip) && CPU_ISPANTHER(mdatap->m_cip)) {
		if ((memtest_get_sibling(mdatap)) != NULL) {
			if (memtest_park_core(mdatap) != 0) {
				DPRINTF(0, "%s: core park failed! Aborting "
				    "injection\n", fname);
				return (-1);
			} else {
				core_parked = 1;
			}
		}

		/*
		 * Unsplit either the L2 or L3 cache based on the command.
		 */
		pn_unsplit_cache(ERR_LEVEL_L2(iocp->ioc_command) ? 0 : 1);

		/*
		 * Unpark the sibling core.
		 */
		if (core_parked == 1) {
			if (memtest_unpark_core(mdatap) != 0) {
				DPRINTF(0, "%s: core unpark failed!\n", fname);
				return (-1);
			}
		}
	}

	return (0);
}

/*
 * Determine if there is a sibling core and if so return it's cpu struct.
 * This is used for CMP processors such as Panther (UltraSPARC-IV+).
 */
struct cpu *
memtest_get_sibling(mdata_t *mdatap)
{
	struct cpu	*sib_cp = NULL;
	struct pg	*pg;
	pg_cpu_itr_t	i;

	if (CPU_SHARED_CACHE(mdatap->m_cip) &&
	    ((pg = (pg_t *)pghw_find_pg(CPU, PGHW_CHIP)) != NULL)) {

		/*
		 * Iterate over the CPUs on the chip to find the sibling core
		 */
		PG_CPU_ITR_INIT(pg, i);
		while ((sib_cp = pg_cpu_next(&i)) != NULL) {
			if (sib_cp != CPU)
			break;
		}
	}

	return (sib_cp);
}

/*
 * CMP: For certain shared resources such as L2 and L3 caches
 * the sibling core must be parked before error injection
 * in those caches.  Alternatively the caches can be split in
 * order to isolate the actions of each separate core.
 *
 * This routine parks the sibling core of the currently executing
 * thread.
 *
 * Note that parking may require that the sibling was previously
 * offlined otherwise the park will fail due to the cyclic timer
 * code which does a cross call to the parked core, and then times
 * out and panics.
 */
int
memtest_park_core(mdata_t *mdatap)
{
	int		core_id = mdatap->m_cip->c_core_id;
	int 		sibling_cid = SIBLING_CORE_ID(core_id);
	int		park_mask;
	int		i;
	uint64_t	crunning_status;

	DPRINTF(2, "memtest_park_core(): Parking sibling core with "
	    "coreid=%d\n", sibling_cid);

	(void) cmp_park_core(sibling_cid);

	/*
	 * CMP spec says there is a delay but doesn't specify bounds on
	 * the time when the core will be parked.
	 * Lets give it a reasonable time to finish its work and park.
	 */
	park_mask = 0x01 << sibling_cid;
	for (i = 0; i < MAX_PARK_TRIES; i++) {
		/*
		 * fetch status from ASI_CORE_RUNNING_STATUS register
		 * VA=0x58(ASI_CORE_RUNNING_STATUS),
		 * ASI=0x41(ASI_CMP_SHARED)
		 */
		crunning_status = peek_asi64(ASI_CMP_SHARED,
		    ASI_CORE_RUNNING_STATUS);
		/* a 0 in core position indicates its Parked */
		if ((crunning_status & park_mask) == 0)
			break;
	}
	if (i == MAX_PARK_TRIES) { /* Timeout */
		DPRINTF(0, "memtest_park_core(): Timeout: Core Park Fail!\n");
		return (1);
	}

	return (0);
}

/*
 * This routine unparks the sibling core of the currently executing
 * thread.
 */
int
memtest_unpark_core(mdata_t *mdatap)
{
	int		core_id = mdatap->m_cip->c_core_id;
	int 		sibling_cid = SIBLING_CORE_ID(core_id);
	int		park_mask;
	int		i;
	uint64_t	crunning_status;

	DPRINTF(2, "memtest_unpark_core(): UnParking sibling core with "
	    "coreid=%d\n", sibling_cid);

	(void) cmp_unpark_core(sibling_cid);

	/*
	 * CMP spec says there is a delay but doesn't specify bounds on delay
	 * when core will be parked/unparked.
	 * Lets give it a reasonable time to finish its work and park/unpark.
	 * We loop checking on the ASI_CORE_RUNNING_STATUS register.
	 */
	park_mask = 0x01 << sibling_cid;
	for (i = 0; i < MAX_PARK_TRIES; i++) {
		/*
		 * Fetch the status from ASI_CORE_RUNNING_STATUS register
		 *	VA=0x58(ASI_CORE_RUNNING_STATUS)
		 *	ASI=0x41(ASI_CMP_SHARED)
		 */
		crunning_status = peek_asi64(ASI_CMP_SHARED,
		    ASI_CORE_RUNNING_STATUS);

		/*
		 * A set bit in core position indicates it's back up
		 * and running
		 */
		if ((crunning_status & park_mask) > 0)
			break;
	}
	if (i == MAX_PARK_TRIES) { /* Timeout */
		DPRINTF(0, "memtest_unpark_core(): Timeout: "
		    "Core UnPark Failed!\n");
		return (-1);
	}

	return (0);
}
