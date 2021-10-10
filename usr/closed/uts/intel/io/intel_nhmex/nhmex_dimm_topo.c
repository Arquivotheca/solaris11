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
 * Copyright (c) 2009, 2010, Oracle and/or its affiliates. All rights reserved.
 */

#include <sys/types.h>
#include <sys/time.h>
#include <sys/nvpair.h>
#include <sys/cmn_err.h>
#include <sys/cred.h>
#include <sys/open.h>
#include <sys/ddi.h>
#include <sys/sunddi.h>
#include <sys/conf.h>
#include <sys/modctl.h>
#include <sys/cyclic.h>
#include <sys/errorq.h>
#include <sys/stat.h>
#include <sys/cpuvar.h>
#include <sys/mc_intel.h>
#include <sys/mc.h>
#include <sys/fm/protocol.h>
#include "nhmex.h"
#include "nhmex_log.h"

extern nvlist_t **inhmex_mc_nvl;
extern char ecc_enabled;

static void
inhmex_vrank(nvlist_t *vrank, int num, uint64_t dimm_base, uint64_t dimm_end,
    int cinterleave, int rinterleave, int sway, int rway)
{
	char buf[128];

	(void) snprintf(buf, sizeof (buf), "dimm-rank-base-%d", num);
	(void) nvlist_add_uint64(vrank, buf, dimm_base);
	(void) snprintf(buf, sizeof (buf), "dimm-rank-limit-%d", num);
	(void) nvlist_add_uint64(vrank, buf, dimm_end);
	if (cinterleave > 1) {
		(void) snprintf(buf, sizeof (buf),
		    "dimm-controller-interleave-%d",
		    num);
		(void) nvlist_add_uint32(vrank, buf, (uint32_t)cinterleave);
		(void) snprintf(buf, sizeof (buf), "dimm-controller-way-%d",
		    num);
		(void) nvlist_add_uint32(vrank, buf, (uint32_t)sway);
	}
	if (rinterleave > 1) {
		(void) snprintf(buf, sizeof (buf), "dimm-rank-interleave-%d",
		    num);
		(void) nvlist_add_uint32(vrank, buf, (uint32_t)rinterleave);
		(void) snprintf(buf, sizeof (buf), "dimm-rank-way-%d",
		    num);
		(void) nvlist_add_uint32(vrank, buf, (uint32_t)rway);
	}
}

static uint64_t
rank_limit(uint32_t node, int mc, uint64_t pa, uint64_t rank_size,
    uint32_t rinterleave, uint32_t *interleave_p, uint64_t *next)
{
	uint64_t limit;
	uint64_t rt;
	uint64_t r_start, start;
	uint64_t tad_limit;
	uint32_t tad_payload;
	int i;
	int interleave;

	for (i = 0; i < SAD_NDECODE_DRAM; i++) {
		limit = PCSR_SADCAMARY(node, i);
		if (pa < limit)
			break;
	}
	if (i == SAD_NDECODE_DRAM) {
		*next = 0;
		return (0);
	}
	r_start = 0;
	start = 0;
	for (i = 0; i < MAX_TAD; i++) {
		B_PCSR_TAD_CTL_REG_WR(node, mc, TAD_LIMIT_READ(i));
		tad_limit = TAD_LIMIT(B_PCSR_TAD_RDDATA_REG_RD(node, mc));
		B_PCSR_TAD_CTL_REG_WR(node, mc, TAD_PAYLOAD_READ(i));
		tad_payload = B_PCSR_TAD_RDDATA_REG_RD(node, mc);
		start = r_start - TAD_OFFSET(tad_payload);
		r_start = tad_limit - start;
		if (pa < tad_limit)
			break;
	}
	interleave = TAD_INTERLEAVE(tad_payload);
	if (interleave_p)
		*interleave_p = interleave;
	if (tad_limit < limit)
		limit = tad_limit;
	for (i++; i < MAX_TAD; i++) {
		B_PCSR_TAD_CTL_REG_WR(node, mc, TAD_LIMIT_READ(i));
		tad_limit = TAD_LIMIT(B_PCSR_TAD_RDDATA_REG_RD(node, mc));
		B_PCSR_TAD_CTL_REG_WR(node, mc, TAD_PAYLOAD_READ(i));
		tad_payload = B_PCSR_TAD_RDDATA_REG_RD(node, mc);
		start = r_start - TAD_OFFSET(tad_payload);
		if (TAD_NXM(tad_payload) == 0)
			break;
		r_start = tad_limit - start;
	}
	rt = pa + (rank_size * interleave * rinterleave * LOCKSTEP_RANKS);
	if (rt > limit) {
		rt = limit;
		i++;
		B_PCSR_TAD_CTL_REG_WR(node, mc, TAD_PAYLOAD_READ(i));
		*next =  start;
	} else {
		*next = rt;
	}
	return (rt);
}

