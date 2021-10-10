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
 *  Copyright (c) 2010, Oracle and/or its affiliates. All rights reserved.
 */

/*
 * mcxnex_ioctl.c
 *    Mcxnex IOCTL Routines
 *
 *    Implements all ioctl access into the driver.  This includes all routines
 *    necessary for updating firmware, accessing the mcxnex flash device, and
 *    providing interfaces for VTS.
 */

#include <sys/types.h>
#include <sys/conf.h>
#include <sys/ddi.h>
#include <sys/sunddi.h>
#include <sys/modctl.h>
#include <sys/file.h>

#include "mcxnex.h"

/* mcxnex HCA state pointer (extern) */
extern void	*mcxnex_statep;
extern int mcxnex_verbose;

#define	DO_WRCONF	1
static int do_bar0 = 1;

/*
 * The ioctl declarations (for firmware flash burning, register read/write
 * (DEBUG-only), and VTS interfaces)
 */
static int mcxnex_ioctl_flash_read(mcxnex_state_t *state, dev_t dev,
    intptr_t arg, int mode);
static int mcxnex_ioctl_flash_write(mcxnex_state_t *state, dev_t dev,
    intptr_t arg, int mode);
static int mcxnex_ioctl_flash_erase(mcxnex_state_t *state, dev_t dev,
    intptr_t arg, int mode);
static int mcxnex_ioctl_flash_init(mcxnex_state_t *state, dev_t dev,
    intptr_t arg, int mode);
static int mcxnex_ioctl_flash_fini(mcxnex_state_t *state, dev_t dev);
static int mcxnex_ioctl_flash_cleanup(mcxnex_state_t *state);
static int mcxnex_ioctl_flash_cleanup_nolock(mcxnex_state_t *state);
#ifdef	DEBUG
static int mcxnex_ioctl_reg_write(mcxnex_state_t *state, intptr_t arg,
    int mode);
static int mcxnex_ioctl_reg_read(mcxnex_state_t *state, intptr_t arg,
    int mode);
#endif	/* DEBUG */
static int mcxnex_ioctl_write_boot_addr(mcxnex_state_t *state, dev_t dev,
    intptr_t arg, int mode);
static int mcxnex_ioctl_info(mcxnex_state_t *state, dev_t dev,
    intptr_t arg, int mode);

/* Hemron Flash Functions */
static void mcxnex_flash_spi_exec_command(mcxnex_state_t *state,
    ddi_acc_handle_t hdl, uint32_t cmd);
static int mcxnex_flash_read_sector(mcxnex_state_t *state,
    uint32_t sector_num);
static int mcxnex_flash_read_quadlet(mcxnex_state_t *state, uint32_t *data,
    uint32_t addr);
static int mcxnex_flash_write_sector(mcxnex_state_t *state,
    uint32_t sector_num);
static int mcxnex_flash_spi_write_dword(mcxnex_state_t *state,
    uint32_t addr, uint32_t data);
static int mcxnex_flash_write_byte(mcxnex_state_t *state, uint32_t addr,
    uchar_t data);
static int mcxnex_flash_erase_sector(mcxnex_state_t *state,
    uint32_t sector_num);
static int mcxnex_flash_erase_chip(mcxnex_state_t *state);
static int mcxnex_flash_bank(mcxnex_state_t *state, uint32_t addr);
static uint32_t mcxnex_flash_read(mcxnex_state_t *state, uint32_t addr,
    int *err);
static void mcxnex_flash_write(mcxnex_state_t *state, uint32_t addr,
    uchar_t data, int *err);
static int mcxnex_flash_spi_wait_wip(mcxnex_state_t *state);
static void mcxnex_flash_spi_write_enable(mcxnex_state_t *state);
static int mcxnex_flash_init(mcxnex_state_t *state);
static int mcxnex_flash_cfi_init(mcxnex_state_t *state, uint32_t *cfi_info,
    int *intel_xcmd);
static int mcxnex_flash_fini(mcxnex_state_t *state);
static int mcxnex_flash_reset(mcxnex_state_t *state);
static uint32_t mcxnex_flash_read_cfg(mcxnex_state_t *state,
    ddi_acc_handle_t pci_config_hdl, uint32_t addr);
#ifdef DO_WRCONF
static void mcxnex_flash_write_cfg(mcxnex_state_t *state,
    ddi_acc_handle_t pci_config_hdl, uint32_t addr, uint32_t data);
static void mcxnex_flash_write_cfg_helper(mcxnex_state_t *state,
    ddi_acc_handle_t pci_config_hdl, uint32_t addr, uint32_t data);
static void mcxnex_flash_write_confirm(mcxnex_state_t *state,
    ddi_acc_handle_t pci_config_hdl);
#endif
static void mcxnex_flash_cfi_byte(uint8_t *ch, uint32_t dword, int i);
static void mcxnex_flash_cfi_dword(uint32_t *dword, uint8_t *ch, int i);

/* Patchable timeout values for flash operations */
int mcxnex_hw_flash_timeout_gpio_sema = MCXNEX_HW_FLASH_TIMEOUT_GPIO_SEMA;
int mcxnex_hw_flash_timeout_config = MCXNEX_HW_FLASH_TIMEOUT_CONFIG;
int mcxnex_hw_flash_timeout_write = MCXNEX_HW_FLASH_TIMEOUT_WRITE;
int mcxnex_hw_flash_timeout_erase = MCXNEX_HW_FLASH_TIMEOUT_ERASE;

/*
 * mcxnex_ioctl()
 */
/* ARGSUSED */
int
mcxnex_ioctl(dev_t dev, int cmd, intptr_t arg, int mode, cred_t *credp,
    int *rvalp)
{
	mcxnex_state_t	*state;
	minor_t		instance;
	int		status;

	if (drv_priv(credp) != 0) {
		return (EPERM);
	}

	instance = MCXNEX_DEV_INSTANCE(dev);
	if (instance == (minor_t)-1) {
		return (EBADF);
	}

	state = ddi_get_soft_state(mcxnex_statep, instance);
	if (state == NULL) {
		return (EBADF);
	}

	status = 0;

	switch (cmd) {
	case HERMON_IOCTL_FLASH_READ:
		status = mcxnex_ioctl_flash_read(state, dev, arg, mode);
		break;

	case HERMON_IOCTL_FLASH_WRITE:
		status = mcxnex_ioctl_flash_write(state, dev, arg, mode);
		break;

	case HERMON_IOCTL_FLASH_ERASE:
		status = mcxnex_ioctl_flash_erase(state, dev, arg, mode);
		break;

	case HERMON_IOCTL_FLASH_INIT:
		status = mcxnex_ioctl_flash_init(state, dev, arg, mode);
		break;

	case HERMON_IOCTL_FLASH_FINI:
		status = mcxnex_ioctl_flash_fini(state, dev);
		break;

	case HERMON_IOCTL_INFO:
		status = mcxnex_ioctl_info(state, dev, arg, mode);
		break;

#ifdef	DEBUG
	case HERMON_IOCTL_REG_WRITE:
		status = mcxnex_ioctl_reg_write(state, arg, mode);
		break;

	case HERMON_IOCTL_REG_READ:
		status = mcxnex_ioctl_reg_read(state, arg, mode);
		break;
#endif	/* DEBUG */

	case HERMON_IOCTL_DDR_READ:
		/* XXX guard until the ioctl header is cleaned up */
		status = ENODEV;
		break;

	case HERMON_IOCTL_WRITE_BOOT_ADDR:
		status = mcxnex_ioctl_write_boot_addr(state, dev, arg, mode);
		break;

	default:
		status = ENOTTY;
		break;
	}
	*rvalp = status;

	return (status);
}

/*
 * mcxnex_ioctl_flash_read()
 */
static int
mcxnex_ioctl_flash_read(mcxnex_state_t *state, dev_t dev, intptr_t arg,
    int mode)
{
	mcxnex_flash_ioctl_t ioctl_info;
	int status = 0;

	/*
	 * Check that flash init ioctl has been called first.  And check
	 * that the same dev_t that called init is the one calling read now.
	 */
	mutex_enter(&state->hs_fw_flashlock);
	if ((state->hs_fw_flashdev != dev) ||
	    (state->hs_fw_flashstarted == 0)) {
		mutex_exit(&state->hs_fw_flashlock);
		return (EIO);
	}

	/* copy user struct to kernel */
#ifdef _MULTI_DATAMODEL
	if (ddi_model_convert_from(mode & FMODELS) == DDI_MODEL_ILP32) {
		mcxnex_flash_ioctl32_t info32;

		if (ddi_copyin((void *)arg, &info32,
		    sizeof (mcxnex_flash_ioctl32_t), mode) != 0) {
			mutex_exit(&state->hs_fw_flashlock);
			return (EFAULT);
		}
		ioctl_info.af_type = info32.af_type;
		ioctl_info.af_sector = (caddr_t)(uintptr_t)info32.af_sector;
		ioctl_info.af_sector_num = info32.af_sector_num;
		ioctl_info.af_addr = info32.af_addr;
	} else
#endif /* _MULTI_DATAMODEL */
	if (ddi_copyin((void *)arg, &ioctl_info, sizeof (mcxnex_flash_ioctl_t),
	    mode) != 0) {
		mutex_exit(&state->hs_fw_flashlock);
		return (EFAULT);
	}

	/*
	 * Determine type of READ ioctl
	 */
	switch (ioctl_info.af_type) {
	case HERMON_FLASH_READ_SECTOR:
		/* Check if sector num is too large for flash device */
		if (ioctl_info.af_sector_num >=
		    (state->hs_fw_device_sz >> state->hs_fw_log_sector_sz)) {
			mutex_exit(&state->hs_fw_flashlock);
			return (EFAULT);
		}

		/* Perform the Sector Read */
		if ((status = mcxnex_flash_reset(state)) != 0 ||
		    (status = mcxnex_flash_read_sector(state,
		    ioctl_info.af_sector_num)) != 0) {
			mutex_exit(&state->hs_fw_flashlock);
			return (status);
		}

		/* copyout the firmware sector image data */
		if (ddi_copyout(&state->hs_fw_sector[0],
		    &ioctl_info.af_sector[0], 1 << state->hs_fw_log_sector_sz,
		    mode) != 0) {
			mutex_exit(&state->hs_fw_flashlock);
			return (EFAULT);
		}
		break;

	case HERMON_FLASH_READ_QUADLET:
		/* Check if addr is too large for flash device */
		if (ioctl_info.af_addr >= state->hs_fw_device_sz) {
			mutex_exit(&state->hs_fw_flashlock);
			return (EFAULT);
		}

		/* Perform the Quadlet Read */
		if ((status = mcxnex_flash_reset(state)) != 0 ||
		    (status = mcxnex_flash_read_quadlet(state,
		    &ioctl_info.af_quadlet, ioctl_info.af_addr)) != 0) {
			mutex_exit(&state->hs_fw_flashlock);
			return (status);
		}
		break;

	default:
		mutex_exit(&state->hs_fw_flashlock);
		return (EINVAL);
	}

	/* copy results back to userland */
#ifdef _MULTI_DATAMODEL
	if (ddi_model_convert_from(mode & FMODELS) == DDI_MODEL_ILP32) {
		mcxnex_flash_ioctl32_t info32;

		info32.af_quadlet = ioctl_info.af_quadlet;
		info32.af_type = ioctl_info.af_type;
		info32.af_sector_num = ioctl_info.af_sector_num;
		info32.af_sector = (caddr32_t)(uintptr_t)ioctl_info.af_sector;
		info32.af_addr = ioctl_info.af_addr;

		if (ddi_copyout(&info32, (void *)arg,
		    sizeof (mcxnex_flash_ioctl32_t), mode) != 0) {
			mutex_exit(&state->hs_fw_flashlock);
			return (EFAULT);
		}
	} else
#endif /* _MULTI_DATAMODEL */
	if (ddi_copyout(&ioctl_info, (void *)arg,
	    sizeof (mcxnex_flash_ioctl_t), mode) != 0) {
		mutex_exit(&state->hs_fw_flashlock);
		return (EFAULT);
	}

	mutex_exit(&state->hs_fw_flashlock);
	return (status);
}

