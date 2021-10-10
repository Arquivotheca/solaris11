/*
 * Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#ifndef _MEMTEST_H
#define	_MEMTEST_H

/*
 * Interfaces for the memory error injection driver (memtest).  This driver is
 * intended for use only by mtst.
 */

#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

#define	MEMTEST_DEVICE		"/devices/pseudo/memtest@0:memtest"

#define	MEMTEST_VERSION		1

#define	MEMTESTIOC		('M' << 8)
#define	MEMTESTIOC_INQUIRE	(MEMTESTIOC | 0)
#define	MEMTESTIOC_CONFIG	(MEMTESTIOC | 1)
#define	MEMTESTIOC_INJECT	(MEMTESTIOC | 2)
#define	MEMTESTIOC_MEMREQ	(MEMTESTIOC | 3)
#define	MEMTESTIOC_MEMREL	(MEMTESTIOC | 4)

#define	MEMTEST_F_DEBUG		0x1
#define	MEMTEST_F_DRYRUN	0x2

typedef struct memtest_inq {
	uint_t minq_version;		/* [out] driver version */
} memtest_inq_t;

/*
 * Used by the userland injector to request a memory region from the driver.
 * This region (or a portion thereof) will be used for the error.  The caller
 * is expected to fill in the restrictions, if any, that are to be applied to
 * the region.  If the driver cannot allocate a region that meets the supplied
 * restrictions, the ioctl will fail.  Upon success, all members will be filled
 * in with values that reflect the allocated area.
 */

#define	MEMTEST_MEMREQ_MAXNUM	5	/* maximum number of open allocations */
#define	MEMTEST_MEMREQ_MAXSIZE	8192	/* maximum size of each allocation */

#define	MEMTEST_MEMREQ_UNSPEC	((uint64_t)-1)

typedef struct memtest_memreq {
	int mreq_cpuid;			/* cpu restriction (opt, -1 if unset) */
	uint32_t mreq_size;		/* size of allocation */
	uint64_t mreq_vaddr;		/* [out] VA of allocation */
	uint64_t mreq_paddr;		/* [out] PA of allocation */
} memtest_memreq_t;

/*
 * Arrays of statements are passed to the memtest driver for error injection.
 *
 * These must match the corresponding MTST_INJ_STMT_* definitions in
 * mtst_cpumod_api.h
 */
#define	MEMTEST_INJECT_MAXNUM	20	/* Max # of stmts per INJECT ioctl */

#define	MEMTEST_INJ_STMT_MSR_RD		0x1	/* an MSR to be read */
#define	MEMTEST_INJ_STMT_MSR_WR		0x2	/* an MSR to be written */
#define	MEMTEST_INJ_STMT_PCICFG_RD	0x3	/* PCI config space read */
#define	MEMTEST_INJ_STMT_PCICFG_WR	0x4	/* PCI config space write */
#define	MEMTEST_INJ_STMT_INT		0x5	/* an interrupt to be raised */
#define	MEMTEST_INJ_STMT_POLL		0x6	/* request CPU module poll */

/*
 * Flags for MEMTEST_INJ_STMT_MSR.
 *
 * These must mtach the corresponding MTST_MIS_FLAG_* in mtst_cpumod_api.h.
 */
#define	MEMTEST_INJ_FLAG_MSR_FORCE	0x1
#define	MEMTEST_INJ_FLAG_INTERPOSEOK	0x2
#define	MEMTEST_INJ_FLAG_INTERPOSE	0x4

/*
 * Must be kept in-sync with mtst_cpuid_t in mtst_cpumod_api.h.
 */
typedef struct memtest_cpuid {
/* 0 */		int32_t mci_hwchipid;
/* 4 */		int32_t mci_hwcoreid;
/* 8 */		int32_t mci_hwstrandid;
/* c */		int32_t mci_cpuid;
/* 10 */	int32_t mci_hwprocnodeid;
/* 14 */	int32_t mci_procnodes_per_pkg;
} memtest_cpuid_t;

/*
 * Must be kept in-sync with mtst_inj_statement in mtst_cpumod_api.h.
 * Structure alignment in both 32 bit mtst and 32/64 bit kernel must
 * be identical.  The destination values are 32-bit userland addresses
 * for results.
 */
typedef struct memtest_inj_stmt {
/* 0 */	memtest_cpuid_t mis_target;	/* target for injection */
/* 18 */uint_t mis_type;		/* MEMTEST_INJ_STMT_* */
/* 1c */uint_t mis_flags;		/* MEMTEST_INJ_FLAG_* */
/* 20 */uint_t mis_pad[2];
/* 28 */union {
	/* 28 */struct {		/* MEMTEST_INJ_STMT_MSR */
		/* 28 */uint32_t _mis_msrnum;	/* MSR number */
		/* 2c */uint32_t _mis_msrdest;	/* destination for MSR_RD */
		/* 30 */uint64_t _mis_msrval;	/* value for MSR_WR */
		} _mis_msr;
	/* 28 */struct {		/* MEMTEST_INJ_STMT_PCICFG */
		/* 28 */uint32_t _mis_pcibus;	/* Bus */
		/* 2c */uint32_t _mis_pcidev;	/* Device */
		/* 30 */uint32_t _mis_pcifunc;	/* Function */
		/* 34 */uint32_t _mis_pcireg;	/* Offset */
		/* 38 */uint32_t _mis_asz;	/* Access size */
		/* 3c */uint32_t _mis_pcidest;	/* destination for PCI_RD */
		/* 40 */uint32_t _mis_pcival;	/* value for PCI_WR */
		/* 44 */uint32_t _mis_pad;
		} _mis_pci;
	/* 28 */uint8_t _mis_int;	/* MEMTEST_INJ_STMT_INT; int num */
	} _mis_data;
} memtest_inj_stmt_t;	/* sizeof (memtest_inj_stmt_t) is 0x48 */

#define	mis_msrnum	_mis_data._mis_msr._mis_msrnum
#define	mis_msrdest	_mis_data._mis_msr._mis_msrdest
#define	mis_msrval	_mis_data._mis_msr._mis_msrval
#define	mis_pcibus	_mis_data._mis_pci._mis_pcibus
#define	mis_pcidev	_mis_data._mis_pci._mis_pcidev
#define	mis_pcifunc	_mis_data._mis_pci._mis_pcifunc
#define	mis_pcireg	_mis_data._mis_pci._mis_pcireg
#define	mis_asz		_mis_data._mis_pci._mis_asz
#define	mis_pcidest	_mis_data._mis_pci._mis_pcidest
#define	mis_pcival	_mis_data._mis_pci._mis_pcival
#define	mis_int		_mis_data._mis_int

#define	MEMTEST_INJ_ASZ_B	1
#define	MEMTEST_INJ_ASZ_W	2
#define	MEMTEST_INJ_ASZ_L	4

typedef struct memtest_inject {
	int mi_nstmts;
	uint32_t mi_pad;
	memtest_inj_stmt_t mi_stmts[1];
} memtest_inject_t;

#ifdef __cplusplus
}
#endif

#endif /* _MEMTEST_H */
