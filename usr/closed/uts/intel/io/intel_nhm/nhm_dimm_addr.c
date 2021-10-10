/*
 * INTEL CONFIDENTIAL
 * SPECIAL INTEL MODIFICATIONS
 *
 * Copyright 2008 Intel Corporation All Rights Reserved.
 *
 * Approved for Solaris or OpenSolaris use only.
 * Approved for binary distribution only.
 *
 */

#include <sys/types.h>
#include <sys/time.h>
#include <sys/fm/protocol.h>
#include <sys/sunddi.h>
#include <sys/cmn_err.h>
#include "intel_nhm.h"
#include "mem_addr.h"


/*
 * convert_rankaddr()
 * Converts a channel address to a rank address given Node, Ch, RIR index,
 * and number of RIR ways
 */
static uint64_t
convert_rankaddr(uint64_t chanaddr, int node, int channel, int rule,
    int way, int num_ways)
{
	uint64_t rankaddr, tmp;

	/* Add RIR offset to CA[37:28] (stored in 2's complement) */
	rankaddr = (chanaddr >> RIR_LIMIT_GRANULARITY);
	rankaddr += rir[node][channel][rule].way[way].offset;
	rankaddr &= RIR_OFFSET_SIZE_MASK; /* Drop any carry bits */
	rankaddr = (rankaddr << RIR_LIMIT_GRANULARITY) |
	    (chanaddr & RIR_OFFSET_ADDR_MASK); /* Add back in lower bits */

	/* Remove interleaved bits from address */
	if (! closed_page) {
		if (num_ways == 2) {
			/* Remove CA[12] for 2-way interleave */
			tmp = (rankaddr >> RIR_INTLV_PGOPEN_BIT);
			rankaddr = ((tmp >> 1) << RIR_INTLV_PGOPEN_BIT) |
			    (rankaddr & RIR_INTLV_PGOPEN_MASK);
		} else if (num_ways == 4) {
			/* Remove CA[13:12] for 4-way interleave */
			tmp = (rankaddr >> RIR_INTLV_PGOPEN_BIT);
			rankaddr = ((tmp >> 2) << RIR_INTLV_PGOPEN_BIT) |
			    (rankaddr & RIR_INTLV_PGOPEN_MASK);
		}
	} else {
		if (num_ways == 2) {
			/* Remove CA[6] for 2-way interleave */
			tmp = (rankaddr >> RIR_INTLV_PGCLS_BIT);
			rankaddr = ((tmp >> 1) << RIR_INTLV_PGCLS_BIT) |
			    (rankaddr&RIR_INTLV_PGCLS_MASK);
		} else if (num_ways == 4) {
			/* Remove CA[7:6] for 2-way interleave */
			tmp = (rankaddr >> RIR_INTLV_PGCLS_BIT);
			rankaddr = ((tmp >> 2) << RIR_INTLV_PGCLS_BIT) |
			    (rankaddr & RIR_INTLV_PGCLS_MASK);
		}
	}
	return (rankaddr);
}

static uint64_t
rankaddr_to_chanaddr(int rule, int node, int channel, int way,
    int num_ways, uint64_t rankaddr)
{
	uint64_t rankaddr_lo, rankaddr_hi, rankaddr_copy;
	uint64_t tmp;
	uint64_t chanaddr_lo, chanaddr_hi, chanaddr = -1ULL;

	/*
	 * Get range of channel addresses that participate in this RIR rule
	 * (based on RIR limits)
	 */
	chanaddr_lo = ((rule == 0) ? 0 :
	    rir[node][channel][rule - 1].limit);
	chanaddr_hi = rir[node][channel][rule].limit - 1;

	/*
	 * Convert this to range of rank addresses
	 */
	rankaddr_lo = convert_rankaddr(chanaddr_lo, node, channel, rule,
	    way, num_ways);
	rankaddr_hi = convert_rankaddr(chanaddr_hi, node, channel, rule,
	    way, num_ways);

	/* Check if this rank address falls in the range covered by this RIR */
	if ((rankaddr >= rankaddr_lo) && (rankaddr <= rankaddr_hi)) {

		/*
		 * Insert the interleaved bits that were removed when channel
		 * address was translated to this rank address
		 */
		rankaddr_copy = rankaddr;
		if (!closed_page) {
			if (num_ways == 2) {
				/*
				 * Set CA[12] based on RIR way for 2-way
				 * interleaving in page open mode
				 */
				tmp = (rankaddr_copy >> RIR_INTLV_PGOPEN_BIT);
				rankaddr_copy = (((tmp << 1) |
				    (way % num_ways)) << RIR_INTLV_PGOPEN_BIT)
				    | (rankaddr_copy & RIR_INTLV_PGOPEN_MASK);
			} else if (num_ways == 4) {
				/*
				 * Set CA[13:12] based on RIR way for 2-way
				 * interleaving in page open mode
				 */
				tmp = (rankaddr_copy >> RIR_INTLV_PGOPEN_BIT);
				rankaddr_copy = (((tmp <<2) | (way % num_ways))
				    << RIR_INTLV_PGOPEN_BIT) | (rankaddr_copy &
				    RIR_INTLV_PGOPEN_MASK);
			}
		} else {
			if (num_ways == 2) {
				/*
				 * Set CA[6] based on RIR way for 2-way
				 * interleaving in page close mode
				 */
				tmp = (rankaddr_copy >> RIR_INTLV_PGCLS_BIT);
				rankaddr_copy = (((tmp << 1) |
				    (way % num_ways)) << RIR_INTLV_PGCLS_BIT)
				    | (rankaddr_copy & RIR_INTLV_PGCLS_MASK);
			} else if (num_ways == 4) {
				/*
				 * Set CA[7:6] based on RIR way for
				 * 4-way interleaving in page close
				 * mode
				 */
				tmp = (rankaddr_copy >> RIR_INTLV_PGCLS_BIT);
				rankaddr_copy = (((tmp << 2) |
				    (way % num_ways)) << RIR_INTLV_PGCLS_BIT)
				    | (rankaddr_copy & RIR_INTLV_PGCLS_MASK);
			}
		}

		/* Convert to channel address by subtracting RIR offset */
		chanaddr = ((rankaddr_copy >> RIR_LIMIT_GRANULARITY) -
		    rir[node][channel][rule].way[way].offset);
		chanaddr &= RIR_OFFSET_SIZE_MASK;
		chanaddr = (chanaddr << RIR_LIMIT_GRANULARITY);
		chanaddr |= (rankaddr_copy & RIR_OFFSET_ADDR_MASK);
	}
	return (chanaddr);
}

