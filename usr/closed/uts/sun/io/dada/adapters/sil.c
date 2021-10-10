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
 * Copyright 2006 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"

/*
 * This file contains the code for Chip specific routines for
 * Sil 0680A controller.
 */

#include <sys/types.h>
#include <sys/modctl.h>
#include <sys/debug.h>
#include <sys/note.h>
#include <sys/dada/adapters/ata_common.h>
#include <sys/dada/adapters/ata_disk.h>
#include <sys/dada/adapters/atapi.h>

/*
 * Prototype of all the chip specific routines.
 */
static void
sil_init_timing_tables(struct ata_controller   *ata_ctlp);
static int
sil_get_intr_status(struct ata_controller   *ata_ctlp, int chno);
static void
sil_program_timing_reg(struct ata_drive *ata_drvp);
static void
sil_disable_intr(struct ata_controller   *ata_ctlp, int chno);
static void
sil_enable_intr(struct ata_controller   *ata_ctlp, int chno);
static int
sil_get_speed_capabilities(struct ata_controller   *ata_ctlp, int chno);
static int
sil_power_entry_point(struct ata_controller *ata_ctlp, int component,
		int level);
static int
sil_power_mgmt_initialize(void);
static void
sil_null_func();
#ifndef __lint
static void
sil_nien_toggle(struct ata_controller *ata_ctlp, int chno,
	uint8_t cmd);
#endif

/*
 * Extern functions.
 */
extern void ata_raise_power(struct ata_controller *ata_ctlp);
extern int ata_lower_power(struct ata_controller *ata_ctlp);

/*
 * sil_init is for initializing the function pointers of the
 * ata_controller structure to the chip specific functions.
 */
void
sil_init(struct ata_controller   *ata_ctlp)
{
	ata_ctlp->init_timing_tables = sil_init_timing_tables;
	ata_ctlp->program_read_ahead = sil_null_func;
	ata_ctlp->clear_interrupt = sil_null_func;
	ata_ctlp->get_intr_status = sil_get_intr_status;
	ata_ctlp->program_timing_reg = sil_program_timing_reg;
	ata_ctlp->enable_channel = sil_null_func;
	ata_ctlp->disable_intr = sil_disable_intr;
	ata_ctlp->enable_intr = sil_enable_intr;
	ata_ctlp->get_speed_capabilities = sil_get_speed_capabilities;
#ifndef __lint
	ata_ctlp->nien_toggle = sil_nien_toggle;
#endif
	ata_ctlp->power_entry_point = sil_power_entry_point;
	ata_ctlp->reset_chip = sil_null_func;
	ata_ctlp->power_mgmt_initialize = sil_power_mgmt_initialize;
}

/*
 * Functions for initializing the timing tables for cmd
 */
static void
sil_init_timing_tables(struct ata_controller   *ata_ctlp)
{
	ata_ctlp->ac_piortable[0] = (uchar_t)SPIOR0;
	ata_ctlp->ac_piortable[1] = (uchar_t)SPIOR1;
	ata_ctlp->ac_piortable[2] = (uchar_t)SPIOR2;
	ata_ctlp->ac_piortable[3] = (uchar_t)SPIOR3;
	ata_ctlp->ac_piortable[4] = (uchar_t)SPIOR4;
	ata_ctlp->ac_piowtable[0] = (uchar_t)(SPIOR0 >> 8);
	ata_ctlp->ac_piowtable[1] = (uchar_t)(SPIOR1 >> 8);
	ata_ctlp->ac_piowtable[2] = (uchar_t)(SPIOR2 >> 8);
	ata_ctlp->ac_piowtable[3] = (uchar_t)(SPIOR3 >> 8);
	ata_ctlp->ac_piowtable[4] = (uchar_t)(SPIOR4 >> 8);
	ata_ctlp->ac_dmartable[0] = (uchar_t)SDMAR0;
	ata_ctlp->ac_dmartable[1] = (uchar_t)SDMAR1;
	ata_ctlp->ac_dmartable[2] = (uchar_t)SDMAR2;
	ata_ctlp->ac_dmawtable[0] = (uchar_t)(SDMAR0 >> 8);
	ata_ctlp->ac_dmawtable[1] = (uchar_t)(SDMAR1 >> 8);
	ata_ctlp->ac_dmawtable[2] = (uchar_t)(SDMAR2 >> 8);
	ata_ctlp->ac_udmatable[0] = (uchar_t)SUDMA0;
	ata_ctlp->ac_udmatable[1] = (uchar_t)SUDMA1;
	ata_ctlp->ac_udmatable[2] = (uchar_t)SUDMA2;
	ata_ctlp->ac_udmatable[3] = (uchar_t)SUDMA3;
	ata_ctlp->ac_udmatable[4] = (uchar_t)SUDMA4;

	ata_ctlp->ac_udmatable[5] = (uchar_t)SUDMA5;
	ata_ctlp->ac_udmatable[6] = (uchar_t)SUDMA6;
}