/*
 * mcxnex_ioctl_flash_write()
 */
static int
mcxnex_ioctl_flash_write(mcxnex_state_t *state, dev_t dev, intptr_t arg,
    int mode)
{
	mcxnex_flash_ioctl_t	ioctl_info;
	int status = 0;

	/*
	 * Check that flash init ioctl has been called first.  And check
	 * that the same dev_t that called init is the one calling write now.
	 */
	mutex_enter(&state->hs_fw_flashlock);
	if ((state->hs_fw_flashdev != dev) ||
	    (state->hs_fw_flashstarted == 0)) {
		mutex_exit(&state->hs_fw_flashlock);
		return (EIO);
	}

	/* copy user struct to kernel */
#ifdef _MULTI_DATAMODEL
	if (ddi_model_convert_from(mode & FMODELS) == DDI_MODEL_ILP32) {
		mcxnex_flash_ioctl32_t info32;

		if (ddi_copyin((void *)arg, &info32,
		    sizeof (mcxnex_flash_ioctl32_t), mode) != 0) {
			mutex_exit(&state->hs_fw_flashlock);
			return (EFAULT);
		}
		ioctl_info.af_type = info32.af_type;
		ioctl_info.af_sector = (caddr_t)(uintptr_t)info32.af_sector;
		ioctl_info.af_sector_num = info32.af_sector_num;
		ioctl_info.af_addr = info32.af_addr;
		ioctl_info.af_byte = info32.af_byte;
	} else
#endif /* _MULTI_DATAMODEL */
	if (ddi_copyin((void *)arg, &ioctl_info,
	    sizeof (mcxnex_flash_ioctl_t), mode) != 0) {
		mutex_exit(&state->hs_fw_flashlock);
		return (EFAULT);
	}

	/*
	 * Determine type of WRITE ioctl
	 */
	switch (ioctl_info.af_type) {
	case HERMON_FLASH_WRITE_SECTOR:
		/* Check if sector num is too large for flash device */
		if (ioctl_info.af_sector_num >=
		    (state->hs_fw_device_sz >> state->hs_fw_log_sector_sz)) {
			mutex_exit(&state->hs_fw_flashlock);
			return (EFAULT);
		}

		/* copy in fw sector image data */
		if (ddi_copyin(&ioctl_info.af_sector[0],
		    &state->hs_fw_sector[0], 1 << state->hs_fw_log_sector_sz,
		    mode) != 0) {
			mutex_exit(&state->hs_fw_flashlock);
			return (EFAULT);
		}

		/* Perform Write Sector */
		status = mcxnex_flash_write_sector(state,
		    ioctl_info.af_sector_num);
		break;

	case HERMON_FLASH_WRITE_BYTE:
		/* Check if addr is too large for flash device */
		if (ioctl_info.af_addr >= state->hs_fw_device_sz) {
			mutex_exit(&state->hs_fw_flashlock);
			return (EFAULT);
		}

		/* Perform Write Byte */
		/*
		 * CMJ -- is a reset really needed before and after writing
		 * each byte?  This code came from arbel, but we should look
		 * into this.  Also, for SPI, no reset is actually performed.
		 */
		if ((status = mcxnex_flash_bank(state,
		    ioctl_info.af_addr)) != 0 ||
		    (status = mcxnex_flash_reset(state)) != 0 ||
		    (status = mcxnex_flash_write_byte(state,
		    ioctl_info.af_addr, ioctl_info.af_byte)) != 0 ||
		    (status = mcxnex_flash_reset(state)) != 0) {
			mutex_exit(&state->hs_fw_flashlock);
			return (status);
		}
		break;

	default:
		status = EINVAL;
		break;
	}

	mutex_exit(&state->hs_fw_flashlock);
	return (status);
}

/*
 * mcxnex_ioctl_flash_erase()
 */
static int
mcxnex_ioctl_flash_erase(mcxnex_state_t *state, dev_t dev, intptr_t arg,
    int mode)
{
	mcxnex_flash_ioctl_t	ioctl_info;
	int status = 0;

	/*
	 * Check that flash init ioctl has been called first.  And check
	 * that the same dev_t that called init is the one calling erase now.
	 */
	mutex_enter(&state->hs_fw_flashlock);
	if ((state->hs_fw_flashdev != dev) ||
	    (state->hs_fw_flashstarted == 0)) {
		mutex_exit(&state->hs_fw_flashlock);
		return (EIO);
	}

	/* copy user struct to kernel */
#ifdef _MULTI_DATAMODEL
	if (ddi_model_convert_from(mode & FMODELS) == DDI_MODEL_ILP32) {
		mcxnex_flash_ioctl32_t info32;

		if (ddi_copyin((void *)arg, &info32,
		    sizeof (mcxnex_flash_ioctl32_t), mode) != 0) {
			mutex_exit(&state->hs_fw_flashlock);
			return (EFAULT);
		}
		ioctl_info.af_type = info32.af_type;
		ioctl_info.af_sector_num = info32.af_sector_num;
	} else
#endif /* _MULTI_DATAMODEL */
	if (ddi_copyin((void *)arg, &ioctl_info, sizeof (mcxnex_flash_ioctl_t),
	    mode) != 0) {
		mutex_exit(&state->hs_fw_flashlock);
		return (EFAULT);
	}

	/*
	 * Determine type of ERASE ioctl
	 */
	switch (ioctl_info.af_type) {
	case HERMON_FLASH_ERASE_SECTOR:
		/* Check if sector num is too large for flash device */
		if (ioctl_info.af_sector_num >=
		    (state->hs_fw_device_sz >> state->hs_fw_log_sector_sz)) {
			mutex_exit(&state->hs_fw_flashlock);
			return (EFAULT);
		}

		/* Perform Sector Erase */
		status = mcxnex_flash_erase_sector(state,
		    ioctl_info.af_sector_num);
		break;

	case HERMON_FLASH_ERASE_CHIP:
		/* Perform Chip Erase */
		status = mcxnex_flash_erase_chip(state);
		break;

	default:
		status = EINVAL;
		break;
	}

	mutex_exit(&state->hs_fw_flashlock);
	return (status);
}

/*
 * mcxnex_ioctl_flash_init()
 */
