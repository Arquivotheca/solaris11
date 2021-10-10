/*
 * Copyright (c) 2009, 2010, Oracle and/or its affiliates. All rights reserved.
 */

#ifndef _MEMTESTIO_KT_H
#define	_MEMTESTIO_KT_H

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * Rainbow Falls (UltraSPARC-T3 aka KT) specific error definitions.
 */

/*
 * Memory (DRAM) errors.
 *
 * Note that these errors are continued from those defined by Niagara-I
 * and Niagara-II in memtestio_ni.h and memtestio_n2.h.
 *
 * NOTE: these INT defs could use a new MISC or ACCESS type, right now only
 *	 CPU type differentiates these from "normal" memory injection.
 *
 *			 CPU  CLASS SUBCL  TRAP  PROT  MODE   ACCESS MISC
 */
#define	KT_HD_DAUINT	(KT | MEM | DATA | DIS | UE  | HYPR | LOAD | NA0)
#define	KT_HD_DACINT	(KT | MEM | DATA | DIS | CE  | HYPR | LOAD | NA0)

/*
 * Memory (DRAM) "NotData" errors.
 *
 *			 CPU  CLASS SUBCL  TRAP  PROT  MODE   ACCESS MISC
 */
#define	KT_HD_MEMND	(KT | MEM | DATA | PRE | ND  | HYPR | LOAD | NA0)
#define	KT_HD_MEMNDMA	(KT | MEM | DATA | DIS | ND  | HYPR | MAL  | NA0)
#define	KT_HD_MEMNDCWQ	(KT | MEM | DATA | DIS | ND  | HYPR | CWQAC| NA0)
#define	KT_HI_MEMND	(KT | MEM | DATA | PRE | ND  | HYPR | FETC | NA0)

#define	KT_KD_MEMND	(KT | MEM | DATA | PRE | ND  | KERN | LOAD | NA0)
#define	KT_KD_MEMNDDTLB	(KT | MEM | DATA | PRE | ND  | KERN | DTAC | NA0)
#define	KT_KI_MEMNDITLB	(KT | MEM | DATA | PRE | ND  | KERN | ITAC | NA0)
#define	KT_KD_MEMNDTL1	(KT | MEM | DATA | PRE | ND  | KERN | LOAD | TL1)

#define	KT_KI_MEMND	(KT | MEM | DATA | PRE | ND  | KERN | FETC | NA0)
#define	KT_KI_MEMNDTL1	(KT | MEM | DATA | PRE | ND  | KERN | FETC | TL1)
#define	KT_UD_MEMND	(KT | MEM | DATA | PRE | ND  | USER | LOAD | NA0)
#define	KT_UI_MEMND	(KT | MEM | DATA | PRE | ND  | USER | FETC | NA0)

#define	KT_KD_MEMNDSC	(KT | MEM | DATA | PRE | ND  | KERN | SCRB | NA0)
#define	KT_IO_MEMND	(KT | MEM | DATA | DIS | ND  | DMA  | LOAD | NA0)
#define	KT_MNDPHYS	(KT | MEM | DATA | NA4 | ND  | NA2  | NA1  | PHYS)

/*
 * Remote memory (DRAM) "NotData" errors.
 *
 * Note that these errors are continued from those defined by Victoria Falls
 * in memtestio_vf.h.
 *			 CPU   CLASS SUBCL  TRAP  PROT  MODE   ACCESS MISC
 */
#define	KT_HD_MFRND	(KT | MEM | DATA | DIS | ND  | HYPR | LOAD | NA0 | VFR)
#define	KT_HD_MFRNDCWQ	(KT | MEM | DATA | DIS | ND  | HYPR | CWQAC| NA0 | VFR)
#define	KT_HD_MFRNDMA	(KT | MEM | DATA | DIS | ND  | HYPR | MAL  | NA0 | VFR)
#define	KT_KD_MFRND	(KT | MEM | DATA | DIS | ND  | KERN | LOAD | NA0 | VFR)
#define	KT_KD_MFRNDTL1	(KT | MEM | DATA | DIS | ND  | KERN | LOAD | TL1 | VFR)
#define	KT_KI_MFRND	(KT | MEM | DATA | DIS | ND  | KERN | FETC | NA0 | VFR)
#define	KT_KI_MFRNDTL1	(KT | MEM | DATA | DIS | ND  | KERN | FETC | TL1 | VFR)

