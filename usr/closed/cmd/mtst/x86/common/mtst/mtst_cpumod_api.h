/*
 * Copyright 2010 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#ifndef _MTST_CPUMOD_API_H
#define	_MTST_CPUMOD_API_H

/*
 * Interfaces used by CPU modules for interaction with mtst
 */

#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

#define	MTST_CMD_F_VERBOSE	0x0001	/* set verbose mode */
#define	MTST_CMD_F_INT18	0x0002	/* force #mc exception after error */
#define	MTST_CMD_F_MERGE	0x0004	/* trigger all errors simultaneously */
#define	MTST_CMD_F_POLLED	0x0008	/* to get an uncorrectable in a poll */
#define	MTST_CMD_F_FORCEMSRWR	0x0010	/* try bare WRMSR if no model support */
#define	MTST_CMD_F_INTERPOSEOK	0x0020	/* may fallback to interposing */
#define	MTST_CMD_F_INTERPOSE	0x0040	/* must interpose */
#define	MTST_CMD_F_INT_CMCI	0x0080	/* force cmci interrupt */

#define	MTST_CMD_OK		0
#define	MTST_CMD_ERR		1
#define	MTST_CMD_USAGE		2

/*
 * Arrays of statements are passed to the memtest driver for error injection.
 * MTST_INJ_STMT_* define the statement type.  These must be kept in sync
 * with the corresponding MEMTEST_INJ_STMT_* in <sys/memtest.h>.
 */
#define	MTST_INJ_STMT_MSR_RD	0x1	/* an MSR to be read */
#define	MTST_INJ_STMT_MSR_WR	0x2	/* an MSR to be written */
#define	MTST_INJ_STMT_PCICFG_RD	0x3	/* PCI config space read */
#define	MTST_INJ_STMT_PCICFG_WR	0x4	/* PCI config space write */
#define	MTST_INJ_STMT_INT	0x5	/* an interrupt to be raised */
#define	MTST_INJ_STMT_POLL	0x6	/* request CPU module poll */

/*
 * Flags for MTST_INJ_STMT_MSR.  These must be kept in sync with the
 * corresponding MEMTEST_INJ_FLAG_* in <sys/memtest.h>.
 */
#define	MTST_MIS_FLAG_MSR_FORCE		0x1	/* May attempt bare WRMSR */
#define	MTST_MIS_FLAG_MSR_INTERPOSEOK	0x2	/* Fallback to interposition */
#define	MTST_MIS_FLAG_MSR_INTERPOSE	0x4	/* Must use interposition */
#define	MTST_MIS_FLAG_ALLCPU		0x8	/* inject in all CPUs */

#define	MTST_MIS_PCIENREG	0x80000000UL
#define	MTST_MIS_PCIBUS_MASK	0x00ff0000UL
#define	MTST_MIS_PCIBUS_SHIFT	16
#define	MTST_MIS_PCIDEV_MASK	0x0000f800UL
#define	MTST_MIS_PCIDEV_SHIFT	11
#define	MTST_MIS_PCIFUNC_MASK	0x00000700UL
#define	MTST_MIS_PCIFUNC_SHIFT	8
#define	MTST_MIS_PCIREG_MASK	0x000000fcUL

#define	MTST_MIS_INT_MASK	0xff

typedef struct mtst_cpuid {
	int32_t mci_hwchipid;		/* 0 */
	int32_t mci_hwcoreid;		/* 4 */
	int32_t mci_hwstrandid;		/* 8 */
	int32_t mci_cpuid;		/* c */
	int32_t mci_hwprocnodeid;	/* 10 */
	int32_t mci_procnodes_per_pkg;	/* 14 */
} mtst_cpuid_t;

/*
 * Statement structure.  Must be kept in sync with memtest_inj_statement in
 * memtest.h, with offsets in this 32-bit userland structure maintained
 * identical to those of a 32 or 64 bit kernel version.  The destination
 * values are 32-bit userland addresses of a suitably sized object to
 * receive the requested data.
 */
