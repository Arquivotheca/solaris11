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
 * Copyright 2010 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#include <sys/types.h>
#include <sys/time.h>
#include <sys/fm/protocol.h>
#include <sys/cpu_module_impl.h>
#include <sys/mc_intel.h>
#include "intel_nhm.h"
#include "nhm_log.h"
#include "mem_addr.h"

char closed_page;
char ecc_enabled;
char divby3_enabled;
char lockstep[2];
char mirror_mode[2];
char spare_channel[2];
sad_t sad[MAX_SAD_DRAM_RULE];
tad_t tad[MAX_CPU_NODES][MAX_TAD_DRAM_RULE];
sag_ch_t sag_ch[MAX_CPU_NODES][CHANNELS_PER_MEMORY_CONTROLLER]
	[MAX_TAD_DRAM_RULE];
rir_t rir[MAX_CPU_NODES][CHANNELS_PER_MEMORY_CONTROLLER]
	[MAX_TAD_DRAM_RULE];
dod_t dod_reg[MAX_CPU_NODES][CHANNELS_PER_MEMORY_CONTROLLER]
	[MAX_DIMMS_PER_CHANNEL];

static int
channel_in_interleave(int node, int channel, int rule, int *way_p,
    int *no_interleave_p)
{
	int way;
	int c;
	int i;
	uint32_t mc_channel_mapper;
	int lc;
	int rt = 0;
	int start = 0;

	if (lockstep[node] || mirror_mode[node]) {
		*no_interleave_p = 0;
		if (channel > 1)
			return (0);
		else
			return (1);
	}
	mc_channel_mapper = MC_CHANNEL_MAPPER_RD(node);
	lc = -1;
	c = 1 << channel;
	for (i = 0; i < CHANNELS_PER_MEMORY_CONTROLLER; i++) {
		if ((CHANNEL_MAP(mc_channel_mapper, i, 0) & c) != 0) {
			lc = i;
			break;
		}
	}
	if (lc == -1) {
		for (i = 0; i < CHANNELS_PER_MEMORY_CONTROLLER; i++) {
			if ((CHANNEL_MAP(mc_channel_mapper, i, 1) & c) != 0) {
				lc = i;
				break;
			}
		}
	}
	if (lc == -1) {
		return (0);
	}
	*way_p = 0;
	*no_interleave_p = 0;
	if (node && tad[node][rule].mode == 2)
		start = 4;
	for (way = start; way < INTERLEAVE_NWAY; way++) {
		if (lc == TAD_INTERLEAVE(tad[node][rule].pkg_list, way)) {
			*way_p = way;
			if (way == 0) {
				for (i = way + 1; i < INTERLEAVE_NWAY; i++) {
					c = TAD_INTERLEAVE(
					    tad[node][rule].pkg_list, i);
					if (lc != c) {
						break;
					}
				}
				if (i == INTERLEAVE_NWAY)
					*no_interleave_p = 1;
			}
			rt = 1;
			break;
		}
	}
	return (rt);
}

int
address_to_node(uint64_t addr, int *interleave_p)
{
	int i;
	int node = -1;
	uint64_t base;
	int way;
	uchar_t package;

	base = 0;
	for (i = 0; i < MAX_SAD_DRAM_RULE; i++) {
		if (sad[i].enable && addr >= base && addr < sad[i].limit) {
			switch (sad[i].mode) {
			case 0:
				way = (addr >> 6) & 7;
				break;
			case 1:
				way = ((addr >> 6) & 7) ^ ((addr >> 16) & 7);
				break;
			case 2:
				way = ((addr >> 4) & 4) |
				    (((addr >> 6) & 0x3ffffffff) % 3);
				break;
			default:
				return (-1);
			}
			package = SAD_INTERLEAVE(sad[i].node_list, way);
			if (interleave_p)
				*interleave_p = sad[i].interleave;
			if (package == 1)
				node = 0;
			else if (package == 2)
				node = 1;
			else
				node = -1;
			break;
		}
		base = sad[i].limit;
	}
	return (node);
}

