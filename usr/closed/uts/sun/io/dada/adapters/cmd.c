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
 * Copyright (c) 2000, 2011, Oracle and/or its affiliates. All rights reserved.
 */

/*
 * This file contains the code for Chip specific routines for
 * CMD 646U controller.
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
cmd_init_timing_tables(struct ata_controller   *ata_ctlp);
static void
cmd_program_read_ahead(struct ata_controller   *ata_ctlp);
static void
cmd_clear_interrupt(struct ata_controller   *ata_ctlp, int chno);
static int
cmd_get_intr_status(struct ata_controller   *ata_ctlp, int chno);
static void
cmd_program_timing_reg(struct ata_drive *ata_drvp);
static void
cmd_enable_channel(struct ata_controller   *ata_ctlp, int chno);
static void
cmd_disable_intr(struct ata_controller   *ata_ctlp, int chno);
static void
cmd_enable_intr(struct ata_controller   *ata_ctlp, int chno);
static int
cmd_get_speed_capabilities(struct ata_controller   *ata_ctlp, int chno);

#ifndef __lint
static int
cmd_power_entry_point(struct ata_controller *ata_ctlp, int component,
		int level);
static void
cmd_nien_toggle(struct ata_controller *ata_ctlp, int chno,
		uint8_t cmd);
static void
cmd_reset_chip(struct ata_controller   *ata_ctlp, int chno);

#endif

static int
cmd_power_mgmt_initialize(void);

/*
 * cmd_init is for initializing the function pointers of the
 * ata_controller structure to the chip specific functions.
 */
void
cmd_init(struct ata_controller   *ata_ctlp)
{
	ata_ctlp->init_timing_tables = cmd_init_timing_tables;
	ata_ctlp->program_read_ahead = cmd_program_read_ahead;
	ata_ctlp->clear_interrupt = cmd_clear_interrupt;
	ata_ctlp->get_intr_status = cmd_get_intr_status;
	ata_ctlp->program_timing_reg = cmd_program_timing_reg;
	ata_ctlp->enable_channel = cmd_enable_channel;
	ata_ctlp->disable_intr = cmd_disable_intr;
	ata_ctlp->enable_intr = cmd_enable_intr;
	ata_ctlp->get_speed_capabilities = cmd_get_speed_capabilities;
#ifndef __lint
	ata_ctlp->nien_toggle = cmd_nien_toggle;
	ata_ctlp->power_entry_point = cmd_power_entry_point;
	ata_ctlp->reset_chip = cmd_reset_chip;
#endif
	ata_ctlp->power_mgmt_initialize = cmd_power_mgmt_initialize;
}

/*
 * Functions for initializing the timing tables for cmd
 */
static void
cmd_init_timing_tables(struct ata_controller   *ata_ctlp)
{

	ata_ctlp->ac_piortable[0] = (uchar_t)PIOR0;
	ata_ctlp->ac_piortable[1] = (uchar_t)PIOR1;
	ata_ctlp->ac_piortable[2] = (uchar_t)PIOR2;
	ata_ctlp->ac_piortable[3] = (uchar_t)PIOR3;
	ata_ctlp->ac_piortable[4] = (uchar_t)PIOR4;
	ata_ctlp->ac_piowtable[0] = (uchar_t)PIOW0;
	ata_ctlp->ac_piowtable[1] = (uchar_t)PIOW1;
	ata_ctlp->ac_piowtable[2] = (uchar_t)PIOW2;
	ata_ctlp->ac_piowtable[3] = (uchar_t)PIOW3;
	ata_ctlp->ac_piowtable[4] = (uchar_t)PIOW4;
	ata_ctlp->ac_dmartable[0] = (uchar_t)DMAR0;
	ata_ctlp->ac_dmartable[1] = (uchar_t)DMAR1;
	ata_ctlp->ac_dmartable[2] = (uchar_t)DMAR2;
	ata_ctlp->ac_dmawtable[0] = (uchar_t)DMAW0;
	ata_ctlp->ac_dmawtable[1] = (uchar_t)DMAW1;
	ata_ctlp->ac_dmawtable[2] = (uchar_t)DMAW2;

	/* CMD ultra modes, only available with the CMD 649 */

	ata_ctlp->ac_udmatable[0] = (uchar_t)UDMA0;
	ata_ctlp->ac_udmatable[1] = (uchar_t)UDMA1;
	ata_ctlp->ac_udmatable[2] = (uchar_t)UDMA2;
	ata_ctlp->ac_udmatable[3] = (uchar_t)UDMA3;
	ata_ctlp->ac_udmatable[4] = (uchar_t)UDMA4;
	ata_ctlp->ac_udmatable[5] = (uchar_t)UDMA5;
	ata_ctlp->ac_udmatable[6] = (uchar_t)UDMA6;
}


