/*
 * Copyright (c) 2005, 2010, Oracle and/or its affiliates. All rights reserved.
 */

#ifndef _MEMTESTIO_NI_H
#define	_MEMTESTIO_NI_H

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * UltraSPARC-T1 (Niagara) specific error definitions.
 */

/*
 * Memory (DRAM) errors.
 *			 CPU   CLASS SUBCL  TRAP  PROT  MODE   ACCESS MISC
 */
#define	NI_HD_DAU	(NI1 | MEM | DATA | DIS | UE  | HYPR | LOAD | NA0)
#define	NI_HI_DAU	(NI1 | MEM | DATA | DIS | UE  | HYPR | FETC | NA0)
#define	NI_KD_DAU	(NI1 | MEM | DATA | DIS | UE  | KERN | LOAD | NA0)
#define	NI_HD_DAUMA	(NI1 | MEM | DATA | DIS | UE  | HYPR | MAL  | NA0)
#define	NI_KD_DAUTL1	(NI1 | MEM | DATA | DIS | UE  | KERN | LOAD | TL1)
#define	NI_KD_DAUPR	(NI1 | MEM | DATA | DIS | UE  | KERN | PFETC| NA0)
#define	NI_KI_DAU	(NI1 | MEM | DATA | DIS | UE  | KERN | FETC | NA0)
#define	NI_KI_DAUTL1	(NI1 | MEM | DATA | DIS | UE  | KERN | FETC | TL1)
#define	NI_UD_DAU	(NI1 | MEM | DATA | DIS | UE  | USER | LOAD | NA0)
#define	NI_UI_DAU	(NI1 | MEM | DATA | DIS | UE  | USER | FETC | NA0)

#define	NI_KD_DSU	(NI1 | MEM | DATA | DIS | UE  | KERN | SCRB | NA0)
#define	NI_KD_DBU	(NI1 | MEM | DATA | DIS | UE  | NA2  | LOAD | PHYS)
#define	NI_IO_DRU	(NI1 | MEM | DATA | DIS | UE  | DMA  | LOAD | NA0)

#define	NI_HD_DAC	(NI1 | MEM | DATA | DIS | CE  | HYPR | LOAD | NA0)
#define	NI_HI_DAC	(NI1 | MEM | DATA | DIS | CE  | HYPR | FETC | NA0)
#define	NI_KD_DAC	(NI1 | MEM | DATA | DIS | CE  | KERN | LOAD | NA0)
#define	NI_HD_DACMA	(NI1 | MEM | DATA | DIS | CE  | HYPR | MAL  | NA0)
#define	NI_KD_DACTL1	(NI1 | MEM | DATA | DIS | CE  | KERN | LOAD | TL1)
#define	NI_KD_DACPR	(NI1 | MEM | DATA | DIS | CE  | KERN | PFETC| NA0)
#define	NI_KD_DACSTORM	(NI1 | MEM | DATA | DIS | CE  | KERN | LOAD | STORM)
#define	NI_KI_DAC	(NI1 | MEM | DATA | DIS | CE  | KERN | FETC | NA0)
#define	NI_KI_DACTL1	(NI1 | MEM | DATA | DIS | CE  | KERN | FETC | TL1)
#define	NI_UD_DAC	(NI1 | MEM | DATA | DIS | CE  | USER | LOAD | NA0)
#define	NI_UI_DAC	(NI1 | MEM | DATA | DIS | CE  | USER | FETC | NA0)

#define	NI_KD_DSC	(NI1 | MEM | DATA | DIS | CE  | KERN | SCRB | NA0)
#define	NI_IO_DRC	(NI1 | MEM | DATA | DIS | CE  | DMA  | LOAD | NA0)

/*
 * L2 cache data and tag errors.
 *			 CPU   CLASS SUBCL  TRAP  PROT  MODE   ACCESS MISC
 */
