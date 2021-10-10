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
 * Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"

#include <sys/types.h>
#include <sys/modctl.h>
#include <sys/debug.h>
#include <sys/note.h>
#include <sys/dada/adapters/ata_common.h>
#include <sys/dada/adapters/ata_disk.h>
#include <sys/dada/adapters/atapi.h>

#pragma weak plat_ide_chipreset

/*
 * This file contains the chip specific functions for the cmd chip
 */

static void
acersb_init_timing_tables(struct ata_controller   *ata_ctlp);
static int
acersb_get_intr_status(struct ata_controller   *ata_ctlp, int chno);
static void
acersb_program_timing_reg(struct ata_drive *ata_drvp);
static int
acersb_get_speed_capabilities(struct ata_controller   *ata_ctlp, int chno);
static void
acersb_null_func();
static int
acersb_power_mgmt_initialize(void);
static int
acersb_power_entry_point(struct ata_controller *ata_ctlp, int component,
			int level);
static void
acersb_nien_toggle(struct ata_controller *ata_ctlp, int chno, uint8_t cmd);
static void
acersb_reset_chip(struct ata_controller   *ata_ctlp, int chno);

/*
 * Local Functions
 */
static void
acersb_program_mode(struct ata_controller   *ata_ctlp, int umode,
		int targ, uchar_t reg, int mode);
static void
acersb_disable_ultra_mode(struct ata_controller   *ata_ctlp, int targ);

/*
 * Extern functions.
 */
extern void ata_raise_power(struct ata_controller *ata_ctlp);
extern int ata_lower_power(struct ata_controller *ata_ctlp);
extern int plat_ide_chipreset(dev_info_t *dip, int chno);

/*
 * acersb_init is for initializing the function pointers of the
 * ata_controller structure to the chip specific functions.
 */
void
acersb_init(struct ata_controller   *ata_ctlp)
{
	uchar_t	val;

	ata_ctlp->init_timing_tables = acersb_init_timing_tables;
	ata_ctlp->program_read_ahead = acersb_null_func;
	ata_ctlp->clear_interrupt = acersb_null_func;
	ata_ctlp->get_intr_status = acersb_get_intr_status;
	ata_ctlp->program_timing_reg = acersb_program_timing_reg;
	ata_ctlp->get_speed_capabilities = acersb_get_speed_capabilities;
	ata_ctlp->enable_channel = acersb_null_func;
	ata_ctlp->disable_intr = acersb_null_func;
	ata_ctlp->enable_intr = acersb_null_func;
	ata_ctlp->power_mgmt_initialize = acersb_power_mgmt_initialize;
	ata_ctlp->power_entry_point = acersb_power_entry_point;
	ata_ctlp->nien_toggle = acersb_nien_toggle;

	/*
	 * Put the default values of chip here as they are not default
	 * OBP modifies these before giving control to the driver.
	 */

	/*
	 * Initialize for 1573 controller
	 */

	if (ata_ctlp->ac_revision >= ASB_1573_REV) {
		ddi_put8(ata_ctlp->ata_conf_handle,
		    (uchar_t *)ata_ctlp->ata_conf_addr + CONTROL0, 0x64);
		ddi_put8(ata_ctlp->ata_conf_handle,
		    (uchar_t *)ata_ctlp->ata_conf_addr + CONTROL1, 0xCB);
		ata_ctlp->reset_chip = acersb_null_func;

	} else {
		val = ddi_get8(ata_ctlp->ata_conf_handle,
		    (uchar_t *)ata_ctlp->ata_conf_addr + CONTROL0) & 0x3;
		ddi_put8(ata_ctlp->ata_conf_handle,
		    (uchar_t *)ata_ctlp->ata_conf_addr + CONTROL0,
		    0x20 | val);
		ddi_put8(ata_ctlp->ata_conf_handle,
		    (uchar_t *)ata_ctlp->ata_conf_addr + CONTROL1, 0xCC);
		ata_ctlp->reset_chip = acersb_reset_chip;

	}

	ddi_put8(ata_ctlp->ata_conf_handle,
	    (uchar_t *)ata_ctlp->ata_conf_addr + CONTROL2, 0x03);
	ddi_put8(ata_ctlp->ata_conf_handle,
	    (uchar_t *)ata_ctlp->ata_conf_addr + PRIUDMA, 0x44);
	ddi_put8(ata_ctlp->ata_conf_handle,
	    (uchar_t *)ata_ctlp->ata_conf_addr + SECUDMA, 0x44);

}

