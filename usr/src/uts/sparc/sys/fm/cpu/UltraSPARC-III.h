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
 * Copyright (c) 2004, 2006, Oracle and/or its affiliates. All rights reserved.
 */

#ifndef	_SYS_FM_ULTRASPARC_III_H
#define	_SYS_FM_ULTRASPARC_III_H

#ifdef	__cplusplus
extern "C" {
#endif

/* Ereport class subcategories for UltraSPARC III and IV families */
#define	FM_EREPORT_CPU_USIII		"ultraSPARC-III"
#define	FM_EREPORT_CPU_USIIIplus	"ultraSPARC-IIIplus"
#define	FM_EREPORT_CPU_USIIIi		"ultraSPARC-IIIi"
#define	FM_EREPORT_CPU_USIIIiplus	"ultraSPARC-IIIiplus"
#define	FM_EREPORT_CPU_USIV		"ultraSPARC-IV"
#define	FM_EREPORT_CPU_USIVplus		"ultraSPARC-IVplus"
#define	FM_EREPORT_CPU_UNSUPPORTED	"unsupported"

/*
 * Ereport payload definitions.
 */
#define	FM_EREPORT_PAYLOAD_NAME_AFSR		"afsr"
#define	FM_EREPORT_PAYLOAD_NAME_AFAR		"afar"
#define	FM_EREPORT_PAYLOAD_NAME_AFAR_STATUS	"afar-status"
#define	FM_EREPORT_PAYLOAD_NAME_PC		"pc"
#define	FM_EREPORT_PAYLOAD_NAME_TL		"tl"
#define	FM_EREPORT_PAYLOAD_NAME_TT		"tt"
#define	FM_EREPORT_PAYLOAD_NAME_PRIV		"privileged"
#define	FM_EREPORT_PAYLOAD_NAME_ME		"multiple"
#define	FM_EREPORT_PAYLOAD_NAME_SYND		"syndrome"
#define	FM_EREPORT_PAYLOAD_NAME_SYND_STATUS	"syndrome-status"
#define	FM_EREPORT_PAYLOAD_NAME_EMU_EMR_SIZE	"emu-mask-size"
#define	FM_EREPORT_PAYLOAD_NAME_EMU_EMR_DATA	"emu-mask-data"
#define	FM_EREPORT_PAYLOAD_NAME_EMU_ESR_SIZE	"emu-shadow-size"
#define	FM_EREPORT_PAYLOAD_NAME_EMU_ESR_DATA	"emu-shadow-data"
#define	FM_EREPORT_PAYLOAD_NAME_L2_WAYS		"l2-cache-ways"
#define	FM_EREPORT_PAYLOAD_NAME_L2_DATA		"l2-cache-data"
#define	FM_EREPORT_PAYLOAD_NAME_L3_WAYS		"l3-cache-ways"
#define	FM_EREPORT_PAYLOAD_NAME_L3_DATA		"l3-cache-data"
#define	FM_EREPORT_PAYLOAD_NAME_L1D_WAYS	"dcache-ways"
#define	FM_EREPORT_PAYLOAD_NAME_L1D_DATA	"dcache-data"
#define	FM_EREPORT_PAYLOAD_NAME_L1I_WAYS	"icache-ways"
#define	FM_EREPORT_PAYLOAD_NAME_L1I_DATA	"icache-data"
#define	FM_EREPORT_PAYLOAD_NAME_L1P_WAYS	"pcache-ways"
#define	FM_EREPORT_PAYLOAD_NAME_L1P_DATA	"pcache-data"
#define	FM_EREPORT_PAYLOAD_NAME_ITLB_ENTRIES	"itlb-entries"
#define	FM_EREPORT_PAYLOAD_NAME_ITLB_DATA	"itlb-data"
#define	FM_EREPORT_PAYLOAD_NAME_DTLB_ENTRIES	"dtlb-entries"
#define	FM_EREPORT_PAYLOAD_NAME_DTLB_DATA	"dtlb-data"
#define	FM_EREPORT_PAYLOAD_NAME_ERR_TYPE	"error-type"
#define	FM_EREPORT_PAYLOAD_NAME_RESOURCE	"resource"
#define	FM_EREPORT_PAYLOAD_NAME_VA		"va"
#define	FM_EREPORT_PAYLOAD_NAME_AFSR_EXT	"afsr-ext"
#define	FM_EREPORT_PAYLOAD_NAME_COPYFUNCTION	"copy-function"
#define	FM_EREPORT_PAYLOAD_NAME_INSTRBLOCK	"instr-block"
#define	FM_EREPORT_PAYLOAD_NAME_HOWDETECTED	"how-detected"
#define	FM_EREPORT_PAYLOAD_NAME_ERR_DISP	"error-disposition"

#define	FM_EREPORT_PAYLOAD_FLAG_AFSR		0x0000000000000001
#define	FM_EREPORT_PAYLOAD_FLAG_AFAR_STATUS	0x0000000000000002
#define	FM_EREPORT_PAYLOAD_FLAG_AFAR		0x0000000000000004
#define	FM_EREPORT_PAYLOAD_FLAG_PC		0x0000000000000008
#define	FM_EREPORT_PAYLOAD_FLAG_TL		0x0000000000000010
#define	FM_EREPORT_PAYLOAD_FLAG_TT		0x0000000000000020
#define	FM_EREPORT_PAYLOAD_FLAG_PRIV		0x0000000000000040
#define	FM_EREPORT_PAYLOAD_FLAG_ME		0x0000000000000080
#define	FM_EREPORT_PAYLOAD_FLAG_SYND		0x0000000000000100
#define	FM_EREPORT_PAYLOAD_FLAG_SYND_STATUS	0x0000000000000200
#define	FM_EREPORT_PAYLOAD_FLAG_EMU_EMR_SIZE	0x0000000000000400
#define	FM_EREPORT_PAYLOAD_FLAG_EMU_EMR_DATA	0x0000000000000800
#define	FM_EREPORT_PAYLOAD_FLAG_EMU_ESR_SIZE	0x0000000000001000
#define	FM_EREPORT_PAYLOAD_FLAG_EMU_ESR_DATA	0x0000000000002000
#define	FM_EREPORT_PAYLOAD_FLAG_L2_WAYS		0x0000000000004000
#define	FM_EREPORT_PAYLOAD_FLAG_L2_DATA		0x0000000000008000
#define	FM_EREPORT_PAYLOAD_FLAG_L1D_WAYS	0x0000000000010000
#define	FM_EREPORT_PAYLOAD_FLAG_L1D_DATA	0x0000000000020000
#define	FM_EREPORT_PAYLOAD_FLAG_L1I_WAYS	0x0000000000040000
#define	FM_EREPORT_PAYLOAD_FLAG_L1I_DATA	0x0000000000080000
#define	FM_EREPORT_PAYLOAD_FLAG_ERR_TYPE	0x0000000000100000
#define	FM_EREPORT_PAYLOAD_FLAG_RESOURCE	0x0000000000200000
#define	FM_EREPORT_PAYLOAD_FLAG_AFSR_EXT	0x0000000000400000
#define	FM_EREPORT_PAYLOAD_FLAG_L1P_WAYS	0x0000000000800000
#define	FM_EREPORT_PAYLOAD_FLAG_L1P_DATA	0x0000000001000000
#define	FM_EREPORT_PAYLOAD_FLAG_ITLB_ENTRIES	0x0000000002000000
#define	FM_EREPORT_PAYLOAD_FLAG_ITLB_DATA	0x0000000004000000
#define	FM_EREPORT_PAYLOAD_FLAG_DTLB_ENTRIES	0x0000000008000000
#define	FM_EREPORT_PAYLOAD_FLAG_DTLB_DATA	0x0000000010000000
#define	FM_EREPORT_PAYLOAD_FLAG_FAULT_VA	0x0000000020000000
#define	FM_EREPORT_PAYLOAD_FLAG_L3_WAYS		0x0000000040000000
#define	FM_EREPORT_PAYLOAD_FLAG_L3_DATA		0x0000000080000000
#define	FM_EREPORT_PAYLOAD_FLAG_COPYFUNCTION	0x0000000100000000
#define	FM_EREPORT_PAYLOAD_FLAG_INSTRBLOCK	0x0000000200000000
#define	FM_EREPORT_PAYLOAD_FLAG_HOWDETECTED	0x0000000400000000
#define	FM_EREPORT_PAYLOAD_FLAG_ERR_DISP	0x0000000800000000

#define	FM_EREPORT_PAYLOAD_FLAGS_AFAR \
				(FM_EREPORT_PAYLOAD_FLAG_AFAR | \
				    FM_EREPORT_PAYLOAD_FLAG_AFAR_STATUS)
#define	FM_EREPORT_PAYLOAD_FLAGS_TRAP \
				(FM_EREPORT_PAYLOAD_FLAG_TL | \
				    FM_EREPORT_PAYLOAD_FLAG_TT)
#define	FM_EREPORT_PAYLOAD_FLAGS_SYND \
				(FM_EREPORT_PAYLOAD_FLAG_SYND | \
				    FM_EREPORT_PAYLOAD_FLAG_SYND_STATUS)
#define	FM_EREPORT_PAYLOAD_FLAGS_EMU \
				(FM_EREPORT_PAYLOAD_FLAG_EMU_EMR_SIZE | \
				    FM_EREPORT_PAYLOAD_FLAG_EMU_EMR_DATA | \
				    FM_EREPORT_PAYLOAD_FLAG_EMU_ESR_SIZE | \
				    FM_EREPORT_PAYLOAD_FLAG_EMU_ESR_DATA)
#define	FM_EREPORT_PAYLOAD_FLAGS_L2 \
				(FM_EREPORT_PAYLOAD_FLAG_L2_WAYS | \
				    FM_EREPORT_PAYLOAD_FLAG_L2_DATA)
#define	FM_EREPORT_PAYLOAD_FLAGS_L3 \
				(FM_EREPORT_PAYLOAD_FLAG_L3_WAYS | \
				    FM_EREPORT_PAYLOAD_FLAG_L3_DATA)
#define	FM_EREPORT_PAYLOAD_FLAGS_L1D \
				(FM_EREPORT_PAYLOAD_FLAG_L1D_WAYS | \
				    FM_EREPORT_PAYLOAD_FLAG_L1D_DATA)
#define	FM_EREPORT_PAYLOAD_FLAGS_L1I \
				(FM_EREPORT_PAYLOAD_FLAG_L1I_WAYS | \
				    FM_EREPORT_PAYLOAD_FLAG_L1I_DATA)
#define	FM_EREPORT_PAYLOAD_FLAGS_L1P \
				(FM_EREPORT_PAYLOAD_FLAG_L1P_WAYS | \
				    FM_EREPORT_PAYLOAD_FLAG_L1P_DATA)
#define	FM_EREPORT_PAYLOAD_FLAGS_L1 \
				(FM_EREPORT_PAYLOAD_FLAGS_L1D | \
				    FM_EREPORT_PAYLOAD_FLAGS_L1I)
#define	FM_EREPORT_PAYLOAD_FLAGS_L1L2 \
				(FM_EREPORT_PAYLOAD_FLAGS_L1 | \
				    FM_EREPORT_PAYLOAD_FLAGS_L2)
#define	FM_EREPORT_PAYLOAD_FLAGS_ITLB \
				(FM_EREPORT_PAYLOAD_FLAG_ITLB_ENTRIES | \
				    FM_EREPORT_PAYLOAD_FLAG_ITLB_DATA)
#define	FM_EREPORT_PAYLOAD_FLAGS_DTLB \
				(FM_EREPORT_PAYLOAD_FLAG_DTLB_ENTRIES | \
				    FM_EREPORT_PAYLOAD_FLAG_DTLB_DATA)
#define	FM_EREPORT_PAYLOAD_FLAGS_TLB \
				(FM_EREPORT_PAYLOAD_FLAGS_ITLB | \
				    FM_EREPORT_PAYLOAD_FLAGS_DTLB)
#define	FM_EREPORT_PAYLOAD_FLAG_AFSRS \
				(FM_EREPORT_PAYLOAD_FLAG_AFSR | \
				    FM_EREPORT_PAYLOAD_FLAG_AFSR_EXT)


#define	FM_EREPORT_PAYLOAD_UNKNOWN	0
#define	FM_EREPORT_PAYLOAD_INVALID_AFSR	(FM_EREPORT_PAYLOAD_FLAG_AFSRS | \
					    FM_EREPORT_PAYLOAD_FLAGS_AFAR | \
					    FM_EREPORT_PAYLOAD_FLAG_PC | \
					    FM_EREPORT_PAYLOAD_FLAGS_TRAP | \
					    FM_EREPORT_PAYLOAD_FLAG_PRIV | \
					    FM_EREPORT_PAYLOAD_FLAG_ME)
