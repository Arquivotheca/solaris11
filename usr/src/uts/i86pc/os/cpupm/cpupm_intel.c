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
 * Copyright (c) 2011, Intel Corporation.
 * All rights reserved.
 */

/*
 * Intel specific CPU power management support.
 */

#include <sys/x86_archext.h>
#include <sys/cpupm_mach.h>
#include <sys/cpu_acpi.h>
#include <sys/speedstep.h>
#include <sys/cpupm_throttle.h>
#include <sys/cpupm_pur.h>
#include <sys/cpu_idle.h>
#include <sys/archsystm.h>

/*
 * The Intel Processor Driver Capabilities (_PDC).
 * See Intel Processor Vendor-Specific ACPI Interface Specification
 * for details.
 */
#define	CPUPM_INTEL_PDC_REVISION	0x1
#define	CPUPM_INTEL_PDC_PS_MSR		0x0001
#define	CPUPM_INTEL_PDC_C1_HALT		0x0002
#define	CPUPM_INTEL_PDC_TS_MSR		0x0004
#define	CPUPM_INTEL_PDC_MP		0x0008
#define	CPUPM_INTEL_PDC_C2C3_MP		0x0010
#define	CPUPM_INTEL_PDC_SW_PSD		0x0020
#define	CPUPM_INTEL_PDC_TSD		0x0080
#define	CPUPM_INTEL_PDC_C1_FFH		0x0100
#define	CPUPM_INTEL_PDC_HW_PSD		0x0800

static uint32_t cpupm_intel_pdccap = 0;

/*
 * MSR for Intel ENERGY_PERF_BIAS feature.
 * The default processor power operation policy is max performance.
 * Power control unit drives to max performance at any energy cost.
 * This MSR is designed to be a power master control knob,
 * it provides 4-bit OS input to the HW for the logical CPU, based on
 * user power-policy preference(scale of 0 to 15). 0 is highest
 * performance, 15 is minimal energy consumption.
 * 7 is a good balance between performance and energy consumption.
 */
#define	IA32_ENERGY_PERF_BIAS_MSR	0x1B0
#define	EPB_MSR_MASK			0xF
#define	EPB_MAX_PERF			0
#define	EPB_BALANCE			7
#define	EPB_MAX_POWER_SAVE		15
/*
 * The value is used to initialize the user power policy preference
 * in IA32_ENERGY_PERF_BIAS_MSR. Variable is used here to allow tuning
 * from the /etc/system file.
 */
uint64_t cpupm_iepb_policy = EPB_MAX_PERF;

static void cpupm_iepb_set_policy(uint64_t power_policy);

/*
 * Intel RAPL (Running Average Power Limit) support definitions
 */
#define	RAPL_PACKAGE_DOMAIN		0x1
#define	RAPL_PP0_DOMAIN			0x2
#define	RAPL_PP1_DOMAIN			0x4
#define	RAPL_DRAM_DOMAIN		0x8

#define	RAPL_NOT_SUPPORT		0x0
#define	RAPL_CLIENT_SUPPORT		RAPL_PACKAGE_DOMAIN | \
					RAPL_PP0_DOMAIN | \
					RAPL_PP1_DOMAIN
#define	RAPL_SERVER_SUPPORT		RAPL_PACKAGE_DOMAIN | \
					RAPL_PP0_DOMAIN | \
					RAPL_DRAM_DOMAIN
/*
 * MSR_RAPL_POWER_UNIT MSR, read-only
 * Units for power, energy and time are exposed in this MSR
 */
#define	MSR_RAPL_POWER_UNIT		0x606

#define	MSR_RAPL_TIME_UNITS_MASK	0x000F0000
#define	MSR_RAPL_TIME_UNITS_SHIFT	16
#define	MSR_RAPL_ENERGY_UNITS_MASK	0x00001F00
#define	MSR_RAPL_ENERGY_UNITS_SHIFT	8
#define	MSR_RAPL_POWER_UNITS_MASK	0x0000000F

/*
 * Energy Status MSRs, read-only. Report the actual
 * energy use for the corresponding domain.
 */
