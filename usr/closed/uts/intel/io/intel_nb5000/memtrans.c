/*
 * INTEL CONFIDENTIAL
 * SPECIAL INTEL MODIFICATIONS
 *
 * Copyright (c) 2008-2009,  Intel Corporation.
 * All Rights Reserved.
 *
 * Approved for Solaris or OpenSolaris use only.
 * Approved for binary distribution only.
 *
 */


#include <sys/mc_intel.h>
#include <nb_log.h>
#include "memtrans.h"

int intel_nb5000_memtrans_debug;

static int
createDramAddr(uint64_t mySAddr, uint16_t branch, uint16_t rank,
    uint64_t *column, uint64_t *row, uint64_t *bank)
{
	int technology  = 0;
	uint16_t numCol, numRow, numBank, width, mcaSchdimm;
	uint32_t regvalue32;
	uint16_t regvalue16, tmp = 0;

	/*  make two slot into a pair which occupy an item in the array */
	tmp = rank/2;

	*column = 0;
	*row = 0;
	*bank = 0;

	regvalue16 = MTR_RD(branch, tmp);
	numCol = MTR_NUMCOL(regvalue16);
	numRow = MTR_NUMROW(regvalue16);
	numBank = MTR_NUMBANK(regvalue16);
	width = MTR_WIDTH(regvalue16);

	regvalue32 = MCA_RD();
	mcaSchdimm = (regvalue32 & MCA_SCHDIMM) >> 14;


	/* Find out which memory technology is used for the rank */

	if ((numCol == 10) && (numRow == 13) &&
	    (numBank == 4) && (width == 8)) {
		technology = 0;
		if (mySAddr >= 0x20000000)
			return (38);
	}

	else if ((numCol == 11) && (numRow == 13) &&
	    (numBank == 4) && (width == 4)) {
		technology = 1;
		if (mySAddr >= 0x40000000)
			return (38);
	}

	else if ((numCol == 10) && (numRow == 14) &&
	    (numBank == 4) && (width == 8)) {
		technology = 2;
		if (mySAddr >= 0x40000000)
			return (38);
	}

	else if ((numCol == 11) && (numRow == 14) &&
	    (numBank == 4) && (width == 4)) {
		technology = 3;
		if (mySAddr >= 0x80000000)
			return (38);
	}

	else if ((numCol == 10) && (numRow == 14) &&
	    (numBank == 8) && (width == 8)) {
		technology = 4;
		if (mySAddr >= 0x80000000)
			return (38);
	}

	else if ((numCol == 11) && (numRow == 14) &&
	    (numBank == 8) && (width == 4)) {
		technology = 5;
		if (mySAddr >= 0x100000000ull)
			return (38);
	}

	else if ((numCol == 10) && (numRow == 15) &&
	    (numBank == 8) && (width == 8)) {
		technology = 6;
		if (mySAddr >= 0x100000000ull)
			return (38);
	}

	else if ((numCol == 11) && (numRow == 15) &&
	    (numBank == 8) && (width == 4)) {
		technology = 7;
		if (mySAddr >= 0x200000000ull)
			return (38);
	}

	else if ((numCol == 10) && (numRow == 16) &&
	    (numBank == 8) && (width == 8)) {
		technology = 8;
		if (mySAddr >= 0x200000000ull)
			return (38);
	}

	else if ((numCol == 11) && (numRow == 16) &&
	    (numBank == 8) && (width == 4)) {
		technology = 10;
		if (mySAddr >= 0x400000000ull)
			return (38);
	}

	else {
		technology = -1;
		return (39);
	}

	if (!mcaSchdimm) {

		*bank |= ((mySAddr & BIT6) ? BIT0 : 0);
		*bank |= ((mySAddr & BIT7) ? BIT1 : 0);
		*row |= ((mySAddr & BIT8) ? BIT0 : 0);
		*row |= ((mySAddr & BIT9) ? BIT1 : 0);
		*row |= ((mySAddr & BIT10) ? BIT2 : 0);
		*row |= ((mySAddr & BIT11) ? BIT3 : 0);
		*row |= ((mySAddr & BIT12) ? BIT4 : 0);
		*row |= ((mySAddr & BIT13) ? BIT5 : 0);
		*row |= ((mySAddr & BIT14) ? BIT6 : 0);
		*row |= ((mySAddr & BIT15) ? BIT7 : 0);
		*row |= ((mySAddr & BIT16) ? BIT8 : 0);
		*row |= ((mySAddr & BIT17) ? BIT9 : 0);
		*row |= ((mySAddr & BIT18) ? BIT10 : 0);
		*row |= ((mySAddr & BIT19) ? BIT11 : 0);
		*row |= ((mySAddr & BIT20) ? BIT12 : 0);
		*column |= ((mySAddr & BIT21) ? BIT2 : 0);
		*column |= ((mySAddr & BIT22) ? BIT3 : 0);
		*column |= ((mySAddr & BIT23) ? BIT4 : 0);
		*column |= ((mySAddr & BIT24) ? BIT5 : 0);
		*column |= ((mySAddr & BIT25) ? BIT6 : 0);
		*column |= ((mySAddr & BIT26) ? BIT7 : 0);
		*column |= ((mySAddr & BIT27) ? BIT8 : 0);
		*column |= ((mySAddr & BIT28) ? BIT9 : 0);

	} else if (mcaSchdimm) {

		*bank |= ((mySAddr & BIT6) ? BIT0 : 0);
		*bank |= ((mySAddr & BIT7) ? BIT1 : 0);
		*row |= ((mySAddr & BIT8) ? BIT0 : 0);
		*row |= ((mySAddr & BIT9) ? BIT1 : 0);
		*row |= ((mySAddr & BIT10) ? BIT2 : 0);
		*row |= ((mySAddr & BIT11) ? BIT3 : 0);
		*row |= ((mySAddr & BIT12) ? BIT4 : 0);
		*row |= ((mySAddr & BIT13) ? BIT5 : 0);
		*row |= ((mySAddr & BIT14) ? BIT6 : 0);
		*row |= ((mySAddr & BIT15) ? BIT7 : 0);
		*row |= ((mySAddr & BIT16) ? BIT8 : 0);
		*row |= ((mySAddr & BIT17) ? BIT9 : 0);
		*row |= ((mySAddr & BIT18) ? BIT10 : 0);
		*row |= ((mySAddr & BIT19) ? BIT11 : 0);
		*row |= ((mySAddr & BIT20) ? BIT12 : 0);
		*column |= ((mySAddr & BIT5) ? BIT2 : 0);
		*column |= ((mySAddr & BIT21) ? BIT3 : 0);
		*column |= ((mySAddr & BIT22) ? BIT4 : 0);
		*column |= ((mySAddr & BIT23) ? BIT5 : 0);
		*column |= ((mySAddr & BIT24) ? BIT6 : 0);
		*column |= ((mySAddr & BIT25) ? BIT7 : 0);
		*column |= ((mySAddr & BIT26) ? BIT8 : 0);
		*column |= ((mySAddr & BIT27) ? BIT9 : 0);
	}

	/* Map the technology specific bits for dual channel mode */
	if (!mcaSchdimm) {

		switch (technology) {

		case 0 :
			break;

		case 1 :
			*column |= ((mySAddr & BIT29) ? BIT11 : 0);
			break;

		case 2 :
			*row |= ((mySAddr & BIT29) ? BIT13 : 0);
			break;

		case 3 :
			*column |= ((mySAddr & BIT29) ? BIT11 : 0);
			*row |= ((mySAddr & BIT30) ? BIT13 : 0);
			break;

		case 4 :
			*bank |= ((mySAddr & BIT29) ? BIT2 : 0);
			*row |= ((mySAddr & BIT30) ? BIT13 : 0);
			break;

		case 5 :
			*bank |= ((mySAddr & BIT29) ? BIT2 : 0);
			*row |= ((mySAddr & BIT30) ? BIT13 : 0);
			*column |= ((mySAddr & BIT31) ? BIT11 : 0);
			break;

		case 6 :
			*bank |= ((mySAddr & BIT29) ? BIT2 : 0);
			*row |= ((mySAddr & BIT30) ? BIT13 : 0);
			*row |= ((mySAddr & BIT31) ? BIT14 : 0);
			break;

		case 7 :
			*bank |= ((mySAddr & BIT29) ? BIT2 : 0);
			*row |= ((mySAddr & BIT30) ? BIT13 : 0);
			*row |= ((mySAddr & BIT31) ? BIT14 : 0);
			*column |= ((mySAddr & BIT32) ? BIT11 : 0);
			break;

		case 8 :
			*bank |= ((mySAddr & BIT29) ? BIT2 : 0);
			*row |= ((mySAddr & BIT30) ? BIT13 : 0);
			*row |= ((mySAddr & BIT31) ? BIT14 : 0);
			*row |= ((mySAddr & BIT32) ? BIT15 : 0);
			break;

		case 10 :
			*bank |= ((mySAddr & BIT29) ? BIT2 : 0);
			*row |= ((mySAddr & BIT30) ? BIT13 : 0);
			*row |= ((mySAddr & BIT31) ? BIT14 : 0);
			*row |= ((mySAddr & BIT32) ? BIT15 : 0);
			*column |= ((mySAddr & BIT33) ? BIT11 : 0);
			break;

		case -1 :
			break;

		default :
			break;

		}
	}

	/* Map the technology specific bits for single channel mode */
	else {

		switch (technology) {

		case 0 :
			break;

		case 1 :
			*column |= ((mySAddr & BIT28) ? BIT11 : 0);
			break;

		case 2 :
			*row |= ((mySAddr & BIT28) ? BIT13 : 0);
			break;

		case 3 :
			*column |= ((mySAddr & BIT28) ? BIT11 : 0);
			*row |= ((mySAddr & BIT29) ? BIT13 : 0);
			break;

		case 4 :
			*bank |= ((mySAddr & BIT28) ? BIT2 : 0);
			*row |= ((mySAddr & BIT29) ? BIT13 : 0);
			break;

		case 5 :
			*bank |= ((mySAddr & BIT28) ? BIT2 : 0);
			*row |= ((mySAddr & BIT29) ? BIT13 : 0);
			*column |= ((mySAddr & BIT30) ? BIT11 : 0);
			break;

		case 6 :
			*bank |= ((mySAddr & BIT28) ? BIT2 : 0);
			*row |= ((mySAddr & BIT29) ? BIT13 : 0);
			*row |= ((mySAddr & BIT30) ? BIT14 : 0);
			break;

		case 7 :
			*bank |= ((mySAddr & BIT28) ? BIT2 : 0);
			*row |= ((mySAddr & BIT29) ? BIT13 : 0);
			*row |= ((mySAddr & BIT30) ? BIT14 : 0);
			*column |= ((mySAddr & BIT31) ? BIT11 : 0);
			break;

		case 8 :
			*bank |= ((mySAddr & BIT28) ? BIT2 : 0);
			*row |= ((mySAddr & BIT29) ? BIT13 : 0);
			*row |= ((mySAddr & BIT30) ? BIT14 : 0);
			*row |= ((mySAddr & BIT31) ? BIT15 : 0);
			break;

		case 10 :
			*bank |= ((mySAddr & BIT28) ? BIT2 : 0);
			*row |= ((mySAddr & BIT29) ? BIT13 : 0);
			*row |= ((mySAddr & BIT30) ? BIT14 : 0);
			*row |= ((mySAddr & BIT31) ? BIT15 : 0);
			*column |= ((mySAddr & BIT32) ? BIT11 : 0);
			break;

		case -1 :
			break;

		default :
			break;

		}
	}
	return (30);
}