static void
divby3_process(int node, int channel, int rule, int tgt,
    uint64_t *chanaddr_copy)
{
	uint64_t tmp1, tmp2;
	uint64_t lower_bit_exp, lower_bit_act;
	int i;
	for (i = 0; i < 6; ++i) {
		/* Generate candidate address */
		tmp1 = (((*chanaddr_copy >> NUM_CACHELINE_BITS) * 3 + i) <<
		    NUM_CACHELINE_BITS) | (*chanaddr_copy &
		    CACHELINE_ADDR_MASK);
		/*
		 * Skip this candidate if would result in SA[6] contradicting
		 * what we know the real value is
		 */
		lower_bit_exp = (tgt >= 4) ? 1 : 0;
		lower_bit_act = (tmp1 >> NUM_CACHELINE_BITS) & 0x1;
		if (lower_bit_exp != lower_bit_act &&
		    (sag_ch[node][channel][rule].remove6 &&
		    !sag_ch[node][channel][rule].remove7 &&
		    !sag_ch[node][channel][rule].remove8))
			continue;
		/* Add SAG offset */
		tmp2 = (tmp1 >> SAG_OFFSET_GRANULARITY) -
		    sag_ch[node][channel][rule].offset;
		tmp2 &= SAG_OFFSET_SIZE_MASK; /* Drop any carry bits */
		tmp2 = tmp2 << SAG_OFFSET_GRANULARITY;
		tmp2 |= (tmp1 & SAG_OFFSET_ADDR_MASK);
		/*
		 * Check if this address meets the MOD3
		 * interleave that was chosen
		 */
		if (((tmp2 >> NUM_CACHELINE_BITS) % 3) == (tgt % 4)) {
			*chanaddr_copy = tmp1;
			break;
		}
	}
}


/*
 * convert_chanaddr() - Converts a system address to a channel address
 * given Node, Ch, and TAD rule
 */
static uint64_t
convert_chanaddr(uint64_t phyaddr, int node, int channel, int rule)
{
	uint64_t chanaddr, tmp;

	/* Add SAG offset */
	chanaddr = (phyaddr >> SAG_OFFSET_GRANULARITY);
	chanaddr += sag_ch[node][channel][rule].offset;
	chanaddr &= SAG_OFFSET_SIZE_MASK; /* Drop any carry bits */
	chanaddr = ((chanaddr << SAG_OFFSET_GRANULARITY) |
	    (phyaddr & SAG_OFFSET_ADDR_MASK)); /* Add back in lower bits */

	/* Perform divide by 3 if necessary */
	if (divby3_enabled && sag_ch[node][channel][rule].divby3) {
		chanaddr = (((chanaddr >> NUM_CACHELINE_BITS) / 3) <<
		    NUM_CACHELINE_BITS) | (chanaddr & CACHELINE_ADDR_MASK);
	}

	/* Remove interleaved bits from address */
	if (sag_ch[node][channel][rule].remove6 &&
	    !sag_ch[node][channel][rule].remove7 &&
	    !sag_ch[node][channel][rule].remove8) {
		/* Remove SA[6] for 2-way interleaves */
		tmp = (chanaddr >> SAD_INTLV_DIRECT_BIT);
		chanaddr = ((tmp >> 1) << SAD_INTLV_DIRECT_BIT) |
		    (chanaddr&SAD_INTLV_ADDR_MASK);
	} else if (sag_ch[node][channel][rule].remove6 &&
	    sag_ch[node][channel][rule].remove7 &&
	    !sag_ch[node][channel][rule].remove8) {
		/* Remove SA[7:6] for 4-way interleaves */
		tmp = (chanaddr >> SAD_INTLV_DIRECT_BIT);
		chanaddr = ((tmp >> 2) << SAD_INTLV_DIRECT_BIT) |
		    (chanaddr&SAD_INTLV_ADDR_MASK);
	} else if (sag_ch[node][channel][rule].remove6 ||
	    sag_ch[node][channel][rule].remove7 ||
	    sag_ch[node][channel][rule].remove8) {
		return (-1ULL);
	}
	return (chanaddr);
}

