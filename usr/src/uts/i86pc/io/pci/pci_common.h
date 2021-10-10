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
 * Copyright (c) 2005, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#ifndef	_PCI_PCI_COMMON_H
#define	_PCI_PCI_COMMON_H

#ifdef	__cplusplus
extern "C" {
#endif

/*
 *	Common header file with definitions shared between
 *	pci(7d) and npe(7d)
 */

/* State structure. */
typedef struct pci_state {
	dev_info_t *pci_dip;
	int pci_fmcap;
	uint_t pci_soft_state;
	ddi_iblock_cookie_t pci_fm_ibc;
	kmutex_t pci_mutex;
	kmutex_t pci_peek_poke_mutex;
	kmutex_t pci_err_mutex;
} pci_state_t;

/*
 * These are the access routines.
 * The pci_bus_map sets the handle to point to these in pci(7d).
 * The npe_bus_map sets the handle to point to these in npe(7d).
 */
uint8_t		pci_config_rd8(ddi_acc_impl_t *hdlp, uint8_t *addr);
uint16_t	pci_config_rd16(ddi_acc_impl_t *hdlp, uint16_t *addr);
uint32_t	pci_config_rd32(ddi_acc_impl_t *hdlp, uint32_t *addr);
uint64_t	pci_config_rd64(ddi_acc_impl_t *hdlp, uint64_t *addr);

void		pci_config_wr8(ddi_acc_impl_t *hdlp, uint8_t *addr,
		    uint8_t value);
void		pci_config_wr16(ddi_acc_impl_t *hdlp, uint16_t *addr,
		    uint16_t value);
void		pci_config_wr32(ddi_acc_impl_t *hdlp, uint32_t *addr,
		    uint32_t value);
void		pci_config_wr64(ddi_acc_impl_t *hdlp, uint64_t *addr,
		    uint64_t value);

void		pci_config_rep_rd8(ddi_acc_impl_t *hdlp, uint8_t *host_addr,
		    uint8_t *dev_addr, size_t repcount, uint_t flags);
void		pci_config_rep_rd16(ddi_acc_impl_t *hdlp, uint16_t *host_addr,
		    uint16_t *dev_addr, size_t repcount, uint_t flags);
void		pci_config_rep_rd32(ddi_acc_impl_t *hdlp, uint32_t *host_addr,
		    uint32_t *dev_addr, size_t repcount, uint_t flags);
void		pci_config_rep_rd64(ddi_acc_impl_t *hdlp, uint64_t *host_addr,
		    uint64_t *dev_addr, size_t repcount, uint_t flags);

void		pci_config_rep_wr8(ddi_acc_impl_t *hdlp, uint8_t *host_addr,
		    uint8_t *dev_addr, size_t repcount, uint_t flags);
void		pci_config_rep_wr16(ddi_acc_impl_t *hdlp, uint16_t *host_addr,
		    uint16_t *dev_addr, size_t repcount, uint_t flags);
void		pci_config_rep_wr32(ddi_acc_impl_t *hdlp, uint32_t *host_addr,
		    uint32_t *dev_addr, size_t repcount, uint_t flags);
void		pci_config_rep_wr64(ddi_acc_impl_t *hdlp, uint64_t *host_addr,
		    uint64_t *dev_addr, size_t repcount, uint_t flags);

/*
 * PCI tool related declarations
 */
int	pci_common_ioctl(dev_info_t *dip, dev_t dev, int cmd,
	    intptr_t arg, int mode, cred_t *credp, int *rvalp);

/*
 * Interrupt related declaration
 */
int	pci_common_intr_ops(dev_info_t *, dev_info_t *, ddi_intr_op_t,
	    ddi_intr_handle_impl_t *, void *);
void	pci_common_set_parent_private_data(dev_info_t *);

/*
 * Miscellaneous library functions
 */
int	pci_common_get_reg_prop(dev_info_t *dip, pci_regspec_t *pci_rp);
int	pci_common_name_child(dev_info_t *child, char *name, int namelen);
int	pci_common_peekpoke(dev_info_t *dip, dev_info_t *rdip,
	ddi_ctl_enum_t ctlop, void *arg, void *result);
int	pci_fm_acc_setup(ddi_acc_hdl_t *hp, off_t offset, off_t len);

/*
 * DDI DMA functions. Used in both pci(7d) and npe(7d) to
 * use an IOMMU, if present.
 */
int	pci_dma_allochdl(dev_info_t *, dev_info_t *, ddi_dma_attr_t *,
	    int (*)(caddr_t), caddr_t, ddi_dma_handle_t *);
int	pci_dma_freehdl(dev_info_t *, dev_info_t *, ddi_dma_handle_t);
int	pci_dma_bindhdl(dev_info_t *, dev_info_t *, ddi_dma_handle_t,
	    struct ddi_dma_req *, ddi_dma_cookie_t *, uint_t *);
int	pci_dma_unbindhdl(dev_info_t *, dev_info_t *, ddi_dma_handle_t);
int	pci_dma_win(dev_info_t *, dev_info_t *, ddi_dma_handle_t, uint_t,
	    off_t *, size_t *, ddi_dma_cookie_t *, uint_t *);
int	pci_dma_map(dev_info_t *dip, dev_info_t *rdip,
	    struct ddi_dma_req *dmareqp, ddi_dma_handle_t *handlep);
int	pci_dma_mctl(dev_info_t *dip, dev_info_t *rdip, ddi_dma_handle_t handle,
	    enum ddi_dma_ctlops request, off_t *offp, size_t *lenp,
	    caddr_t *objp, uint_t flags);
int	pci_dma_flush(dev_info_t *dip, dev_info_t *rdip,
	    ddi_dma_handle_t handle, off_t off, size_t len,
	    uint_t cache_flags);

#ifdef	__cplusplus
}
#endif

#endif	/* _PCI_PCI_COMMON_H */
