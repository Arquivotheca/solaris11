
/*
 * INTEL CONFIDENTIAL
 * SPECIAL INTEL MODIFICATIONS
 *
 * Copyright (c) 2009,  Intel Corporation.
 * All Rights Reserved.
 */

#include <sys/types.h>
#include <sys/cmn_err.h>
#include <sys/ddi.h>
#include <sys/sunddi.h>
#include <sys/cpu_module_impl.h>
#include <sys/fm/protocol.h>
#include "nhmex_mem_addr.h"

int nhmex_debug = 0;

#define	INHMEX_DEBUG(args...)	if (nhmex_debug)	cmn_err(args);

/* system level clump topo structure */
nhmex_clump_topo_t **nhmex_gclp;
extern krwlock_t inhmex_mc_lock;

extern int
nhmex_mb_rd(int id, int mc, int mb, int dimm, int func, int offset,
	uint32_t *datap);

static
uint32_t mem_reg_collect(nhmex_clump_topo_t *clump_topo);

/*ARGSUSED*/
uint16_t
fbd_dimm_to_ddr(nhmex_mem_info_t *mi, uint64_t addr, uint16_t socket,
    uint16_t branch, uint16_t fbd_ds, uint16_t fbd_rs,
    uint16_t *ddrch, uint16_t *fbdch, uint16_t *ddr_rank)
{
	int i, cs_hit = 0;
	uint16_t cval, ddr_dimm;

	fbd_ds = fbd_ds | ((fbd_rs & 2) << 1);

	/*
	 * a channel in this case is an fbd link, so there are 4 channels / bek,
	 * 2 channels / memory box.
	 * Channels 0 and 1 are a lockstep pair as are channels 2 and 3,
	 * so the sb commands are going to be the same within the lockstep pair
	 */

	if (branch == 0)
		*fbdch = 0;
	else
		*fbdch = 2;

	cval = (fbd_ds & 0x06) | (fbd_rs & 0x01);

	if (fbd_ds & 0x1)
		*ddrch = 1;
	else
		*ddrch = 0;
	/*
	 * find out the chip select matches fbd ds[2:1] and rs[0]
	 */
	for (i = 0; i < NHMEX_MAX_DDR_CS; i++) {
		INHMEX_DEBUG(CE_NOTE, "-14-csctl[%d][%d][%d][%d]\n",
		    socket, *fbdch, *ddrch, i);
		if (mi->csctl[socket][*fbdch][*ddrch][i].enable == 1) {
			if (mi->csctl[socket][*fbdch][*ddrch][i].mask == 2) {
				/*
				 * DS[2] and CS_IDi[2] not compared
				 */
				cval = (fbd_ds & 0x02) | (fbd_rs & 0x01);
			}
			if (mi->csctl[socket][*fbdch][*ddrch][i].id == cval) {
				cs_hit = i;
				INHMEX_DEBUG(CE_NOTE,
				    "socket = %d, fbdch = %d, cs = %d\n",
				    socket, *fbdch, i);
				break;
			}
		}
	}

	if (i >= NHMEX_MAX_DDR_CS) {
		INHMEX_DEBUG(CE_NOTE, "fbd_dimm_to_ddr() - \
		    No match chip select!\n");
		return (0xffff);
	}

	*ddr_rank = cs_hit % 4;
	if (cs_hit < 4)
		ddr_dimm = 0;
	else
		ddr_dimm = 1;

	return (ddr_dimm);
}

uint16_t
ddr_dimm_to_fbd(nhmex_mem_info_t *mi, uint16_t socket,
    uint16_t branch, uint16_t ddrch, uint16_t dimm,
    uint16_t rank, uint16_t *fbd_rank)
{
	uint16_t fbd_dimm, fbdch, chip_sel, tmp;

	if (dimm == 0)
		chip_sel = rank;
	else
		chip_sel = rank + 4;

	if (branch == 0)
		fbdch = 0;
	else
		fbdch = 2;
	/*
	 * here we don't cover the situation of mask =2,
	 * we are not able to translate QR dimms
	 */
	if (mi->csctl[socket][fbdch][ddrch][chip_sel].enable == 0) {
		INHMEX_DEBUG(CE_NOTE, "this chip_sel is not enabled!\n");
		return (0xffff);
	}
	INHMEX_DEBUG(CE_NOTE,  "socket = %d, fbdch = %d, cs = %d\n",
	    socket, fbdch, chip_sel);
	*fbd_rank = mi->csctl[socket][fbdch][ddrch][chip_sel].id & 0x1;
	fbd_dimm = mi->csctl[socket][fbdch][ddrch][chip_sel].id & 0x6;
	fbd_dimm = fbd_dimm | ddrch;
	INHMEX_DEBUG(CE_NOTE, "fbd_popctl = %x\n",
	    mi->fbd_popctl[socket][branch]);
	tmp = ((mi->fbd_popctl[socket][branch] & 0xfff) >> fbd_dimm) & 1;
	*fbd_rank = *fbd_rank | (((~tmp) & (fbd_dimm >> 2)) << 1);
	fbd_dimm = (fbd_dimm & 0x3) | ((tmp & (fbd_dimm >> 2)) << 2);

	return (fbd_dimm);
}

/*
 * Explore the system topology
 *
 * the parameter defined as
 * nhmex_clump_topo_t *clump_topo[NHMEX_MAX_CLUMP_NUMBER]
 * in the calling function.
 */
uint32_t
nhmex_memtrans_init(void)
{
	int i, j, l, bus, cpu_exist = 0;
	uint32_t val;
	uint8_t tmp, bit_pos;

	ASSERT(RW_LOCK_HELD(&inhmex_mc_lock));
	nhmex_gclp = kmem_zalloc(
	    NHMEX_MAX_CLUMP_NUMBER * NHMEX_MAX_CPU_NUMBER *
	    sizeof (nhmex_clump_topo_t *), KM_SLEEP);
	/*
	 * probe the system topo, we need to know how many clumps
	 * exist, thus can get CPU SCA bus numbers
	 */
	for (i = 0; i < NHMEX_MAX_CLUMP_NUMBER; i++) {
		for (j = 0; j < NHMEX_MAX_CPU_NUMBER; j++) {
			/*
			 * MAX 8 CPUs per clump.
			 * let's start from bus 255 (clump#=11111'b,
			 * bus[2:0]=111'b)
			 */
			bus = (((NHMEX_MAX_CLUMP_NUMBER - 1) - i) << 3) |
			    ((NHMEX_MAX_CPU_NUMBER - 1) - j);

			if (NHMEX_DID_VID(bus) != 0xffffffff) {
				cpu_exist = 1;
				break;
			}
		}
		if (cpu_exist == 0)
			/*
			 * no cpu in this clump, search next
			 */
			continue;

		nhmex_gclp[i] =
		    kmem_zalloc(sizeof (nhmex_clump_topo_t), KM_SLEEP);
		if (nhmex_gclp[i] == NULL) {
			INHMEX_DEBUG(CE_NOTE,
			    "NHM_EX_ERROR:nhmex_memtrans_init() \
			    memory alloc failed!\n");
			return (0xffffffff);
		}
		/*
		 * collect clump information
		 */
		val = NHMEX_IOMMEN(bus);
		nhmex_gclp[i]->clump_id = (val >> 12) & 0x1f;
		nhmex_gclp[i]->sca_mask = (val >> 17) & 0x7;
		nhmex_gclp[i]->sca_ena = (val >> 4) & 0xff;

		/*
		 * sca_mask = 8 - MAX CPU#/clump
		 */
		nhmex_gclp[i]->socket_number = 8 - nhmex_gclp[i]->sca_mask;

		/*
		 * calculate bus number[2:0] according
		 * to sca_ena bit position
		 */
		tmp = nhmex_gclp[i]->sca_ena;
		bit_pos = 0x80;
		/*
		 * record the bus number for each socket
		 */
		for (l = 0; l < 8; l++) { /* socket id */
			if (tmp & (bit_pos >> l)) {
				if (l >= NHMEX_MAX_CPU_NUMBER) {
					INHMEX_DEBUG(CE_NOTE, "l leak!");
					return (0xffffffff);
				}
				nhmex_gclp[i]->bus_number[l] =
				    (nhmex_gclp[i]->clump_id << 3) |
				    ((~l) & 0x7);
				tmp &= ~(bit_pos >> l);
			}
		}

		if (mem_reg_collect(nhmex_gclp[i]) != 0)
			return (0xffffffff);

	}
	if (cpu_exist == 0) {
		INHMEX_DEBUG(CE_NOTE, "NHM_EX_ERROR, cpu_exist == 0\n");
		return (0xffffffff);
	}
	return (0);
}

/*
 * collect memory controller reg information
 */