static int
mcxnex_ioctl_flash_init(mcxnex_state_t *state, dev_t dev, intptr_t arg,
    int mode)
{
	mcxnex_flash_init_ioctl_t init_info;
	int ret;
	int intel_xcmd = 0;
	ddi_acc_handle_t pci_hdl = mcxnex_get_pcihdl(state);

	/* initialize the FMA retry loop */
	mcxnex_pio_init(fm_loop_cnt, fm_status, fm_test);

	state->hs_fw_sector = NULL;

	/*
	 * init cannot be called more than once.  If we have already init'd the
	 * flash, return directly.
	 */
	mutex_enter(&state->hs_fw_flashlock);
	if (state->hs_fw_flashstarted == 1) {
		mutex_exit(&state->hs_fw_flashlock);
		return (EINVAL);
	}

	/* copyin the user struct to kernel */
	if (ddi_copyin((void *)arg, &init_info,
	    sizeof (mcxnex_flash_init_ioctl_t), mode) != 0) {
		mutex_exit(&state->hs_fw_flashlock);
		return (EFAULT);
	}

	/* Init Flash */
	if ((ret = mcxnex_flash_init(state)) != 0) {
		if (ret == EIO) {
			goto pio_error;
		}
		mutex_exit(&state->hs_fw_flashlock);
		return (ret);
	}

	/* Read CFI info */
	if ((ret = mcxnex_flash_cfi_init(state, &init_info.af_cfi_info[0],
	    &intel_xcmd)) != 0) {
		if (ret == EIO) {
			goto pio_error;
		}
		mutex_exit(&state->hs_fw_flashlock);
		return (ret);
	}

	/*
	 * Return error if the command set is unknown.
	 */
	if (state->hs_fw_cmdset == HERMON_FLASH_UNKNOWN_CMDSET) {
		if ((ret = mcxnex_ioctl_flash_cleanup_nolock(state)) != 0) {
			if (ret == EIO) {
				goto pio_error;
			}
			mutex_exit(&state->hs_fw_flashlock);
			return (ret);
		}
		mutex_exit(&state->hs_fw_flashlock);
		return (EFAULT);
	}

	/* the FMA retry loop starts. */
	mcxnex_pio_start(state, pci_hdl, pio_error,
	    fm_loop_cnt, fm_status, fm_test);

	/* Read HWREV - least significant 8 bits is revision ID */
	init_info.af_hwrev = pci_config_get32(pci_hdl,
	    MCXNEX_HW_FLASH_CFG_HWREV) & 0xFF;

	/* the FMA retry loop ends. */
	mcxnex_pio_end(state, pci_hdl, pio_error, fm_loop_cnt,
	    fm_status, fm_test);

	/* Fill in the firmwate revision numbers */
	init_info.af_fwrev.afi_maj	= state->hs_fw.fw_rev_major;
	init_info.af_fwrev.afi_min	= state->hs_fw.fw_rev_minor;
	init_info.af_fwrev.afi_sub	= state->hs_fw.fw_rev_subminor;

	/* Alloc flash mem for one sector size */
	state->hs_fw_sector = (uint32_t *)kmem_zalloc(1 <<
	    state->hs_fw_log_sector_sz, KM_SLEEP);

	/* Set HW part number and length */
	init_info.af_pn_len = state->hs_hca_pn_len;
	if (state->hs_hca_pn_len != 0) {
		(void) memcpy(init_info.af_hwpn, state->hs_hca_pn,
		    state->hs_hca_pn_len);
	}

	/* Copy ioctl results back to userland */
	if (ddi_copyout(&init_info, (void *)arg,
	    sizeof (mcxnex_flash_init_ioctl_t), mode) != 0) {
		if ((ret = mcxnex_ioctl_flash_cleanup_nolock(state)) != 0) {
			if (ret == EIO) {
				goto pio_error;
			}
			mutex_exit(&state->hs_fw_flashlock);
			return (ret);
		}
		mutex_exit(&state->hs_fw_flashlock);
		return (EFAULT);
	}

	/* Set flash state to started */
	state->hs_fw_flashstarted = 1;
	state->hs_fw_flashdev	  = dev;

	mutex_exit(&state->hs_fw_flashlock);

	/*
	 * If "flash init" is successful, add an "on close" callback to the
	 * current dev node to ensure that "flash fini" gets called later
	 * even if the userland process prematurely exits.
	 */
	ret = mcxnex_umap_db_set_onclose_cb(dev,
	    MCXNEX_ONCLOSE_FLASH_INPROGRESS,
	    (int (*)(void *))mcxnex_ioctl_flash_cleanup, state);
	if (ret != DDI_SUCCESS) {
		int status = mcxnex_ioctl_flash_fini(state, dev);
		if (status != 0) {
			if (status == EIO) {
				mcxnex_fm_ereport(state, HCA_SYS_ERR,
				    HCA_ERR_IOCTL);
				return (EIO);
			}
			return (status);
		}
	}
	return (0);

pio_error:
	mutex_exit(&state->hs_fw_flashlock);
	mcxnex_fm_ereport(state, HCA_SYS_ERR, HCA_ERR_IOCTL);
	return (EIO);
}

/*
 * mcxnex_ioctl_flash_fini()
 */
static int
mcxnex_ioctl_flash_fini(mcxnex_state_t *state, dev_t dev)
{
	int ret;

	/*
	 * Check that flash init ioctl has been called first.  And check
	 * that the same dev_t that called init is the one calling fini now.
	 */
	mutex_enter(&state->hs_fw_flashlock);
	if ((state->hs_fw_flashdev != dev) ||
	    (state->hs_fw_flashstarted == 0)) {
		mutex_exit(&state->hs_fw_flashlock);
		return (EINVAL);
	}

	if ((ret = mcxnex_ioctl_flash_cleanup_nolock(state)) != 0) {
		mutex_exit(&state->hs_fw_flashlock);
		if (ret == EIO) {
			mcxnex_fm_ereport(state, HCA_SYS_ERR, HCA_ERR_IOCTL);
		}
		return (ret);
	}
	mutex_exit(&state->hs_fw_flashlock);

	/*
	 * If "flash fini" is successful, remove the "on close" callback
	 * that was setup during "flash init".
	 */
	ret = mcxnex_umap_db_clear_onclose_cb(dev,
	    MCXNEX_ONCLOSE_FLASH_INPROGRESS);
	if (ret != DDI_SUCCESS) {
		return (EFAULT);
	}
	return (0);
}


/*
 * mcxnex_ioctl_flash_cleanup()
 */
static int
mcxnex_ioctl_flash_cleanup(mcxnex_state_t *state)
{
	int status;

	mutex_enter(&state->hs_fw_flashlock);
	status = mcxnex_ioctl_flash_cleanup_nolock(state);
	mutex_exit(&state->hs_fw_flashlock);

	return (status);
}


/*
 * mcxnex_ioctl_flash_cleanup_nolock()
 */
static int
mcxnex_ioctl_flash_cleanup_nolock(mcxnex_state_t *state)
{
	int status;
	ASSERT(MUTEX_HELD(&state->hs_fw_flashlock));

	/* free flash mem */
	if (state->hs_fw_sector) {
		kmem_free(state->hs_fw_sector, 1 << state->hs_fw_log_sector_sz);
	}

	/* Fini the Flash */
	if ((status = mcxnex_flash_fini(state)) != 0)
		return (status);

	/* Set flash state to fini */
	state->hs_fw_flashstarted = 0;
	state->hs_fw_flashdev	  = 0;
	return (0);
}


/*
 * mcxnex_ioctl_info()
 */
static int
mcxnex_ioctl_info(mcxnex_state_t *state, dev_t dev, intptr_t arg, int mode)
{
	mcxnex_info_ioctl_t	 info;
	mcxnex_flash_init_ioctl_t init_info;

	/*
	 * Access to Hemron VTS ioctls is not allowed in "maintenance mode".
	 */
	if (state->hs_operational_mode == MCXNEX_MAINTENANCE_MODE) {
		return (EFAULT);
	}

	/* copyin the user struct to kernel */
	if (ddi_copyin((void *)arg, &info, sizeof (mcxnex_info_ioctl_t),
	    mode) != 0) {
		return (EFAULT);
	}

	/*
	 * Check ioctl revision
	 */
	if (info.ai_revision != HERMON_VTS_IOCTL_REVISION) {
		return (EINVAL);
	}

	/*
	 * If the 'fw_device_sz' has not been initialized yet, we initialize it
	 * here.  This is done by leveraging the
	 * mcxnex_ioctl_flash_init()/fini() calls.  We also hold our own mutex
	 * around this operation in case we have multiple VTS threads in
	 * process at the same time.
	 */
	mutex_enter(&state->hs_info_lock);
	if (state->hs_fw_device_sz == 0) {
		if (mcxnex_ioctl_flash_init(state, dev, (intptr_t)&init_info,
		    (FKIOCTL | mode)) != 0) {
			mutex_exit(&state->hs_info_lock);
			return (EFAULT);
		}
		(void) mcxnex_ioctl_flash_fini(state, dev);
	}
	mutex_exit(&state->hs_info_lock);

	info.ai_hw_rev		 = state->hs_revision_id;
	info.ai_flash_sz	 = state->hs_fw_device_sz;
	info.ai_fw_rev.afi_maj	 = state->hs_fw.fw_rev_major;
	info.ai_fw_rev.afi_min	 = state->hs_fw.fw_rev_minor;
	info.ai_fw_rev.afi_sub	 = state->hs_fw.fw_rev_subminor;

	/* Copy ioctl results back to user struct */
	if (ddi_copyout(&info, (void *)arg, sizeof (mcxnex_info_ioctl_t),
	    mode) != 0) {
		return (EFAULT);
	}

	return (0);
}

#ifdef	DEBUG
/*
 * mcxnex_ioctl_reg_read()
 */
static int
mcxnex_ioctl_reg_read(mcxnex_state_t *state, intptr_t arg, int mode)
{
	mcxnex_reg_ioctl_t	rdreg;
	uint32_t		*addr;
	uintptr_t		baseaddr;
	int			status;
	ddi_acc_handle_t	handle;

	/* initialize the FMA retry loop */
	mcxnex_pio_init(fm_loop_cnt, fm_status, fm_test);

	/*
	 * Access to Hemron registers is not allowed in "maintenance mode".
	 * This is primarily because the device may not have BARs to access
	 */
	if (state->hs_operational_mode == MCXNEX_MAINTENANCE_MODE) {
		return (EFAULT);
	}

	/* Copy in the mcxnex_reg_ioctl_t structure */
	status = ddi_copyin((void *)arg, &rdreg, sizeof (mcxnex_reg_ioctl_t),
	    mode);
	if (status != 0) {
		return (EFAULT);
	}

	/* Determine base address for requested register set */
	switch (rdreg.arg_reg_set) {
	case MCXNEX_CMD_BAR:
		baseaddr = (uintptr_t)state->hs_reg_cmd_baseaddr;
		handle = mcxnex_get_cmdhdl(state);
		break;

	case MCXNEX_UAR_BAR:
		baseaddr = (uintptr_t)state->hs_reg_uar_baseaddr;
		handle = mcxnex_get_uarhdl(state);
		break;


	default:
		return (EINVAL);
	}

	/* Ensure that address is properly-aligned */
	addr = (uint32_t *)((baseaddr + rdreg.arg_offset) & ~0x3);

	/* the FMA retry loop starts. */
	mcxnex_pio_start(state, handle, pio_error, fm_loop_cnt,
	    fm_status, fm_test);

	/* Read the register pointed to by addr */
	rdreg.arg_data = ddi_get32(handle, addr);

	/* the FMA retry loop ends. */
	mcxnex_pio_end(state, handle, pio_error, fm_loop_cnt, fm_status,
	    fm_test);

	/* Copy in the result into the mcxnex_reg_ioctl_t structure */
	status = ddi_copyout(&rdreg, (void *)arg, sizeof (mcxnex_reg_ioctl_t),
	    mode);
	if (status != 0) {
		return (EFAULT);
	}
	return (0);

pio_error:
	mcxnex_fm_ereport(state, HCA_SYS_ERR, HCA_ERR_IOCTL);
	return (EIO);
}


/*
 * mcxnex_ioctl_reg_write()
 */