static uint64_t
channel_address(int node, int channel, int rule, uint64_t addr)
{
	uint64_t caddr;

	if (lockstep[node] || mirror_mode[node])
		channel = 0;
	caddr = (((addr >> 16) +
	    (int64_t)sag_ch[node][channel][rule].soffset) << 16) |
	    (addr & 0xffc0);
	if (sag_ch[node][channel][rule].remove8) {
		caddr = ((caddr >> 1) & ~0xff) | (caddr & 0xff);
	}
	if (sag_ch[node][channel][rule].remove7) {
		caddr = ((caddr >> 1) & ~0x7f) | (caddr & 0x7f);
	}
	if (sag_ch[node][channel][rule].remove6) {
		caddr = ((caddr >> 1) & ~0x3f) | (caddr & 0x3f);
	}
	caddr = caddr & 0x1fffffffff;
	if (sag_ch[node][channel][rule].divby3) {
		caddr = ((((caddr >> 6) / 3) << 6) & 0x1fffffffc0) |
		    (caddr & 0x3f);
	}
	return (caddr);
}

int
address_to_channel(int node, uint64_t addr, int write,
    int *log_chan, uint64_t *channel_addrp, int *interleave_p)
{
	int i;
	int channel = -1;
	uint64_t base;
	uint32_t mapper;
	uint32_t lc;
	int way;

	base = 0;
	for (i = 0; i < MAX_TAD_DRAM_RULE; i++) {
		if (tad[node][i].enable && addr >= base &&
		    addr < tad[node][i].limit) {
			switch (tad[node][i].mode) {
			case 0:
				way = (addr >> 6) & 7;
				break;
			case 1:
				way = ((addr >> 6) & 7) ^ ((addr >> 16) & 7);
				break;
			case 2:
				way = ((addr >> 4) & 4) |
				    (((addr >> 6) & 0x3ffffffff) % 3);
				break;
			default:
				return (-1);
			}
			/* get logical channel number */
			channel = TAD_INTERLEAVE(tad[node][i].pkg_list, way);
			if (log_chan)
				*log_chan = channel;

			if (channel_addrp) {
				*channel_addrp = channel_address(node,
				    channel, i, addr);
			}
			if (interleave_p)
				*interleave_p = tad[node][i].interleave;
			break;
		}
		base = tad[node][i].limit;
	}
	if (!lockstep[node] && channel != -1) {
		mapper = MC_CHANNEL_MAPPER_RD(node);
		lc = CHANNEL_MAP(mapper, channel, write);
		switch (lc) {
		case 1:
			channel = 0;
			break;
		case 2:
			channel = 1;
			break;
		case 4:
			channel = 2;
			break;
		case 3:		/* mirror PCH0 and PCH1 */
			if (!write) {
				if (((addr >> 24) & 1) ^ ((addr >> 12) & 1) ^
				    ((addr >> 6) & 1))
					channel = 1;
				else
					channel = 0;
			}
			break;
		case 5:		/* sparing PCH0 to PCH2 */
			channel = 0;
			break;
		case 6:		/* sparing PCH1 to PCH2 */
			channel = 1;
			break;
		}
	}
	return (channel);
}

int
channels_interleave(uint64_t addr)
{
	int node;
	int sinterleave;
	int channels, channels1;

	node = address_to_node(addr, &sinterleave);
	if (sinterleave == 1) {
		channels = 0;
		(void) address_to_channel(node, addr, 0, 0, 0, &channels);
	} else {
		channels = 0;
		channels1 = 0;
		(void) address_to_channel(0, addr, 0, 0, 0, &channels);
		(void) address_to_channel(1, addr, 0, 0, 0, &channels1);
		channels += channels1;
	}
	return (channels);
}

int
channel_addr_to_dimm(int node, int channel, uint64_t caddr, int *rank_p,
    uint64_t *rank_addr_p)
{
	int i;
	uint64_t base;
	uint64_t rank_addr;
	int rank;
	int dimm;
	int way;

	dimm = -1;
	rank = -1;
	base = 0;
	rank_addr = -1ULL;
	for (i = 0; i < MAX_TAD_DRAM_RULE; i++) {
		if (caddr >= base && caddr < rir[node][channel][i].limit) {
			if (closed_page) {
				way = (caddr >> 6) & 3;
				rank_addr = (((caddr + (int64_t)
				    rir[node][channel][i].way[way].offset *
				    VRANK_SZ) /
				    rir[node][channel][i].interleave) &
				    ~0x3f) + (caddr & 0x3f);
			} else {
				way = (caddr >> 12) & 3;
				rank_addr = (((caddr + (int64_t)
				    rir[node][channel][i].way[way].offset *
				    VRANK_SZ) /
				    rir[node][channel][i].interleave) &
				    ~0xfff) + (caddr & 0xfff);
			}
			rank = rir[node][channel][i].way[way].rank;
			dimm = rank >> 2;
			break;
		}
		base = rir[node][channel][i].limit;
	}
	*rank_p = rank;
	*rank_addr_p = rank_addr;
	return (dimm);
}

