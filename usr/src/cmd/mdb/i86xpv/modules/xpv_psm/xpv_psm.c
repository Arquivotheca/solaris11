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
 * Copyright (c) 2007, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#include <mdb/mdb_modapi.h>
#include <mdb/mdb_ks.h>
#include <mdb/mdb_ctf.h>
#include <sys/evtchn_impl.h>
#include <errno.h>
#include <sys/xc_levels.h>

#include "intr_common.h"

static shared_info_t shared_info;
static int have_shared_info;
static uintptr_t evtchn_cpus_addr;
static struct av_head avec_tbl[NR_IRQS];
static irq_info_t irq_tbl[NR_IRQS];
static mec_info_t ipi_tbl[MAXIPL];
static mec_info_t virq_tbl[NR_VIRQS];
static short evtchn_tbl[NR_EVENT_CHANNELS];

static int
update_tables(void)
{
	GElf_Sym sym;
	uintptr_t shared_info_addr;

	if (mdb_readvar(&irq_tbl, "irq_info") == -1) {
		mdb_warn("failed to read irq_info");
		return (0);
	}

	if (mdb_readvar(&ipi_tbl, "ipi_info") == -1) {
		mdb_warn("failed to read ipi_info");
		return (0);
	}

	if (mdb_readvar(&avec_tbl, "autovect") == -1) {
		mdb_warn("failed to read autovect");
		return (0);
	}

	if (mdb_readvar(&irq_tbl, "irq_info") == -1) {
		mdb_warn("failed to read irq_info");
		return (0);
	}

	if (mdb_readvar(&ipi_tbl, "ipi_info") == -1) {
		mdb_warn("failed to read ipi_info");
		return (0);
	}

	if (mdb_readvar(&virq_tbl, "virq_info") == -1) {
		mdb_warn("failed to read virq_info");
		return (0);
	}

	if (mdb_readvar(&evtchn_tbl, "evtchn_to_irq") == -1) {
		mdb_warn("failed to read evtchn_to_irq");
		return (0);
	}

	if (mdb_lookup_by_name("evtchn_cpus", &sym) == -1) {
		mdb_warn("failed to lookup evtchn_cpus");
		return (0);
	}

	evtchn_cpus_addr = sym.st_value;

	if (mdb_readvar(&shared_info_addr, "HYPERVISOR_shared_info") == -1) {
		mdb_warn("failed to read HYPERVISOR_shared_info");
		return (0);
	}

	/*
	 * It's normal for this to fail with a domain dump.
	 */
	if (mdb_ctf_vread(&shared_info, "shared_info_t",
	    shared_info_addr, 0) != -1)
		have_shared_info = 1;

	return (1);
}

static const char *
virq_type(int irq)
{
	int i;

	for (i = 0; i < NR_VIRQS; i++) {
		if (virq_tbl[i].mi_irq == irq)
			break;
	}

	switch (i) {
	case VIRQ_TIMER:
		return ("virq:timer");
	case VIRQ_DEBUG:
		return ("virq:debug");
	default:
		break;
	}

	return ("virq:?");
}

static const char *
irq_type(int irq, int extended)
{
	switch (irq_tbl[irq].ii_type) {
	case IRQT_UNBOUND:
		return ("unset");
	case IRQT_VIRQ:
		if (extended)
			return (virq_type(irq));
		return ("virq");
	case IRQT_IPI:
		return ("ipi");
	case IRQT_EVTCHN:
		return ("evtchn");
	}

	return ("?");
}

/*
 * We need a non-trivial IPL lookup as the CPU poke's IRQ doesn't have ii_ipl
 * set -- see evtchn.h.
 */
static int
irq_ipl(int irq)
{
	int i;

	if (irq_tbl[irq].ii_u2.ipl != 0)
		return (irq_tbl[irq].ii_u2.ipl);

	for (i = 0; i < MAXIPL; i++) {
		if (ipi_tbl[i].mi_irq == irq) {
			return (i);
		}
	}

	return (0);
}

static void
print_cpu(irq_info_t *irqp, int evtchn)
{
	size_t cpuset_size = BT_BITOUL(NCPU) * sizeof (ulong_t);
	int cpu;

	if (irqp != NULL) {
		switch (irqp->ii_type) {
		case IRQT_VIRQ:
		case IRQT_IPI:
			mdb_printf("all ");
			return;
		default:
			break;
		}
	}

	if (evtchn >= NR_EVENT_CHANNELS || evtchn == 0) {
		mdb_printf("-   ");
		return;
	}

	cpu = mdb_cpuset_find(evtchn_cpus_addr +
	    (cpuset_size * evtchn));

	/*
	 * XXPV: we should verify this against the CPU's mask and show
	 * something if they don't match.
	 */
	mdb_printf("%-4d", cpu);
}