static int
mcxnex_ioctl_reg_write(mcxnex_state_t *state, intptr_t arg, int mode)
{
	mcxnex_reg_ioctl_t	wrreg;
	uint32_t		*addr;
	uintptr_t		baseaddr;
	int			status;
	ddi_acc_handle_t	handle;

	/* initialize the FMA retry loop */
	mcxnex_pio_init(fm_loop_cnt, fm_status, fm_test);

	/*
	 * Access to Mcxnex registers is not allowed in "maintenance mode".
	 * This is primarily because the device may not have BARs to access
	 */
	if (state->hs_operational_mode == MCXNEX_MAINTENANCE_MODE) {
		return (EFAULT);
	}

	/* Copy in the mcxnex_reg_ioctl_t structure */
	status = ddi_copyin((void *)arg, &wrreg, sizeof (mcxnex_reg_ioctl_t),
	    mode);
	if (status != 0) {
		return (EFAULT);
	}

	/* Determine base address for requested register set */
	switch (wrreg.arg_reg_set) {
	case MCXNEX_CMD_BAR:
		baseaddr = (uintptr_t)state->hs_reg_cmd_baseaddr;
		handle = mcxnex_get_cmdhdl(state);
		break;

	case MCXNEX_UAR_BAR:
		baseaddr = (uintptr_t)state->hs_reg_uar_baseaddr;
		handle = mcxnex_get_uarhdl(state);
		break;

	default:
		return (EINVAL);
	}

	/* Ensure that address is properly-aligned */
	addr = (uint32_t *)((baseaddr + wrreg.arg_offset) & ~0x3);

	/* the FMA retry loop starts. */
	mcxnex_pio_start(state, handle, pio_error, fm_loop_cnt,
	    fm_status, fm_test);

	/* Write the data to the register pointed to by addr */
	ddi_put32(handle, addr, wrreg.arg_data);

	/* the FMA retry loop ends. */
	mcxnex_pio_end(state, handle, pio_error, fm_loop_cnt, fm_status,
	    fm_test);
	return (0);

pio_error:
	mcxnex_fm_ereport(state, HCA_SYS_ERR, HCA_ERR_IOCTL);
	return (EIO);
}
#endif	/* DEBUG */

static int
mcxnex_ioctl_write_boot_addr(mcxnex_state_t *state, dev_t dev, intptr_t arg,
    int mode)
{
	mcxnex_flash_ioctl_t	ioctl_info;

	/* initialize the FMA retry loop */
	mcxnex_pio_init(fm_loop_cnt, fm_status, fm_test);

	/*
	 * Check that flash init ioctl has been called first.  And check
	 * that the same dev_t that called init is the one calling write now.
	 */
	mutex_enter(&state->hs_fw_flashlock);
	if ((state->hs_fw_flashdev != dev) ||
	    (state->hs_fw_flashstarted == 0)) {
		mutex_exit(&state->hs_fw_flashlock);
		return (EIO);
	}

	/* copy user struct to kernel */
#ifdef _MULTI_DATAMODEL
	if (ddi_model_convert_from(mode & FMODELS) == DDI_MODEL_ILP32) {
		mcxnex_flash_ioctl32_t info32;

		if (ddi_copyin((void *)arg, &info32,
		    sizeof (mcxnex_flash_ioctl32_t), mode) != 0) {
			mutex_exit(&state->hs_fw_flashlock);
			return (EFAULT);
		}
		ioctl_info.af_type = info32.af_type;
		ioctl_info.af_sector = (caddr_t)(uintptr_t)info32.af_sector;
		ioctl_info.af_sector_num = info32.af_sector_num;
		ioctl_info.af_addr = info32.af_addr;
		ioctl_info.af_byte = info32.af_byte;
	} else
#endif /* _MULTI_DATAMODEL */
	if (ddi_copyin((void *)arg, &ioctl_info,
	    sizeof (mcxnex_flash_ioctl_t), mode) != 0) {
		mutex_exit(&state->hs_fw_flashlock);
		return (EFAULT);
	}

	switch (state->hs_fw_cmdset) {
	case HERMON_FLASH_AMD_CMDSET:
	case HERMON_FLASH_INTEL_CMDSET:
		break;

	case HERMON_FLASH_SPI_CMDSET:
	{
		ddi_acc_handle_t pci_hdl = mcxnex_get_pcihdl(state);

		/* the FMA retry loop starts. */
		mcxnex_pio_start(state, pci_hdl, pio_error,
		    fm_loop_cnt, fm_status, fm_test);

		mcxnex_flash_write_cfg(state, pci_hdl,
		    MCXNEX_HW_FLASH_SPI_BOOT_ADDR_REG,
		    (ioctl_info.af_addr << 8) | 0x06);

		/* the FMA retry loop ends. */
		mcxnex_pio_end(state, pci_hdl, pio_error,
		    fm_loop_cnt, fm_status, fm_test);
		break;
	}

	case HERMON_FLASH_UNKNOWN_CMDSET:
	default:
		mutex_exit(&state->hs_fw_flashlock);
		return (EINVAL);
	}
	mutex_exit(&state->hs_fw_flashlock);
	return (0);

pio_error:
	mutex_exit(&state->hs_fw_flashlock);
	mcxnex_fm_ereport(state, HCA_SYS_ERR, HCA_ERR_IOCTL);
	return (EIO);
}

/*
 * mcxnex_flash_reset()
 */
static int
mcxnex_flash_reset(mcxnex_state_t *state)
{
	int status;

	/*
	 * Performs a reset to the flash device.  After a reset the flash will
	 * be operating in normal mode (capable of read/write, etc.).
	 */
	switch (state->hs_fw_cmdset) {
	case HERMON_FLASH_AMD_CMDSET:
		mcxnex_flash_write(state, 0x555, MCXNEX_HW_FLASH_RESET_AMD,
		    &status);
		if (status != 0) {
			return (status);
		}
		break;

	case HERMON_FLASH_INTEL_CMDSET:
		mcxnex_flash_write(state, 0x555, MCXNEX_HW_FLASH_RESET_INTEL,
		    &status);
		if (status != 0) {
			return (status);
		}
		break;

	/* It appears no reset is needed for SPI */
	case HERMON_FLASH_SPI_CMDSET:
		status = 0;
		break;

	case HERMON_FLASH_UNKNOWN_CMDSET:
	default:
		status = EINVAL;
		break;
	}
	return (status);
}

/*
 * mcxnex_flash_read_sector()
 */
static int
mcxnex_flash_read_sector(mcxnex_state_t *state, uint32_t sector_num)
{
	uint32_t addr;
	uint32_t end_addr;
	uint32_t *image;
	int i, status;

	image = (uint32_t *)&state->hs_fw_sector[0];

	/*
	 * Calculate the start and end address of the sector, based on the
	 * sector number passed in.
	 */
	addr = sector_num << state->hs_fw_log_sector_sz;
	end_addr = addr + (1 << state->hs_fw_log_sector_sz);

	/* Set the flash bank correctly for the given address */
	if ((status = mcxnex_flash_bank(state, addr)) != 0)
		return (status);

	/* Read the entire sector, one quadlet at a time */
	for (i = 0; addr < end_addr; i++, addr += 4) {
		image[i] = mcxnex_flash_read(state, addr, &status);
		if (status != 0) {
			return (status);
		}
	}
	return (0);
}

/*
 * mcxnex_flash_read_quadlet()
 */
static int
mcxnex_flash_read_quadlet(mcxnex_state_t *state, uint32_t *data,
    uint32_t addr)
{
	int status;

	/* Set the flash bank correctly for the given address */
	if ((status = mcxnex_flash_bank(state, addr)) != 0) {
		return (status);
	}

	/* Read one quadlet of data */
	*data = mcxnex_flash_read(state, addr, &status);
	if (status != 0) {
		return (EIO);
	}

	return (0);
}

/*
 * mcxnex_flash_write_sector()
 */
static int
mcxnex_flash_write_sector(mcxnex_state_t *state, uint32_t sector_num)
{
	uint32_t	addr;
	uint32_t	end_addr;
	uint32_t	*databuf;
	uchar_t		*sector;
	int		status = 0;
	int		i;

	sector = (uchar_t *)&state->hs_fw_sector[0];

	/*
	 * Calculate the start and end address of the sector, based on the
	 * sector number passed in.
	 */
	addr = sector_num << state->hs_fw_log_sector_sz;
	end_addr = addr + (1 << state->hs_fw_log_sector_sz);

	/* Set the flash bank correctly for the given address */
	if ((status = mcxnex_flash_bank(state, addr)) != 0 ||
	    (status = mcxnex_flash_reset(state)) != 0) {
		return (status);
	}

	/* Erase the sector before writing */
	status = mcxnex_flash_erase_sector(state, sector_num);
	if (status != 0) {
		return (status);
	}

	switch (state->hs_fw_cmdset) {
	case HERMON_FLASH_SPI_CMDSET:
		databuf = (uint32_t *)(void *)sector;
		/* Write the sector, one dword at a time */
		for (i = 0; addr < end_addr; i++, addr += 4) {
			if ((status = mcxnex_flash_spi_write_dword(state, addr,
			    htonl(databuf[i]))) != 0) {
				return (status);
			}
		}
		status = mcxnex_flash_reset(state);
		break;

	case HERMON_FLASH_INTEL_CMDSET:
	case HERMON_FLASH_AMD_CMDSET:
		/* Write the sector, one byte at a time */
		for (i = 0; addr < end_addr; i++, addr++) {
			status = mcxnex_flash_write_byte(state, addr,
			    sector[i]);
			if (status != 0) {
				break;
			}
		}
		status = mcxnex_flash_reset(state);
		break;

	case HERMON_FLASH_UNKNOWN_CMDSET:
	default:
		status = EINVAL;
		break;
	}

	return (status);
}

/*
 * mcxnex_flash_spi_write_dword()
 *
 * NOTE: This function assumes that "data" is in network byte order.
 *
 */