/*
 * L2 cache data and tag errors.
 *
 * Note that these errors are continued from those defined by Niagara-I
 * and Niagara-II in memtestio_ni.h and memtestio_n2.h.
 *
 *			 CPU  CLASS SUBCL  TRAP  PROT  MODE   ACCESS MISC
 */
#define	KT_HD_LDTF	(KT | L2  | TAG  | FAT | UE  | HYPR | LOAD | NA0)
#define	KT_HI_LDTF	(KT | L2  | TAG  | FAT | UE  | HYPR | FETC | NA0)
#define	KT_KD_LDTF	(KT | L2  | TAG  | FAT | UE  | KERN | LOAD | NA0)
#define	KT_KD_LDTFTL1	(KT | L2  | TAG  | FAT | UE  | KERN | LOAD | TL1)
#define	KT_KI_LDTF	(KT | L2  | TAG  | FAT | UE  | KERN | FETC | NA0)
#define	KT_KI_LDTFTL1	(KT | L2  | TAG  | FAT | UE  | KERN | FETC | TL1)
#define	KT_UD_LDTF	(KT | L2  | TAG  | FAT | UE  | USER | LOAD | NA0)
#define	KT_UI_LDTF	(KT | L2  | TAG  | FAT | UE  | USER | FETC | NA0 | NS)

/*
 * L2 cache data and tag errors injected by address.
 *
 * Note that these errors are defined by Niagara-I and Niagara-II in
 * memtestio_ni.h and memtestio_n2.h.
 *
 *			 CPU   CLASS SUBCL  TRAP  PROT  MODE   ACCESS MISC
 */

/*
 * L2 cache "NotData" errors (including NotData write back).
 *
 * Note that these errors are defined by Niagara-I and Niagara-II in
 * memtestio_ni.h and memtestio_n2.h.
 *
 *			 CPU   CLASS SUBCL  TRAP  PROT  MODE   ACCESS MISC
 */

/*
 * Remote L2 cache "NotData" errors.
 *
 * Note that these errors are continued from those defined by Victoria Falls
 * in memtestio_vf.h.
 *			 CPU  CLASS SUBCL  TRAP  PROT  MODE   ACCESS MISC
 */
#define	KT_HD_L2FRND	(KT | L2  | DATA | DIS | ND  | HYPR | LOAD | NA0 | VFR)
#define	KT_HD_L2FRNDCWQ	(KT | L2  | DATA | DIS | ND  | HYPR | CWQAC| NA0 | VFR)
#define	KT_HD_L2FRNDMA	(KT | L2  | DATA | DIS | ND  | HYPR | MAL  | NA0 | VFR)
#define	KT_HD_L2FRNDPRI	(KT | L2  | DATA | DIS | ND  | HYPR | PRICE| NA0 | VFR)
#define	KT_KD_L2FRND	(KT | L2  | DATA | DIS | ND  | KERN | LOAD | NA0 | VFR)
#define	KT_KD_L2FRNDTL1	(KT | L2  | DATA | DIS | ND  | KERN | LOAD | TL1 | VFR)
#define	KT_KI_L2FRND	(KT | L2  | DATA | DIS | ND  | KERN | FETC | NA0 | VFR)
#define	KT_KI_L2FRNDTL1	(KT | L2  | DATA | DIS | ND  | KERN | FETC | TL1 | VFR)

/*
 * L2 cache V(U)ADS uncorrectable (fatal) and correctable errors.
 *
 * Note that these errors are continued from those defined by Niagara-I
 * and Niagara-II in memtestio_ni.h and memtestio_n2.h.
 *
 *			 CPU  CLASS SUBCL  TRAP  PROT  MODE   ACCESS MISC
 */
