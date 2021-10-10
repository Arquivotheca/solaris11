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
 * Copyright (c) 2008, 2011, Oracle and/or its affiliates. All rights reserved.
 */

/*
 * Performance Counter Back-End for Intel Sandy Bridge processors supporting
 * Architectural Performance Monitoring.
 */

#include <sys/cpuvar.h>
#include <sys/param.h>
#include <sys/cpc_impl.h>
#include <sys/cpc_pcbe.h>
#include <sys/modctl.h>
#include <sys/inttypes.h>
#include <sys/systm.h>
#include <sys/cmn_err.h>
#include <sys/x86_archext.h>
#include <sys/sdt.h>
#include <sys/archsystm.h>
#include <sys/privregs.h>
#include <sys/ddi.h>
#include <sys/sunddi.h>
#include <sys/cred.h>
#include <sys/policy.h>
#include "pcbe_utils.h"
#include "intel_pcbe_utils.h"
#include "snb_pcbe.h"

static const char *snb_cpuref =
	"See Appendix A of the \"Intel 64 and IA-32 Architectures Software" \
	" Developer's Manual Volume 3B: System Programming Guide, Part 2\"" \
	" Order Number: 253669-038US, April 2011";

static const char *snb_cpuref_get();

/* Support functions for pcbe_list_events */
static char *snb_event_row_cb(int row, uint_t *counters);

static char **snb_gpc_names = NULL;
static void snb_events_init();
static char *snb_gpc_events_name(uint_t pic_num);
static char *snb_list_events(uint_t pic_num);

/* Support functions for pcbe_event_coverage */
static uint64_t snb_gpc_events_coverage(char *event);
static uint64_t snb_event_coverage(char *event);

/* Support functions for pcbe_configure */
static int snb_configure(uint_t pic_num, char *event, uint64_t preset,
    uint32_t flags, uint_t nattrs, kcpc_attr_t *attrs, void **data,
    void *token);
static const snb_pcbe_events_table_t *snb_find_event(char *event);
static const snb_pcbe_events_table_t *snb_find_raw_event(uint8_t raw_event);
static int snb_gpc_configure(uint_t pic_num, char *event, uint64_t preset,
    uint32_t flags, uint_t nattrs, kcpc_attr_t *attrs, void **data);

/* Support functions for pcbe_program */
static void snb_program(void *token);
static uint64_t snb_gpc_program(void *token);

/* Support functions for pcbe_free */
static void snb_free(void *config);

pcbe_ops_t snb_pcbe_ops = {
	PCBE_VER_1,			/* pcbe_ver */
	CPC_CAP_OVERFLOW_INTERRUPT | CPC_CAP_OVERFLOW_PRECISE,	/* pcbe_caps */
	ipcbe_ncounters_get,		/* pcbe_ncounters */
	ipcbe_impl_name_get,		/* pcbe_impl_name */
	snb_cpuref_get,			/* pcbe_cpuref */
	snb_list_events,		/* pcbe_list_events */
	ipcbe_attrs_list,		/* pcbe_list_attrs */
	snb_event_coverage,		/* pcbe_event_coverage */
	ipcbe_overflow_bitmap,		/* pcbe_overflow_bitmap */
	snb_configure,			/* pcbe_configure */
	snb_program,			/* pcbe_program */
	ipcbe_allstop,			/* pcbe_allstop */
	ipcbe_sample,			/* pcbe_sample */
	snb_free			/* pcbe_free */
};


static struct modlpcbe snb_modlpcbe = {
	&mod_pcbeops,
	"Core Performance Counters",
	&snb_pcbe_ops
};

static struct modlinkage snb_modl = {
	MODREV_1,
	&snb_modlpcbe,
};

int
_init(void)
{
	pcbe_init();
	if (ipcbe_init() != 0)
		goto fail;

	ipcbe_attrs_add(IPCBE_ATTR_MSR_OFFCORE);

	return (mod_install(&snb_modl));
fail:
	return (ENOTSUP);
}

int
_fini(void)
{
	return (mod_remove(&snb_modl));
}

int
_info(struct modinfo *mi)
{
	return (mod_info(&snb_modl, mi));
}

/* Part of pcbe_cpuref */
static const char *
snb_cpuref_get()
{
	return (snb_cpuref);
}

/*
 * Callback function to check an event is supported in a specific counter.  Used
 * to keep the common pcbe_utils module from needing to know too much about snb
 * data structures.
 */