uint64_t
rankaddr_to_dimm(uint64_t rankaddr, int node, int channel, int dimm,
    int writing, uint64_t *bank, uint64_t *row, uint64_t *column)
{
	uint64_t bankaddr, rowaddr, coladdr;
	int lockstep_offset;
	int banks, rows, cols;
	uint64_t rowbit_offset;
	int i;

	/*
	 * Set Bank address (assume only 3 bank bits)
	 */
	if ((banks = dod_reg[node][channel][dimm].NUMBank) <= 0)
		return (-1ULL);
	if (banks != 8)
		return (-1ULL);
	if (!closed_page)
		/* BankAddr[2:0] = RankAddr[19:18,12] for page open mode */
		bankaddr = (((rankaddr >>18) & 0x3) << 1) |
		    ((rankaddr >> 12) & 0x1);
	else
		/* BankAddr[2:0] = RankAddr[8:6] for page close mode */
		bankaddr = (rankaddr >> 6) & 0x7;

	/*
	 * Set Row address (12-16 bits depending on DIMM size)
	 */

	/* Lockstep moves some bits up by 1 position */
	lockstep_offset = lockstep[node];

	/* Get the number of rows */
	if ((rows = dod_reg[node][channel][dimm].NUMRow) <= 0)
		return (-1ULL);

	if (!closed_page) {
		/*
		 * RowAddr[11:0] = RankAddr[26:20,27,17:14]
		 * for page open mode
		 */
		rowaddr = (((rankaddr >> 20) & 0x7f) << 5) |
		    (((rankaddr >> 27) & 0x1) << 4) |
		    ((rankaddr >> 14) & 0xf);
		for (i = 12; i < rows; ++i) {
			/*
			 * RowAddr[15:12] = RankAddr[31:28]
			 * (or RankAddr[32:29] for Lockstep)
			 * for page open mode
			 */
			rowaddr |= (((rankaddr >> (i + 16 + lockstep_offset))
			    & 0x1) << i);
		}
	} else {
		/*
		 * RowAddr[11:0] = RankAddr[12:9,27,21:19,17:14]
		 * for page close mode
		 */
		rowaddr = (((rankaddr >> 9) & 0xf) << 8) |
		    (((rankaddr >> 27) & 0x1) << 7) |
		    (((rankaddr >> 19) & 0x7) << 4) |
		    ((rankaddr >> 14) & 0xf);
		for (i = 12; i < rows; ++i) {
			/*
			 * RowAddr[15:12] = RankAddr[31:28]
			 * (or RankAddr[32:29] for Lockstep)
			 * for page close mode
			 */
			rowaddr |= (((rankaddr >> (i + 16 + lockstep_offset))
			    & 0x1) << i);
		}
	}
	/* Get the number of cols */
	if ((cols = dod_reg[node][channel][dimm].NUMCol) < 0)
		return (-1ULL);

	/*
	 * Set Column address (10-12 bits depending on DIMM size,
	 * set bit 10 for AP)
	 */
	rowbit_offset = rows - 12 + lockstep_offset;
	if (!closed_page) {
		/* ColAddr[9:3] = RankAddr[13,11:6] for page open mode */
		coladdr = (((rankaddr >> 13) & 0x1) << 9) |
		    (((rankaddr >> 6) & 0x3f) << 3);
		if (cols > 10) {
			/*
			 * ColAddr[11] = RankAddr[28 + RowBits-12 + Lockstep]
			 * for page open mode
			 */
			coladdr |= (((rankaddr >> (28 + rowbit_offset))
			    & 0x1) << 11);
		}
		if (cols > 11) {
			/*
			 * ColAddr[13] = RankAddr[32 + RowBits-15 + Lockstep]
			 * for page open mode
			 */
			coladdr |= (((rankaddr >> (32 + rowbit_offset - 3))
			    & 0x1) << 13);
		}
	} else {
		/* ColAddr[9:3] = RankAddr[26:22,18,13] for page close mode */
		coladdr = (((rankaddr >> 22) & 0x1f) << 5) |
		    (((rankaddr >> 18) & 0x1) << 4) |
		    (((rankaddr >> 13) & 0x1) << 3);
		if (cols > 10) {
			/*
			 * ColAddr[11] = RankAddr[28 + RowBits-12 + Lockstep]
			 * for page close mode
			 */
			coladdr |= (((rankaddr >> (28 + rowbit_offset))
			    & 0x1) << 11);
		}
		if (cols > 11) {
			/*
			 * ColAddr[13] = RankAddr[32 + RowBits-15 + Lockstep]
			 * for page close mode
			 */
			coladdr |= (((rankaddr >> (32 + rowbit_offset - 3))
			    & 0x1) << 13);
		}
	}
	if (closed_page) {
		/* ColAddr[10] = 1 for page close mode (autoprecharge bit) */
		coladdr |= (0x1 << 10);
	}
	if (lockstep[node]) {
		/* Col[2,1] = RankAddr[28,5] in Lockstep mode */
		coladdr |= ((((rankaddr >> 28) & 0x1) << 2) |
		    (((rankaddr >> 5) & 0x1) << 1));
	} else {
		/* Col[2:0] = RankAddr[5:3] in Independent mode */
		coladdr |= ((rankaddr >> 3) & 0x7);
	}
	if (writing) {
		if (lockstep[node])
			/* Col[1:0] is 0 for writes in Lockstep mode */
			coladdr &= 0x3ffc;
		else
			/* Col[2:0] is 0 for writes in Independent mode */
			coladdr &= 0x3ff8;
	} else {
		if (ecc_enabled) {
			/*
			 * Col[1:0] is 0 for reads w/ ECC if in
			 * independent mode
			 */
			if (!lockstep[node] && !mirror_mode[node])
				coladdr &= 0x3ffc;
		}
	}

	*bank = bankaddr;
	*row = rowaddr;
	*column = coladdr;

	return (DDI_SUCCESS);
}