#define	FM_EREPORT_PAYLOAD_SYSTEM1	 (FM_EREPORT_PAYLOAD_FLAG_AFSRS | \
					    FM_EREPORT_PAYLOAD_FLAG_PC | \
					    FM_EREPORT_PAYLOAD_FLAGS_TRAP | \
					    FM_EREPORT_PAYLOAD_FLAG_PRIV | \
					    FM_EREPORT_PAYLOAD_FLAG_ME)
#define	FM_EREPORT_PAYLOAD_SYSTEM2	 (FM_EREPORT_PAYLOAD_FLAG_AFSRS | \
					    FM_EREPORT_PAYLOAD_FLAGS_AFAR | \
					    FM_EREPORT_PAYLOAD_FLAG_PC | \
					    FM_EREPORT_PAYLOAD_FLAGS_TRAP | \
					    FM_EREPORT_PAYLOAD_FLAG_PRIV | \
					    FM_EREPORT_PAYLOAD_FLAG_ME | \
					    FM_EREPORT_PAYLOAD_FLAGS_EMU)
#define	FM_EREPORT_PAYLOAD_SYSTEM3	 (FM_EREPORT_PAYLOAD_FLAG_AFSR | \
					    FM_EREPORT_PAYLOAD_FLAGS_AFAR | \
					    FM_EREPORT_PAYLOAD_FLAG_PC | \
					    FM_EREPORT_PAYLOAD_FLAGS_TRAP | \
					    FM_EREPORT_PAYLOAD_FLAG_PRIV | \
					    FM_EREPORT_PAYLOAD_FLAG_ME | \
					    FM_EREPORT_PAYLOAD_FLAGS_L2)