/*
 * Functions for initializing the timing tables for cmd
 */
static void
acersb_init_timing_tables(struct ata_controller   *ata_ctlp)
{

	ata_ctlp->ac_piortable[0] = (uchar_t)ASBPIO0;
	ata_ctlp->ac_piortable[1] = (uchar_t)ASBPIO1;
	ata_ctlp->ac_piortable[2] = (uchar_t)ASBPIO2;
	ata_ctlp->ac_piortable[3] = (uchar_t)ASBPIO3;
	ata_ctlp->ac_piortable[4] = (uchar_t)ASBPIO4;
	ata_ctlp->ac_piowtable[0] = (uchar_t)ASBADP0;
	ata_ctlp->ac_piowtable[1] = (uchar_t)ASBADP1;
	ata_ctlp->ac_piowtable[2] = (uchar_t)ASBADP2;
	ata_ctlp->ac_piowtable[3] = (uchar_t)ASBADP3;
	ata_ctlp->ac_piowtable[4] = (uchar_t)ASBADP4;
	ata_ctlp->ac_dmartable[0] = (uchar_t)ASBDMA0;
	ata_ctlp->ac_dmartable[1] = (uchar_t)ASBDMA1;
	ata_ctlp->ac_dmartable[2] = (uchar_t)ASBDMA2;
	ata_ctlp->ac_dmawtable[0] = (uchar_t)ASBADM0;
	ata_ctlp->ac_dmawtable[1] = (uchar_t)ASBADM1;
	ata_ctlp->ac_dmawtable[2] = (uchar_t)ASBADM2;

	ata_ctlp->ac_udmatable[0] = (uchar_t)ASBUDMA0;
	ata_ctlp->ac_udmatable[1] = (uchar_t)ASBUDMA1;
	ata_ctlp->ac_udmatable[2] = (uchar_t)ASBUDMA2;
	ata_ctlp->ac_udmatable[3] = (uchar_t)ASBUDMA3;
	ata_ctlp->ac_udmatable[4] = (uchar_t)ASBUDMA4;
	ata_ctlp->ac_udmatable[5] = (uchar_t)ASBUDMA5;
	ata_ctlp->ac_udmatable[6] = (uchar_t)ASBUDMA6;
}

int acersb_intr_spin_period = 100;  /* (usec) */
int acersb_intr_spin_count = 200000;

/*
 *  Wait for interrupt on an IDE Channel
 */
static int
acersb_get_intr_status(struct ata_controller   *ata_ctlp, int chno)
{
	int period = acersb_intr_spin_period;
	int count = acersb_intr_spin_count;

	if (ata_wait(ata_ctlp->ata_datap1[chno], ata_ctlp->ac_altstatus[chno],
	    0, ATS_BSY, IGN_ERR, period, count) == FAILURE) {
		return (FAILURE);
	}

	return (SUCCESS);
}

/*
 * Routine which writes the timing and control values
 * for different mode of operations for acer southbridge timing registers.
 */
