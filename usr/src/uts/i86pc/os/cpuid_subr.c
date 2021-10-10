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
 * Portions Copyright 2009 Advanced Micro Devices, Inc.
 */

/*
 * Support functions that interpret CPUID and similar information.
 * These should not be used from anywhere other than cpuid.c and
 * cmi_hw.c - as such we will not list them in any header file
 * such as x86_archext.h.
 *
 * In cpuid.c we process CPUID information for each cpu_t instance
 * we're presented with, and stash this raw information and material
 * derived from it in per-cpu_t structures.
 *
 * If we are virtualized then the CPUID information derived from CPUID
 * instructions executed in the guest is based on whatever the hypervisor
 * wanted to make things look like, and the cpu_t are not necessarily in 1:1
 * or fixed correspondence with real processor execution resources.  In cmi_hw.c
 * we are interested in the native properties of a processor - for fault
 * management (and potentially other, such as power management) purposes;
 * it will tunnel through to real hardware information, and use the
 * functionality provided in this file to process it.
 */

#include <sys/types.h>
#include <sys/systm.h>
#include <sys/bitmap.h>
#include <sys/x86_archext.h>
#include <sys/pci_cfgspace.h>

/*
 * AMD socket types.
 * First index :
 *		0 for family 0xf, revs B thru E
 *		1 for family 0xf, revs F and G
 *		2 for family 0x10
 *		3 for family 0x11
 * Second index by (model & 0x3) for family 0fh
 * or CPUID bits for later families
 */
static uint32_t amd_skts[4][8] = {
	/*
	 * Family 0xf revisions B through E
	 */
#define	A_SKTS_0			0
	{
		X86_SOCKET_754,		/* 0b000 */
		X86_SOCKET_940,		/* 0b001 */
		X86_SOCKET_754,		/* 0b010 */
		X86_SOCKET_939,		/* 0b011 */
		X86_SOCKET_UNKNOWN,	/* 0b100 */
		X86_SOCKET_UNKNOWN,	/* 0b101 */
		X86_SOCKET_UNKNOWN,	/* 0b110 */
		X86_SOCKET_UNKNOWN	/* 0b111 */
	},
	/*
	 * Family 0xf revisions F and G
	 */
#define	A_SKTS_1			1
	{
		X86_SOCKET_S1g1,	/* 0b000 */
		X86_SOCKET_F1207,	/* 0b001 */
		X86_SOCKET_UNKNOWN,	/* 0b010 */
		X86_SOCKET_AM2,		/* 0b011 */
		X86_SOCKET_UNKNOWN,	/* 0b100 */
		X86_SOCKET_UNKNOWN,	/* 0b101 */
		X86_SOCKET_UNKNOWN,	/* 0b110 */
		X86_SOCKET_UNKNOWN	/* 0b111 */
	},
	/*
	 * Family 0x10
	 */
#define	A_SKTS_2			2
	{
		X86_SOCKET_F1207,	/* 0b000 */
		X86_SOCKET_AM,		/* 0b001 */
		X86_SOCKET_S1g3,	/* 0b010 */
		X86_SOCKET_G34,		/* 0b011 */
		X86_SOCKET_ASB2,	/* 0b100 */
		X86_SOCKET_C32,		/* 0b101 */
		X86_SOCKET_UNKNOWN,	/* 0b110 */
		X86_SOCKET_UNKNOWN	/* 0b111 */
	},

	/*
	 * Family 0x11
	 */
#define	A_SKTS_3			3
	{
		X86_SOCKET_UNKNOWN,	/* 0b000 */
		X86_SOCKET_UNKNOWN,	/* 0b001 */
		X86_SOCKET_S1g2,	/* 0b010 */
		X86_SOCKET_UNKNOWN,	/* 0b011 */
		X86_SOCKET_UNKNOWN,	/* 0b100 */
		X86_SOCKET_UNKNOWN,	/* 0b101 */
		X86_SOCKET_UNKNOWN,	/* 0b110 */
		X86_SOCKET_UNKNOWN	/* 0b111 */
	}
};

struct amd_sktmap_s {
	uint32_t	skt_code;
	char		sktstr[16];
};
static struct amd_sktmap_s amd_sktmap[15] = {
	{ X86_SOCKET_754,	"754" },
	{ X86_SOCKET_939,	"939" },
	{ X86_SOCKET_940,	"940" },
	{ X86_SOCKET_S1g1,	"S1g1" },
	{ X86_SOCKET_AM2,	"AM2" },
	{ X86_SOCKET_F1207,	"F(1207)" },
	{ X86_SOCKET_S1g2,	"S1g2" },
	{ X86_SOCKET_S1g3,	"S1g3" },
	{ X86_SOCKET_AM,	"AM" },
	{ X86_SOCKET_AM2R2,	"AM2r2" },
	{ X86_SOCKET_AM3,	"AM3" },
	{ X86_SOCKET_G34,	"G34" },
	{ X86_SOCKET_ASB2,	"ASB2" },
	{ X86_SOCKET_C32,	"C32" },
	{ X86_SOCKET_UNKNOWN,	"Unknown" }
};