static uint32_t
mem_reg_collect(nhmex_clump_topo_t *clump_topo)
{
	int h, i, j, k, l;
	uint32_t sad_tgtlist, prmary, val, val1, val2, map1, map0;
	uint16_t socket, offset;
	nhmex_mem_info_t *pm;

	/*
	 * Collect SAD information
	 */
	for (i = 0; i < clump_topo->socket_number; i++) {
		if ((clump_topo->bus_number[i] != 0) &&
		    NHMEX_DID_VID(clump_topo->bus_number[i]) != 0xffffffff)
			/*
			 * we have CPU exist in this clump
			 */
			break;
	}

	if (i == clump_topo->socket_number) {
		/*
		 * cannot find CPU
		 */
		INHMEX_DEBUG(CE_NOTE, "NHM_EX_ERROR:mem_reg_collect() \
		    cannot find CPU!\n");
		return (0xffffffff);
	}
	/*
	 * alloc memory space for mem infor in this clump
	 */
	clump_topo->mem_info =
	    kmem_zalloc(sizeof (nhmex_mem_info_t), KM_SLEEP);
	if (clump_topo->mem_info == NULL) {
		INHMEX_DEBUG(CE_NOTE, "NHM_EX_ERROR:mem_reg_collect() \
		    memory alloc failed!\n");
		return (0xffffffff);
	}
	pm = clump_topo->mem_info;

	for (i = 0; i < NHMEX_MAX_SAD_ENTRY; i++) {
		pm->sad_array[i].limit =
		    ((((uint64_t)NHMEX_SAD_DRAM_LIMIT_RD(clump_topo, 0, i)) &
		    0xFFFF) << 28) |0xfffffff;
		NHMEX_SAD_DRAM_TGTLST_PICK(clump_topo, 0, i,
		    NHMEX_DECODER_DRAM);
		sad_tgtlist = NHMEX_SAD_DRAM_TGTLST_RD(clump_topo, 0);
		for (j = 0; j < NHMEX_MAX_TARGET_LIST; j++) {
			/* contains NodeID[4:1] */
			pm->sad_array[i].tgt[j] =
			    (sad_tgtlist >> (j * 4)) & 0xf;
		}
		prmary = NHMEX_SAD_DRAM_PRMARY_RD(clump_topo, 0);
		pm->sad_array[i].attr =
		    (uint_t)(prmary & 0x7);
		pm->sad_array[i].tgtsel =
		    (uint_t)((prmary >> 3) & 0x1);
		pm->sad_array[i].hemi =
		    (uint_t)((prmary >> 4) & 0x1);
		pm->sad_array[i].idbase =
		    (uint_t)((prmary >> 5) & 0x1);
		pm->sad_array[i].eid =
		    (uint_t)((prmary >> 10) & 0x1f);
		pm->sad_array[i].did =
		    (uint_t)((prmary >> 15) & 0x1);
	}

	socket = clump_topo->socket_number;
	for (k = 0; k < socket; k++) {
		if ((clump_topo->bus_number[k] == 0) ||
		    NHMEX_DID_VID(clump_topo->bus_number[k]) == 0xffffffff)
			continue;
		for (j = 0; j < NHMEX_MAX_BRANCH_PER_SOCKET; j++) {
			/*
			 * get nodeid for each Bbox
			 */
			pm->nodeid[k][j] = (uint8_t)
			    NHMEX_PCSR_MODE_RD(clump_topo, k, j) & 0x1f;

			/*
			 * get fbd_popctl for each memory controller
			 */
			pm->fbd_popctl[k][j] =
			    NHMEX_PCSR_POPCTL_RD(clump_topo, k, j);

			/*
			 * Collect TAD information
			 * There are 3 structures(tables)
			 *  - TAD region limits
			 *  - TAD regions payload table
			 *  - MAP region limits
			 */
			for (i = 0; i < NHMEX_MAX_TAD_ENTRY; i++) {

				/*
				 * get offset, interleave
				 * ways from payload table
				 */
				NHMEX_TAD_CTL_WR(clump_topo, k, j,
				    NHMEX_TAD_RD | (NHMEX_TAD_TBL_TAD_PAYLOAD
				    << 2) | (i << 4));
				val = NHMEX_TAD_RDDATA_RD(clump_topo, k, j);
				offset = (val & 0xfff);
				if (offset  != 0) {
					pm->tad_array[k][j][i].offset =
					    (~offset & 0xfff) + 1;
				} else {
					pm->tad_array[k][j][i].offset = 0;
				}
				/*
				 * shft is programed as LOG2(interleave
				 * ways for this region as spec'd by SAD)
				 */
				pm->tad_array[k][j][i].ways =
				    (uint16_t)(1 << ((val >> 16) & 0x3));

				/*
				 * get tad limit from tad region limits table
				 */
				NHMEX_TAD_CTL_WR(clump_topo, k, j,
				    NHMEX_TAD_RD | (NHMEX_TAD_TBL_TAD_LIMIT
				    << 2) | (i << 4));
				val =  NHMEX_TAD_RDDATA_RD(clump_topo, k, j);
				pm->tad_array[k][j][i].tad_limit
				    = ((((uint64_t)val) & 0xffff) << 28) |
				    0xfffffff;
			}

			for (i = 0; i < NHMEX_MAX_MAP_ENTRY; i++) {
				/*
				 * get map limit from map region limits table
				 */
				NHMEX_TAD_CTL_WR(clump_topo, k, j,
				    NHMEX_TAD_RD | (NHMEX_TAD_TBL_MAP_LIMIT
				    << 2) | (i << 4));
				val =  NHMEX_TAD_RDDATA_RD(clump_topo, k, j);
				pm->map_limit[k][j][i] =
				    ((((uint64_t)val) & 0x3fff) << 30) |
				    0x3fffffff;
			}

			/*
			 * collect map info
			 */
			for (i = 0; i < NHMEX_MAX_MAP_NUMBER; i++) {
				map0 = NHMEX_MAP_0_RD(clump_topo, k, j, i);
				pm->m_csr_map_0[k][j][i].c13_use =
				    (uint_t)((map0 >> 20) &0x3);
				pm->m_csr_map_0[k][j][i].c12_use =
				    (uint_t)((map0 >> 18) & 0x3);
				pm->m_csr_map_0[k][j][i].c11_use =
				    (uint_t)((map0 >> 16) & 0x3);
				pm->m_csr_map_0[k][j][i].c10_use =
				    (uint_t)((map0 >> 14) & 0x3);
				pm->m_csr_map_0[k][j][i].c2_use =
				    (uint_t)((map0 >> 12) & 0x3);
				pm->m_csr_map_0[k][j][i].col =
				    (uint_t)((map0 >> 8) & 0xf);
				pm->m_csr_map_0[k][j][i].r15_use =
				    (uint_t)((map0 >> 6) & 0x3);
				pm->m_csr_map_0[k][j][i].r14_use =
				    (uint_t)((map0 >> 4) & 0x3);
				pm->m_csr_map_0[k][j][i].row =
				    (uint_t)(map0 & 0xf);
				map1 = NHMEX_MAP_1_RD(clump_topo, k, j, i);
				pm->m_csr_map_1[k][j][i].bank3_use =
				    (uint_t)((map1 >> 20) & 0x3);
				pm->m_csr_map_1[k][j][i].bank2_use =
				    (uint_t)((map1 >> 18) & 0x3);
				pm->m_csr_map_1[k][j][i].bank =
				    (uint_t)((map1 >> 14) & 0xf);
				pm->m_csr_map_1[k][j][i].srank1_use =
				    (uint_t)((map1 >> 22) & 0x3);
				pm->m_csr_map_1[k][j][i].srank0_use =
				    (uint_t)((map1 >> 12) & 0x3);
				pm->m_csr_map_1[k][j][i].stacked_rank =
				    (uint_t)((map1 >> 8) & 0xf);
				pm->m_csr_map_1[k][j][i].dimm2_use =
				    (uint_t)((map1 >> 7) & 0x1);
				pm->m_csr_map_1[k][j][i].dimm1_use =
				    (uint_t)((map1 >> 6) & 0x1);
				pm->m_csr_map_1[k][j][i].dimm0_use =
				    (uint_t)((map1 >> 5) & 0x1);
				pm->m_csr_map_1[k][j][i].dimm =
				    (uint_t)(map1 & 0xf);
				pm->m_csr_map_1[k][j][i].rank2x =
				    (uint_t)((map1 >> 24) & 0x1);
				pm->m_csr_map_1[k][j][i].dimm_spc =
				    (uint_t)((map1 >> 4) & 0x1);
			}
			pm->open_map[k][j] =
			    NHMEX_MAP_OPEN_RD(clump_topo, k, j);

			for (i = 0; i < NHMEX_MAX_PHYS_DIMM_MAP; i++) {
				val =
				    NHMEX_MAP_PHYS_DIMM_RD(clump_topo,
				    k, j, i);
				for (l = 0; l < NHMEX_MAX_MAP_NUMBER; l++) {
					pm->phydmap[k][j][i].pdmap[l] =
					    (uint_t)((val >> (5 * l)) & 0x1f);
					pm->phydmap[k][j][i].pdimm[l] =
					    (uint_t)((val >> (20 + 3 * l)) &
					    0x7);
				}
			}

			/*
			 * collect the xor map for this branch
			 */
			pm->xor_map[k][j] =
			    NHMEX_MAP_XORING_RD(clump_topo, k, j);
		}
		/*
		 * collect MB registers
		 */
		for (j = 0; j < NHMEX_MAX_FBD_CHANNEL; j++) {
			for (l = 0; l < NHMEX_MAX_DDR_CH; l++) {
				if (nhmex_mb_rd((clump_topo)->bus_number[k],
				    j >= 2 ? 1 : 0, j & 1, l, MB_FUC3,
				    MB_CSCTL_REG, &val1) == -1)
					continue;
				if (nhmex_mb_rd((clump_topo)->bus_number[k],
				    j >= 2 ? 1 : 0, j & 1, l, MB_FUC3,
				    MB_CSMASK_REG, &val2) == -1)
					continue;
				for (h = 0; h < NHMEX_MAX_DDR_CS; h++) {
					pm->csctl[k][j][l][h].id =
					    (val1 >> (h * 4)) & 0x7;
					pm->csctl[k][j][l][h].enable =
					    (val1 >> (3 + (h *4))) & 0x1;
					pm->csctl[k][j][l][h].mask =
					    (val2 >> (h * 2) & 0x3);
				}
			}
		}
	}
	return (0);
}