#define	FM_EREPORT_PAYLOAD_IO		(FM_EREPORT_PAYLOAD_FLAG_AFSR | \
					    FM_EREPORT_PAYLOAD_FLAGS_AFAR | \
					    FM_EREPORT_PAYLOAD_FLAG_PC | \
					    FM_EREPORT_PAYLOAD_FLAGS_TRAP | \
					    FM_EREPORT_PAYLOAD_FLAG_PRIV | \
					    FM_EREPORT_PAYLOAD_FLAG_ME)
#define	FM_EREPORT_PAYLOAD_L2_TAG_PE	(FM_EREPORT_PAYLOAD_FLAG_AFSR | \
					    FM_EREPORT_PAYLOAD_FLAGS_AFAR | \
					    FM_EREPORT_PAYLOAD_FLAG_PC | \
					    FM_EREPORT_PAYLOAD_FLAGS_TRAP | \
					    FM_EREPORT_PAYLOAD_FLAG_PRIV | \
					    FM_EREPORT_PAYLOAD_FLAG_ME | \
					    FM_EREPORT_PAYLOAD_FLAGS_L1L2 | \
					    FM_EREPORT_PAYLOAD_FLAG_RESOURCE)
#define	FM_EREPORT_PAYLOAD_L2_TAG_ECC	(FM_EREPORT_PAYLOAD_FLAG_AFSRS | \
					    FM_EREPORT_PAYLOAD_FLAGS_AFAR | \
					    FM_EREPORT_PAYLOAD_FLAG_PC | \
					    FM_EREPORT_PAYLOAD_FLAGS_TRAP | \
					    FM_EREPORT_PAYLOAD_FLAG_PRIV | \
					    FM_EREPORT_PAYLOAD_FLAG_ME | \
					    FM_EREPORT_PAYLOAD_FLAGS_L1L2 | \
					    FM_EREPORT_PAYLOAD_FLAG_RESOURCE | \
					    FM_EREPORT_PAYLOAD_FLAGS_L3)