static void
acersb_program_timing_reg(struct ata_drive *ata_drvp)
{
	uchar_t	rd_par, wr_par;
	struct ata_controller *ata_ctlp = ata_drvp->ad_ctlp;
	int	targ = ata_drvp->ad_targ, offset, mode = 0;
	uchar_t val;

	if (ata_drvp->ad_piomode != 0x7f) {
		/* PIOMODE */
		if (!ata_drvp->ad_run_ultra) {
			rd_par = ata_ctlp->ac_piortable[ata_drvp->ad_piomode];
			wr_par = ata_ctlp->ac_piowtable[ata_drvp->ad_piomode];
		}
		mode = 0;
	} else {
		/* DMAMODE */
		if (!ata_drvp->ad_run_ultra) {
			rd_par =
			    ata_ctlp->ac_dmartable[ata_drvp->ad_dmamode & 0x03];
			wr_par =
			    ata_ctlp->ac_dmawtable[ata_drvp->ad_dmamode & 0x03];
		}

		mode = 1;
	}

	switch (targ) {

		case 0:
			if (ata_drvp->ad_run_ultra) {
				/*
				 * Program the drive in ultra mode.
				 */
				acersb_program_mode(ata_ctlp,
				    ata_drvp->ad_dmamode & DMA_BITS, 0,
				    0x56, 2);

			} else {

				offset = 0x5A;
				acersb_program_mode(ata_ctlp,
				    0, 0, 0x54, mode);
			}
			break;
		case 1:
			if (ata_drvp->ad_run_ultra) {
				acersb_program_mode(ata_ctlp,
				    ata_drvp->ad_dmamode & DMA_BITS, 1,
				    0x56, 2);
			} else {

				offset = 0x5B;
				acersb_program_mode(ata_ctlp,
				    0, 1, 0x54, mode);
			}
			break;
		case 2:
			if (ata_drvp->ad_run_ultra) {
				acersb_program_mode(ata_ctlp,
				    ata_drvp->ad_dmamode & DMA_BITS, 2,
				    0x57, 2);
			} else {

				offset = 0x5E;
				acersb_program_mode(ata_ctlp,
				    0, 2, 0x55, mode);
			}
			break;
		case 3:
			if (ata_drvp->ad_run_ultra) {
				acersb_program_mode(ata_ctlp,
				    ata_drvp->ad_dmamode & DMA_BITS, 3,
				    0x57, 2);
			} else {
				offset = 0x5F;
				acersb_program_mode(ata_ctlp,
				    0, 3, 0x55, mode);
			}
			break;
		default:
			cmn_err(CE_PANIC,
			"\n Invalid target value passed to acer timing init\n");
	}
	if (!ata_drvp->ad_run_ultra) {

		/*
		 * Disable ultra dma for this target
		 */
		acersb_disable_ultra_mode(ata_ctlp, targ);
		ddi_put8(ata_ctlp->ata_conf_handle,
		    (uchar_t *)ata_ctlp->ata_conf_addr + offset, rd_par);
		ddi_put8(ata_ctlp->ata_conf_handle,
		    (uchar_t *)ata_ctlp->ata_conf_addr + 0x58, wr_par);
		ddi_put8(ata_ctlp->ata_conf_handle,
		    (uchar_t *)ata_ctlp->ata_conf_addr + 0x5C, wr_par);
	} else {
		if ((ata_drvp->ad_dmamode & DMA_BITS) > 2) {
			/*
			 * Switch ON the Ultra support.
			 */
			val = ddi_get8(ata_ctlp->ata_conf_handle,
			    (uchar_t *)ata_ctlp->ata_conf_addr + 0x4B);
			if (!(val & 0x01)) {
				ddi_put8(ata_ctlp->ata_conf_handle,
				    (uchar_t *)ata_ctlp->ata_conf_addr + 0x4B,
				    0xCD);
			}
		}
	}
}


/*
 * This function takes the target number and pointer to ata controller
 * and checks if the ultra dma is enabled for this target.If it is
 * enabled then it disables the ultra dma mode.
 * Offset Reg#56 bit 3 controls primary master drives' ultra mode
 * Offset Reg#56 bit 7 controls primary slave drive's ultra dma mode
 * Offset Reg#57 bit 3 controls secondary master drives' ultra mode
 * Offset Reg#57 bit 7 controls secondary slave drives' ultra mode
 * If bit set it means Ultra dma enabled.
 */

