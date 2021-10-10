/*
 * Copyright (c) 2006, 2010, Oracle and/or its affiliates. All rights reserved.
 */

#ifndef _MEMTESTIO_N2_H
#define	_MEMTESTIO_N2_H

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * UltraSPARC-T2 (Niagara-II) specific error definitions.
 */

/*
 * Memory (DRAM) errors.
 *
 * Note that these errors are continued from those defined by Niagara-I
 * in memtestio_ni.h.
 *			 CPU   CLASS SUBCL  TRAP  PROT  MODE   ACCESS MISC
 */
#define	N2_HD_DAUCWQ	(NI2 | MEM | DATA | DIS | UE  | HYPR | CWQAC| NA0)
#define	N2_KD_DAUDTLB	(NI2 | MEM | DATA | PRE | UE  | KERN | DTAC | NA0)
#define	N2_KI_DAUITLB	(NI2 | MEM | DATA | PRE | UE  | KERN | ITAC | NA0)

#define	N2_HD_DACCWQ	(NI2 | MEM | DATA | DIS | CE  | HYPR | CWQAC| NA0)
#define	N2_KD_DACDTLB	(NI2 | MEM | DATA | DIS | CE  | KERN | DTAC | NA0)
#define	N2_KI_DACITLB	(NI2 | MEM | DATA | DIS | CE  | KERN | ITAC | NA0)

/*
 * L2 cache data and tag errors.
 *
 * Note that these errors are continued from those defined by Niagara-I
 * in memtestio_ni.h.
 *			 CPU   CLASS SUBCL  TRAP  PROT  MODE   ACCESS MISC
 */
#define	N2_HD_LDAUCWQ	(NI2 | L2  | DATA | DIS | UE  | HYPR | CWQAC| NA0)
#define	N2_KD_LDAUDTLB	(NI2 | L2  | DATA | PRE | UE  | KERN | DTAC | NA0)
#define	N2_KI_LDAUITLB	(NI2 | L2  | DATA | PRE | UE  | KERN | ITAC | NA0)
#define	N2_HD_LDAUPRI	(NI2 | L2  | DATA | PRE | UE  | HYPR | PRICE| NA0)

#define	N2_HD_LDACCWQ	(NI2 | L2  | DATA | DIS | CE  | HYPR | CWQAC| NA0)
#define	N2_KD_LDACDTLB	(NI2 | L2  | DATA | DIS | CE  | KERN | DTAC | NA0)
#define	N2_KI_LDACITLB	(NI2 | L2  | DATA | DIS | CE  | KERN | ITAC | NA0)
#define	N2_HD_LDACPRI	(NI2 | L2  | DATA | PRE | CE  | HYPR | PRICE| NA0)

/*
 * L2 cache data and tag errors injected by address.
 *
 * Note that these errors are defined by Niagara-I in memtestio_ni.h.
 *
 *			 CPU   CLASS SUBCL  TRAP  PROT  MODE   ACCESS MISC
 */

/*
 * L2 cache "NotData" errors (including NotData write back).
 *
 *			 CPU   CLASS SUBCL  TRAP  PROT  MODE   ACCESS MISC
 */
#define	N2_HD_L2ND	(NI2 | L2  | DATA | PRE | ND  | HYPR | LOAD | NA0)
#define	N2_HD_L2NDMA	(NI2 | L2  | DATA | DIS | ND  | HYPR | MAL  | NA0)
#define	N2_HD_L2NDCWQ	(NI2 | L2  | DATA | DIS | ND  | HYPR | CWQAC| NA0)
#define	N2_HI_L2ND	(NI2 | L2  | DATA | PRE | ND  | HYPR | FETC | NA0)

#define	N2_KD_L2ND	(NI2 | L2  | DATA | PRE | ND  | KERN | LOAD | NA0)
#define	N2_KD_L2NDDTLB	(NI2 | L2  | DATA | PRE | ND  | KERN | DTAC | NA0)
#define	N2_KI_L2NDITLB	(NI2 | L2  | DATA | PRE | ND  | KERN | ITAC | NA0)
#define	N2_L2NDCOPYIN	(NI2 | L2  | DATA | PRE | ND  | USER | LOAD | CPIN | NS)
#define	N2_KD_L2NDTL1	(NI2 | L2  | DATA | PRE | ND  | KERN | LOAD | TL1)
#define	N2_KD_L2NDPR	(NI2 | L2  | DATA | PRE | ND  | KERN | PFETC| NA0)
#define	N2_HD_L2NDPRI	(NI2 | L2  | DATA | PRE | ND  | HYPR | PRICE| NA0)