#define	KT_HD_LVF_D	(KT | L2  | DIRT | DIS | UE  | HYPR | LOAD | NA0)
#define	KT_HI_LVF_D	(KT | L2  | DIRT | DIS | UE  | HYPR | FETC | NA0)
#define	KT_KD_LVF_D	(KT | L2  | DIRT | DIS | UE  | KERN | LOAD | NA0)
#define	KT_KI_LVF_D	(KT | L2  | DIRT | DIS | UE  | KERN | FETC | NA0)
#define	KT_UD_LVF_D	(KT | L2  | DIRT | DIS | UE  | USER | LOAD | NA0 | NS)
#define	KT_UI_LVF_D	(KT | L2  | DIRT | DIS | UE  | USER | FETC | NA0 | NS)

#define	KT_HD_LVF_S	(KT | L2  | SHRD | DIS | UE  | HYPR | LOAD | NA0)
#define	KT_HI_LVF_S	(KT | L2  | SHRD | DIS | UE  | HYPR | FETC | NA0)
#define	KT_KD_LVF_S	(KT | L2  | SHRD | DIS | UE  | KERN | LOAD | NA0)
#define	KT_KI_LVF_S	(KT | L2  | SHRD | DIS | UE  | KERN | FETC | NA0)
#define	KT_UD_LVF_S	(KT | L2  | SHRD | DIS | UE  | USER | LOAD | NA0 | NS)
#define	KT_UI_LVF_S	(KT | L2  | SHRD | DIS | UE  | USER | FETC | NA0 | NS)

#define	KT_HD_LVC_D	(KT | L2  | DIRT | DIS | CE  | HYPR | LOAD | NA0)
#define	KT_HI_LVC_D	(KT | L2  | DIRT | DIS | CE  | HYPR | FETC | NA0)
#define	KT_KD_LVC_D	(KT | L2  | DIRT | DIS | CE  | KERN | LOAD | NA0)
#define	KT_KI_LVC_D	(KT | L2  | DIRT | DIS | CE  | KERN | FETC | NA0)
#define	KT_UD_LVC_D	(KT | L2  | DIRT | DIS | CE  | USER | LOAD | NA0 | NS)
#define	KT_UI_LVC_D	(KT | L2  | DIRT | DIS | CE  | USER | FETC | NA0 | NS)

#define	KT_HD_LVC_S	(KT | L2  | SHRD | DIS | CE  | HYPR | LOAD | NA0)
#define	KT_HI_LVC_S	(KT | L2  | SHRD | DIS | CE  | HYPR | FETC | NA0)
#define	KT_KD_LVC_S	(KT | L2  | SHRD | DIS | CE  | KERN | LOAD | NA0)
#define	KT_KI_LVC_S	(KT | L2  | SHRD | DIS | CE  | KERN | FETC | NA0)
#define	KT_UD_LVC_S	(KT | L2  | SHRD | DIS | CE  | USER | LOAD | NA0 | NS)
#define	KT_UI_LVC_S	(KT | L2  | SHRD | DIS | CE  | USER | FETC | NA0 | NS)

#define	KT_L2LVCDPHYS	(KT | L2  | DIRT | NA4 | NA3 | NA2  | NA1  | PHYS)
#define	KT_L2LVCSPHYS	(KT | L2  | SHRD | NA4 | NA3 | NA2  | NA1  | PHYS)

/*
 * L2 cache directory, fill, miss, and write buffer errors.
 *
 *			 CPU  CLASS SUBCL  TRAP  PROT  MODE   ACCESS MISC
 */
#define	KT_HD_LDC	(KT | L2  | DIR  | DIS | PE  | HYPR | LOAD | NA0)
#define	KT_HI_LDC	(KT | L2  | DIR  | DIS | PE  | HYPR | FETC | NA0)
#define	KT_KD_LDC	(KT | L2  | DIR  | DIS | PE  | KERN | LOAD | NA0)
#define	KT_KI_LDC	(KT | L2  | DIR  | DIS | PE  | KERN | FETC | NA0 | NS)
#define	KT_UD_LDC	(KT | L2  | DIR  | DIS | PE  | USER | LOAD | NA0 | NS)
#define	KT_UI_LDC	(KT | L2  | DIR  | DIS | PE  | USER | FETC | NA0 | NS)