static int
SCcreateDramAddr(uint64_t mySAddr, uint16_t branch, uint16_t rank,
    uint64_t *column, uint64_t *row, uint64_t *bank)
{
	int technology  = 0;
	uint16_t numCol, numRow, numBank, width, PageHitMode = 0;
	uint32_t regvalue32;
	uint16_t regvalue16;

	*column = 0;
	*row = 0;
	*bank = 0;

	regvalue16 = MTR_RD(branch, rank);
	numCol = MTR_NUMCOL(regvalue16);
	numRow = MTR_NUMROW(regvalue16);
	numBank = MTR_NUMBANK(regvalue16);
	width = MTR_WIDTH(regvalue16);

	regvalue32 = MC_RD();
	PageHitMode = (regvalue32 >> 18) & 3;


	/* Find out which memory technology is used for the rank */

	if ((numCol == 10) && (numRow == 13) &&
	    (numBank == 4) && (width == 8)) {
		technology = 0;
		if (mySAddr >= 0x20000000)
			return (38);
	}

	else if ((numCol == 11) && (numRow == 13) &&
	    (numBank == 4) && (width == 4)) {
		technology = 1;
		if (mySAddr >= 0x40000000)
			return (38);
	}

	else if ((numCol == 10) && (numRow == 14) &&
	    (numBank == 4) && (width == 8)) {
		technology = 2;
		if (mySAddr >= 0x40000000)
			return (38);
	}

	else if ((numCol == 11) && (numRow == 14) &&
	    (numBank == 4) && (width == 4)) {
		technology = 3;
		if (mySAddr >= 0x80000000)
			return (38);
	}

	else if ((numCol == 10) && (numRow == 14) &&
	    (numBank == 8) && (width == 8)) {
		technology = 4;
		if (mySAddr >= 0x80000000)
			return (38);
	}

	else if ((numCol == 11) && (numRow == 14) &&
	    (numBank == 8) && (width == 4)) {
		technology = 5;
		if (mySAddr >= 0x100000000ull)
			return (38);
	}

	else if ((numCol == 10) && (numRow == 15) &&
	    (numBank == 8) && (width == 8)) {
		technology = 6;
		if (mySAddr >= 0x100000000ull)
			return (38);
	}

	else if ((numCol == 11) && (numRow == 15) &&
	    (numBank == 8) && (width == 4)) {
		technology = 7;
		if (mySAddr >= 0x200000000ull)
			return (38);
	}

	else if ((numCol == 10) && (numRow == 16) &&
	    (numBank == 8) && (width == 8)) {
		technology = 8;
		if (mySAddr >= 0x200000000ull)
			return (38);
	}

	else if ((numCol == 11) && (numRow == 16) &&
	    (numBank == 8) && (width == 4)) {
		technology = 10;
		if (mySAddr >= 0x400000000ull)
			return (38);
	}

	else {
		technology = -1;
		return (39);
	}

	if (!PageHitMode) {

		*bank |= ((mySAddr & BIT6) ? BIT0 : 0);
		*bank |= ((mySAddr & BIT7) ? BIT1 : 0);
		*row |= ((mySAddr & BIT8) ? BIT0 : 0);
		*row |= ((mySAddr & BIT9) ? BIT1 : 0);
		*row |= ((mySAddr & BIT10) ? BIT2 : 0);
		*row |= ((mySAddr & BIT11) ? BIT3 : 0);
		*row |= ((mySAddr & BIT12) ? BIT4 : 0);
		*row |= ((mySAddr & BIT13) ? BIT5 : 0);
		*row |= ((mySAddr & BIT14) ? BIT6 : 0);
		*row |= ((mySAddr & BIT15) ? BIT7 : 0);
		*row |= ((mySAddr & BIT16) ? BIT8 : 0);
		*row |= ((mySAddr & BIT17) ? BIT9 : 0);
		*row |= ((mySAddr & BIT18) ? BIT10 : 0);
		*row |= ((mySAddr & BIT19) ? BIT11 : 0);
		*row |= ((mySAddr & BIT20) ? BIT12 : 0);
		*column |= ((mySAddr & BIT5) ? BIT2 : 0);
		*column |= ((mySAddr & BIT21) ? BIT3 : 0);
		*column |= ((mySAddr & BIT22) ? BIT4 : 0);
		*column |= ((mySAddr & BIT23) ? BIT5 : 0);
		*column |= ((mySAddr & BIT24) ? BIT6 : 0);
		*column |= ((mySAddr & BIT25) ? BIT7 : 0);
		*column |= ((mySAddr & BIT26) ? BIT8 : 0);
		*column |= ((mySAddr & BIT27) ? BIT9 : 0);

	} else if (PageHitMode) {

		*bank |= ((mySAddr & BIT7) ? BIT0 : 0);
		*bank |= ((mySAddr & BIT8) ? BIT1 : 0);
		*row |= ((mySAddr & BIT9) ? BIT0 : 0);
		*row |= ((mySAddr & BIT10) ? BIT1 : 0);
		*row |= ((mySAddr & BIT11) ? BIT2 : 0);
		*row |= ((mySAddr & BIT12) ? BIT3 : 0);
		*row |= ((mySAddr & BIT13) ? BIT4 : 0);
		*row |= ((mySAddr & BIT14) ? BIT5 : 0);
		*row |= ((mySAddr & BIT15) ? BIT6 : 0);
		*row |= ((mySAddr & BIT16) ? BIT7 : 0);
		*row |= ((mySAddr & BIT17) ? BIT8 : 0);
		*row |= ((mySAddr & BIT18) ? BIT9 : 0);
		*row |= ((mySAddr & BIT19) ? BIT10 : 0);
		*row |= ((mySAddr & BIT20) ? BIT11 : 0);
		*row |= ((mySAddr & BIT21) ? BIT12 : 0);
		*column |= ((mySAddr & BIT5) ? BIT2 : 0);
		*column |= ((mySAddr & BIT6) ? BIT3 : 0);
		*column |= ((mySAddr & BIT22) ? BIT4 : 0);
		*column |= ((mySAddr & BIT23) ? BIT5 : 0);
		*column |= ((mySAddr & BIT24) ? BIT6 : 0);
		*column |= ((mySAddr & BIT25) ? BIT7 : 0);
		*column |= ((mySAddr & BIT26) ? BIT8 : 0);
		*column |= ((mySAddr & BIT27) ? BIT9 : 0);
	}

	/* Map the technology specific bits for dual channel mode */
	switch (technology) {

	case 0 :
		break;

	case 1 :
		*column |= ((mySAddr & BIT28) ? BIT11 : 0);
		break;

	case 2 :
		*row |= ((mySAddr & BIT28) ? BIT13 : 0);
		break;

	case 3 :
		*column |= ((mySAddr & BIT28) ? BIT11 : 0);
		*row |= ((mySAddr & BIT29) ? BIT13 : 0);
		break;

	case 4 :
		*bank |= ((mySAddr & BIT28) ? BIT2 : 0);
		*row |= ((mySAddr & BIT29) ? BIT13 : 0);
		break;

	case 5 :
		*bank |= ((mySAddr & BIT28) ? BIT2 : 0);
		*row |= ((mySAddr & BIT29) ? BIT13 : 0);
		*column |= ((mySAddr & BIT30) ? BIT11 : 0);
		break;

	case 6 :
		*bank |= ((mySAddr & BIT28) ? BIT2 : 0);
		*row |= ((mySAddr & BIT29) ? BIT13 : 0);
		*row |= ((mySAddr & BIT30) ? BIT14 : 0);
		break;

	case 7 :
		*bank |= ((mySAddr & BIT28) ? BIT2 : 0);
		*row |= ((mySAddr & BIT29) ? BIT13 : 0);
		*row |= ((mySAddr & BIT30) ? BIT14 : 0);
		*column |= ((mySAddr & BIT31) ? BIT11 : 0);
		break;

	case -1 :
		break;

	default :
		break;

	}

	return (30);
}