#define	N2_OBP_L2ND	(NI2 | L2  | DATA | PRE | ND  | OBP  | LOAD | NA0 | NS)
#define	N2_KI_L2ND	(NI2 | L2  | DATA | PRE | ND  | KERN | FETC | NA0)
#define	N2_KI_L2NDTL1	(NI2 | L2  | DATA | PRE | ND  | KERN | FETC | TL1)
#define	N2_UD_L2ND	(NI2 | L2  | DATA | PRE | ND  | USER | LOAD | NA0)
#define	N2_UI_L2ND	(NI2 | L2  | DATA | PRE | ND  | USER | FETC | NA0 | NS)

#define	N2_HD_L2NDWB	(NI2 | L2WB| DATA | NA4 | ND  | HYPR | LOAD | NA0)
#define	N2_HI_L2NDWB	(NI2 | L2WB| DATA | NA4 | ND  | HYPR | FETC | NA0)
#define	N2_IO_L2ND	(NI2 | L2  | DATA | DIS | ND  | DMA  | LOAD | NA0)
#define	N2_L2NDPHYS	(NI2 | L2  | DATA | NA4 | ND  | NA2  | NA1  | PHYS)

/*
 * L2 cache V(U)AD correctable errors.
 *
 *			 CPU   CLASS SUBCL  TRAP  PROT  MODE   ACCESS MISC
 */
#define	N2_HD_LVF_VD	(NI2 | L2  | VD   | DIS | UE  | HYPR | LOAD | NA0)
#define	N2_HI_LVF_VD	(NI2 | L2  | VD   | DIS | UE  | HYPR | FETC | NA0)
#define	N2_KD_LVF_VD	(NI2 | L2  | VD   | DIS | UE  | KERN | LOAD | NA0)
#define	N2_KI_LVF_VD	(NI2 | L2  | VD   | DIS | UE  | KERN | FETC | NA0)
#define	N2_UD_LVF_VD	(NI2 | L2  | VD   | DIS | UE  | USER | LOAD | NA0 | NS)
#define	N2_UI_LVF_VD	(NI2 | L2  | VD   | DIS | UE  | USER | FETC | NA0 | NS)

#define	N2_HD_LVF_UA	(NI2 | L2  | UA   | DIS | UE  | HYPR | LOAD | NA0)
#define	N2_HI_LVF_UA	(NI2 | L2  | UA   | DIS | UE  | HYPR | FETC | NA0)
#define	N2_KD_LVF_UA	(NI2 | L2  | UA   | DIS | UE  | KERN | LOAD | NA0)
#define	N2_KI_LVF_UA	(NI2 | L2  | UA   | DIS | UE  | KERN | FETC | NA0)
#define	N2_UD_LVF_UA	(NI2 | L2  | UA	  | DIS | UE  | USER | LOAD | NA0 | NS)
#define	N2_UI_LVF_UA	(NI2 | L2  | UA   | DIS | UE  | USER | FETC | NA0 | NS)

#define	N2_HD_LVC_VD	(NI2 | L2  | VD   | DIS | CE  | HYPR | LOAD | NA0)
#define	N2_HI_LVC_VD	(NI2 | L2  | VD   | DIS | CE  | HYPR | FETC | NA0)
#define	N2_KD_LVC_VD	(NI2 | L2  | VD   | DIS | CE  | KERN | LOAD | NA0)
#define	N2_KI_LVC_VD	(NI2 | L2  | VD   | DIS | CE  | KERN | FETC | NA0)
#define	N2_UD_LVC_VD	(NI2 | L2  | VD   | DIS | CE  | USER | LOAD | NA0 | NS)
#define	N2_UI_LVC_VD	(NI2 | L2  | VD   | DIS | CE  | USER | FETC | NA0 | NS)

#define	N2_HD_LVC_UA	(NI2 | L2  | UA   | DIS | CE  | HYPR | LOAD | NA0)
#define	N2_HI_LVC_UA	(NI2 | L2  | UA   | DIS | CE  | HYPR | FETC | NA0)
#define	N2_KD_LVC_UA	(NI2 | L2  | UA   | DIS | CE  | KERN | LOAD | NA0)
#define	N2_KI_LVC_UA	(NI2 | L2  | UA   | DIS | CE  | KERN | FETC | NA0)
#define	N2_UD_LVC_UA	(NI2 | L2  | UA	  | DIS | CE  | USER | LOAD | NA0 | NS)
#define	N2_UI_LVC_UA	(NI2 | L2  | UA   | DIS | CE  | USER | FETC | NA0 | NS)