#define	NI_HD_LDAU	(NI1 | L2  | DATA | DIS | UE  | HYPR | LOAD | NA0)
#define	NI_HI_LDAU	(NI1 | L2  | DATA | DIS | UE  | HYPR | FETC | NA0)
#define	NI_KD_LDAU	(NI1 | L2  | DATA | DIS | UE  | KERN | LOAD | NA0)
#define	NI_LDAUCOPYIN	(NI1 | L2  | DATA | DIS | UE  | USER | LOAD | CPIN)
#define	NI_HD_LDAUMA	(NI1 | L2  | DATA | DIS | UE  | HYPR | MAL  | NA0)
#define	NI_KD_LDAUTL1	(NI1 | L2  | DATA | DIS | UE  | KERN | LOAD | TL1)
#define	NI_KD_LDAUPR	(NI1 | L2  | DATA | DIS | UE  | KERN | PFETC| NA0)
#define	NI_OBP_LDAU	(NI1 | L2  | DATA | DIS | UE  | OBP  | LOAD | NA0)
#define	NI_KI_LDAU	(NI1 | L2  | DATA | PRE | UE  | KERN | FETC | NA0)
#define	NI_KI_LDAUTL1	(NI1 | L2  | DATA | PRE | UE  | KERN | FETC | TL1)
#define	NI_UD_LDAU	(NI1 | L2  | DATA | DIS | UE  | USER | LOAD | NA0)
#define	NI_UI_LDAU	(NI1 | L2  | DATA | PRE | UE  | USER | FETC | NA0 | NS)

#define	NI_KD_LDSU	(NI1 | L2  | DATA | DIS | UE  | KERN | SCRB | NA0)
#define	NI_IO_LDRU	(NI1 | L2  | DATA | DIS | UE  | DMA  | LOAD | NA0)

#define	NI_HD_LDTU	(NI1 | L2  | TAG  | DIS | UE  | HYPR | LOAD | NA0 | NS)
#define	NI_KD_LDTU	(NI1 | L2  | TAG  | DIS | UE  | KERN | LOAD | NA0 | NS)
#define	NI_KI_LDTU	(NI1 | L2  | TAG  | DIS | UE  | KERN | FETC | NA0 | NS)
#define	NI_UD_LDTU	(NI1 | L2  | TAG  | DIS | UE  | USER | LOAD | NA0 | NS)
#define	NI_UI_LDTU	(NI1 | L2  | TAG  | DIS | UE  | USER | FETC | NA0 | NS)

#define	NI_HD_LDAC	(NI1 | L2  | DATA | DIS | CE  | HYPR | LOAD | NA0)
#define	NI_HI_LDAC	(NI1 | L2  | DATA | DIS | CE  | HYPR | FETC | NA0)
#define	NI_KD_LDAC	(NI1 | L2  | DATA | DIS | CE  | KERN | LOAD | NA0)
#define	NI_LDACCOPYIN	(NI1 | L2  | DATA | DIS | CE  | USER | LOAD | CPIN)
#define	NI_HD_LDACMA	(NI1 | L2  | DATA | DIS | CE  | HYPR | MAL  | NA0)
#define	NI_KD_LDACTL1	(NI1 | L2  | DATA | DIS | CE  | KERN | LOAD | TL1)
#define	NI_KD_LDACPR	(NI1 | L2  | DATA | DIS | CE  | KERN | PFETC| NA0)
#define	NI_OBP_LDAC	(NI1 | L2  | DATA | DIS | CE  | OBP  | LOAD | NA0)
#define	NI_KI_LDAC	(NI1 | L2  | DATA | DIS | CE  | KERN | FETC | NA0)
#define	NI_KI_LDACTL1	(NI1 | L2  | DATA | DIS | CE  | KERN | FETC | TL1)
#define	NI_UD_LDAC	(NI1 | L2  | DATA | DIS | CE  | USER | LOAD | NA0)
#define	NI_UI_LDAC	(NI1 | L2  | DATA | DIS | CE  | USER | FETC | NA0 | NS)