/*
 * Use SAD to determin the socket id and branch id
 */
static uint16_t
address_to_socket(uint64_t sys_addr, uint16_t *branch,
    uint16_t *clumpid, int *sadid)
{
	int i, j, index, tmp_sad_id = 0, sad_hit = 0, bbox_hit = 0;
	uint16_t socket, cboxid, nodeid;
	nhmex_mem_info_t *pm;

	for (j = 0; j < NHMEX_MAX_CLUMP_NUMBER; j++) {
		if (nhmex_gclp[j] == NULL || nhmex_gclp[j]->mem_info == NULL)
			continue;

		pm = nhmex_gclp[j]->mem_info;
		for (i = 0; i < NHMEX_MAX_SAD_ENTRY; i++) {
			if ((pm->sad_array[i].did ==
			    NHMEX_SAD_PRIMARY_DRAM) &&
			    (sys_addr  <= pm->sad_array[i].limit))
				/* array hit */
				if (pm->sad_array[i].idbase == 1) {
					/*
					 * This is home agent,
					 * clump and sad are both hit
					 */
					*clumpid = (uint16_t)j;
					tmp_sad_id = i;
					sad_hit = 1;
					break;
				}
		}
		if (sad_hit != 0)
			break;
	}

	if (sad_hit == 0) {
		return	(0xffff);
	}
	if (sadid != NULL) {
		*sadid = tmp_sad_id;
	}
	if (pm->sad_array[tmp_sad_id].tgtsel == 1) {
		/* addr[8:6] as target index */
		index = (int)((sys_addr >> 6) & 0x7);
	} else {
		/* addr[18:16] ^ addr[8:6] as target index */
		index = (int)(((sys_addr >> 6) & 0x7) ^
		    ((sys_addr >> 16) & 0x7));
	}

	/*
	 * get node ID [0] for this target
	 */
	nodeid = pm->sad_array[tmp_sad_id].idbase;

	if (pm->sad_array[tmp_sad_id].hemi) {

		/*
		 * node ID[1] = target[0] ^ cboxid[2]
		 * node ID[4:2] = target[3:1]
		 * NOTE: tgt list stores node ID[4:1]
		 */

		/*
		 *  cboxid[2] = hash[0] = addr[19^13^10^6]
		 */
		cboxid = ((sys_addr >> 19) & 0x1) ^ ((sys_addr >> 13) & 0x1) ^
		    ((sys_addr >> 10) & 0x1) ^ ((sys_addr >> 6) & 0x1);

		nodeid |= ((pm->sad_array[tmp_sad_id].tgt[index] >> 1)
		    & 0x7) << 2;

		nodeid |=
		    ((pm->sad_array[tmp_sad_id].tgt[index] & 0x01)
		    ^ cboxid) << 1;
	} else {

		/*
		 * node ID[4:1] = target index
		 */
		nodeid |= (pm->sad_array[tmp_sad_id].tgt[index] & 0xf) << 1;
	}

	INHMEX_DEBUG(CE_NOTE, "address_to_socket() nodeid = %x\n", nodeid);
	/*
	 * search all the sockets to see which socket and Bbox contain this NID
	 */
	for (i = 0; i < nhmex_gclp[*clumpid]->socket_number; i++) {
		for (j = 0; j < NHMEX_MAX_BRANCH_PER_SOCKET; j++) {
			INHMEX_DEBUG(CE_NOTE, "PCSR_MODE nid = %x\n",
			    pm->nodeid[i][j]);
			if (nodeid == pm->nodeid[i][j]) {
				bbox_hit = 1;
				break;
			}
		}
		if (bbox_hit)
			break;
	}

	socket = (uint16_t)i;
	if (branch != NULL)
		*branch = (uint16_t)j;

	if ((nhmex_gclp[*clumpid]->bus_number[socket] == 0) ||
	    NHMEX_DID_VID(nhmex_gclp[*clumpid]->bus_number[socket]) ==
	    0xffffffff) {
		INHMEX_DEBUG(CE_NOTE,
		    "ERROR:address_to_socket(), socket doesn't exist!\n");
		return (0xffff);
	}
	return (socket);
}

static void
xor_convert(uint16_t socket, uint16_t branch, uint64_t addr,
    nhmex_mem_info_t *mi, uint16_t *dimm, uint64_t *bank, uint16_t *rank)
{
	int i, bit, voff, hoff, bindex;
	uint64_t xor_bit;

	if (mi->xor_map[socket][branch] == 0)
		return;

	INHMEX_DEBUG(CE_NOTE, "before xor addr = %llx, dimm = %x,\
	    bank = %llx, rank = %x\n", (long long)addr,
	    *dimm, (long long)*bank, *rank);
	INHMEX_DEBUG(CE_NOTE, "xor_map = %x\n", mi->xor_map[socket][branch]);

	/*
	 * if the bit int xor_map is set, it means that Bbox local address
	 * bit is used for XORing.
	 */
	for (i = 0; i < 32; i++) {
		voff = i /4;
		hoff = i % 4;
		bindex = ((hoff * 6) + 7) + voff;
		if (voff == 7)
			bindex++;

		xor_bit = (addr >> bindex) & 1;
		bit = (mi->xor_map[socket][branch] >> i) & 1;
		if (bit) {
			switch (voff) {
			case 0:
				/* dimm[0] */
				*dimm =
				    (*dimm & 0x6) | ((*dimm & 1) ^ xor_bit);
				break;
			case 1:
				/* dimm[1] */
				*dimm =
				    (*dimm & 0x5) |
				    ((((*dimm >> 1) & 1) ^ xor_bit) << 1);
				break;
			case 2:
				/* bank[0] */
				*bank =
				    (*bank & 0x6) | ((*bank & 1) ^ xor_bit);
				break;
			case 3:
				/* bank[1] */
				*bank =
				    (*bank & 0x5) |
				    ((((*bank >> 1) & 1) ^ xor_bit) << 1);
				break;
			case 4:
				/* bank[2] */
				*bank =
				    (*bank & 0x3) |
				    ((((*bank >> 2) & 1) ^ xor_bit) << 2);
				break;
			case 5:
				/* rank[0] */
				*rank =
				    (*rank & 0x6) | ((*rank & 1) ^ xor_bit);
				break;
			case 6:
				/* rank[1] */
				*rank =
				    (*rank  & 0x5) |
				    ((((*rank >> 1) & 1) ^ xor_bit) << 1);
				break;
			case 7:
				/* dimm[2] */
				*dimm =
				    (*dimm & 0x3) |
				    ((((*dimm >> 2) & 1) ^ xor_bit) << 2);
				break;
			}
		}
	}
	INHMEX_DEBUG(CE_NOTE, "after xor rank = %x, bank = %llx, dimm = %x\n",
	    *rank, (long long)*bank, *dimm);
}

/*
 * Use TAD to determin the map id,
 * and corresponding Mbox address
 */
static uint64_t
address_to_mbox(uint64_t sys_addr, nhmex_mem_info_t *mi,
    uint16_t socket, uint16_t branch, uint16_t *map)
{
	/*
	 * Since there is a single pair of lock-stepped
	 * channels connected to a Mbox, channel selection
	 * bits are not needed.
	 * 1. range match
	 * 2. remove interleave bits
	 * 3. add relocation amount that help remove gaps
	 */

	int i, map_hit = 0, tad_hit = 0;
	uint64_t mbox_addr, tmp_addr, addr1, addr2;

	/*
	 * 1. select map rigion, get target mapper by comparing
	 * the addr with map rigion, the 14b MAP region limit
	 * specifies the system addre[43:30] upper boundary
	 */
	for (i = 0; i < NHMEX_MAX_MAP_ENTRY; i++) {
		if (sys_addr <= mi->map_limit[socket][branch][i]) {
			map_hit = i;
			break;
		}
	}

	if (i >= NHMEX_MAX_MAP_ENTRY) {
		return (EX_MEM_TRANS_ERR);
	} else {
		*map = (uint16_t)map_hit;
	}

	/*
	 * 2. select tad region, get interleave way from shift amount,
	 * eliminate interleave bits {8/7/6}:6, right shift 0...3.
	 * form addr[39:6]
	 */
	for (i = 0; i < NHMEX_MAX_TAD_ENTRY; i++) {
		if (sys_addr <=
		    (uint64_t)mi->tad_array[socket][branch][i].tad_limit) {
			tad_hit = i;
			break;
		}
	}

	if (i >= NHMEX_MAX_TAD_ENTRY) {
		return (EX_MEM_TRANS_ERR);
	} else {
		switch (mi->tad_array[socket][branch][tad_hit].ways) {
		case 2:
			/* remove bit 6 */
			tmp_addr = (sys_addr & 0x3f) | ((sys_addr >> 7) << 6);
			break;
		case 4:
			/* remove bit 7 & bit 6 */
			tmp_addr = (sys_addr & 0x3f) | ((sys_addr >> 8) << 6);
			break;
		case 8:
			/* remove bit 6-8 */
			tmp_addr = (sys_addr & 0x3f) | ((sys_addr >> 9) << 6);
			break;
		case 0:
		default:
			tmp_addr = sys_addr;
			break;
		}
	}

	/*
	 * 3. get addr[43:6]
	 */
	tmp_addr &= 0xfffffffffc0;

	/*
	 * 4. get local base addr (offset) 12b, add with the addr[39:28]
	 * we get from 3, calculate addr[33:22] to send to Mbox
	 */
	addr1 = (((tmp_addr >> 28) & 0xffff) -
	    mi->tad_array[socket][branch][tad_hit].offset) << 22;
	INHMEX_DEBUG(CE_NOTE, "OFFSET = %x\n",
	    mi->tad_array[socket][branch][tad_hit].offset);
	/*
	 * 5. get addr[27:6] get from 3 to form addr[21:0] to Mbox
	 */
	addr2 = (tmp_addr >> 6) & 0x3fffff;

	/*
	 * 6. calculate the the final addr that ready to be sent to Mbox
	 */
	mbox_addr = addr1 | addr2;

	INHMEX_DEBUG(CE_NOTE, "MBoxAddr %llx\n", (long long)mbox_addr);
	return (mbox_addr);
}