#define	FM_EREPORT_PAYLOAD_L3_TAG_ECC	(FM_EREPORT_PAYLOAD_FLAG_AFSRS | \
					    FM_EREPORT_PAYLOAD_FLAGS_AFAR | \
					    FM_EREPORT_PAYLOAD_FLAG_PC | \
					    FM_EREPORT_PAYLOAD_FLAGS_TRAP | \
					    FM_EREPORT_PAYLOAD_FLAG_PRIV | \
					    FM_EREPORT_PAYLOAD_FLAG_ME | \
					    FM_EREPORT_PAYLOAD_FLAGS_L1L2 | \
					    FM_EREPORT_PAYLOAD_FLAG_RESOURCE | \
					    FM_EREPORT_PAYLOAD_FLAGS_L3)
#define	FM_EREPORT_PAYLOAD_L2_DATA	(FM_EREPORT_PAYLOAD_FLAG_AFSRS | \
					    FM_EREPORT_PAYLOAD_FLAGS_AFAR | \
					    FM_EREPORT_PAYLOAD_FLAG_PC | \
					    FM_EREPORT_PAYLOAD_FLAGS_TRAP | \
					    FM_EREPORT_PAYLOAD_FLAG_PRIV | \
					    FM_EREPORT_PAYLOAD_FLAG_ME | \
					    FM_EREPORT_PAYLOAD_FLAGS_SYND | \
					    FM_EREPORT_PAYLOAD_FLAGS_L1L2 | \
					    FM_EREPORT_PAYLOAD_FLAG_RESOURCE | \
					    FM_EREPORT_PAYLOAD_FLAGS_L3)