#define	NI_KD_LDSC	(NI1 | L2  | DATA | DIS | CE  | KERN | SCRB | NA0)
#define	NI_IO_LDRC	(NI1 | L2  | DATA | DIS | CE  | DMA  | LOAD | NA0)

#define	NI_HD_LDTC	(NI1 | L2  | TAG  | DIS | CE  | HYPR | LOAD | NA0)
#define	NI_HI_LDTC	(NI1 | L2  | TAG  | DIS | CE  | HYPR | FETC | NA0)
#define	NI_KD_LDTC	(NI1 | L2  | TAG  | DIS | CE  | KERN | LOAD | NA0)
#define	NI_KD_LDTCTL1	(NI1 | L2  | TAG  | DIS | CE  | KERN | LOAD | TL1)
#define	NI_KI_LDTC	(NI1 | L2  | TAG  | DIS | CE  | KERN | FETC | NA0)
#define	NI_KI_LDTCTL1	(NI1 | L2  | TAG  | DIS | CE  | KERN | FETC | TL1)
#define	NI_UD_LDTC	(NI1 | L2  | TAG  | DIS | CE  | USER | LOAD | NA0)
#define	NI_UI_LDTC	(NI1 | L2  | TAG  | DIS | CE  | USER | FETC | NA0 | NS)

/*
 * L2 cache data and tag errors injected by address.
 *			 CPU   CLASS SUBCL  TRAP  PROT  MODE   ACCESS MISC
 */
#define	NI_L2PHYS	(NI1 | L2  | DATA | NA4 | NA3 | NA2  | NA1  | PHYS)
#define	NI_K_L2VIRT	(NI1 | L2  | DATA | NA4 | NA3 | NA2  | NA1  | VIRT)
#define	NI_U_L2VIRT	(NI1 | L2  | DATA | NA4 | NA3 | USER | NA1  | VIRT)
#define	NI_L2TPHYS	(NI1 | L2  | TAG  | NA4 | NA3 | NA2  | NA1  | PHYS)
#define	NI_L2SCRUBPHYS	(NI1 | L2  | DATA | NA4 | NA3 | NA2  | SCRB | PHYS)
#define	NI_K_L2TVIRT	(NI1 | L2  | TAG  | NA4 | NA3 | NA2  | NA1  | VIRT | NS)
#define	NI_U_L2TVIRT	(NI1 | L2  | TAG  | NA4 | NA3 | USER | NA1  | VIRT | NS)

/*
 * L2 cache write back errors.
 *			 CPU   CLASS  SUBCL  TRAP  PROT  MODE   ACCESS MISC
 */
#define	NI_HD_LDWU	(NI1 | L2WB | DATA | DIS | UE  | HYPR | LOAD | NA0)
#define	NI_HI_LDWU	(NI1 | L2WB | DATA | DIS | UE  | HYPR | FETC | NA0)
#define	NI_KD_LDWU	(NI1 | L2WB | DATA | DIS | UE  | KERN | LOAD | NA0)
#define	NI_KI_LDWU	(NI1 | L2WB | DATA | DIS | UE  | KERN | FETC | NA0)
#define	NI_UD_LDWU	(NI1 | L2WB | DATA | DIS | UE  | USER | LOAD | NA0)
#define	NI_UI_LDWU	(NI1 | L2WB | DATA | DIS | UE  | USER | FETC | NA0 | NS)

#define	NI_HD_LDWC	(NI1 | L2WB | DATA | DIS | CE  | HYPR | LOAD | NA0)
#define	NI_HI_LDWC	(NI1 | L2WB | DATA | DIS | CE  | HYPR | FETC | NA0)
#define	NI_KD_LDWC	(NI1 | L2WB | DATA | DIS | CE  | KERN | LOAD | NA0)
#define	NI_KI_LDWC	(NI1 | L2WB | DATA | DIS | CE  | KERN | FETC | NA0)
#define	NI_UD_LDWC	(NI1 | L2WB | DATA | DIS | CE  | USER | LOAD | NA0)
#define	NI_UI_LDWC	(NI1 | L2WB | DATA | DIS | CE  | USER | FETC | NA0 | NS)