int
ReverseTranslate(uint64_t *column, uint64_t *row, uint64_t *bank,
	uint16_t *branch, uint16_t *rank, uint64_t myPhyAddr)
{

	int i, j;
	int error = 0;
	int dmirRank[2 * MAX_DMIR_NUMBER][MAX_DMIR_INTERLEAVE];
	int mirWay[MAX_MIR_NUMBER][MAX_MIR_INTERLEAVE];
	int tolmTolm = 1, mcMirror = 0, mcaSchdimm;
	int retVal, rt;
	int way = 1;
	int mirShift = 0, rankCount = 0;
	uint32_t regvalue32;
	uint16_t regvalue16;
	uint64_t upperL, lowMemLimit, tolmGap;
	uint64_t tolmAddr, mirTop = 0, mirAddr = 0, front = 0;
	uint64_t mirLimit[MAX_MIR_NUMBER];
	uint64_t mySAddr = 0, dmirLimit[2 * MAX_DMIR_NUMBER];
	uint64_t dmirTop = 0, dmirAddr = 0;
	uint64_t dfront = 0, dlimit;
	uint64_t dbase[DMIR_MAX_RANK_NUMBER];

	/*  get DMIR */
	for (i = 0; i < MAX_DMIR_NUMBER; i++) {
		regvalue32 = DMIR_RD(0, i);
		dmirLimit[i] = (regvalue32 >> 16) & DMIR_LIMIT_MASK;
		/* get DMIR RANK of the 1st branch */
		DMIR_RANKS(regvalue32, dmirRank[i][0], dmirRank[i][1],
		    dmirRank[i][2], dmirRank[i][3]);

		regvalue32 = DMIR_RD(1, i);
		dmirLimit[i + MAX_DMIR_NUMBER] = (regvalue32 >> 16) &
		    DMIR_LIMIT_MASK;
		/* get DMIR RANK of the 2nd branch */
		DMIR_RANKS(regvalue32, dmirRank[i + MAX_DMIR_NUMBER][0],
		    dmirRank[i + MAX_DMIR_NUMBER][1],
		    dmirRank[i + MAX_DMIR_NUMBER][2],
		    dmirRank[i + MAX_DMIR_NUMBER][3]);
	}

	upperL = 0;

	/*  get MIR */
	for (i = 0; i < MAX_MIR_NUMBER; i++) {

		regvalue16 = MIR_RD(i);
		mirLimit[i] = (regvalue16 & 0xfff0) >> 4;
		mirWay[i][0] = regvalue16 & 0x0001; /* get way0 */
		mirWay[i][1] = (regvalue16 & 0x0002) >> 1; /* get way1 */
		if (mirLimit[i] > upperL) {
			upperL = mirLimit[i];
		}

	}

	regvalue16 = TOLM_RD();
	tolmTolm = (regvalue16 & 0xf000) >> 12;

	regvalue32 = MC_RD();
	mcMirror = (regvalue32 & MC_MIRROR) >> 16;

	regvalue32 = MCA_RD();
	mcaSchdimm = (regvalue32 & MCA_SCHDIMM) >> 14;

	/* Eliminate cases where address is not in a valid range */
	lowMemLimit = tolmTolm;
	lowMemLimit <<= DRAM_GRANULARITY;
	tolmGap = CGL_FOURGIG - lowMemLimit;

	if ((myPhyAddr < CGL_FOURGIG) && (myPhyAddr >= lowMemLimit)) {
		return (11);
	}

	upperL = (upperL << DRAM_GRANULARITY) + tolmGap;
	if (myPhyAddr >= upperL) {
		return (12);
	}

	/* Perform TOLM Shift, if necessary */
	tolmAddr = myPhyAddr & (0xFFFFFFFF80ULL | CGL_CACHELINE);

	if (myPhyAddr >= CGL_FOURGIG) tolmAddr -= tolmGap;

	for (i = 0; i < MAX_MIR_NUMBER; i++) {
		mirTop = (mirLimit[i] << DRAM_GRANULARITY);

		if (i != 0) {
			if ((!mcMirror) &&
			    (mirWay[i-1][0] == mirWay[i-1][1] == 1)) {
				way = 2;
			} else if (mirWay[i-1][0] != mirWay[i-1][1]) {
				way = 1;
			} else if (mcMirror) {
				way = 1;
			} else way = 0;

			if (way != 0) {
				if (i == 1) {
					front = (mirLimit[i-1]<<28)/way;
				} else {
					front = front +	(((mirLimit[i-1]-
					    mirLimit[i-2])<<28)/way);
				}
			}
		}

		if (tolmAddr < mirTop) {
			/* Is the shifted address within the given MIR limit? */
			if ((!mcMirror) &&
			    (mirWay[i][0] == mirWay[i][1] == 1)) {
				/* Interleave between branches */
				mirAddr = mirAddr & 0x7FFFFFFFDFull;
				mirAddr = tolmAddr / 2;
				/* remove branch bit from address */
				*branch = (tolmAddr & BIT6) >> 6;
				mirShift = 1;
			} else if (mirWay[i][0] != mirWay[i][1]) {
				/* No branch interleaving */
				mirAddr = tolmAddr;
				if (mirWay[i][0] == 1) *branch = 0;
					if (mirWay[i][1] == 1) *branch = 1;
					mirShift = 0;
				} else if (mcMirror) {
					/* Mirror mode, branch is forced to 0 */
					mirAddr = tolmAddr;
					*branch = 0;
					mirShift = 0;
				} else {
					error = 1; /* Error */
					retVal = 13;
				}
			break; /* Don't need to check the other MIRs */
		}
	}

	if (intel_nb5000_memtrans_debug)
		cmn_err(CE_WARN, "ReverseTranslation() mirAddr = %llx\n",
		    (long long)mirAddr);

	for (i = 0; i < DMIR_MAX_RANK_NUMBER; i++) {
		dbase[i] = 0;
	}

	for (i = 0 + (*branch * MAX_DMIR_NUMBER);
	    i < MAX_DMIR_NUMBER + (*branch * MAX_DMIR_NUMBER); i++) {
		if (i != 0 + (*branch * MAX_DMIR_NUMBER)) {
			dfront = dmirLimit[i-1] << 28;
		}

		dmirTop = (dmirLimit[i] << 28);

		if (mirAddr < (dmirTop + front)) {
			*rank = dmirRank[i][ (mirAddr & (BIT6 | BIT7)) >> 6];

			for (j = 0; j < 4; j++) {
				if (dmirRank[i][j] == *rank)
				rankCount++;
			}

			if (rankCount == 1) {
			/* remove rank bit from address */
				mirAddr = mirAddr &
				    (mcaSchdimm ? 0x3FFFFFFF3Full :
				    (mirShift ? 0x3FFFFFFFCFull :
				    0x3FFFFFFF9Full));
			/* Interleaves between 4 ranks */
			dmirAddr = (mirAddr - front - dfront) / 4;
			} else if (rankCount == 2) {
			/* remove rank bit from address */
				mirAddr = mirAddr &
				    (mcaSchdimm ? 0x3FFFFFFFBFull :
				    (mirShift ? 0x3FFFFFFFEFull :
				    0x3FFFFFFFDFull));
			/* Interleaves between 2 ranks */
			dmirAddr = (mirAddr - front - dfront) / 2;
			} else if (rankCount == 4) {
			/* Interleaves between 1 rank */
			dmirAddr = (mirAddr - front - dfront) / 1;
			} else {
			error = 1; /* Error */
			retVal = 14;
			}

			dmirAddr = dmirAddr + dbase[*rank];
			break; /* Don't need to check the other DMIRs */
		} else {
			/* get interleave way */
			if ((dmirRank[i][0] != dmirRank[i][1]) &&
			    (dmirRank[i][2] != dmirRank[i][3]) &&
			    (dmirRank[i][3] != dmirRank[i][0])) {
				way = 4;
			} else if ((dmirRank[i][0] == dmirRank[i][2]) &&
			    (dmirRank[i][1] == dmirRank[i][3])) {
				way = 2;
			} else {
				way = 1;
			}
			if (i == 0 + (*branch * MAX_DMIR_NUMBER)) {
				dlimit = dmirTop / way;
			} else {
				dlimit = ((dmirLimit[i] - dmirLimit[i-1])
				    << DRAM_GRANULARITY) / way;
			}

			for (j = 0; j < 4; j++) {
				*rank = dmirRank[i][j];
				dbase[*rank] = dbase[*rank] + dlimit;
			}
		}
	}

	mySAddr = dmirAddr;

	rt = (nb_chipset == INTEL_NB_5100) ?
	    SCcreateDramAddr(mySAddr, *branch, *rank, column, row, bank) :
	    createDramAddr(mySAddr, *branch, *rank, column, row, bank);

	if (rt != 30) {
		if (intel_nb5000_memtrans_debug)
			cmn_err(CE_WARN, "createDramAddr() error %d\n", rt);
		return (rt - 20);
	}

	if (error) {
		if (intel_nb5000_memtrans_debug)
			cmn_err(CE_WARN, "ReverseTranslation() error %d\n",
			    retVal);
		return (retVal);
	} else {
		return (10);
	}
}