static void
bbox_map(uint32_t node, int mc, uint32_t dimm, int rank, uint64_t address,
    uint32_t *rinterleave_p, int *rway_p)
{
	uint64_t limit;
	uint32_t map_pd;
	int i;
	int cs_id;
	uint32_t csctl;
	int cs;
	int ds;
	int rs;
	int fbdimm;
	int map;
	uint32_t map1;
	int interleave;
	int way;
	int open_map;

	if (MB_CSCTL_RD(node, mc, dimm & 1, (dimm >> 1) & 1, &csctl) == -1) {
		*rinterleave_p = 0;
		*rway_p = 0;
		return;
	}
	cs =  ((dimm & 1) * 4) + rank;
	if (CSCTL_CS_ENABLE(csctl, cs) == 0) {
		*rinterleave_p = 0;
		*rway_p = 0;
		return;
	}
	cs_id = CSCTL_CS_ID(csctl, cs);
	rs = cs_id & 1;
	ds = (cs_id >> 1) & 3;
	map_pd = M_PCSR_MAP_PHYS_DIMM(node, mc, 0);
	for (i = 0; i < 4; i++) {
		if (PHYDIMM(map_pd, i) == ds)
			break;
	}
	fbdimm = i;
	if (i == 4) {
		map_pd = M_PCSR_MAP_PHYS_DIMM(node, mc, 1);
		for (i = 0; i < 4; i++) {
			if (PHYDIMM(map_pd, i) == ds)
				break;
		}
		fbdimm += i;
	}
	if (fbdimm == 8) {
		*rinterleave_p = 0;
		*rway_p = 0;
		return;
	}
	for (i = 0; i < MAX_MAP_LIMIT; i++) {
		B_PCSR_TAD_CTL_REG_WR(node, mc, MAP_LIMIT_READ(i));
		limit = MAP_LIMIT(B_PCSR_TAD_RDDATA_REG_RD(node, mc));
		if (address < limit)
			break;
	}
	map = i;
	map1 = M_PCSR_MAP_1(node, mc, map);
	interleave = 1;
	way = 0;
	open_map = OPEN_MAP(M_PCSR_MAP_OPEN_CLOSED(node, mc));
	if (SRANK_BIT0_USE(map1) == BIT_USED) {
		way |= rs << (STACKED_RANK(map1) + DIMM_SPC(map1) ? 23 :
		    open_map ? 7 : 0);
		interleave <<= 1;
	} else {
		way |= SRANK_BIT0_USE(map1) << (STACKED_RANK(map1) +
		    DIMM_SPC(map1) ? 23 : open_map ? 7 : 0);
		interleave += SRANK_BIT0_USE(map1);
	}
	if (SRANK_BIT1_USE(map1) == BIT_USED) {
		way |= rs << (STACKED_RANK(map1) + DIMM_SPC(map1) ? 23 :
		    open_map ? 8 : 1);
		interleave <<= 1;
	} else {
		way |= SRANK_BIT1_USE(map1) << (STACKED_RANK(map1) +
		    DIMM_SPC(map1) ? 23 : open_map ? 8 : 1);
		interleave <<= SRANK_BIT1_USE(map1);
	}
	if (DIMM_BIT0_USE(map1)) {
		way |= (ds & 1) << (DIMM(map1) + DIMM_SPC(map1) ? 23 :
		    RANK_2X(map1) ? 0 : 7);
		interleave <<= 1;
	}
	if (DIMM_BIT1_USE(map1)) {
		way |= (ds & 2) << (DIMM(map1) + DIMM_SPC(map1) ? 24 :
		    open_map ? 8 : 1);
		interleave <<= 1;
	}
	if (DIMM_BIT2_USE(map1)) {
		way |= (ds & 4) << (DIMM(map1) + DIMM_SPC(map1) ? 25 :
		    open_map ? 9 : 2);
		interleave <<= 1;
	}
	*rinterleave_p = interleave;
	*rway_p = way;
}