uint64_t
dimm_to_rankaddr(int node, int channel, int dimm, uint64_t rowaddr,
    uint64_t bankaddr, uint64_t coladdr, int *log_chan)
{
	uint64_t rankaddr;
	int lockstep_offset;
	int nrows, ncols;
	int rowbit_offset;
	int i;

	/* Determine logical channel (based on RAS modes) */
	if (mirror_mode[node] || lockstep[node])
		/*
		 * Ch0 is the only logical channel for Mirroing
		 * and Lockstep modes
		 */
		*log_chan = 0;
	else
		/*
		 * For Independent mode, PhysCh == LogCh
		 * Also, for Sparing mode, we assume we are in the
		 * pre-failover state so PhysCh == LogCh
		 */
		*log_chan = channel;

	/*
	 * PART 1: DRAM Address -> Rank Address
	 * (based on Table 20/Figure 6 in Memory Controller
	 * High Level Architecture Spec 2.0)
	 */

	/* Piece together rank address from Bank/Row/Col address bits */
	rankaddr = 0;
	lockstep_offset = (lockstep[node] ? 1 : 0);
	if ((nrows = dod_reg[node][channel][dimm].NUMRow) <= 0)
		return (-1ULL);
	if ((ncols = dod_reg[node][channel][dimm].NUMCol) < 0)
		return (-1ULL);
	rowbit_offset = nrows - 12 + lockstep_offset;
	if (!closed_page) {
		/*
		 * RankAddr[27:18] = {R[4],R[11:5],B[2:1]}
		 * for page open mode
		 */
		rankaddr |= ((((rowaddr >> 4) & 0x1) << 27) |
		    (((rowaddr >> 5) & 0x7f) << 20) |
		    (((bankaddr >> 1) & 0x3) << 18));
		/*
		 * RankAddr[17:6] = {R[3:0],C[9],B[0],C[8:3]}
		 * for page open mode
		 */
		rankaddr |= (((rowaddr & 0xf) << 14) | (
		    ((coladdr >> 9) & 0x1) << 13) |
		    (((bankaddr & 0x1) << 12) |
		    (((coladdr >> 3) & 0x3f) << 6)));
		for (i = 12; i < nrows; ++i) {
			/*
			 * RankAddr[31:28] = RowAddr[15:12] (RankAddr[32:29]
			 * for Lockstep) for page open mode
			 */
			rankaddr |= (((rowaddr >> i) & 0x1) <<
			    (16 + lockstep_offset + i)); /* Precedence */
		}
		if (ncols > 10) {
			/*
			 * RankAddr[28 + RowBits-12 + Lockstep] = ColAddr[11]
			 * for page open mode
			 */
			rankaddr |= (((coladdr >> 11) & 0x1) <<
			    (28 + rowbit_offset)); /* Precedence? */
		}
		if (ncols > 11) {
			/*
			 * RankAddr[32 + RowBits-15 + Lockstep] = ColAddr[13]
			 * for page open mode
			 */
			rankaddr |= (((coladdr >> 13) & 0x1) <<
			    (32 + rowbit_offset-3)); /* Precedence? */
		}
		if (lockstep[node]) {
			/*
			 * RankAddr[28] = ColAddr[2] for Lockstep mode
			 * and page open mode
			 */
			rankaddr |= (((coladdr >> 2) & 0x1) << 28);
			/*
			 * RankAddr[5] = ColAddr[1] for Lockstep mode
			 * and page open mode
			 */
			rankaddr |= (((coladdr >> 1) & 0x1) << 5);
		} else {
			/*
			 * RankAddr[5:3] = ColAddr[2:0] for Independent mode
			 * and page open mode
			 */
			rankaddr |= ((coladdr & 0x7) << 3);
		}

	} else {
		/*
		 * RankAddr[27:19] = {R[7],C[9:5],R[6:4]}
		 * for page close mode
		 */
		rankaddr |= ((((rowaddr >> 7) & 0x1) << 27) |
		    (((coladdr >> 5) & 0x1f) << 22) |
		    (((rowaddr >> 4) & 0x7) << 19));
		/* RankAddr[18:13] = {C[4],R[3:0],C[3]} for page close mode */
		rankaddr |= ((((coladdr >> 4) & 0x1) << 18) |
		    ((rowaddr & 0xf) << 14) |
		    (((coladdr >> 3) & 0x1) << 13));
		/* RankAddr[12:6] = {R[11:8],B[2:0]} for page close mode */
		rankaddr |= ((((rowaddr >> 8) & 0xf) << 9) |
		    ((bankaddr & 0x7) << 6));
		for (i = 12; i < nrows; ++i) {
			/*
			 * RankAddr[31:28] = RowAddr[15:12]
			 * (RankAddr[32:29] for Lockstep) for page
			 * mode
			 */
			rankaddr |= (((rowaddr >> i) & 0x1) <<
			    (16 + lockstep_offset + i)); /* Precedence?? */
		}
		if (ncols > 10) {
			/*
			 * RankAddr[28 + RowBits-12 + Lockstep] = ColAddr[11]
			 * for page close mode
			 */
			rankaddr |= (((coladdr >> 11) & 0x1) <<
			    (28 + rowbit_offset)); /* Precedence?? */
		}
		if (ncols > 11) {
			/*
			 * RankAddr[32 + RowBits-15 + Lockstep] = ColAddr[13]
			 * for page close mode
			 */
			rankaddr |= (((coladdr >> 13) & 0x1) <<
			    (32 + rowbit_offset - 3)); /* Precedence?? */
		}
		if (lockstep[node]) {
			/*
			 * RankAddr[28] = ColAddr[2] for Lockstep mode and
			 * page close mode
			 */
			rankaddr |= (((coladdr >> 2) & 0x1) << 28);
			/*
			 * RankAddr[5] = ColAddr[1] for Lockstep mode and
			 * page close mode
			 */
			rankaddr |= (((coladdr >> 1) & 0x1) << 5);
		} else {
			/*
			 * RankAddr[5:3] = ColAddr[2:0] for Independent mode
			 * and page close mode
			 */
			rankaddr |= ((coladdr & 0x7) << 3);
		}
	}

	return (rankaddr);
}