/*
 * L2 cache directory (fatal) errors.
 *
 * Note that these errors are defined by Niagara-I in memtestio_ni.h.
 *
 *			 CPU   CLASS SUBCL  TRAP  PROT  MODE   ACCESS MISC
 */

/*
 * L2 cache write back errors.
 *
 * Note that these errors are continued from those defined by Niagara-I
 * in memtestio_ni.h.
 *			 CPU   CLASS  SUBCL  TRAP  PROT  MODE   ACCESS MISC
 */
#define	N2_HD_LDWUPRI	(NI2 | L2WB | DATA | DIS | UE  | HYPR | LOAD | MPIO)
#define	N2_HI_LDWUPRI	(NI2 | L2WB | DATA | DIS | UE  | HYPR | FETC | MPIO)

#define	N2_HD_LDWCPRI	(NI2 | L2WB | DATA | DIS | CE  | HYPR | LOAD | MPIO)
#define	N2_HI_LDWCPRI	(NI2 | L2WB | DATA | DIS | CE  | HYPR | FETC | MPIO)

/*
 * L1 data cache data and tag errors.
 *
 * Note that these errors are continued from those defined by Niagara-I
 * in memtestio_ni.h.
 *			 CPU   CLASS SUBCL  TRAP  PROT  MODE   ACCESS MISC
 */
#define	N2_HD_DCVP	(NI2 | DC  | TAG  | DIS | VAL | HYPR | LOAD | NA0)
#define	N2_KD_DCVP	(NI2 | DC  | TAG  | DIS | VAL | KERN | LOAD | NA0)
#define	N2_KD_DCVPTL1	(NI2 | DC  | TAG  | DIS | VAL | KERN | LOAD | TL1)

#define	N2_HD_DCTM	(NI2 | DC  | MH   | DIS | CE  | HYPR | LOAD | NA0)
#define	N2_KD_DCTM	(NI2 | DC  | MH   | DIS | CE  | KERN | LOAD | NA0)
#define	N2_KD_DCTMTL1	(NI2 | DC  | MH   | DIS | CE  | KERN | LOAD | TL1)

#define	N2_DVPHYS	(NI2 | DC  | TAG  | NA4 | VAL | NA2  | NA1  | PHYS)
#define	N2_DMPHYS	(NI2 | DC  | MH   | NA4 | NA3 | NA2  | NA1  | PHYS)

/*
 * L1 instruction cache data and tag errors.
 *
 * Note that these errors are continued from those defined by Niagara-I
 * in memtestio_ni.h.
 *			 CPU   CLASS SUBCL  TRAP  PROT  MODE   ACCESS MISC
 */
#define	N2_HI_ICVP	(NI2 | IC  | TAG  | DIS | VAL | HYPR | FETC | NA0)
#define	N2_KI_ICVP	(NI2 | IC  | TAG  | DIS | VAL | KERN | FETC | NA0)
#define	N2_KI_ICVPTL1	(NI2 | IC  | TAG  | DIS | VAL | KERN | FETC | TL1)

#define	N2_HI_ICTM	(NI2 | IC  | MH   | DIS | CE  | HYPR | FETC | NA0)
#define	N2_KI_ICTM	(NI2 | IC  | MH   | DIS | CE  | KERN | FETC | NA0)
#define	N2_KI_ICTMTL1	(NI2 | IC  | MH   | DIS | CE  | KERN | FETC | TL1)

#define	N2_IVPHYS	(NI2 | IC  | TAG  | NA4 | VAL | NA2  | NA1  | PHYS)
#define	N2_IMPHYS	(NI2 | IC  | MH   | NA4 | NA3 | NA2  | NA1  | PHYS)

/*
 * Instruction and data TLB data and tag (CAM) errors.
 *
 *			 CPU   CLASS  SUBCL  TRAP  PROT  MODE   ACCESS MISC
 */
#define	N2_KD_DTDP	(NI2 | DTLB | DATA | PRE | PE  | KERN | LOAD | NA0)
#define	N2_KD_DTDPV	(NI2 | DTLB | DATA | PRE | PE  | KERN | LOAD | ORPH)
#define	N2_KD_DTTP	(NI2 | DTLB | TAG  | PRE | PE  | KERN | LOAD | NA0)
#define	N2_KD_DTTM	(NI2 | DTLB | MH   | PRE | PE  | KERN | LOAD | NA0)
#define	N2_KD_DTMU	(NI2 | DTLB | MMU  | PRE | PE  | KERN | LOAD | NA0)

