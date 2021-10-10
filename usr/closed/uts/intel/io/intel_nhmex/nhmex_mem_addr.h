
/*
 * INTEL CONFIDENTIAL
 * SPECIAL INTEL MODIFICATIONS
 *
 * Copyright (c) 2009,  Intel Corporation.
 * All Rights Reserved.
 */

#ifndef	_NHMEXMTRANS_H
#define	_NHMEXMTRANS_H

#ifdef __cplusplus
extern "C" {
#endif

#include <sys/mc_intel.h>

#define	NHMEX_DECODER_DRAM	0
#define	NHMEX_MAX_CPU_NUMBER	8
#define	NHMEX_MAX_CLUMP_NUMBER	0x20
#define	NHMEX_MAX_BRANCH_PER_SOCKET	2
#define	NHMEX_MAX_SAD_ENTRY	20
#define	NHMEX_MAX_TAD_ENTRY	8
#define	NHMEX_MAX_MAP_ENTRY	3
#define	NHMEX_MAX_MAP_NUMBER	4
#define	NHMEX_MAX_TARGET_LIST	8
#define	NHMEX_MAX_FBD_CHANNEL	4
#define	NHMEX_MAX_FBD_DIMM	2
#define	NHMEX_MAX_DDR_CH	2
#define	NHMEX_MAX_DDR_CS	8
#define	NHMEX_TAD_RD	1
#define	NHMEX_TAD_WR	2
#define	NHMEX_TAD_TBL_TAD_PAYLOAD	0
#define	NHMEX_TAD_TBL_TAD_LIMIT	1
#define	NHMEX_TAD_TBL_MAP_LIMIT	2
#define	NHMEX_MAX_PHYS_DIMM_MAP	2

/* SAD decoder */
#define	NHMEX_SAD_PRIMARY_DRAM	0
#define	NHMEX_SAD_IO_LARGE	1

extern uint32_t
nhmex_pci_getl(int bus, int dev, int func, int reg, int *interpose);

extern void
nhmex_pci_putl(int bus, int dev, int func, int reg, uint32_t val);

#define	EX_MEM_TRANS_ERR	-1ULL
#define	REG_PCSR_IOMMEN	0xe0
#define	REG_PCSR_SADCAMARY	0x80
#define	REG_PCSR_SADARYID	0xf0
#define	REG_PCSR_SADPLDARY	0xf4
#define	REG_PCSR_SADPRMARY	0xf8
#define	REG_PCSR_TAD_CTL	0x70
#define	REG_PCSR_TAD_RDDATA	0x74
#define	REG_PCSR_MAP0		0xe0
#define	REG_PCSR_MAP1		0xf0
#define	REG_PCSR_MAP_OPEN_CLOSED	0x40
#define	REG_PCSR_MAP_PHYS_DIMM_0	0x44
#define	REG_PCSR_MAP_XORING	0x4c
#define	REG_PCSR_MODE	0x58
#define	REG_PCSR_FBD_POP_CTL	0xb8
#define	REG_PCSR_FBD_CMD_0	0x94
#define	REG_PCSR_NB_REG_DATA_0	0x70
#define	NHMEX_DID_VID(bus) \
    nhmex_pci_getl(bus, 0, 0, 0, 0)
#define	NHMEX_IOMMEN(bus) \
	nhmex_pci_getl(bus, 8, 0, REG_PCSR_IOMMEN, 0)
#define	NHMEX_SAD_DRAM_LIMIT_RD(clump_topo, socket, i) \
    nhmex_pci_getl((clump_topo)->bus_number[socket],\
    8, 0, REG_PCSR_SADCAMARY + ((i) * 4), 0)
#define	NHMEX_SAD_DRAM_TGTLST_PICK(clump_topo, socket, eid, did) \
    nhmex_pci_putl((clump_topo)->bus_number[socket],\
    8, 0, REG_PCSR_SADARYID, (eid) | ((did) << 5))
#define	NHMEX_SAD_DRAM_TGTLST_RD(clump_topo, socket) \
    nhmex_pci_getl((clump_topo)->bus_number[socket],\
    8, 0, REG_PCSR_SADPLDARY, 0)
#define	NHMEX_SAD_DRAM_PRMARY_RD(clump_topo, socket) \
    nhmex_pci_getl((clump_topo)->bus_number[socket],\
    8, 0, REG_PCSR_SADPRMARY, 0)
#define	NHMEX_TAD_CTL_WR(clump_topo, socket, j, val) \
    nhmex_pci_putl((clump_topo)->bus_number[socket],\
    4 + (j) * 2, 0, REG_PCSR_TAD_CTL, val)
#define	NHMEX_TAD_RDDATA_RD(clump_topo, socket, j) \
    nhmex_pci_getl((clump_topo)->bus_number[socket],\
    4 + (j) * 2, 0, REG_PCSR_TAD_RDDATA, 0)
#define	NHMEX_MAP_1_RD(clump_topo, socket, branch, id) \
    nhmex_pci_getl((clump_topo)->bus_number[socket],\
    5 + (branch) * 2, 0, REG_PCSR_MAP1 + (4 * (id)), 0)
#define	NHMEX_MAP_0_RD(clump_topo, socket, branch, id) \
    nhmex_pci_getl((clump_topo)->bus_number[socket],\
    5 + (branch) * 2, 0, REG_PCSR_MAP0 + (4 * (id)), 0)
#define	NHMEX_MAP_OPEN_RD(clump_topo, socket, branch) \
    nhmex_pci_getl((clump_topo)->bus_number[socket],\
    5 + (branch) * 2, 2, REG_PCSR_MAP_OPEN_CLOSED, 0)
#define	NHMEX_MAP_PHYS_DIMM_RD(clump_topo, socket, branch, id) \
    nhmex_pci_getl((clump_topo)->bus_number[socket], \
    5 + (branch) * 2, 2, REG_PCSR_MAP_PHYS_DIMM_0 + (4 * (id)), 0)
#define	NHMEX_MAP_XORING_RD(clump_topo, socket, branch) \
    nhmex_pci_getl((clump_topo)->bus_number[socket], \
    5 + (branch) * 2, 2, REG_PCSR_MAP_XORING, 0)
#define	NHMEX_PCSR_MODE_RD(clump_topo, socket, branch) \
    nhmex_pci_getl((clump_topo)->bus_number[socket], \
    4 + (branch) * 2, 0, REG_PCSR_MODE, 0)
#define	NHMEX_PCSR_POPCTL_RD(clump_topo, socket, branch) \
    nhmex_pci_getl((clump_topo)->bus_number[socket], \
    5 + (branch) * 2, 0, REG_PCSR_FBD_POP_CTL, 0)

/* FBD CMD related definitions */
#define	FBD_SCMD_CFG_RD	0x1d
#define	FBD_SCMD_NOP	0xc
#define	FBD_OP_CFG_RD	0x4

#define	BIT_ZERO	0
#define	RESERVED	1
#define	BIT_USE		2
#define	BIT_ONE		3

#define	CSR_FBD_CMD_WR(socket, branch, i, cmd)	   \
    nhmex_pci_putl((clump_topo)->bus_number[socket],\
    5 + ((branch) * 2), 0, REG_PCSR_FBD_CMD_0 + (i) * 4, cmd)

#define	CSR_FBD_CMD_RD(socket, branch, i) \
    nhmex_pci_getl((clump_topo)->bus_number[socket],\
    5 + ((branch) * 2), 0, REG_PCSR_FBD_CMD_0 + (i) * 4, 0)

#define	CSR_NB_REG_DATA0_RD(socket, branch) \
    nhmex_pci_getl((clump_topo)->bus_number[socket],\
    5 + ((branch) * 2), 2, REG_PCSR_NB_REG_DATA_0, 0)

/* MillBrook register definition */
#define	MB_FUC3	3
#define	MB_CSCTL_REG	0x64
#define	MB_CSMASK_REG	0x68

/* XOR operation */
#define	XOR_FORWARD	1
#define	XOR_REVERSE	2

typedef struct sad_ary {
	uint64_t limit;
	uint16_t tgt[NHMEX_MAX_TARGET_LIST];
	uint_t attr;
	uint_t tgtsel;
	uint_t hemi;
	uint_t idbase;
	uint_t eid;
	uint_t did;
	uint_t ways;
} sad_ary_t;

typedef struct tad_ary {
	uint64_t tad_limit;	/* 16b, system addr[43:28] */
	uint16_t offset;	/* 12b */
	uint16_t ways;
	uint16_t shift;
} tad_ary_t;

typedef struct map_0 {
	uint_t c13_use;
	uint_t c12_use;
	uint_t c11_use;
	uint_t c10_use;
	uint_t c2_use;
	uint_t r15_use;
	uint_t r14_use;
	uint_t col;
	uint_t row;
} map_0_t;

typedef struct map_1 {
	uint_t bank3_use;
	uint_t bank2_use;
	uint_t bank;
	uint_t srank1_use;
	uint_t srank0_use;
	uint_t stacked_rank;
	uint_t dimm2_use;
	uint_t dimm1_use;
	uint_t dimm0_use;
	uint_t dimm;
	uint_t dimm_spc;
	uint_t rank2x;
} map_1_t;

typedef struct phy_dimm_map {
	uint_t pdmap[NHMEX_MAX_MAP_NUMBER]; /* contain map number */
	uint_t pdimm[NHMEX_MAX_MAP_NUMBER]; /* contain logical dimm number */
} phy_dimm_map_t;

typedef struct mb_csctl {
	uint_t enable;
	uint_t id;
	uint_t mask;
} mb_csctl_t;

typedef struct nhmex_mem_info {
	sad_ary_t sad_array[NHMEX_MAX_SAD_ENTRY];
	tad_ary_t tad_array[NHMEX_MAX_CPU_NUMBER][NHMEX_MAX_BRANCH_PER_SOCKET]
	    [NHMEX_MAX_TAD_ENTRY];
	uint64_t map_limit[NHMEX_MAX_CPU_NUMBER][NHMEX_MAX_BRANCH_PER_SOCKET]
	    [NHMEX_MAX_MAP_ENTRY];
	map_0_t m_csr_map_0[NHMEX_MAX_CPU_NUMBER][NHMEX_MAX_BRANCH_PER_SOCKET]
	    [NHMEX_MAX_MAP_NUMBER];
	map_1_t m_csr_map_1[NHMEX_MAX_CPU_NUMBER][NHMEX_MAX_BRANCH_PER_SOCKET]
	    [NHMEX_MAX_MAP_NUMBER];
	int open_map[NHMEX_MAX_CPU_NUMBER][NHMEX_MAX_BRANCH_PER_SOCKET];
	phy_dimm_map_t phydmap[NHMEX_MAX_CPU_NUMBER]
	    [NHMEX_MAX_BRANCH_PER_SOCKET][NHMEX_MAX_PHYS_DIMM_MAP];
	mb_csctl_t csctl[NHMEX_MAX_CPU_NUMBER][NHMEX_MAX_FBD_CHANNEL]
	    [NHMEX_MAX_DDR_CH][NHMEX_MAX_DDR_CS];
	uint32_t fbd_popctl[NHMEX_MAX_CPU_NUMBER][NHMEX_MAX_BRANCH_PER_SOCKET];
	uint8_t nodeid[NHMEX_MAX_CPU_NUMBER][NHMEX_MAX_BRANCH_PER_SOCKET];
	uint32_t xor_map[NHMEX_MAX_CPU_NUMBER][NHMEX_MAX_BRANCH_PER_SOCKET];
} nhmex_mem_info_t;

typedef struct nhmex_clump_topo {
	uint8_t clump_id;
	uint8_t sca_mask;
	uint8_t sca_ena;
	int socket_number;
	uint8_t bus_number[NHMEX_MAX_CPU_NUMBER];
	nhmex_mem_info_t *mem_info;
} nhmex_clump_topo_t;

#ifdef __cplusplus
}
#endif

#endif	/* _NHMEXMTRANS_H */
