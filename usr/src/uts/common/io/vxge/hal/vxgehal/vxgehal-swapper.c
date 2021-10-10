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
 * Copyright (c) 2010, Oracle and/or its affiliates. All rights reserved.
 */
/*
 * Copyright Exar 2010. Copyright (c) 2002-2010 Neterion, Inc.
 * All right Reserved.
 *
 * FileName :   vxgehal-swapper.c
 *
 * Description:  Swapper control routines
 *
 * Created:       26 July 2007
 */

#include "vxgehal.h"

/*
 * _hal_legacy_swapper_set - Set the swapper bits for the legacy secion.
 * @pdev: PCI device object.
 * @regh: BAR0 mapped memory handle (Solaris), or simply PCI device @pdev
 *	(Linux and the rest.)
 * @legacy_reg: Address of the legacy register space.
 *
 * Set the swapper bits appropriately for the lagacy section.
 *
 * Returns:  VXGE_HAL_OK - success.
 * VXGE_HAL_ERR_SWAPPER_CTRL - failed.
 *
 * See also: vxge_hal_status_e {}.
 */
/*ARGSUSED*/
vxge_hal_status_e
__vxge_hal_legacy_swapper_set(
			pci_dev_h		pdev,
			pci_reg_h		regh,
			vxge_hal_legacy_reg_t	*legacy_reg)
{
	u64 val64;
	vxge_hal_status_e status;

	vxge_assert(legacy_reg != NULL);

	vxge_hal_trace_log_driver(NULL_HLDEV, NULL_VPID,
	    "==>  %s:%s:%d", __FILE__, __func__, __LINE__);

	vxge_hal_trace_log_driver(NULL_HLDEV, NULL_VPID,
	    "pdev = 0x"VXGE_OS_STXFMT", regh = 0x"VXGE_OS_STXFMT", "
	    "legacy_reg = 0x"VXGE_OS_STXFMT, (ptr_t)pdev, (ptr_t)regh,
	    (ptr_t)legacy_reg);

	val64 = vxge_os_pio_mem_read64(pdev, regh, &legacy_reg->toc_swapper_fb);

	vxge_hal_info_log_driver(NULL_HLDEV, NULL_VPID,
	    "TOC Swapper Fb: 0x"VXGE_OS_LLXFMT, val64);

	vxge_os_wmb();

	switch (val64) {

	case VXGE_HAL_SWAPPER_INITIAL_VALUE:
		return (VXGE_HAL_OK);

	case VXGE_HAL_SWAPPER_BYTE_SWAPPED_BIT_FLIPPED:
		vxge_os_pio_mem_write64(pdev, regh,
		    VXGE_HAL_SWAPPER_READ_BYTE_SWAP_ENABLE,
		    &legacy_reg->pifm_rd_swap_en);
		vxge_os_pio_mem_write64(pdev, regh,
		    VXGE_HAL_SWAPPER_READ_BIT_FLAP_ENABLE,
		    &legacy_reg->pifm_rd_flip_en);
		vxge_os_pio_mem_write64(pdev, regh,
		    VXGE_HAL_SWAPPER_WRITE_BYTE_SWAP_ENABLE,
		    &legacy_reg->pifm_wr_swap_en);
		vxge_os_pio_mem_write64(pdev, regh,
		    VXGE_HAL_SWAPPER_WRITE_BIT_FLAP_ENABLE,
		    &legacy_reg->pifm_wr_flip_en);
		break;

	case VXGE_HAL_SWAPPER_BYTE_SWAPPED:
		vxge_os_pio_mem_write64(pdev, regh,
		    VXGE_HAL_SWAPPER_READ_BYTE_SWAP_ENABLE,
		    &legacy_reg->pifm_rd_swap_en);
		vxge_os_pio_mem_write64(pdev, regh,
		    VXGE_HAL_SWAPPER_WRITE_BYTE_SWAP_ENABLE,
		    &legacy_reg->pifm_wr_swap_en);
		break;

	case VXGE_HAL_SWAPPER_BIT_FLIPPED:
		vxge_os_pio_mem_write64(pdev, regh,
		    VXGE_HAL_SWAPPER_READ_BIT_FLAP_ENABLE,
		    &legacy_reg->pifm_rd_flip_en);
		vxge_os_pio_mem_write64(pdev, regh,
		    VXGE_HAL_SWAPPER_WRITE_BIT_FLAP_ENABLE,
		    &legacy_reg->pifm_wr_flip_en);
		break;

	}

	vxge_os_wmb();

	val64 = vxge_os_pio_mem_read64(pdev, regh, &legacy_reg->toc_swapper_fb);

	if (val64 == VXGE_HAL_SWAPPER_INITIAL_VALUE) {
		status = VXGE_HAL_OK;
	} else {
		vxge_hal_err_log_driver(NULL_HLDEV, NULL_VPID,
		    "%s:TOC Swapper setting failed", __func__);
		status = VXGE_HAL_ERR_SWAPPER_CTRL;
	}

	vxge_hal_info_log_driver(NULL_HLDEV, NULL_VPID,
	    "TOC Swapper Fb: 0x"VXGE_OS_LLXFMT, val64);

	vxge_hal_trace_log_driver(NULL_HLDEV, NULL_VPID,
	    "<==  %s:%s:%d  Result: %d", __FILE__, __func__, __LINE__, status);

	return (status);
}