/*
 * convert the Mbox addr to system addr
 */
static uint64_t
mboxaddr_to_sysaddr(uint64_t mbox_addr, nhmex_mem_info_t *mi,
    uint16_t socket, uint16_t branch)
{
	uint64_t addr1, addr2, tmpaddr, sysaddr;
	uint64_t low_limit = 0, high_limit = 0;
	int i, index, tad_hit = 0, sad_hit = 0;
	uint16_t tclump, target;
	uint32_t pa_19_13_10 = 0, pa_6 = 0;

	if ((socket >= NHMEX_MAX_CPU_NUMBER) ||
	    (branch >= NHMEX_MAX_BRANCH_PER_SOCKET)) {
		return (EX_MEM_TRANS_ERR);
	}
	/*
	 * get the right tad array id
	 */
	for (i = 0; i <  NHMEX_MAX_TAD_ENTRY; i++) {
		/* Guess the right TAD entry */
		high_limit = mi->tad_array[socket][branch][i].tad_limit;
		tmpaddr =  ((((mbox_addr >> 22) & 0xffff) +
		    mi->tad_array[socket][branch][i].offset) << 22) |
		    (mbox_addr & 0x7fffff);

		/*
		 * get sys addr[27:6] first, sys addr[27:6] = mbox addr[21:0]
		 */
		addr1 = (mbox_addr & 0x3fffff) << 6;

		/*
		 * get sys addr[39:28] and add with the tad offset
		 */
		INHMEX_DEBUG(CE_NOTE, "OFFSET = %x\n",
		    mi->tad_array[socket][branch][i].offset);
		addr2 = (((mbox_addr >> 22) & 0xffff) +
		    mi->tad_array[socket][branch][i].offset) << 28;

		/*
		 * get local address
		 */
		tmpaddr = addr1 | addr2;

		/*
		 * add the interleave bits
		 */
		switch (mi->tad_array[socket][branch][i].ways) {
		case 2:
			/* add back bit 6 */
			sysaddr = (tmpaddr & 0x3f) |
			    ((tmpaddr & 0xfffffffffffc0) << 1);
			break;
		case 4:
			/* add back bit 6 & bit 7 */
			sysaddr = (tmpaddr & 0x3f) |
			    ((tmpaddr & 0xfffffffffffc0) << 2);
			break;
		case 8:
			/* add back bit 6-8 */
			sysaddr =  (tmpaddr & 0x3f) |
			    ((tmpaddr & 0xfffffffffffc0) << 3);
			break;
		case 0:
		default:
			sysaddr = tmpaddr;
			break;
		}
		if ((sysaddr <= high_limit) && (sysaddr >= low_limit)) {
			if (address_to_socket(sysaddr, NULL,
			    &tclump, &sad_hit) == 0xffff)
				return (EX_MEM_TRANS_ERR);
			tad_hit = i;
			break;
		}
		low_limit = low_limit + 1;
	}

	if (i >= NHMEX_MAX_TAD_ENTRY) {
		return (EX_MEM_TRANS_ERR);
	}

	target = mi->nodeid[socket][branch] >> 1;
	if (mi->sad_array[sad_hit].hemi) {
		/* get a hit for sysaddr[6] */
		pa_19_13_10 = ((sysaddr >> 19) & 1) ^ ((sysaddr >> 13) & 1) ^
		    ((sysaddr >> 10) & 1);
		pa_6 = pa_19_13_10 ^ (target & 1);
		target = (target >> 1) << 1;
	}

	for (i = 0; i < mi->tad_array[socket][branch][tad_hit].ways; i++) {
		if (target == mi->sad_array[sad_hit].tgt[i]) {
			if (mi->sad_array[sad_hit].tgtsel == 0) {
				index = i ^ ((sysaddr >> 16) & 7);
			} else {
				index = i;
			}
			if ((mi->sad_array[sad_hit].hemi) &&
			    (pa_6 != (index & 1)))
				continue;

			sysaddr = (sysaddr & 0xfffffffffc0) |
			    (index << 6);
			break;
		}
	}
	/*
	 * Need to add back bit[43:40]
	 */
	sysaddr |= mi->tad_array[socket][branch][tad_hit].tad_limit
	    & 0xf0000000000;

	INHMEX_DEBUG(CE_NOTE, "SysAddr: %llx\n", (long long)sysaddr);
	return (sysaddr);
}

void
use_bit_check(nhmex_mem_info_t *mi, uint16_t socket, uint16_t branch,
    uint16_t map, uint16_t *dimm, uint16_t *rank, uint64_t *bank)
{

	/* set or clear bits according to map setting */
	if (!mi->m_csr_map_1[socket][branch][map].dimm2_use)
		*dimm &= 3;
	if (!mi->m_csr_map_1[socket][branch][map].dimm1_use)
		*dimm &= 5;
	if (!mi->m_csr_map_1[socket][branch][map].dimm0_use)
		*dimm &= 6;

	if (mi->m_csr_map_1[socket][branch][map].srank1_use  == BIT_ZERO)
		*rank &= 1;
	else if (mi->m_csr_map_1[socket][branch][map].srank1_use  == BIT_ONE)
		*rank = *rank | 2;
	else if (mi->m_csr_map_1[socket][branch][map].srank1_use  == RESERVED) {
		INHMEX_DEBUG(CE_NOTE, "srank0_use is RESERVERD\n");
	}

	if (mi->m_csr_map_1[socket][branch][map].srank0_use  == BIT_ZERO)
		*rank &= 2;
	else if (mi->m_csr_map_1[socket][branch][map].srank0_use  == BIT_ONE)
		*rank = *rank | 1;
	else if (mi->m_csr_map_1[socket][branch][map].srank0_use  == RESERVED) {
		INHMEX_DEBUG(CE_NOTE, "srank0_use is RESERVERD\n");
	}

	if (mi->m_csr_map_1[socket][branch][map].bank3_use == BIT_ZERO)
		*bank = *bank & 0x7;
	else if (mi->m_csr_map_1[socket][branch][map].bank3_use == BIT_ONE)
		*bank = *bank | 0x8;
	else if (mi->m_csr_map_1[socket][branch][map].bank3_use == RESERVED) {
		INHMEX_DEBUG(CE_NOTE, "bank_bit3_use is RESERVERD\n");
	}

	if (mi->m_csr_map_1[socket][branch][map].bank2_use == BIT_ZERO)
		*bank = *bank & 0xb;
	else if (mi->m_csr_map_1[socket][branch][map].bank2_use == BIT_ONE)
		*bank = *bank | 0x4;
	else if (mi->m_csr_map_1[socket][branch][map].bank2_use == RESERVED) {
		INHMEX_DEBUG(CE_NOTE, "bank_bit2_use is RESERVERD\n");
	}

}
/*
 * calculate the DRAM address mapping by giving the Mbox address(33bit)
 */
