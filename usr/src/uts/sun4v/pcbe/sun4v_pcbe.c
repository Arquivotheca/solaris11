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

/*
 * sun4v Performance Counter Backend
 */

#include <sys/cpuvar.h>
#include <sys/systm.h>
#include <sys/archsystm.h>
#include <sys/cmn_err.h>
#include <sys/cpc_impl.h>
#include <sys/cpc_pcbe.h>
#include <sys/modctl.h>
#include <sys/machsystm.h>
#include <sys/sdt.h>
#include <sys/niagara2regs.h>
#include <sys/hsvc.h>
#include <sys/hypervisor_api.h>
#include <sys/disp.h>
#include <sys/suspend.h>
#include <sys/sun4v_pcbe.h>

/*LINTLIBRARY*/
static int sun4v_pcbe_init(void);

static int sun4v_pcbe_disable(void);
static int sun4v_pcbe_enable(void);
static const char *sun4v_pcbe_error_decode(int);

static void pcbe_set_suspend_callbacks(void);

static void construct_event_list(void);
static void free_event_list(void);

pcbe_ops_t		sun4v_pcbe_ops;

pcbe_event_t		*pcbe_eventsp;
pcbe_generic_event_t	*pcbe_generic_eventsp;
char			*pcbe_evlist;
size_t			pcbe_evlist_sz;

/*
 * Index to the current initialized plat-specific PCBE support
 */
int			cpu_pcbe_index = -1;

/*
 * Platform-specific function numbers to use when calling
 * the currently registered HV perf counter API.
 */
int			hv_getperf_func_num;
int			hv_setperf_func_num;

static int		sun4v_pcbe_enabled = 0;


extern int		ni2_pcbe_init(void);
extern void		ni2_pcbe_fini(void);
extern pcbe_ops_t	ni2_pcbe_ops;

extern int		vt_pcbe_init(void);
extern void		vt_pcbe_fini(void);
extern pcbe_ops_t	vt_pcbe_ops;


typedef struct _cpu_pcbe {
	int	(*pcbe_init)(void);	/* CPU-specific init function */
	void	(*pcbe_fini)(void);	/* CPU-specific fini function */
	pcbe_ops_t	*pcbe_opsp;
} cpu_pcbe_t;

cpu_pcbe_t	cpu_pcbes[] = {
	{
		vt_pcbe_init,
		vt_pcbe_fini,
		&vt_pcbe_ops
	},
	{
		ni2_pcbe_init,
		ni2_pcbe_fini,
		&ni2_pcbe_ops
	}
};

static int
sun4v_pcbe_init(void)
{
	int	status;

	status = sun4v_pcbe_enable();

	if (status == 0)
		pcbe_set_suspend_callbacks();

	return (status);
}

static int
sun4v_pcbe_enable(void)
{
	int		i;
	int		ncpu_pcbes;
	cpu_pcbe_t	*pcbep;
	int		status;
	extern	uint_t cpc_ncounters;

	/*
	 * Return success if the pcbe is already enabled
	 */
	if (sun4v_pcbe_enabled)
		return (0);

	ncpu_pcbes = sizeof (cpu_pcbes) / sizeof (cpu_pcbe_t);

	/*
	 * Find and initialize the specific PCBE support appropriate
	 * for this platform.
	 */
	for (i = 0; i < ncpu_pcbes; i++) {
		pcbep = &cpu_pcbes[i];
		status = pcbep->pcbe_init();
		if (status == 0) {
			break;	/* Successfully init'd pcbe */
		}
	}

	if (i == ncpu_pcbes) {
		cmn_err(CE_WARN, "sun4v_pcbe_enable: no HV API found");
		return (-1);
	}

	cpu_pcbe_index = i;

	/*
	 * Copy the platform-specific ops structure so that
	 * it will be used by the CPC framework.
	 */
	sun4v_pcbe_ops = *(pcbep->pcbe_opsp);

	construct_event_list();

	cpc_ncounters = sun4v_pcbe_ops.pcbe_ncounters();

	kcpc_enable_all();

	sun4v_pcbe_enabled = 1;

	return (status);
}

