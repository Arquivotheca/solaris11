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
 * Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/sysmacros.h>
#include <sys/sunddi.h>
#include <sys/modctl.h>
#include <sys/promif.h>
#include <sys/kobj.h>
#include <sys/machparam.h>
#include <sys/machsystm.h>
#include <sys/fhc.h>
#include <sys/sysctrl.h>
#include <sys/mem_cage.h>

#include <sys/platform_module.h>
#include <sys/errno.h>

/* Let user disable overtemp cpu power off */
int enable_overtemp_cpu_poweroff = 1;

/* Let user disabled cpu power on */
int enable_cpu_poweron = 1;

/* Preallocation of spare tsb's for DR */
uint_t sunfire_tsb_spares = 0;	/* Patchable. */

/* Set the maximum number of boards... for DR */
/* This is used to early for /etc/system, so must be patched in misc/platmod. */
uint_t sunfire_boards = 0;

/* Preferred minimum cage size (expressed in pages)... for DR */
pgcnt_t sunfire_minimum_cage_size = 0;

static int get_platform_slot_info();

static void (*tod_fault_func)(enum tod_fault_type) = NULL;

static kmutex_t callback_lock;

int
set_platform_max_ncpus(void)
{
	int slots;

	slots = get_platform_slot_info();
	/* This shouldn't happen. If it does assume a maximum system. */
	if (slots == 0)
		slots = MAX_BOARDS;

	/* Apply any patched value, only allowing patching down in size. */
	if (sunfire_boards != 0) {
		slots = MIN(slots, sunfire_boards);
		/* Minimum configuration is one CPU board and one I/O board. */
		if (slots < 2)
			slots = 2;
	}

	sunfire_boards = slots;


	/*
	 * Maximum number of CPU boards is the number of slots minus
	 * at least one I/O board.
	 */
	return ((sunfire_boards-1)*2);
}

/*
 * probe for the slot information of the system
 */
static int
get_platform_slot_info()
{
	uchar_t status1, clk_ver;
	int slots = 0;

	status1 = ldbphysio(SYS_STATUS1_PADDR);
	clk_ver = ldbphysio(CLK_VERSION_REG_PADDR);

	/*
	 * calculate the number of slots on this system
	 */
	switch (SYS_TYPE(status1)) {
	case SYS_16_SLOT:
		slots = 16;
		break;

	case SYS_8_SLOT:
		slots = 8;
		break;

	case SYS_4_SLOT:
		if (SYS_TYPE2(clk_ver) == SYS_PLUS_SYSTEM) {
			slots = 5;
		} else {
			slots = 4;
		}
		break;
	}

	return (slots);
}

void
startup_platform(void)
{
	mutex_init(&callback_lock, NULL, MUTEX_DEFAULT, NULL);
}

int
set_platform_tsb_spares()
{
	int spares;

	/*
	 * If the number of spare TSBs has not been patched,
	 * use the maximum number of I/O boards times 2
	 * IOMMUs per board. The maximum number of boards
	 * is the number of slots (sunfire_boards set by the
	 * early call to set_platform_max_ncpus) minus 1,
	 * the minimum number of CPU boards.
	 */
	if ((spares = sunfire_tsb_spares) == 0)
		spares = (sunfire_boards - 1) * 2;
	return (MIN(spares, MAX_UPA));
}

void
set_platform_defaults(void)
{

#ifdef DEBUG
	ce_verbose_memory = 2;
	ce_verbose_other = 2;
#endif

	/* Check the firmware for CPU poweroff support */
	if (prom_test("SUNW,Ultra-Enterprise,cpu-off") != 0) {
		cmn_err(CE_WARN, "Firmware does not support CPU power off");
		enable_overtemp_cpu_poweroff = 0;
	}

	/* Also check site-settable enable flag for power down support */
	if (enable_overtemp_cpu_poweroff == 0)
		cmn_err(CE_WARN, "Automatic CPU shutdown on over-temperature "
		    "disabled");

	/* Check the firmware for CPU poweron support */
	if (prom_test("SUNW,wakeup-cpu") != 0) {
		cmn_err(CE_WARN, "Firmware does not support CPU restart "
		    "from power off");
		enable_cpu_poweron = 0;
	}

	/* Also check site-settable enable flag for power on support */
	if (enable_cpu_poweron == 0)
		cmn_err(CE_WARN, "The ability to restart individual CPUs "
		    "disabled");

	/* Check the firmware for Dynamic Reconfiguration support */
	if (prom_test("SUNW,Ultra-Enterprise,rm-brd") != 0) {
		cmn_err(CE_WARN, "Firmware does not support"
			" Dynamic Reconfiguration");
	}
}