static uint64_t
address_to_dimm(uint64_t addr, uint16_t socket,
    uint16_t branch, uint16_t map, nhmex_mem_info_t *mi,
    uint16_t *ddrch, uint16_t *fbdch, uint16_t *dimm,
    uint16_t *rank, uint64_t *bank, uint64_t *row, uint64_t *col)
{
	uint16_t ldimm = 0, fbd_dimm = 0, fbd_rank = 0;
	int i, j, pdimm_hit = 0;

	if (mi->open_map[socket][branch]) {
		if (mi->m_csr_map_1[socket][branch][map].rank2x) {
			if (!mi->m_csr_map_0[socket][branch][map].col)
				/*
				 * col[13:10,9:7,6:3] =
				 * addr[11:8,3:1,7:4]
				 */
				*col = (((addr >> 8) & 0xf) << 10) |
				    (((addr >> 1) & 0x7) << 7) |
				    (((addr >> 4) & 0xf) << 3);
			else
				/*
				 * col[13:10,9:8,7:3,2] =
				 * addr[12:9,3:2,8:4,1]
				 */
				*col = (((addr >> 9) & 0xf) << 10) |
				    (((addr >> 2) & 0x3) << 8) |
				    (((addr >> 4) & 0x1f) << 3) |
				    (((addr >> 1) & 0x1) << 2);

			if (!mi->m_csr_map_1[socket][branch][map].dimm_spc)
				/* dimm[0] = addr[0] */
				ldimm |= addr & 0x1;
		} else {
			if (!mi->m_csr_map_0[socket][branch][map].col)
				/*
				 * col addr[13:10,9:7,6:3] =
				 * addr[10:7,2:0,6:3]
				 */
				*col = (((addr >> 7) & 0xf) << 10) |
				    ((addr & 0x7) << 7) |
				    (((addr >> 3) & 0xf) << 3);
			else
				/*
				 * col addr[13:10,9:8,7:3,2] =
				 * addr[11:8,2:1,7:3,0]
				 */
				*col = (((addr >> 8) & 0xf) << 10) |
				    (((addr >> 1) & 0x3) << 8) |
				    (((addr >> 3) & 0x1f) << 3) |
				    ((addr & 0x1) << 2);

			if (!mi->m_csr_map_1[socket][branch][map].dimm_spc)
				/* dimm[0] */
				ldimm |= (addr >> (7 +
				    mi->m_csr_map_1[socket][branch][map].dimm))
				    & 0x1;
		}
		if (!mi->m_csr_map_1[socket][branch][map].dimm_spc)
			/* dimm[2:1] */
			ldimm |= ((addr >> (8 +
			    mi->m_csr_map_1[socket][branch][map].dimm)) &
			    0x3) << 1;

		/* rank[1:0] */
		fbd_rank |= ((addr >> (7 +
		    mi->m_csr_map_1[socket][branch][map].stacked_rank)) & 0x3);

		/* bank[2:0] */
		*bank |= ((addr >> (7 +
		    mi->m_csr_map_1[socket][branch][map].bank)) & 0x7);

	} else {
		/* open_map == 0 */
		switch (mi->m_csr_map_0[socket][branch][map].col) {
		case 0:
			/*
			 * col[13:10,9,8:3] = addr[12:9,2,8:3]
			 */
			*col = (((addr >> 9) & 0xf) << 10) |
			    (((addr >> 2) & 0x1) << 9) |
			    (((addr >> 3) & 0x3f) << 3);
			break;
		case 1:
			/*
			 * col[13:2] = addr[13:2]
			 */
			*col = ((addr >> 2) & 0xfff) << 2;
			break;
		case 2:
			/*
			 * col[13:10,3,9:4,2] = addr[14:11,10,9:4,3]
			 */
			*col = (((addr >> 11) & 0xf) << 10) |
			    (((addr >> 10) & 1) << 3) |
			    (((addr >> 4) & 0x3f) << 4) |
			    (((addr >> 3) & 1) << 2);
			break;
		case 3:
			/*
			 * col[13:10,4:3,9:5,2] = addr[15:12,11:10, 9:5,4]
			 */
			*col = (((addr >> 12) & 0xf) << 10) |
			    (((addr >> 10) & 0x3) << 3) |
			    (((addr >> 5) & 0x1f) << 5) |
			    (((addr >> 4) & 1) << 2);
			break;
		case 4:
			/*
			 * col[13:10,5:3,9:6,2] = addr[16:13,12:10,9:6,5]
			 */
			*col = (((addr >> 13) & 0xf) << 10) |
			    (((addr >> 10) & 0x7) << 3) |
			    (((addr >> 6) & 0xf) << 6) |
			    (((addr >> 5) & 1) << 2);
			break;
		case 5:
			/*
			 * col[13:10,6:3,9:7,2] = addr[17:14,13:10:9:7,6]
			 */
			*col = (((addr >> 14) & 0xf) << 13) |
			    (((addr >> 10) & 0xf) << 3) |
			    (((addr >> 7) & 0x7) << 7) |
			    (((addr >> 6) & 1) << 2);
			break;
		case 6:
			/*
			 * col[13:10,7:3,9:8,2] = addr[18:15,14:10,9:8,7]
			 */
			*col = (((addr >> 15) & 0xf) << 13) |
			    (((addr >> 10) & 0x1f) << 3) |
			    (((addr >> 8) & 0x3) << 8) |
			    (((addr >> 7) & 1) << 2);
			break;
		case 7:
			/*
			 * col[13:10,8:3,9,2] = addr[19:16,15:10,9,8]
			 */
			*col = (((addr >> 16) & 0xf) << 13) |
			    (((addr >> 10) & 0x3f) << 3) |
			    (((addr >> 9) & 1) << 9) |
			    (((addr >> 8) & 1) << 2);
			break;
		default:
			break;
		}
		if (!mi->m_csr_map_1[socket][branch][map].dimm_spc)
			/* dimm[2:0] = addr[2:0] */
			ldimm |= addr & 0x7;

		/* rank[1:0] */
		fbd_rank |= (addr >>
		    mi->m_csr_map_1[socket][branch][map].stacked_rank) & 0x3;

		/* bank[3:0] */
		*bank |= (addr >>
		    mi->m_csr_map_1[socket][branch][map].bank) & 0x7;
	}

	/* row addr */
	switch (mi->m_csr_map_0[socket][branch][map].row) {
	case 0:
		/* row[15:0] = addr[24:9] */
		*row = (addr >> 9) & 0xffff;
		break;
	case 1:
		/* row[15:14,13:1,0] = addr[25:24,22:10,23] */
		*row = (((addr >> 24) & 0x3) << 14) |
		    (((addr >> 10) & 0x1fff) << 1) |
		    ((addr >> 23) & 0x1);
		break;
	case 2:
		/* row[15:14,13:2,1:0] = addr[26:25,22:11,24:23] */
		*row = (((addr >> 25) & 0x3) << 14) |
		    (((addr >> 11) & 0xfff) << 2)|
		    ((addr >> 23) & 0x3);
		break;
	case 3:
		/* row[15:14,13:3,2:0] = addr[27:26,22:12,25:23] */
		*row = (((addr >> 26) & 0x3) << 14) |
		    (((addr >> 12) & 0x7ff) << 3) |
		    ((addr >> 23) & 7);
		break;
	case 4:
		/* row[15:14,13:4,3:0] = addr[28:27,22:13,26:23] */
		*row = (((addr >> 27) & 0x3) << 14) |
		    (((addr >> 13) & 0x3ff) << 4) |
		    ((addr >> 23) & 0xf);
		break;
	case 5:
		/* row[15:14,13:5,4:0] = addr[29:28,22:14,27:23] */
		*row = (((addr >> 28) & 0x3) << 14) |
		    (((addr >> 14) & 0x1ff) << 5) |
		    ((addr >> 23) & 0x1f);
		break;
	case 6:
		/* row[15:14,13:6,5:0] = addr[30:29,22:15,28:23] */
		*row = (((addr >> 29) & 0x3) << 14) |
		    (((addr >> 15) & 0xff) << 6) |
		    ((addr >> 23) & 0x3f);
		break;
	case 7:
		/* row[15:14,13:7,6:0] = addr[31:30,22:16,29:23] */
		*row = (((addr >> 30) & 0x3) << 14) |
		    (((addr >> 16) & 0x7f) << 7) |
		    ((addr >> 23) & 0x7f);
		break;
	case 8:
		/* row[15:14,31:8,7:0] = addr[32:31,22:17,30:23] */
		*row = (((addr >> 31) & 0x3) << 14) |
		    (((addr >> 17) & 0x3f) << 8) |
		    ((addr >> 23) & 0xff);
		break;
	case 9:
		/* row[15:14,13:9,8:0] = addr[33:32,22:18,31:23] */
		*row = (((addr >> 32) & 0x3) << 14) |
		    (((addr >> 18) & 0x1f) << 9) |
		    ((addr >> 23) & 0x1ff);
		break;
	case 10:
		/* row[14,13:10,9:0] = addr[33,22:19,32:23] */
		*row = (((addr >> 33) & 0x1) << 14) |
		    (((addr >> 19) & 0xf) << 10) |
		    ((addr >> 23) & 0x3ff);
		break;
	case 11:
		/* row[13:11,10:0] = addr[22:20,33:23] */
		*row = (((addr >> 20) & 0x3) << 11) |
		    ((addr >> 23) & 0x7ff);
	default:
		break;
	}

	if (mi->m_csr_map_1[socket][branch][map].dimm_spc) {
		/* dimm[2:0] */
		ldimm = (addr >>
		    (23 + mi->m_csr_map_1[socket][branch][map].dimm)) & 0x7;
	}
	INHMEX_DEBUG(CE_NOTE, "1. socket%d branch%d fbd_dimm%x fbd_rank%x \
	    row%llx bank%llx col%llx.\n",  socket, branch, fbd_dimm, fbd_rank,
	    (long long)*row, (long long)*bank, (long long)*col);

	use_bit_check(mi, socket, branch, map, &ldimm, &fbd_rank, bank);

	if (mi->m_csr_map_0[socket][branch][map].c10_use == BIT_ZERO)
		*col = *col & 0x3bff;
	else if (mi->m_csr_map_0[socket][branch][map].c10_use == BIT_ONE)
		*col = *col | 0x0400;
	else if (mi->m_csr_map_0[socket][branch][map].c10_use == RESERVED) {
		INHMEX_DEBUG(CE_NOTE, "col_bit10 is RESERVED\n");
		return (-1ULL);
	}
	if (mi->m_csr_map_0[socket][branch][map].c11_use == BIT_ZERO)
		*col = *col & 0x37ff;
	else if (mi->m_csr_map_0[socket][branch][map].c11_use == BIT_ONE)
		*col = *col | 0x0800;
	else if (mi->m_csr_map_0[socket][branch][map].c11_use == RESERVED) {
		INHMEX_DEBUG(CE_NOTE, "col_bit11 is RESERVED\n");
		return (-1ULL);
	}
	if (mi->m_csr_map_0[socket][branch][map].c12_use == BIT_ZERO)
		*col = *col & 0x2fff;
	else if (mi->m_csr_map_0[socket][branch][map].c12_use == BIT_ONE)
		*col = *col | 0x1000;
	else if (mi->m_csr_map_0[socket][branch][map].c12_use == RESERVED) {
		INHMEX_DEBUG(CE_NOTE, "col_bit11 is RESERVED\n");
		return (-1ULL);
	}
	if (mi->m_csr_map_0[socket][branch][map].c13_use == BIT_ZERO)
		*col = *col & 0x1fff;
	else if (mi->m_csr_map_0[socket][branch][map].c13_use == BIT_ONE)
		*col = *col | 0x2000;
	else if (mi->m_csr_map_0[socket][branch][map].c13_use == RESERVED) {
		INHMEX_DEBUG(CE_NOTE, "col_bit11 is RESERVED\n");
		return (-1ULL);
	}

	if (mi->m_csr_map_0[socket][branch][map].r14_use == BIT_ZERO)
		*row = *row & 0xbfff;
	else if (mi->m_csr_map_0[socket][branch][map].r14_use == BIT_ONE)
		*row = *row | 0x4000;
	else if (mi->m_csr_map_0[socket][branch][map].r14_use == RESERVED) {
		INHMEX_DEBUG(CE_NOTE, "row_bit14 is RESERVED\n");
		return (-1ULL);
	}
	if (mi->m_csr_map_0[socket][branch][map].r15_use == BIT_ZERO)
		*row = *row & 0x7fff;
	else if (mi->m_csr_map_0[socket][branch][map].r15_use == BIT_ONE)
		*row = *row | 0x8000;
	else if (mi->m_csr_map_0[socket][branch][map].r15_use == RESERVED) {
		INHMEX_DEBUG(CE_NOTE, "row_bit15 is RESERVED\n");
		return (-1ULL);
	}
	INHMEX_DEBUG(CE_NOTE, "-12-socket = %x, branch = %x, ldimm = %x\n",
	    socket, branch, ldimm);

	xor_convert(socket, branch, addr, mi,  &ldimm, bank, &fbd_rank);

	for (i = 0; i < NHMEX_MAX_PHYS_DIMM_MAP; i++) {
		for (j = 0; j < NHMEX_MAX_MAP_NUMBER; j++) {
			INHMEX_DEBUG(CE_NOTE, "pdmap = %x\n",
			    mi->phydmap[socket][branch][i].pdmap[j]);
			if (((mi->phydmap[socket][branch][i].pdmap[j] & 3) ==
			    map) && (((mi->phydmap[socket][branch][i].pdmap[j]
			    >> 2) & 7) == ldimm)) {
				pdimm_hit = 1;
				break;
			}
		}
		if (pdimm_hit)
			break;
	}

	if (pdimm_hit != 1) {
		INHMEX_DEBUG(CE_NOTE,
		    "Can not find this physical dimm location.\n");
		return (EX_MEM_TRANS_ERR);
	}
	fbd_dimm = mi->phydmap[socket][branch][i].pdimm[j];

	INHMEX_DEBUG(CE_NOTE, "2. socket%d branch%d fbd_dimm%x fbd_rank%x \
	    bank%llx, row%llx col%llx.\n",  socket, branch, fbd_dimm, fbd_rank,
	    (long long)*bank, (long long)*row, (long long)*col);

	*dimm = fbd_dimm_to_ddr(mi, addr, socket, branch,
	    fbd_dimm, fbd_rank, ddrch, fbdch, rank);

	INHMEX_DEBUG(CE_NOTE, "3. socket%d branch%d channel %x dimm%x rank%x \
	    row%llx bank%llx col%llx.\n", socket, branch, *fbdch,
	    *dimm, *rank, (long long)*row, (long long)*bank, (long long)*col);

	if (*dimm != 0xffff)
		return (0);
	else
		return (-1ULL);
}