static int
socket_interleave(uint64_t addr, int node, int channel, int rule,
    int *way_p)
{
	int i, j;
	uint64_t base;
	uchar_t package;
	uchar_t xp;
	uchar_t xc;
	int ot = 0;
	int mode;
	int start;
	int rt = 1;
	int found = 0;

	if (mirror_mode[node] || lockstep[node])
		channel = 0;
	package = node + 1;
	mode = tad[node][rule].mode;
	base = 0;
	for (i = 0; i < MAX_SAD_DRAM_RULE; i++) {
		if (sad[i].enable && addr >= base && addr < sad[i].limit) {
			if (mode == 2) {
				for (j = 0; j < INTERLEAVE_NWAY; j++) {
					xp = SAD_INTERLEAVE(sad[i].node_list,
					    j);
					if (package != xp) {
						ot++;
						if (found) {
							rt = 2;
							break;
						}
					} else {
						found = 1;
						if (ot) {
							rt = 2;
							break;
						}
					}
				}
			} else {
				if (mode == 2)
					start = *way_p;
				else
					start = 0;
				for (j = start; j < INTERLEAVE_NWAY; j++) {
					xp = SAD_INTERLEAVE(sad[i].node_list,
					    j);
					if (package != xp) {
						ot++;
						if (found) {
							rt = 2;
							break;
						}
					} else if (!found) {
						xc = TAD_INTERLEAVE(
						    tad[node][rule].pkg_list,
						    j);
						if (channel == xc) {
							*way_p = j;
							if (ot) {
								rt = 2;
								break;
							}
							found = 1;
						}
					}
				}
			}
			break;
		}
		base = sad[i].limit;
	}
	return (rt);
}