static void
inhmex_rank(nvlist_t *newdimm, nhmex_dimm_t *nhmex_dimm, uint32_t node,
    uint8_t channel, uint32_t dimm, uint64_t rank_size)
{
	nvlist_t **newrank;
	int num;
	int way;
	int rway;
	int i;
	int mc;
	uint64_t pa, ea, next;
	uint64_t vsize;
	uint32_t cinterleave;
	uint32_t rinterleave;
	mc_unum_t unum;

	newrank = kmem_zalloc(sizeof (nvlist_t *) * nhmex_dimm->nranks,
	    KM_SLEEP);
	for (i = 0; i < nhmex_dimm->nranks; i++) {
		(void) nvlist_alloc(&newrank[i], NV_UNIQUE_NAME, KM_SLEEP);
		mc = channel / CHANNELS_PER_MEMORY_CONTROLLER;
		unum.unum_board = 0;
		unum.unum_chip = node;
		unum.unum_mc = mc;
		unum.unum_cs = dimm;
		unum.unum_rank = (dimm * MAX_RANKS_PER_DIMM) + i;
		unum.unum_offset = 0;
		(void) nvlist_add_uint32(newrank[i], "ddr3-dimm-rank",
		    ((dimm & 1) * 4) + (i & 3));
		if (nhmex_unumtopa_i(0, &unum, 0, &pa) == CMI_SUCCESS) {
			num = 0;
			way = INTERLEAVE_WAY(pa);
			pa &= REGION_MASK;
			vsize = rank_size;
			do {
				bbox_map(node, mc, dimm, i, pa, &rinterleave,
				    &rway);
				if (rinterleave == 0)
					break;
				ea = rank_limit(node, mc, pa, vsize,
				    rinterleave, &cinterleave, &next);
				if (next == 0)
					break;
				inhmex_vrank(newrank[i], num, pa, ea,
				    cinterleave, rinterleave, way, rway);
				num++;
				vsize -= (ea - pa) /
				    (cinterleave * rinterleave *
				    LOCKSTEP_RANKS);
				pa = next;
			} while (next > ea);
		}
	}
	(void) nvlist_add_nvlist_array(newdimm, MCINTEL_NVLIST_RANKS, newrank,
	    nhmex_dimm->nranks);
	for (i = 0; i < nhmex_dimm->nranks; i++)
		nvlist_free(newrank[i]);
	kmem_free(newrank, sizeof (nvlist_t *) * nhmex_dimm->nranks);
}