/*
 * For programming the read ahead
 */

static void
cmd_program_read_ahead(struct ata_controller   *ata_ctlp)
{

	struct ata_drive	*ata_drvp;
	uchar_t	targ, ch0_rahead, ch1_rahead;
	int one_atapi = 0;

	ch0_rahead = ddi_get8(ata_ctlp->ata_conf_handle,
		(uchar_t *)ata_ctlp->ata_conf_addr + 0x51);
	ch0_rahead |= 0xC0;
	ch1_rahead = ddi_get8(ata_ctlp->ata_conf_handle,
			(uchar_t *)ata_ctlp->ata_conf_addr + 0x57);
	ch1_rahead |= 0xC;
	for (targ = 0; targ < ATA_MAXTARG; targ++) {
		ata_drvp = CTL2DRV(ata_ctlp, targ, 0);
		if (ata_drvp) {
			if (ata_drvp->ad_flags & AD_DISK) {
				switch (targ) {
					case 0 :
						ch0_rahead &= ~0x40;
						ddi_put8(
						ata_ctlp->ata_conf_handle,
						(uchar_t *)
						ata_ctlp->ata_conf_addr + 0x53,
						0x80);
						break;
					case 1 :
						ch0_rahead &= ~0x80;
						ddi_put8(
						ata_ctlp->ata_conf_handle,
						(uchar_t *)
						ata_ctlp->ata_conf_addr + 0x55,
						0x80);
						break;
					case 2 :
						ch1_rahead &= ~0x4;
						break;
					case 3 :
						ch1_rahead &= ~0x8;
						break;
				}
			} else if (ata_drvp->ad_flags & AD_ATAPI) {
				if ((targ == 2) || (targ == 3)) {
					one_atapi = 1;
				}
			}
		}
	}
	ddi_put8(ata_ctlp->ata_conf_handle, (uchar_t *)ata_ctlp->ata_conf_addr
			+ 0x51, ch0_rahead);
	if (!one_atapi) {
		ch1_rahead &= 0x3f;
		ch1_rahead |= 0x80;
	}
	ddi_put8(ata_ctlp->ata_conf_handle, (uchar_t *)ata_ctlp->ata_conf_addr
			+ 0x57, ch1_rahead);
}



/*
 * Clear any pending interrupts
 */

static void
cmd_clear_interrupt(struct ata_controller   *ata_ctlp, int chno)
{
	unsigned char val;

	switch (chno) {

		case 0:
		case 2:
			val = ddi_get8(ata_ctlp->ata_conf_handle,
			(uchar_t *)(ata_ctlp->ata_conf_addr + 0x50));
			if ((val & 0x4) && ((ata_ctlp->ac_revision >= 3) ||
			    (ata_ctlp->ac_device_id == CMD649))) {
				ddi_put8(ata_ctlp->ata_conf_handle,
				(uchar_t *)(ata_ctlp->ata_conf_addr + 0x50),
				val | 0x4);
			}
			if (chno == 0)
				break;
			/*
			 * If it was case 2 then fall thru and clear channel 1
			 */
			/* FALLTHROUGH */
		case 1:
			val = ddi_get8(ata_ctlp->ata_conf_handle,
			(uchar_t *)(ata_ctlp->ata_conf_addr + 0x57));
			if ((val & 0x10) && ((ata_ctlp->ac_revision >= 3) ||
			    (ata_ctlp->ac_device_id == CMD649))) {
				ddi_put8(ata_ctlp->ata_conf_handle,
				(uchar_t *)(ata_ctlp->ata_conf_addr + 0x57),
				val | 0x10);
			}
			break;
		default:
			cmn_err(CE_PANIC,
			"uata: cmd_clear_interrupt:  incorrect chno %d\n",
			chno);
	}

}


