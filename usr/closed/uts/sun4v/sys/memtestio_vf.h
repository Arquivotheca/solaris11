/*
 * Copyright (c) 2007, 2010, Oracle and/or its affiliates. All rights reserved.
 */

#ifndef _MEMTESTIO_VF_H
#define	_MEMTESTIO_VF_H

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * UltraSPARC-T2+ (Victoria Falls) specific error definitions.
 *
 * Errors defined below are in addition to those defined for
 * Niagara-I and Niagara-II in memtestio_ni.h amd memtestio_n2.h
 * respectively.
 */
#define	ERR_VF_SHIFT		(ERR_CPU_SHIFT + 4)
#define	ERR_VF_INV		UINT64_C(0x0)	/* invalid */
#define	ERR_VF_NONE		UINT64_C(0x1)	/* nothing */
#define	ERR_VF_FR		UINT64_C(0x2)	/* remote errors */
#define	ERR_VF_MASK		0xf
#define	ERR_VF(x)		(((x) >> ERR_VF_SHIFT) & ERR_VF_MASK)
#define	ERR_VF_ISFR(x)		(ERR_VF(x) == ERR_VF_FR)
#define	VFR			(ERR_VF_FR << ERR_VF_SHIFT)

/*
 * Remote Memory (DRAM) errors
 *                       CPU  CLASS SUBCL  TRAP  PROT  MODE   ACCESS MISC
 */
#define	VF_HD_FDAU	(VF | MEM | DATA | DIS | UE  | HYPR | LOAD | NA0 | VFR)
#define	VF_HD_FDAUCWQ	(VF | MEM | DATA | DIS | UE  | HYPR | CWQAC| NA0 | VFR)
#define	VF_HD_FDAUMA    (VF | MEM | DATA | DIS | UE  | HYPR | MAL  | NA0 | VFR)
#define	VF_KD_FDAU	(VF | MEM | DATA | DIS | UE  | KERN | LOAD | NA0 | VFR)
#define	VF_KD_FDAUTL1	(VF | MEM | DATA | DIS | UE  | KERN | LOAD | TL1 | VFR)
#define	VF_KD_FDAUPR	(VF | MEM | DATA | DIS | UE  | KERN | PFETC| NA0 | VFR)
#define	VF_KI_FDAU	(VF | MEM | DATA | DIS | UE  | KERN | FETC | NA0 | VFR)
#define	VF_KI_FDAUTL1	(VF | MEM | DATA | DIS | UE  | KERN | FETC | TL1 | VFR)
#define	VF_UD_FDAU	(VF | MEM | DATA | DIS | UE  | USER | LOAD | NA0 | VFR)
#define	VF_UI_FDAU	(VF | MEM | DATA | DIS | UE  | USER | FETC | NA0 | VFR)

#define	VF_IO_FDRU	(VF | MEM | DATA | DIS | UE  | UDMA | LOAD | NA0 | VFR)

#define	VF_HD_FDACCWQ	(VF | MEM | DATA | DIS | CE  | HYPR | CWQAC| NA0 | VFR)
#define	VF_HD_FDAC	(VF | MEM | DATA | DIS | CE  | HYPR | LOAD | NA0 | VFR)
#define	VF_HD_FDACMA	(VF | MEM | DATA | DIS | CE  | HYPR | MAL  | NA0 | VFR)
#define	VF_KD_FDAC	(VF | MEM | DATA | DIS | CE  | KERN | LOAD | NA0 | VFR)
#define	VF_KD_FDACTL1	(VF | MEM | DATA | DIS | CE  | KERN | LOAD | TL1 | VFR)
#define	VF_KD_FDACPR	(VF | MEM | DATA | DIS | CE  | KERN | PFETC| NA0 | VFR)
#define	VF_KD_FDACSTORM	(VF | MEM | DATA | DIS | CE  | KERN | LOAD | STORM| VFR)
#define	VF_KI_FDAC	(VF | MEM | DATA | DIS | CE  | KERN | FETC | NA0 | VFR)
#define	VF_KI_FDACTL1	(VF | MEM | DATA | DIS | CE  | KERN | FETC | TL1 | VFR)
#define	VF_UD_FDAC	(VF | MEM | DATA | DIS | CE  | USER | LOAD | NA0 | VFR)
#define	VF_UI_FDAC	(VF | MEM | DATA | DIS | CE  | USER | FETC | NA0 | VFR)

#define	VF_IO_FDRC	(VF | MEM | DATA | DIS | CE  | UDMA | LOAD | NA0 | VFR)