uint64_t
get_addr(uint16_t socket, uint16_t branch, uint16_t map, nhmex_mem_info_t *mi,
    uint16_t tdimm, uint16_t trank, uint64_t tbank, uint64_t col, uint64_t row)
{
	uint64_t addr = 0;

	if (mi->open_map[socket][branch]) {
		if (mi->m_csr_map_1[socket][branch][map].rank2x) {
			if (!mi->m_csr_map_0[socket][branch][map].col)
				/*
				 * col[13:10,9:7,6:3] =
				 * addr[11:8,3:1,7:4]
				 */
				addr |= (((col >> 10) & 0xf) << 8) |
				    (((col >> 7) & 0x7) << 1) |
				    (((col >> 3) & 0xf) << 4);
			else
				/*
				 * col[13:10,9:8,7:3,2] =
				 * addr[12:9,3:2,8:4,1]
				 */
				addr |= (((col >> 10) & 0xf) << 9) |
				    (((col >> 8) & 0x3) << 2) |
				    (((col >> 3) & 0x1f) << 4) |
				    (((col >> 2) & 0x1) << 1);

			if (!mi->m_csr_map_1[socket][branch][map].dimm_spc)
				/* dimm[0] = addr[0] */
				addr |= tdimm & 0x1;
		} else {
			if (!mi->m_csr_map_0[socket][branch][map].col)
				/*
				 * col addr[13:10,9:7,6:3] =
				 * addr[10:7,2:0,6:3]
				 */
				addr |= (((col >> 10) & 0xf) << 7) |
				    ((col >> 7) & 0x7) |
				    (((col >> 3) & 0xf) << 3);
			else
				/*
				 * col addr[13:10,9:8,7:3,2] =
				 * addr[11:8,2:1,7:3,0]
				 */
				addr |= (((col >> 10) & 0xf) << 8) |
				    (((col >> 8) & 0x3) << 1) |
				    (((col >> 3) & 0x1f) << 3) |
				    ((col >> 2) & 0x1);

			if (!mi->m_csr_map_1[socket][branch][map].dimm_spc)
				/* dimm[0] */
				addr |=
				    (tdimm & 0x1) << (7 +
				    mi->m_csr_map_1[socket][branch][map].dimm);
		}

		if (!mi->m_csr_map_1[socket][branch][map].dimm_spc)
			/* dimm[2:1] */
			addr |= ((tdimm >> 1) & 0x3) <<
			    (8 + mi->m_csr_map_1[socket][branch][map].dimm);

		/* rank[1:0] */
		addr |= (trank & 0x3) <<
		    (7 + mi->m_csr_map_1[socket][branch][map].stacked_rank);

		/* bank[2:0] */
		addr |=  (tbank & 0x7) <<
		    (7 + mi->m_csr_map_1[socket][branch][map].bank);
	} else {
		/* open_map == 0 */
		switch (mi->m_csr_map_0[socket][branch][map].col) {
		case 0:
			/*
			 * col[13:10,9,8:3] = addr[12:9,2,8:3]
			 */
			addr |= (((col >> 10) & 0xf) << 9) |
			    (((col >> 9) & 0x1) << 2) |
			    (((col >> 3) & 0x3f) << 3);
			break;
		case 1:
			/*
			 * col[13:2] = addr[13:2]
			 */
			addr |= ((col >> 2) & 0xfff) << 2;
			break;
		case 2:
			/*
			 * col[13:10,3,9:4,2] = addr[14:11,10,9:4,3]
			 */
			addr |= (((col >> 10) & 0xf) << 11) |
			    (((col >> 3) & 0x1) << 10) |
			    (((col >> 4) & 0x3f) << 4) |
			    (((col >> 2) & 1) << 3);
			break;
		case 3:
			/*
			 * col[13:10,4:3,9:5,2] = addr[15:12,11:10, 9:5,4]
			 */
			addr |= (((col >> 10) & 0xf) << 12) |
			    (((col >> 3) & 0x3) << 10) |
			    (((col >> 5) & 0x1f) << 5) |
			    (((col >> 2) & 1) << 4);
			break;
		case 4:
			/*
			 * col[13:10,5:3,9:6,2] = addr[16:13,12:10,9:6,5]
			 */
			addr |= (((col >> 10) & 0xf) << 13) |
			    (((col >> 3) & 0x7) << 10) |
			    (((col >> 6) & 0xf) << 6) |
			    (((col >> 2) & 1) << 5);
			break;
		case 5:
			/*
			 * col[13:10,6:3,9:7,2] = addr[17:14,13:10,9:7,6]
			 */
			addr |= (((col >> 10) & 0xf) << 14) |
			    (((col >> 3) & 0xf) << 10) |
			    (((col >> 7) & 0x7) << 7) |
			    (((col >> 2) & 1) << 6);
			break;
		case 6:
			/*
			 * col[13:10,7:3,9:8,2] = addr[18:15,14:10,9:8,7]
			 */
			addr |= (((col >> 10) & 0xf) << 15) |
			    (((col >> 3) & 0x1f) << 10) |
			    (((col >> 8) & 0x3) << 8) |
			    (((col >> 2) & 1) << 7);
			break;
		case 7:
			/*
			 * col[13:10,8:3,9,2] = addr[19:16,15:10,9,8]
			 */
			addr |= (((col >> 10) & 0xf) << 16) |
			    (((col >> 3) & 0x3f) << 10) |
			    (((col >> 9) & 1) << 9) |
			    (((col >> 2) & 1) << 8);
			break;
		default:
			break;
		}
		INHMEX_DEBUG(CE_NOTE, "****addr = %llx", (long long)addr);

		if (!mi->m_csr_map_1[socket][branch][map].dimm_spc)
			/* dimm[2:0] = addr[2:0] */
			addr |= tdimm & 0x7;

		/* rank[1:0] */
		addr |= (trank & 0x3)  <<
		    mi->m_csr_map_1[socket][branch][map].stacked_rank;
		INHMEX_DEBUG(CE_NOTE, "****1.addr = %llx", (long long)addr);

		/* bank[3:0] */
		addr |= (tbank & 0x7) <<
		    mi->m_csr_map_1[socket][branch][map].bank;
		INHMEX_DEBUG(CE_NOTE, "****2.addr = %llx", (long long)addr);
	}

	/* row addr */
	switch (mi->m_csr_map_0[socket][branch][map].row) {
	case 0:
		/* row[15:0] = addr[24:9] */
		addr |= (row & 0xffff) << 9;
		break;
	case 1:
		/* row[15:14,13:1,0] = addr[25:24,22:10,23] */
		addr |= (((row >> 14) & 0x3) << 24) |
		    (((row >> 1) & 0x1fff) << 10) |
		    ((row & 0x1) << 23);
		break;
	case 2:
		/* row[15:14,13:2,1:0] = addr[26:25,22:11,24:23] */
		addr |= (((row >> 14) & 0x3) << 25) |
		    (((row >> 2) & 0xfff) << 11) |
		    ((row & 0x3) << 23);
		break;
	case 3:
		/* row[15:14,13:3,2:0] = addr[27:26,22:12,25:23] */
		addr |= (((row >> 14) & 0x3) << 26) |
		    (((row >> 3) & 0x7ff) << 12) |
		    ((row & 0x7) << 23);
		break;
	case 4:
		/* row[15:14,13:4,3:0] = addr[28:27,22:13,26:23] */
		addr |= (((row >> 14) & 0x3) << 27) |
		    (((row >> 4) & 0x3ff) << 13) |
		    ((row & 0xf) << 23);
		break;
	case 5:
		/* row[15:14,13:5,4:0] = addr[29:28,22:14,27:23] */
		addr |= (((row >> 14) & 0x3) << 28) |
		    (((row >> 5) & 0x1ff) << 14) |
		    ((row & 0x1f) << 23);
		break;
	case 6:
		/* row[15:14,13:6,5:0] = addr[30:29,22:15,28:23] */
		addr |= (((row >> 14) & 0x3) << 29) |
		    (((row >> 6) & 0xff) << 15) |
		    ((row & 0x3f) << 23);
		break;
	case 7:
		/* row[15:14,13:7,6:0] = addr[31:30,22:16,29:23] */
		addr |= (((row >> 14) & 0x3) << 30) |
		    (((row >> 7) & 0x7f) << 16) |
		    ((row & 0x7f) << 23);
		break;
	case 8:
		/* row[15:14,13:8,7:0] = addr[32:31,22:17,30:23] */
		addr |= (((row >> 14) & 0x3) << 31) |
		    (((row >> 8) & 0x3f) << 17) |
		    ((row & 0xff) << 23);
		break;
	case 9:
		/* row[15:14,13:9,8:0] = addr[33:32,22:18,31:23] */
		addr |= (((row >> 14) & 0x3) << 32) |
		    (((row >> 9) & 0x1f) << 18) |
		    ((row & 0x1ff) << 23);
		break;
	case 10:
		/* row[14,13:10,9:0] = addr[33,22:19,32:23] */
		addr |= (((row >> 14) & 0x1) << 33) |
		    (((row >> 10) & 0xf) << 19) |
		    ((row & 0x3ff) << 23);
		break;
	case 11:
		/* row[13:11,10:0] = addr[22:20,33:23] */
		addr |= (((row >> 11) & 0x7) << 20) |
		    ((row & 0x7ff) << 23);
	default:
		break;
	}

	if (mi->m_csr_map_1[socket][branch][map].dimm_spc)
		/* dimm[2:0] */
		addr |= (tdimm & 0x7) <<
		    (23 + mi->m_csr_map_1[socket][branch][map].dimm);

	return (addr);
}
/*
 * calculate the Mbox address by giving DRAM address mapping
 */