#define	MSR_PKG_ENERGY_STATUS		0x611
#define	MSR_PP0_ENERGY_STATUS		0x639
#define	MSR_PP1_ENERGY_STATUS		0x641
#define	MSR_DRAM_ENERGY_STATUS		0x619

#define	MSR_ENERGY_CONSUMED_MASK	0xFFFFFFFFUL

/*
 * energy unit, millijoules, one thousandth of a joule.
 * Kstat reports energy status in millijoule units.
 */
#define	MILLIJOULE			1000

typedef struct rapl_kstat {
	struct kstat_named	pkg_energy; /* Package Domain energy  */
	struct kstat_named	pp0_energy; /* PP0 domain energy */
	struct kstat_named	pp1_energy; /* PP1 domain energy */
	struct kstat_named	dram_energy; /* DRAM domain energy */
	struct kstat_named	energy_wraparound; /* wraparound value */
} rapl_kstat_t;

typedef struct rapl_power_meter {
	uint32_t		cpu_id;		/* CPU ID */
	kstat_t			*ksp;		/* RAPL Kstat pointer */
	uint_t			feature;	/* RAPL feature support */
	uint32_t		energy_unit;	/* 1/2^ESU in Joules */
} rapl_power_meter_t;

rapl_kstat_t rapl_kstat = {
	{ "pkg_energy",		KSTAT_DATA_UINT64 },
	{ "pp0_energy",		KSTAT_DATA_UINT64 },
	{ "pp1_energy",		KSTAT_DATA_UINT64 },
	{ "dram_energy",	KSTAT_DATA_UINT64 },
	{ "energy_wraparound",	KSTAT_DATA_UINT64 },
};

static kmutex_t rapl_mutex;

static uint_t rapl_supported(uint_t family, uint_t model);
static int rapl_kstat_update(kstat_t *, int);
static uint64_t rapl_read_energy(rapl_power_meter_t *, uint_t);
static uint64_t rapl_energy_wraparound(rapl_power_meter_t *);