/*
 * L2 cache V(U)AD errors.
 *			 CPU   CLASS SUBCL  TRAP  PROT  MODE   ACCESS MISC
 */
#define	NI_HD_LVU_VD	(NI1 | L2  | VD   | FAT | PE  | HYPR | LOAD | NA0)
#define	NI_HI_LVU_VD	(NI1 | L2  | VD   | FAT | PE  | HYPR | FETC | NA0)
#define	NI_KD_LVU_VD	(NI1 | L2  | VD   | FAT | PE  | KERN | LOAD | NA0)
#define	NI_KI_LVU_VD	(NI1 | L2  | VD   | FAT | PE  | KERN | FETC | NA0)
#define	NI_UD_LVU_VD	(NI1 | L2  | VD   | FAT | PE  | USER | LOAD | NA0)
#define	NI_UI_LVU_VD	(NI1 | L2  | VD   | FAT | PE  | USER | FETC | NA0)

#define	NI_HD_LVU_UA	(NI1 | L2  | UA   | FAT | PE  | HYPR | LOAD | NA0)
#define	NI_HI_LVU_UA	(NI1 | L2  | UA   | FAT | PE  | HYPR | FETC | NA0)
#define	NI_KD_LVU_UA	(NI1 | L2  | UA   | FAT | PE  | KERN | LOAD | NA0)
#define	NI_KI_LVU_UA	(NI1 | L2  | UA   | FAT | PE  | KERN | FETC | NA0)
#define	NI_UD_LVU_UA	(NI1 | L2  | UA	  | FAT | PE  | USER | LOAD | NA0)
#define	NI_UI_LVU_UA	(NI1 | L2  | UA   | FAT | PE  | USER | FETC | NA0)

#define	NI_L2VDPHYS	(NI1 | L2  | VD   | NA4 | NA3 | NA2  | NA1  | PHYS)
#define	NI_L2UAPHYS	(NI1 | L2  | UA   | NA4 | NA3 | NA2  | NA1  | PHYS)

/*
 * L2 cache directory errors.
 *			 CPU   CLASS SUBCL  TRAP  PROT  MODE   ACCESS MISC
 */
#define	NI_KD_LRU	(NI1 | L2  | DATA | FAT | PE  | KERN | LOAD | NA0)
#define	NI_KI_LRU	(NI1 | L2  | DATA | FAT | PE  | KERN | FETC | NA0)
#define	NI_UD_LRU	(NI1 | L2  | DATA | FAT | PE  | USER | LOAD | NA0 | NS)
#define	NI_UI_LRU	(NI1 | L2  | DATA | FAT | PE  | USER | FETC | NA0 | NS)

#define	NI_L2DIRPHYS	(NI1 | L2  | DATA | NA4 | PE  | NA2  | NA1  | PHYS)

/*
 * L1 data cache data and tag errors.
 *			 CPU   CLASS SUBCL  TRAP  PROT  MODE   ACCESS MISC
 */
#define	NI_HD_DDC	(NI1 | DC  | DATA | DIS | CE  | HYPR | LOAD | NA0)
#define	NI_KD_DDC	(NI1 | DC  | DATA | DIS | CE  | KERN | LOAD | NA0)
#define	NI_KD_DDCTL1	(NI1 | DC  | DATA | DIS | CE  | KERN | LOAD | TL1)
#define	NI_HD_DTC	(NI1 | DC  | TAG  | DIS | CE  | HYPR | LOAD | NA0)
#define	NI_KD_DTC	(NI1 | DC  | TAG  | DIS | CE  | KERN | LOAD | NA0)
#define	NI_KD_DTCTL1	(NI1 | DC  | TAG  | DIS | CE  | KERN | LOAD | TL1)
#define	NI_UD_DDC	(NI1 | DC  | DATA | DIS | CE  | USER | LOAD | NA0 | NS)
#define	NI_UD_DTC	(NI1 | DC  | TAG  | DIS | CE  | USER | LOAD | NA0 | NS)