#define	FM_EREPORT_PAYLOAD_L3_DATA	(FM_EREPORT_PAYLOAD_FLAG_AFSRS | \
					    FM_EREPORT_PAYLOAD_FLAGS_AFAR | \
					    FM_EREPORT_PAYLOAD_FLAG_PC | \
					    FM_EREPORT_PAYLOAD_FLAGS_TRAP | \
					    FM_EREPORT_PAYLOAD_FLAG_PRIV | \
					    FM_EREPORT_PAYLOAD_FLAG_ME | \
					    FM_EREPORT_PAYLOAD_FLAGS_SYND | \
					    FM_EREPORT_PAYLOAD_FLAGS_L1L2 | \
					    FM_EREPORT_PAYLOAD_FLAG_RESOURCE | \
					    FM_EREPORT_PAYLOAD_FLAGS_L3)
#define	FM_EREPORT_PAYLOAD_MEMORY	(FM_EREPORT_PAYLOAD_FLAG_AFSRS | \
					    FM_EREPORT_PAYLOAD_FLAGS_AFAR | \
					    FM_EREPORT_PAYLOAD_FLAG_PC | \
					    FM_EREPORT_PAYLOAD_FLAGS_TRAP | \
					    FM_EREPORT_PAYLOAD_FLAG_PRIV | \
					    FM_EREPORT_PAYLOAD_FLAG_ME | \
					    FM_EREPORT_PAYLOAD_FLAGS_SYND | \
					    FM_EREPORT_PAYLOAD_FLAG_ERR_TYPE | \
					    FM_EREPORT_PAYLOAD_FLAG_ERR_DISP | \
					    FM_EREPORT_PAYLOAD_FLAGS_L1L2 | \
					    FM_EREPORT_PAYLOAD_FLAG_RESOURCE | \
					    FM_EREPORT_PAYLOAD_FLAGS_L3)