static void
print_isr(int i)
{
	if (avec_tbl[i].avh_link != NULL) {
		struct autovec avhp;

		(void) mdb_vread(&avhp, sizeof (struct autovec),
		    (uintptr_t)avec_tbl[i].avh_link);

		interrupt_print_isr((uintptr_t)avhp.av_vector,
		    (uintptr_t)avhp.av_intarg1, (uintptr_t)avhp.av_dip);
	} else if (irq_ipl(i) == XC_CPUPOKE_PIL) {
		mdb_printf("poke_cpu");
	}
}

static int
evtchn_masked(int i)
{
	return (!!TEST_EVTCHN_BIT(i, &shared_info.evtchn_mask[0]));
}

static int
evtchn_pending(int i)
{
	return (!!TEST_EVTCHN_BIT(i, &shared_info.evtchn_pending[0]));
}

static void
print_bus(int irq)
{
	char parent[7];
	uintptr_t dip_addr;
	struct dev_info	dev_info;
	struct autovec avhp;

	bzero(&avhp, sizeof (avhp));

	if (mdb_ctf_vread(&avhp, "struct autovec",
	    (uintptr_t)avec_tbl[irq].avh_link, 0) == -1)
		goto fail;

	dip_addr = (uintptr_t)avhp.av_dip;

	if (dip_addr == NULL)
		goto fail;

	/*
	 * Sigh.  As a result of the perennial confusion of how you do opaque
	 * handles, dev_info_t has a funny old type, which means we can't use
	 * mdb_ctf_vread() here.
	 */

	if (mdb_vread(&dev_info, sizeof (struct dev_info), dip_addr) == -1)
		goto fail;

	dip_addr = (uintptr_t)dev_info.devi_parent;

	if (mdb_vread(&dev_info, sizeof (struct dev_info), dip_addr) == -1)
		goto fail;

	if (mdb_readstr(parent, 7, (uintptr_t)dev_info.devi_node_name) == -1)
		goto fail;

	mdb_printf("%-6s ", parent);
	return;

fail:
	mdb_printf("-      ");
}

static void
ec_interrupt_dump(int i)
{
	irq_info_t *irqp = &irq_tbl[i];
	char evtchn[8];

	if (irqp->ii_type == IRQT_UNBOUND)
		return;

	if (option_flags & INTR_DISPLAY_INTRSTAT) {
		print_cpu(irqp, irqp->ii_u.evtchn);
		print_isr(i);
		mdb_printf("\n");
		return;
	}

	switch (irqp->ii_type) {
	case IRQT_EVTCHN:
	case IRQT_VIRQ:
		if (irqp->ii_u.index == VIRQ_TIMER) {
			strcpy(evtchn, "T");
		} else {
			mdb_snprintf(evtchn, sizeof (evtchn), "%-7d",
			    irqp->ii_u.evtchn);
		}
		break;
	case IRQT_IPI:
		strcpy(evtchn, "I");
		break;
	}

	/* IRQ */
	mdb_printf("%3d  ", i);
	/* Vector */
	mdb_printf("-    ");
	/* Evtchn */
	mdb_printf("%-7s", evtchn);
	/* IPL */
	mdb_printf("%-4d", irq_ipl(i));
	/* Bus */
	print_bus(i);
	/* Trigger */
	mdb_printf("%-4s", "Edg");
	/* Type */
	mdb_printf("%-7s", irq_type(i, 0));
	/* CPU */
	print_cpu(irqp, irqp->ii_u.evtchn);
	/* Share */
	mdb_printf("-     ");
	/* APIC/INT# */
	mdb_printf("-         ");

	print_isr(i);

	mdb_printf("\n");
}