/*
 * Table for mapping AMD Family 0xf and AMD Family 0x10 model/stepping
 * combination to chip "revision" and socket type.
 *
 * The first member of this array that matches a given family, extended model
 * plus model range, and stepping range will be considered a match.
 */
static const struct amd_rev_mapent {
	uint_t rm_family;
	uint_t rm_modello;
	uint_t rm_modelhi;
	uint_t rm_steplo;
	uint_t rm_stephi;
	uint32_t rm_chiprev;
	const char *rm_chiprevstr;
	int rm_sktidx;
} amd_revmap[] = {
	/*
	 * =============== AuthenticAMD Family 0xf ===============
	 */

	/*
	 * Rev B includes model 0x4 stepping 0 and model 0x5 stepping 0 and 1.
	 */
	{ 0xf, 0x04, 0x04, 0x0, 0x0, X86_CHIPREV_AMD_F_REV_B, "B", A_SKTS_0 },
	{ 0xf, 0x05, 0x05, 0x0, 0x1, X86_CHIPREV_AMD_F_REV_B, "B", A_SKTS_0 },
	/*
	 * Rev C0 includes model 0x4 stepping 8 and model 0x5 stepping 8
	 */
	{ 0xf, 0x04, 0x05, 0x8, 0x8, X86_CHIPREV_AMD_F_REV_C0, "C0", A_SKTS_0 },
	/*
	 * Rev CG is the rest of extended model 0x0 - i.e., everything
	 * but the rev B and C0 combinations covered above.
	 */
	{ 0xf, 0x00, 0x0f, 0x0, 0xf, X86_CHIPREV_AMD_F_REV_CG, "CG", A_SKTS_0 },
	/*
	 * Rev D has extended model 0x1.
	 */
	{ 0xf, 0x10, 0x1f, 0x0, 0xf, X86_CHIPREV_AMD_F_REV_D, "D", A_SKTS_0 },
	/*
	 * Rev E has extended model 0x2.
	 * Extended model 0x3 is unused but available to grow into.
	 */
	{ 0xf, 0x20, 0x3f, 0x0, 0xf, X86_CHIPREV_AMD_F_REV_E, "E", A_SKTS_0 },
	/*
	 * Rev F has extended models 0x4 and 0x5.
	 */
	{ 0xf, 0x40, 0x5f, 0x0, 0xf, X86_CHIPREV_AMD_F_REV_F, "F", A_SKTS_1 },
	/*
	 * Rev G has extended model 0x6.
	 */
	{ 0xf, 0x60, 0x6f, 0x0, 0xf, X86_CHIPREV_AMD_F_REV_G, "G", A_SKTS_1 },

	/*
	 * =============== AuthenticAMD Family 0x10 ===============
	 */

	/*
	 * Rev A has model 0 and stepping 0/1/2 for DR-{A0,A1,A2}.
	 * Give all of model 0 stepping range to rev A.
	 */
	{ 0x10, 0x00, 0x00, 0x0, 0x2, X86_CHIPREV_AMD_10_REV_A, "A", A_SKTS_2 },

	/*
	 * Rev B has model 2 and steppings 0/1/0xa/2 for DR-{B0,B1,BA,B2}.
	 * Give all of model 2 stepping range to rev B.
	 */
	{ 0x10, 0x02, 0x02, 0x0, 0xf, X86_CHIPREV_AMD_10_REV_B, "B", A_SKTS_2 },

	/*
	 * Rev C has models 4-6 (depending on L3 cache configuration)
	 * Give all of models 4-6 stepping range to rev C.
	 */
	{ 0x10, 0x04, 0x06, 0x0, 0xf, X86_CHIPREV_AMD_10_REV_C, "C", A_SKTS_2 },

	/*
	 * Rev D has models 8 and 9
	 * Give all of model 8 and 9 stepping range to rev D.
	 */
	{ 0x10, 0x08, 0x09, 0x0, 0xf, X86_CHIPREV_AMD_10_REV_D, "D", A_SKTS_2 },

	/*
	 * =============== AuthenticAMD Family 0x11 ===============
	 */
	{ 0x11, 0x03, 0x3, 0x0, 0xf, X86_CHIPREV_AMD_11, "B", A_SKTS_3 },
};

