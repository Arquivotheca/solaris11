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

#ifndef _NHMEX_LOG_H
#define	_NHMEX_LOG_H

#ifdef __cplusplus
extern "C" {
#endif

#include <sys/cpu_module.h>

typedef struct nhmex_dimm {
	uint64_t dimm_size;
	uint8_t nranks;
	uint8_t nbanks;
	uint8_t ncolumn;
	uint8_t nrow;
	uint8_t width;
	char manufacturer[64];
	char serial_number[64];
	char part_number[16];
	char revision[2];
	char label[64];
} nhmex_dimm_t;

extern nhmex_dimm_t **nhmex_dimms;
extern uint32_t nhmex_chipset;

extern errorq_t *nhmex_queue;
extern kmutex_t nhmex_mutex;

extern int nhmex_init(void);
extern int nhmex_dev_init(void);
extern void nhmex_dev_reinit(void);
extern void nhmex_unload(void);
extern void nhmex_dev_unload(void);

extern int inhmex_mc_register(cmi_hdl_t, void *, void *, void *);
extern void nhmex_scrubber_enable(void);
extern void nhmex_error_trap(cmi_hdl_t, boolean_t, boolean_t);

extern void nhmex_pci_cfg_setup(dev_info_t *);
extern void nhmex_pci_cfg_free(void);
extern uint8_t nhmex_pci_getb(int, int, int, int, int *);
extern uint16_t nhmex_pci_getw(int, int, int, int, int *);
extern uint32_t nhmex_pci_getl(int, int, int, int, int *);
extern void nhmex_pci_putb(int, int, int, int, uint8_t);
extern void nhmex_pci_putw(int, int, int, int, uint16_t);
extern void nhmex_pci_putl(int, int, int, int, uint32_t);
extern int nhmex_mb_rd(int, int, int, int, int, int, uint32_t *);

extern cmi_errno_t nhmex_patounum(void *, uint64_t, uint8_t, uint8_t, uint32_t,
    int, mc_unum_t *);
extern cmi_errno_t nhmex_unumtopa(void *, mc_unum_t *, nvlist_t *, uint64_t *);
extern cmi_errno_t nhmex_patounum_i(void *, uint64_t, uint8_t, uint8_t,
    uint32_t, int, mc_unum_t *);
extern cmi_errno_t nhmex_unumtopa_i(void *, mc_unum_t *, nvlist_t *,
    uint64_t *);
extern uint32_t nhmex_memtrans_init(void);
extern void nhmex_memtrans_fini(void);

#ifdef __cplusplus
}
#endif

#endif /* _NHMEX_LOG_H */