/* ARGSUSED */
static int
interrupts_dump(uintptr_t addr, uint_t flags, int argc, const mdb_arg_t *argv)
{
	int i;

	option_flags = 0;
	if (mdb_getopts(argc, argv,
	    'd', MDB_OPT_SETBITS, INTR_DISPLAY_DRVR_INST, &option_flags,
	    'i', MDB_OPT_SETBITS, INTR_DISPLAY_INTRSTAT, &option_flags,
	    NULL) != argc)
		return (DCMD_USAGE);

	if (!update_tables())
		return (DCMD_ERR);

	if (option_flags & INTR_DISPLAY_INTRSTAT) {
		mdb_printf("%<u>CPU ");
	} else {
		mdb_printf("%<u>IRQ  Vect Evtchn IPL Bus    Trg Type   "
		    "CPU Share APIC/INT# ");
	}
	mdb_printf("%s %</u>\n", option_flags & INTR_DISPLAY_DRVR_INST ?
	    "Driver Name(s)" : "ISR(s)");

	/*
	 * For a domU guest, the interrupt numbering starts at DYNIRQ_BASE,
	 * skip the range reserved for physical irqs
	 */

	for (i = 0; i < NR_IRQS; i++) {
			if (irq_tbl[i].ii_type == IRQT_PIRQ) {
				continue;
			}
		ec_interrupt_dump(i);
	}

	return (DCMD_OK);
}

static void
evtchn_dump(int i)
{
	int irq = evtchn_tbl[i];

	if (irq == INVALID_IRQ) {
		mdb_printf("%-14s%-7d%-4s%-4s", "unassigned", i, "-", "-");
		print_cpu(NULL, i);
		if (have_shared_info) {
			mdb_printf("%-7d", evtchn_masked(i));
			mdb_printf("%-8d", evtchn_pending(i));
		}
		mdb_printf("\n");
		return;
	}

	/* Type */
	mdb_printf("%-14s", irq_type(irq, 1));
	/* Evtchn */
	mdb_printf("%-7d", i);
	/* IRQ */
	mdb_printf("%-4d", irq);
	/* IPL */
	mdb_printf("%-4d", irq_ipl(irq));
	/* CPU */
	print_cpu(NULL, i);
	if (have_shared_info) {
		/* Masked/Pending */
		mdb_printf("%-7d", evtchn_masked(i));
		mdb_printf("%-8d", evtchn_pending(i));
	}
	/* ISR */
	print_isr(irq);

	mdb_printf("\n");
}

/* ARGSUSED */
static int
evtchns_dump(uintptr_t addr, uint_t flags, int argc, const mdb_arg_t *argv)
{
	int i;

	option_flags = 0;
	if (mdb_getopts(argc, argv,
	    'd', MDB_OPT_SETBITS, INTR_DISPLAY_DRVR_INST, &option_flags,
	    NULL) != argc)
		return (DCMD_USAGE);

	if (!update_tables())
		return (DCMD_ERR);

	if (flags & DCMD_ADDRSPEC) {
		/*
		 * Note: we allow the invalid evtchn 0, as it can help catch if
		 * we incorrectly try to configure it.
		 */
		if ((int)addr >= NR_EVENT_CHANNELS) {
			mdb_warn("Invalid event channel %d.\n", (int)addr);
			return (DCMD_ERR);
		}
	}

	mdb_printf("%<u>Type          Evtchn IRQ IPL CPU ");
	if (have_shared_info)
		mdb_printf("Masked Pending ");

	mdb_printf("%s %</u>\n", option_flags & INTR_DISPLAY_DRVR_INST ?
	    "Driver Name(s)" : "ISR(s)");

	if (flags & DCMD_ADDRSPEC) {
		evtchn_dump((int)addr);
		return (DCMD_OK);
	}

	for (i = 0; i < NR_EVENT_CHANNELS; i++) {
		if (evtchn_tbl[i] == INVALID_IRQ)
			continue;

		evtchn_dump(i);
	}

	return (DCMD_OK);
}

static void
evtchns_help(void)
{
	mdb_printf("Print valid event channels\n"
	    "If %<u>addr%</u> is given, interpret it as an evtchn to print "
	    "details of.\n"
	    "By default, only interrupt service routine names are printed.\n\n"
	    "Switches:\n"
	    "  -d   instead of ISR, print <driver_name><instance#>\n");
}

static const mdb_dcmd_t dcmds[] = {
	{ "interrupts", "?[-di]", "print interrupts", interrupts_dump,
	    interrupt_help },
	{ "evtchns", "?[-d]", "print event channels", evtchns_dump,
	    evtchns_help },
	{ "softint", "?[-d]", "print soft interrupts", soft_interrupt_dump,
	    soft_interrupt_help},
	{ NULL }
};

static const mdb_modinfo_t modinfo = { MDB_API_VERSION, dcmds, NULL };

const mdb_modinfo_t *
_mdb_init(void)
{
	GElf_Sym sym;

	if (mdb_lookup_by_name("gld_intr", &sym) != -1)
		if (GELF_ST_TYPE(sym.st_info) == STT_FUNC)
			gld_intr_addr = (uintptr_t)sym.st_value;

	return (&modinfo);
}