static uint64_t
dimm_to_address(uint16_t socket, uint16_t branch,
    uint16_t ddrch, uint16_t ddr_dimm, uint16_t ddr_rank, uint64_t bank,
    uint64_t row, uint64_t col, nhmex_mem_info_t *mi)
{
	uint64_t raddr = 0, addr = 0;
	uint_t map, d0_use, d1_use, d2_use, srank0, srank1, b3, b2;
	uint16_t dimm, rank, solved_xor = 0;
	uint16_t tdimm, trank, xor_dimm, xor_rank, tmp;
	uint64_t tbank, xor_bank;
	int i;
	/*
	 * get fbd dimm#[2:1] and rank#
	 */
	ddr_rank = ddr_rank % 4;
	dimm =
	    ddr_dimm_to_fbd(mi, socket, branch,
	    ddrch, ddr_dimm, ddr_rank, &rank);

	INHMEX_DEBUG(CE_NOTE, "fbddimm = %d, fbdrank = %d\n", dimm, rank);

	if (dimm == 0xffff)
		return (EX_MEM_TRANS_ERR);

	/*
	 * Here we don't get the right dimm# bank# rank# for calculation
	 * if there's xor bit set. Since we also don't have the bbox address
	 * to do the xor, we have to guess untill we satisfy all the conditions.
	 */
	tmp = dimm;
	for (i = 0; i < 2; i++) {
		if (i == 1) {
			tmp |= 0x1;
		}
		/*
		 * Get corresponding map id, since we are not sure about
		 * dimm[0], need to try the 2nd time if we cannot
		 * hit during the first round.
		 */
		map = mi->phydmap[socket][branch][tmp / 4].pdmap[tmp % 4] &
		    0x3;

		/*
		 * Remove unused bit from col, row before calculate the address
		 */
		if (mi->m_csr_map_0[socket][branch][map].c13_use != BIT_USE)
			col = col & 0x1fff;
		if (mi->m_csr_map_0[socket][branch][map].c12_use != BIT_USE)
			col = col & 0x2fff;
		if (mi->m_csr_map_0[socket][branch][map].c11_use != BIT_USE)
			col = col & 0x37ff;
		if (mi->m_csr_map_0[socket][branch][map].c10_use != BIT_USE)
			col = col & 0x3bff;
		if (mi->m_csr_map_0[socket][branch][map].c2_use != BIT_USE)
			col = col & 0x3ffb;

		if (mi->m_csr_map_0[socket][branch][map].r15_use != BIT_USE)
			row = row & 0x7fff;
		if (mi->m_csr_map_0[socket][branch][map].r14_use != BIT_USE)
			row = row & 0xbfff;

		d2_use = mi->m_csr_map_1[socket][branch][map].dimm2_use;
		d1_use = mi->m_csr_map_1[socket][branch][map].dimm1_use;
		d0_use = mi->m_csr_map_1[socket][branch][map].dimm0_use;
		srank0 = mi->m_csr_map_1[socket][branch][map].srank0_use;
		srank1 = mi->m_csr_map_1[socket][branch][map].srank1_use;
		b3 = mi->m_csr_map_1[socket][branch][map].bank3_use;
		b2 = mi->m_csr_map_1[socket][branch][map].bank2_use;

		/*
		 * set the corresponding bit in dimm, rank, bank based
		 * on map settings.
		 */
		use_bit_check(mi, socket, branch, map, &dimm, &rank, &bank);

		/*
		 * Now begin to guess the dimm, rank, bank number.
		 * We'll skip a loop once the guessing number has
		 * conflict with the map settings.
		 */
		for (tdimm = 0; tdimm <= 7; tdimm++) {
			xor_dimm = tdimm;
			if ((d2_use == 0) && ((tdimm & 4) != 0)) {
				continue;
			}
			if ((d1_use == 0) && ((tdimm & 2) != 0)) {
				continue;
			}
			if ((d0_use == 0) && ((tdimm & 1) != 0)) {
				continue;
			}
			for (trank = 0; trank <= 3; trank++) {
				xor_rank = trank;
				if (srank0 != BIT_USE) {
					if ((xor_rank & 1) != srank0)
						continue;
				}
				if (srank1 != BIT_USE) {
					if (((xor_rank >> 1) & 1) != srank1)
						continue;
				}
				for (tbank = 0; tbank <= 7; tbank++) {
					xor_bank = tbank;
					if (b3 != BIT_USE) {
						if (((xor_bank >> 3)
						    & 1) != b3)
							continue;
					}
					if (b2 != BIT_USE) {
						if (((xor_bank >> 2)
						    & 1) != b2)
							continue;
					}
					/*
					 * calculate the address based on
					 * current guess result
					 */
					addr = get_addr(socket, branch, map,
					    mi, xor_dimm, xor_rank, xor_bank,
					    col, row);

					xor_convert(socket, branch, addr, mi,
					    &xor_dimm, &xor_bank, &xor_rank);

					INHMEX_DEBUG(CE_NOTE, "tdimm=%x,\
					    trank=%x,tbank=%llx,xdimm=%x,\
					    xrank=%x,xbank=%llx,dimm=%x,\
					    rank=%x,bank=%llx\n", tdimm, trank,
					    (long long)tbank, xor_dimm,
					    xor_rank, (long long)xor_bank,
					    dimm, rank, (long long)bank);
					/*
					 * If xored results match the
					 * input values, we hit.
					 */
					if ((xor_dimm == dimm) &&
					    (xor_bank == bank) &&
					    (xor_rank == rank)) {
						solved_xor = 1;
						break;
					}
				}
				if (solved_xor)
					break;
			}
			if (solved_xor)
				break;
		}
		if (solved_xor)
			break;
	}

	if (solved_xor)
		raddr = addr;
	else {
		raddr = EX_MEM_TRANS_ERR;
		INHMEX_DEBUG(CE_NOTE, "solved_xor = 0!\n");
	}

	INHMEX_DEBUG(CE_NOTE, "Mbox Addr:%llx\n", (long long)raddr);
	return (raddr);
}