#define	N2_KI_ITDP	(NI2 | ITLB | DATA | PRE | PE  | KERN | FETC | NA0)
#define	N2_KI_ITDPV	(NI2 | ITLB | DATA | PRE | PE  | KERN | FETC | ORPH)
#define	N2_KI_ITTP	(NI2 | ITLB | TAG  | PRE | PE  | KERN | FETC | NA0)
#define	N2_KI_ITTM	(NI2 | ITLB | MH   | PRE | PE  | KERN | FETC | NA0)
#define	N2_KI_ITMU	(NI2 | ITLB | MMU  | PRE | PE  | KERN | FETC | NA0)

/*
 * Integer register file (SPARC Internal) errors.
 *
 * Note that these errors are defined by Niagara-I in memtestio_ni.h.
 *
 *			 CPU   CLASS SUBCL  TRAP  PROT  MODE   ACCESS MISC
 */

/*
 * Floating-point register file (SPARC Internal) errors.
 *
 * Note that these errors are defined by Niagara-I in memtestio_ni.h.
 *
 *			 CPU   CLASS SUBCL  TRAP  PROT  MODE   ACCESS MISC
 */

/*
 * Store Buffer (SPARC Internal) errors.
 *
 *			 CPU   CLASS SUBCL  TRAP  PROT  MODE   ACCESS MISC
 */
#define	N2_HD_SBDLU	(NI2 | STB | DATA | PRE | UE  | HYPR | LOAD | NA0)
#define	N2_HD_SBDPU	(NI2 | STB | DATA | DIS | UE  | HYPR | PCX  | NA0)
#define	N2_HD_SBDPUASI	(NI2 | STB | DATA | DIS | UE  | HYPR | ASI  | NA0)
#define	N2_KD_SBDLU	(NI2 | STB | DATA | PRE | UE  | KERN | LOAD | NA0)
#define	N2_KD_SBDPU	(NI2 | STB | DATA | DIS | UE  | KERN | PCX  | NA0)

#define	N2_HD_SBAPP	(NI2 | STB | TAG  | DEF | PE  | HYPR | PCX  | NA0)
#define	N2_HD_SBAPPASI	(NI2 | STB | TAG  | DEF | PE  | HYPR | ASI  | NA0)
#define	N2_KD_SBAPP	(NI2 | STB | TAG  | DEF | PE  | KERN | PCX  | NA0)
#define	N2_KD_SBAPPASI	(NI2 | STB | TAG  | DEF | PE  | KERN | ASI  | NA0 | NS)

#define	N2_IO_SBDIOU	(NI2 | STB | DATA | DEF | UE  | DMA  | PCX  | NA0)
#define	N2_IO_SBDIOUASI	(NI2 | STB | DATA | DEF | UE  | DMA  | ASI  | NA0 | NS)

#define	N2_HD_SBDLC	(NI2 | STB | DATA | PRE | CE  | HYPR | LOAD | NA0)
#define	N2_HD_SBDPC	(NI2 | STB | DATA | DIS | CE  | HYPR | PCX  | NA0)
#define	N2_HD_SBDPCASI	(NI2 | STB | DATA | DIS | CE  | HYPR | ASI  | NA0)
#define	N2_KD_SBDLC	(NI2 | STB | DATA | PRE | CE  | KERN | LOAD | NA0)
#define	N2_KD_SBDPC	(NI2 | STB | DATA | DIS | CE  | KERN | PCX  | NA0)

#define	N2_IO_SBDPC	(NI2 | STB | DATA | DEF | CE  | DMA  | PCX  | NA0)
#define	N2_IO_SBDPCASI	(NI2 | STB | DATA | DEF | CE  | DMA  | ASI  | NA0 | NS)

/*
 * Internal register array (SPARC Internal) errors.
 *
 *			 CPU   CLASS SUBCL  TRAP  PROT  MODE   ACCESS MISC
 */
#define	N2_HD_SCAU	(NI2 | INT | SCR  | PRE | UE  | HYPR | ASI  | NA0)
#define	N2_HD_SCAC	(NI2 | INT | SCR  | PRE | CE  | HYPR | ASI  | NA0)
#define	N2_KD_SCAU	(NI2 | INT | SCR  | PRE | UE  | KERN | ASI  | NA0)
#define	N2_KD_SCAC	(NI2 | INT | SCR  | PRE | CE  | KERN | ASI  | NA0)