#define	FM_EREPORT_PAYLOAD_ICACHE_PE	(FM_EREPORT_PAYLOAD_FLAG_PC | \
					    FM_EREPORT_PAYLOAD_FLAGS_TRAP | \
					    FM_EREPORT_PAYLOAD_FLAG_PRIV | \
					    FM_EREPORT_PAYLOAD_FLAGS_L1I| \
					    FM_EREPORT_PAYLOAD_FLAG_RESOURCE)
#define	FM_EREPORT_PAYLOAD_DCACHE_PE	(FM_EREPORT_PAYLOAD_FLAG_PC | \
					    FM_EREPORT_PAYLOAD_FLAGS_TRAP | \
					    FM_EREPORT_PAYLOAD_FLAG_PRIV | \
					    FM_EREPORT_PAYLOAD_FLAGS_L1D| \
					    FM_EREPORT_PAYLOAD_FLAG_RESOURCE)
#define	FM_EREPORT_PAYLOAD_PCACHE_PE	(FM_EREPORT_PAYLOAD_FLAG_PC | \
					    FM_EREPORT_PAYLOAD_FLAGS_TRAP | \
					    FM_EREPORT_PAYLOAD_FLAG_PRIV | \
					    FM_EREPORT_PAYLOAD_FLAGS_L1P| \
					    FM_EREPORT_PAYLOAD_FLAG_RESOURCE)
#define	FM_EREPORT_PAYLOAD_ITLB_PE	(FM_EREPORT_PAYLOAD_FLAG_PC | \
					    FM_EREPORT_PAYLOAD_FLAGS_TRAP | \
					    FM_EREPORT_PAYLOAD_FLAG_PRIV | \
					    FM_EREPORT_PAYLOAD_FLAGS_ITLB| \
					    FM_EREPORT_PAYLOAD_FLAG_RESOURCE)
#define	FM_EREPORT_PAYLOAD_DTLB_PE	(FM_EREPORT_PAYLOAD_FLAG_PC | \
					    FM_EREPORT_PAYLOAD_FLAGS_TRAP | \
					    FM_EREPORT_PAYLOAD_FLAG_PRIV | \
					    FM_EREPORT_PAYLOAD_FLAGS_DTLB | \
					    FM_EREPORT_PAYLOAD_FLAG_FAULT_VA| \
					    FM_EREPORT_PAYLOAD_FLAG_RESOURCE)
#define	FM_EREPORT_PAYLOAD_FPU_HWCOPY	(FM_EREPORT_PAYLOAD_FLAG_COPYFUNCTION |\
					    FM_EREPORT_PAYLOAD_FLAG_INSTRBLOCK|\
					    FM_EREPORT_PAYLOAD_FLAG_RESOURCE | \
					    FM_EREPORT_PAYLOAD_FLAG_HOWDETECTED)
/*
 * FM_EREPORT_PAYLOAD_UNKNOWN
 */
#define	FM_EREPORT_CPU_USIII_UNKNOWN		"unknown"

/*
 * FM_EREPORT_PAYLOAD_INVALID_AFSR
 */
#define	FM_EREPORT_CPU_USIII_INVALID_AFSR	"invalid-afsr"

/*
 * FM_EREPORT_PAYLOAD_SYSTEM1
 */