void
nhmex_memtrans_fini(void)
{
	int i;

	ASSERT(RW_LOCK_HELD(&inhmex_mc_lock));
	if (nhmex_gclp == NULL)
		return;

	for (i = 0; i < NHMEX_MAX_CLUMP_NUMBER; i++) {
		if (nhmex_gclp[i] != NULL) {
			if (nhmex_gclp[i]->mem_info != NULL) {
				kmem_free(nhmex_gclp[i]->mem_info,
				    sizeof (nhmex_mem_info_t));
			}
			kmem_free(nhmex_gclp[i], sizeof (nhmex_clump_topo_t));
		}
	}
	kmem_free(nhmex_gclp, NHMEX_MAX_CLUMP_NUMBER * NHMEX_MAX_CPU_NUMBER *
	    sizeof (nhmex_clump_topo_t *));
	nhmex_gclp = NULL;
}

/*ARGSUSED*/
cmi_errno_t
nhmex_unumtopa_i(void *arg, mc_unum_t *unump, nvlist_t *nvl, uint64_t *pap)
{
	uint16_t clump, socket, branch, ddrch, dimm, rank;
	uint64_t maddr, sys_addr, bank, row, col;
	nvlist_t **hcl, *hcsp;
	uint64_t rank_addr, pa;
	uint_t npr;
	long v;
	int i;
	char *hcnm, *hcid;

	if (unump == NULL) {
		if (nvlist_lookup_nvlist(nvl, FM_FMRI_HC_SPECIFIC, &hcsp) != 0)
			return (CMIERR_UNKNOWN);
		if (nvlist_lookup_uint64(hcsp,
		    "asru-" FM_FMRI_HC_SPECIFIC_OFFSET, &rank_addr) != 0 &&
		    nvlist_lookup_uint64(hcsp, FM_FMRI_HC_SPECIFIC_OFFSET,
		    &rank_addr) != 0) {
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
		clump = 0xffff;
		socket = 0xffff;
		branch = 0xffff;
		ddrch = 0xffff;
		dimm = 0xffff;
		rank = 0xffff;
		for (i = 0; i < npr; i++) {
			if (nvlist_lookup_string(hcl[i], FM_FMRI_HC_NAME,
			    &hcnm) != 0 ||
			    nvlist_lookup_string(hcl[i], FM_FMRI_HC_ID,
			    &hcid) != 0 ||
			    ddi_strtol(hcid, NULL, 0, &v) != 0)
				return (CMIERR_UNKNOWN);
			if (strcmp(hcnm, "motherboard") == 0)
				clump = (int)v;
			else if (strcmp(hcnm, "chip") == 0)
				socket = (int)v;
			else if (strcmp(hcnm, "memory-controller") == 0)
				branch = (int)v;
			else if (strcmp(hcnm, "dimm") == 0) {
				dimm = (int)v & 1;
				ddrch = (int)v >> 1;
			} else if (strcmp(hcnm, "rank") == 0)
				rank = (int)v & 3;
		}
		if (clump == 0xffff || socket == 0xffff || branch == 0xffff ||
		    dimm == 0xffff || rank == 0xffff)
			return (CMIERR_UNKNOWN);
		row = TCODE_OFFSET_RAS(rank_addr);
		bank = TCODE_OFFSET_BANK(rank_addr);
		col =  TCODE_OFFSET_CAS(rank_addr);
	} else {
		clump = unump->unum_board;
		socket = unump->unum_chip;
		branch = unump->unum_mc;
		dimm = unump->unum_cs & 1;
		ddrch = unump->unum_cs >> 1;
		rank = unump->unum_rank & 0x3;
		row = TCODE_OFFSET_RAS(unump->unum_offset);
		bank = TCODE_OFFSET_BANK(unump->unum_offset);
		col = TCODE_OFFSET_CAS(unump->unum_offset);
	}
	INHMEX_DEBUG(CE_NOTE, "socket%d branch%d dimm%x rank%x bank%llx \
	    row%llx col%llx.\n",  socket, branch, dimm, rank,
	    (long long)bank, (long long)row, (long long)col);

	if (nhmex_gclp[clump]->mem_info == NULL) {
		return (CMIERR_UNKNOWN);
	}
	maddr = dimm_to_address(socket, branch, ddrch, dimm, rank, bank,
	    row, col, nhmex_gclp[clump]->mem_info);
	if (maddr == EX_MEM_TRANS_ERR) {
		INHMEX_DEBUG(CE_NOTE, "maddr returns 0xffffffff\n");
		return (CMIERR_UNKNOWN);
	}
	sys_addr = mboxaddr_to_sysaddr(maddr,
	    nhmex_gclp[clump]->mem_info, socket, branch);

	if (sys_addr == EX_MEM_TRANS_ERR) {
		INHMEX_DEBUG(CE_NOTE, "sys_addr returns 0xffffffff\n");
		return (CMIERR_UNKNOWN);
	}
	*pap = sys_addr;
	INHMEX_DEBUG(CE_NOTE, "unumtopa - phaddr = %llx\n", (long long)*pap);

	return (CMI_SUCCESS);
}

cmi_errno_t
nhmex_unumtopa(void *arg, mc_unum_t *unump, nvlist_t *nvl, uint64_t *pap)
{
	cmi_errno_t rt;

	/*
	 * We need to hold the lock when calling from outside this driver
	 */
	rw_enter(&inhmex_mc_lock, RW_READER);
	rt = nhmex_unumtopa_i(arg, unump, nvl, pap);
	rw_exit(&inhmex_mc_lock);
	return (rt);
}

/*ARGSUSED*/
cmi_errno_t
nhmex_patounum_i(void *arg, uint64_t pa, uint8_t valid_hi, uint8_t valid_lo,
    uint32_t synd, int syndtype, mc_unum_t *unump)
{
	uint16_t clumpid = 0, socket = 0, branch = 0, ddrch = 0, fbdch = 0,
	    dimm = 0, rank = 0, map = 0;
	uint64_t sys_addr, maddr = 0, bank = 0, row = 0, col = 0, rt = 0;

	sys_addr = pa;
	socket = address_to_socket(sys_addr, &branch, &clumpid, NULL);
	if (socket == 0xffff) {
		return (CMIERR_UNKNOWN);
	}

	INHMEX_DEBUG(CE_NOTE, "-10-clumpid = %x, socket = %x, branch = %x\n",
	    clumpid, socket, branch);

	maddr = address_to_mbox(sys_addr, nhmex_gclp[clumpid]->mem_info,
	    socket, branch, &map);

	INHMEX_DEBUG(CE_NOTE, "-11-map = %x\n", map);

	if (maddr == EX_MEM_TRANS_ERR) {
		return (CMIERR_UNKNOWN);
	}

	rt = address_to_dimm(maddr, socket, branch, map,
	    nhmex_gclp[clumpid]->mem_info, &ddrch, &fbdch, &dimm,
	    &rank, &bank, &row, &col);

	if (rt == EX_MEM_TRANS_ERR)
		return (CMIERR_UNKNOWN);

	unump->unum_board = clumpid;
	unump->unum_chip = socket;
	unump->unum_mc = branch;
	unump->unum_chan = fbdch;
	unump->unum_cs = (ddrch << 1) | dimm;
	unump->unum_rank = (unump->unum_cs << 2) | rank;
	unump->unum_offset = TCODE_OFFSET(rank, bank, row, col);

	INHMEX_DEBUG(CE_NOTE, "patoumun - clump %d socket %d branch %d \
	    fbdch %d dimm %x rank %x bank %llx row %llx col %llx\n",
	    clumpid, socket, branch, fbdch, unump->unum_cs, rank,
	    (long long)bank, (long long)row, (long long)col);
	return (CMI_SUCCESS);
}

cmi_errno_t
nhmex_patounum(void * arg, uint64_t pa, uint8_t valid_hi, uint8_t valid_lo,
uint32_t synd, int syndtype, mc_unum_t *unump)
{
	cmi_errno_t rt;

	/*
	 * We need to hold the lock when calling from outside this driver
	 */
	rw_enter(&inhmex_mc_lock, RW_READER);
	rt = nhmex_patounum_i(arg, pa, valid_hi, valid_lo,
	    synd, syndtype, unump);
	rw_exit(&inhmex_mc_lock);
	return (rt);
}