/*
 * A return value of 3 indicates speeds up to UDMA100 possible.
 * A value of 1 indicates UDMA33 possible
 * A value of 0 indicates only DMA0-2 possible.
 */

static int
cmd_get_speed_capabilities(struct ata_controller   *ata_ctlp, int chno)
{
	uchar_t	val;


	if (ata_ctlp->ac_device_id == CMD649) {

		/* Check for 80-pin cable */
		val = ddi_get8(ata_ctlp->ata_cs_handle,
		    (uchar_t *)(ata_ctlp->ata_cs_addr + 0x9));

		/*
		 * On the CMD-649 the BMIDECSR register contains the
		 * FIFO threshold control bits. Experimentation
		 * suggests a slight performance gain in triggering
		 * PCI access when the FIFO is 1/4 used.
		 */
		val &= 0x0f;
		val |= 0x50;

		ddi_put8(ata_ctlp->ata_cs_handle,
		    (uchar_t *)(ata_ctlp->ata_cs_addr + 0x9), val);

		return (val & 1 << chno ? 3 : 1);
	} else {
		/*
		 * CMD646 Rev greaterthan 5 has no issues with
		 * Ultra DMA mode2. Enable Ultra DMA Mode2 for
		 * this revision of the chip
		 */
		if ((ata_ctlp->ac_device_id == CMDDID) &&
			(ata_ctlp->ac_revision > 0x5)) {
			return (1);
		} else {
			return (0);
		}
	}


}
#ifndef __lint
static int
cmd_power_entry_point(struct ata_controller *ata_ctlp, int component,
			int level)
{
	return (SUCCESS);
}

static void
cmd_nien_toggle(struct ata_controller *ata_ctlp, int chno,
		uint8_t cmd)
{
}

/*
 * Note that this routine was added specifically for southbridge chip.
 * Any code added in this routine should be added with caution.
 */
static void
cmd_reset_chip(struct ata_controller   *ata_ctlp, int chno)
{
}
#endif

int cmd_intr_spin_period = 100;   /* (usec) */
int cmd_intr_spin_count = 200000;

/*
 *  Wait for interrupt on an IDE Channel
 */
static int
cmd_get_intr_status(struct ata_controller   *ata_ctlp, int chno)
{
	int period = cmd_intr_spin_period;
	int count = cmd_intr_spin_count;

	if (chno == 0) {
		if (ata_wait(ata_ctlp->ata_conf_handle,
		    (uint8_t *)(ata_ctlp->ata_conf_addr + 0x50), 4,
		    0, IGN_ERR, period, count) == FAILURE) {
			return (FAILURE);
		}
	} else {
		if (ata_wait(ata_ctlp->ata_conf_handle,
		    (uint8_t *)(ata_ctlp->ata_conf_addr + 0x57), 0x10,
		    0, IGN_ERR, period, count) == FAILURE) {
			return (FAILURE);
		}
	}

	return (SUCCESS);
}

/*
 * Routine which writes the timing and control values
 */