/*
 * L2 cache copy-back errors
 *                       CPU  CLASS SUBCL  TRAP  PROT  MODE   ACCESS MISC
 */
#define	VF_HD_L2CBU	(VF | L2  | DATA | DIS | UE  | HYPR | LOAD | NA0 | VFR)
#define	VF_HI_L2CBU	(VF | L2  | DATA | DIS | UE  | HYPR | FETC | NA0 | VFR \
			| NS)
#define	VF_KD_L2CBU	(VF | L2  | DATA | DIS | UE  | KERN | LOAD | NA0 | VFR)
#define	VF_KI_L2CBU	(VF | L2  | DATA | DIS | UE  | KERN | FETC | NA0 | VFR)
#define	VF_UD_L2CBU	(VF | L2  | DATA | DIS | UE  | USER | LOAD | NA0 | VFR)
#define	VF_UI_L2CBU	(VF | L2  | DATA | DIS | UE  | USER | FETC | NA0 | VFR \
			| NS)
#define	VF_HD_L2CBUMA	(VF | L2  | DATA | DIS | UE  | HYPR | LOAD | NA0 | VFR)
#define	VF_HD_L2CBUCWQ	(VF | L2  | DATA | DIS | UE  | HYPR | LOAD | NA0 | VFR)
#define	VF_KD_L2CBUTL1	(VF | L2  | DATA | DIS | UE  | KERN | LOAD | TL1 | VFR)
#define	VF_KD_L2CBUPR	(VF | L2  | DATA | DIS | UE  | KERN | LOAD | NA0 | VFR)
#define	VF_HD_L2CBUPRI	(VF | L2  | DATA | DIS | UE  | KERN | PRICE| NA0 | VFR)
#define	VF_KI_L2CBUTL1	(VF | L2  | DATA | DIS | UE  | KERN | FETC | TL1 | VFR)

#define	VF_HD_L2CBC	(VF | L2  | DATA | DIS | CE  | HYPR | LOAD | NA0 | VFR)
#define	VF_HI_L2CBC	(VF | L2  | DATA | DIS | CE  | HYPR | FETC | NA0 | VFR \
			| NS)
#define	VF_KD_L2CBC	(VF | L2  | DATA | DIS | CE  | KERN | LOAD | NA0 | VFR)
#define	VF_KI_L2CBC	(VF | L2  | DATA | DIS | CE  | KERN | FETC | NA0 | VFR)
#define	VF_UD_L2CBC	(VF | L2  | DATA | DIS | CE  | USER | LOAD | NA0 | VFR)
#define	VF_UI_L2CBC	(VF | L2  | DATA | DIS | CE  | USER | FETC | NA0 | VFR \
			| NS)

#define	VF_HD_L2CBCMA	(VF | L2  | DATA | DIS | CE  | HYPR | LOAD | NA0 | VFR)
#define	VF_HD_L2CBCCWQ	(VF | L2  | DATA | DIS | CE  | HYPR | LOAD | NA0 | VFR)
#define	VF_KD_L2CBCTL1	(VF | L2  | DATA | DIS | CE  | KERN | LOAD | TL1 | VFR)
#define	VF_KD_L2CBCPR	(VF | L2  | DATA | DIS | CE  | KERN | LOAD | NA0 | VFR)
#define	VF_HD_L2CBCPRI	(VF | L2  | DATA | DIS | CE  | KERN | PRICE| NA0 | VFR)
#define	VF_KI_L2CBCTL1	(VF | L2  | DATA | DIS | CE  | KERN | FETC | TL1 | VFR)

/*
 * L2 cache write back to remote memory uncorrectable errors
 *                       CPU  CLASS  SUBCL  TRAP  PROT  MODE   ACCESS MISC
 */
#define	VF_HD_LWBU	(VF | L2WB | DATA | DIS | UE  | HYPR | LOAD | NA0 | VFR)
#define	VF_HI_LWBU	(VF | L2WB | DATA | DIS | UE  | HYPR | FETC | NA0 | VFR)
#define	VF_KD_LWBU	(VF | L2WB | DATA | DIS | UE  | KERN | LOAD | NA0 | VFR)
#define	VF_KI_LWBU	(VF | L2WB | DATA | DIS | UE  | KERN | FETC | NA0 | VFR)
#define	VF_UD_LWBU	(VF | L2WB | DATA | DIS | UE  | USER | LOAD | NA0 | VFR)
#define	VF_UI_LWBU	(VF | L2WB | DATA | DIS | UE  | USER | FETC | NA0 | NS)

/*
 * FBR due to failover error
 */