/*
 * __vxge_hal_vpath_swapper_set - Set the swapper bits for the vpath.
 * @hldev: HAL device object.
 * @vp_id: Vpath Id
 *
 * Set the swapper bits appropriately for the vpath.
 *
 * Returns:  VXGE_HAL_OK - success.
 * VXGE_HAL_ERR_SWAPPER_CTRL - failed.
 *
 * See also: vxge_hal_status_e {}.
 */
vxge_hal_status_e
__vxge_hal_vpath_swapper_set(
			vxge_hal_device_t *hldev,
			u32 vp_id)
{
#if !defined(VXGE_OS_HOST_BIG_ENDIAN)
	u64 val64;
	vxge_hal_vpath_reg_t *vpath_reg;

	vxge_assert(hldev != NULL);

	vxge_hal_trace_log_vpath(hldev, vp_id,
	    "==>  %s:%s:%d", __FILE__, __func__, __LINE__);

	vxge_hal_trace_log_vpath(hldev, vp_id,
	    "hldev = 0x"VXGE_OS_STXFMT", vp_id = %d",
	    (ptr_t)hldev, vp_id);

	vpath_reg = ((__vxge_hal_device_t *)hldev)->vpath_reg[vp_id];

	val64 = vxge_os_pio_mem_read64(hldev->pdev, hldev->regh0,
	    &vpath_reg->vpath_general_cfg1);

	vxge_os_wmb();

	val64 |=
	    // VXGE_HAL_VPATH_GENERAL_CFG1_DATA_BYTE_SWAPEN |
	    VXGE_HAL_VPATH_GENERAL_CFG1_CTL_BYTE_SWAPEN; // |
	    // VXGE_HAL_VPATH_GENERAL_CFG1_MSIX_ADDR_SWAPEN |
	    // VXGE_HAL_VPATH_GENERAL_CFG1_MSIX_DATA_SWAPEN;


	vxge_os_pio_mem_write64(hldev->pdev, hldev->regh0,
	    val64,
	    &vpath_reg->vpath_general_cfg1);
	vxge_os_wmb();


	vxge_hal_trace_log_vpath(hldev, vp_id,
	    "<==  %s:%s:%d  Result: 0", __FILE__, __func__, __LINE__);
#endif
	return (VXGE_HAL_OK);
}


/*
 * __vxge_hal_kdfc_swapper_set - Set the swapper bits for the kdfc.
 * @hldev: HAL device object.
 * @vp_id: Vpath Id
 *
 * Set the swapper bits appropriately for the vpath.
 *
 * Returns:  VXGE_HAL_OK - success.
 * VXGE_HAL_ERR_SWAPPER_CTRL - failed.
 *
 * See also: vxge_hal_status_e {}.
 */
vxge_hal_status_e
__vxge_hal_kdfc_swapper_set(
			vxge_hal_device_t *hldev,
			u32 vp_id)
{
	u64 val64;
	vxge_hal_vpath_reg_t *vpath_reg;
	vxge_hal_legacy_reg_t *legacy_reg;

	vxge_assert(hldev != NULL);

	vxge_hal_trace_log_vpath(hldev, vp_id,
	    "==>  %s:%s:%d", __FILE__, __func__, __LINE__);

	vxge_hal_trace_log_vpath(hldev, vp_id,
	    "hldev = 0x"VXGE_OS_STXFMT", vp_id = %d",
	    (ptr_t)hldev, vp_id);

	vpath_reg = ((__vxge_hal_device_t *)hldev)->vpath_reg[vp_id];
	legacy_reg = ((__vxge_hal_device_t *)hldev)->legacy_reg;

	val64 = vxge_os_pio_mem_read64(hldev->pdev, hldev->regh0,
	    &legacy_reg->pifm_wr_swap_en);

	if (val64 == VXGE_HAL_SWAPPER_WRITE_BYTE_SWAP_ENABLE) {

		val64 = vxge_os_pio_mem_read64(hldev->pdev, hldev->regh0,
		    &vpath_reg->kdfcctl_cfg0);

		vxge_os_wmb();

		val64 |= VXGE_HAL_KDFCCTL_CFG0_BYTE_SWAPEN_FIFO0 |
		    VXGE_HAL_KDFCCTL_CFG0_BYTE_SWAPEN_FIFO1	 |
		    VXGE_HAL_KDFCCTL_CFG0_BYTE_SWAPEN_FIFO2;

		vxge_os_pio_mem_write64(hldev->pdev, hldev->regh0,
		    val64,
		    &vpath_reg->kdfcctl_cfg0);
		vxge_os_wmb();

	}

	vxge_hal_trace_log_vpath(hldev, vp_id,
	    "<==  %s:%s:%d  Result: 0", __FILE__, __func__, __LINE__);

	return (VXGE_HAL_OK);
}