#define	NI_DPHYS	(NI1 | DC  | DATA | NA4 | NA3 | NA2  | NA1  | PHYS)
#define	NI_DTPHYS	(NI1 | DC  | TAG  | NA4 | NA3 | NA2  | NA1  | PHYS)

/*
 * L1 instruction cache data and tag errors.
 *			 CPU   CLASS SUBCL  TRAP  PROT  MODE   ACCESS MISC
 */
#define	NI_HI_IDC	(NI1 | IC  | DATA | DIS | CE  | HYPR | FETC | NA0)
#define	NI_KI_IDC	(NI1 | IC  | DATA | DIS | CE  | KERN | FETC | NA0)
#define	NI_KI_IDCTL1	(NI1 | IC  | DATA | DIS | CE  | KERN | FETC | TL1)
#define	NI_HI_ITC	(NI1 | IC  | TAG  | DIS | CE  | HYPR | FETC | NA0)
#define	NI_KI_ITC	(NI1 | IC  | TAG  | DIS | CE  | KERN | FETC | NA0)
#define	NI_KI_ITCTL1	(NI1 | IC  | TAG  | DIS | CE  | KERN | FETC | TL1)
#define	NI_UI_IDC	(NI1 | IC  | DATA | DIS | CE  | USER | FETC | NA0 | NS)
#define	NI_UI_ITC	(NI1 | IC  | TAG  | DIS | CE  | USER | FETC | NA0 | NS)

#define	NI_IPHYS	(NI1 | IC  | DATA | NA4 | NA3 | NA2  | NA1  | PHYS)
#define	NI_ITPHYS	(NI1 | IC  | TAG  | NA4 | NA3 | NA2  | NA1  | PHYS)

/*
 * Instruction and data TLB data and tag (CAM) errors.
 *			 CPU   CLASS  SUBCL  TRAP  PROT  MODE   ACCESS MISC
 */
#define	NI_KD_DMDU	(NI1 | DTLB | DATA | PRE | PE  | KERN | LOAD | NA0)
#define	NI_KD_DMDUTL1	(NI1 | DTLB | DATA | PRE | PE  | KERN | LOAD | TL1 | NI)
#define	NI_HD_DMTU	(NI1 | DTLB | TAG  | PRE | PE  | HYPR | ASI  | NA0)
#define	NI_UD_DMDU	(NI1 | DTLB | DATA | PRE | PE  | USER | LOAD | NA0 | NI)
#define	NI_UD_DMTU	(NI1 | DTLB | TAG  | PRE | PE  | USER | ASI  | NA0)
#define	NI_HD_DMDUASI	(NI1 | DTLB | DATA | PRE | PE  | HYPR | ASI  | NA0)
#define	NI_UD_DMDUASI	(NI1 | DTLB | DATA | PRE | PE  | USER | ASI  | NA0)
#define	NI_KD_DMSU	(NI1 | DTLB | DATA | PRE | PE  | KERN | STOR | NA0)

#define	NI_DMDURAND	(NI1 | DTLB | DATA | PRE | PE  | NA2  | NA1  | RAND)
#define	NI_DMTURAND	(NI1 | DTLB | TAG  | PRE | PE  | NA2  | NA1  | RAND)

#define	NI_KI_IMDU	(NI1 | ITLB | DATA | PRE | PE  | KERN | FETC | NA0)
#define	NI_KI_IMDUTL1	(NI1 | ITLB | DATA | PRE | PE  | KERN | FETC | TL1 | NI)
#define	NI_HI_IMTU	(NI1 | ITLB | TAG  | PRE | PE  | HYPR | ASI  | NA0)
#define	NI_UI_IMDU	(NI1 | ITLB | DATA | PRE | PE  | USER | FETC | NA0 | NI)
#define	NI_UI_IMTU	(NI1 | ITLB | TAG  | PRE | PE  | USER | ASI  | NA0)
#define	NI_HI_IMDUASI	(NI1 | ITLB | DATA | PRE | PE  | HYPR | ASI  | NA0)
#define	NI_UI_IMDUASI	(NI1 | ITLB | DATA | PRE | PE  | USER | ASI  | NA0)