#define	N2_HD_TCUP	(NI2 | INT | TCA  | PRE | UE  | HYPR | ASR  | NA0)
#define	N2_HD_TCCP	(NI2 | INT | TCA  | PRE | CE  | HYPR | ASR  | NA0)
#define	N2_HD_TCUD	(NI2 | INT | TCA  | DIS | UE  | HYPR | NA1  | NA0)
#define	N2_HD_TCCD	(NI2 | INT | TCA  | DIS | CE  | HYPR | NA1  | NA0)

#define	N2_HD_TSAU	(NI2 | INT | TSA  | PRE | UE  | HYPR | LOAD | NA0)
#define	N2_HD_TSAC	(NI2 | INT | TSA  | PRE | CE  | HYPR | LOAD | NA0)

#define	N2_HD_MRAU	(NI2 | INT | DATA | PRE | PE  | HYPR | NA1  | NA0)
#define	N2_HD_MRAUASI	(NI2 | INT | DATA | PRE | PE  | HYPR | ASI  | NA0)

/*
 * Modular Arithmetic Unit and Control Word Queue (SPARC Internal) errors.
 *
 * Note that the MA errors are defined by Niagara-I in memtestio_ni.h.
 *
 *			 CPU   CLASS SUBCL  TRAP  PROT  MODE   ACCESS MISC
 */
#define	N2_HD_CWQP	(NI2 | INT | CWQ  | PRE | UE  | HYPR | OP   | NA0)

/*
 * The following definitions are for use with the SOC error types (below),
 * unlike other definitions the SOC errors will use SUBCL values that are
 * specific to the SOC and are not related to the already defined SUBCL
 * values used by other commands.
 */
#define	SOC_SUBCL_INV		(0x00ULL)	/* invalid */
#define	SOC_SUBCL_ECC		(0x01ULL)	/* MCU ECC count errs */
#define	SOC_SUBCL_FBR		(0x02ULL)	/* MCU FBR count errs */
#define	SOC_SUBCL_NIU		(0x03ULL)	/* SIO unit errs */
#define	SOC_SUBCL_SIO		(0x04ULL)	/* SIO unit errs */
#define	SOC_SUBCL_NCUT		(0x05ULL)	/* NCU C-tag errs */
#define	SOC_SUBCL_NCUD		(0x06ULL)	/* NCU data errs */
#define	SOC_SUBCL_NCUC		(0x07ULL)	/* NCU CPX (FIFO) errs */
#define	SOC_SUBCL_NCUP		(0x08ULL)	/* NCU PCX errs */
#define	SOC_SUBCL_NCUI		(0x09ULL)	/* NCU interrupt errs */
#define	SOC_SUBCL_NCUM		(0x0aULL)	/* NCU mondo errs */
#define	SOC_SUBCL_DMUD		(0x0bULL)	/* DMU data errs */
#define	SOC_SUBCL_DMUS		(0x0cULL)	/* DMU SII credit errs */
#define	SOC_SUBCL_DMUT		(0x0dULL)	/* DMU C-tag errs */
#define	SOC_SUBCL_DMUN		(0x0eULL)	/* DMU NCU errs */
#define	SOC_SUBCL_DMUI		(0x0fULL)	/* DMU interrupt errs */
#define	SOC_SUBCL_SIID		(0x10ULL)	/* SII DMU errs */
#define	SOC_SUBCL_SIIN		(0x11ULL)	/* SII NIU errs */

