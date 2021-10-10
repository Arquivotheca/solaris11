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
 * FileName :   vxgehal-mgmtaux.h
 *
 * Description:  management API
 *
 * Created:       08 October 2007
 */

#ifndef	VXGE_HAL_MGMTAUX_H
#define	VXGE_HAL_MGMTAUX_H

__EXTERN_BEGIN_DECLS

/*
 * vxge_hal_aux_about_read - Retrieve and format about info.
 * @devh: HAL device handle.
 * @bufsize: Buffer size.
 * @retbuf: Buffer pointer.
 * @retsize: Size of the result. Cannot be greater than @bufsize.
 *
 * Retrieve about info (using vxge_hal_mgmt_about()) and sprintf it
 * into the provided @retbuf.
 *
 * Returns: VXGE_HAL_OK - success.
 * VXGE_HAL_ERR_INVALID_DEVICE - Device is not valid.
 * VXGE_HAL_ERR_VERSION_CONFLICT - Version it not maching.
 * VXGE_HAL_FAIL - Failed to retrieve the information.
 *
 * See also: vxge_hal_mgmt_about(), vxge_hal_aux_device_dump().
 */
vxge_hal_status_e vxge_hal_aux_about_read(vxge_hal_device_h devh, int bufsize,
			char *retbuf, int *retsize);

/*
 * vxge_hal_aux_driver_config_read - Read Driver configuration.
 * @bufsize: Buffer size.
 * @retbuf: Buffer pointer.
 * @retsize: Size of the result. Cannot be greater than @bufsize.
 *
 * Read driver configuration,
 *
 * Returns: VXGE_HAL_OK - success.
 * VXGE_HAL_ERR_VERSION_CONFLICT - Version it not maching.
 *
 * See also: vxge_hal_aux_device_config_read().
 */
vxge_hal_status_e
vxge_hal_aux_driver_config_read(int bufsize, char *retbuf, int *retsize);

/*
 * vxge_hal_aux_pci_config_read - Retrieve and format PCI Configuration
 * info.
 * @devh: HAL device handle.
 * @bufsize: Buffer size.
 * @retbuf: Buffer pointer.
 * @retsize: Size of the result. Cannot be greater than @bufsize.
 *
 * Retrieve about info (using vxge_hal_mgmt_pci_config()) and sprintf it
 * into the provided @retbuf.
 *
 * Returns: VXGE_HAL_OK - success.
 * VXGE_HAL_ERR_INVALID_DEVICE - Device is not valid.
 * VXGE_HAL_ERR_VERSION_CONFLICT - Version it not maching.
 *
 * See also: vxge_hal_mgmt_pci_config(), vxge_hal_aux_device_dump().
 */
vxge_hal_status_e vxge_hal_aux_pci_config_read(vxge_hal_device_h devh,
						int bufsize,
						char *retbuf,
						int *retsize);

/*
 * vxge_hal_aux_device_config_read - Read device configuration.
 * @devh: HAL device handle.
 * @bufsize: Buffer size.
 * @retbuf: Buffer pointer.
 * @retsize: Size of the result. Cannot be greater than @bufsize.
 *
 * Read device configuration,
 *
 * Returns: VXGE_HAL_OK - success.
 * VXGE_HAL_ERR_INVALID_DEVICE - Device is not valid.
 * VXGE_HAL_ERR_VERSION_CONFLICT - Version it not maching.
 *
 * See also: vxge_hal_aux_driver_config_read().
 */
vxge_hal_status_e vxge_hal_aux_device_config_read(vxge_hal_device_h devh,
				int bufsize, char *retbuf, int *retsize);

/*
 * vxge_hal_aux_bar0_read - Read and format X3100 BAR0 register.
 * @devh: HAL device handle.
 * @offset: Register offset in the BAR0 space.
 * @bufsize: Buffer size.
 * @retbuf: Buffer pointer.
 * @retsize: Size of the result. Cannot be greater than @bufsize.
 *
 * Read X3100 register from BAR0 space. The result is formatted as an
 * ascii string.
 *
 * Returns: VXGE_HAL_OK - success.
 * VXGE_HAL_ERR_OUT_OF_SPACE - Buffer size is very small.
 * VXGE_HAL_ERR_INVALID_DEVICE - Device is not valid.
 * VXGE_HAL_ERR_INVALID_OFFSET - Register offset in the BAR space is not
 * valid.
 *
 * See also: vxge_hal_mgmt_reg_read().
 */
vxge_hal_status_e vxge_hal_aux_bar0_read(vxge_hal_device_h devh,
			unsigned int offset, int bufsize, char *retbuf,
			int *retsize);