#define	NI_IMDURAND	(NI1 | ITLB | DATA | PRE | PE  | NA2  | NA1  | RAND)
#define	NI_IMTURAND	(NI1 | ITLB | TAG  | PRE | PE  | NA2  | NA1  | RAND)

/*
 * Integer register file (SPARC Internal) errors.
 *			 CPU   CLASS SUBCL  TRAP  PROT  MODE   ACCESS MISC
 */
#define	NI_HD_IRUL	(NI1 | INT | IREG | PRE | UE  | HYPR | LOAD | NA0)
#define	NI_HD_IRUS	(NI1 | INT | IREG | PRE | UE  | HYPR | STOR | NA0)
#define	NI_HD_IRUO	(NI1 | INT | IREG | PRE | UE  | HYPR | OP   | NA0)

#define	NI_KD_IRUL	(NI1 | INT | IREG | PRE | UE  | KERN | LOAD | NA0)
#define	NI_KD_IRUS	(NI1 | INT | IREG | PRE | UE  | KERN | STOR | NA0)
#define	NI_KD_IRUO	(NI1 | INT | IREG | PRE | UE  | KERN | OP   | NA0)

#define	NI_HD_IRCL	(NI1 | INT | IREG | DIS | CE  | HYPR | LOAD | NA0)
#define	NI_HD_IRCS	(NI1 | INT | IREG | DIS | CE  | HYPR | STOR | NA0)
#define	NI_HD_IRCO	(NI1 | INT | IREG | DIS | CE  | HYPR | OP   | NA0)

#define	NI_KD_IRCL	(NI1 | INT | IREG | DIS | CE  | KERN | LOAD | NA0)
#define	NI_KD_IRCS	(NI1 | INT | IREG | DIS | CE  | KERN | STOR | NA0)
#define	NI_KD_IRCO	(NI1 | INT | IREG | DIS | CE  | KERN | OP   | NA0)

/*
 * Floating-point register file (SPARC Internal) errors.
 *			 CPU   CLASS SUBCL  TRAP  PROT  MODE   ACCESS MISC
 */
#define	NI_HD_FRUL	(NI1 | INT | FREG | PRE | UE  | HYPR | LOAD | NA0)
#define	NI_HD_FRUS	(NI1 | INT | FREG | PRE | UE  | HYPR | STOR | NA0)
#define	NI_HD_FRUO	(NI1 | INT | FREG | PRE | UE  | HYPR | OP   | NA0)

#define	NI_KD_FRUL	(NI1 | INT | FREG | PRE | UE  | KERN | LOAD | NA0)
#define	NI_KD_FRUS	(NI1 | INT | FREG | PRE | UE  | KERN | STOR | NA0)
#define	NI_KD_FRUO	(NI1 | INT | FREG | PRE | UE  | KERN | OP   | NA0)

#define	NI_HD_FRCL	(NI1 | INT | FREG | DIS | CE  | HYPR | LOAD | NA0)
#define	NI_HD_FRCS	(NI1 | INT | FREG | DIS | CE  | HYPR | STOR | NA0)
#define	NI_HD_FRCO	(NI1 | INT | FREG | DIS | CE  | HYPR | OP   | NA0)

#define	NI_KD_FRCL	(NI1 | INT | FREG | DIS | CE  | KERN | LOAD | NA0)
#define	NI_KD_FRCS	(NI1 | INT | FREG | DIS | CE  | KERN | STOR | NA0)
#define	NI_KD_FRCO	(NI1 | INT | FREG | DIS | CE  | KERN | OP   | NA0)