#define	SOC_SUBCLASS_ISECC(x)	(ERR_SUBCLASS(x) == SOC_SUBCL_ECC)
#define	SOC_SUBCLASS_ISFBR(x)	(ERR_SUBCLASS(x) == SOC_SUBCL_FBR)
#define	SOC_SUBCLASS_ISNIU(x)	(ERR_SUBCLASS(x) == SOC_SUBCL_NIU)
#define	SOC_SUBCLASS_ISSIO(x)	(ERR_SUBCLASS(x) == SOC_SUBCL_SIO)
#define	SOC_SUBCLASS_ISNCUT(x)	(ERR_SUBCLASS(x) == SOC_SUBCL_NCUT)
#define	SOC_SUBCLASS_ISNCUD(x)	(ERR_SUBCLASS(x) == SOC_SUBCL_NCUD)
#define	SOC_SUBCLASS_ISNCUC(x)	(ERR_SUBCLASS(x) == SOC_SUBCL_NCUC)
#define	SOC_SUBCLASS_ISNCUP(x)	(ERR_SUBCLASS(x) == SOC_SUBCL_NCUP)
#define	SOC_SUBCLASS_ISNCUI(x)	(ERR_SUBCLASS(x) == SOC_SUBCL_NCUI)
#define	SOC_SUBCLASS_ISNCUM(x)	(ERR_SUBCLASS(x) == SOC_SUBCL_NCUM)
#define	SOC_SUBCLASS_ISDMUD(x)	(ERR_SUBCLASS(x) == SOC_SUBCL_DMUD)
#define	SOC_SUBCLASS_ISDMUS(x)	(ERR_SUBCLASS(x) == SOC_SUBCL_DMUS)
#define	SOC_SUBCLASS_ISDMUT(x)	(ERR_SUBCLASS(x) == SOC_SUBCL_DMUT)
#define	SOC_SUBCLASS_ISDMUN(x)	(ERR_SUBCLASS(x) == SOC_SUBCL_DMUN)
#define	SOC_SUBCLASS_ISDMUI(x)	(ERR_SUBCLASS(x) == SOC_SUBCL_DMUI)
#define	SOC_SUBCLASS_ISSIID(x)	(ERR_SUBCLASS(x) == SOC_SUBCL_SIID)
#define	SOC_SUBCLASS_ISSIIN(x)	(ERR_SUBCLASS(x) == SOC_SUBCL_SIIN)

/* Also define some super-subclasses to make code cleaner */
#define	SOC_SUBCLASS_ISMCU(x)	(SOC_SUBCLASS_ISECC(x) || SOC_SUBCLASS_ISFBR(x))

#define	SOC_SUBCLASS_ISNCU(x)	(SOC_SUBCLASS_ISNCUT(x) ||		\
					SOC_SUBCLASS_ISNCUD(x) ||	\
					SOC_SUBCLASS_ISNCUC(x) ||	\
					SOC_SUBCLASS_ISNCUP(x) ||	\
					SOC_SUBCLASS_ISNCUI(x) ||	\
					SOC_SUBCLASS_ISNCUM(x))

#define	SOC_SUBCLASS_ISDMU(x)	(SOC_SUBCLASS_ISDMUD(x) ||		\
					SOC_SUBCLASS_ISDMUS(x) ||	\
					SOC_SUBCLASS_ISDMUT(x) ||	\
					SOC_SUBCLASS_ISDMUN(x) ||	\
					SOC_SUBCLASS_ISDMUI(x))

#define	ECC	(SOC_SUBCL_ECC	<< ERR_SUBCL_SHIFT)
#define	FBR	(SOC_SUBCL_FBR	<< ERR_SUBCL_SHIFT)
#define	NIU	(SOC_SUBCL_NIU	<< ERR_SUBCL_SHIFT)
#define	SIO	(SOC_SUBCL_SIO	<< ERR_SUBCL_SHIFT)
#define	NCUT	(SOC_SUBCL_NCUT	<< ERR_SUBCL_SHIFT)
#define	NCUD	(SOC_SUBCL_NCUD	<< ERR_SUBCL_SHIFT)
#define	NCUC	(SOC_SUBCL_NCUC	<< ERR_SUBCL_SHIFT)
#define	NCUP	(SOC_SUBCL_NCUP	<< ERR_SUBCL_SHIFT)
#define	NCUI	(SOC_SUBCL_NCUI	<< ERR_SUBCL_SHIFT)
#define	NCUM	(SOC_SUBCL_NCUM	<< ERR_SUBCL_SHIFT)
#define	DMUD	(SOC_SUBCL_DMUD	<< ERR_SUBCL_SHIFT)
#define	DMUS	(SOC_SUBCL_DMUS	<< ERR_SUBCL_SHIFT)
#define	DMUT	(SOC_SUBCL_DMUT	<< ERR_SUBCL_SHIFT)
#define	DMUN	(SOC_SUBCL_DMUN	<< ERR_SUBCL_SHIFT)
#define	DMUI	(SOC_SUBCL_DMUI	<< ERR_SUBCL_SHIFT)
#define	SIID	(SOC_SUBCL_SIID	<< ERR_SUBCL_SHIFT)
#define	SIIN	(SOC_SUBCL_SIIN	<< ERR_SUBCL_SHIFT)