uint64_t
dimm_to_addr(int node, int channel, int rank, uint64_t rank_addr,
    uint64_t *rank_base_p, uint64_t *rank_sz_p, uint32_t *socket_interleave_p,
    uint32_t *channel_interleave_p, uint32_t *rank_interleave_p,
    uint32_t *socket_way_p, uint32_t *channel_way_p, uint32_t *rank_way_p)
{
	int i;
	int way, xway;
	uint64_t addr;
	uint64_t caddr;
	uint64_t cbaddr;
	uint64_t baddr;
	uint64_t rlimit;
	uint64_t rank_sz;
	uint64_t base;
	int lchannel;
	int bits;
	int no_interleave;
	int sinterleave;
	int cinterleave;
	int rinterleave;
	int found = 0;

	if (lockstep[node] || mirror_mode[node])
		lchannel = 0;
	else
		lchannel = channel;
	addr = -1;
	base = 0;
	for (i = 0; i < MAX_TAD_DRAM_RULE && found == 0; i++) {
		for (way = 0; way < MAX_RIR_WAY; way++) {
			if (rir[node][channel][i].way[way].dimm_rank == rank) {
				rlimit = rir[node][channel][i].way[way].rlimit;
				if (rlimit && rank_addr >= rlimit)
					continue;
				cbaddr = base;
				if (closed_page) {
					caddr = (rank_addr & ~0x3f) *
					    rir[node][channel][i].interleave -
					    (int64_t)rir[node][channel][i].
					    way[way].soffset * VRANK_SZ;
					caddr += way << 6;
					caddr |= rank_addr & 0x3f;
				} else {
					caddr = (rank_addr & ~0xfff) *
					    rir[node][channel][i].interleave -
					    (int64_t)rir[node][channel][i].
					    way[way].soffset * VRANK_SZ;
					caddr += way << 12;
					caddr |= rank_addr & 0xfff;
				}
				if (caddr < rir[node][channel][i].limit) {
					rinterleave =
					    rir[node][channel][i].interleave;
					rank_sz = (rir[node][channel][i].limit -
					    base) / rinterleave;
					found = 1;
					if (rank_interleave_p) {
						*rank_interleave_p =
						    rinterleave;
					}
					if (rank_way_p)
						*rank_way_p = way;
					break;
				}
			}
		}
		base = rir[node][channel][i].limit;
	}
	if (!found)
		return (-1ULL);
	base = 0;
	for (i = 0; i < MAX_TAD_DRAM_RULE; i++) {
		way = 0;
		if (tad[node][i].enable &&
		    channel_in_interleave(node, channel, i, &way,
		    &no_interleave)) {
			bits = 0;
			addr = caddr;
			baddr = cbaddr;
			if (sag_ch[node][lchannel][i].divby3) {
				addr = (((addr >> 6) * 3) << 6) +
				    (addr & 0x3f);
				baddr = (((baddr >> 6) * 3) << 6);
			}
			if (sag_ch[node][lchannel][i].remove6) {
				bits = 1;
				addr = ((addr & ~0x3f) << 1) | (addr & 0x3f);
				baddr = (baddr & ~0x3f) << 1;
			}
			if (sag_ch[node][lchannel][i].remove7) {
				bits =  bits | 2;
				addr = ((addr & ~0x7f) << 1) | (addr & 0x7f);
				baddr = ((baddr & ~0x7f) << 1) | (baddr & 0x40);
			}
			if (sag_ch[node][lchannel][i].remove8) {
				bits =  bits | 4;
				addr = ((addr & ~0xff) << 1) | (addr & 0xff);
				baddr = ((baddr & ~0xff) << 1) | (baddr & 0xc0);
			}
			addr -= (int64_t)sag_ch[node][lchannel][i].soffset <<
			    16;
			baddr -= (int64_t)
			    sag_ch[node][lchannel][i].soffset << 16;
			if (addr < tad[node][i].limit) {
				/*
				 * this is the target address descripter to use
				 */
				sinterleave = socket_interleave(addr,
				    node, channel, i, &way);
				if (socket_interleave_p) {
					*socket_interleave_p = sinterleave;
				}
				if (socket_way_p)
					*socket_way_p = way;
				if ((no_interleave && sinterleave == 1) ||
				    mirror_mode[node] || lockstep[node]) {
					cinterleave = 1;
				} else {
					cinterleave = channels_interleave(addr);
				}
				if (channel_interleave_p) {
					*channel_interleave_p = cinterleave;
				}
				if (baddr + (rank_sz * rinterleave *
				    cinterleave * sinterleave) >
				    tad[node][i].limit) {
					/*
					 * The system address mapped to this
					 * rank is not contiguous or has
					 * different socket/channel interleave
					 * adjust vitual rank to address where
					 * change or break occures
					 */
					rank_sz = (tad[node][i].limit - baddr) /
					    (cinterleave * sinterleave *
					    rinterleave);
				}
				if (rank_sz_p) {
					*rank_sz_p = rank_sz;
				}
				if (rank_base_p)
					*rank_base_p = baddr;
				if (channel_way_p)
					*channel_way_p = way;
				if (sinterleave == 1 && no_interleave) {
					break;
				}
				switch (tad[node][i].mode) {
				case 0:
					addr += way * 0x40;
					break;
				case 1:
					way = (way ^ (addr >> 16)) & bits;
					addr += way * 0x40;
					break;
				case 2:
					if (sinterleave == 1) {
						xway = ((addr >> 4) & 4) |
						    (((addr >> 6) &
						    0x3ffffffff) % 3);
						if (((way - xway) & 3) == 3)
							xway = (way - xway) & 4;
						else
							xway = way - xway;
						switch (xway) {
						case 0:
							way = 0;
							break;
						case 5:
							way = 1;
							break;
						case 2:
							way = 2;
							break;
						case 4:
							way = 3;
							break;
						case 1:
							way = 4;
							break;
						case 6:
							way = 5;
							break;
						}
					} else {
						xway = (way & 3) -
						    (((addr >> 6) &
						    0x3ffffffff) % 3);
						if (xway < 0)
							xway += 3;
						switch (xway) {
						case 0:
							way = 0;
							break;
						case 1:
							way = 1;
							break;
						case 2:
							way = 2;
							break;
						}
					}
					addr += way * 0x40;
					break;
				}
				break;
			} else if (baddr < tad[node][i].limit) {
				/*
				 * the channel address is not contiguous or
				 * socket/channel interleave changes in the
				 * middle of the rank adjust base and size for
				 * virtual rank to where the break occurs
				 */
				sinterleave = socket_interleave(baddr,
				    node, channel, i, &way);
				if ((no_interleave && sinterleave == 1) ||
				    mirror_mode[node] || lockstep[node]) {
					cinterleave = 1;
				} else {
					cinterleave =
					    channels_interleave(baddr);
				}
				rank_sz -= (tad[node][i].limit - baddr) /
				    (cinterleave * sinterleave * rinterleave);
				cbaddr += (tad[node][i].limit - baddr) /
				    (cinterleave * sinterleave);
			}
		}
		base = tad[node][i].limit;
	}
	return (addr);
}
/*ARGSUSED*/
static cmi_errno_t
nhm_patounum(void *arg, uint64_t pa, uint8_t valid_hi, uint8_t valid_lo,
    uint32_t synd, int syndtype, mc_unum_t *unump)
{
	int node;
	int channel;
	int dimm;
	int rank;
	int log_chan;
	uint64_t bank, row, column;
	uint64_t caddr, raddr;

	node = address_to_node(pa, 0);
	if (node == -1) {
		return (CMIERR_UNKNOWN);
	}
	channel = address_to_channel(node, pa, syndtype, &log_chan, &caddr, 0);
	if (channel == -1) {
		return (CMIERR_UNKNOWN);
	}
	/*
	 * If driver was built with closed tree present then we will have Intel
	 * proprietary functions caddr_to_dimm and rankaddr_to_dimm for finding
	 * dimm/bank/row/column address otherwise we just locate dimm and
	 * offset.
	 */
	if (&caddr_to_dimm)
		dimm = caddr_to_dimm(node, log_chan, caddr, &rank, &raddr);
	else
		dimm = channel_addr_to_dimm(node, log_chan, caddr, &rank,
		    &raddr);
	if (dimm == -1) {
		return (CMIERR_UNKNOWN);

	}
	unump->unum_board = 0;
	unump->unum_chip = node;
	unump->unum_mc = 0;
	unump->unum_chan = channel;
	unump->unum_cs = dimm;
	unump->unum_rank = rank;

	if (&rankaddr_to_dimm) {
		if (rankaddr_to_dimm(raddr, node, channel, dimm, 0, &bank, &row,
		    &column) != DDI_SUCCESS) {
			return (CMIERR_UNKNOWN);
		};
		unump->unum_offset = TCODE_OFFSET(rank, bank, row, column);
	} else {
		unump->unum_offset = raddr;
	}

	return (CMI_SUCCESS);
}