static int
mcxnex_flash_spi_write_dword(mcxnex_state_t *state, uint32_t addr,
    uint32_t data)
{
	int status;
	ddi_acc_handle_t	hdl;

	/* initialize the FMA retry loop */
	mcxnex_pio_init(fm_loop_cnt, fm_status, fm_test);

	hdl = mcxnex_get_pcihdl(state);

	/* the FMA retry loop starts. */
	mcxnex_pio_start(state, hdl, pio_error, fm_loop_cnt, fm_status,
	    fm_test);

	/* Issue Write Enable */
	mcxnex_flash_spi_write_enable(state);

	/* Set the Address */
	mcxnex_flash_write_cfg(state, hdl, MCXNEX_HW_FLASH_SPI_ADDR,
	    addr & MCXNEX_HW_FLASH_SPI_ADDR_MASK);

	/* Set the Data */
	mcxnex_flash_write_cfg(state, hdl, MCXNEX_HW_FLASH_SPI_DATA, data);

	/* Set the Page Program and execute */
	mcxnex_flash_spi_exec_command(state, hdl,
	    MCXNEX_HW_FLASH_SPI_INSTR_PHASE_OFF |
	    MCXNEX_HW_FLASH_SPI_ADDR_PHASE_OFF |
	    MCXNEX_HW_FLASH_SPI_DATA_PHASE_OFF |
	    MCXNEX_HW_FLASH_SPI_TRANS_SZ_4B |
	    (MCXNEX_HW_FLASH_SPI_PAGE_PROGRAM <<
	    MCXNEX_HW_FLASH_SPI_INSTR_SHIFT));

	/* Wait for write to complete */
	if ((status = mcxnex_flash_spi_wait_wip(state)) != 0) {
		return (status);
	}

	/* the FMA retry loop ends. */
	mcxnex_pio_end(state, hdl, pio_error, fm_loop_cnt, fm_status, fm_test);
	return (0);

pio_error:
	mcxnex_fm_ereport(state, HCA_SYS_ERR, HCA_ERR_IOCTL);
	return (EIO);
}

/*
 * mcxnex_flash_write_byte()
 */
static int
mcxnex_flash_write_byte(mcxnex_state_t *state, uint32_t addr, uchar_t data)
{
	uint32_t stat;
	int status = 0;
	int dword_addr;
	int byte_offset;
	int i;
	union {
		uint8_t		bytes[4];
		uint32_t	dword;
	} dword;

	switch (state->hs_fw_cmdset) {
	case HERMON_FLASH_AMD_CMDSET:
		/* Issue Flash Byte program command */
		mcxnex_flash_write(state, addr, 0xAA, &status);
		if (status != 0) {
			return (status);
		}

		mcxnex_flash_write(state, addr, 0x55, &status);
		if (status != 0) {
			return (status);
		}

		mcxnex_flash_write(state, addr, 0xA0, &status);
		if (status != 0) {
			return (status);
		}

		mcxnex_flash_write(state, addr, data, &status);
		if (status != 0) {
			return (status);
		}

		/* Wait for Write Byte to Complete */
		i = 0;
		do {
			drv_usecwait(1);
			stat = mcxnex_flash_read(state, addr & ~3, &status);
			if (status != 0) {
				return (status);
			}

			if (i == mcxnex_hw_flash_timeout_write) {
				cmn_err(CE_WARN,
				    "mcxnex_flash_write_byte: ACS write "
				    "timeout: addr: 0x%x, data: 0x%x\n",
				    addr, data);
				mcxnex_fm_ereport(state, HCA_SYS_ERR,
				    HCA_ERR_IOCTL);
				return (EIO);
			}
			i++;
		} while (data != ((stat >> ((3 - (addr & 3)) << 3)) & 0xFF));

		break;

	case HERMON_FLASH_INTEL_CMDSET:
		/* Issue Flash Byte program command */
		mcxnex_flash_write(state, addr, MCXNEX_HW_FLASH_ICS_WRITE,
		    &status);
		if (status != 0) {
			return (status);
		}
		mcxnex_flash_write(state, addr, data, &status);
		if (status != 0) {
			return (status);
		}

		/* Wait for Write Byte to Complete */
		i = 0;
		do {
			drv_usecwait(1);
			stat = mcxnex_flash_read(state, addr & ~3, &status);
			if (status != 0) {
				return (status);
			}

			if (i == mcxnex_hw_flash_timeout_write) {
				cmn_err(CE_WARN,
				    "mcxnex_flash_write_byte: ICS write "
				    "timeout: addr: %x, data: %x\n",
				    addr, data);
				mcxnex_fm_ereport(state, HCA_SYS_ERR,
				    HCA_ERR_IOCTL);
				return (EIO);
			}
			i++;
		} while ((stat & MCXNEX_HW_FLASH_ICS_READY) == 0);

		if (stat & MCXNEX_HW_FLASH_ICS_ERROR) {
			cmn_err(CE_WARN,
			    "mcxnex_flash_write_byte: ICS write cmd error: "
			    "addr: %x, data: %x\n",
			    addr, data);
			mcxnex_fm_ereport(state, HCA_SYS_ERR, HCA_ERR_IOCTL);
			return (EIO);
		}
		break;

	case HERMON_FLASH_SPI_CMDSET:
		/*
		 * Our lowest write granularity on SPI is a dword.
		 * To support this ioctl option, we can read in the
		 * dword that contains this byte, modify this byte,
		 * and write the dword back out.
		 */

		/* Determine dword offset and byte offset within the dword */
		byte_offset = addr & 3;
		dword_addr = addr - byte_offset;
#ifdef _LITTLE_ENDIAN
		byte_offset = 3 - byte_offset;
#endif

		/* Read in dword */
		if ((status = mcxnex_flash_read_quadlet(state, &dword.dword,
		    dword_addr)) != 0)
			break;

		/* Set "data" to the appopriate byte */
		dword.bytes[byte_offset] = data;

		/* Write modified dword back out */
		status = mcxnex_flash_spi_write_dword(state, dword_addr,
		    dword.dword);

		break;

	case HERMON_FLASH_UNKNOWN_CMDSET:
	default:
		cmn_err(CE_WARN,
		    "mcxnex_flash_write_byte: unknown cmd set: 0x%x\n",
		    state->hs_fw_cmdset);
		status = EINVAL;
		break;
	}

	return (status);
}

/*
 * mcxnex_flash_erase_sector()
 */
static int
mcxnex_flash_erase_sector(mcxnex_state_t *state, uint32_t sector_num)
{
	ddi_acc_handle_t	hdl;
	uint32_t addr;
	uint32_t stat;
	int status = 0;
	int i;

	/* initialize the FMA retry loop */
	mcxnex_pio_init(fm_loop_cnt, fm_status, fm_test);

	/* Get address from sector num */
	addr = sector_num << state->hs_fw_log_sector_sz;

	switch (state->hs_fw_cmdset) {
	case HERMON_FLASH_AMD_CMDSET:
		/* Issue Flash Sector Erase Command */
		mcxnex_flash_write(state, addr, 0xAA, &status);
		if (status != 0) {
			return (status);
		}

		mcxnex_flash_write(state, addr, 0x55, &status);
		if (status != 0) {
			return (status);
		}

		mcxnex_flash_write(state, addr, 0x80, &status);
		if (status != 0) {
			return (status);
		}

		mcxnex_flash_write(state, addr, 0xAA, &status);
		if (status != 0) {
			return (status);
		}

		mcxnex_flash_write(state, addr, 0x55, &status);
		if (status != 0) {
			return (status);
		}

		mcxnex_flash_write(state, addr, 0x30, &status);
		if (status != 0) {
			return (status);
		}

		/* Wait for Sector Erase to complete */
		i = 0;
		do {
			drv_usecwait(1);
			stat = mcxnex_flash_read(state, addr, &status);
			if (status != 0) {
				return (status);
			}

			if (i == mcxnex_hw_flash_timeout_erase) {
				cmn_err(CE_WARN,
				    "mcxnex_flash_erase_sector: "
				    "ACS erase timeout\n");
				mcxnex_fm_ereport(state, HCA_SYS_ERR,
				    HCA_ERR_IOCTL);
				return (EIO);
			}
			i++;
		} while (stat != 0xFFFFFFFF);
		break;

	case HERMON_FLASH_INTEL_CMDSET:
		/* Issue Flash Sector Erase Command */
		mcxnex_flash_write(state, addr, MCXNEX_HW_FLASH_ICS_ERASE,
		    &status);
		if (status != 0) {
			return (status);
		}

		mcxnex_flash_write(state, addr, MCXNEX_HW_FLASH_ICS_CONFIRM,
		    &status);
		if (status != 0) {
			return (status);
		}

		/* Wait for Sector Erase to complete */
		i = 0;
		do {
			drv_usecwait(1);
			stat = mcxnex_flash_read(state, addr & ~3, &status);
			if (status != 0) {
				return (status);
			}

			if (i == mcxnex_hw_flash_timeout_erase) {
				cmn_err(CE_WARN,
				    "mcxnex_flash_erase_sector: "
				    "ICS erase timeout\n");
				mcxnex_fm_ereport(state, HCA_SYS_ERR,
				    HCA_ERR_IOCTL);
				return (EIO);
			}
			i++;
		} while ((stat & MCXNEX_HW_FLASH_ICS_READY) == 0);

		if (stat & MCXNEX_HW_FLASH_ICS_ERROR) {
			cmn_err(CE_WARN,
			    "mcxnex_flash_erase_sector: "
			    "ICS erase cmd error\n");
			mcxnex_fm_ereport(state, HCA_SYS_ERR,
			    HCA_ERR_IOCTL);
			return (EIO);
		}
		break;

	case HERMON_FLASH_SPI_CMDSET:
		hdl = mcxnex_get_pcihdl(state);

		/* the FMA retry loop starts. */
		mcxnex_pio_start(state, hdl, pio_error, fm_loop_cnt, fm_status,
		    fm_test);

		/* Issue Write Enable */
		mcxnex_flash_spi_write_enable(state);

		/* Set the Address */
		mcxnex_flash_write_cfg(state, hdl, MCXNEX_HW_FLASH_SPI_ADDR,
		    addr & MCXNEX_HW_FLASH_SPI_ADDR_MASK);

		/* Issue Flash Sector Erase */
		mcxnex_flash_spi_exec_command(state, hdl,
		    MCXNEX_HW_FLASH_SPI_INSTR_PHASE_OFF |
		    MCXNEX_HW_FLASH_SPI_ADDR_PHASE_OFF |
		    ((uint32_t)(MCXNEX_HW_FLASH_SPI_SECTOR_ERASE) <<
		    MCXNEX_HW_FLASH_SPI_INSTR_SHIFT));

		/* the FMA retry loop ends. */
		mcxnex_pio_end(state, hdl, pio_error, fm_loop_cnt, fm_status,
		    fm_test);

		/* Wait for Sector Erase to complete */
		status = mcxnex_flash_spi_wait_wip(state);
		break;

	case HERMON_FLASH_UNKNOWN_CMDSET:
	default:
		cmn_err(CE_WARN,
		    "mcxnex_flash_erase_sector: unknown cmd set: 0x%x\n",
		    state->hs_fw_cmdset);
		status = EINVAL;
		break;
	}

	/* Reset the flash device */
	if (status == 0) {
		status = mcxnex_flash_reset(state);
	}
	return (status);

pio_error:
	mcxnex_fm_ereport(state, HCA_SYS_ERR, HCA_ERR_IOCTL);
	return (EIO);
}