#define	KT_HD_FBDC	(KT | L2  | FBUF | DIS | CE  | HYPR | LOAD | NA0)
#define	KT_HI_FBDC	(KT | L2  | FBUF | DIS | CE  | HYPR | FETC | NA0)
#define	KT_KD_FBDC	(KT | L2  | FBUF | DIS | CE  | KERN | LOAD | NA0)
#define	KT_KI_FBDC	(KT | L2  | FBUF | DIS | CE  | KERN | FETC | NA0 | NS)
#define	KT_HD_FBDU	(KT | L2  | FBUF | DIS | UE  | HYPR | LOAD | NA0)
#define	KT_HI_FBDU	(KT | L2  | FBUF | DIS | UE  | HYPR | FETC | NA0)
#define	KT_KD_FBDU	(KT | L2  | FBUF | DIS | UE  | KERN | LOAD | NA0)
#define	KT_KI_FBDU	(KT | L2  | FBUF | DIS | UE  | KERN | FETC | NA0 | NS)

#define	KT_HD_MBDU	(KT | L2  | MBUF | DIS | PE  | HYPR | LOAD | NA0)
#define	KT_HI_MBDU	(KT | L2  | MBUF | DIS | PE  | HYPR | FETC | NA0)
#define	KT_KD_MBDU	(KT | L2  | MBUF | DIS | PE  | KERN | LOAD | NA0)
#define	KT_KI_MBDU	(KT | L2  | MBUF | DIS | PE  | KERN | FETC | NA0 | NS)

#define	KT_HD_LDWBC	(KT | L2  | WBUF | DIS | CE  | HYPR | LOAD | NA0)
#define	KT_HI_LDWBC	(KT | L2  | WBUF | DIS | CE  | HYPR | FETC | NA0 | NS)
#define	KT_KD_LDWBC	(KT | L2  | WBUF | DIS | CE  | KERN | LOAD | NA0)
#define	KT_KI_LDWBC	(KT | L2  | WBUF | DIS | CE  | KERN | FETC | NA0 | NS)
#define	KT_HD_LDWBU	(KT | L2  | WBUF | DIS | UE  | HYPR | LOAD | NA0)
#define	KT_HI_LDWBU	(KT | L2  | WBUF | DIS | UE  | HYPR | FETC | NA0 | NS)
#define	KT_KD_LDWBU	(KT | L2  | WBUF | DIS | UE  | KERN | LOAD | NA0)
#define	KT_KI_LDWBU	(KT | L2  | WBUF | DIS | UE  | KERN | FETC | NA0 | NS)

#define	KT_L2DIRPHYS	(KT | L2  | DIR  | NA4 | PE  | NA2  | NA1  | PHYS)
#define	KT_L2FBUFPHYS	(KT | L2  | FBUF | NA4 | CE  | NA2  | NA1  | PHYS)
#define	KT_L2MBUFPHYS	(KT | L2  | MBUF | NA4 | PE  | NA2  | NA1  | PHYS)
#define	KT_L2WBUFPHYS	(KT | L2  | WBUF | NA4 | CE  | NA2  | NA1  | PHYS)

/*
 * L2 cache write back errors.
 *
 * Note that these errors are defined by Niagara-I and Niagara-II in
 * memtestio_ni.h and memtestio_n2.h.
 *			 CPU   CLASS  SUBCL  TRAP  PROT  MODE   ACCESS MISC
 */

/*
 * L1 data cache data and tag errors.
 *
 * Note that these errors are defined by Niagara-I and Niagara-II in
 * memtestio_ni.h and memtestio_n2.h.
 *			 CPU   CLASS SUBCL  TRAP  PROT  MODE   ACCESS MISC
 */

/*
 * L1 instruction cache data and tag errors.
 *
 * Note that these errors are defined by Niagara-I and Niagara-II in
 * memtestio_ni.h and memtestio_n2.h.
 *			 CPU  CLASS SUBCL  TRAP  PROT  MODE   ACCESS MISC
 */

/*
 * Instruction and data TLB data and tag (CAM) errors.
 *
 * Note that these errors are defined by Niagara-II in memtestio_n2.h.
 *			 CPU   CLASS  SUBCL  TRAP  PROT  MODE   ACCESS MISC
 */

/*
 * Integer register file (SPARC Internal) errors.
 *
 * Note that these errors are defined by Niagara-I in memtestio_ni.h.
 *			 CPU   CLASS SUBCL  TRAP  PROT  MODE   ACCESS MISC
 */