static nvlist_t *
inhmex_dimm(nhmex_dimm_t *nhmex_dimm, uint32_t node, uint8_t channel,
    uint32_t dimm)
{
	nvlist_t *newdimm;
	uint8_t t;
	char sbuf[65];

	(void) nvlist_alloc(&newdimm, NV_UNIQUE_NAME, KM_SLEEP);
	(void) nvlist_add_uint32(newdimm, MCINTEL_NVLIST_DIMM_NUM, dimm);
	(void) nvlist_add_uint32(newdimm, "ddr3-channel", dimm/2);
	(void) nvlist_add_uint32(newdimm, "ddr3-dimm-number", dimm & 1);

	if (nhmex_dimm->dimm_size >= 1024*1024*1024) {
		(void) snprintf(sbuf, sizeof (sbuf), "%dG",
		    (int)(nhmex_dimm->dimm_size / (1024*1024*1024)));
	} else {
		(void) snprintf(sbuf, sizeof (sbuf), "%dM",
		    (int)(nhmex_dimm->dimm_size / (1024*1024)));
	}
	(void) nvlist_add_string(newdimm, "dimm-size", sbuf);
	(void) nvlist_add_uint64(newdimm, "size", nhmex_dimm->dimm_size);
	(void) nvlist_add_uint32(newdimm, "nbanks",
	    (uint32_t)nhmex_dimm->nbanks);
	(void) nvlist_add_uint32(newdimm, "ncolumn",
	    (uint32_t)nhmex_dimm->ncolumn);
	(void) nvlist_add_uint32(newdimm, "nrow", (uint32_t)nhmex_dimm->nrow);
	(void) nvlist_add_uint32(newdimm, "width", (uint32_t)nhmex_dimm->width);
	(void) nvlist_add_int32(newdimm, MCINTEL_NVLIST_1ST_RANK,
	    dimm * MAX_RANKS_PER_DIMM);
	(void) nvlist_add_uint32(newdimm, "ranks",
	    (uint32_t)nhmex_dimm->nranks);
	inhmex_rank(newdimm, nhmex_dimm, node, channel, dimm,
	    nhmex_dimm->dimm_size / nhmex_dimm->nranks);
	if (nhmex_dimm->manufacturer && nhmex_dimm->manufacturer[0]) {
		t = sizeof (nhmex_dimm->manufacturer);
		(void) strncpy(sbuf, nhmex_dimm->manufacturer, t);
		sbuf[t] = 0;
		(void) nvlist_add_string(newdimm, "manufacturer", sbuf);
	}
	if (nhmex_dimm->serial_number && nhmex_dimm->serial_number[0]) {
		t = sizeof (nhmex_dimm->serial_number);
		(void) strncpy(sbuf, nhmex_dimm->serial_number, t);
		sbuf[t] = 0;
		(void) nvlist_add_string(newdimm, FM_FMRI_HC_SERIAL_ID, sbuf);
	}
	if (nhmex_dimm->part_number && nhmex_dimm->part_number[0]) {
		t = sizeof (nhmex_dimm->part_number);
		(void) strncpy(sbuf, nhmex_dimm->part_number, t);
		sbuf[t] = 0;
		(void) nvlist_add_string(newdimm, FM_FMRI_HC_PART, sbuf);
	}
	if (nhmex_dimm->revision && nhmex_dimm->revision[0]) {
		t = sizeof (nhmex_dimm->revision);
		(void) strncpy(sbuf, nhmex_dimm->revision, t);
		sbuf[t] = 0;
		(void) nvlist_add_string(newdimm, FM_FMRI_HC_REVISION, sbuf);
	}
	t = sizeof (nhmex_dimm->label);
	(void) strncpy(sbuf, nhmex_dimm->label, t);
	sbuf[t] = 0;
	(void) nvlist_add_string(newdimm, FM_FAULT_FRU_LABEL, sbuf);
	return (newdimm);
}