typedef struct mtst_inj_stmt {
/* 0 */	mtst_cpuid_t mis_target;		/* target for injection */
/* 18 */uint_t mis_type;			/* MTST_INJ_STMT_* */
/* 1c */uint_t mis_flags;			/* MTST_MIS_FLAG_* */
/* 20 */uint_t mis_pad[2];
/* 28 */union {
	/* 28 */struct {		/* MTST_INJ_STMT_MSR_{RD,WR} */
		/* 28 */uint32_t _mis_msrnum;	/* MSR number */
		/* 2c */uint32_t _mis_msrdest;	/* destination for MSR_RD */
		/* 30 */uint64_t _mis_msrval;	/* value for MSR_WR */
		} _mis_msr;
	/* 28 */struct {		/* MTST_INJ_STMT_PCICFG_{RD,WR} */
		/* 28 */uint32_t _mis_pcibus;	/* Bus */
		/* 2c */uint32_t _mis_pcidev;	/* Device */
		/* 30 */uint32_t _mis_pcifunc;	/* Function */
		/* 34 */uint32_t _mis_pcireg;	/* Offset */
		/* 38 */uint32_t _mis_asz;	/* Access size */
		/* 3c */uint32_t _mis_pcidest;	/* destination for PCI_RD */
		/* 40 */uint32_t _mis_pcival;	/* value for PCI_WR */
		/* 44 */uint32_t _mis_pad;
		} _mis_pci;
	/* 28 */uint8_t _mis_int;	/* MTST_INJ_STMT_INT */
	} _mis_data;
} mtst_inj_stmt_t;	/* sizeof (mtst_inj_stmt_t) is 0x48 */

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

#define	MTST_MIS_ASZ_B		1
#define	MTST_MIS_ASZ_W		2
#define	MTST_MIS_ASZ_L		4

extern void mtst_mis_init_msr_rd(mtst_inj_stmt_t *, mtst_cpuid_t *, uint32_t,
    uint64_t *, uint_t);
extern void mtst_mis_init_msr_wr(mtst_inj_stmt_t *, mtst_cpuid_t *, uint32_t,
    uint64_t, uint_t);
extern void mtst_mis_init_pci_rd(mtst_inj_stmt_t *, uint_t, uint_t,
    uint_t, uint_t, int, uint32_t *, uint_t);
extern void mtst_mis_init_pci_wr(mtst_inj_stmt_t *, uint_t, uint_t, uint_t,
    uint_t, int, uint32_t, uint_t);
extern void mtst_mis_init_int(mtst_inj_stmt_t *, mtst_cpuid_t *, uint_t,
    uint_t);
extern void mtst_mis_init_poll(mtst_inj_stmt_t *, mtst_cpuid_t *, uint_t);

extern int mtst_inject(mtst_inj_stmt_t *, uint_t);

#define	MTST_ARGTYPE_VALUE	0
#define	MTST_ARGTYPE_STRING	1
#define	MTST_ARGTYPE_BOOLEAN	2

typedef struct mtst_argspec {
	const char *mas_argnm;
	uint_t mas_argtype;
	union {
		uint64_t _mas_argval;
		const char *_mas_argstr;
	} _mas_arg;
} mtst_argspec_t;

#define	mas_argval	_mas_arg._mas_argval
#define	mas_argstr	_mas_arg._mas_argstr

/* Describes a single error injection command */
typedef struct mtst_cmd {
	const char *mcmd_cmdname;		/* invocation name */
	const char *mcmd_args;			/* description of arguments */
	int (*mcmd_inject)(mtst_cpuid_t *,	/* function that generates */
	    uint_t, const mtst_argspec_t *,	/* ... and injects required */
	    int, uint64_t);			/* ... injector statements */
	uint64_t mcmd_injarg;			/* last arg to mcmd_inject */
	const char *mcmd_desc;			/* command description */
} mtst_cmd_t;

/* Operations exposed by CPU modules */
typedef struct mtst_cpumod_ops {
	void (*mco_fini)(void);			/* clean up module state */
} mtst_cpumod_ops_t;

/* CPU module meta-data */
#define	MTST_CPUMOD_VERSION	2

typedef struct mtst_cpumod {
	uint_t mcpu_version;			/* MTST_CPUMOD_VERSION */
	const char *mcpu_name;			/* name of CPU module */
	const mtst_cpumod_ops_t *mcpu_ops;	/* module-level operations */
	const mtst_cmd_t *mcpu_cmds;		/* commands exported by mod */
} mtst_cpumod_t;

/*
 * Commands may request the reservation of specific types of memory at specific
 * locations.  The following interfaces allow them to make those requests.
 */
#define	MTST_MEM_RESERVE_USER	0x1		/* Reserve in mtst's addr spc */
#define	MTST_MEM_RESERVE_KERNEL	0x2		/* Reserve kernel memory */

#define	MTST_MEM_ADDR_UNSPEC	(uint64_t)-1

extern int mtst_mem_reserve(uint_t, int *, size_t *, uint64_t *, uint64_t *);
extern int mtst_mem_unreserve(int);

extern void mtst_cmd_warn(const char *, ...);
extern void mtst_cmd_dprintf(const char *, ...);

#ifdef __cplusplus
}
#endif

#endif /* _MTST_CPUMOD_API_H */