/*
 * A return value of 3 indicates speeds up to UDMA100 possible.
 * A value of 1 indicates UDMA33 possible
 */
static int
sil_get_speed_capabilities(struct ata_controller   *ata_ctlp, int chno)
{
	uchar_t	val;

	/* Check for 80-pin cable */
	if (chno)
		val = ddi_get8(ata_ctlp->ata_conf_handle,
		    (uchar_t *)(ata_ctlp->ata_conf_addr + 0xb0));
	else
		val = ddi_get8(ata_ctlp->ata_conf_handle,
		    (uchar_t *)(ata_ctlp->ata_conf_addr + 0xa0));

	if (!(val & 1)) {
		return (1); /* 40 pin cable */
	}

	return (3); /* 80 pin cable - UDMA5 */
}

static int
sil_power_entry_point(struct ata_controller *ata_ctlp, int component,
			int level)
{
	uchar_t val;

	if (component != 0)
		return (FAILURE);
	/*
	 * Get the current power level from Reg. offset 0x64
	 */
	val = ddi_get8(ata_ctlp->ata_conf_handle,
			(uchar_t *)(ata_ctlp->ata_conf_addr + 0x64));

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
		(uchar_t *)(ata_ctlp->ata_conf_addr + 0x64), val & 0xfc);

		/*
		 * Provide a 10ms delay as per pci bus power mgmt spec rev 1.1
		 */
		drv_usecwait(10000);

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
		(uchar_t *)(ata_ctlp->ata_conf_addr + 0x64), val | 0x3);
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
		    "ata_controller(SIL) - Invalid power level %x for "
		    "{IDE controller\n", level);
	}

	return (SUCCESS);
}

int sil_intr_spin_period = 100;   /* (usec) */
int sil_intr_spin_count = 200000;

/*
 *  Wait for interrupt on an IDE Channel
 */
static int
sil_get_intr_status(struct ata_controller   *ata_ctlp, int chno)
{
	int period = sil_intr_spin_period;
	int count = sil_intr_spin_count;

	if (ata_wait(ata_ctlp->ata_datap1[chno], ata_ctlp->ac_altstatus[chno],
	    0, ATS_BSY, IGN_ERR, period, count) == FAILURE)
		return (FAILURE);

	return (SUCCESS);
}

/*
 * Routine which writes the timing and control values
 */