static int
createSAddr(uint64_t *mySAddr, uint64_t column, uint64_t row, uint64_t bank,
    uint16_t branch, uint16_t rank)
{

	int technology = 0;
	int mcaSchdimm;
	uint16_t tmp;
	uint16_t numCol, numRow, numBank, width, present;
	uint32_t regvalue32;
	uint16_t regvalue16;

	/* make two slot into a pair which occupy a item in the array */
	tmp = rank/2;

	regvalue16 = MTR_RD(branch, tmp);
	numCol = MTR_NUMCOL(regvalue16);
	numRow = MTR_NUMROW(regvalue16);
	numBank = MTR_NUMBANK(regvalue16);
	width = MTR_WIDTH(regvalue16);
	present = MTR_PRESENT(regvalue16);

	regvalue32 = MCA_RD();
	mcaSchdimm = (regvalue32 & MCA_SCHDIMM) >> 14;

	if (present == 0) { /* there is not a dram in this socket */
		return (22);
	}

	/* physical address is beyond dimm capacity */
	if ((bank >= numBank) || (row >= (1ULL << numRow))) {
		return (21);
	}
	if (numCol > 10) {
		if (column >= (1ULL << (numCol + 1)))
		return (21);
	} else {
		if (column >= (1ULL << numCol))
		return (21);
	}

	/* Find out which memory technology is used for the rank */

	if ((numCol == 10) && (numRow == 13) &&
	    (numBank == 4) && (width == 8))
		technology = 0;

	else if ((numCol == 11) && (numRow == 13) &&
	    (numBank == 4) && (width == 4))
		technology = 1;

	else if ((numCol == 10) && (numRow == 14) &&
	    (numBank == 4) && (width == 8))
		technology = 2;

	else if ((numCol == 11) && (numRow == 14) &&
	    (numBank == 4) && (width == 4))
		technology = 3;

	else if ((numCol == 10) && (numRow == 14) &&
	    (numBank == 8) && (width == 8))
		technology = 4;

	else if ((numCol == 11) && (numRow == 14) &&
	    (numBank == 8) && (width == 4))
		technology = 5;

	else if ((numCol == 10) && (numRow == 15) &&
	    (numBank == 8) && (width == 8))
		technology = 6;

	else if ((numCol == 11) && (numRow == 15) &&
	    (numBank == 8) && (width == 4))
		technology = 7;

	else if ((numCol == 10) && (numRow == 16) &&
	    (numBank == 8) && (width == 8))
		technology = 8;

	else if ((numCol == 11) && (numRow == 16) &&
	    (numBank == 8) && (width == 4))
		technology = 10;

	else {
		technology = -1;
		return (29);
	}

	if (mcaSchdimm) {

		*mySAddr |= ((column & BIT2) ? BIT5 : 0);
		*mySAddr |= ((bank & BIT0) ? BIT6 : 0);
		*mySAddr |= ((bank & BIT1) ? BIT7 : 0);
		*mySAddr |= ((row & BIT0) ? BIT8 : 0);
		*mySAddr |= ((row & BIT1) ? BIT9 : 0);
		*mySAddr |= ((row & BIT2) ? BIT10 : 0);
		*mySAddr |= ((row & BIT3) ? BIT11 : 0);
		*mySAddr |= ((row & BIT4) ? BIT12 : 0);
		*mySAddr |= ((row & BIT5) ? BIT13 : 0);
		*mySAddr |= ((row & BIT6) ? BIT14 : 0);
		*mySAddr |= ((row & BIT7) ? BIT15 : 0);
		*mySAddr |= ((row & BIT8) ? BIT16 : 0);
		*mySAddr |= ((row & BIT9) ? BIT17 : 0);
		*mySAddr |= ((row & BIT10) ? BIT18 : 0);
		*mySAddr |= ((row & BIT11) ? BIT19 : 0);
		*mySAddr |= ((row & BIT12) ? BIT20 : 0);
		*mySAddr |= ((column & BIT3) ? BIT21 : 0);
		*mySAddr |= ((column & BIT4) ? BIT22 : 0);
		*mySAddr |= ((column & BIT5) ? BIT23 : 0);
		*mySAddr |= ((column & BIT6) ? BIT24 : 0);
		*mySAddr |= ((column & BIT7) ? BIT25 : 0);
		*mySAddr |= ((column & BIT8) ? BIT26 : 0);
		*mySAddr |= ((column & BIT9) ? BIT27 : 0);

	} else if (!mcaSchdimm) {

		*mySAddr |= ((column & BIT1) ? BIT5 : 0);
		*mySAddr |= ((bank & BIT0) ? BIT6 : 0);
		*mySAddr |= ((bank & BIT1) ? BIT7 : 0);
		*mySAddr |= ((row & BIT0) ? BIT8 : 0);
		*mySAddr |= ((row & BIT1) ? BIT9 : 0);
		*mySAddr |= ((row & BIT2) ? BIT10 : 0);
		*mySAddr |= ((row & BIT3) ? BIT11 : 0);
		*mySAddr |= ((row & BIT4) ? BIT12 : 0);
		*mySAddr |= ((row & BIT5) ? BIT13 : 0);
		*mySAddr |= ((row & BIT6) ? BIT14 : 0);
		*mySAddr |= ((row & BIT7) ? BIT15 : 0);
		*mySAddr |= ((row & BIT8) ? BIT16 : 0);
		*mySAddr |= ((row & BIT9) ? BIT17 : 0);
		*mySAddr |= ((row & BIT10) ? BIT18 : 0);
		*mySAddr |= ((row & BIT11) ? BIT19 : 0);
		*mySAddr |= ((row & BIT12) ? BIT20 : 0);
		*mySAddr |= ((column & BIT2) ? BIT21 : 0);
		*mySAddr |= ((column & BIT3) ? BIT22 : 0);
		*mySAddr |= ((column & BIT4) ? BIT23 : 0);
		*mySAddr |= ((column & BIT5) ? BIT24 : 0);
		*mySAddr |= ((column & BIT6) ? BIT25 : 0);
		*mySAddr |= ((column & BIT7) ? BIT26 : 0);
		*mySAddr |= ((column & BIT8) ? BIT27 : 0);
		*mySAddr |= ((column & BIT9) ? BIT28 : 0);

	}

	/* Map the technology specific bits for dual channel mode */
	if (!mcaSchdimm) {

		switch (technology) {

		case 0 :
			break;

		case 1 :
			*mySAddr |= ((column & BIT11) ? BIT29 : 0);
			break;

		case 2 :
			*mySAddr |= ((row & BIT13) ? BIT29 : 0);
			break;

		case 3 :
			*mySAddr |= ((column & BIT11) ? BIT29 : 0);
			*mySAddr |= ((row & BIT13) ? BIT30 : 0);
			break;

		case 4 :
			*mySAddr |= ((bank & BIT2) ? BIT29 : 0);
			*mySAddr |= ((row & BIT13) ? BIT30 : 0);
			break;

		case 5 :
			*mySAddr |= ((bank & BIT2) ? BIT29 : 0);
			*mySAddr |= ((row & BIT13) ? BIT30 : 0);
			*mySAddr |= ((column & BIT11) ? BIT31 : 0);
			break;

		case 6 :
			*mySAddr |= ((bank & BIT2) ? BIT29 : 0);
			*mySAddr |= ((row & BIT13) ? BIT30 : 0);
			*mySAddr |= ((row & BIT14) ? BIT31 : 0);
			break;

		case 7 :
			*mySAddr |= ((bank & BIT2) ? BIT29 : 0);
			*mySAddr |= ((row & BIT13) ? BIT30 : 0);
			*mySAddr |= ((row & BIT14) ? BIT31 : 0);
			*mySAddr |= ((column & BIT11) ? BIT32 : 0);
			break;

		case 8 :
			*mySAddr |= ((bank & BIT2) ? BIT29 : 0);
			*mySAddr |= ((row & BIT13) ? BIT30 : 0);
			*mySAddr |= ((row & BIT14) ? BIT31 : 0);
			*mySAddr |= ((row & BIT15) ? BIT32 : 0);
			break;

		case 10 :
			*mySAddr |= ((bank & BIT2) ? BIT29 : 0);
			*mySAddr |= ((row & BIT13) ? BIT30 : 0);
			*mySAddr |= ((row & BIT14) ? BIT31 : 0);
			*mySAddr |= ((row & BIT15) ? BIT32 : 0);
			*mySAddr |= ((column & BIT11) ? BIT33 : 0);
			break;

		case -1 :
			break;

		default :
			break;

		}

	}

	/* Map the technology specific bits for single channel mode */
	else {

		switch (technology) {

		case 0 :
			break;

		case 1 :
			*mySAddr |= ((column & BIT11) ? BIT28 : 0);
			break;

		case 2 :
			*mySAddr |= ((row & BIT13) ? BIT28 : 0);
			break;

		case 3 :
			*mySAddr |= ((column & BIT11) ? BIT28 : 0);
			*mySAddr |= ((row & BIT13) ? BIT29 : 0);
			break;

		case 4 :
			*mySAddr |= ((bank & BIT2) ? BIT28 : 0);
			*mySAddr |= ((row & BIT13) ? BIT29 : 0);
			break;

		case 5 :
			*mySAddr |= ((bank & BIT2) ? BIT28 : 0);
			*mySAddr |= ((row & BIT13) ? BIT29 : 0);
			*mySAddr |= ((column & BIT11) ? BIT30 : 0);
			break;

		case 6 :
			*mySAddr |= ((bank & BIT2) ? BIT28 : 0);
			*mySAddr |= ((row & BIT13) ? BIT29 : 0);
			*mySAddr |= ((row & BIT14) ? BIT30 : 0);
			break;

		case 7 :
			*mySAddr |= ((bank & BIT2) ? BIT28 : 0);
			*mySAddr |= ((row & BIT13) ? BIT29 : 0);
			*mySAddr |= ((row & BIT14) ? BIT30 : 0);
			*mySAddr |= ((column & BIT11) ? BIT31 : 0);
			break;

		case 8 :
			*mySAddr |= ((bank & BIT2) ? BIT28 : 0);
			*mySAddr |= ((row & BIT13) ? BIT29 : 0);
			*mySAddr |= ((row & BIT14) ? BIT30 : 0);
			*mySAddr |= ((row & BIT15) ? BIT31 : 0);
			break;

		case 10 :
			*mySAddr |= ((bank & BIT2) ? BIT28 : 0);
			*mySAddr |= ((row & BIT13) ? BIT29 : 0);
			*mySAddr |= ((row & BIT14) ? BIT30 : 0);
			*mySAddr |= ((row & BIT15) ? BIT31 : 0);
			*mySAddr |= ((column & BIT11) ? BIT32 : 0);
			break;

		case -1 :
			break;

		default :
			break;

		}

	}

	return (20);

}