/*
 * mcxnex_flash_erase_chip()
 */
static int
mcxnex_flash_erase_chip(mcxnex_state_t *state)
{
	uint32_t stat;
	uint_t size;
	int status = 0;
	int i;
	int num_sect;

	switch (state->hs_fw_cmdset) {
	case HERMON_FLASH_AMD_CMDSET:
		/* Issue Flash Chip Erase Command */
		mcxnex_flash_write(state, 0, 0xAA, &status);
		if (status != 0) {
			return (status);
		}

		mcxnex_flash_write(state, 0, 0x55, &status);
		if (status != 0) {
			return (status);
		}

		mcxnex_flash_write(state, 0, 0x80, &status);
		if (status != 0) {
			return (status);
		}

		mcxnex_flash_write(state, 0, 0xAA, &status);
		if (status != 0) {
			return (status);
		}

		mcxnex_flash_write(state, 0, 0x55, &status);
		if (status != 0) {
			return (status);
		}

		mcxnex_flash_write(state, 0, 0x10, &status);
		if (status != 0) {
			return (status);
		}

		/* Wait for Chip Erase to Complete */
		i = 0;
		do {
			drv_usecwait(1);
			stat = mcxnex_flash_read(state, 0, &status);
			if (status != 0) {
				return (status);
			}

			if (i == mcxnex_hw_flash_timeout_erase) {
				cmn_err(CE_WARN,
				    "mcxnex_flash_erase_chip: erase timeout\n");
				mcxnex_fm_ereport(state, HCA_SYS_ERR,
				    HCA_ERR_IOCTL);
				return (EIO);
			}
			i++;
		} while (stat != 0xFFFFFFFF);
		break;

	case HERMON_FLASH_INTEL_CMDSET:
	case HERMON_FLASH_SPI_CMDSET:
		/*
		 * These chips don't have a chip erase command, so erase
		 * all blocks one at a time.
		 */
		size = (0x1 << state->hs_fw_log_sector_sz);
		num_sect = state->hs_fw_device_sz / size;

		for (i = 0; i < num_sect; i++) {
			status = mcxnex_flash_erase_sector(state, i);
			if (status != 0) {
				cmn_err(CE_WARN,
				    "mcxnex_flash_erase_chip: "
				    "sector %d erase error\n", i);
				return (status);
			}
		}
		break;

	case HERMON_FLASH_UNKNOWN_CMDSET:
	default:
		cmn_err(CE_WARN, "mcxnex_flash_erase_chip: "
		    "unknown cmd set: 0x%x\n", state->hs_fw_cmdset);
		status = EINVAL;
		break;
	}

	return (status);
}

/*
 * mcxnex_flash_spi_write_enable()
 */
static void
mcxnex_flash_spi_write_enable(mcxnex_state_t *state)
{
	ddi_acc_handle_t	hdl;

	hdl = mcxnex_get_pcihdl(state);

	mcxnex_flash_spi_exec_command(state, hdl,
	    MCXNEX_HW_FLASH_SPI_INSTR_PHASE_OFF |
	    (MCXNEX_HW_FLASH_SPI_WRITE_ENABLE <<
	    MCXNEX_HW_FLASH_SPI_INSTR_SHIFT));
}

/*
 * mcxnex_flash_spi_wait_wip()
 */
static int
mcxnex_flash_spi_wait_wip(mcxnex_state_t *state)
{
	ddi_acc_handle_t	hdl;
	uint32_t		status;

	/* initialize the FMA retry loop */
	mcxnex_pio_init(fm_loop_cnt, fm_status, fm_test);

	hdl = mcxnex_get_pcihdl(state);

	/* the FMA retry loop starts. */
	mcxnex_pio_start(state, hdl, pio_error, fm_loop_cnt, fm_status,
	    fm_test);

	/* wait on the gateway to clear busy */
	do {
		status = mcxnex_flash_read_cfg(state, hdl,
		    MCXNEX_HW_FLASH_SPI_GW);
	} while (status & MCXNEX_HW_FLASH_SPI_BUSY);

	/* now, get the status and check for WIP to clear */
	do {
		mcxnex_flash_spi_exec_command(state, hdl,
		    MCXNEX_HW_FLASH_SPI_READ_OP |
		    MCXNEX_HW_FLASH_SPI_INSTR_PHASE_OFF |
		    MCXNEX_HW_FLASH_SPI_DATA_PHASE_OFF |
		    MCXNEX_HW_FLASH_SPI_TRANS_SZ_4B |
		    (MCXNEX_HW_FLASH_SPI_READ_STATUS_REG <<
		    MCXNEX_HW_FLASH_SPI_INSTR_SHIFT));

		status = mcxnex_flash_read_cfg(state, hdl,
		    MCXNEX_HW_FLASH_SPI_DATA);
	} while (status & MCXNEX_HW_FLASH_SPI_WIP);

	/* the FMA retry loop ends. */
	mcxnex_pio_end(state, hdl, pio_error, fm_loop_cnt, fm_status, fm_test);
	return (0);

pio_error:
	mcxnex_fm_ereport(state, HCA_SYS_ERR, HCA_ERR_IOCTL);
	return (EIO);
}

/*
 * mcxnex_flash_bank()
 */
static int
mcxnex_flash_bank(mcxnex_state_t *state, uint32_t addr)
{
	ddi_acc_handle_t	hdl;
	uint32_t		bank;

	/* initialize the FMA retry loop */
	mcxnex_pio_init(fm_loop_cnt, fm_status, fm_test);

	/* Set handle */
	hdl = mcxnex_get_pcihdl(state);

	/* Determine the bank setting from the address */
	bank = addr & MCXNEX_HW_FLASH_BANK_MASK;

	_NOTE(NOW_INVISIBLE_TO_OTHER_THREADS(state->hs_fw_flashbank))

	/*
	 * If the bank is different from the currently set bank, we need to
	 * change it.  Also, if an 'addr' of 0 is given, this allows the
	 * capability to force the flash bank to 0.  This is useful at init
	 * time to initially set the bank value
	 */
	if (state->hs_fw_flashbank != bank || addr == 0) {
		switch (state->hs_fw_cmdset) {
		case HERMON_FLASH_SPI_CMDSET:
			/* CMJ: not needed for mcxnex */
			break;

		case HERMON_FLASH_INTEL_CMDSET:
		case HERMON_FLASH_AMD_CMDSET:
			/* the FMA retry loop starts. */
			mcxnex_pio_start(state, hdl, pio_error, fm_loop_cnt,
			    fm_status, fm_test);

			mcxnex_flash_write_cfg(state, hdl,
			    MCXNEX_HW_FLASH_GPIO_DATACLEAR, 0x70);
			mcxnex_flash_write_cfg(state, hdl,
			    MCXNEX_HW_FLASH_GPIO_DATASET, (bank >> 15) & 0x70);

			/* the FMA retry loop ends. */
			mcxnex_pio_end(state, hdl, pio_error, fm_loop_cnt,
			    fm_status, fm_test);
			break;

		case HERMON_FLASH_UNKNOWN_CMDSET:
		default:
			return (EINVAL);
		}

		state->hs_fw_flashbank = bank;
	}
	return (0);

pio_error:
	mcxnex_fm_ereport(state, HCA_SYS_ERR, HCA_ERR_IOCTL);
	return (EIO);
}

/*
 * mcxnex_flash_spi_exec_command()
 */
static void
mcxnex_flash_spi_exec_command(mcxnex_state_t *state, ddi_acc_handle_t hdl,
    uint32_t cmd)
{
	uint32_t data;
	int timeout = 0;

	cmd |= MCXNEX_HW_FLASH_SPI_BUSY | MCXNEX_HW_FLASH_SPI_ENABLE_OFF;

	mcxnex_flash_write_cfg(state, hdl, MCXNEX_HW_FLASH_SPI_GW, cmd);

	do {
		data = mcxnex_flash_read_cfg(state, hdl,
		    MCXNEX_HW_FLASH_SPI_GW);
		timeout++;
	} while ((data & MCXNEX_HW_FLASH_SPI_BUSY) &&
	    (timeout < mcxnex_hw_flash_timeout_config));
}

/*
 * mcxnex_flash_read()
 */