static uint64_t
chanaddr_to_phyaddr(int node, int channel, int rule, int num_ways,
    uint64_t chanaddr, int sad_rule, uint64_t *phyaddr)
{
	int tgt;
	uint64_t chanaddr_copy;
	uint64_t phyaddr_lo, phyaddr_hi;
	uint64_t chanaddr_lo, chanaddr_hi;
	uint64_t tmp, tgt_xor;

	/* Check if this channel participates in this TAD rule */
	for (tgt = 0; tgt < INTERLEAVE_NWAY; ++tgt) {
		if (sad[sad_rule].node_tgt[tgt] == ((node == 0) ? 1 : 2) &&
		    tad[node][rule].pkg_tgt[tgt] == channel) {
			/* Determine how wide the channel interleave is */
			if (!sag_ch[node][channel][rule].remove6 &&
			    !sag_ch[node][channel][rule].remove7 &&
			    !sag_ch[node][channel][rule].remove8) {
				if (tad[node][rule].mode == MOD3)
					/*
					 * MOD3 with no bit removal is
					 * 3-way interleaving
					 */
					num_ways = 3;
				else
					/*
					 * DIRECT/XOR with no bit removal
					 * is 1-way interleaving
					 */
					num_ways = 1;
			} else if (sag_ch[node][channel][rule].remove6 &&
			    !sag_ch[node][channel][rule].remove7 &&
			    !sag_ch[node][channel][rule].remove8) {
				if (tad[node][rule].mode == MOD3)
					/*
					 * MOD3 with 1 bit removed is
					 * 6-way interleaving
					 */
					num_ways = 6;
				else
					/*
					 * DIRECT/XOR with 1 bit removed
					 * is 2-way interleaving
					 */
					num_ways = 2;
			} else if (sag_ch[node][channel][rule].remove6 &&
			    sag_ch[node][channel][rule].remove7 &&
			    !sag_ch[node][channel][rule].remove8)
				/*
				 * DIRECT/XOR with 2 bits removed in
				 * 4-way interleave
				 */
				num_ways = 4;
			else {
				return (-1ULL);
			}

			/* Skip repeated or invalid targets */
			if ((num_ways != 6) && (tgt >= num_ways))
				/*
				 * Targets are repeated to fill up
				 * all 8 slots
				 */
				break;
			if (tad[node][rule].mode == MOD3 && ((tgt == 3) ||
			    (tgt == 7)))
				/* Target indices 3 & 7 are invalid for MOD3 */
				continue;
			/*
			 * Get range of system addresses that participate
			 * in this TAD rule (based on TAD limits)
			 */
			phyaddr_lo = (rule == 0) ? 0 :
			    tad[node][rule-1].limit;
			phyaddr_hi = tad[node][rule].limit - 1;

			/*
			 * Convert this to range of channel addresses
			 * that participate in this TAD rule
			 */
			chanaddr_lo = convert_chanaddr(phyaddr_lo, node,
			    channel, rule);
			chanaddr_hi = convert_chanaddr(phyaddr_hi, node,
			    channel, rule);
			/*
			 * Check if this channel address falls in the range
			 * covered by this TAD rule
			 */
			if ((chanaddr >= chanaddr_lo) &&
			    (chanaddr <= chanaddr_hi)) {
				/*
				 * Insert the interleaved bits that were
				 * removed when system address was translated
				 * to this channel address
				 */
				chanaddr_copy = chanaddr;
				if (sag_ch[node][channel][rule].remove6 &&
				    !sag_ch[node][channel][rule].remove7 &&
				    !sag_ch[node][channel][rule].remove8) {
					if (tad[node][rule].mode == DIRECT) {
						/*
						 * Set SA[6] based on target
						 * index for 2-way DIRECT
						 * interleaving
						 */
						tmp = (chanaddr_copy >>
						    SAD_INTLV_DIRECT_BIT);
						chanaddr_copy = (((tmp << 1)
						    | (tgt % num_ways)) <<
						    SAD_INTLV_DIRECT_BIT)|
						    (chanaddr_copy &
						    SAD_INTLV_ADDR_MASK);
					} else if
					    (tad[node][rule].mode == XOR) {
						/*
						 * Target index = SA[18:16] ^
						 * SA[8:6] therefore SA[8:6]=
						 * Target index ^
						 * SA[18:16] = Target index ^
						 * CA[17:15]
						 */
						tgt_xor = ((chanaddr_copy >>
						    15) & SAD_INTLV_SIZE_MASK)
						    ^ tgt;
						tmp = (chanaddr_copy >>
						    SAD_INTLV_DIRECT_BIT);
						chanaddr_copy = (((tmp << 1) |
						    (tgt_xor % num_ways)) <<
						    SAD_INTLV_DIRECT_BIT)|
						    (chanaddr_copy &
						    SAD_INTLV_ADDR_MASK);
					} else if (tad[node][rule].mode ==
					    MOD3) {
						/*
						 * Set SA[6] if target index
						 * is 4-7
						 */
						tmp = (chanaddr_copy >>
						    SAD_INTLV_DIRECT_BIT);
						chanaddr_copy = ((tmp << 1) <<
						    SAD_INTLV_DIRECT_BIT)|
						    (chanaddr_copy &
						    SAD_INTLV_ADDR_MASK);
					} else {
						return (-1ULL);
					}
				} else if
				    (sag_ch[node][channel][rule].remove6 &&
				    sag_ch[node][channel][rule].remove7 &&
				    !sag_ch[node][channel][rule].remove8) {
					if (tad[node][rule].mode == DIRECT) {
						/*
						 * Set SA[7:6] based on target
						 * index for 4-way DIRECT
						 * interleaving
						 */
						tmp = (chanaddr_copy >>
						    SAD_INTLV_DIRECT_BIT);
						chanaddr_copy = (((tmp << 2) |
						    (tgt % num_ways)) <<
						    SAD_INTLV_DIRECT_BIT)|
						    (chanaddr_copy &
						    SAD_INTLV_ADDR_MASK);
					} else if
					    (tad[node][rule].mode == XOR) {
						/*
						 * Target index = SA[18:16] ^
						 * SA[8:6] therefore SA[8:6]=
						 * Target index ^
						 * SA[18:16]
						 */
						tgt_xor = ((chanaddr_copy >>
						    14) & SAD_INTLV_SIZE_MASK)
						    ^ tgt;
						tmp = (chanaddr_copy >>
						    SAD_INTLV_DIRECT_BIT);
						chanaddr_copy = (((tmp << 2) |
						    (tgt_xor % num_ways)) <<
						    SAD_INTLV_DIRECT_BIT) |
						    (chanaddr_copy &
						    SAD_INTLV_ADDR_MASK);
					} else {
						return (-1ULL);
					}
				} else if
				    (sag_ch[node][channel][rule].remove6 ||
				    sag_ch[node][channel][rule].remove7 ||
				    sag_ch[node][channel][rule].remove8) {
					return (-1ULL);
				}

				/*
				 * Reverse divide by 3 operations if necessary
				 * Note: Since remainder of phyaddr->ChAddr
				 * division is not known, we need to try all
				 * 3 possibilities
				 */
				if (divby3_enabled &&
				    sag_ch[node][channel][rule].divby3) {
					divby3_process(node, channel,
					    rule, tgt, &chanaddr_copy);
				}

				/* Subtract SAG offset to get system address */
				*phyaddr = (chanaddr_copy >>
				    SAG_OFFSET_GRANULARITY) -
				    sag_ch[node][channel][rule].offset;
				/* Drop any carry bits */
				*phyaddr &= SAG_OFFSET_SIZE_MASK;
				*phyaddr = *phyaddr << SAG_OFFSET_GRANULARITY;
				*phyaddr |= (chanaddr_copy &
				    SAG_OFFSET_ADDR_MASK); /* Set lower bits */
				return (1);
			}
		}
	}
	return (0);
}