#define	FM_EREPORT_CPU_USIII_IVC		"ivc"
#define	FM_EREPORT_CPU_USIII_IVU		"ivu"
#define	FM_EREPORT_CPU_USIII_IMC		"imc"
#define	FM_EREPORT_CPU_USIII_IMU		"imu"
#define	FM_EREPORT_CPU_USIII_JETO		"jeto"
#define	FM_EREPORT_CPU_USIII_SCE		"sce"
#define	FM_EREPORT_CPU_USIII_JEIC		"jeic"
#define	FM_EREPORT_CPU_USIII_JEIT		"jeit"
#define	FM_EREPORT_CPU_USIII_JEIS		"jeis"
#define	FM_EREPORT_CPU_USIII_ISAP		"isap"
#define	FM_EREPORT_CPU_USIII_IVPE		"ivpe"

/*
 * FM_EREPORT_PAYLOAD_SYSTEM2
 */
#define	FM_EREPORT_CPU_USIII_PERR		"perr"
#define	FM_EREPORT_CPU_USIII_IERR		"ierr"

/*
 * FM_EREPORT_PAYLOAD_SYSTEM3
 */
#define	FM_EREPORT_CPU_USIII_BP			"bp"
#define	FM_EREPORT_CPU_USIII_WBP		"wbp"

/*
 * FM_EREPORT_PAYLOAD_IO
 */
#define	FM_EREPORT_CPU_USIII_TO			"to"
#define	FM_EREPORT_CPU_USIII_BERR		"berr"
#define	FM_EREPORT_CPU_USIII_DTO		"dto"
#define	FM_EREPORT_CPU_USIII_DBERR		"dberr"
#define	FM_EREPORT_CPU_USIII_OM			"om"
#define	FM_EREPORT_CPU_USIII_UMS		"ums"

/*
 * FM_EREPORT_PAYLOAD_L2_TAG_PE
 */
#define	FM_EREPORT_CPU_USIII_ETP		"etp"

/*
 * FM_EREPORT_PAYLOAD_L2_TAG_ECC
 */
#define	FM_EREPORT_CPU_USIII_THCE		"thce"
#define	FM_EREPORT_CPU_USIII_TSCE		"tsce"
#define	FM_EREPORT_CPU_USIII_TUE		"tue"
#define	FM_EREPORT_CPU_USIII_TUE_SH		"tue-sh"
#define	FM_EREPORT_CPU_USIII_ETU		"etu"
#define	FM_EREPORT_CPU_USIII_ETC		"etc"
#define	FM_EREPORT_CPU_USIII_ETI		"eti"
#define	FM_EREPORT_CPU_USIII_ETS		"ets"

/*
 * FM_EREPORT_PAYLOAD_L3_TAG_ECC
 */
#define	FM_EREPORT_CPU_USIII_L3_THCE		"l3-thce"
#define	FM_EREPORT_CPU_USIII_L3_TUE		"l3-tue"
#define	FM_EREPORT_CPU_USIII_L3_TUE_SH		"l3-tue-sh"

/*
 * FM_EREPORT_PAYLOAD_L2_DATA
 */
#define	FM_EREPORT_CPU_USIII_UCC		"ucc"
#define	FM_EREPORT_CPU_USIII_UCU		"ucu"
#define	FM_EREPORT_CPU_USIII_CPC		"cpc"
#define	FM_EREPORT_CPU_USIII_CPU		"cpu"
#define	FM_EREPORT_CPU_USIII_WDC		"wdc"
#define	FM_EREPORT_CPU_USIII_WDU		"wdu"
#define	FM_EREPORT_CPU_USIII_EDC		"edc"
#define	FM_EREPORT_CPU_USIII_EDUBL		"edu-bl"
#define	FM_EREPORT_CPU_USIII_EDUST		"edu-st"

/*
 * FM_EREPORT_PAYLOAD_L3_DATA
 */