static int
SCcreateSAddr(uint64_t *mySAddr, uint64_t column, uint64_t row, uint64_t bank,
    uint16_t branch, uint16_t rank)
{

	int technology = 0;
	int PageHitMode = 0;
	uint16_t numCol, numRow, numBank, width, present;
	uint32_t regvalue32;
	uint16_t regvalue16;

	regvalue16 = MTR_RD(branch, rank);
	numCol = MTR_NUMCOL(regvalue16);
	numRow = MTR_NUMROW(regvalue16);
	numBank = MTR_NUMBANK(regvalue16);
	width = MTR_WIDTH(regvalue16);
	present = MTR_PRESENT(regvalue16);

	regvalue32 = MC_RD();
	PageHitMode = (regvalue32 >> 18) & 3;

	if (intel_nb5000_memtrans_debug) {
		cmn_err(CE_NOTE, "regvalue32 = MC_RD() = %0x", regvalue32);
		cmn_err(CE_NOTE, "regvalue16 = MTR_RD(%d,%d) = %0x",
		    branch, rank, regvalue16);
	}

	if (present == 0) { /* there is not a dram in this socket */
		return (22);
	}

	/* physical address is beyond dimm capacity */
	if ((bank >= numBank) || (row >= (1ULL << numRow))) {
		return (21);
	}
	if (numCol > 10) {
		if (column >= (1ULL << (numCol + 1)))
		return (21);
	} else {
		if (column >= (1ULL << numCol))
		return (21);
	}

	/* Find out which memory technology is used for the rank */

	if ((numCol == 10) && (numRow == 13) &&
	    (numBank == 4) && (width == 8))
		technology = 0;

	else if ((numCol == 11) && (numRow == 13) &&
	    (numBank == 4) && (width == 4))
		technology = 1;

	else if ((numCol == 10) && (numRow == 14) &&
	    (numBank == 4) && (width == 8))
		technology = 2;

	else if ((numCol == 11) && (numRow == 14) &&
	    (numBank == 4) && (width == 4))
		technology = 3;

	else if ((numCol == 10) && (numRow == 14) &&
	    (numBank == 8) && (width == 8))
		technology = 4;

	else if ((numCol == 11) && (numRow == 14) &&
	    (numBank == 8) && (width == 4))
		technology = 5;

	else if ((numCol == 10) && (numRow == 15) &&
	    (numBank == 8) && (width == 8))
		technology = 6;

	else if ((numCol == 11) && (numRow == 15) &&
	    (numBank == 8) && (width == 4))
		technology = 7;

	else if ((numCol == 10) && (numRow == 16) &&
	    (numBank == 8) && (width == 8))
		technology = 8;

	else if ((numCol == 11) && (numRow == 16) &&
	    (numBank == 8) && (width == 4))
		technology = 10;

	else {
		technology = -1;
		return (29);
	}

	if (PageHitMode) {

		*mySAddr |= ((column & BIT2) ? BIT5 : 0);
		*mySAddr |= ((column & BIT3) ? BIT6 : 0);
		*mySAddr |= ((bank & BIT0) ? BIT7 : 0);
		*mySAddr |= ((bank & BIT1) ? BIT8 : 0);
		*mySAddr |= ((row & BIT0) ? BIT9 : 0);
		*mySAddr |= ((row & BIT1) ? BIT10 : 0);
		*mySAddr |= ((row & BIT2) ? BIT11 : 0);
		*mySAddr |= ((row & BIT3) ? BIT12 : 0);
		*mySAddr |= ((row & BIT4) ? BIT13 : 0);
		*mySAddr |= ((row & BIT5) ? BIT14 : 0);
		*mySAddr |= ((row & BIT6) ? BIT15 : 0);
		*mySAddr |= ((row & BIT7) ? BIT16 : 0);
		*mySAddr |= ((row & BIT8) ? BIT17 : 0);
		*mySAddr |= ((row & BIT9) ? BIT18 : 0);
		*mySAddr |= ((row & BIT10) ? BIT19 : 0);
		*mySAddr |= ((row & BIT11) ? BIT20 : 0);
		*mySAddr |= ((row & BIT12) ? BIT21 : 0);
		*mySAddr |= ((column & BIT4) ? BIT22 : 0);
		*mySAddr |= ((column & BIT5) ? BIT23 : 0);
		*mySAddr |= ((column & BIT6) ? BIT24 : 0);
		*mySAddr |= ((column & BIT7) ? BIT25 : 0);
		*mySAddr |= ((column & BIT8) ? BIT26 : 0);
		*mySAddr |= ((column & BIT9) ? BIT27 : 0);

	} else if (!PageHitMode) {

		*mySAddr |= ((column & BIT2) ? BIT5 : 0);
		*mySAddr |= ((bank & BIT0) ? BIT6 : 0);
		*mySAddr |= ((bank & BIT1) ? BIT7 : 0);
		*mySAddr |= ((row & BIT0) ? BIT8 : 0);
		*mySAddr |= ((row & BIT1) ? BIT9 : 0);
		*mySAddr |= ((row & BIT2) ? BIT10 : 0);
		*mySAddr |= ((row & BIT3) ? BIT11 : 0);
		*mySAddr |= ((row & BIT4) ? BIT12 : 0);
		*mySAddr |= ((row & BIT5) ? BIT13 : 0);
		*mySAddr |= ((row & BIT6) ? BIT14 : 0);
		*mySAddr |= ((row & BIT7) ? BIT15 : 0);
		*mySAddr |= ((row & BIT8) ? BIT16 : 0);
		*mySAddr |= ((row & BIT9) ? BIT17 : 0);
		*mySAddr |= ((row & BIT10) ? BIT18 : 0);
		*mySAddr |= ((row & BIT11) ? BIT19 : 0);
		*mySAddr |= ((row & BIT12) ? BIT20 : 0);
		*mySAddr |= ((column & BIT3) ? BIT21 : 0);
		*mySAddr |= ((column & BIT4) ? BIT22 : 0);
		*mySAddr |= ((column & BIT5) ? BIT23 : 0);
		*mySAddr |= ((column & BIT6) ? BIT24 : 0);
		*mySAddr |= ((column & BIT7) ? BIT25 : 0);
		*mySAddr |= ((column & BIT8) ? BIT26 : 0);
		*mySAddr |= ((column & BIT9) ? BIT27 : 0);

	}

	/* Map the technology specific bits for dual channel mode */
	switch (technology) {

	case 0 :
		break;

	case 1 :
		*mySAddr |= ((column & BIT11) ? BIT28 : 0);
		break;

	case 2 :
		*mySAddr |= ((row & BIT13) ? BIT28 : 0);
		break;

	case 3 :
		*mySAddr |= ((column & BIT11) ? BIT28 : 0);
		*mySAddr |= ((row & BIT13) ? BIT29 : 0);
		break;

	case 4 :
		*mySAddr |= ((bank & BIT2) ? BIT28 : 0);
		*mySAddr |= ((row & BIT13) ? BIT29 : 0);
		break;

	case 5 :
		*mySAddr |= ((bank & BIT2) ? BIT28 : 0);
		*mySAddr |= ((row & BIT13) ? BIT29 : 0);
		*mySAddr |= ((column & BIT11) ? BIT30 : 0);
		break;

	case 6 :
		*mySAddr |= ((bank & BIT2) ? BIT28 : 0);
		*mySAddr |= ((row & BIT13) ? BIT29 : 0);
		*mySAddr |= ((row & BIT14) ? BIT30 : 0);
		break;

	case 7 :
		*mySAddr |= ((bank & BIT2) ? BIT28 : 0);
		*mySAddr |= ((row & BIT13) ? BIT29 : 0);
		*mySAddr |= ((row & BIT14) ? BIT30 : 0);
		*mySAddr |= ((column & BIT11) ? BIT31 : 0);
		break;

	case 8 :
		*mySAddr |= ((bank & BIT2) ? BIT28 : 0);
		*mySAddr |= ((row & BIT13) ? BIT29 : 0);
		*mySAddr |= ((row & BIT14) ? BIT30 : 0);
		*mySAddr |= ((row & BIT15) ? BIT31 : 0);
		break;

	case 10 :
		*mySAddr |= ((bank & BIT2) ? BIT28 : 0);
		*mySAddr |= ((row & BIT13) ? BIT29 : 0);
		*mySAddr |= ((row & BIT14) ? BIT30 : 0);
		*mySAddr |= ((row & BIT15) ? BIT31 : 0);
		*mySAddr |= ((column & BIT11) ? BIT32 : 0);
		break;

	case -1 :
		break;

	default :
		break;

	}

	return (20);

}