/*
 * System on Chip offsets for the SOC error registers, these
 * are used in the command definitions in the file mtst_n2.h.
 */
#define	N2_SOC_NCUDMUCREDIT_SHIFT	42
#define	N2_SOC_MCU3ECC_SHIFT		41
#define	N2_SOC_MCU3FBR_SHIFT		40
#define	N2_SOC_MCU3FBU_SHIFT		39

#define	N2_SOC_MCU2ECC_SHIFT		38
#define	N2_SOC_MCU2FBR_SHIFT		37
#define	N2_SOC_MCU2FBU_SHIFT		36

#define	N2_SOC_MCU1ECC_SHIFT		35
#define	N2_SOC_MCU1FBR_SHIFT		34
#define	N2_SOC_MCU1FBU_SHIFT		33

#define	N2_SOC_MCU0ECC_SHIFT		32
#define	N2_SOC_MCU0FBR_SHIFT		31
#define	N2_SOC_MCU0FBU_SHIFT		30

#define	N2_SOC_NIUDATAPARITY_SHIFT	29
#define	N2_SOC_NIUCTAGUE_SHIFT		28
#define	N2_SOC_NIUCTAGCE_SHIFT		27
#define	N2_SOC_SIOCTAGCE_SHIFT		26
#define	N2_SOC_SIOCTAGUE_SHIFT		25

#define	N2_SOC_NCUCTAGCE_SHIFT		23
#define	N2_SOC_NCUCTAGUE_SHIFT		22
#define	N2_SOC_NCUDMUUE_SHIFT		21
#define	N2_SOC_NCUCPXUE_SHIFT		20
#define	N2_SOC_NCUPCXUE_SHIFT		19
#define	N2_SOC_NCUPCXDATA_SHIFT		18
#define	N2_SOC_NCUINTTABLE_SHIFT	17
#define	N2_SOC_NCUMONDOFIFO_SHIFT	16
#define	N2_SOC_NCUMONDOTABLE_SHIFT	15
#define	N2_SOC_NCUDATAPARITY_SHIFT	14

#define	N2_SOC_DMUDATAPARITY_SHIFT	13
#define	N2_SOC_DMUSIICREDIT_SHIFT	12
#define	N2_SOC_DMUCTAGUE_SHIFT		11
#define	N2_SOC_DMUCTAGCE_SHIFT		10
#define	N2_SOC_DMUNCUCREDIT_SHIFT	9
#define	N2_SOC_DMUINTERNAL_SHIFT	8

#define	N2_SOC_SIIDMUAPARITY_SHIFT	7
#define	N2_SOC_SIINIUDPARITY_SHIFT	6
#define	N2_SOC_SIIDMUDPARITY_SHIFT	5
#define	N2_SOC_SIINIUAPARITY_SHIFT	4
#define	N2_SOC_SIIDMUCTAGCE_SHIFT	3
#define	N2_SOC_SIINIUCTAGCE_SHIFT	2
#define	N2_SOC_SIIDMUCTAGUE_SHIFT	1
#define	N2_SOC_SIINIUCTAGUE_SHIFT	0

/*
 * System on Chip (SOC) MCU errors.
 *			 CPU   CLASS SUBCL  TRAP  PROT  MODE   ACCESS MISC
 */
#define	N2_HD_MCUECC	(NI2 | MCU | ECC  | DIS | CE  | HYPR | NA1  | NA0)
#define	N2_HD_MCUFBU	(NI2 | MCU | FBR  | DIS | UE  | HYPR | NA1  | NA0)
#define	N2_HD_MCUFBR	(NI2 | MCU | FBR  | DIS | CE  | HYPR | NA1  | NA0)

/*
 * System on Chip (SOC) Internal errors.
 *			 CPU   CLASS SUBCL  TRAP  PROT  MODE   ACCESS MISC
 */
#define	N2_IO_NIUDPAR	(NI2 | SOC | NIU  | FAT | PE  | DMA  | LOAD | NA0)
#define	N2_IO_NIUCTAGUE	(NI2 | SOC | NIU  | FAT | UE  | DMA  | LOAD | NA0)
#define	N2_IO_NIUCTAGCE	(NI2 | SOC | NIU  | DIS | CE  | DMA  | LOAD | NA0)

#define	N2_IO_SIOCTAGUE	(NI2 | SOC | SIO  | FAT | UE  | DMA  | LOAD | NA0)
#define	N2_IO_SIOCTAGCE	(NI2 | SOC | SIO  | DIS | CE  | DMA  | LOAD | NA0)