/*ARGSUSED*/
static cmi_errno_t
nhm_unumtopa(void *arg, mc_unum_t *unump, nvlist_t *nvl, uint64_t *pap)
{
	uint64_t pa;
	cmi_errno_t rt;
	int node;
	int channel;
	int log_chan;
	int rank;
	int i;
	nvlist_t **hcl, *hcsp;
	uint_t npr;
	uint64_t offset;
	char *hcnm, *hcid;
	long v;
	uint64_t row, bank, col;
	int dimm;
	uint64_t rank_addr;

	if (unump == NULL) {
		if (nvlist_lookup_nvlist(nvl, FM_FMRI_HC_SPECIFIC,
		    &hcsp) != 0)
			return (CMIERR_UNKNOWN);
		if (nvlist_lookup_uint64(hcsp,
		    "asru-" FM_FMRI_HC_SPECIFIC_OFFSET, &offset) != 0 &&
		    nvlist_lookup_uint64(hcsp, FM_FMRI_HC_SPECIFIC_OFFSET,
		    &offset) != 0) {
			if (nvlist_lookup_uint64(hcsp,
			    "asru-" FM_FMRI_HC_SPECIFIC_PHYSADDR, &pa) == 0 ||
			    nvlist_lookup_uint64(hcsp,
			    FM_FMRI_HC_SPECIFIC_PHYSADDR, &pa) == 0) {
				*pap = pa;
				return (CMI_SUCCESS);
			}
			return (CMIERR_UNKNOWN);
		}
		if (nvlist_lookup_nvlist_array(nvl, FM_FMRI_HC_LIST,
		    &hcl, &npr) != 0)
			return (CMIERR_UNKNOWN);
		node = -1;
		channel = -1;
		dimm = -1;
		rank = -1;
		for (i = 0; i < npr; i++) {
			if (nvlist_lookup_string(hcl[i], FM_FMRI_HC_NAME,
			    &hcnm) != 0 ||
			    nvlist_lookup_string(hcl[i], FM_FMRI_HC_ID,
			    &hcid) != 0 ||
			    ddi_strtol(hcid, NULL, 0, &v) != 0)
				return (CMIERR_UNKNOWN);
			if (strcmp(hcnm, "chip") == 0)
				node = (int)v;
			else if (strcmp(hcnm, "dram-channel") == 0)
				channel = (int)v;
			else if (strcmp(hcnm, "dimm") == 0)
				dimm = (int)v;
			else if (strcmp(hcnm, "rank") == 0)
				rank = (int)v;
		}
		if (node == -1 || channel == -1 || dimm == -1 || rank == -1)
			return (CMIERR_UNKNOWN);
	} else {
		node = unump->unum_chip;
		channel = unump->unum_chan;
		rank = unump->unum_rank;
		dimm = unump->unum_cs;
		offset = unump->unum_offset;
	}

	/*
	 * If driver was built with closed tree present then we will have Intel
	 * proprietary functions dimm_to_rankaddr for finding
	 * physical address.
	 */
	if (&dimm_to_rankaddr && (offset & OFFSET_ROW_BANK_COL) != 0) {
		row = TCODE_OFFSET_RAS(offset);
		bank = TCODE_OFFSET_BANK(offset);
		col = TCODE_OFFSET_CAS(offset);
		rank_addr = dimm_to_rankaddr(node, channel, dimm, row,
		    bank, col, &log_chan);
		pa = rankaddr_to_phyaddr(node, log_chan, dimm, rank,
		    rank_addr);
	} else if ((offset & OFFSET_ROW_BANK_COL) == 0) {
		pa = dimm_to_addr(node, channel, rank, offset, 0, 0, 0, 0, 0,
		    0, 0, 0);
	} else {
		pa = -1LL;
	}

	if (pa == -1) {
		rt = CMIERR_UNKNOWN;
	} else {
		rt = CMI_SUCCESS;
		*pap = pa;
	}
	return (rt);
}