char *sunfire_drivers[] = {
	"ac",
	"sysctrl",
	"environ",
	"simmstat",
	"sram",
};

int must_load_sunfire_modules = 1;

void
load_platform_drivers(void)
{
	int i;
	char *c = NULL;
	char buf[128];

	buf[0] = '\0';

	for (i = 0; i < (sizeof (sunfire_drivers) / sizeof (char *)); i++) {
		if (i_ddi_attach_hw_nodes(sunfire_drivers[i]) != DDI_SUCCESS) {
			(void) strcat(buf, sunfire_drivers[i]);
			c = strcat(buf, ",");
		}
	}

	if (c) {
		c = strrchr(buf, ',');
		*c = '\0';
		cmn_err(must_load_sunfire_modules ? CE_PANIC : CE_WARN,
		    "Cannot load the [%s] system module(s) which "
		    "monitor hardware including temperature, "
		    "power supplies, and fans", buf);
	}
}

int
plat_cpu_poweron(struct cpu *cp)
{
	int (*sunfire_cpu_poweron)(struct cpu *) = NULL;

	sunfire_cpu_poweron =
	    (int (*)(struct cpu *))kobj_getsymvalue("fhc_cpu_poweron", 0);

	if (enable_cpu_poweron == 0 || sunfire_cpu_poweron == NULL)
		return (ENOTSUP);
	else
		return ((sunfire_cpu_poweron)(cp));
}

int
plat_cpu_poweroff(struct cpu *cp)
{
	int (*sunfire_cpu_poweroff)(struct cpu *) = NULL;

	sunfire_cpu_poweroff =
	    (int (*)(struct cpu *))kobj_getsymvalue("fhc_cpu_poweroff", 0);

	if (enable_overtemp_cpu_poweroff == 0 || sunfire_cpu_poweroff == NULL)
		return (ENOTSUP);
	else
		return ((sunfire_cpu_poweroff)(cp));
}

#ifdef DEBUG
pgcnt_t sunfire_cage_size_limit;
#endif

void
set_platform_cage_params(void)
{
	extern pgcnt_t total_pages;
	extern struct memlist *phys_avail;

	if (kernel_cage_enable) {
		pgcnt_t preferred_cage_size;

		preferred_cage_size =
			MAX(sunfire_minimum_cage_size, total_pages / 256);
#ifdef DEBUG
		if (sunfire_cage_size_limit)
			preferred_cage_size = sunfire_cage_size_limit;
#endif
		/*
		 * Note: we are assuming that post has load the
		 * whole show in to the high end of memory. Having
		 * taken this leap, we copy the whole of phys_avail
		 * the glist and arrange for the cage to grow
		 * downward (descending pfns).
		 */
		kcage_range_init(phys_avail, KCAGE_DOWN, preferred_cage_size);
	}

	if (kcage_on)
		cmn_err(CE_NOTE, "!DR Kernel Cage is ENABLED");
	else
		cmn_err(CE_NOTE, "!DR Kernel Cage is DISABLED");
}

/*ARGSUSED*/
void
plat_freelist_process(int mnode)
{
}

/*
 * No platform drivers on this platform
 */
char *platform_module_list[] = {
	(char *)0
};

void
plat_register_tod_fault(void (*func)(enum tod_fault_type tod_bad))
{
	mutex_enter(&callback_lock);
	tod_fault_func = func;
	mutex_exit(&callback_lock);
}

void
plat_tod_fault(enum tod_fault_type tod_bad)
{
	/*
	 * Holding the lock prevents the driver which registered this
	 * from being unloaded while its tod_fault handler is running.
	 */
	mutex_enter(&callback_lock);
	if (tod_fault_func)
		tod_fault_func(tod_bad);
	mutex_exit(&callback_lock);
}