#define	VF_HD_MCUFBRF	(VF | MCU | FBR  | DIS | CE  | HYPR | NA1  | NA0)

/*
 * Coherency link protocol errors
 * Note that only VF_CLTO is defined.  There is currently no way to trigger the
 * others.
 */
#define	VF_CLTO		(VF | CL  | NA5  | FAT | NA3 | DMA  | NA1  | NA0)
#define	VF_CLFRACK
#define	VF_CLFSR
#define	VF_CLFDR
#define	VF_CLSNPTYP

/*
 * SUBCL definitions for LFU
 */
#define	LFU_SUBCL_INV		0ULL
#define	LFU_SUBCL_RTF		1ULL	/* retrain fail */
#define	LFU_SUBCL_TTO		2ULL	/* training timeout */
#define	LFU_SUBCL_CTO		3ULL	/* configuration timeout */
#define	LFU_SUBCL_MLF		4ULL	/* multi-lane failure */
#define	LFU_SUBCL_SLF		5ULL	/* single-lane failure */

#define	LFU_SUBCLASS_ISRTF(x)	(ERR_SUBCLASS(x) == LFU_SUBCL_RTF)
#define	LFU_SUBCLASS_ISTTO(x)	(ERR_SUBCLASS(x) == LFU_SUBCL_TTO)
#define	LFU_SUBCLASS_ISCTO(x)	(ERR_SUBCLASS(x) == LFU_SUBCL_CTO)
#define	LFU_SUBCLASS_ISMLF(x)	(ERR_SUBCLASS(x) == LFU_SUBCL_MLF)
#define	LFU_SUBCLASS_ISSLF(x)	(ERR_SUBCLASS(x) == LFU_SUBCL_SLF)

#define	RTF	(LFU_SUBCL_RTF << ERR_SUBCL_SHIFT)
#define	TTO	(LFU_SUBCL_TTO << ERR_SUBCL_SHIFT)
#define	CTO	(LFU_SUBCL_CTO << ERR_SUBCL_SHIFT)
#define	MLF	(LFU_SUBCL_MLF << ERR_SUBCL_SHIFT)
#define	SLF	(LFU_SUBCL_SLF << ERR_SUBCL_SHIFT)

/*
 * Link Framing Unit (LFU) errors
 *                       CPU  CLASS  SUBCL  TRAP  PROT  MODE   ACCESS MISC
 */
#define	VF_LFU_RTF	(VF | LFU  | RTF  | FAT | NA3 | DMA  | NA1  | NA0)
#define	VF_LFU_TTO	(VF | LFU  | TTO  | FAT | NA3 | DMA  | NA1  | NA0)
#define	VF_LFU_CTO	(VF | LFU  | CTO  | FAT | NA3 | DMA  | NA1  | NA0)
#define	VF_LFU_MLF	(VF | LFU  | MLF  | FAT | NA3 | DMA  | NA1  | NA0)
#define	VF_LFU_SLF	(VF | LFU  | SLF  | NA4 | NA3 | DMA  | NA1  | NA0)

/*
 * Non-cache Crossbar (NCX) errors
 *                       CPU  CLASS  SUBCL  TRAP  PROT  MODE   ACCESS MISC
 */
#define	VF_IO_NCXFDRTO	(VF | NCX  | NA5  | FAT | NA3 | DMA  | NA1  | NA0)
#define	VF_IO_NCXFSRTO	(VF | NCX  | NA5  | FAT | NA3 | DMA  | NA1  | NA0)
#define	VF_IO_NCXFRE	(VF | NCX  | NA5  | FAT | NA3 | DMA  | NA1  | NA0 | NS)
#define	VF_IO_NCXFSE	(VF | NCX  | NA5  | FAT | NA3 | DMA  | NA1  | NA0 | NS)
#define	VF_IO_NCXFDE	(VF | NCX  | NA5  | FAT | NA3 | DMA  | NA1  | NA0 | NS)

/*
 * Test case(s) useful for debugging.
 *			 CPU   CLASS  SUBCL  TRAP  PROT  MODE   ACCESS MISC
 */
#define	VF_PRINT_ESRS	(VF  | UTIL | NA5  | NA4 | NA3 | NA2  | LOAD | NA0)
#define	VF_CLEAR_ESRS	(VF  | UTIL | NA5  | NA4 | NA3 | NA2  | STOR | NA0)

#define	VF_SET_STEER	(VF  | UTIL | NA5  | NA4 | NA3 | NA2  | ASI  | NA0)

#ifdef	__cplusplus
}
#endif

#endif	/* _MEMTESTIO_VF_H */
