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
 * Copyright 2010 QLogic Corporation. All rights reserved.
 */

#ifndef _QLCNIC_HW_
#define	_QLCNIC_HW_

#ifdef __cplusplus
extern "C" {
#endif

#include "qlcnic_inc.h"

#define	NXHAL_VERSION	1

/* Hardware memory size of 128 meg */
#define	BAR0_SIZE (128 * 1024 * 1024)
/*
 * It can be calculated by looking at the first 1 bit of the BAR0 addr after
 * bit 4 For us lets assume that BAR0 is D8000008, then the size is 0x8000000,
 * 8 represents first bit containing 1.   FSL temp notes....pg 162 of PCI
 * systems arch...
 */

#define	QLCNIC_HW_BLOCK_WRITE_64(DATA_PTR, ADDR, NUM_WORDS)        \
{                                                           \
	int i;                                              \
	u64 *a = (u64 *) (DATA_PTR);                        \
	u64 *b = (u64 *) (ADDR);                            \
	u64 tmp;					    \
	for (i = 0; i < (NUM_WORDS); i++, a++, b++) {       \
		tmp = QLCNIC_PCI_READ_64(a);		    \
		QLCNIC_PCI_WRITE_64(tmp, b);		    \
	}						    \
}

#define	QLCNIC_HW_BLOCK_READ_64(DATA_PTR, ADDR, NUM_WORDS)           \
{                                                             \
	int i;                                                \
	u64 *a = (u64 *) (DATA_PTR);                          \
	u64 *b = (u64 *) (ADDR);                              \
	u64 tmp;					      \
	for (i = 0; i < (NUM_WORDS); i++, a++, b++) {            \
		tmp = QLCNIC_PCI_READ_64(b);		      \
		QLCNIC_PCI_WRITE_64(tmp, a);		      \
	}                                                     \
}

#define	QLCNIC_PCI_MAPSIZE_BYTES  (QLCNIC_PCI_MAPSIZE << 20)

#define	QLCNIC_LOCKED_READ_REG(X, Y)   \
	addr = (void *)(uptr_t)(pci_base_offset(adapter, (X)));     \
	*(uint32_t *)(Y) = QLCNIC_PCI_READ_32(addr);

#define	QLCNIC_LOCKED_WRITE_REG(X, Y)   \
	addr = (void *)(uptr_t)(pci_base_offset(adapter, (X))); \
	QLCNIC_PCI_WRITE_32(*(uint32_t *)(Y), addr);

/* For Multicard support */
#define	QLCNIC_CRB_READ_VAL_ADAPTER(ADDR, ADAPTER) \
	qlcnic_crb_read_val_adapter((ADDR), (struct qlcnic_adapter_s *)ADAPTER)

#define	QLCNIC_CRB_READ_CHECK_ADAPTER(ADDR, VALUE, ADAPTER)		\
	{								\
		if (qlcnic_crb_read_adapter(ADDR, VALUE,		\
		    (struct qlcnic_adapter_s *)ADAPTER)) return -1;	\
	}

#define	QLCNIC_CRB_WRITELIT_ADAPTER(ADDR, VALUE, ADAPTER)		\
	{								\
		adapter->qlcnic_crb_writelit_adapter(			\
		    (struct qlcnic_adapter_s *)ADAPTER,			\
		    ADDR, (int)VALUE);			\
	}

struct qlcnic_adapter_s;
void qlcnic_set_link_parameters(struct qlcnic_adapter_s *adapter);
long xge_mdio_init(struct qlcnic_adapter_s *adapter);
void qlcnic_flash_print(struct qlcnic_adapter_s *adapter);
void qlcnic_get_serial_num(struct qlcnic_adapter_s *adapter);

typedef struct {
	unsigned valid;
	unsigned start_128M;
	unsigned end_128M;
	unsigned start_2M;
} crb_128M_2M_sub_block_map_t;

typedef struct {
	crb_128M_2M_sub_block_map_t sub_block[16];
} crb_128M_2M_block_map_t;

#ifdef __cplusplus
}
#endif

#endif /* _QLCNIC_HW_ */