boolean_t
cpupm_intel_init(cpu_t *cp)
{
	cpupm_mach_state_t *mach_state =
	    (cpupm_mach_state_t *)(cp->cpu_m.mcpu_pm_mach_state);
	cpupm_intel_t *ms_vendor = (cpupm_intel_t *)mach_state->ms_vendor;
	rapl_power_meter_t *rapl;
	uint_t family;
	uint_t model;
	uint_t rapl_feature;

	if (x86_vendor != X86_VENDOR_Intel)
		return (B_FALSE);

	if (ms_vendor == NULL)
		ms_vendor = mach_state->ms_vendor =
		    kmem_zalloc(sizeof (cpupm_intel_t), KM_SLEEP);

	family = cpuid_getfamily(cp);
	model = cpuid_getmodel(cp);

	cpupm_intel_pdccap = CPUPM_INTEL_PDC_MP;

	/*
	 * If we support SpeedStep on this processor, then set the
	 * correct cma_ops for the processor and enable appropriate
	 * _PDC bits.
	 */
	if (speedstep_supported(family, model)) {
		mach_state->ms_pstate.cma_ops = &speedstep_ops;
		cpupm_intel_pdccap |= CPUPM_INTEL_PDC_PS_MSR |
		    CPUPM_INTEL_PDC_C1_HALT | CPUPM_INTEL_PDC_SW_PSD |
		    CPUPM_INTEL_PDC_HW_PSD;
	} else {
		mach_state->ms_pstate.cma_ops = NULL;
	}

	/*
	 * Set the correct tstate_ops for the processor and
	 * enable appropriate _PDC bits.
	 */
	mach_state->ms_tstate.cma_ops = &cpupm_throttle_ops;
	cpupm_intel_pdccap |= CPUPM_INTEL_PDC_TS_MSR |
	    CPUPM_INTEL_PDC_TSD;

	/*
	 * If we support deep cstates on this processor, then set the
	 * correct cstate_ops for the processor and enable appropriate
	 * _PDC bits.
	 */
	mach_state->ms_cstate.cma_ops = &cpu_idle_ops;
	cpupm_intel_pdccap |= CPUPM_INTEL_PDC_C1_HALT |
	    CPUPM_INTEL_PDC_C2C3_MP | CPUPM_INTEL_PDC_C1_FFH;

	mach_state->ms_pur.cma_ops = &pur_ops;
	/*
	 * _PDC support is optional and the driver should
	 * function even if the _PDC write fails.
	 */
	(void) cpu_acpi_write_pdc(mach_state->ms_acpi_handle,
	    CPUPM_INTEL_PDC_REVISION, 1, &cpupm_intel_pdccap);

	/*
	 * If Intel ENERGY PERFORMANCE BIAS feature is supported,
	 * provides input to the HW, based on user power-policy.
	 */
	if (cpuid_iepb_supported(cp)) {
		cpupm_iepb_set_policy(cpupm_iepb_policy);
	}

	/*
	 * If Intel RAPL (Running Average Power Limit) feature is supported,
	 * load the power meter kstat report
	 */
	if (rapl_feature = rapl_supported(family, model)) {
		/*
		 * Allocate, initialize and install RAPL power meter kstat
		 */
		ms_vendor->rapl = kmem_zalloc(sizeof (rapl_power_meter_t),
		    KM_SLEEP);
		rapl = (rapl_power_meter_t *)ms_vendor->rapl;
		rapl->cpu_id = cp->cpu_id;
		rapl->feature = rapl_feature;
		rapl->energy_unit = (rdmsr(MSR_RAPL_POWER_UNIT) &
		    MSR_RAPL_ENERGY_UNITS_MASK) >> MSR_RAPL_ENERGY_UNITS_SHIFT;
		rapl->ksp = kstat_create("rapl", cp->cpu_id,
		    "power_meter", "misc",
		    KSTAT_TYPE_NAMED,
		    sizeof (rapl_kstat) / sizeof (kstat_named_t),
		    KSTAT_FLAG_VIRTUAL);

		if (rapl->ksp == NULL) {
			cmn_err(CE_NOTE, "!kstat_create(rapl) power meter"
			    " failed");
		} else {
			rapl->ksp->ks_data = &rapl_kstat;
			rapl->ksp->ks_lock = &rapl_mutex;
			rapl->ksp->ks_update = rapl_kstat_update;
			rapl->ksp->ks_data_size += MAXNAMELEN;
			rapl->ksp->ks_private = rapl;
			kstat_install(rapl->ksp);
		}
	}

	return (B_TRUE);
}

void
cpupm_intel_fini(cpu_t *cp)
{
	cpupm_mach_state_t *mach_state =
	    (cpupm_mach_state_t *)cp->cpu_m.mcpu_pm_mach_state;
	cpupm_intel_t *vendor;
	rapl_power_meter_t *rapl;

	if (x86_vendor != X86_VENDOR_Intel)
		return;

	vendor = (cpupm_intel_t *)mach_state->ms_vendor;

	/*
	 * release the vendor requested resource
	 */
	if (vendor != NULL) {
		rapl = (rapl_power_meter_t *)vendor->rapl;
		if (rapl != NULL) {
			if (rapl->ksp != NULL)
				kstat_delete(rapl->ksp);
			kmem_free(rapl, sizeof (rapl_power_meter_t));
			vendor->rapl = NULL;
		}
		kmem_free(vendor, sizeof (cpupm_intel_t));
		mach_state->ms_vendor = NULL;
	}
}

/*
 * ENERGY_PERF_BIAS setting,
 * A hint to HW, based on user power-policy
 */
static void
cpupm_iepb_set_policy(uint64_t iepb_policy)
{
	ulong_t		iflag;
	uint64_t	epb_value;

	epb_value = iepb_policy & EPB_MSR_MASK;

	iflag = intr_clear();
	wrmsr(IA32_ENERGY_PERF_BIAS_MSR, epb_value);
	intr_restore(iflag);
}

/*
 * Check CPUID to see if RAPL (Running Average Power Limit) is suppported
 */