/*
 * Modular Arithmetic Unit (SPARC Internal) errors.
 *			 CPU   CLASS SUBCL  TRAP  PROT  MODE   ACCESS MISC
 */
#define	NI_HD_MAUL	(NI1 | INT | MA   | DIS | PE  | HYPR | LOAD | NA0)
#define	NI_HD_MAUS	(NI1 | INT | MA   | DIS | PE  | HYPR | STOR | NA0)
#define	NI_HD_MAUO	(NI1 | INT | MA   | DIS | PE  | HYPR | OP   | NA0)

/*
 * DEBUG test cases to ensure injector and system are behaving.
 *			 CPU   CLASS  SUBCL  TRAP  PROT  MODE   ACCESS MISC
 */
#define	NI_TEST		(NI1 | UTIL | NA5  | NA4 | NA3 | NA2  | NA1  | NA0)
#define	NI_PRINT_ESRS	(NI1 | UTIL | NA5  | NA4 | NA3 | NA2  | LOAD | NA0)
#define	NI_PRINT_UE	(NI1 | UTIL | NA5  | NA4 | UE  | NA2  | LOAD | NA0)
#define	NI_PRINT_CE	(NI1 | UTIL | NA5  | NA4 | CE  | NA2  | LOAD | NA0)

/*
 * JBus definitions - to build command types below.
 */
#define	ERR_JBUS_SHIFT		UINT64_C(ERR_CPU_SHIFT + 4)
#define	ERR_JBUS_INV		UINT64_C(0x0)	/* invalid */
#define	ERR_JBUS_NONE		UINT64_C(0x1)	/* nothing */
#define	ERR_JBUS_BERR		UINT64_C(0x2)	/* bus error response */
#define	ERR_JBUS_APAR		UINT64_C(0x3)	/* adress parity error */
#define	ERR_JBUS_CPAR		UINT64_C(0x4)	/* control parity error */
#define	ERR_JBUS_DPAR		UINT64_C(0x5)	/* data parity error */
#define	ERR_JBUS_TO		UINT64_C(0x6)	/* timeout error */
#define	ERR_JBUS_UM		UINT64_C(0x7)	/* unmapped memory error */
#define	ERR_JBUS_NEM		UINT64_C(0x8)	/* nonexist memory error */
#define	ERR_JBUS_MASK		(0xf)

#define	ERR_JBUS(x)		(((x) >> ERR_JBUS_SHIFT) & ERR_JBUS_MASK)
#define	ERR_JBUS_ISBERR(x)	(ERR_JBUS(x) == ERR_JBUS_BERR)
#define	ERR_JBUS_ISAPAR(x)	(ERR_JBUS(x) == ERR_JBUS_APAR)
#define	ERR_JBUS_ISCPAR(x)	(ERR_JBUS(x) == ERR_JBUS_CPAR)
#define	ERR_JBUS_ISDPAR(x)	(ERR_JBUS(x) == ERR_JBUS_DPAR)
#define	ERR_JBUS_ISTO(x)	(ERR_JBUS(x) == ERR_JBUS_TO)
#define	ERR_JBUS_ISUM(x)	(ERR_JBUS(x) == ERR_JBUS_UM)
#define	ERR_JBUS_ISNEM(x)	(ERR_JBUS(x) == ERR_JBUS_NEM)

#define	JNA			(ERR_JBUS_NONE  << ERR_JBUS_SHIFT)
#define	JBE			(ERR_JBUS_BERR	<< ERR_JBUS_SHIFT)
#define	JAPE			(ERR_JBUS_APAR	<< ERR_JBUS_SHIFT)
#define	JCPE			(ERR_JBUS_CPAR	<< ERR_JBUS_SHIFT)
#define	JDPE			(ERR_JBUS_DPAR	<< ERR_JBUS_SHIFT)
#define	JTO			(ERR_JBUS_TO	<< ERR_JBUS_SHIFT)
#define	JUM			(ERR_JBUS_UM	<< ERR_JBUS_SHIFT)
#define	JNEM			(ERR_JBUS_NEM	<< ERR_JBUS_SHIFT)