static char *
snb_event_row_cb(int row, uint_t *counters)
{
	snb_pcbe_events_table_t *event;

	if (row >= snb_pcbe_events_num)
		return (NULL);

	event = (snb_pcbe_events_table_t *)&snb_gpc_events_tbl[row];

	*counters = (uint_t)event->event_info.supported_counters;
	return (event->event_info.name);
}

static void
snb_events_init() {
	int i, num_gpc = ipcbe_gpc_num_get();

	if (num_gpc == 0)
		return;

	/* Allocate an array for each GPC */
	snb_gpc_names = kmem_alloc(num_gpc * sizeof (char *), KM_SLEEP);

	/* Create a comma separated name list for each supported event */
	for (i = 0; i < num_gpc; i++) {
		snb_gpc_names[i] = ipcbe_create_name_list(snb_event_row_cb, i);
	}
}

static char *
snb_gpc_events_name(uint_t pic_num) {
	if (!snb_gpc_names)
		snb_events_init();

	return (snb_gpc_names[pic_num]);
}

static char *
snb_list_events(uint_t pic_num)
{
	if (ipcbe_is_gpc(pic_num)) {
		return (snb_gpc_events_name(pic_num));
	} else {
		return (ipcbe_ffc_events_name(pic_num));
	}
}

static uint64_t
snb_gpc_events_coverage(char *event)
{
	int i;
	char *name;
	uint64_t bitmap = 0;
	uint64_t gpc_ctrl_mask = ipcbe_gpc_ctrl_mask_get();

	for (i = 0; i < snb_pcbe_events_num; i++) {
		name = snb_gpc_events_tbl[i].event_info.name;
		if (pcbe_name_compare(name, event) != 0)
			continue;

		bitmap |= snb_gpc_events_tbl[i].event_info.supported_counters;
		break;
	}

	return (bitmap & gpc_ctrl_mask);
}

/*
 * See the comments in the pcbe_ffc_events_coverage.
 * part of pcbe_event_coverage
 */
static uint64_t
snb_event_coverage(char *event)
{
	uint64_t bitmap = 0;

	bitmap |= ipcbe_arch_events_coverage(event);
	bitmap |= snb_gpc_events_coverage(event);
	bitmap |= ipcbe_ffc_events_coverage(event);

	return (bitmap);
}


/* Part of pcbe_configure */
/*ARGSUSED*/
static int
snb_configure(uint_t pic_num, char *event, uint64_t preset, uint32_t flags,
    uint_t nattrs, kcpc_attr_t *attrs, void **data, void *token)
{
	/*
	 * If we've been handed an existing configuration, we need only to
	 * preset the counter value.
	 */
	if (*data != NULL) {
		ipcbe_update_configure(preset, data);
		return (0);
	}

	if (pic_num >= ipcbe_ncounters_get())
		return (CPC_INVALID_PICNUM);

	if (ipcbe_is_ffc(pic_num)) {
		return (ipcbe_ffc_configure(pic_num, event, preset, flags,
		    nattrs, attrs, data));
	} else if (ipcbe_is_gpc(pic_num)) {
		return (snb_gpc_configure(pic_num, event, preset, flags,
		    nattrs, attrs, data));
	}

	return (CPC_INVALID_PICNUM);
}

static const snb_pcbe_events_table_t *
snb_find_event(char *event) {
	int i;
	char *names;

	for (i = 0; i < snb_pcbe_events_num; i++) {
		names = snb_gpc_events_tbl[i].event_info.name;
		if (pcbe_name_compare(names, event) == 0)
			return (&snb_gpc_events_tbl[i]);
	}

	return (NULL);
}

static const snb_pcbe_events_table_t *
snb_find_raw_event(uint8_t raw_event) {
	int i;
	uint8_t event_select;

	for (i = 0; i < snb_pcbe_events_num; i++) {
		event_select = snb_gpc_events_tbl[i].event_info.event_select;
		if (event_select == raw_event)
			return (&snb_gpc_events_tbl[i]);
	}

	return (NULL);
}