static const cmi_mc_ops_t nhm_mc_ops = {
	nhm_patounum,
	nhm_unumtopa,
	nhm_error_trap	/* cmi_mc_logout */
};

/*ARGSUSED*/
int
inhm_mc_register(cmi_hdl_t hdl, void *arg1, void *arg2, void *arg3)
{
	cmi_mc_register(hdl, &nhm_mc_ops, NULL);
	return (CMI_HDL_WALK_NEXT);
}

static int
choose_cpu(int *lastslot_p)
{
	uint32_t id;
	int first;
	int last;

	first = 0;
	last = MAX_CPU_NODES;
	id = CPU_ID_RD(0);
	if (id == NHM_EP_CPU || id == NHM_WS_CPU || id == NHM_JF_CPU ||
	    id == NHM_WM_CPU) {
		id = CPU_ID_RD(1);
		if (id != NHM_EP_CPU && id != NHM_WS_CPU && id != NHM_JF_CPU &&
		    id != NHM_WM_CPU) {
			last = 1;
		}
	} else {
		first = 1;
	}
	*lastslot_p = last;
	return (first);
}

static int
sad_interleave(uint32_t list)
{
	int rt = 1;
	int i, j;
	int p;

	for (i = 1; i < INTERLEAVE_NWAY; i++) {
		p = SAD_INTERLEAVE(list, i);
		for (j = 0; j < i; j++) {
			if (p == SAD_INTERLEAVE(list, j))
				break;
		}
		if (i == j)
			rt++;
	}
	return (rt);
}