uint64_t
rankaddr_to_phyaddr(int node, int log_chan, int dimm, int rank,
    int rankaddr)
{
	/*
	 * Rank Address -> Channel Address
	 */
	uint64_t chanaddr;
	uint64_t phyaddr;
	int rule, sad_rule;
	int way, ways[MAX_RIR_WAY], num_ways;
	int i;

	/* Figure out which RIR rule this rank address maps to */
	chanaddr = -1ULL;
	for (rule = 0; rule < MAX_TAD_DRAM_RULE; ++rule) {

		/* Check if this rank participates in this RIR rule */
		for (way = 0; way < MAX_RIR_WAY; ++way) {
			if (rir[node][log_chan][rule].way[way].dimm ==
			    dimm && rir[node][log_chan][rule].way[way].rank ==
			    rank) {

				/* Determine how wide the rank interleave is */
				for (i = 0; i < MAX_RIR_WAY; ++i) {
					/*
					 * Pack DIMM and rank indices
					 * together so a single number
					 * can be compared
					 */
					ways[i] = (rir[node][log_chan][rule].
					    way[i].dimm << 2) + rir[node]
					    [log_chan][rule].way[i].rank;
				}
				if (ways[0] == ways[2] &&
				    ways[1] == ways[3] &&
				    ways[0] == ways[1])
					/*
					 * All ways the same means
					 * 1-way interleaving
					 */
					num_ways = 1;
				else if (ways[0] == ways[2] && ways[1] ==
				    ways[3] && ways[0] != ways[1])
					/*
					 * 2 pairs of ways means 2-way
					 * interleaving
					 */
					num_ways = 2;
				else if (ways[0] != ways[2] && ways[1] !=
				    ways[3] && ways[0] != ways[1])
					/*
					 * All ways different means
					 * 4-way interleaving
					 */
					num_ways = 4;
				else
					return (-1ULL);

				/* Skip duplicated ways */
				if (way >= num_ways)
					break;
				chanaddr = rankaddr_to_chanaddr(rule, node,
				    log_chan, way, num_ways, rankaddr);
				break;
			}
		}
		if (chanaddr != -1ULL)
			break;
	}

	/*
	 * Channel Address -> Physical Address
	 */

	/* Figure out which TAD rule this channel address maps to */

	phyaddr = -1ULL;

	for (rule = 0; rule < MAX_SAD_DRAM_RULE; ++rule) {
		/* Skip disabled rules */
		if (!tad[node][rule].enable)
			continue;

		/*
		 * Determine which SAD rule covers the
		 * range of this TAD rule
		 */
		for (sad_rule = 0; sad_rule < MAX_SAD_DRAM_RULE; ++sad_rule) {
			if ((sad[sad_rule].limit >= tad[node][rule].limit) &&
			    sad[sad_rule].enable)
				break;
		}
		if (sad_rule >= MAX_SAD_DRAM_RULE) {
			return (-1ULL);
		}
		if (chanaddr_to_phyaddr(node, log_chan, rule, num_ways,
		    chanaddr, sad_rule, &phyaddr) == 1)
			break;
	}

	/* Return encoded system address */
	return (phyaddr);
}