uint64_t
dimm_getphys(uint16_t branch, uint16_t rank, uint64_t bank,
    uint64_t ras, uint64_t cas)
{
	int i, j;
	int error = 0;
	int dmirRank[MAX_DMIR_NUMBER][MAX_DMIR_INTERLEAVE];
	int mirWay[MAX_MIR_NUMBER][MAX_MIR_INTERLEAVE];
	int tolmTolm = 1, mcMirror = 0;
	int rstVal;
	int count = 0, position = 0;
	int dmirMult = 0, flag = 0, mirMult = 0;
	uint32_t regvalue32;
	uint16_t regvalue16;
	uint64_t mySAddr = 0, PhyAddr = 0;
	uint64_t dmirLimit[MAX_DMIR_NUMBER];
	uint64_t mirLimit[MAX_MIR_NUMBER];
	uint64_t dmirTop = 0, dmirAddr = 0, addrRem = 0;
	uint64_t mirTop = 0, mirAddr = 0, lowMemLimit;
	uint64_t tolmGap, tolmAddr = 0;

	rstVal  = nb_chipset == INTEL_NB_5100 ?
	    SCcreateSAddr(&mySAddr, cas, ras, bank, branch, rank) :
	    createSAddr(&mySAddr, cas, ras, bank, branch, rank);
	mySAddr &= CGL_CACHELINE_MASK;
	if (rstVal != 20) {
		if (intel_nb5000_memtrans_debug)
			cmn_err(CE_WARN, "createSAdrr() error: %d\n", rstVal);
		return (-1ULL);
	}

	if (intel_nb5000_memtrans_debug)
		cmn_err(CE_WARN, "SAddr = %llx\n", (long long)mySAddr);

	/* get DMIR */
	for (i = 0; i < MAX_DMIR_NUMBER; i++) {
		regvalue32 = DMIR_RD(branch, i);
		dmirLimit[i] = DMIR_LIMIT(regvalue32);
		if (intel_nb5000_memtrans_debug)
			cmn_err(CE_WARN,
			    "regvalue32 = %x, dmirLImit[%d] = %llx\n",
			    regvalue32, i, (long long)dmirLimit[i]);
		/* get DMIR RANK of this branch */
		DMIR_RANKS(regvalue32, dmirRank[i][0], dmirRank[i][1],
		    dmirRank[i][2], dmirRank[i][3]);
	}

	/* get MIR */
	for (i = 0; i < MAX_MIR_NUMBER; i++) {
		regvalue16 = MIR_RD(i);
		mirLimit[i] = (regvalue16 & 0xfff0) >> 4;
		mirWay[i][0] = regvalue16 & 0x0001; /* get way0 */
		mirWay[i][1] = (regvalue16 & 0x0002) >> 1; /* get way1 */
	}

	regvalue16 = TOLM_RD();
	tolmTolm = (regvalue16 & 0xf000) >> 12;

	lowMemLimit = tolmTolm;

	/* convert to G */
	lowMemLimit <<= DRAM_GRANULARITY;

	/* CGL_FOURGIG is 4G */
	tolmGap = CGL_FOURGIG - lowMemLimit;

	regvalue32 = MC_RD();
	mcMirror = (regvalue32 & MC_MIRROR) >> 16;

	for (i = 0; i <= MAX_DMIR_NUMBER - 1; i++) {
		dmirTop = dmirLimit[i];
		dmirTop <<= DRAM_GRANULARITY;
		position = -1;
		count = 0;

		for (j = 0; j < MAX_DMIR_INTERLEAVE; j++) {
			if (dmirRank[i][j] == rank) {
				count++;
				if (position == -1)
					position = j;
			}
		}

		if (count != 0) {
			switch (count) {
			case 1 :
				dmirMult = 4;
				break;
			case 2 :
				dmirMult = 2;
				break;
			case 4 :
				dmirMult = 1;
				break;
			default :
				error = 1;
				rstVal = 3;
				break;
			}

			if (flag == 0) {	/* first pass */
				dmirAddr += mySAddr * dmirMult;
				flag = 1;
			} else {	/* subsequent pass */
				dmirAddr += addrRem * dmirMult;
			}

			if (dmirAddr > dmirTop) {
				if (i == (MAX_DMIR_NUMBER - 1)) {
					error = 1;
					rstVal = 4;
					break;
				} else  {
					if (dmirMult != 0) {
						addrRem = (dmirAddr -
						    dmirTop) / dmirMult;
						dmirAddr = dmirTop;
					}
				}
			} else {
				i = MAX_DMIR_NUMBER - 1;
			}
		} else {
			dmirAddr = dmirTop;

		}
	}

	if (intel_nb5000_memtrans_debug)
		cmn_err(CE_WARN, "dimm_getphys() dmirAddr = %llx\n",
		    (long long)dmirAddr);

	/*
	 * Find out which branches are enabled and calculate
	 * address in the MIR range
	 */

	mirTop = mirLimit[0];
	/* convert to G */
	mirTop <<= DRAM_GRANULARITY;

	if ((mirWay[0][branch ? 0 :1]) && (!mcMirror))
		mirMult = 2;
	else
		mirMult = 1;

	if (((mirWay[0][branch ? 1 :0]) && (!mcMirror)) || mcMirror) {
		mirAddr = dmirAddr * mirMult;
	} else {
		mirMult = 0;
		mirAddr = dmirAddr + mirTop;
	}

	if (mirAddr >= mirTop) {
		if (mcMirror) {
			error = 1;
			rstVal = 5;
		}

		if (mirWay[1][branch] == 0) {
			error = 1;
			rstVal = 6;
		}

		if (mirMult == 0) {
			if ((mirWay[1][branch ? 0 :1]) && (!mcMirror)) {
				mirAddr = (2 * mirAddr) - mirTop;
				mirMult = 2;
			} else {
				mirMult = 1;
			}
		}

		if (mirMult == 1) {
			if ((mirWay[1][branch ? 0 :1]) && (!mcMirror)) {
				mirAddr = (2 * mirAddr) - mirTop;
				mirMult = 2;
			} else {
				mirMult = 1;
			}
		}

		if (mirMult == 2) {
			if ((mirWay[1][branch ? 0 : 1]) && (!mcMirror)) {
				mirMult = 2;
			} else {
				mirAddr -= (mirAddr - mirTop) / 2;
				mirMult = 1;
			}
		}

		mirTop = mirLimit[1];
		mirTop <<= DRAM_GRANULARITY;

		if (mirAddr >= mirTop) {
			error = 1;
			rstVal = 7;
		}
	}

	if (intel_nb5000_memtrans_debug)
		cmn_err(CE_WARN, "dimm_getphys() mirAddr = %llx\n",
		    (long long)mirAddr);

	/* Compensate for TOLM shift */

	if (mirAddr >= lowMemLimit)
		tolmAddr = mirAddr + tolmGap;
	else
		tolmAddr = mirAddr;


	if (((mirMult == 1) && (dmirMult == 1)) ||
	    ((mcMirror) && (dmirMult == 1)))
		PhyAddr = tolmAddr;

	else if ((mirMult == 2) && (dmirMult == 1))
		PhyAddr = tolmAddr + (CGL_CACHELINE * branch);

	else if (((mirMult == 1) && ((dmirMult == 2) || (dmirMult == 4))) ||
	    ((mcMirror) && ((dmirMult == 2) || (dmirMult == 4))))
		PhyAddr = tolmAddr + (CGL_CACHELINE * position);

	else if ((mirMult == 2) && ((dmirMult == 2) || (dmirMult == 4)))
		PhyAddr = tolmAddr + (CGL_CACHELINE *
		    ((position * 2) + branch));

	else {
		PhyAddr = 0;
		error = 1;
		rstVal = 8;
	}


	/* Clean up and exit function */
	if (error) {
		if (intel_nb5000_memtrans_debug)
			cmn_err(CE_WARN, "dimm_getphys() error: %d\n", rstVal);
		return (-1ULL);
	}
	if (intel_nb5000_memtrans_debug)
		cmn_err(CE_WARN, "PhyAddr = %llx\n", (long long)PhyAddr);

	return (PhyAddr);

}