static int
snb_gpc_configure(uint_t pic_num, char *event, uint64_t preset, uint32_t flags,
    uint_t nattrs, kcpc_attr_t *attrs, void **data)
{
	intel_pcbe_config_t *conf;
	const snb_pcbe_events_table_t *event_code;
	ipcbe_events_table_t *event_info = NULL;
	int err, i;
	char *name;
	uint64_t val;

	event_code = snb_find_event(event);

	/* Check if this is a supported GPC event */
	if (event_code) {
		event_info = (ipcbe_events_table_t *)&event_code->event_info;
		if (!(C(pic_num) & event_info->supported_counters))
			event_info = NULL;
	}

	/* Let the common code do all the leg work */
	err = ipcbe_gpc_configure(pic_num, event, preset, flags, nattrs, attrs,
	    data, event_info);

	if (err != 0)
		goto fail;

	conf = (intel_pcbe_config_t *)*data;

	/* Check for RAW types and whether it requires MSR_OFFCORE */
	if (PCBE_CONF_TYPE(conf) == PCBE_RAW) {
		event_code = snb_find_raw_event(PCBE_CONF_EVENT(conf));
	}

	/* Fill in the missing msr pieces */
	if (event_code && event_code->msr_offset) {
		snb_pcbe_config_t *snb_conf;

		snb_conf = kmem_alloc(sizeof (snb_pcbe_config_t), KM_SLEEP);
		snb_conf->msr_offset = event_code->msr_offset;

		/*
		 * Find the msr value, default to zero.
		 * This will end up having the counter count nothing.
		 */
		snb_conf->msr_value = 0x0;
		for (i = 0; i < nattrs; i++) {
			name = attrs[i].ka_name;
			if (!ATTR_CMP(name, IPCBE_ATTR_MSR_OFFCORE))
				continue;

			val = attrs[i].ka_val;
			/* Masked reserved bits to prevent GP faults */
			snb_conf->msr_value = val & SNB_OFFCORE_RSP_MASK;
			break;
		}

		conf->cookie = snb_conf;
	} else {
		/* If the event does not require MSR_OFFCORE, undo the setup. */
		for (i = 0; i < nattrs; i++) {
			name = attrs[i].ka_name;
			if (ATTR_CMP(name, IPCBE_ATTR_MSR_OFFCORE)) {
				ipcbe_free(conf);
				*data = NULL;
				err = CPC_INVALID_ATTRIBUTE;
				goto fail;
			}
		}


	}

	return (0);
fail:
	return (err);
}

static void
snb_program(void *token)
{
	uint64_t curcr4;
	uint64_t perf_global_ctrl;

	ipcbe_allstop();

	/* Clear any overflow indicators before programming the counters. */
	WRMSR(IA32_PERF_GLOBAL_OVF_CTRL, 0x0);

	curcr4 = getcr4();
	if (kcpc_allow_nonpriv(token)) {
		/* Allow RDPMC at any ring level */
		setcr4(curcr4 | CR4_PCE);
	} else {
		/* Allow RDPMC only at ring 0 */
		setcr4(curcr4 & ~CR4_PCE);
	}

	/* Program the FFC first */
	perf_global_ctrl = ipcbe_ffc_program(token);

	/* Program the GPCs */
	perf_global_ctrl |= snb_gpc_program(token);

	/* Enable the interrupts */
	WRMSR(IA32_PERF_GLOBAL_CTRL, perf_global_ctrl);
}


static uint64_t
snb_gpc_program(void *token)
{
	intel_pcbe_config_t	*conf;
	uint64_t		perf_global_ctrl = 0;
	uint_t			pic_num;

	for (conf = kcpc_next_config(token, NULL, NULL);
	    conf != NULL;
	    conf = kcpc_next_config(token, conf, NULL)) {

		if (!IPCBE_IS_GPC_TYPE(PCBE_CONF_TYPE(conf)))
			continue;

		pic_num = conf->pic_num;

		PCBE_CONF_PRESET(conf) = ipcbe_pmc_write(PCBE_CONF_PRESET(conf),
		    pic_num);

		/* Setup extra MSR values if the event requires it. */
		if (conf->cookie) {
			snb_pcbe_config_t *snb_conf;

			snb_conf = (snb_pcbe_config_t *)conf->cookie;
			WRMSR(snb_conf->msr_offset, snb_conf->msr_value);
		}

		/* Update the counter's control register */
		WRMSR(IA32_PERFEVTSEL(pic_num), PCBE_CONF_GPC_CTRL(conf));
		perf_global_ctrl |= 1ull << pic_num;
	}

	return (perf_global_ctrl);
}

static void
snb_free(void *config)
{
	intel_pcbe_config_t *conf = (intel_pcbe_config_t *)config;

	if (IPCBE_IS_GPC_TYPE(conf->pic_type) && conf->cookie) {
		kmem_free(conf->cookie, sizeof (snb_pcbe_config_t));
	}
	ipcbe_free(config);
}
