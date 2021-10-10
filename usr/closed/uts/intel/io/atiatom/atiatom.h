/*
 * Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */
/*
 * Copyright 2001, ATI Technologies Inc. All rights reserved.
 * The material in this document constitutes an unpublished work
 * created in 2001. The use of this copyright notice is intended to
 * provide notice that ATI owns a copyright in this unpublished work.
 * The copyright notice is not an admission that publication has occurred.
 * This work contains confidential, proprietary information and trade
 * secrets of ATI. No part of this document may be used, reproduced,
 * or transmitted in any form or by any means without the prior
 * written permission of ATI Technologies Inc
 */


#ifndef	_SYS_ATIATOM_H
#define	_SYS_ATIATOM_H

#pragma ident	"%Z%%M%	%I%	%E% SMI"

#ifdef __cplusplus
extern "C" {
#endif

#define	MEM_BUF_CNTL_InvalidateRbCache	0x00800000
#define	LfbOffset		0x7ff800

/*
 * RageXL register definitions
 */
#define	CLOCK_CNTL_WR_EN		0x2

#define	CRTC_H_TOTAL_DISP		0x0000  /* Dword offset 00 */
#define	CRTC_H_SYNC_STRT_WID		0x0004  /* Dword offset 01 */
#define	CRTC_V_TOTAL_DISP		0x0008  /* Dword offset 02 */
#define	CRTC_V_SYNC_STRT_WID		0x000C  /* Dword offset 03 */
#define	CRTC_VLINE_CRNT_VLINE		0x0010  /* Dword offset 04 */
#define	CRTC_OFF_PITCH			0x0014  /* Dword offset 05 */
#define	CRTC_INT_CNTL			0x0018  /* Dword offset 06 */
#define	CRTC_GEN_CNTL			0x001C  /* Dword offset 07 */
#define	DSP_CONFIG			0x0020	/* Dword offset 08 */
#define	DSP_ON_OFF			0x0024	/* Dword offset 09 */
#define	TIMER_CONFIG			0x0028	/* Dword offset 0A */
#define	MEM_BUF_CNTL			0x002C	/* Dword offset 0B */
#define	SHARED_CNTL			0x0030	/* Dword offset 0C */
#define	SHARED_MEM_CONFIG		0x0034	/* Dword offset 0D */
#define	MEM_ADDR_CONFIG			0x0034	/* Dword offset 0D */
#define	CRT_TRAP			0x0038	/* Dword offset 0E */

#define	OVR_CLR				0x0040  /* Dword offset 10 */
#define	OVR_WID_LEFT_RIGHT		0x0044  /* Dword offset 11 */
#define	OVR_WID_TOP_BOTTOM		0x0048  /* Dword offset 12 */

#define	VGA_DSP_CONFIG			0x004c  /* Dword offset 13 */
#define	VGA_DSP_ON_0FF			0x0050  /* Dword offset 14 */

#define	CUR_CLR0			0x0060  /* Dword offset 18 */
#define	CUR_CLR1			0x0064  /* Dword offset 19 */
#define	CUR_OFFSET			0x0068  /* Dword offset 1A */
#define	CUR_HORZ_VERT_POSN		0x006C  /* Dword offset 1B */
#define	CUR_HORZ_VERT_OFF		0x0070  /* Dword offset 1C */

#define	GP_IO				0x0078  /* Dword offset 1E */
#define	HW_DEBUG			0x007C  /* Dword offset 1F */

#define	SCRATCH_REG0			0x0080  /* Dword offset 20 */
#define	SCRATCH_REG1			0x0084  /* Dword offset 21 */

#define	CLOCK_SEL_CNTL			0x0090  /* Dword offset 24 */
#define	CLOCK_CNTL0			0x0090	/* 1-byte registers */
#define	CLOCK_CNTL1			0x0091	/* TODO: big/little endian? */
#define	CLOCK_CNTL2			0x0092

#define	BUS_CNTL			0x00A0  /* Dword offset 28 */

#define	LCD_INDEX			0x00A4  /* Dword offset 29 */
#define	LCD_DATA			0x00A8  /* Dword offset 2A */

#define	EXT_MEM_CNTL			0x00AC  /* Dword offset 2B */
#define	MEM_CNTL			0x00B0  /* Dword offset 2C */

#define	MEM_VGA_WP_SEL			0x00B4  /* Dword offset 2D */
#define	MEM_VGA_RP_SEL			0x00B8  /* Dword offset 2E */

#define	DAC_REGS			0x00C0  /* Dword offset 30 */
#define	DAC_CNTL			0x00C4  /* Dword offset 31 */

#define	GEN_TEST_CNTL			0x00D0  /* Dword offset 34 */
#define	CONFIG_CNTL			0x00DC  /* Dword offset 37 */

#define	CONFIG_CHIP_ID			0x00E0  /* Dword offset 38 */
#define	CONFIG_STAT0			0x00E4  /* Dword offset 39 */
#define	CONFIG_STAT1			0x00E8  /* Dword offset 3A */
#define	CRC_SIG				0x00E8  /* Dword offset 3A */

#define	DST_OFF_PITCH			0x0100  /* Dword offset 40 */
#define	DST_X				0x0104  /* Dword offset 41 */
#define	DST_Y				0x0108  /* Dword offset 42 */
#define	DST_Y_X				0x010C  /* Dword offset 43 */
#define	DST_WIDTH			0x0110  /* Dword offset 44 */
#define	DST_HEIGHT			0x0114  /* Dword offset 45 */
#define	DST_HEIGHT_WIDTH		0x0118  /* Dword offset 46 */
#define	DST_X_WIDTH			0x011C  /* Dword offset 47 */
#define	DST_BRES_LNTH			0x0120  /* Dword offset 48 */
#define	DST_BRES_ERR			0x0124  /* Dword offset 49 */
#define	DST_BRES_INC			0x0128  /* Dword offset 4A */
#define	DST_BRES_DEC			0x012C  /* Dword offset 4B */
#define	DST_CNTL			0x0130  /* Dword offset 4C */

#define	SRC_OFF_PITCH			0x0180  /* Dword offset 60 */
#define	SRC_X				0x0184  /* Dword offset 61 */
#define	SRC_Y				0x0188  /* Dword offset 62 */
#define	SRC_Y_X				0x018C  /* Dword offset 63 */
#define	SRC_WIDTH1			0x0190  /* Dword offset 64 */
#define	SRC_HEIGHT1			0x0194  /* Dword offset 65 */
#define	SRC_HEIGHT1_WIDTH1		0x0198  /* Dword offset 66 */
#define	SRC_X_START			0x019C  /* Dword offset 67 */
#define	SRC_Y_START			0x01A0  /* Dword offset 68 */
#define	SRC_Y_X_START			0x01A4  /* Dword offset 69 */
#define	SRC_WIDTH2			0x01A8  /* Dword offset 6A */
#define	SRC_HEIGHT2			0x01AC  /* Dword offset 6B */
#define	SRC_HEIGHT2_WIDTH2		0x01B0  /* Dword offset 6C */
#define	SRC_CNTL			0x01B4  /* Dword offset 6D */

#define	HOST_DATA0			0x0200  /* Dword offset 80 */
#define	HOST_DATA1			0x0204  /* Dword offset 81 */
#define	HOST_DATA2			0x0208  /* Dword offset 82 */
#define	HOST_DATA3			0x020C  /* Dword offset 83 */
#define	HOST_DATA4			0x0210  /* Dword offset 84 */
#define	HOST_DATA5			0x0214  /* Dword offset 85 */
#define	HOST_DATA6			0x0218  /* Dword offset 86 */
#define	HOST_DATA7			0x021C  /* Dword offset 87 */
#define	HOST_DATA8			0x0220  /* Dword offset 88 */
#define	HOST_DATA9			0x0224  /* Dword offset 89 */
#define	HOST_DATAA			0x0228  /* Dword offset 8A */
#define	HOST_DATAB			0x022C  /* Dword offset 8B */
#define	HOST_DATAC			0x0230  /* Dword offset 8C */
#define	HOST_DATAD			0x0234  /* Dword offset 8D */
#define	HOST_DATAE			0x0238  /* Dword offset 8E */
#define	HOST_DATAF			0x023C  /* Dword offset 8F */
#define	HOST_CNTL			0x0240  /* Dword offset 90 */

#define	PAT_REG0			0x0280  /* Dword offset A0 */
#define	PAT_REG1			0x0284  /* Dword offset A1 */
#define	PAT_CNTL			0x0288  /* Dword offset A2 */

#define	SC_LEFT				0x02A0  /* Dword offset A8 */
#define	SC_RIGHT			0x02A4  /* Dword offset A9 */
#define	SC_LEFT_RIGHT			0x02A8  /* Dword offset AA */
#define	SC_TOP				0x02AC  /* Dword offset AB */
#define	SC_BOTTOM			0x02B0  /* Dword offset AC */
#define	SC_TOP_BOTTOM			0x02B4  /* Dword offset AD */

#define	DP_BKGD_CLR			0x02C0  /* Dword offset B0 */
#define	DP_FRGD_CLR			0x02C4  /* Dword offset B1 */
#define	DP_WRITE_MASK			0x02C8  /* Dword offset B2 */
#define	DP_CHAIN_MASK			0x02CC  /* Dword offset B3 */
#define	DP_PIX_WIDTH			0x02D0  /* Dword offset B4 */
#define	DP_MIX				0x02D4  /* Dword offset B5 */
#define	DP_SRC				0x02D8  /* Dword offset B6 */

#define	CLR_CMP_CLR			0x0300  /* Dword offset C0 */
#define	CLR_CMP_MASK			0x0304  /* Dword offset C1 */
#define	CLR_CMP_CNTL			0x0308  /* Dword offset C2 */

#define	FIFO_STAT			0x0310  /* Dword offset C4 */

#define	CONTEXT_MASK			0x0320  /* Dword offset C8 */
#define	CONTEXT_LOAD_CNTL		0x032C  /* Dword offset CB */

#define	GUI_TRAJ_CNTL			0x0330  /* Dword offset CC */
#define	GUI_STAT			0x0338  /* Dword offset CE */

/*
 * Opcodes for ScriptEngine
 */
#define	DELAY_IN_US				0x80
#define	DELAY_IN_MS				0x81
#define	INDEXED_REG_ACCESS			0x82
#define	DIRECT_REG_ACCESS			0x83
#define	PCI_CONFIG_ACCESS			0x84
#define	COMPARE_READ_AGAINST_VALUE		0x85
#define	COMPARE_TWO_READS			0x86
#define	JUMP_TO_NTH_BYTE			0x87
#define	JUMP_TO_NTH_BYTE_IF_EQUAL		0x88
#define	JUMP_TO_NTH_BYTE_IF_NOT_EQUAL		0x89
#define	JUMP_TO_NTH_BYTE_IF_LESS_THAN		0x8A
#define	JUMP_TO_NTH_BYTE_IF_GREATER_THAN	0x8B
#define	JUMP_TO_NTH_BYTE_IF_LESS_THAN_EQUAL	0x8C
#define	JUMP_TO_NTH_BYTE_IF_GREATER_THAN_EQUAL	0x8D
#define	CALL_FUNCTION_AT_NTH_BYTE		0x8E
#define	JUMP_RELATIVE				0x8F
#define	JUMP_RELATIVE_IF_EQUAL			0x90
#define	JUMP_RELATIVE_IF_NOT_EQUAL		0x91
#define	JUMP_RELATIVE_IF_LESS_THAN		0x92
#define	JUMP_RELATIVE_IF_GREATER_THAN		0x93
#define	JUMP_RELATIVE_IF_LESS_THAN_EQUAL	0x94
#define	JUMP_RELATIVE_IF_GREATER_THAN_EQUAL	0x95
#define	CALL_FUNCTION_AT_RELATIVE_OFFSET	0x96
#define	CALL_FUNCTION_RETURN			0x97
#define	JUMP_TO_ANOTHER_TABLE			0x98
#define	CALL_ANOTHER_TABLE			0x99
#define	CALL_TABLE_RETURN			0x9A
#define	PLL_REG_ACCESS				0x9B
#define	CALL_ASM_FUNCTION			0x9C
#define	CALL_FUNCTION_AT_BIOS_OFFSET		0x9D
#define	TEST_READ_AGAINST_VALUE			0x9E
#define	TEST_TWO_READS				0x9F
#define	CLEAR_ZERO_FLAG				0xA0
#define	SET_ZERO_FLAG				0xA1
#define	TEST_MEMORY_CONFIG			0xA2
#define	TEST_BEEP				0xA3
#define	TEST_NULL				0xA4
#define	TEST_STOP				0xA5
#define	POST_PORT80				0xA6
#define	TRACE_PORT80				0xA7
#define	TEST_BREAK				0xA8
#define	END_OF_TABLE				0xFF
#define	ARRAY_WRITE_OP				(0x00 << 5)
#define	ARRAY_SUB_OP				(0x01 << 5)
#define	ARRAY_OR_OP				(0x02 << 5)
#define	ARRAY_ADD_OP				(0x03 << 5)

typedef enum {
	REGX_LOWER_WORD = 0,	/* 0 - value corresponds to byte	*/
				/* ucAlignment in ULONG			*/
	REGX_MIDDLE_WORD,	/* 1 - ditto				*/
	REGX_UPPER_WORD,	/* 2 - ditto				*/
	REGX_DWORD,		/* 3 - full dword			*/
	REGX_BYTE0,		/* 4 - substract 4 for byte ucAlignment	*/
				/* in ULONG				*/
	REGX_BYTE1,		/* 5 - substract 4 for byte ucAlignment	*/
				/* in ULONG				*/
	REGX_BYTE2,		/* 6 - substract 4 for byte ucAlignment	*/
				/* in ULONG				*/
	REGX_BYTE3		/* 7 - substract 4 for byte ucAlignment	*/
				/* in ULONG				*/
} ALIGNMENT;

enum {
	ATTRIB_BYTE_PCI_IO = 0,		/* 0x00 */
	ATTRIB_BYTE_ARRAY_INDEX = 0,	/* 0x00 */
	ATTRIB_BYTE_DEST_ARRAY,		/* 0x01 */
	ATTRIB_BYTE_INDEX_BASE_IO,	/* 0x02 */
	ATTRIB_BYTE_DOS_DATA,		/* 0x03 */
	ATTRIB_WORD_MEM_MAP_0,		/* 0x04 */
	ATTRIB_WORD_MEM_MAP_1,		/* 0x05 */
	ATTRIB_WORD_IO_ADDRESS,		/* 0x06 */
	ATTRIB_WORD_BIOS_IMAGE,		/* 0x07 */
	ATTRIB_WORD_BUS_DEV_FCN = 0x40,	/* 0x40 */
	ATTRIB_PRIMARY_ONLY = 0x80	/* 0x80 */
};

typedef enum {
	EQUAL = 0,
	LESS_THAN,
	GREATER_THAN
} COMPAREFLAG;

typedef enum {
	T_BYTE = 0,
	T_WORD,
	T_DWORD
} T_TYPE;

typedef union {
	unsigned char	ucAttrib;
	unsigned short	usAttrib;
} RegisterAttribute;

typedef union _MEM_STORE {
	unsigned char	ucMem[4];
	unsigned short	usMem[2];
	unsigned int	ulMem;
} MEM_STORE;

typedef struct {
	MEM_STORE	mem_store[4];
	/* WorkArray */
	unsigned int	ulSavedValue;
	/* value from prev.read, to compare */

	unsigned char	ucAlignment;
	/* =opcode[2:0] for operat.commands in case of direct Rg access	*/
	unsigned char	ucIndexOfWS;	/* just for PCI config BUS DEV	*/
					/* FCN				*/
	/*
	 * loaded in scriptPciConfigAccess(TWorkSpace * pWS) . Dword3, upper
	 * word
	 */
	unsigned char	ucFlags;
	/* see Flagdefinition above */
	unsigned char	ucSpecialCmd;	/* keeps the sp. command code	*/
					/* from ExecuteScript		*/
	COMPAREFLAG	compareFlag;
	/*
	 * /equal etc.- see above pWS->bPrimaryAdapter || (!(pWS->ucIOMode &
	 * ATTRIB_PRIMARY_ONLY) ? 1 : 0);
	 */
	char		bScriptRunnable;
	char		bEndOfProcessing;
	/*
	 * set by Fcn ret.and eot
	 */
	unsigned char   ucIOMode;	/* holds attr. byte after */
					/* ucSpec.command ATTRIB_BYTE_PCI_IO, */
					/* ATTRIB_BYTE_DEST_ARRAY: 0x01 */
					/* ATTRIB_BYTE_INDEX_BASE_IO: // 0x02 */
					/* ATTRIB_BYTE_DOS_DATA: // 0x03 */
					/* ATTRIB_BYTE_MEM_MAP: // 0x04 */
					/* ATTRIB_WORD_MEM_MAP: // 0x05 */
					/* ATTRIB_WORD_IO_ADDRESS: // 0x06 */
					/* ATTRIB_WORD_BIOS_IMAGE: // 0x07 */
					/* ATTRIB_WORD_BUS_DEV_FCN: */
	unsigned char	ucNumberOfIndexBits;
	unsigned char	ucStartBitOfIndex;
	unsigned char	ucIndexAddress;
	unsigned char	ucDataAddress;
	unsigned short	usAndMask;
	unsigned short	usOrMask;
	unsigned short	usIOBaseAddress;

	unsigned char	*pucBIOSImage;
	unsigned char	*pucScriptTable;
	unsigned char	*pucScript;
	unsigned char	*pucCmdScript;
	unsigned char	*pucSeqNextScript;
	char		bPrimaryAdapter;
	struct atiatom_softc *softc;
} TWorkSpace;

/*
 * Definitions for procedures that implement ScriptEngine opcodes
 */
typedef void (*PARSERCALLBACK) (TWorkSpace * pWS);
static void scriptClearCmd(TWorkSpace * pWS);
static void scriptWriteCmd(TWorkSpace * pWS);
static void scriptSkewCmd(TWorkSpace * pWS);
static void scriptReadCmd(TWorkSpace * pWS);
static void scriptReadToMem(TWorkSpace * pWS);
static void scriptReadAndCompareFromMem(TWorkSpace * pWS);
static void scriptWriteFromMem(TWorkSpace * pWS);
static void scriptMaskWriteFromMem(TWorkSpace * pWS);
static void scriptSetBitsFromMem(TWorkSpace * pWS);
static void scriptClearBitsFromMem(TWorkSpace * pWS);
static void scriptMaskWriteCmd(TWorkSpace * pWS);
static void scriptSetBitsCmd(TWorkSpace * pWS);
static void scriptClearBitsCmd(TWorkSpace * pWS);
static void scriptBatchMaskWriteCmd(TWorkSpace * pWS);
static void scriptBatchWriteCmd(TWorkSpace * pWS);
static void scriptBatchClearCmd(TWorkSpace * pWS);
static void scriptDelayInUs(TWorkSpace * pWS);
static void scriptDelayInMs(TWorkSpace * pWS);
static void scriptIndexedRegAccess(TWorkSpace * pWS);
static void scriptDirectRegAccess(TWorkSpace * pWS);
static void scriptPciConfigAccess(TWorkSpace * pWS);
static void scriptCompareReadValue(TWorkSpace * pWS);
static void scriptCompareReads(TWorkSpace * pWS);
static void scriptJumpToNthByte(TWorkSpace * pWS);
static void scriptJumpToNthByteIfE(TWorkSpace * pWS);
static void scriptJumpToNthByteIfNE(TWorkSpace * pWS);
static void scriptJumpToNthByteIfLT(TWorkSpace * pWS);
static void scriptJumpToNthByteIfGT(TWorkSpace * pWS);
static void scriptJumpToNthByteIfLTE(TWorkSpace * pWS);
static void scriptJumpToNthByteIfGTE(TWorkSpace * pWS);
static void scriptCallFuncAtNthByte(TWorkSpace * pWS);
static void scriptJumpRelative(TWorkSpace * pWS);
static void scriptJumpRelativeIfE(TWorkSpace * pWS);
static void scriptJumpRelativeIfNE(TWorkSpace * pWS);
static void scriptJumpRelativeIfLT(TWorkSpace * pWS);
static void scriptJumpRelativeIfGT(TWorkSpace * pWS);
static void scriptJumpRelativeIfLTE(TWorkSpace * pWS);
static void scriptJumpRelativeIfGTE(TWorkSpace * pWS);
static void scriptCallFuncAtRelative(TWorkSpace * pWS);
static void scriptCallFunctionReturn(TWorkSpace * pWS);
static void scriptJumpToTable(TWorkSpace * pWS);
static void scriptCallTable(TWorkSpace * pWS);
static void scriptCallTableReturn(TWorkSpace * pWS);
static void scriptPllRegAccess(TWorkSpace * pWS);
static void scriptCallAsmFunction(TWorkSpace * pWS);
static void scriptCallFunctionAtBiosOffset(TWorkSpace * pWS);
static void scriptTestReadAgainstValue(TWorkSpace * pWS);
static void scriptTestTwoReads(TWorkSpace * pWS);
static void scriptClearZeroFlag(TWorkSpace * pWS);
static void scriptSetZeroFlag(TWorkSpace * pWS);
static void scriptTestMemConfig(TWorkSpace * pWS);
static void scriptBeep();
static void scriptNull();
static void scriptTestStop();
static void scriptPostPort80();
static void scriptTracePort80();
static void scriptTestBreak();

/*
 * Definitions for support procedures
 */
static unsigned char ucFilterBYTE(TWorkSpace * pWS);
static unsigned short usFilterWORD(TWorkSpace * pWS);
static unsigned int ulFilterDWORD(TWorkSpace * pWS);
static void vScanAttribute(TWorkSpace * pWS, RegisterAttribute * pAttr);
static unsigned int ulGetValue(TWorkSpace * pWS);
static void vCompareWithNextRead(TWorkSpace * pWS, unsigned int);
static void vCompareWithNextValue(TWorkSpace * pWS, unsigned int);
static void vComparePending(TWorkSpace * pWS, unsigned int);
static void vTestWithNextRead(TWorkSpace * pWS, unsigned int);
static void vTestWithNextValue(TWorkSpace * pWS, unsigned int);
static void vTestPending(TWorkSpace * pWS, unsigned int);
static unsigned char  *GetMemoryByPage(struct atiatom_softc *, unsigned int);
static char bIs32BitMemory(struct atiatom_softc *);
static char bTest64BitMemory(struct atiatom_softc *);
static char bTestMemoryWithPattern(struct atiatom_softc *, void *,
	unsigned char, unsigned int);
static char bTestPattern(struct atiatom_softc *, unsigned char, unsigned int);
static char bTestPackedPixel(struct atiatom_softc *, unsigned int ulMemPages);
static char bTestWrapAround(struct atiatom_softc *, unsigned int ulMemPages);

/*
 * Note: following are not used at present, but may
 * be needed for other atiatom-based graphics drivers
 *
 * static char bMapLfbAperture(struct atiatom_softc *);
 *
 * static unsigned int ulReadDataFromMemory(unsigned char *pIoAddress,
 *	unsigned char ucAlignment);
 *
 * static void vWriteDataToMemory(unsigned char *pIoAddress, unsigned char,
 *	unsigned int);
 *
 * static void vWriteDataToPLL(TWorkSpace * pWS, RegisterAttribute,
 *	unsigned int);
 *
 * static void DebugOut(TWorkSpace * pWS);
 *
 * static int		vpost[] = {1, 2, 4, 8, 3, 0, 6, 12};
 */

static char bTestMemConfig(struct atiatom_softc *, unsigned int ulMemPages);
static unsigned int ulReadDataFromIO(unsigned char *pIoAddress,
	unsigned char ucAlignment);
static unsigned int ulReadDataFromWorkSpace(TWorkSpace * pWS,
	unsigned char ucIndex, unsigned char ucAlignment);
static unsigned int ulReadDataFromIndexedRegister(TWorkSpace * pWS,
	RegisterAttribute attr);
static unsigned int ulReadDataFromPLL(TWorkSpace * pWS, RegisterAttribute attr);
static unsigned int ulReadDataFromPCIConfigSpace(TWorkSpace * pWS,
	RegisterAttribute attr);
static unsigned int ulReadDataFromDirectRegister(TWorkSpace * pWS,
	RegisterAttribute attr);
static unsigned int ulReadDataFrom(TWorkSpace * pWS, RegisterAttribute attr);
static void vWriteDataToIO(unsigned char *, unsigned char, unsigned int);
static void vWriteDataToWorkSpace(TWorkSpace * pWS, unsigned char,
	unsigned int);
static void vWriteDataToIndexedRegister(TWorkSpace * pWS,
	RegisterAttribute, unsigned int);
static void vWriteDataToPCIConfigSpace(TWorkSpace * pWS, RegisterAttribute,
	unsigned int);
static void vWriteDataToDirectRegister(TWorkSpace * pWS,
	RegisterAttribute, unsigned int);
static void vWriteDataTo(TWorkSpace * pWS, RegisterAttribute, unsigned int);
static void ExecuteScript(TWorkSpace * pWS);
static void vWriteDataToVGA(TWorkSpace * pWS, unsigned short, unsigned char,
	unsigned int);
static unsigned int ulReadDataFromVGA(TWorkSpace * pWS, unsigned short,
	unsigned char);

/* PLL REGISTER NAMES */
#define	REFCLK 14318L

#define	MPLL_CNTL	0
#define	VPLL_CNTL	1
#define	PLL_REF_DIV	2
#define	PLL_GEN_CNTL	3
#define	MCLK_FB_DIV	4
#define	PLL_VCLK_CNTL	5
#define	VCLK_POST_DIV	6
#define	VCLK0_FB_DIV	7
#define	VCLK1_FB_DIV	8
#define	VCLK2_FB_DIV	9
#define	VCLK3_FB_DIV	10
#define	PLL_XCLK_CNTL	11
#define	DLL_CNTL	12
#define	VFC_CNTL	13
#define	PLL_TEST_CNTL	14
#define	PLL_TEST_COUNT	15

#define	FREQ_DELTA	5
#ifndef min
#define	min(a, b)	((a) < (b) ? (a) : (b))
#define	max(a, b)	((a) > (b) ? (a) : (b))
#endif

#ifndef abs
#define	abs(a)		((a) >= 0 ? (a) : -(a))
#endif

/*
 * Not used at present, but may be needed to
 * support other atiatom-based graphics devices
 *
 * static uint_t get_clock_reg(struct atiatom_softc *, uint_t);
 */
static void set_clock_reg(struct atiatom_softc *, uint_t, uint_t);
#define	ATIREG2(reg, offset)	(*(uint32_t *)((reg)+(offset)))
#define	ATIREG(softc, offset)	ATIREG2(softc->registers, (offset))
#define	ATIREGB(softc, offset)	(*(uint8_t *)((softc->registers)+(offset)))
#define	ATIREGW(softc, offset)	(*(uint16_t *)((softc->registers)+(offset)))
#define	ATIREGL(softc, offset)	(*(uint32_t *)((softc->registers)+(offset)))
#define	DEBUGTRACE(n) ddi_io_put8(softc->regs.handle, (void *) 0x80, n)
#define	INHIBIT 0x80000000

/*
 * Procedures local to atiatom.c
 * (and their supporting typedefs)
 */

typedef enum {
	INIT_PLL = 0,
	INIT_EXTENDED_REGISTERS,
	INIT_MEMORY
} ScriptInitType;

typedef struct {
	unsigned short  RegAddress;
	unsigned int    RegContent;
} CXRegTable;

typedef struct {
	unsigned char   RegAddress;
	unsigned char   RegContent;
} PllRegTable;

static void	RageXLInitChip(struct atiatom_softc *);
static void	scriptNull();
static void	scriptTestStop();
static void	SetupScriptInitEntry(struct atiatom_softc *);
static int  	atiatom_map_vga_registers(struct atiatom_softc *);

static void	VGA_prog_regs(struct atiatom_softc *, int);
static void	move_bios(volatile unsigned int *, volatile unsigned int *);
static void	RunScriptEngine(struct atiatom_softc *, int);
static void	LoadFirst4K(struct atiatom_softc *);
static void	vInitPll(struct atiatom_softc *, PllRegTable *, int);
static void	NoBiosPowerOnAdapter(struct atiatom_softc *);
static void	vLoadInitBlock(struct atiatom_softc *, CXRegTable *, int);
static void	VideoPortMoveMemory(void *, void *, unsigned int);
static unsigned char	VideoPortReadRegisterUchar(unsigned char *);
static unsigned short	VideoPortReadRegisterUshort(unsigned short *);
static unsigned int	VideoPortReadRegisterUint(unsigned int *);
static void	VideoPortWriteRegisterUchar(unsigned char *, unsigned char);
static void	VideoPortWriteRegisterUshort(unsigned short *, unsigned short);
static void	VideoPortWriteRegisterUint(unsigned int *, unsigned int);

/*
 * Not used at present, but may be needed to
 * support other atiatom-based graphics devices
 *
 * static unsigned long	VideoPortReadRegisterUlong(unsigned long *);
 * static void	VideoPortWriteRegisterUlong(unsigned long *, unsigned long);
 */

#define	SETBITS(source, mask) source |= mask;
#define	CLEARBITS(source, mask) source &= mask;

#define	OFFSET_BLOCK0   0x0400
#define	OFFSET_BLOCK1   0x0000

#define	BLOCK0_ADDRESS_OF(a) (OFFSET_BLOCK0 + ((a) << 0))
#define	BLOCK1_ADDRESS_OF(a) (OFFSET_BLOCK1 + ((a) << 0))

#define	CFG_VGA_AP_EN	4
#define	VGA_Z128K	0
#define	VGA_TEXT_132	0x20
#define	VGA_XCRT_CNT_EN	0x40
#define	VGA_LADDR	0x08
#define	VMODE_3		0
#define	VMODE_7		1
#define	VMODE_12	2
#define	VMODE_62	3
#define	CRTC_EXT_EN	2
#define	CRTC_DISP_REQ_DISABLE	0x04
#define	GENENA		0x46e8
#define	GENENB		0x3c3
#define	CRTX		0x3d4
#define	CRTD		0x3d5
#define	DAC_MASK	0x3c6
#define	SEQX		0x3c4
#define	SEQD		0x3c5
#define	GRAX		0x3ce
#define	GRAD		0x3cf
#define	ATTRX		0x3c0
#define	GENS1		0x3da
#define	GENVS		0x102
#define	GENMO		0x3c2
#define	GENMOR		0x3cc

#define	CRTC_PARM_LEN	25
#define	GRP_PARM_LEN	9
#define	ATTR_PARM_LEN	20
static unsigned int ulMaskTable[] =
{
	0x0001, 0x0003, 0x0007, 0x000F,
	0x001F, 0x003F, 0x007F, 0x00FF,
	0x01FF, 0x03FF, 0x07FF, 0x0FFF,
	0x1FFF, 0x3FFF, 0x7FFF, 0xFFFF
};
const int	ACCESS_LEN[] =
{
	sizeof (unsigned short),
	sizeof (unsigned short),
	sizeof (unsigned short),
	sizeof (unsigned int),
	sizeof (unsigned char),
	sizeof (unsigned char),
	sizeof (unsigned char),
	sizeof (unsigned char)
};

const T_TYPE    CLASSIFY[] =
{
	T_WORD, T_WORD, T_WORD,
	T_DWORD,
	T_BYTE, T_BYTE, T_BYTE, T_BYTE
};

/*
 * Opcode to procedure dispatch table for Register type opcodes
 */
PARSERCALLBACK  RegisterCmdTable[] =
{
	scriptClearCmd,			/* REG_CLEAR */
	scriptWriteCmd,			/* REG_WRITE */
	scriptSkewCmd,			/* REG_SKEW */
	scriptReadCmd,			/* REG_READ_AND_COMPARE */
	scriptReadToMem,		/* READ_TO_MEM */
	scriptReadAndCompareFromMem,	/* READ_AND_COMPARE_FROM_MEM */
	scriptWriteFromMem,		/* WRITE_FROM_MEM */
	scriptMaskWriteFromMem,		/* MASK_WRITE_FROM_MEM */
	scriptSetBitsFromMem,		/* SET_BITS_FROM_MEM */
	scriptClearBitsFromMem,		/* CLEAR_BITS_FROM_MEM */
	scriptMaskWriteCmd,		/* REG_MASK_WRITE */
	scriptSetBitsCmd,		/* REG_SET_BITS */
	scriptClearBitsCmd,		/* REG_CLEAR_BITS */
	scriptBatchMaskWriteCmd,	/* REG_BATCH_MASK_WRITE */
	scriptBatchWriteCmd,		/* REG_BATCH_WRITE */
	scriptBatchClearCmd,		/* REG_BATCH_CLEAR */
};

/*
 * Opcode to procedure dispatch table for Special command type opcodes
 */
PARSERCALLBACK  SpecialCmdTable[] =
{
	scriptDelayInUs,
	scriptDelayInMs,
	scriptIndexedRegAccess,
	scriptDirectRegAccess,
	scriptPciConfigAccess,
	scriptCompareReadValue,
	scriptCompareReads,
	scriptJumpToNthByte,
	scriptJumpToNthByteIfE,
	scriptJumpToNthByteIfNE,
	scriptJumpToNthByteIfLT,
	scriptJumpToNthByteIfGT,
	scriptJumpToNthByteIfLTE,
	scriptJumpToNthByteIfGTE,
	scriptCallFuncAtNthByte,
	scriptJumpRelative,
	scriptJumpRelativeIfE,
	scriptJumpRelativeIfNE,
	scriptJumpRelativeIfLT,
	scriptJumpRelativeIfGT,
	scriptJumpRelativeIfLTE,
	scriptJumpRelativeIfGTE,
	scriptCallFuncAtRelative,
	scriptCallFunctionReturn,
	scriptJumpToTable,
	scriptCallTable,
	scriptCallTableReturn,
	scriptPllRegAccess,
	scriptCallAsmFunction,
	scriptCallFunctionAtBiosOffset,
	scriptTestReadAgainstValue,
	scriptTestTwoReads,
	scriptClearZeroFlag,
	scriptSetZeroFlag,
	scriptTestMemConfig,
	scriptBeep,
	scriptNull,
	scriptTestStop,
	scriptPostPort80,
	scriptTracePort80,
	scriptTestBreak,

};

/*
 * Sizes of the two opcode dispatch tables
 */
#define	NUMBER_OF_ARRAY(a)   (sizeof (a) / sizeof (a[0]))
#define	NumberOfRegisterCmd NUMBER_OF_ARRAY(RegisterCmdTable)
#define	NumberOfSpecialCmd NUMBER_OF_ARRAY(SpecialCmdTable)

#define	DEBUG_OUTPUT_FOR_CALL
#define	DEBUG_CALL_RETURN
#define	DEBUG_TRACE_START_OF_CMD
#define	Cntl_print	0
#define	IS_SPECIALCMD(cmd) ((cmd) & 0x80)
#define	IS_ENDOFTABLE(cmd) ((cmd) == END_OF_TABLE)

#define	COMPARE_PENDING		0x80
#define	COMPARE_WITH_NEXTVALUE	0x40
#define	COMPARE_WITH_NEXTREAD	0x20
#define	COMPARE_ENABLED		0x10
#define	TEST_PENDING		0x08
#define	TEST_WITH_NEXTVALUE	0x04
#define	TEST_WITH_NEXTREAD	0x02
#define	TEST_ENABLED		0x01
#define	TEST_BYTES_NUMBER	32

#define	ATI_REGBASE8		0x7FFC00L	/* 8M fb, register base */

#ifdef __cplusplus
}
#endif

#endif /* _SYS_ATIATOM_H */