/*
 * vxge_hal_aux_bar1_read - Read and format X3100 BAR1 register.
 * @devh: HAL device handle.
 * @offset: Register offset in the BAR1 space.
 * @bufsize: Buffer size.
 * @retbuf: Buffer pointer.
 * @retsize: Size of the result. Cannot be greater than @bufsize.
 *
 * Read X3100 register from BAR1 space. The result is formatted as ascii string
 * Returns: VXGE_HAL_OK - success.
 * VXGE_HAL_ERR_OUT_OF_SPACE - Buffer size is very small.
 * VXGE_HAL_ERR_INVALID_DEVICE - Device is not valid.
 * VXGE_HAL_ERR_INVALID_OFFSET - Register offset in the BAR space is not
 * valid.
 *
 */
vxge_hal_status_e vxge_hal_aux_bar1_read(vxge_hal_device_h devh,
			unsigned int offset, int bufsize, char *retbuf,
			int *retsize);

/*
 * vxge_hal_aux_bar0_write - Write BAR0 register.
 * @devh: HAL device handle.
 * @offset: Register offset in the BAR0 space.
 * @value: Regsister value (to write).
 *
 * Write BAR0 register.
 *
 * Returns: VXGE_HAL_OK - success.
 * VXGE_HAL_ERR_INVALID_DEVICE - Device is not valid.
 * VXGE_HAL_ERR_INVALID_OFFSET - Register offset in the BAR space is not
 * valid.
 *
 * See also: vxge_hal_mgmt_reg_write().
 */
vxge_hal_status_e vxge_hal_aux_bar0_write(vxge_hal_device_h devh,
			unsigned int offset, u64 value);

/*
 * vxge_hal_aux_stats_vpath_hw_read - Read vpath hardware statistics.
 * @vpath_handle: HAL Vpath handle.
 * @bufsize: Buffer size.
 * @retbuf: Buffer pointer.
 * @retsize: Size of the result. Cannot be greater than @bufsize.
 *
 * Read vpath hardware statistics. This is a subset of stats counters
 * from vxge_hal_vpath_stats_hw_info_t {}.
 *
 */
vxge_hal_status_e vxge_hal_aux_stats_vpath_hw_read(
			vxge_hal_vpath_h vpath_handle,
			int bufsize,
			char *retbuf,
			int *retsize);

/*
 * vxge_hal_aux_stats_device_hw_read - Read device hardware statistics.
 * @devh: HAL device handle.
 * @bufsize: Buffer size.
 * @retbuf: Buffer pointer.
 * @retsize: Size of the result. Cannot be greater than @bufsize.
 *
 * Read device hardware statistics. This is a subset of stats counters
 * from vxge_hal_device_stats_hw_info_t {}.
 *
 */
vxge_hal_status_e vxge_hal_aux_stats_device_hw_read(vxge_hal_device_h devh,
			int bufsize, char *retbuf, int *retsize);

/*
 * vxge_hal_aux_stats_vpath_sw_fifo_read - Read vpath fifo software statistics.
 * @vpath_handle: HAL Vpath handle.
 * @bufsize: Buffer size.
 * @retbuf: Buffer pointer.
 * @retsize: Size of the result. Cannot be greater than @bufsize.
 *
 * Read vpath fifo software statistics. This is a subset of stats counters
 * from vxge_hal_vpath_stats_sw_fifo_info_t {}.
 *
 */
vxge_hal_status_e vxge_hal_aux_stats_vpath_sw_fifo_read(
			vxge_hal_vpath_h vpath_handle,
			int bufsize,
			char *retbuf,
			int *retsize);

/*
 * vxge_hal_aux_stats_vpath_sw_ring_read - Read vpath ring software statistics.
 * @vpath_handle: HAL Vpath handle.
 * @bufsize: Buffer size.
 * @retbuf: Buffer pointer.
 * @retsize: Size of the result. Cannot be greater than @bufsize.
 *
 * Read vpath ring software statistics. This is a subset of stats counters
 * from vxge_hal_vpath_stats_sw_ring_info_t {}.
 *
 */
vxge_hal_status_e vxge_hal_aux_stats_vpath_sw_ring_read(
			vxge_hal_vpath_h vpath_handle,
			int bufsize,
			char *retbuf,
			int *retsize);


/*
 * vxge_hal_aux_stats_vpath_sw_err_read - Read vpath err software statistics.
 * @vpath_handle: HAL Vpath handle.
 * @bufsize: Buffer size.
 * @retbuf: Buffer pointer.
 * @retsize: Size of the result. Cannot be greater than @bufsize.
 *
 * Read vpath err software statistics. This is a subset of stats counters
 * from vxge_hal_vpath_stats_sw_err_info_t {}.
 *
 */