static void
sil_program_timing_reg(struct ata_drive *ata_drvp)
{
	struct ata_controller *ata_ctlp = ata_drvp->ad_ctlp;
	ushort_t	timing_val, timing, offset;
	uchar_t		controller_mode;
	int		targ = ata_drvp->ad_targ;

	if (ata_drvp->ad_piomode != 0x7f) {
	/* PIOMODE */
		timing_val = ata_ctlp->ac_piortable[ata_drvp->ad_piomode] |
		    (ata_ctlp->ac_piowtable[ata_drvp->ad_piomode] << 8);

		switch (targ) {
		case 0:
			ddi_put16(ata_ctlp->ata_conf_handle,
			    (ushort_t *)(ata_ctlp->ata_conf_addr + 0xa4),
			    timing_val);
			break;
		case 1:
			ddi_put16(ata_ctlp->ata_conf_handle,
			    (ushort_t *)(ata_ctlp->ata_conf_addr + 0xa6),
			    timing_val);
			break;
		case 2:
			ddi_put16(ata_ctlp->ata_conf_handle,
			    (ushort_t *)(ata_ctlp->ata_conf_addr + 0xb4),
			    timing_val);
			break;
		case 3:
			ddi_put16(ata_ctlp->ata_conf_handle,
			    (ushort_t *)(ata_ctlp->ata_conf_addr + 0xb6),
			    timing_val);
			break;
		}

		if (ata_drvp->ad_piomode >= 3)
			timing = 0x01; /* Use IORDY monitoring */
		else
			timing = 0x00;
	} else {
		if (ata_drvp->ad_run_ultra) {
			/* Ultra-DMA */

			timing_val = ata_ctlp->ac_udmatable[ata_drvp->ad_dmamode
			    & 0x1f];

			timing = 0x3;

			switch (targ) {
			case 0:
				ddi_put16(ata_ctlp->ata_conf_handle,
				    (ushort_t *)(ata_ctlp->ata_conf_addr +
				    0xac), timing_val);
				break;
			case 1:
				ddi_put16(ata_ctlp->ata_conf_handle,
				    (ushort_t *)(ata_ctlp->ata_conf_addr +
				    + 0xae), timing_val);
				break;
			case 2:
				ddi_put16(ata_ctlp->ata_conf_handle,
				    (ushort_t *)(ata_ctlp->ata_conf_addr +
				    0xbc), timing_val);
				break;
			case 3:
				ddi_put16(ata_ctlp->ata_conf_handle,
				    (ushort_t *)(ata_ctlp->ata_conf_addr +
				    0xbe), timing_val);
				break;
			}
		} else {
			/* Multi-Word DMA */

			timing_val = ata_ctlp->ac_dmartable[ata_drvp->ad_dmamode
			    & 3] | (ata_ctlp->ac_dmawtable[ata_drvp->ad_dmamode
			    & 3] << 8);

			timing = 2;

			switch (targ) {
			case 0:
				ddi_put16(ata_ctlp->ata_conf_handle,
				    (ushort_t *)(ata_ctlp->ata_conf_addr +
				    0xa8), timing_val);
				break;
			case 1:
				ddi_put16(ata_ctlp->ata_conf_handle,
				    (ushort_t *)(ata_ctlp->ata_conf_addr +
				    + 0xaa), timing_val);
				break;
			case 2:
				ddi_put16(ata_ctlp->ata_conf_handle,
				    (ushort_t *)(ata_ctlp->ata_conf_addr +
				    0xb8), timing_val);
				break;
			case 3:
				ddi_put16(ata_ctlp->ata_conf_handle,
				    (ushort_t *)(ata_ctlp->ata_conf_addr +
				    0xba), timing_val);
				break;
			}
		}
	}

	if (targ < 2)
		offset = 0x80;
	else
		offset = 0x84;

	controller_mode = ddi_get8(ata_ctlp->ata_conf_handle,
	    (uchar_t *)(ata_ctlp->ata_conf_addr + offset));

	if (targ & 1)
		controller_mode = (controller_mode & 0xcf) | (timing << 4);
	else
		controller_mode = (controller_mode & 0xfc) | timing;

	ddi_put8(ata_ctlp->ata_conf_handle,
	    (uchar_t *)(ata_ctlp->ata_conf_addr + offset), controller_mode);
}

/*
 * This routine is for disabling the propogation of interrupt from the
 * controller to the cpu
 * A value of 2 in chno indicates, disable for both the channels.
 */
static void
sil_disable_intr(struct ata_controller   *ata_ctlp, int chno)
{

	uchar_t	system_config_status;

	system_config_status = ddi_get8(ata_ctlp->ata_conf_handle,
		(uchar_t *)(ata_ctlp->ata_conf_addr + 0x8a));

	switch (chno) {
		case 0:
			system_config_status |= 0x40;
			break;
		case 1:
			system_config_status |= 0x80;
			break;
		case 2:
			system_config_status |= 0xc0;
	}
	ddi_put8(ata_ctlp->ata_cs_handle,
		(uchar_t *)(ata_ctlp->ata_conf_addr + 0x8a),
		system_config_status);
}

/*
 * This routine is for enabling the propogation of interrupt from the
 * controller to the cpu
 * A value of 2 in chno indicates, enable for both the channels.
 */
static void
sil_enable_intr(struct ata_controller   *ata_ctlp, int chno)
{

	uchar_t	system_config_status;

	system_config_status = ddi_get8(ata_ctlp->ata_conf_handle,
		(uchar_t *)(ata_ctlp->ata_conf_addr + 0x8a));

	switch (chno) {
		case 0:
			system_config_status &= 0xbf;
			break;
		case 1:
			system_config_status &= 0x7f;
			break;
		case 2:
			system_config_status &= 0x3f;
	}
	ddi_put8(ata_ctlp->ata_cs_handle,
		(uchar_t *)(ata_ctlp->ata_conf_addr + 0x8a),
		system_config_status);
}

static int
sil_power_mgmt_initialize(void)
{
	/*
	 * Set Bit 0 and Bit 3 to 1 to indicate support for D0 and D3 state.
	 */
	return (0x9);
}

static void
sil_null_func()
{
}

#ifndef __lint
static void
sil_nien_toggle(struct ata_controller *ata_ctlp, int chno,
		uint8_t cmd)
{
}
#endif