static uint32_t
mcxnex_flash_read(mcxnex_state_t *state, uint32_t addr, int *err)
{
	ddi_acc_handle_t	hdl;
	uint32_t		data = 0;
	int			timeout, status = 0;

	/* initialize the FMA retry loop */
	mcxnex_pio_init(fm_loop_cnt, fm_status, fm_test);

	hdl = mcxnex_get_pcihdl(state);

	/* the FMA retry loop starts. */
	mcxnex_pio_start(state, hdl, pio_error, fm_loop_cnt, fm_status,
	    fm_test);

	switch (state->hs_fw_cmdset) {
	case HERMON_FLASH_SPI_CMDSET:
		/* Set the transaction address */
		mcxnex_flash_write_cfg(state, hdl, MCXNEX_HW_FLASH_SPI_ADDR,
		    (addr & MCXNEX_HW_FLASH_SPI_ADDR_MASK));

		mcxnex_flash_spi_exec_command(state, hdl,
		    MCXNEX_HW_FLASH_SPI_READ_OP |
		    MCXNEX_HW_FLASH_SPI_INSTR_PHASE_OFF |
		    MCXNEX_HW_FLASH_SPI_ADDR_PHASE_OFF |
		    MCXNEX_HW_FLASH_SPI_DATA_PHASE_OFF |
		    MCXNEX_HW_FLASH_SPI_TRANS_SZ_4B |
		    (MCXNEX_HW_FLASH_SPI_READ <<
		    MCXNEX_HW_FLASH_SPI_INSTR_SHIFT));

		data = mcxnex_flash_read_cfg(state, hdl,
		    MCXNEX_HW_FLASH_SPI_DATA);
		break;

	case HERMON_FLASH_INTEL_CMDSET:
	case HERMON_FLASH_AMD_CMDSET:
		/*
		 * The Read operation does the following:
		 *   1) Write the masked address to the HERMON_FLASH_ADDR
		 *	register. Only the least significant 19 bits are valid.
		 *   2) Read back the register until the command has completed.
		 *   3) Read the data retrieved from the address at the
		 *	HERMON_FLASH_DATA register.
		 */
		mcxnex_flash_write_cfg(state, hdl, MCXNEX_HW_FLASH_ADDR,
		    (addr & MCXNEX_HW_FLASH_ADDR_MASK) | (1 << 29));

		timeout = 0;
		do {
			data = mcxnex_flash_read_cfg(state, hdl,
			    MCXNEX_HW_FLASH_ADDR);
			timeout++;
		} while ((data & MCXNEX_HW_FLASH_CMD_MASK) &&
		    (timeout < mcxnex_hw_flash_timeout_config));

		data = mcxnex_flash_read_cfg(state, hdl, MCXNEX_HW_FLASH_DATA);
		break;

	case HERMON_FLASH_UNKNOWN_CMDSET:
	default:
		cmn_err(CE_CONT, "mcxnex_flash_read: unknown cmdset: 0x%x\n",
		    state->hs_fw_cmdset);
		status = EINVAL;
		break;
	}

	if (timeout == mcxnex_hw_flash_timeout_config) {
		cmn_err(CE_WARN, "mcxnex_flash_read: command timed out.\n");
		*err = EIO;
		mcxnex_fm_ereport(state, HCA_SYS_ERR, HCA_ERR_IOCTL);
		return (data);
	}

	/* the FMA retry loop ends. */
	mcxnex_pio_end(state, hdl, pio_error, fm_loop_cnt, fm_status, fm_test);
	*err = status;
	return (data);

pio_error:
	*err = EIO;
	mcxnex_fm_ereport(state, HCA_SYS_ERR, HCA_ERR_IOCTL);
	return (data);
}

/*
 * mcxnex_flash_write()
 */
static void
mcxnex_flash_write(mcxnex_state_t *state, uint32_t addr, uchar_t data, int *err)
{
	ddi_acc_handle_t	hdl;
	int			cmd;
	int			timeout;

	/* initialize the FMA retry loop */
	mcxnex_pio_init(fm_loop_cnt, fm_status, fm_test);

	hdl = mcxnex_get_pcihdl(state);

	/* the FMA retry loop starts. */
	mcxnex_pio_start(state, hdl, pio_error, fm_loop_cnt, fm_status,
	    fm_test);

	/*
	 * The Write operation does the following:
	 *   1) Write the data to be written to the HERMON_FLASH_DATA offset.
	 *   2) Write the address to write the data to to the HERMON_FLASH_ADDR
	 *	offset.
	 *   3) Wait until the write completes.
	 */

	mcxnex_flash_write_cfg(state, hdl, MCXNEX_HW_FLASH_DATA, data << 24);
	mcxnex_flash_write_cfg(state, hdl, MCXNEX_HW_FLASH_ADDR,
	    (addr & 0x7FFFF) | (2 << 29));

	timeout = 0;
	do {
		cmd = mcxnex_flash_read_cfg(state, hdl, MCXNEX_HW_FLASH_ADDR);
		timeout++;
	} while ((cmd & MCXNEX_HW_FLASH_CMD_MASK) &&
	    (timeout < mcxnex_hw_flash_timeout_config));

	if (timeout == mcxnex_hw_flash_timeout_config) {
		cmn_err(CE_WARN, "mcxnex_flash_write: config cmd timeout.\n");
		*err = EIO;
		mcxnex_fm_ereport(state, HCA_SYS_ERR, HCA_ERR_IOCTL);
		return;
	}

	/* the FMA retry loop ends. */
	mcxnex_pio_end(state, hdl, pio_error, fm_loop_cnt, fm_status, fm_test);
	*err = 0;
	return;

pio_error:
	*err = EIO;
	mcxnex_fm_ereport(state, HCA_SYS_ERR, HCA_ERR_IOCTL);
}

/*
 * mcxnex_flash_init()
 */
static int
mcxnex_flash_init(mcxnex_state_t *state)
{
	uint32_t		word;
	ddi_acc_handle_t	hdl;
	int			sema_cnt;
	int			gpio;

	/* initialize the FMA retry loop */
	mcxnex_pio_init(fm_loop_cnt, fm_status, fm_test);

	/* Set handle */
	hdl = mcxnex_get_pcihdl(state);

	/* the FMA retry loop starts. */
	mcxnex_pio_start(state, hdl, pio_error, fm_loop_cnt, fm_status,
	    fm_test);

	/* Init the flash */

#ifdef DO_WRCONF
	/*
	 * Grab the WRCONF semaphore.
	 */
	word = mcxnex_flash_read_cfg(state, hdl, MCXNEX_HW_FLASH_WRCONF_SEMA);
#endif

	/*
	 * Grab the GPIO semaphore.  This allows us exclusive access to the
	 * GPIO settings on the Mcxnex for the duration of the flash burning
	 * procedure.
	 */
	sema_cnt = 0;
	do {
		word = mcxnex_flash_read_cfg(state, hdl,
		    MCXNEX_HW_FLASH_GPIO_SEMA);
		if (word == 0) {
			break;
		}

		sema_cnt++;
		drv_usecwait(1);

	} while (sema_cnt < mcxnex_hw_flash_timeout_gpio_sema);

	/*
	 * Determine if we timed out trying to grab the GPIO semaphore
	 */
	if (sema_cnt == mcxnex_hw_flash_timeout_gpio_sema) {
		cmn_err(CE_WARN, "mcxnex_flash_init: GPIO SEMA timeout\n");
		cmn_err(CE_WARN, "GPIO_SEMA value: 0x%x\n", word);
		mcxnex_fm_ereport(state, HCA_SYS_ERR, HCA_ERR_IOCTL);
		return (EIO);
	}

	/* Save away original GPIO Values */
	state->hs_fw_gpio[0] = mcxnex_flash_read_cfg(state, hdl,
	    MCXNEX_HW_FLASH_GPIO_DATA);

	/* Set new GPIO value */
	gpio = state->hs_fw_gpio[0] | MCXNEX_HW_FLASH_GPIO_PIN_ENABLE;
	mcxnex_flash_write_cfg(state, hdl, MCXNEX_HW_FLASH_GPIO_DATA, gpio);

	/* Save away original GPIO Values */
	state->hs_fw_gpio[1] = mcxnex_flash_read_cfg(state, hdl,
	    MCXNEX_HW_FLASH_GPIO_MOD0);
	state->hs_fw_gpio[2] = mcxnex_flash_read_cfg(state, hdl,
	    MCXNEX_HW_FLASH_GPIO_MOD1);

	/* unlock GPIO */
	mcxnex_flash_write_cfg(state, hdl, MCXNEX_HW_FLASH_GPIO_LOCK,
	    MCXNEX_HW_FLASH_GPIO_UNLOCK_VAL);

	/*
	 * Set new GPIO values
	 */
	gpio = state->hs_fw_gpio[1] | MCXNEX_HW_FLASH_GPIO_PIN_ENABLE;
	mcxnex_flash_write_cfg(state, hdl, MCXNEX_HW_FLASH_GPIO_MOD0, gpio);

	gpio = state->hs_fw_gpio[2] & ~MCXNEX_HW_FLASH_GPIO_PIN_ENABLE;
	mcxnex_flash_write_cfg(state, hdl, MCXNEX_HW_FLASH_GPIO_MOD1, gpio);

	/* re-lock GPIO */
	mcxnex_flash_write_cfg(state, hdl, MCXNEX_HW_FLASH_GPIO_LOCK, 0);

	/* Set CPUMODE to enable mcxnex to access the flash device */
	/* CMJ This code came from arbel.  Mcxnex doesn't seem to need it. */
	/*
	 *	mcxnex_flash_write_cfg(state, hdl, MCXNEX_HW_FLASH_CPUMODE,
	 *	    1 << MCXNEX_HW_FLASH_CPU_SHIFT);
	 */

	/* the FMA retry loop ends. */
	mcxnex_pio_end(state, hdl, pio_error, fm_loop_cnt, fm_status, fm_test);
	return (0);

pio_error:
	mcxnex_fm_ereport(state, HCA_SYS_ERR, HCA_ERR_IOCTL);
	return (EIO);
}

/*
 * mcxnex_flash_cfi_init
 *   Implements access to the CFI (Common Flash Interface) data
 */