#define	N2_IO_NCUDMUC	(NI2 | SOC | NCUC | FAT | PE  | DMA  | LOAD | MPIO)
#define	N2_IO_NCUCTAGUE	(NI2 | SOC | NCUT | FAT | UE  | DMA  | LOAD | MPIO)
#define	N2_IO_NCUCTAGCE	(NI2 | SOC | NCUT | DIS | CE  | DMA  | LOAD | MPIO)
#define	N2_IO_NCUDMUUE	(NI2 | SOC | NCUD | FAT | UE  | DMA  | STOR | MPIO)
#define	N2_IO_NCUCPXUE	(NI2 | SOC | NCUC | FAT | UE  | DMA  | STOR | MPIO)
#define	N2_IO_NCUPCXUE	(NI2 | SOC | NCUP | FAT | UE  | DMA  | STOR | MPIO)
#define	N2_IO_NCUPCXD	(NI2 | SOC | NCUP | FAT | PE  | DMA  | STOR | MPIO)
#define	N2_IO_NCUINT	(NI2 | SOC | NCUI | FAT | UE  | DMA  | LOAD | MPIO)
#define	N2_IO_NCUMONDOF	(NI2 | SOC | NCUM | FAT | UE  | DMA  | LOAD | MPIO)
#define	N2_IO_NCUMONDOT	(NI2 | SOC | NCUM | FAT | PE  | DMA  | LOAD | MPIO)
#define	N2_IO_NCUDPAR	(NI2 | SOC | NCUD | FAT | PE  | DMA  | LOAD | MPIO)

#define	N2_IO_DMUDPAR	(NI2 | SOC | DMUD | FAT | PE  | DMA  | LOAD | NA0)
#define	N2_IO_DMUSIIC	(NI2 | SOC | DMUS | FAT | PE  | DMA  | LOAD | NA0)
#define	N2_IO_DMUCTAGUE	(NI2 | SOC | DMUT | FAT | UE  | DMA  | LOAD | NA0)
#define	N2_IO_DMUCTAGCE	(NI2 | SOC | DMUT | DIS | CE  | DMA  | LOAD | NA0)
#define	N2_IO_DMUNCUC	(NI2 | SOC | DMUN | FAT | PE  | DMA  | NA1  | NA0)
#define	N2_IO_DMUINT	(NI2 | SOC | DMUI | FAT | NA3 | DMA  | NA1  | NA0)

#define	N2_IO_SIIDMUAP	(NI2 | SOC | SIID | FAT | APE | DMA  | LOAD | NA0)
#define	N2_IO_SIIDMUDP	(NI2 | SOC | SIID | FAT | PE  | DMA  | LOAD | NA0)
#define	N2_IO_SIINIUAP	(NI2 | SOC | SIIN | FAT | APE | DMA  | LOAD | NA0)
#define	N2_IO_SIINIUDP	(NI2 | SOC | SIIN | FAT | PE  | DMA  | LOAD | NA0)

#define	N2_IO_SIIDMUCTU	(NI2 | SOC | SIID | FAT | UE  | DMA  | LOAD | MPIO)
#define	N2_IO_SIIDMUCTC	(NI2 | SOC | SIID | FAT | CE  | DMA  | LOAD | MPIO)
#define	N2_IO_SIINIUCTU	(NI2 | SOC | SIIN | FAT | UE  | DMA  | LOAD | NA0)
#define	N2_IO_SIINIUCTC	(NI2 | SOC | SIIN | FAT | CE  | DMA  | LOAD | NA0)

/*
 * SSI (BootROM interface) errors.
 *
 * Note that these errors are defined by Niagara-I in memtestio_ni.h.
 *			 CPU   CLASS SUBCL  TRAP  PROT  MODE  ACCESS  MISC
 */

/*
 * DEBUG test case(s) to ensure injector and system are behaving.
 *			 CPU   CLASS  SUBCL  TRAP  PROT  MODE   ACCESS MISC
 */
#define	N2_TEST		(NI2 | UTIL | NA5  | NA4 | NA3 | NA2  | NA1  | NA0)
#define	N2_PRINT_ESRS	(NI2 | UTIL | NA5  | NA4 | NA3 | NA2  | LOAD | NA0)
#define	N2_CLEAR_ESRS	(NI2 | UTIL | NA5  | NA4 | NA3 | NA2  | STOR | NA0)

#ifdef	__cplusplus
}
#endif

#endif	/* _MEMTESTIO_N2_H */