/*
 * Floating-point register file (SPARC Internal) errors.
 *
 * Note that these errors are defined by Niagara-I in memtestio_ni.h.
 *			 CPU   CLASS SUBCL  TRAP  PROT  MODE   ACCESS MISC
 */

/*
 * Store Buffer (SPARC Internal) errors.
 *
 * Note that these errors are defined by Niagara-II in memtestio_n2.h.
 *			 CPU   CLASS SUBCL  TRAP  PROT  MODE   ACCESS MISC
 */

/*
 * Internal register array (SPARC Internal) errors.
 *
 * Note that these errors are defined by Niagara-II in memtestio_n2.h.
 *			 CPU   CLASS SUBCL  TRAP  PROT  MODE   ACCESS MISC
 */

/*
 * Modular Arithmetic Unit and Control Word Queue (SPARC Internal) errors.
 *
 * Note that these errors are defined by Niagara-I and Niagara-II in
 * memtestio_ni.h and memtestio_n2.h.
 *			 CPU   CLASS SUBCL  TRAP  PROT  MODE   ACCESS MISC
 */

/*
 * The following definitions are for use with the SOC error types (below),
 * unlike other definitions the SOC errors will use SUBCL values that are
 * specific to the SOC and are not related to the already defined SUBCL
 * values used by other commands.
 */
#define	SOC_SUBCL_INV		(0x00ULL)	/* invalid */
#define	SOC_SUBCL_ECC		(0x01ULL)	/* MCU ECC count errs */
#define	SOC_SUBCL_FBR		(0x02ULL)	/* MCU FBR count errs */
#define	SOC_SUBCL_NIU		(0x03ULL)	/* NIU unit errs */
#define	SOC_SUBCL_SIU		(0x04ULL)	/* SIU unit errs */
#define	SOC_SUBCL_CPUB		(0x05ULL)	/* CPU buffer errs */
#define	SOC_SUBCL_SIUB		(0x06ULL)	/* SIU buffer errs */
#define	SOC_SUBCL_DMUB		(0x07ULL)	/* DMU buffer errs */
#define	SOC_SUBCL_IOB		(0x08ULL)	/* IO buffer errs */


#define	SOC_SUBCLASS_ISECC(x)	(ERR_SUBCLASS(x) == SOC_SUBCL_ECC)
#define	SOC_SUBCLASS_ISFBR(x)	(ERR_SUBCLASS(x) == SOC_SUBCL_FBR)
#define	SOC_SUBCLASS_ISNIU(x)	(ERR_SUBCLASS(x) == SOC_SUBCL_NIU)
#define	SOC_SUBCLASS_ISSIU(x)	(ERR_SUBCLASS(x) == SOC_SUBCL_SIU)

#define	SOC_SUBCLASS_ISCPUB(x)	(ERR_SUBCLASS(x) == SOC_SUBCL_CPUB)
#define	SOC_SUBCLASS_ISSIUB(x)	(ERR_SUBCLASS(x) == SOC_SUBCL_SIUB)
#define	SOC_SUBCLASS_ISDMUB(x)	(ERR_SUBCLASS(x) == SOC_SUBCL_DMUB)
#define	SOC_SUBCLASS_ISIOB(x)	(ERR_SUBCLASS(x) == SOC_SUBCL_IOB)

/*
 * Also define some super-subclasses to make code cleaner.
 */
#define	SOC_SUBCLASS_ISMCU(x)	(SOC_SUBCLASS_ISECC(x) || SOC_SUBCLASS_ISFBR(x))

#define	ECC	(SOC_SUBCL_ECC	<< ERR_SUBCL_SHIFT)
#define	FBR	(SOC_SUBCL_FBR	<< ERR_SUBCL_SHIFT)
#define	NIU	(SOC_SUBCL_NIU	<< ERR_SUBCL_SHIFT)
#define	SIU	(SOC_SUBCL_SIU	<< ERR_SUBCL_SHIFT)

#define	CPUB	(SOC_SUBCL_CPUB	<< ERR_SUBCL_SHIFT)
#define	SIUB	(SOC_SUBCL_SIUB	<< ERR_SUBCL_SHIFT)
#define	DMUB	(SOC_SUBCL_DMUB	<< ERR_SUBCL_SHIFT)
#define	IOB	(SOC_SUBCL_IOB	<< ERR_SUBCL_SHIFT)