static void
acersb_disable_ultra_mode(struct ata_controller   *ata_ctlp, int targ)
{
	uchar_t val, reg, andfactor = 0x08;

	/*
	 * For channel 0 the offset is 0x56
	 * For channel 1 the offset is 0x57
	 */
	if (targ & 0x2)
		/*
		 * i.e for target 2 and target 3
		 */
		reg = 0x57;
	else
		reg = 0x56;

	/*
	 * Read the values from the offset register,reg,
	 */
	val = ddi_get8(ata_ctlp->ata_conf_handle,
	    (uchar_t *)ata_ctlp->ata_conf_addr + reg);
	if (targ%2) {

		/*
		 * For slave device
		 */
		if (val & (andfactor << 4)) {
			ddi_put8(ata_ctlp->ata_conf_handle,
			    (uchar_t *)(ata_ctlp->ata_conf_addr + reg),
			    val & (~(andfactor << 4)));
		}
	} else {
		/*
		 * For Master device
		 */

		if (val & andfactor) {
			ddi_put8(ata_ctlp->ata_conf_handle,
			    (uchar_t *)(ata_ctlp->ata_conf_addr + reg),
			    val & (~andfactor));
		}
	}
}


/*
 * A return value of 3 indicates UDMA100 possible.
 * A return value of 2 indicates UDMA66 possible.
 * A value of 1 indicates UDMA33 possible.
 * A value of 0 indicates only DMA0-2 possible.
 */
static int
acersb_get_speed_capabilities(struct ata_controller   *ata_ctlp, int chno)
{
	int	ret = 0;
	uchar_t val;

	/*
	 * For M1573 and M1575 controllers there is no cable
	 * type detection capability. So forcing cable
	 * detected type to be 40-pin
	 */

	if (ata_ctlp->ac_revision >= ASB_1573_REV) {
		return (1);
	}

	/*
	 * First check the information provided by the host controller.
	 */
	val = ddi_get8(ata_ctlp->ata_conf_handle,
	    (uchar_t *)ata_ctlp->ata_conf_addr + 0x4A);
	if (chno == 0) {
		val &= 0x01;
	} else {
		val &= 0x02;
	}

	if (val) {
		/*
		 * Host detected a 40 pin cable - return 1.
		 */
		ret = 1;
	} else {
		/*
		 * Host detected a 80 pin cable - if the
		 * controller supports UDMA5 return 3
		 * otherwise return 2 for UDMA4.
		 */
		if (ata_ctlp->ac_revision < ASBREV) {
			ret = 2;
		} else {
			ret = 3;
		}
	}

	return (ret);
}

static void
acersb_null_func()
{
}

/*
 * Local functions to be used just in this file
 */

/*
 * This routine programs the programming mode registers
 * The registers which can be programmed are 0x56, 0x57, 0x54, 0x55
 * mode 0 = PIO, mode 1 = DMA, mode 2 = ultra DMA
 */
static void
acersb_program_mode(struct ata_controller   *ata_ctlp, int umode,
			int targ, uchar_t reg, int mode)
{
	uchar_t val, val1, andfactor, offset;

	switch (mode) {
		case 0:
			val = 0x04;
			break;
		case 1:
			val = 0x08;
			break;
		case 2:
			val = ata_ctlp->ac_udmatable[umode];
	}

	if ((targ == 1) || (targ == 3)) {
		andfactor = 0x0F;
		val = val << 0x4;
		val1 = 0x40;
	} else {
		andfactor = 0xF0;
		val1 = 0x20;
	}

	val |= ddi_get8(ata_ctlp->ata_conf_handle,
	    (uchar_t *)ata_ctlp->ata_conf_addr + reg) & andfactor;
	ddi_put8(ata_ctlp->ata_conf_handle,
	    (uchar_t *)ata_ctlp->ata_conf_addr + reg, val);

	if (mode) {
		/*
		 * Indicate in DMA programming regs that the drive is DMA
		 * capable
		 */
		if (targ < 2)
			offset = 0x2;
		else
			offset = 0xA;

		val = (ddi_get8(ata_ctlp->ata_cs_handle,
		    (uchar_t *)(ata_ctlp->ata_cs_addr + offset)));
		ddi_put8(ata_ctlp->ata_cs_handle,
		    (uchar_t *)(ata_ctlp->ata_cs_addr + offset),
		    val | val1);
	}
}

static int
acersb_power_mgmt_initialize()
{
	/*
	 * Set Bit 0 and Bit 3 to 1 to indicate support for D0 and D3 state.
	 */
	return (0x9);
}