static int
sun4v_pcbe_disable(void)
{
	cpu_pcbe_t	*pcbep;
	int		ncpu_pcbes;
	int		status;

	/*
	 * Return success if the pcbe is already disabled.
	 */
	if (!sun4v_pcbe_enabled)
		return (0);

	if (status = kcpc_disable_all()) {
		return (status);
	}

	sun4v_pcbe_enabled = 0;

	ncpu_pcbes = sizeof (cpu_pcbes) / sizeof (cpu_pcbe_t);

	if (cpu_pcbe_index < 0 || cpu_pcbe_index >= ncpu_pcbes) {
		cmn_err(CE_WARN, "sun4v_pcbe_disable: No pcbe enabled.");
		return (-1);
	}

	pcbep = &cpu_pcbes[cpu_pcbe_index];

	pcbep->pcbe_fini();

	free_event_list();

	return (status);
}

static const char *
sun4v_pcbe_error_decode(int error)
{
	switch (error) {
	case EBUSY:
		return ("CPC framework is busy");
	default:
		return (NULL);
	}
}

static void
pcbe_set_suspend_callbacks(void)
{
	extern const char *(*pcbe_suspend_error_decode)(int);
	extern int (*pcbe_suspend_pre_callback)(void);
	extern int (*pcbe_suspend_post_callback)(void);

	pcbe_suspend_error_decode = &sun4v_pcbe_error_decode;
	pcbe_suspend_pre_callback = &sun4v_pcbe_disable;
	pcbe_suspend_post_callback = &sun4v_pcbe_enable;
}



static struct modlpcbe modlpcbe = {
	&mod_pcbeops,
	"SPARC sun4v Performance Counters",
	&sun4v_pcbe_ops
};

static struct modlinkage modl = {
	MODREV_1,
	&modlpcbe,
};

int
_init(void)
{
	if (sun4v_pcbe_init() != 0)
		return (ENOTSUP);
	return (mod_install(&modl));
}

int
_fini(void)
{
	return (mod_remove(&modl));
}

int
_info(struct modinfo *mi)
{
	return (mod_info(&modl, mi));
}

static void
construct_event_list(void)
{
	pcbe_event_t		*evp;
	pcbe_generic_event_t	*gevp;

	/*
	 * Construct event list.
	 *
	 * First pass:  Calculate size needed. We'll need an additional byte
	 *		for the NULL pointer during the last strcat.
	 *
	 * Second pass: Copy strings.
	 */
	for (evp = pcbe_eventsp; evp->name != NULL; evp++)
		pcbe_evlist_sz += strlen(evp->name) + 1;

	for (gevp = pcbe_generic_eventsp; gevp->name != NULL; gevp++)
		pcbe_evlist_sz += strlen(gevp->name) + 1;

	pcbe_evlist = kmem_alloc(pcbe_evlist_sz + 1, KM_SLEEP);
	pcbe_evlist[0] = '\0';

	for (evp = pcbe_eventsp; evp->name != NULL; evp++) {
		(void) strlcat(pcbe_evlist, evp->name, pcbe_evlist_sz);
		(void) strlcat(pcbe_evlist, ",", pcbe_evlist_sz);
	}

	for (gevp = pcbe_generic_eventsp; gevp->name != NULL; gevp++) {
		(void) strlcat(pcbe_evlist, gevp->name, pcbe_evlist_sz);
		(void) strlcat(pcbe_evlist, ",", pcbe_evlist_sz);
	}

	/*
	 * Remove trailing comma.
	 */
	pcbe_evlist[pcbe_evlist_sz - 1] = '\0';
}

static void
free_event_list(void)
{
	kmem_free(pcbe_evlist, pcbe_evlist_sz + 1);
	pcbe_evlist = NULL;
	pcbe_evlist_sz = 0;
}