/*
 * System on Chip offsets for the SOC error registers, these
 * are used in the command definitions in the file mtst_n2.h.
 */
#define	KT_SOC_NIUDATAPARITY_SHIFT	29
#define	KT_SOC_NIUCTAGUE_SHIFT		28
#define	KT_SOC_NIUCTAGCE_SHIFT		27

#define	KT_SOC_SIU_ERROR_SHIFT		23

#define	KT_SOC_CBD_SHIFT		21
#define	KT_SOC_CBA_SHIFT		20
#define	KT_SOC_CBH_SHIFT		19

#define	KT_SOC_SBD_SHIFT		18
#define	KT_SOC_SBH_SHIFT		17

#define	KT_SOC_DBD_SHIFT		15
#define	KT_SOC_DBA_SHIFT		14
#define	KT_SOC_DBH_SHIFT		13
#define	KT_SOC_IBH_SHIFT		12

/*
 * System on Chip (SOC) MCU errors.
 *
 * Note that these errors are defined by Niagara-II in memtestio_n2.h.
 *			 CPU   CLASS SUBCL  TRAP  PROT  MODE   ACCESS MISC
 */

/*
 * System on Chip (SOC) Internal errors.
 *
 * Note that these errors are continued from those defined by Niagara-II
 * in memtestio_n2.h.
 *			 CPU  CLASS SUBCL  TRAP  PROT  MODE   ACCESS MISC
 */
#define	KT_IO_SIU_ERR	(KT | SOC | SIU  | FAT | PE  | DMA  | LOAD | NA0)

#define	KT_IO_SOC_CBDU	(KT | SOC | CPUB | NA4 | UE  | DMA  | LOAD | NA0)
#define	KT_IO_SOC_CBDC	(KT | SOC | CPUB | DIS | CE  | DMA  | LOAD | NA0)
#define	KT_IO_SOC_CBAP	(KT | SOC | CPUB | DIS | PE  | DMA  | LOAD | NA0)
#define	KT_IO_SOC_CBHP	(KT | SOC | CPUB | FAT | PE  | DMA  | LOAD | NA0)

#define	KT_IO_SOC_SBDU	(KT | SOC | SIUB | NA4 | UE  | DMA  | LOAD | NA0)
#define	KT_IO_SOC_SBDC	(KT | SOC | SIUB | DIS | CE  | DMA  | LOAD | NA0)
#define	KT_IO_SOC_SBHP	(KT | SOC | SIUB | DIS | PE  | DMA  | LOAD | NA0)

#define	KT_IO_SOC_DBDU	(KT | SOC | DMUB | DIS | UE  | DMA  | LOAD | NA0)
#define	KT_IO_SOC_DBDC	(KT | SOC | DMUB | DIS | CE  | DMA  | LOAD | NA0)
#define	KT_IO_SOC_DBAP	(KT | SOC | DMUB | DIS | PE  | DMA  | LOAD | NA0)
#define	KT_IO_SOC_DBHP	(KT | SOC | DMUB | FAT | PE  | DMA  | LOAD | NA0)

#define	KT_IO_SOC_IBHP	(KT | SOC | IOB  | FAT | PE  | DMA  | LOAD | NA0)

/*
 * SSI (BootROM interface) errors.
 *
 * Note that these errors are defined by Niagara-I in memtestio_ni.h.
 *			 CPU  CLASS SUBCL  TRAP  PROT  MODE  ACCESS  MISC
 */

/*
 * DEBUG test case(s) to ensure injector and system are behaving.
 *
 *			 CPU  CLASS  SUBCL  TRAP  PROT  MODE   ACCESS MISC
 */
#define	KT_TEST		(KT | UTIL | NA5  | NA4 | NA3 | NA2  | NA1  | NA0)
#define	KT_PRINT_ESRS	(KT | UTIL | NA5  | NA4 | NA3 | NA2  | LOAD | NA0)
#define	KT_CLEAR_ESRS	(KT | UTIL | NA5  | NA4 | NA3 | NA2  | STOR | NA0)

#ifdef	__cplusplus
}
#endif

#endif	/* _MEMTESTIO_KT_H */