vxge_hal_status_e vxge_hal_aux_stats_vpath_sw_err_read(
			vxge_hal_vpath_h vpath_handle,
			int bufsize,
			char *retbuf,
			int *retsize);

/*
 * vxge_hal_aux_stats_vpath_sw_read - Read vpath soft statistics.
 * @vpath_handle: HAL Vpath handle.
 * @bufsize: Buffer size.
 * @retbuf: Buffer pointer.
 * @retsize: Size of the result. Cannot be greater than @bufsize.
 *
 * Read device hardware statistics. This is a subset of stats counters
 * from vxge_hal_vpath_stats_sw_info_t {}.
 *
 */
vxge_hal_status_e vxge_hal_aux_stats_vpath_sw_read(
			vxge_hal_vpath_h vpath_handle,
			int bufsize,
			char *retbuf,
			int *retsize);

/*
 * vxge_hal_aux_stats_device_sw_read - Read device software statistics.
 * @devh: HAL device handle.
 * @bufsize: Buffer size.
 * @retbuf: Buffer pointer.
 * @retsize: Size of the result. Cannot be greater than @bufsize.
 *
 * Read device software statistics. This is a subset of stats counters
 * from vxge_hal_device_stats_sw_info_t {}.
 *
 */
vxge_hal_status_e vxge_hal_aux_stats_device_sw_read(vxge_hal_device_h devh,
			int bufsize, char *retbuf, int *retsize);

/*
 * vxge_hal_aux_stats_device_sw_err_read - Read device software error statistics
 * @devh: HAL device handle.
 * @bufsize: Buffer size.
 * @retbuf: Buffer pointer.
 * @retsize: Size of the result. Cannot be greater than @bufsize.
 *
 * Read device software error statistics. This is a subset of stats counters
 * from vxge_hal_device_stats_sw_info_t {}.
 *
 */
vxge_hal_status_e vxge_hal_aux_stats_device_sw_err_read(vxge_hal_device_h devh,
			int bufsize, char *retbuf, int *retsize);

/*
 * vxge_hal_aux_stats_device_read - Read device statistics.
 * @devh: HAL device handle.
 * @bufsize: Buffer size.
 * @retbuf: Buffer pointer.
 * @retsize: Size of the result. Cannot be greater than @bufsize.
 *
 * Read device statistics. This is a subset of stats counters
 * from vxge_hal_device_stats_t {}.
 *
 */
vxge_hal_status_e vxge_hal_aux_stats_device_read(vxge_hal_device_h devh,
			int bufsize, char *retbuf, int *retsize);

/*
 * vxge_hal_aux_stats_xpak_read - Read device xpak statistics.
 * @devh: HAL device handle.
 * @bufsize: Buffer size.
 * @retbuf: Buffer pointer.
 * @retsize: Size of the result. Cannot be greater than @bufsize.
 *
 * Read device xpak statistics. This is valid for function 0 device only
 *
 */
vxge_hal_status_e vxge_hal_aux_stats_xpak_read(vxge_hal_device_h devh,
			int bufsize, char *retbuf, int *retsize);

/*
 * vxge_hal_aux_stats_mrpcim_read - Read device mrpcim statistics.
 * @devh: HAL device handle.
 * @bufsize: Buffer size.
 * @retbuf: Buffer pointer.
 * @retsize: Size of the result. Cannot be greater than @bufsize.
 *
 * Read mrpcim statistics. This is valid for function 0 device only
 *
 */
vxge_hal_status_e vxge_hal_aux_stats_mrpcim_read(vxge_hal_device_h devh,
			int bufsize, char *retbuf, int *retsize);

/*
 * vxge_hal_aux_vpath_ring_dump - Dump vpath ring.
 * @vpath_handle: Vpath handle.
 *
 * Dump vpath ring.
 */
vxge_hal_status_e
vxge_hal_aux_vpath_ring_dump(vxge_hal_vpath_h vpath_handle);

/*
 * vxge_hal_aux_vpath_fifo_dump - Dump vpath fifo.
 * @vpath_handle: Vpath handle.
 *
 * Dump vpath fifo.
 */
vxge_hal_status_e
vxge_hal_aux_vpath_fifo_dump(vxge_hal_vpath_h vpath_handle);

/*
 * vxge_hal_aux_device_dump - Dump driver "about" info and device state.
 * @devh: HAL device handle.
 *
 * Dump driver & device "about" info and device state,
 * including all BAR0 registers, hardware and software statistics, PCI
 * configuration space.
 */
vxge_hal_status_e vxge_hal_aux_device_dump(vxge_hal_device_h devh);

__EXTERN_END_DECLS

#endif /* VXGE_HAL_MGMTAUX_H */