static void
inhmex_dimmlist(uint32_t node, nvlist_t *nvl)
{
	nvlist_t **dimmlist;
	nvlist_t **newchannel;
	int nchannels = MAX_CPU_MEMORY_CONTROLLERS *
	    CHANNELS_PER_MEMORY_CONTROLLER;
	int nd;
	uint8_t i, j;
	nhmex_dimm_t *dimmp;
	uint32_t mp[MAX_CPU_MEMORY_CONTROLLERS];
	uint32_t mirror[MAX_CPU_MEMORY_CONTROLLERS];
	int one_mode = 1;
	int policy;

	dimmlist =  kmem_zalloc(sizeof (nvlist_t *) * MAX_DIMMS_PER_CHANNEL,
	    KM_SLEEP);
	newchannel = kmem_zalloc(sizeof (nvlist_t *) * nchannels, KM_SLEEP);
	mp[0] = PAGE_POLICY_RD(node, 0);
	policy = PAGE_MODE(mp[0]);
	mirror[0] =  B_PCSR_MEM_MIRROR_REG_RD(node, 0);
	for (i = 1; i < MAX_CPU_MEMORY_CONTROLLERS; i++) {
		mp[i] = PAGE_POLICY_RD(node, i);
		if (PAGE_MODE(mp[i]) != policy) {
			one_mode = 0;
			break;
		}
		mirror[i] =  B_PCSR_MEM_MIRROR_REG_RD(node, i);
	}
	if (one_mode)
		(void) nvlist_add_string(nvl, "memory-policy",
		    CLOSED_PAGE(mp[0]) ? "closed-page" :
		    OPEN_PAGE(mp[0]) ? "open-page" :
		    ADAPTIVE_PAGE(mp[0]) ? "adaptive-page" : "unknown");

	for (i = 0; i < nchannels; i++) {
		(void) nvlist_alloc(&newchannel[i], NV_UNIQUE_NAME, KM_SLEEP);
		(void) nvlist_add_string(newchannel[i], "channel-mode",
		    MIRRORING(mirror[i >> 1]) ? "lockstep-mirror" :
		    MIGRATION(mirror[i >> 1]) ? "lockstep-migration" :
		    "lockstep");
		nd = 0;
		for (j = 0; j < MAX_DIMMS_PER_CHANNEL; j++) {
			dimmp = nhmex_dimms[DIMM_NUM(node, i >> 1, i & 1, j)];
			if (dimmp != NULL) {
				dimmlist[nd] = inhmex_dimm(dimmp, node, i,
				    (uint32_t)j);
				nd++;
			}
		}
		if (nd) {
			(void) nvlist_add_nvlist_array(newchannel[i],
			    "memory-dimms", dimmlist, nd);
			for (j = 0; j < nd; j++)
				nvlist_free(dimmlist[j]);
		}
	}
	(void) nvlist_add_nvlist_array(nvl, MCINTEL_NVLIST_MC, newchannel,
	    nchannels);
	for (i = 0; i < nchannels; i++)
		nvlist_free(newchannel[i]);
	kmem_free(dimmlist, sizeof (nvlist_t *) * MAX_DIMMS_PER_CHANNEL);
	kmem_free(newchannel, sizeof (nvlist_t *) * nchannels);
}

char *
inhmex_mc_name()
{
	return (NHMEX_INTERCONNECT);
}

void
inhmex_create_nvl(int chip)
{
	nvlist_t *nvl;

	(void) nvlist_alloc(&nvl, NV_UNIQUE_NAME, KM_SLEEP);
	(void) nvlist_add_uint8(nvl, MCINTEL_NVLIST_VERSTR,
	    MCINTEL_NVLIST_VERS);
	(void) nvlist_add_string(nvl, MCINTEL_NVLIST_MEM, inhmex_mc_name());
	(void) nvlist_add_uint8(nvl, MCINTEL_NVLIST_NMEM,
	    CHANNELS_PER_MEMORY_CONTROLLER);
	(void) nvlist_add_uint8(nvl, MCINTEL_NVLIST_NRANKS,
	    MAX_RANKS_PER_CHANNEL);
	(void) nvlist_add_uint8(nvl, MCINTEL_NVLIST_NDIMMS,
	    MAX_DIMMS_PER_CHANNEL);
	inhmex_dimmlist(chip, nvl);

	if (inhmex_mc_nvl[chip])
		nvlist_free(inhmex_mc_nvl[chip]);
	inhmex_mc_nvl[chip] = nvl;
}