static int
mcxnex_flash_cfi_init(mcxnex_state_t *state, uint32_t *cfi_info,
    int *intel_xcmd)
{
	uint32_t	data;
	uint32_t	sector_sz_bytes;
	uint32_t	bit_count;
	uint8_t		cfi_ch_info[HERMON_CFI_INFO_SIZE];
	uint32_t	cfi_dw_info[HERMON_CFI_INFO_QSIZE];
	int		i;
	int		status;

	/* Right now, all mcxnex cards use SPI. */
	if (MCXNEX_IS_MAINTENANCE_MODE(state->hs_dip) ||
	    MCXNEX_IS_HCA_MODE(state->hs_dip)) {
		/*
		 * Don't use CFI for SPI part. Just fill in what we need
		 * and return.
		 */
		state->hs_fw_cmdset = HERMON_FLASH_SPI_CMDSET;
		state->hs_fw_log_sector_sz = HERMON_FLASH_SPI_LOG_SECTOR_SIZE;
		state->hs_fw_device_sz = HERMON_FLASH_SPI_DEVICE_SIZE;

		/*
		 * set this to inform caller of cmdset type.
		 */
		cfi_ch_info[0x13] = HERMON_FLASH_SPI_CMDSET;
		mcxnex_flash_cfi_dword(&cfi_info[4], cfi_ch_info, 0x10);
		return (0);
	}

	/*
	 * Determine if the user command supports the Intel Extended
	 * Command Set. The query string is contained in the fourth
	 * quad word.
	 */
	mcxnex_flash_cfi_byte(cfi_ch_info, cfi_info[0x04], 0x10);
	if (cfi_ch_info[0x10] == 'M' &&
	    cfi_ch_info[0x11] == 'X' &&
	    cfi_ch_info[0x12] == '2') {
		*intel_xcmd = 1; /* support is there */
		if (mcxnex_verbose) {
			cmn_err(CE_CONT, "?Support for Intel X is present\n");
		}
	}

	/* CFI QUERY */
	mcxnex_flash_write(state, 0x55, HERMON_FLASH_CFI_INIT, &status);
	if (status != 0) {
		return (status);
	}

	/* temporarily set the cmdset in order to do the initial read */
	state->hs_fw_cmdset = HERMON_FLASH_INTEL_CMDSET;

	/* Read in CFI data */
	for (i = 0; i < HERMON_CFI_INFO_SIZE; i += 4) {
		data = mcxnex_flash_read(state, i, &status);
		if (status != 0) {
			return (status);
		}
		cfi_dw_info[i >> 2] = data;
		mcxnex_flash_cfi_byte(cfi_ch_info, data, i);
	}

	/* Determine chip set */
	state->hs_fw_cmdset = HERMON_FLASH_UNKNOWN_CMDSET;
	if (cfi_ch_info[0x20] == 'Q' &&
	    cfi_ch_info[0x22] == 'R' &&
	    cfi_ch_info[0x24] == 'Y') {
		/*
		 * Mode: x16 working in x8 mode (Intel).
		 * Pack data - skip spacing bytes.
		 */
		if (mcxnex_verbose) {
			cmn_err(CE_CONT, "?x16 working in x8 mode (Intel)\n");
		}
		for (i = 0; i < HERMON_CFI_INFO_SIZE; i += 2) {
			cfi_ch_info[i/2] = cfi_ch_info[i];
		}
	}
	state->hs_fw_cmdset = cfi_ch_info[0x13];

	if (state->hs_fw_cmdset != HERMON_FLASH_INTEL_CMDSET &&
	    state->hs_fw_cmdset != HERMON_FLASH_AMD_CMDSET) {
		cmn_err(CE_WARN, "UNKNOWN chip cmd set 0x%04x",
		    state->hs_fw_cmdset);
		state->hs_fw_cmdset = HERMON_FLASH_UNKNOWN_CMDSET;
		return (0);
	}

	/* Determine total bytes in one sector size */
	sector_sz_bytes = ((cfi_ch_info[0x30] << 8) | cfi_ch_info[0x2F]) << 8;

	/* Calculate equivalent of log2 (n) */
	for (bit_count = 0; sector_sz_bytes > 1; bit_count++) {
		sector_sz_bytes >>= 1;
	}

	/* Set sector size */
	state->hs_fw_log_sector_sz = bit_count;

	/* Set flash size */
	state->hs_fw_device_sz = 0x1 << cfi_ch_info[0x27];

	/* Reset to turn off CFI mode */
	if ((status = mcxnex_flash_reset(state)) != 0)
		goto out;

	/* Pass CFI data back to user command. */
	for (i = 0; i < HERMON_FLASH_CFI_SIZE_QUADLET; i++) {
		mcxnex_flash_cfi_dword(&cfi_info[i], cfi_ch_info, i << 2);
	}

	if (*intel_xcmd == 1) {
		/*
		 * Inform the user cmd that this driver does support the
		 * Intel Extended Command Set.
		 */
		cfi_ch_info[0x10] = 'M';
		cfi_ch_info[0x11] = 'X';
		cfi_ch_info[0x12] = '2';
	} else {
		cfi_ch_info[0x10] = 'Q';
		cfi_ch_info[0x11] = 'R';
		cfi_ch_info[0x12] = 'Y';
	}
	cfi_ch_info[0x13] = state->hs_fw_cmdset;
	mcxnex_flash_cfi_dword(&cfi_info[0x4], cfi_ch_info, 0x10);
out:
	return (status);
}

/*
 * mcxnex_flash_fini()
 */
static int
mcxnex_flash_fini(mcxnex_state_t *state)
{
	int status;
	ddi_acc_handle_t hdl;

	/* initialize the FMA retry loop */
	mcxnex_pio_init(fm_loop_cnt, fm_status, fm_test);

	/* Set handle */
	hdl = mcxnex_get_pcihdl(state);

	if ((status = mcxnex_flash_bank(state, 0)) != 0)
		return (status);

	/* the FMA retry loop starts. */
	mcxnex_pio_start(state, hdl, pio_error, fm_loop_cnt, fm_status,
	    fm_test);

	/*
	 * Restore original GPIO Values
	 */
	mcxnex_flash_write_cfg(state, hdl, MCXNEX_HW_FLASH_GPIO_DATA,
	    state->hs_fw_gpio[0]);

	/* unlock GPIOs */
	mcxnex_flash_write_cfg(state, hdl, MCXNEX_HW_FLASH_GPIO_LOCK,
	    MCXNEX_HW_FLASH_GPIO_UNLOCK_VAL);

	mcxnex_flash_write_cfg(state, hdl, MCXNEX_HW_FLASH_GPIO_MOD0,
	    state->hs_fw_gpio[1]);
	mcxnex_flash_write_cfg(state, hdl, MCXNEX_HW_FLASH_GPIO_MOD1,
	    state->hs_fw_gpio[2]);

	/* re-lock GPIOs */
	mcxnex_flash_write_cfg(state, hdl, MCXNEX_HW_FLASH_GPIO_LOCK, 0);

	/* Give up gpio semaphore */
	mcxnex_flash_write_cfg(state, hdl, MCXNEX_HW_FLASH_GPIO_SEMA, 0);

	/* the FMA retry loop ends. */
	mcxnex_pio_end(state, hdl, pio_error, fm_loop_cnt, fm_status, fm_test);
	return (0);

pio_error:
	mcxnex_fm_ereport(state, HCA_SYS_ERR, HCA_ERR_IOCTL);
	return (EIO);
}

/*
 * mcxnex_flash_read_cfg
 */
static uint32_t
mcxnex_flash_read_cfg(mcxnex_state_t *state, ddi_acc_handle_t pci_config_hdl,
    uint32_t addr)
{
	uint32_t	read;

	if (do_bar0) {
		read = ddi_get32(mcxnex_get_cmdhdl(state), (uint32_t *)(void *)
		    (state->hs_reg_cmd_baseaddr + addr));
	} else {
		/*
		 * Perform flash read operation:
		 *   1) Place addr to read from on the MCXNEX_HW_FLASH_CFG_ADDR
		 *	register
		 *   2) Read data at that addr from the MCXNEX_HW_FLASH_CFG_DATA
		 *	 register
		 */
		pci_config_put32(pci_config_hdl, MCXNEX_HW_FLASH_CFG_ADDR,
		    addr);
		read = pci_config_get32(pci_config_hdl,
		    MCXNEX_HW_FLASH_CFG_DATA);
	}

	return (read);
}

#ifdef DO_WRCONF
static void
mcxnex_flash_write_cfg(mcxnex_state_t *state,
    ddi_acc_handle_t pci_config_hdl, uint32_t addr, uint32_t data)
{
	mcxnex_flash_write_cfg_helper(state, pci_config_hdl, addr, data);
	mcxnex_flash_write_confirm(state, pci_config_hdl);
}

static void
mcxnex_flash_write_confirm(mcxnex_state_t *state,
    ddi_acc_handle_t pci_config_hdl)
{
	uint32_t	sem_value = 1;

	mcxnex_flash_write_cfg_helper(state, pci_config_hdl,
	    MCXNEX_HW_FLASH_WRCONF_SEMA, 0);
	while (sem_value) {
		sem_value = mcxnex_flash_read_cfg(state, pci_config_hdl,
		    MCXNEX_HW_FLASH_WRCONF_SEMA);
	}
}
#endif

/*
 * mcxnex_flash_write_cfg
 */
static void
#ifdef DO_WRCONF
mcxnex_flash_write_cfg_helper(mcxnex_state_t *state,
    ddi_acc_handle_t pci_config_hdl, uint32_t addr, uint32_t data)
#else
mcxnex_flash_write_cfg(mcxnex_state_t *state,
    ddi_acc_handle_t pci_config_hdl, uint32_t addr, uint32_t data)
#endif
{
	if (do_bar0) {
		ddi_put32(mcxnex_get_cmdhdl(state), (uint32_t *)(void *)
		    (state->hs_reg_cmd_baseaddr + addr), data);

	} else {

		/*
		 * Perform flash write operation:
		 *   1) Place addr to write to on the MCXNEX_HW_FLASH_CFG_ADDR
		 *	register
		 *   2) Place data to write on to the MCXNEX_HW_FLASH_CFG_DATA
		 *	register
		 */
		pci_config_put32(pci_config_hdl, MCXNEX_HW_FLASH_CFG_ADDR,
		    addr);
		pci_config_put32(pci_config_hdl, MCXNEX_HW_FLASH_CFG_DATA,
		    data);
	}
}

/*
 * Support routines to convert Common Flash Interface (CFI) data
 * from a 32  bit word to a char array, and from a char array to
 * a 32 bit word.
 */
static void
mcxnex_flash_cfi_byte(uint8_t *ch, uint32_t dword, int i)
{
	ch[i] = (uint8_t)((dword & 0xFF000000) >> 24);
	ch[i+1] = (uint8_t)((dword & 0x00FF0000) >> 16);
	ch[i+2] = (uint8_t)((dword & 0x0000FF00) >> 8);
	ch[i+3] = (uint8_t)((dword & 0x000000FF));
}

static void
mcxnex_flash_cfi_dword(uint32_t *dword, uint8_t *ch, int i)
{
	*dword = (uint32_t)
	    ((uint32_t)ch[i] << 24 |
	    (uint32_t)ch[i+1] << 16 |
	    (uint32_t)ch[i+2] << 8 |
	    (uint32_t)ch[i+3]);
}