static void
synth_amd_info(uint_t family, uint_t model, uint_t step,
    uint32_t *skt_p, uint32_t *chiprev_p, const char **chiprevstr_p)
{
	const struct amd_rev_mapent *rmp;
	int found = 0;
	int i;

	if (family < 0xf)
		return;

	for (i = 0, rmp = amd_revmap; i < sizeof (amd_revmap) / sizeof (*rmp);
	    i++, rmp++) {
		if (family == rmp->rm_family &&
		    model >= rmp->rm_modello && model <= rmp->rm_modelhi &&
		    step >= rmp->rm_steplo && step <= rmp->rm_stephi) {
			found = 1;
			break;
		}
	}

	if (!found)
		return;

	if (chiprev_p != NULL)
		*chiprev_p = rmp->rm_chiprev;
	if (chiprevstr_p != NULL)
		*chiprevstr_p = rmp->rm_chiprevstr;

	if (skt_p != NULL) {

#if !defined(__xpv)
		int platform;
		platform = get_hwenv();

		if ((platform == HW_XEN_HVM) || (platform == HW_VMWARE)) {
			*skt_p = X86_SOCKET_UNKNOWN;
		} else if (family == 0xf) {
			*skt_p = amd_skts[rmp->rm_sktidx][model & 0x3];
		} else {
			/*
			 * Starting with family 10h, socket type is stored in
			 * CPUID Fn8000_0001_EBX
			 */
			struct cpuid_regs cp;
			int idx;

			cp.cp_eax = 0x80000001;
			(void) __cpuid_insn(&cp);

			/* PkgType bits */
			idx = BITX(cp.cp_ebx, 31, 28);

			if (idx > 7) {
				/* Reserved bits */
				*skt_p = X86_SOCKET_UNKNOWN;
			} else if (family == 0x10 &&
			    amd_skts[rmp->rm_sktidx][idx] ==
			    X86_SOCKET_AM) {
				/*
				 * Look at Ddr3Mode bit of DRAM Configuration
				 * High Register to decide whether this is
				 * AM2r2 (aka AM2+) or AM3.
				 */
				uint32_t val;

				val = pci_getl_func(0, 24, 2, 0x94);
				if (BITX(val, 8, 8))
					*skt_p = X86_SOCKET_AM3;
				else
					*skt_p = X86_SOCKET_AM2R2;
			} else {
				*skt_p = amd_skts[rmp->rm_sktidx][idx];
			}
		}
#else
		/* PV guest */
		*skt_p = X86_SOCKET_UNKNOWN;
		return;
#endif
	}
}

uint32_t
_cpuid_skt(uint_t vendor, uint_t family, uint_t model, uint_t step)
{
	uint32_t skt = X86_SOCKET_UNKNOWN;

	switch (vendor) {
	case X86_VENDOR_AMD:
		synth_amd_info(family, model, step, &skt, NULL, NULL);
		break;

	default:
		break;

	}

	return (skt);
}

const char *
_cpuid_sktstr(uint_t vendor, uint_t family, uint_t model, uint_t step)
{
	const char *sktstr = "Unknown";
	struct amd_sktmap_s *sktmapp;
	uint32_t skt = X86_SOCKET_UNKNOWN;

	switch (vendor) {
	case X86_VENDOR_AMD:
		synth_amd_info(family, model, step, &skt, NULL, NULL);

		sktmapp = amd_sktmap;
		while (sktmapp->skt_code != X86_SOCKET_UNKNOWN) {
			if (sktmapp->skt_code == skt)
				break;
			sktmapp++;
		}
		sktstr = sktmapp->sktstr;
		break;

	default:
		break;

	}

	return (sktstr);
}

uint32_t
_cpuid_chiprev(uint_t vendor, uint_t family, uint_t model, uint_t step)
{
	uint32_t chiprev = X86_CHIPREV_UNKNOWN;

	switch (vendor) {
	case X86_VENDOR_AMD:
		synth_amd_info(family, model, step, NULL, &chiprev, NULL);
		break;

	default:
		break;

	}

	return (chiprev);
}

const char *
_cpuid_chiprevstr(uint_t vendor, uint_t family, uint_t model, uint_t step)
{
	const char *revstr = "Unknown";

	switch (vendor) {
	case X86_VENDOR_AMD:
		synth_amd_info(family, model, step, NULL, NULL, &revstr);
		break;

	default:
		break;

	}

	return (revstr);

}

/*
 * Map the vendor string to a type code
 */
uint_t
_cpuid_vendorstr_to_vendorcode(char *vendorstr)
{
	if (strcmp(vendorstr, X86_VENDORSTR_Intel) == 0)
		return (X86_VENDOR_Intel);
	else if (strcmp(vendorstr, X86_VENDORSTR_AMD) == 0)
		return (X86_VENDOR_AMD);
	else if (strcmp(vendorstr, X86_VENDORSTR_Centaur) == 0)
		return (X86_VENDOR_Centaur);
	else
		return (X86_VENDOR_IntelClone);
}