#define	FM_EREPORT_CPU_USIII_L3_UCC		"l3-ucc"
#define	FM_EREPORT_CPU_USIII_L3_UCU		"l3-ucu"
#define	FM_EREPORT_CPU_USIII_L3_CPC		"l3-cpc"
#define	FM_EREPORT_CPU_USIII_L3_CPU		"l3-cpu"
#define	FM_EREPORT_CPU_USIII_L3_WDC		"l3-wdc"
#define	FM_EREPORT_CPU_USIII_L3_WDU		"l3-wdu"
#define	FM_EREPORT_CPU_USIII_L3_EDC		"l3-edc"
#define	FM_EREPORT_CPU_USIII_L3_EDUBL		"l3-edu-bl"
#define	FM_EREPORT_CPU_USIII_L3_EDUST		"l3-edu-st"
#define	FM_EREPORT_CPU_USIII_L3_MECC		"l3-mecc"

/*
 * FM_EREPORT_PAYLOAD_MEMORY
 */
#define	FM_EREPORT_CPU_USIII_CE			"ce"
#define	FM_EREPORT_CPU_USIII_RCE		"rce"
#define	FM_EREPORT_CPU_USIII_FRC		"frc"
#define	FM_EREPORT_CPU_USIII_EMC		"emc"
#define	FM_EREPORT_CPU_USIII_UE			"ue"
#define	FM_EREPORT_CPU_USIII_DUE		"due"
#define	FM_EREPORT_CPU_USIII_RUE		"rue"
#define	FM_EREPORT_CPU_USIII_FRU		"fru"
#define	FM_EREPORT_CPU_USIII_EMU		"emu"

/*
 * FM_EREPORT_PAYLOAD_ICACHE_PE
 */
#define	FM_EREPORT_CPU_USIII_IPE		"ipe"
#define	FM_EREPORT_CPU_USIII_IDSPE		"idspe"
#define	FM_EREPORT_CPU_USIII_ITSPE		"itspe"

/*
 * FM_EREPORT_PAYLOAD_DCACHE_PE
 */
#define	FM_EREPORT_CPU_USIII_DPE		"dpe"
#define	FM_EREPORT_CPU_USIII_DDSPE		"ddspe"
#define	FM_EREPORT_CPU_USIII_DTSPE		"dtspe"

/*
 * FM_EREPORT_PAYLOAD_PCACHE_PE
 */
#define	FM_EREPORT_CPU_USIII_PDSPE		"pdspe"


/*
 * FM_EREPORT_PAYLOAD_DTLB_PE
 */
#define	FM_EREPORT_CPU_USIII_DTLBPE		"dtlbpe"

/*
 * FM_EREPORT_PAYLOAD_ITLB_PE
 */
#define	FM_EREPORT_CPU_USIII_ITLBPE		"itlbpe"

/*
 * FM_EREPORT_PAYLOAD_FPU_HWCOPY
 */
#define	FM_EREPORT_CPU_USIII_FPU_HWCOPY		"fpu.hwcopy"

/*
 * Magic values for cache dump logflags.
 * These flags are used to indicate that the structures
 * defined in cheetahregs.h to capture cache data contain
 * valid information.
 */
#define	EC_LOGFLAG_MAGIC	0xEC0106F1A6	/* =~ EC_LOGFLAG */
#define	DC_LOGFLAG_MAGIC	0xDC0106F1A6	/* =~ DC_LOGFLAG */
#define	IC_LOGFLAG_MAGIC	0x1C0106F1A6	/* =~ IC_LOGFLAG */
#define	PC_LOGFLAG_MAGIC	0x9C0106F1A6	/* =~ PC_LOGFLAG */
#define	IT_LOGFLAG_MAGIC	0x170106F1A6	/* =~ IT_LOGFLAG */
#define	DT_LOGFLAG_MAGIC	0xD70106F1A6	/* =~ DT_LOGFLAG */


#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_FM_ULTRASPARC_III_H */