/*
 * chanaddr_to_rankaddr() - Converts a channel address to a rank address
 * given Node, Ch, RIR index, and number of RIR ways
 */
static uint64_t
chanaddr_to_rankaddr(uint64_t chanaddr, int node, int channel,
    int rir_rule_hit, int rir_way, int num_ways)
{
	uint64_t rankaddr, tmp;

	/* Add RIR offset to CA[37:28] (stored in 2's complement) */
	rankaddr = (chanaddr >> RIR_LIMIT_GRANULARITY);
	rankaddr += rir[node][channel][rir_rule_hit].way[rir_way].offset;
	rankaddr &= RIR_OFFSET_SIZE_MASK;
	rankaddr = (rankaddr<<RIR_LIMIT_GRANULARITY) |
	    (chanaddr&RIR_OFFSET_ADDR_MASK); /* Add back in lower bits */

	/* Remove interleaved bits from address */
	if (!closed_page) {
		if (num_ways == 2) {
			/*
			 * Remove CA[12] for 2-way interleave
			 * in page open mode
			 */
			tmp = (rankaddr >> RIR_INTLV_PGOPEN_BIT);
			rankaddr = ((tmp >> 1) << RIR_INTLV_PGOPEN_BIT) |
			    (rankaddr & RIR_INTLV_PGOPEN_MASK);
		} else if (num_ways == 4) {
			/*
			 * Remove CA[13:12] for 4-way interleave
			 * in page open mode
			 */
			tmp = (rankaddr >> RIR_INTLV_PGOPEN_BIT);
			rankaddr = ((tmp >> 2) << RIR_INTLV_PGOPEN_BIT) |
			    (rankaddr&RIR_INTLV_PGOPEN_MASK);
		}
	} else {
		if (num_ways == 2) {
			/*
			 * Remove CA[6] for 2-way interleave
			 * in page close mode
			 */
			tmp = (rankaddr >> RIR_INTLV_PGCLS_BIT);
			rankaddr = ((tmp >> 1) << RIR_INTLV_PGCLS_BIT) |
			    (rankaddr & RIR_INTLV_PGCLS_MASK);
		} else if (num_ways == 4) {
			/*
			 * Remove CA[7:6] for 2-way interleave
			 * in page close mode
			 */
			tmp = (rankaddr >> RIR_INTLV_PGCLS_BIT);
			rankaddr = ((tmp >> 2) << RIR_INTLV_PGCLS_BIT) |
			    (rankaddr & RIR_INTLV_PGCLS_MASK);
		}
	}
	return (rankaddr);
}