static int
tad_interleave(uint32_t list)
{
	int rt = 1;
	int i, j;
	int c;

	for (i = 1; i < INTERLEAVE_NWAY; i++) {
		c = TAD_INTERLEAVE(list, i);
		for (j = 0; j < i; j++) {
			if (c == TAD_INTERLEAVE(list, j))
				break;
		}
		if (i == j)
			rt++;
	}
	return (rt);
}

static void
set_rank(int socket, int channel, int rule, int way, int rank,
    uint64_t rank_addr)
{
	int k, l;
	if (rank_addr == 0)
		return;
	/*
	 * set limit on any rules which have virtual rank in current rank and
	 * are not already limited by earlier rule
	 */
	for (k = 0; k < rule; k++) {
		for (l = 0; l < MAX_RIR_WAY; l++) {
			if (rir[socket][channel][k].way[l].dimm_rank == rank &&
			    rir[socket][channel][k].way[l].rlimit == 0) {
				rir[socket][channel][k].way[l].rlimit =
				    rank_addr;
			}
		}
	}
	/*
	 * set limit if this rule supplies more than 1 virtual rank from current
	 * rank
	 */
	for (l = 0; l < way; l++) {
		if (rir[socket][channel][k].way[l].dimm_rank == rank &&
		    rir[socket][channel][k].way[l].rlimit == 0) {
			rir[socket][channel][k].way[l].rlimit = rank_addr;
		}
	}
}