/*
 * JBus (system bus) errors.
 *			 CPU   CLASS SUBCL  TRAP  PROT  MODE  ACCESS  JBUS  MISC
 */
#define	NI_KD_BE	(NI1 | BUS | DATA | DEF | BE  | KERN | LOAD | JBE | NA0)
#define	NI_KD_BEPEEK	(NI1 | BUS | DATA | DEF | BE  | KERN | LOAD | JBE | \
									DDIPEEK)
#define	NI_HD_APAR	(NI1 | BUS | DATA | FAT | PE  | HYPR | LOAD | JAPE| NA0)
#define	NI_HI_APAR	(NI1 | BUS | DATA | FAT | PE  | HYPR | FETC | JAPE| NI)
#define	NI_HD_CPAR	(NI1 | BUS | DATA | FAT | BE  | HYPR | LOAD | JCPE| NS)
#define	NI_HI_CPAR	(NI1 | BUS | DATA | FAT | BE  | HYPR | FETC | JCPE| NS)

#define	NI_HD_DPAR	(NI1 | BUS | DATA | DIS | PE  | HYPR | LOAD | JDPE| NA0)
#define	NI_HI_DPAR	(NI1 | BUS | DATA | DIS | PE  | HYPR | FETC | JDPE| NI)
#define	NI_HD_DPARS	(NI1 | BUS | DATA | DIS | PE  | HYPR | STOR | JDPE| NA0)
#define	NI_HD_DPARO	(NI1 | BUS | DATA | DIS | PE  | HYPR | NA1  | JDPE| NS)

#define	NI_HD_L2TO	(NI1 | L2  | DATA | FAT | BE  | HYPR | LOAD | JTO | NA0)
#define	NI_HD_ARBTO	(NI1 | BUS | DATA | FAT | BE  | HYPR | NA1  | JTO | NA0)
#define	NI_HD_RTO	(NI1 | BUS | DATA | DIS | UE  | HYPR | LOAD | JTO | NA0)
#define	NI_HD_INTRTO	(NI1 | BUS | INTR | DIS | UE  | HYPR | LOAD | JTO | NA0)

#define	NI_HD_UMS	(NI1 | BUS | DATA | DIS | UE  | HYPR | STOR | JUM | NA0)
#define	NI_HD_NEMS	(NI1 | BUS | DATA | DIS | UE  | HYPR | STOR | JNEM| NA0)
#define	NI_HD_NEMR	(NI1 | BUS | DATA | DIS | UE  | HYPR | LOAD | JNEM| NA0)

#define	NI_CLR_JBI_LOG	(NI1 | BUS | DATA | NA4 | NA3 | KERN | STOR | JNA | NA0)
#define	NI_PRINT_JBI	(NI1 | BUS | DATA | NA4 | NA3 | KERN | LOAD | JNA | NA0)
#define	NI_TEST_JBI	(NI1 | BUS | NA5  | NA4 | NA3 | KERN | NA1  | JNA | NA0)

/*
 * SSI (BootROM interface) errors.
 *			 CPU   CLASS SUBCL  TRAP  PROT  MODE  ACCESS  MISC
 */
#define	NI_HD_SSIPAR	(NI1 | BUS | DATA | DEF | PE  | NA2  | LOAD | PHYS | NI)
#define	NI_HD_SSIPARS	(NI1 | BUS | DATA | DEF | PE  | NA2  | STOR | PHYS | NI)
#define	NI_HD_SSITO	(NI1 | BUS | DATA | DEF | BE  | NA2  | LOAD | PHYS)
#define	NI_HD_SSITOS	(NI1 | BUS | DATA | DEF | BE  | NA2  | STOR | PHYS)
#define	NI_PRINT_SSI	(NI1 | BUS | NA5  | NA4 | NA3 | NA2  | LOAD | NA0)

#ifdef	__cplusplus
}
#endif

#endif	/* _MEMTESTIO_NI_H */