static void
cmd_program_timing_reg(struct ata_drive *ata_drvp)
{
	uchar_t	rd_par;
	struct ata_controller *ata_ctlp = ata_drvp->ad_ctlp;
	int	targ = ata_drvp->ad_targ;

	if (ata_drvp->ad_piomode != 0x7f) {
		/* PIOMODE */
		rd_par = ata_ctlp->ac_piortable[ata_drvp->ad_piomode];
	} else {
		/* DMAMODE */
		rd_par = ata_ctlp->ac_dmartable[ata_drvp->ad_dmamode & 0x03];
	}

	targ *= 2;
	if (targ == 6) {
		targ = 7;
	}

	ddi_put8(ata_ctlp->ata_conf_handle,
	(uchar_t *)ata_ctlp->ata_conf_addr + 0x54 + targ, rd_par);

	/* For controllers that support ultra speed */
	if ((ata_ctlp->ac_device_id == CMD649) ||
		((ata_ctlp->ac_device_id == CMDDID) &&
		    (ata_ctlp->ac_revision > 0x5)))  {
		uchar_t udma_offset;
		char    chnl, dsk;


		chnl = ata_drvp->ad_targ > 1 ? 1 : 0;
		dsk  = ata_drvp->ad_targ & 1;
		udma_offset = chnl ? 0x8 : 0x0;

		/* Read the appropriate UDIDETCRx value */
		rd_par = ddi_get8(ata_ctlp->ata_cs_handle,
		    (uchar_t *)ata_ctlp->ata_cs_addr + udma_offset + 0x3);
		if (ata_drvp->ad_run_ultra) {
			/* Set bit to enable UDMA mode for this disk */
			rd_par |= 1 << dsk;

			/* Set appropriate Ultra DMA timing mode */
			rd_par &= ~(0x3 << (dsk?6:4));

			rd_par |= ata_ctlp->ac_udmatable[ata_drvp->ad_dmamode &
			    DMA_BITS] << (dsk?6:4);

			if ((ata_drvp->ad_dmamode & DMA_BITS) == 5)
				/* For UDMA 100 set Clock Cycle Resolution */
				rd_par |= dsk ? 0x8 : 0x4;

			ddi_put8(ata_ctlp->ata_cs_handle,
			    (uchar_t *)ata_ctlp->ata_cs_addr + udma_offset +
			    0x3, rd_par);

			/* Set DMA mode in BMIDESRx */
			rd_par = ddi_get8(ata_ctlp->ata_cs_handle,
				(uchar_t *)ata_ctlp->ata_cs_addr + udma_offset +
				0x2);
			ddi_put8(ata_ctlp->ata_cs_handle,
				(uchar_t *)ata_ctlp->ata_cs_addr + udma_offset +
				0x2, rd_par | (dsk ? 0x40 : 0x20));
		} else if (rd_par & (1 << dsk)) {
			/*
			 * If drive is not in ultra dma mode we need to disable
			 * the controllers ultra dma mode bit for that drive.
			 * Clear the appropriate UDMA mode bit in UDIDETCRx .
			 */

			rd_par &= ~(0x01 << dsk);
			ddi_put8(ata_ctlp->ata_cs_handle,
				(uchar_t *)ata_ctlp->ata_cs_addr + udma_offset +
				0x3, rd_par);
		}

	}

}

/*
 * This routine is for enabling the secondary IDE channel.
 */

static void
cmd_enable_channel(struct ata_controller   *ata_ctlp, int chno)
{

	uchar_t	val;

	if (chno == 1) {
		val = ddi_get8(ata_ctlp->ata_conf_handle,
		(uchar_t *)ata_ctlp->ata_conf_addr + 0x51);
		ddi_put8(ata_ctlp->ata_conf_handle,
		(uchar_t *)ata_ctlp->ata_conf_addr + 0x51, val | 0x8);
	}
}

/*
 * This routine is for disabling the propogation of interrupt from the
 * controller to the cpu
 * A value of 2 in chno indicates, disable for both the channels.
 */

static void
cmd_disable_intr(struct ata_controller   *ata_ctlp, int chno)
{

	uchar_t	val;

	val = ddi_get8(ata_ctlp->ata_cs_handle,
		(uchar_t *)(ata_ctlp->ata_cs_addr + 1)) & 0x33;
	switch (chno) {
		case 0:
			val |= 0x10;
			break;
		case 1:
			val |= 0x20;
			break;
		case 2:
			val |= 0x30;
	}
	ddi_put8(ata_ctlp->ata_cs_handle,
		(uchar_t *)(ata_ctlp->ata_cs_addr + 1), val);
}

/*
 * This routine is for enabling the propogation of interrupt from the
 * controller to the cpu
 * A value of 2 in chno indicates, enable for both the channels.
 */

static void
cmd_enable_intr(struct ata_controller   *ata_ctlp, int chno)
{

	uchar_t	val;

	val = ddi_get8(ata_ctlp->ata_cs_handle,
		(uchar_t *)(ata_ctlp->ata_cs_addr + 1)) & 0x33;
	switch (chno) {
		case 0:
			val &= 0x23;
			break;
		case 1:
			val &= 0x13;
			break;
		case 2:
			val &= 0x03;
	}
	ddi_put8(ata_ctlp->ata_cs_handle,
		(uchar_t *)(ata_ctlp->ata_cs_addr + 1), val);
}

static int
cmd_power_mgmt_initialize(void)
{
	/*
	 * Return -1 to indicate no power management support
	 */
	return (-1);
}