static uint_t
rapl_supported(uint_t family, uint_t model)
{
	/* Required features */
	if (!is_x86_feature(x86_featureset, X86FSET_CPUID) ||
	    !is_x86_feature(x86_featureset, X86FSET_MSR)) {
		return (RAPL_NOT_SUPPORT);
	}

	if (family != 0x6)
		return (RAPL_NOT_SUPPORT);

	switch (model) {
	case 42:	/* SNB */
		return (RAPL_CLIENT_SUPPORT);
	case 45:	/* SNB Xeon */
		return (RAPL_SERVER_SUPPORT);
	default:
		return (RAPL_NOT_SUPPORT);
	}
}

/*
 * RAPL power meter kstat update
 */
static int
rapl_kstat_update(kstat_t *ksp, int flag)
{
	rapl_power_meter_t *rapl = ksp->ks_private;
	rapl_kstat_t *rapl_kstat_p = ksp->ks_data;

	ASSERT(rapl != NULL);
	ASSERT(rapl_kstat_p != NULL);

	if (flag == KSTAT_WRITE) {
		return (EACCES);
	}

	/*
	 * Both client and server platforms support Package
	 * RAPL domain. Here, Kstat reports the actual measured package
	 * energy usage.
	 */
	rapl_kstat_p->pkg_energy.value.ui64 =
	    rapl_read_energy(rapl, MSR_PKG_ENERGY_STATUS);

	/*
	 * Both client and server platforms support PP0 RAPL
	 * domain. Generally, PP0 refers to processor cores. Here, Kstat
	 * reports the measured actual energy usage on the processor core
	 * power plane.
	 */
	rapl_kstat_p->pp0_energy.value.ui64 =
	    rapl_read_energy(rapl, MSR_PP0_ENERGY_STATUS);

	switch (rapl->feature) {
	case RAPL_CLIENT_SUPPORT:
		/*
		 * PP1 RAPL domain is supported only on the client
		 * platform. PP1 RAPL domain refers to the power
		 * plane of a specific device in the uncore.
		 */
		rapl_kstat_p->pp1_energy.value.ui64 =
		    rapl_read_energy(rapl, MSR_PP1_ENERGY_STATUS);
		break;
	case RAPL_SERVER_SUPPORT:
		/*
		 * DRAM RAPL domain is supported only on the server
		 * platform. Here, Kstat reports the measured actual
		 * energy usage of DRAM.
		 */
		rapl_kstat_p->dram_energy.value.ui64 =
		    rapl_read_energy(rapl, MSR_DRAM_ENERGY_STATUS);
		break;
	}

	/*
	 * Expose the energy wraparound value as well
	 */
	rapl_kstat_p->energy_wraparound.value.ui64 =
	    rapl_energy_wraparound(rapl);

	return (0);
}

/*
 * RAPL xcall function to read MSR
 */
static void
rapl_read_xc(uint_t r, uint64_t *value)
{
	ASSERT(value != 0);
	*value = rdmsr(r) & MSR_ENERGY_CONSUMED_MASK;
}

/*
 * RAPL helper function to read energy status
 */
static uint64_t
rapl_read_energy(rapl_power_meter_t *rapl, uint_t r)
{
	uint64_t value;
	uint32_t cpu_id = rapl->cpu_id;
	ulong_t	 iflag;
	cpuset_t set;

	/*
	 * read the total amount of energy consumed
	 */
	iflag = intr_clear();
	if (cpu_id == CPU->cpu_id) {
		value = rdmsr(r) & MSR_ENERGY_CONSUMED_MASK;
		intr_restore(iflag);
	} else {
		intr_restore(iflag);
		CPUSET_ONLY(set, cpu_id);
		xc_call((xc_arg_t)r, (xc_arg_t)&value, NULL,
		    CPUSET2BV(set), (xc_func_t)rapl_read_xc);
	}

	/*
	 * calculate the energy in millijoule units
	 */
	value = (value * MILLIJOULE) >> rapl->energy_unit;

	return (value);
}

/*
 * RAPL helper function to calculate the energy wraparound value
 */
static uint64_t
rapl_energy_wraparound(rapl_power_meter_t *rapl)
{
	uint64_t value;

	/*
	 * return the wraparound value in millijoule units
	 */
	value = (MSR_ENERGY_CONSUMED_MASK * MILLIJOULE);

	return (value >> rapl->energy_unit);
}