uint64_t
caddr_to_dimm(int node, int channel, uint64_t caddr, int *rank_p,
    uint64_t *rank_addr_p)
{
	int rir_rule_hit, rir_way, num_ways;
	int dimm_hit, i;
	int ways[MAX_RIR_WAY];

	/* Determine which RIR rule this address hits */
	for (rir_rule_hit = 0; rir_rule_hit < MAX_SAD_DRAM_RULE;
	    ++rir_rule_hit) {
		/*
		 * Compare CA[37:28] to LIMIT field
		 * for each RIR rule
		 */
		if (rir[node][channel][rir_rule_hit].limit - 1
		    >= caddr)
			break;
	}
	if (rir_rule_hit >= MAX_SAD_DRAM_RULE)
		return (-1ULL);

	/* Calculate which RIR way this address hits */
	if (!closed_page)
		/* RIR way = CA[13:12] for page open mode */
		rir_way = (int)((caddr >> RIR_INTLV_PGOPEN_BIT) &
		    RIR_INTLV_SIZE_MASK);
	else if (closed_page)
		/* RIR way = CA[7:6] for page close mode */
		rir_way = (int)((caddr >> RIR_INTLV_PGCLS_BIT) &
		    RIR_INTLV_SIZE_MASK);

	dimm_hit = rir[node][channel][rir_rule_hit].way[rir_way].dimm;
	*rank_p = rir[node][channel][rir_rule_hit].way[rir_way].rank;

	/* Determine how wide the rank interleave is */
	for (i = 0; i < MAX_RIR_WAY; i++) {
		/*
		 * Pack DIMM and rank indices together
		 * so a single number can be compared
		 */
		ways[i] = (rir[node][channel][rir_rule_hit].way[i].dimm <<
		    2) + rir[node][channel][rir_rule_hit].way[i].rank;
	}
	if (ways[0] == ways[2] && ways[1] == ways[3] && ways[0] == ways[1])
		/* All ways the same means 1-way interleaving */
		num_ways = 1;
	else if (ways[0] == ways[2] && ways[1] == ways[3] &&
	    ways[0] != ways[1])
		/* 2 pairs of ways means 2-way interleaving */
		num_ways = 2;
	else if (ways[0] != ways[2] && ways[1] != ways[3] &&
	    ways[0] != ways[1])
		/* All ways different means 4-way interleaving */
		num_ways = 4;
	else
		return (-1ULL);

	/*
	 * Remove interleave bits and add RIR offset
	 * (use function call so same code can be reused
	 * for reverse decoding)
	 */
	*rank_addr_p = chanaddr_to_rankaddr(caddr, node, channel,
	    rir_rule_hit, rir_way, num_ways);

	return (dimm_hit);
}