static int
acersb_power_entry_point(struct ata_controller *ata_ctlp, int component,
			int level)
{
	uchar_t val;

	if (component != 0)
		return (FAILURE);
	/*
	 * Get the current power level from Reg. offset 0x64
	 */
	val = ddi_get8(ata_ctlp->ata_conf_handle,
	    (uchar_t *)ata_ctlp->ata_conf_addr + 0x64);

	if (level == ATA_POWER_D0) {
		/*
		 * Request is for raising the power level of the controller.
		 */
		if ((val & 0x3) == 0x00) {
			ata_ctlp->ac_power_level = ATA_POWER_D0;
			return (SUCCESS);
		}
		/*
		 * Raise the power level on the chip.
		 */
		ddi_put8(ata_ctlp->ata_conf_handle,
		    (uchar_t *)ata_ctlp->ata_conf_addr + 0x64, val & 0xfC);

		/*
		 * Provide a 10ms delay as per pci bus power mgmt spec rev 1.1
		 */
		drv_usecwait(10000);

		/*
		 * Set 0 to 70+02 bit 7 to indicate non-simplex
		 */
		ddi_put8(ata_ctlp->ata_cs_handle,
		    (uchar_t *)(ata_ctlp->ata_cs_addr + 2), 0x00);
		ddi_put8(ata_ctlp->ata_cs_handle,
		    (uchar_t *)(ata_ctlp->ata_cs_addr + 0x0A), 0x00);

		/*
		 * Invoke the generic code for raise power condition
		 */
		ata_raise_power(ata_ctlp);
		ata_ctlp->ac_power_level = ATA_POWER_D0;
	} else if (level == ATA_POWER_D3) {
		/*
		 * Invoke the generic code for lower power condition
		 */
		if (ata_lower_power(ata_ctlp) == FAILURE)
			return (FAILURE);
		/*
		 * Issue a command to take the chip to D3.
		 */

		ddi_put8(ata_ctlp->ata_conf_handle,
		    (uchar_t *)ata_ctlp->ata_conf_addr + 0x64, val | 0x3);
		/*
		 * Provide a 10ms delay as per pci bus power mgmt spec rev 1.1
		 */
		drv_usecwait(10000);
		ata_ctlp->ac_power_level = ATA_POWER_D3;

	} else {
		/*
		 * Unknown power level
		 */
		cmn_err(CE_WARN,
		"ata_controller - Invalid power level %x for IDE controller\n",
		    level);
	}

	return (SUCCESS);
}

static void
acersb_nien_toggle(struct ata_controller *ata_ctlp, int chno, uint8_t cmd)
{
	ddi_put8(ata_ctlp->ata_datap1[chno], ata_ctlp->ac_devctl[chno], cmd);
}


/*
 * This routine will reset the southbridge channel - specified through chno.
 * Register used - 0x51 in pci config space of SB.
 */
static void
acersb_reset_chip(struct ata_controller   *ata_ctlp, int chno)
{
	int	ret = DDI_FAILURE;

	if (chno == 0) {
		/*
		 * Reset channel 1 by writing 1 to bit 4 for reg 0x51.
		 */
		ddi_put8(ata_ctlp->ata_conf_handle,
		    (uchar_t *)ata_ctlp->ata_conf_addr + 0x51, 0x10);
		/*
		 * Platform specific module to do the reset.
		 */
		if (&plat_ide_chipreset)
			ret = plat_ide_chipreset(ata_ctlp->ac_dip, chno);
	} else if (chno == 1) {
		/*
		 * Reset channel 2 by writing 1 to bit 5 for reg 0x51.
		 */
		ddi_put8(ata_ctlp->ata_conf_handle,
		    (uchar_t *)ata_ctlp->ata_conf_addr + 0x51, 0x20);
		if (&plat_ide_chipreset)
			ret = plat_ide_chipreset(ata_ctlp->ac_dip, chno);
	}

	if (ret == DDI_FAILURE)
		cmn_err(CE_PANIC,
		    "Could not reset the IDE core of SouthBridge\n");
}