void
mem_reg_init()
{
	int i, j, k, l, m;
	uint32_t sad_dram_rule;
	uint32_t tad_dram_rule;
	uint32_t mc_ras_enables;
	uint32_t mc_channel_mapping;
	uint32_t sagch;
	uint32_t rir_limit;
	uint32_t rir_way;
	uint32_t mc_control;
	uint32_t id;
	int nhm_slot;
	int nhm_lastslot;
	uint8_t	rank;
	uint64_t base;
	int ras_dev = 0;
	uint32_t dod_value;

	nhm_slot = choose_cpu(&nhm_lastslot);

	for (i = 0; i < MAX_SAD_DRAM_RULE; i++) {
		sad_dram_rule = SAD_DRAM_RULE_RD(nhm_slot, i);
		sad[i].enable = SAD_DRAM_RULE_ENABLE(sad_dram_rule);
		sad[i].limit = SAD_DRAM_LIMIT(sad_dram_rule);
		sad[i].mode = SAD_DRAM_MODE(sad_dram_rule);
		sad[i].node_list = SAD_INTERLEAVE_LIST_RD(nhm_slot, i);
		sad[i].interleave = sad_interleave(sad[i].node_list);
		for (j = 0; j < INTERLEAVE_NWAY; j++) {
			sad[i].node_tgt[j] = (sad[i].node_list >>
			    (j * 4)) & 0x3;
		}
	}

	for (i = nhm_slot; i < nhm_lastslot; i++) {
		id = MC_CPU_RAS_RD(i);
		if (id == NHM_CPU_RAS || id == NHM_JF_CPU_RAS ||
		    id == NHM_WM_CPU_RAS) {
			ras_dev = 1;
			mc_ras_enables = MC_RAS_ENABLES_RD(i);
			if (RAS_LOCKSTEP_ENABLE(mc_ras_enables))
				lockstep[i] = 1;
			if (RAS_MIRROR_MEM_ENABLE(mc_ras_enables))
				mirror_mode[i] = 1;
		}
		mc_channel_mapping = MC_CHANNEL_MAPPER_RD(i);
		if (CHANNEL_MAP(mc_channel_mapping, 2, 0) == 0 &&
		    CHANNEL_MAP(mc_channel_mapping, 2, 1) == 0)
			spare_channel[i] = 1;
		for (j = 0; j < MAX_TAD_DRAM_RULE; j++) {
			tad_dram_rule = TAD_DRAM_RULE_RD(i, j);
			tad[i][j].enable = TAD_DRAM_RULE_ENABLE(tad_dram_rule);
			tad[i][j].limit = TAD_DRAM_LIMIT(tad_dram_rule);
			tad[i][j].mode = TAD_DRAM_MODE(tad_dram_rule);
			tad[i][j].pkg_list =
			    TAD_INTERLEAVE_LIST_RD(i, j);
			for (k = 0; k < INTERLEAVE_NWAY; k++) {
				tad[i][j].pkg_tgt[k] = ((tad[i][j].pkg_list >>
				    (k * 4)) & 0x3);
			}
			if (mirror_mode[i] || lockstep[i]) {
				tad[i][j].interleave = 1;
			} else {
				tad[i][j].interleave =
				    tad_interleave(tad[i][j].pkg_list);
				if (spare_channel[i] &&
				    tad[i][j].interleave ==
				    CHANNELS_PER_MEMORY_CONTROLLER)
					tad[i][j].interleave--;
			}
		}
		for (j = 0; j < CHANNELS_PER_MEMORY_CONTROLLER; j++) {
			m = 0;
			base = 0;
			for (k = 0; k < MAX_TAD_DRAM_RULE; k++) {
				sagch = MC_SAG_RD(i, j, k);
				sag_ch[i][j][k].offset =
				    CH_ADDRESS_OFFSET(sagch);
				sag_ch[i][j][k].soffset =
				    CH_ADDRESS_SOFFSET(sagch);
				sag_ch[i][j][k].divby3 = DIVBY3(sagch);
				sag_ch[i][j][k].remove6 = REMOVE_6(sagch);
				sag_ch[i][j][k].remove7 = REMOVE_7(sagch);
				sag_ch[i][j][k].remove8 = REMOVE_8(sagch);

				rir_limit = MC_RIR_LIMIT_RD(i, j, k);
				rir[i][j][k].limit = RIR_LIMIT(rir_limit);
				for (l = 0; l < MAX_RIR_WAY; l++) {
					rir_way = MC_RIR_WAY_RD(i, j, m);
					rir[i][j][k].way[l].offset =
					    RIR_OFFSET(rir_way);
					rir[i][j][k].way[l].soffset =
					    RIR_SOFFSET(rir_way);
					rir[i][j][k].way[l].rank =
					    RIR_RANK(rir_way);
					rir[i][j][k].way[l].dimm =
					    RIR_DIMM(rir_way);
					rir[i][j][k].way[l].dimm_rank =
					    RIR_DIMM_RANK(rir_way);
					rir[i][j][k].way[l].rlimit = 0;
					m++;
				}
				rank = rir[i][j][k].way[0].dimm_rank;
				if (rank == rir[i][j][k].way[1].dimm_rank &&
				    rank == rir[i][j][k].way[2].dimm_rank &&
				    rank == rir[i][j][k].way[3].dimm_rank) {
					rir[i][j][k].interleave = 1;
				} else if
				    (rank == rir[i][j][k].way[1].dimm_rank ||
				    rank == rir[i][j][k].way[2].dimm_rank ||
				    rank == rir[i][j][k].way[3].dimm_rank) {
					rir[i][j][k].interleave = 2;
				} else {
					rir[i][j][k].interleave = 4;
				}
				for (l = 0; l < MAX_RIR_WAY; l++) {
					set_rank(i, j, k, l,
					    rir[i][j][k].way[l].dimm_rank,
					    ((rir[i][j][k].way[l].soffset +
					    base) /
					    rir[i][j][k].interleave));
				}
				base = rir[i][j][k].limit;
			}
			for (k = 0; k < MAX_DIMMS_PER_CHANNEL; k++) {
				dod_value = MC_DOD_RD(i, j, k);
				dod_reg[i][j][k].NUMCol = NUMCOL(dod_value);
				dod_reg[i][j][k].NUMRow = NUMROW(dod_value);
				dod_reg[i][j][k].NUMBank = NUMBANK(dod_value);
				dod_reg[i][j][k].NUMRank = NUMRANK(dod_value);
				dod_reg[i][j][k].DIMMPresent =
				    DIMMPRESENT(dod_value);
				dod_reg[i][j][k].RankOffset =
				    RANKOFFSET(dod_value);
			}
		}
	}
	mc_control = MC_CONTROL_RD(nhm_slot);
	closed_page = MC_CONTROL_CLOSED_PAGE(mc_control);
	if (ras_dev)
		ecc_enabled = MC_CONTROL_ECCEN(mc_control);
	else if ((MC_STATUS_RD(nhm_slot) & WS_ECC_ENABLED) != 0)
		ecc_enabled = 1;
	divby3_enabled = MC_CONTROL_DIVBY3(mc_control);
}
