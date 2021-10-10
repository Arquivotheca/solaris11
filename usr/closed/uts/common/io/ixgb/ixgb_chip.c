/*
 * Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#include "ixgb.h"

/*
 * Operating register get/set access routines
 */
#undef IXGB_DBG
#define	IXGB_DBG		IXGB_DBG_CHIP

static uint32_t ixgb_stop_start_on_sync	= 0;
static uint32_t ixgb_watchdog_count	= 1 << 26;
extern boolean_t ixgb_br_setup_enable;

uint32_t
ixgb_reg_get32(ixgb_t *ixgbp, ixgb_regno_t regno)
{
	IXGB_TRACE(("ixgb_reg_get32($%p, 0x%lx)",
	    (void *)ixgbp, regno));

	return (ddi_get32(ixgbp->io_handle, PIO_ADDR(ixgbp, regno)));
}

void
ixgb_reg_put32(ixgb_t *ixgbp, ixgb_regno_t regno, uint32_t data)
{
	IXGB_TRACE(("ixgb_reg_put32($%p, 0x%lx, 0x%x)",
	    (void *)ixgbp, regno, data));

	ddi_put32(ixgbp->io_handle, PIO_ADDR(ixgbp, regno), data);
}

void
ixgb_reg_set32(ixgb_t *ixgbp, ixgb_regno_t regno, uint32_t bits)
{
	uint32_t regval;

	IXGB_TRACE(("ixgb_reg_set32($%p, 0x%lx, 0x%x)",
	    (void *)ixgbp, regno, bits));

	regval = ixgb_reg_get32(ixgbp, regno);
	regval |= bits;
	ixgb_reg_put32(ixgbp, regno, regval);
}

void
ixgb_reg_clr32(ixgb_t *ixgbp, ixgb_regno_t regno, uint32_t bits)
{
	uint32_t regval;

	IXGB_TRACE(("ixgb_reg_clr32($%p, 0x%lx, 0x%x)",
	    (void *)ixgbp, regno, bits));

	regval = ixgb_reg_get32(ixgbp, regno);
	regval &= ~bits;
	ixgb_reg_put32(ixgbp, regno, regval);
}

uint64_t
ixgb_reg_get64(ixgb_t *ixgbp, uint64_t regno)
{
	uint64_t regval;

	regval = ddi_get64(ixgbp->io_handle,  PIO_ADDR(ixgbp, regno));

	IXGB_TRACE(("ixgb_reg_get64($%p, 0x%lx) = 0x%016llx",
	    (void *)ixgbp, regno, regval));

	return (regval);
}

void
ixgb_reg_put64(ixgb_t *ixgbp, uint64_t regno, uint64_t data)
{
	IXGB_TRACE(("ixgb_reg_put64($%p, 0x%lx, 0x%016llx)",
	    (void *)ixgbp, regno, data));

	ddi_put64(ixgbp->io_handle, PIO_ADDR(ixgbp, regno), data);
}

/*
 * Perform first-stage chip (re-)initialisation, using only config-space
 * accesses:
 *
 * + Read the vendor/device/revision/subsystem/cache-line-size registers,
 *   returning the data in the structure pointed to by <infop>.
 */
void
ixgb_chip_cfg_init(ixgb_t *ixgbp, chip_info_t *infop, boolean_t reset)
{
	uint16_t command;
	ddi_acc_handle_t handle;
	dev_info_t *pdip;
	uint32_t vendor_id, device_id, revision_id;
	char **prop_val;
	uint_t prop_len;

	IXGB_TRACE(("ixgb_chip_cfg_init($%p, $%p, %d)",
	    (void *)ixgbp, (void *)infop));

	/*
	 * save PCI cache line size and subsystem vendor ID
	 *
	 * Read all the config-space registers that characterise the
	 * chip, specifically vendor/device/revision/subsystem vendor
	 * and subsystem device id.  We expect (but don't check) that
	 */
	handle = ixgbp->cfg_handle;
	/* reading the vendor information once */
	if (!reset) {
		infop->command = pci_config_get16(handle,
		    PCI_CONF_COMM);
		infop->vendor = pci_config_get16(handle,
		    PCI_CONF_VENID);
		infop->device = pci_config_get16(handle,
		    PCI_CONF_DEVID);
		infop->subven = pci_config_get16(handle,
		    PCI_CONF_SUBVENID);
		infop->subdev = pci_config_get16(handle,
		    PCI_CONF_SUBSYSID);
		infop->revision = pci_config_get8(handle,
		    PCI_CONF_REVID);
		infop->clsize = pci_config_get8(handle,
		    PCI_CONF_CACHE_LINESZ);
		infop->latency = pci_config_get8(handle,
		    PCI_CONF_LATENCY_TIMER);
	}

	IXGB_DEBUG(("ixgb_chip_cfg_init: vendor 0x%x device 0x%x",
	    infop->vendor, infop->device));
	IXGB_DEBUG(("ixgb_chip_cfg_init: subven 0x%x subdev 0x%x ",
	    infop->subven, infop->subdev));
	IXGB_DEBUG(("ixgb_chip_cfg_init: clsize %d latency %d command 0x%x",
	    infop->clsize, infop->latency, infop->command));

	command = infop->command | PCI_COMM_MAE;
	command &= ~(PCI_COMM_ME|PCI_COMM_MEMWR_INVAL);
	command |= PCI_COMM_ME;
	pci_config_put16(handle, PCI_CONF_COMM, command);

	/*
	 * Per intel's 82597ex DPM, page 60~61.
	 * configur the size of pci-x burst to tune the performance
	 *
	 * But for the system which using AMD8131 (Revision B2,
	 * revision ID = 0x13) as the PCI-X bridge,
	 * It's recommended to set these values as following. Otherwise
	 * the port may meet tx hang problem.
	 *
	 *   data parity error recovery: enabled
	 *   relaxed ordering: disabled
	 *   max memory read bytecount: 1024
	 *   max outstanding split transactions: 2
	 */

	if (infop->businfo & IXGB_STATUS_PCIX_MODE) {
		pdip = ddi_get_parent(ixgbp->devinfo);
		if (ddi_prop_lookup_int_array(DDI_DEV_T_ANY, pdip,
		    0, "vendor-id", (int **)&prop_val, &prop_len)
		    != DDI_PROP_SUCCESS) {
			goto normal_set;
		}
		vendor_id = *(uint32_t *)prop_val;
		ddi_prop_free(prop_val);

		if (ddi_prop_lookup_int_array(DDI_DEV_T_ANY, pdip,
		    0, "device-id", (int **)&prop_val, &prop_len)
		    != DDI_PROP_SUCCESS) {
			goto normal_set;
		}
		device_id = *(uint32_t *)prop_val;
		ddi_prop_free(prop_val);

		if (ddi_prop_lookup_int_array(DDI_DEV_T_ANY, pdip,
		    0, "revision-id", (int **)&prop_val, &prop_len)
		    != DDI_PROP_SUCCESS) {
			goto normal_set;
		}
		revision_id = *(uint32_t *)prop_val;
		ddi_prop_free(prop_val);

		if ((vendor_id == 0x1022) && (device_id == 0x7450) &&
		    (revision_id == 0x13)) {
			command = PCIX_COMM_DP | PCIX_MAX_RCOUNT_1024 |
			    PCIX_MAX_SPLIT_2;
		} else {
		normal_set:
			command = PCIX_COMM_DP | PCIX_COMM_RO |
			    PCIX_MAX_RCOUNT_2048 | PCIX_MAX_SPLIT_3;
		}

		pci_config_put16(handle, PCIX_CONF_COMM, command);
	}

	/*
	 * Clear any remaining error status bits
	 */
	pci_config_put16(handle, PCI_CONF_STAT, ~0);
}

/*
 * Basic nvmem get/set access routines
 *
 * This uses the chip's manual method, controlled by the
 * EEPROM/Flash(Aka 'nvmem') Address/Data Registers at 0x00018,
 * so the CPU have to fiddle with the individual bits.
 * These functions can not get description in intel's 82597ex DPM,
 * please refer to Atmel's AT93C46 manaul.
 */


/*
 * Acquire the access to the eeprom/flash
 */
static void ixgb_nvmem_init(ixgb_t *ixgbp);
#pragma	no_inline(ixgb_nvmem_init)

static void
ixgb_nvmem_init(ixgb_t *ixgbp)
{
	uint32_t regval;

	IXGB_TRACE(("ixgb_nvmem_acquire($%p)",
	    (void *)ixgbp));

	regval = IXGB_NVMEM_DI | IXGB_NVMEM_SK;
	ixgb_reg_clr32(ixgbp, IXGB_NVMEM, regval);
	ixgb_reg_set32(ixgbp, IXGB_NVMEM, IXGB_NVMEM_CS);
	ixgb_reg_set32(ixgbp, IXGB_NVMEM, IXGB_NVMEM_DI);
	drv_usecwait(50);
	(void) ixgb_reg_get32(ixgbp, IXGB_NVMEM);
}

/*
 * Release the access to the eeprom/flash
 */

static void ixgb_nvmem_finit(ixgb_t *ixgbp);
#pragma	no_inline(ixgb_nvmem_finit)
static void
ixgb_nvmem_finit(ixgb_t *ixgbp)
{
	uint32_t regval;

	regval = IXGB_NVMEM_CS | IXGB_NVMEM_SK;
	ixgb_reg_clr32(ixgbp, IXGB_NVMEM, regval);
	drv_usecwait(50);
	ixgb_reg_set32(ixgbp, IXGB_NVMEM, IXGB_NVMEM_SK);
	drv_usecwait(100);
	ixgb_reg_set32(ixgbp, IXGB_NVMEM, IXGB_NVMEM_CS);
	drv_usecwait(50);
	ixgb_reg_clr32(ixgbp, IXGB_NVMEM, IXGB_NVMEM_SK);
	drv_usecwait(50);
}
/*
 * Read the date into the errpom after acquire eeprom's access
 */
static void
ixgb_nvmem_raccess(ixgb_t *ixgbp, uint64_t *datap)
{
	uint16_t bitwid;
	uint32_t regval;

	*datap = 0;
	regval = IXGB_NVMEM_DO | IXGB_NVMEM_DI;
	ixgb_reg_clr32(ixgbp, IXGB_NVMEM, regval);

	for (bitwid = 0; bitwid < IXGB_NVMEM_DATA_WID; bitwid++) {
		*datap <<= 1;
		ixgb_reg_set32(ixgbp, IXGB_NVMEM, IXGB_NVMEM_SK);
		ixgb_reg_clr32(ixgbp, IXGB_NVMEM, IXGB_NVMEM_DI);
		regval = ixgb_reg_get32(ixgbp, IXGB_NVMEM);
		if (regval & IXGB_NVMEM_DO)
			*datap |= 1;
		ixgb_reg_clr32(ixgbp, IXGB_NVMEM, IXGB_NVMEM_SK);
	}
}

/*
 * Put the date into the errpom after acquire eeprom's access
 */
static void
ixgb_nvmem_waccess(ixgb_t *ixgbp, uint8_t wid, uint64_t data)
{
	uint32_t regval;
	uint32_t mask;

	ASSERT(wid <= IXGB_NVMEM_DATA_WID);
	mask = 1<<(wid-1);
	regval = IXGB_NVMEM_DO | IXGB_NVMEM_DI;
	ixgb_reg_clr32(ixgbp, IXGB_NVMEM, regval);

	do {
		if (data & mask) {
			ixgb_reg_set32(ixgbp, IXGB_NVMEM, IXGB_NVMEM_DI);
			drv_usecwait(50);
		}
		ixgb_reg_set32(ixgbp, IXGB_NVMEM, IXGB_NVMEM_SK);
		ixgb_reg_clr32(ixgbp, IXGB_NVMEM, IXGB_NVMEM_SK);
		ixgb_reg_clr32(ixgbp, IXGB_NVMEM, IXGB_NVMEM_DI);
		mask >>= 1;
	} while (mask);
	ixgb_reg_clr32(ixgbp, IXGB_NVMEM, IXGB_NVMEM_DI);
}

/*
 * Read the date into the errpom.
 */
static void
ixgb_nvmem_read(ixgb_t *ixgbp, uint8_t offset, uint64_t *datap)
{

	ixgb_nvmem_init(ixgbp);
	ixgb_nvmem_waccess(ixgbp, NVMEM_READ_WID, NVMEM_READ);
	ixgb_nvmem_waccess(ixgbp, IXGB_NVMEM_ADDR_WID, offset);
	ixgb_nvmem_raccess(ixgbp, datap);
	ixgb_nvmem_finit(ixgbp);
}

/*
 * Put the date into the errpom.
 */
static void
ixgb_nvmem_write(ixgb_t *ixgbp, uint8_t offset, uint64_t data)
{
	uint32_t regval;
	uint32_t retries;

	ixgb_nvmem_init(ixgbp);
	ixgb_nvmem_waccess(ixgbp, NVMEM_EWEN_WID, NVMEM_EWEN);
	ixgb_nvmem_waccess(ixgbp, NVMEM_DUMM_WID,  NVMEM_DUMM);
	ixgb_nvmem_finit(ixgbp);

	ixgb_nvmem_waccess(ixgbp, NVMEM_WRITE_WID, NVMEM_WRITE);
	ixgb_nvmem_waccess(ixgbp, IXGB_NVMEM_ADDR_WID, offset);
	ixgb_nvmem_waccess(ixgbp, IXGB_NVMEM_DATA_WID, data);

	regval = ixgb_reg_get32(ixgbp, IXGB_NVMEM);
	for (retries = 0; retries < 100; retries++) {
		if (regval & IXGB_NVMEM_DO)
			break;
		regval = ixgb_reg_get32(ixgbp, IXGB_NVMEM);
	}

	ixgb_nvmem_finit(ixgbp);
	ixgb_nvmem_waccess(ixgbp, NVMEM_EWDS_WID, NVMEM_EWDS);
	ixgb_nvmem_waccess(ixgbp, NVMEM_DUMM_WID,  NVMEM_DUMM);

	regval = IXGB_NVMEM_CS | IXGB_NVMEM_DI;
	ixgb_reg_clr32(ixgbp, IXGB_NVMEM, regval);

	ixgb_reg_set32(ixgbp, IXGB_NVMEM, IXGB_NVMEM_SK);
	drv_usecwait(10);
	ixgb_reg_clr32(ixgbp, IXGB_NVMEM, IXGB_NVMEM_SK);
}

/*
 * Functions ixgb_chip_peek_seeprom(read)
 * & ixgb_chip_poke_seeprom (write)
 * are used for ixgb's ioctl (ie. update the contents of eeprom by
 * software)
 */
static void ixgb_chip_peek_seeprom(ixgb_t *ixgbp, ixgb_peekpoke_t *ppd);
#pragma	no_inline(ixgb_chip_peek_seeprom)

static void
ixgb_chip_peek_seeprom(ixgb_t *ixgbp, ixgb_peekpoke_t *ppd)
{
	uint64_t data;

	IXGB_TRACE(("ixgb_chip_peek_seeprom($%p, $%p)",
	    (void *)ixgbp, (void *)ppd));

	ixgb_nvmem_read(ixgbp, ppd->pp_acc_offset, &data);
	ppd->pp_acc_data = data;
}

static void ixgb_chip_poke_seeprom(ixgb_t *ixgbp, ixgb_peekpoke_t *ppd);
#pragma	no_inline(ixgb_chip_poke_seeprom)

static void
ixgb_chip_poke_seeprom(ixgb_t *ixgbp, ixgb_peekpoke_t *ppd)
{
	uint64_t data;

	IXGB_TRACE(("ixgb_chip_poke_seeprom($%p, $%p)",
	    (void *)ixgbp, (void *)ppd));

	data = ppd->pp_acc_data;
	(void) ixgb_nvmem_write(ixgbp, ppd->pp_acc_offset, data);
}

/*
 * Compute the checkum of the contents in eeprom
 * and compare the vaule with the checksum value provide by intel
 * As to the detailed contents in the eeprom, pleas refer to intel's 82507Ex
 * DPM, page 72~76
 */
static int
ixgb_check_hardsum(ixgb_t *ixgbp)
{
	uint16_t sum = 0;
	uint32_t num;
	uint64_t data;

	IXGB_TRACE(("ixgb_check_hardsum($%p)", (void *)ixgbp));

	for (num = 0; num < NVMEM_SIZE; num++) {
		ixgb_nvmem_read(ixgbp, num, &data);
		sum += data;
	}
	if (sum == NVMEM_CHECKSUM)
		return (IXGB_SUCCESS);

	return (IXGB_FAILURE);
}

/*
 * Get the mac address from the offset 0x00~0x03 in
 * the contents of eeprom. Store the value as network order.
 */
static uint64_t
ixgb_get_nvmac(ixgb_t *ixgbp)
{
	uint8_t num;
	uint8_t shift;
	uint64_t mac;
	uint64_t data;

	for (num = 0, shift = NVMEM_MAC_OFFSET-1, mac = 0ull;
	    num < NVMEM_MAC_OFFSET; num++, shift--) {
		ixgb_nvmem_read(ixgbp, num, &data);
		data = (((uint8_t)data << 8) | (data >> 8));
		mac = mac | data;
		mac = shift > 0 ? mac<<16 : mac;
	}
	return (mac);
}

/*
 * Get the hardware info (ie. Bus speed , Bus type, Bus wid
 * ) and judge the board type (ie.Sun's adapter or Intel's adapter).
 */
void
ixgb_chip_info_get(ixgb_t *ixgbp)
{
	uint16_t device_id;
	uint16_t subven_id;
	chip_info_t *chip_infop;
	boolean_t dev_ok = B_TRUE;
	boolean_t sys_ok = B_TRUE;

	chip_infop = &ixgbp->chipinfo;
	device_id = chip_infop->device;
	subven_id = chip_infop->subven;

	chip_infop->businfo = ixgb_reg_get32(ixgbp, IXGB_STATUS);
	IXGB_DEBUG(("ixgb_chip_info_get: %s and %s; bus wid is %s",
	    chip_infop->businfo & IXGB_STATUS_PCIX_MODE ? "PCI-X" : "PCI",
	    chip_infop->businfo & IXGB_STATUS_PCIX_SPD_MASK ? "fast" : "slow",
	    chip_infop->businfo & IXGB_STATUS_BUS64 ? "wide" : "narrow"));

	switch (device_id) {
	default:
		dev_ok = B_FALSE;
		break;

	case IXGB_82597EX:
		chip_infop->phy_type = IXGB_PHY_TXN17401;
		break;

	case IXGB_82597EX_SR:
		chip_infop->phy_type = IXGB_PHY_6005;
		break;

	case IXGB_82597EX_CX4:
		chip_infop->phy_type = IXGB_PHY_6005;
		break;
	}
	IXGB_DEBUG(("ixgb_chip_info_get: phy-device 0x%x",
	    chip_infop->phy_type));

	switch (subven_id) {
	default:
		sys_ok = B_FALSE;
		break;

	case SUBVENDOR_ID_INTEL:
		ixgbp->phys_restart = ixgb_serdes_restart_intel;
		break;

	case SUBVENDOR_ID_SUN:
		ixgbp->phys_restart = ixgb_serdes_restart_sun;
		break;
	}

	if (!sys_ok || !dev_ok) {
		ixgb_log(ixgbp, "device(ven_id(%x),dev_id(%x)"
		    ",subven(%x), sub_dev(%x) is not supported",
		    chip_infop->vendor, chip_infop->device,
		    chip_infop->subven, chip_infop->subdev);
	}
}

/*
 * This routine implements polling for completion of a reset, enable
 * or disable operation, returning B_TRUE on success (bit reached the
 * required state) or B_FALSE on timeout.
 */
static boolean_t ixgb_chip_poll_engine(ixgb_t *ixgbp, ixgb_regno_t regno,
    uint32_t mask, uint32_t val, uint32_t delayusec);
#pragma	no_inline(ixgb_chip_poll_engine)

static boolean_t
ixgb_chip_poll_engine(ixgb_t *ixgbp, ixgb_regno_t regno,
    uint32_t mask, uint32_t val, uint32_t delayusec)
{
	uint32_t regval;

	IXGB_TRACE(("ixgb_chip_poll_engine($%p, 0x%lx, 0x%x, 0x%x, 0x%x)",
	    (void *)ixgbp, regno, mask, val, delayusec));

	drv_usecwait(delayusec);
	regval = ixgb_reg_get32(ixgbp, regno);
	if ((regval & mask) == val)
		return (B_TRUE);

	return (B_FALSE);
}

/*
 * Various registers that control the chip's internal engines (state
 * machines) have an <enable> bit.  To restart the state machine, this bit must
 * be written with 1, then polled to see when the state machine has
 * actually started.
 *
 * The return value is B_TRUE on success (enable bit set), or
 * B_FALSE if the state machine didn't start.
 */
boolean_t
ixgb_chip_reset_engine(ixgb_t *ixgbp, ixgb_regno_t regno,
    uint32_t mask, uint32_t morebits, uint32_t delayusec)
{
	uint32_t regval;
	boolean_t ok;

	regval = ixgb_reg_get32(ixgbp, regno);

	IXGB_TRACE(("ixgb_chip_reset_engine($%p, 0x%lx)",
	    (void *)ixgbp, regno));
	regval |= mask;
	regval |= morebits;
	ixgb_reg_put32(ixgbp, regno, regval);
	ok = ixgb_chip_poll_engine(ixgbp, regno,
	    mask, 0, delayusec);
	if (!ok)
		ixgb_problem(ixgbp, "ixgb reset engine failed");
	return (ok);
}

/*
 * Various registers that control the chip's internal engines (state
 * machines) have an <enable> bit. To stop the state machine, this bit must
 * be written with 0, then need to wait an appropriate interval of time.
 */
static void ixgb_chip_disable_engine(ixgb_t *ixgbp, ixgb_regno_t regno,
    uint32_t mask, uint32_t morebits);
#pragma	no_inline(ixgb_chip_disable_engine)

static void
ixgb_chip_disable_engine(ixgb_t *ixgbp, ixgb_regno_t regno,
    uint32_t mask, uint32_t morebits)
{
	uint32_t regval;

	IXGB_TRACE(("ixgb_chip_disable_engine($%p, 0x%lx, 0x%x)",
	    (void *)ixgbp, regno, morebits));

	regval = ixgb_reg_get32(ixgbp, regno);
	regval &= ~mask;
	regval &= ~morebits;
	ixgb_reg_put32(ixgbp, regno, regval);
	drv_usecwait(IXGB_TXRX_DELAY);
	if (!ixgb_chip_poll_engine(ixgbp, regno, mask, 0, 0))
		ixgb_problem(ixgbp, "ixgb disable engine failed");
}

/*
 * Various registers that control the chip's internal engines (state
 * machines) have an <enable> bit (fortunately, in the same place in
 * each such register :-).  To start the state machine, this bit must
 * be written with 0, then need to wait an appropriate interval of time.
 */
static void ixgb_chip_enable_engine(ixgb_t *ixgbp, ixgb_regno_t regno,
    uint32_t mask, uint32_t morebits);
#pragma	no_inline(ixgb_chip_enable_engine)

static void
ixgb_chip_enable_engine(ixgb_t *ixgbp, ixgb_regno_t regno,
    uint32_t mask, uint32_t morebits)
{
	uint32_t regval;

	IXGB_TRACE(("ixgb_chip_enable_engine($%p, 0x%lx, 0x%x)",
	    (void *)ixgbp, regno, morebits));

	regval = ixgb_reg_get32(ixgbp, regno);
	regval |= mask;
	regval |= morebits;
	ixgb_reg_put32(ixgbp, regno, regval);
	drv_usecwait(IXGB_TXRX_DELAY);
	if (!ixgb_chip_poll_engine(ixgbp, regno, mask, mask, 0))
		ixgb_problem(ixgbp, "ixgb enable engine failed");
}

boolean_t
ixgb_chip_reset_link(ixgb_t *ixgbp)
{
	uint32_t regval;
	int loopcount;
	int tries;
	boolean_t link_up;
	boolean_t lane_align;

	IXGB_TRACE(("ixgb_chip_reset_link($%p)",
	    (void *)ixgbp));

	for (loopcount = 0; loopcount < 10; loopcount++) {
		/* Reset the link */
		ixgb_reg_set32(ixgbp, IXGB_CTRL0, (IXGB_CTRL0_LRST |
		    IXGB_CTRL0_CMDC));

		for (tries = 0; tries < 10; tries++) {
			/* Wait for it to stabilize */
			drv_usecwait(IXGB_LINK_RESET_DELAY/100);

			/* Insure self-clearing LRST bit is clear */
			regval = ixgb_reg_get32(ixgbp, IXGB_CTRL0);
			if (!(regval & IXGB_CTRL0_LRST))
				break;	/* break out on success */
		}

		/*
		 * The specification indicates a maximum PMA PLL sync up
		 * time of 200Us. We loop 100 times at 13Us to wait up to
		 * 1300Us for good measure. If the link comes up early
		 * then we exit.
		 */
		for (tries = 0; tries < 10; tries++) {
			drv_usecwait(IXGB_LINK_RESET_DELAY/100);
			regval = ixgb_reg_get32(ixgbp, IXGB_XPCSS);
			lane_align = BIS(regval, IXGB_XPCSS_ALIGN_STATUS);
			regval = ixgb_reg_get32(ixgbp, IXGB_STATUS);
			link_up = BIS(regval, IXGB_STATUS_LU);
			if (lane_align && link_up)
				return (B_TRUE);
		}
	}
	return (B_FALSE);
}

/*
 * Reprogram the Ethernet, Transmit, and Receive MAC
 * modes to match the ixgb's Flow ,paus, watermark variables
 */
static void
ixgb_sync_flow_modes(ixgb_t *ixgbp)
{
	uint32_t flow;
	uint32_t pause_time;
	uint32_t high_water;
	uint32_t low_water;
	boolean_t xon;

	IXGB_TRACE(("ixgb_sync_flow_modes($%p)", (void *)ixgbp));

	flow = ixgbp->chip_flow;
	high_water = ixgbp->rx_highwater;
	low_water = ixgbp->rx_lowwater;
	pause_time = ixgbp->pause_time;
	xon = B_FALSE;

	switch (flow) {
	default:
	case FLOW_NONE:
		IXGB_DEBUG(("No flow control or error flow control"));
		ixgb_reg_clr32(ixgbp, IXGB_CTRL0, IXGB_CTRL0_TPE |
		    IXGB_CTRL0_RPE);
		low_water = 0;
		high_water = 0;
		pause_time = 0;
		xon = ixgbp->xon;
		break;

	case TX_PAUSE:
		ixgb_reg_clr32(ixgbp, IXGB_CTRL0, IXGB_CTRL0_RPE);
		ixgb_reg_set32(ixgbp, IXGB_CTRL0, IXGB_CTRL0_TPE);
		break;

	case RX_PAUSE:
		ixgb_reg_clr32(ixgbp, IXGB_CTRL0, IXGB_CTRL0_TPE);
		ixgb_reg_set32(ixgbp, IXGB_CTRL0, IXGB_CTRL0_RPE);
		break;

	case FULL_FLOW:
		ixgb_reg_set32(ixgbp, IXGB_CTRL0, IXGB_CTRL0_TPE |
		    IXGB_CTRL0_RPE);
		break;
	}

	ixgb_reg_clr32(ixgbp, IXGB_FCRTL, RX_LOW_WATER_MASK);
	ixgb_reg_set32(ixgbp, IXGB_FCRTL, low_water & RX_LOW_WATER_MASK);

	if (xon)
		ixgb_reg_set32(ixgbp, IXGB_FCRTL, IXGB_FCRTL_XONE);

	ixgb_reg_clr32(ixgbp, IXGB_FCRTL, RX_HIGH_WATER_MASK);
	ixgb_reg_set32(ixgbp, IXGB_FCRTH, high_water & RX_HIGH_WATER_MASK);

	if (pause_time)
		ixgb_reg_put32(ixgbp, IXGB_PAP, pause_time & PAUSE_TIME_MASK);
}

/*
 * ixgb_chip_sync() -- program the chip with the unicast MAC address,
 * the multicast hash table, the required level of promiscuity.
 */

void ixgb_chip_sync(ixgb_t *ixgbp);
#pragma	no_inline(ixgb_chip_sync)

void
ixgb_chip_sync(ixgb_t *ixgbp)
{
	uint8_t i;
	uint32_t reg;
	uint64_t macaddr;

	IXGB_TRACE(("ixgb_chip_sync($%p)", (void *)ixgbp));
	ASSERT(mutex_owned(ixgbp->genlock));

	/*
	 * If the TX/RX MAC engines are already running, we should stop
	 * them (and reset the RX engine) before changing the parameters.
	 * If they're not running, this will have no effect ...
	 *
	 * NOTE: this is currently disabled by default because stopping
	 * and restarting the Tx engine may cause an outgoing packet in
	 * transit to be truncated.  Also, stopping and restarting the
	 * Rx engine will cause rx's state machine halts in heavy traffic
	 */
	if (ixgb_stop_start_on_sync != 0) {
		(void) ixgb_chip_disable_engine(ixgbp, IXGB_RCTL,
		    IXGB_RCTL_RXEN, 0);
		(void) ixgb_chip_disable_engine(ixgbp, IXGB_TCTL,
		    IXGB_TCTL_TXEN, 0);
	}

	/*
	 * Transform the MAC address from host to chip format, the unicast
	 * MAC address(es) ...
	 */
	for (i = ETHERADDRL, macaddr = 0ull; i != 0; --i) {
		macaddr |= ixgbp->curr_addr.addr[i-1];
		macaddr <<= (i > 1) ? 8 : 0;
	}
	IXGB_DEBUG(("ixgb_chip_sync($%p) setting MAC address %012llx",
	    (void *)ixgbp, macaddr));
	ixgb_reg_put32(ixgbp, MAC_ADDRESS_REG(IXGB_RAL, 0), (uint32_t)macaddr);
	macaddr = macaddr >>32;
	ixgb_reg_put32(ixgbp, MAC_ADDRESS_REG(IXGB_RAH, 0),
	    macaddr | IXGB_RAH_AV);

	/*
	 * Zero the other mac address table
	 */
	for (i = 1; i < IXGB_ADDR_NUM; ++i) {
		ixgb_reg_put32(ixgbp, MAC_ADDRESS_REG(IXGB_RAL, i), 0);
		ixgb_reg_clr32(ixgbp, MAC_ADDRESS_REG(IXGB_RAH, i),
		    IXGB_RAH_ADDR_MASK);
	}

	/*
	 * Reprogram the hashed multicast address table ...
	 */
	for (i = 0; i < IXGB_MCA_ENTRY_NUM; ++i)
		ixgb_reg_put32(ixgbp, MCA_ADDRESS_REG(i),
		    ixgbp->mcast_hash[i]);

	/*
	 * Set or clear the PROMISCUOUS mode bit
	 */
	reg = IXGB_RCTL_UPE | IXGB_RCTL_MPE;
	if (ixgbp->promisc == B_TRUE)
		ixgb_reg_set32(ixgbp, IXGB_RCTL, reg);
	else
		ixgb_reg_clr32(ixgbp, IXGB_RCTL, reg);

	/*
	 * Sync the rest of the MAC modes too ...
	 */
	ixgb_sync_flow_modes(ixgbp);

	/*
	 * Restart RX/TX MAC engines if required ...
	 */
	if (ixgb_stop_start_on_sync != 0) {
		(void) ixgb_chip_enable_engine(ixgbp,
		    IXGB_RCTL, IXGB_RCTL_RXEN, 0);
		(void) ixgb_chip_enable_engine(ixgbp,
		    IXGB_TCTL, IXGB_TCTL_TXEN, 0);
	}
}

/*
 * Perform a soft reset of the controller device, resulting in a state
 * nearly-approximating the state following a powerup reset or PCI reset,
 * except for system PCI configuration.
 */
static int
ixgb_chip_reset_global(ixgb_t *ixgbp)
{
	uint32_t subven;
	uint32_t reg;
	uint32_t ok;

	/*
	 * Setup up hardware to known state with global reset.
	 * This resets the chip's tx, rx, DMA, and link units.
	 * Doesn't affect PCI configuration.
	 */
	subven = ixgbp->chipinfo.subven;
	switch (subven) {
	default:
	case SUBVENDOR_ID_SUN:
		/*
		 * Kealia: Set all the GPIO pins as desired. See Kirkwood spec,
		 * page 18.
		 *
		 * SDP0 : (input) reserved
		 * SDP1 : (input) reserved
		 * SDP2 : (output) XFP reset
		 * SDP3 : (output) LXT12101 reset/BCM8704L reset
		 * SDP4:  (input) XFP interrupt
		 * SDP5 : (input) LXT12101/BCM8704L status/interrupt
		 * SDP6 : (output) XFP1 deselect (I2C access)
		 * SDP7 : (output) XFP2 deselect (I2C access)
		 */
		reg = IXGB_XFP_RESET_PIN | IXGB_LXT_RESET_PIN
		    | IXGB_XFP_RESET | IXGB_LXT_RESET | IXGB_CTRL0_CMDC;
		ok = ixgb_chip_reset_engine(ixgbp, IXGB_CTRL0,
		    IXGB_CTRL0_RST, reg, IXGB_RESET_DELAY);

		/*
		 * Enable intr from XFP and LXT12101
		 * Disable 0x2 bit of gpi_en until XFP2 problems are fixed.
		 */
		reg = IXGB_CTRL1_GPI0_EN | IXGB_XFP_INT
		    | IXGB_10G_INT | IXGB_XFP1_DELSEL_PIN
		    | IXGB_XFP2_DELSEL_PIN | IXGB_XFP1_DELSEL
		    | IXGB_XFP2_DELSEL;
		ixgb_reg_put32(ixgbp, IXGB_CTRL1, reg);

		/*
		 * Wait for it to stabilize
		 */
		drv_usecwait(IXGB_MAC_RESET_DELAY);
		reg = ixgb_reg_get32(ixgbp, IXGB_CTRL0);

		/*
		 * Take XFP and LXT12101 out of reset
		 */
		reg &= ~IXGB_XFP_RESET;
		reg |= IXGB_LXT_NO_RESET | IXGB_CTRL0_CMDC;
		ixgb_reg_put32(ixgbp, IXGB_CTRL0, reg);

		/*
		 * Wait for serdes state to stabilize
		 */
		drv_usecwait(IXGB_SERDES_RESET_DELAY);
		break;

	case SUBVENDOR_ID_INTEL:
		/*
		 * Intel's adapter: Set all the GPIO pins as Intel's 10G
		 * adapter.
		 */
		reg = IXGB_CTRL0_CMDC | IXGB_CTRL0_DEFAULT;
		ok = ixgb_chip_reset_engine(ixgbp, IXGB_CTRL0,
		    IXGB_CTRL0_RST, reg, IXGB_RESET_DELAY);
		break;
	}
	if (ok == B_FALSE)  {
		ixgb_problem(ixgbp, "globle reset failed");
		return (IXGB_FAILURE);
	} else
		return (IXGB_SUCCESS);
}


/*
 * Initiate a "reset-like" event to the EEPROM function
 */
static int
ixgb_chip_reset_eeprom(ixgb_t *ixgbp)
{
	uint32_t err;
	uint32_t i;
	uint64_t mac;

	err = ixgb_chip_reset_engine(ixgbp, IXGB_CTRL1, IXGB_CTRL1_EE_RST, 0,
	    IXGB_EE_RESET_DELAY);
	if (err == B_FALSE) {
		ixgb_problem(ixgbp, "reset eeprom failed");
		return (IXGB_FAILURE);
	}

	if (ixgbp->chipinfo.vendor_addr.set != 1) {
		err = ixgb_check_hardsum(ixgbp);
		if (err != IXGB_SUCCESS) {
			ixgb_problem(ixgbp, "hardware checkusm failed");
			return (IXGB_FAILURE);
		}

		mac = ixgb_get_nvmac(ixgbp);
		if (mac != 0ULL && mac != ~0ULL) {
			ixgbp->chipinfo.hw_mac_addr = mac;
			for (i = ETHERADDRL; i-- != 0; ) {
				ixgbp->chipinfo.vendor_addr.addr[i]
				    = (uchar_t)mac;
				mac >>= 8;
			}
			ixgbp->chipinfo.vendor_addr.set = 1;
		}
	}
	return (IXGB_SUCCESS);
}

/*
 * It's a workaround to setup the pcix bridge, and this feature
 * is disable by default. (code comes from ixge)
 */
static ddi_device_acc_attr_t ixgb_dev_attr = {
	DDI_DEVICE_ATTR_V0,
	DDI_STRUCTURE_LE_ACC,
	DDI_STRICTORDER_ACC
};

static void
ixgb_br_setup(ixgb_t *ixgbp)
{
	dev_info_t *pdip;
	ddi_acc_handle_t ixgb_br_pciregh;
	caddr_t ixgb_br_pciregp;
	uint32_t vendor_id, device_id;
	char **prop_val;
	uint_t prop_len;
	uint32_t br_mx_req, br_pf_hi;

	pdip = ddi_get_parent(ixgbp->devinfo);
	if (ddi_prop_lookup_int_array(DDI_DEV_T_ANY, pdip, 0, "vendor-id",
	    (int **)&prop_val, &prop_len) != DDI_PROP_SUCCESS) {
		return;
	}
	vendor_id = *(uint32_t *)prop_val;
	ddi_prop_free(prop_val);
	if (ddi_prop_lookup_int_array(DDI_DEV_T_ANY, pdip, 0, "device-id",
	    (int **)&prop_val, &prop_len) != DDI_PROP_SUCCESS) {
		return;
	}
	device_id = *(uint32_t *)prop_val;
	ddi_prop_free(prop_val);

	if (((vendor_id == 0x8086) &&
	    (device_id == 0x341 || device_id == 0x340)) &&
	    (ddi_regs_map_setup(pdip, 0, (caddr_t *)&ixgb_br_pciregp,
	    0, 0, &ixgb_dev_attr, &ixgb_br_pciregh) == DDI_SUCCESS)) {
		/* i41210 */

		br_mx_req = ddi_get32(ixgb_br_pciregh,
		    (uint32_t *)((uint8_t *)(ixgb_br_pciregp) + 0x4c));
		br_mx_req = (br_mx_req & ~(0x7 << 12)) | (0x5 << 12);
		ddi_put32(ixgb_br_pciregh,
		    (uint32_t *)((uint8_t *)(ixgb_br_pciregp) + 0x4c),
		    br_mx_req);

		br_pf_hi = ddi_get32(ixgb_br_pciregh,
		    (uint32_t *)((uint8_t *)(ixgb_br_pciregp) + 0x17c));
		br_pf_hi |= ((0x3f << 6) | (0x1f << 1));
		ddi_put32(ixgb_br_pciregh,
		    (uint32_t *)((uint8_t *)(ixgb_br_pciregp) + 0x17c),
		    br_pf_hi);

		ddi_regs_map_free(&ixgb_br_pciregh);
	} else if (((vendor_id == 0x10b5) && (device_id == 0x8114)) &&
	    (ddi_regs_map_setup(pdip, 0, (caddr_t *)&ixgb_br_pciregp,
	    0, 0, &ixgb_dev_attr, &ixgb_br_pciregh) == DDI_SUCCESS)) {
		/* pex8114 */

		br_mx_req = ddi_get32(ixgb_br_pciregh,
		    (uint32_t *)((uint8_t *)(ixgb_br_pciregp) + 0x70));
		br_mx_req = (br_mx_req & ~(0x7 << 12)) | (0x5 << 12);
		ddi_put32(ixgb_br_pciregh,
		    (uint32_t *)((uint8_t *)(ixgb_br_pciregp) + 0x70),
		    br_mx_req);

		ddi_regs_map_free(&ixgb_br_pciregh);
	}
}

/*
 * Performs basic configuration of the adapter.
 */
int
ixgb_chip_reset(ixgb_t *ixgbp)
{
	uint32_t i;
	uint32_t entries;

	IXGB_TRACE(("ixgb_chip_reset($%p)", (void *)ixgbp));

	(void) ixgb_chip_reset_global(ixgbp);
	ixgb_chip_cfg_init(ixgbp, &ixgbp->chipinfo, B_TRUE);
	(void) ixgb_chip_reset_eeprom(ixgbp);
	pci_config_put8(ixgbp->cfg_handle, PCI_CONF_CACHE_LINESZ,
	    ixgbp->chipinfo.clsize);
	pci_config_put8(ixgbp->cfg_handle, PCI_CONF_LATENCY_TIMER,
	    ixgbp->chipinfo.latency);

	if (ixgb_br_setup_enable)
		ixgb_br_setup(ixgbp);

	for (i = 0; i < IXGB_ADDR_NUM; ++i) {
		ixgb_reg_put32(ixgbp, MAC_ADDRESS_REG(IXGB_RAL, i), 0x0);
		ixgb_reg_set32(ixgbp, MAC_ADDRESS_REG(IXGB_RAH, i), 0x0);
	}
	for (entries = 0; entries < IXGB_MCA_ENTRY_NUM; entries++) {
		ixgb_reg_put32(ixgbp, MCA_ADDRESS_REG(entries), 0x0);
	}
	for (entries = 0; entries < IXGB_VFTA_ENTRY_NUM; entries++)
		ixgb_reg_put32(ixgbp, VLAN_ID_REG(entries), 0x0);
	(*ixgbp->phys_restart)(ixgbp);
	drv_usecwait(IXGB_SERDES_RESET_DELAY);
	(void) ixgb_serdes_check(ixgbp);
	ixgbp->ixgb_chip_state = IXGB_CHIP_RESET;

	return (IXGB_SUCCESS);
}

/*
 * Setup up hardware to known state with global reset.
 * This resets the chip's tx, rx, DMA, and link units.
 * Doesn't affect PCI configuration.
 *
 */
void
ixgb_chip_stop(ixgb_t *ixgbp, boolean_t fault)
{
	IXGB_TRACE(("ixgb_chip_stop($%p)", (void *)ixgbp));
	ASSERT(mutex_owned(ixgbp->genlock));
	ixgb_chip_stop_unlocked(ixgbp, fault);
}

void
ixgb_chip_stop_unlocked(ixgb_t *ixgbp, boolean_t fault)
{
	ixgb_reg_set32(ixgbp, IXGB_IMC, IXGB_INT_MASK);
	(void) ixgb_reg_get32(ixgbp, IXGB_ICR);	/* clear interrupt */
	(void) ixgb_chip_disable_engine(ixgbp, IXGB_RCTL,
	    IXGB_RCTL_RXEN, 0);
	(void) ixgb_chip_disable_engine(ixgbp, IXGB_TCTL,
	    IXGB_TCTL_TXEN, 0);
	drv_usecwait(IXGB_RESET_DELAY);
	ixgbp->ixgb_chip_state = fault ? IXGB_CHIP_FAULT : IXGB_CHIP_STOPPED;
	ixgbp->link_state = LINK_STATE_UNKNOWN;
}

/*
 * Setup the Tx's resouce and configure tx''s unit.
 * Please refer to Intel's PDM, page 24
 */
static void
ixgb_tx_setup(ixgb_t *ixgbp)
{
	uint64_t paddr;
	uint32_t len;
	uint32_t index;
	uint32_t reg;
	uint32_t maxframe;

	/*
	 * Setting the host address of tx's buffer descriptor
	 * to TDBAL and TDBAH
	 */
	paddr = ixgbp->send->desc.cookie.dmac_laddress;
	ixgb_reg_put32(ixgbp, IXGB_TDBASE, (uint32_t)paddr);
	paddr = paddr >> 32;
	ixgb_reg_put32(ixgbp, IXGB_TDBASE + 4, paddr);

	/*
	 * Setting the length of the whole descriptor ring
	 * to TDLEN
	 */
	len = ixgbp->send->desc.alength;
	ixgb_reg_put32(ixgbp, IXGB_TDLEN, len);

	/*
	 * Initialize the tx's head and tail pointer
	 * to Zero.
	 */
	index = ixgbp->send->tx_next;
	ixgb_reg_put32(ixgbp, IXGB_TDH, index);
	ixgb_reg_put32(ixgbp, IXGB_TDT, index);

	/*
	 * Setting the delay time to report tx's interrupt
	 * to the driver.
	 */
	ixgb_reg_put32(ixgbp, IXGB_TX_COALESCE, IXGB_TX_COALESCE_NUM);

	/*
	 * Eable CRC Padding and Padding short packets to 64 bytes
	 */
	reg = IXGB_TCTL_TCE | IXGB_TCTL_TPDE;
	ixgb_reg_set32(ixgbp, IXGB_TCTL, reg);

	/*
	 * Errata: Do not set PTHRESH and HTHRESH to a non-zero value
	 */
	reg = IXGB_TXDCTL_HTHRESH_MASK | IXGB_TXDCTL_PTHRESH_MASK;
	ixgb_reg_clr32(ixgbp, IXGB_TXDCTL, reg);

	/*
	 * Setting the Max length of Tx's messages
	 */
	maxframe = ixgbp->max_frame + ETHERFCSL;
	ixgb_reg_put32(ixgbp, IXGB_MFS, maxframe << IXGB_MFS_SHIFT);

	/*
	 * Enable Tx's state machine
	 */
	(void) ixgb_chip_enable_engine(ixgbp, IXGB_TCTL, IXGB_TCTL_TXEN, 0);
}

/*
 * Setup the Rx's resouce and configure rx's unit.
 */
static void
ixgb_rx_setup(ixgb_t *ixgbp)
{
	uint64_t paddr;
	uint32_t len;
	uint64_t index_head;
	uint64_t index_tail;
	uint32_t reg;
	uint32_t high_water;
	uint32_t delay_time;
	uint32_t poll_time;

	/*
	 * Setting Rx's parameters:
	 *  Rx' buff size,
	 *  Multicast filter rules,
	 *  ....
	 */
	switch (ixgbp->buf_size) {
	default:
		ixgb_error(ixgbp, "ixgb_rx_setup: invalid buf_size %d",
		    ixgbp->buf_size);
		reg = IXGB_RCTL_BSIZE_2048;
		break;
	case IXGB_BUF_SIZE_2K:
		reg = IXGB_RCTL_BSIZE_2048;
		break;
	case IXGB_BUF_SIZE_4K:
		reg = IXGB_RCTL_BSIZE_4096;
		break;
	case IXGB_BUF_SIZE_8K:
		reg = IXGB_RCTL_BSIZE_8192;
		break;
	case IXGB_BUF_SIZE_16K:
		reg = IXGB_RCTL_BSIZE_16384;
		break;
	}
	switch (ixgbp->mcast_filter) {
	default:
		reg |= IXGB_RCTL_MO_47_36;
		ixgb_error(ixgbp, "errored fileter type = 0x%x",
		    ixgbp->mcast_filter);
		break;

	case FILTER_TYPE0:
		reg |= IXGB_RCTL_MO_47_36;
		break;

	case FILTER_TYPE1:
		reg |= IXGB_RCTL_MO_46_35;
		break;

	case FILTER_TYPE2:
		reg |= IXGB_RCTL_MO_45_34;
		break;

	case FILTER_TYPE3:
		reg |= IXGB_RCTL_MO_43_32;
		break;
	}
	reg |= IXGB_RCTL_BAM | IXGB_RCTL_SECRC | IXGB_RCTL_RXEN;
	reg |= IXGB_RCTL_RDMTS_1_4;
	reg |= IXGB_RCTL_RPDA_DEFAULT;
	ixgb_reg_put32(ixgbp, IXGB_RCTL, reg);

	/*
	 * Setting the host address of rx's buffer descriptor
	 * to TDBAL and TDBAH
	 */
	paddr = ixgbp->recv->desc.cookie.dmac_laddress;
	ixgb_reg_put32(ixgbp, IXGB_RDBASE, (uint32_t)paddr);
	paddr = paddr >> 32;
	ixgb_reg_put32(ixgbp, IXGB_RDBASE + 4, paddr);

	/*
	 * Setting the length of the whole descriptor ring
	 * to TDLEN
	 */
	len = ixgbp->recv->desc.alength;
	ixgb_reg_put32(ixgbp, IXGB_RDLEN, len);

	/*
	 * Initialize the rx's head and tail pointer
	 * to Zero.
	 */
	index_head = ixgbp->recv->rx_next;
	index_tail = ixgbp->recv->rx_tail;
	ixgb_reg_put32(ixgbp, IXGB_RDH, index_head);
	ixgb_reg_put32(ixgbp, IXGB_RDT, index_tail);

	/*
	 * Setting the delay time to report rx's interrupt
	 * to the driver.
	 */
	reg = ixgbp->rdelay;
	ixgb_reg_put32(ixgbp, IXGB_RDTR, reg);

	/*
	 * Errata: Do not set WTHRESH to a non-zero value;
	 * Do not change PTHRESH and HTHRESH from their default values.
	 */
	ixgb_reg_clr32(ixgbp, IXGB_RXDCTL, IXGB_RXDCTL_WTHRESH_MASK);

	/*
	 * Poll every rx_int_delay period, if RBD exists
	 * Receive Backlog Detection is set to <threshold>
	 * Rx Descriptors
	 * max is 0x3F == set to poll when 504 RxDesc left
	 * min is 0
	 * please refer to intel's 82597ex's PDM, page 143
	 */
	if (ixgbp->adaptive_int) {
		high_water = 0x3f;
		delay_time = ixgbp->rdelay << IXGB_RAIDC_DELAY_SHIFT;
		delay_time &= IXGB_RAIDC_DELAY_MASK;
		poll_time = IXGB_RAIDC_POLL_DEFAULT << IXGB_RAIDC_POLL_SHIFT;
		poll_time &= IXGB_RAIDC_POLL_MASK;
		reg = high_water | delay_time | poll_time
		    | IXGB_RAIDC_RXT_GATE | IXGB_RAIDC_EN;
		ixgb_reg_put32(ixgbp, IXGB_RAIDC, reg);
	}

	/*
	 * Enable Receive Checksum Offload for IP.
	 * If Os support TCP/UDP full checksum, enable TCP/UDP.
	 */
	if (ixgbp->rx_hw_chksum) {
		reg = IXGB_RXCSUM_IPOFL | IXGB_RXCSUM_TUOFL;
		ixgb_reg_put32(ixgbp, IXGB_RXCSUM, reg);
	}

	/*
	 * Enable jumbo support
	 */
	ixgb_reg_set32(ixgbp, IXGB_CTRL0, IXGB_CTRL0_JFE | IXGB_CTRL0_CMDC);

	/*
	 * Enable Rx's state machine
	 */
	(void) ixgb_chip_enable_engine(ixgbp, IXGB_RCTL, IXGB_RCTL_RXEN, 0);
}

/*
 * start the chip transmitting and/or receiving,
 * including enabling interrupts
 */
void
ixgb_chip_start(ixgb_t *ixgbp)
{
	/*
	 * Set up tx/rx parameters
	 */
	ixgb_tx_setup(ixgbp);
	ixgb_rx_setup(ixgbp);
	ixgb_chip_sync(ixgbp);

	/*
	 * Enable the rx/tx/link interrupt
	 */
	ixgb_reg_set32(ixgbp, IXGB_IMS,
	    IXGB_INT_TX | IXGB_INT_RX | IXGB_INT_LSC);
	ixgbp->ixgb_chip_state = IXGB_CHIP_RUNNING;
}

/*
 * Factotum routine to check for Tx stall, using the 'watchdog' counter
 */
static boolean_t ixgb_factotum_stall_check(ixgb_t *ixgbp);
#pragma	no_inline(ixgb_factotum_stall_check)

static boolean_t
ixgb_factotum_stall_check(ixgb_t *ixgbp)
{
	uint32_t dogval;

	ASSERT(mutex_owned(ixgbp->genlock));

	/*
	 * Specific check for Tx stall ...
	 *
	 * The 'watchdog' counter is incremented whenever a packet
	 * is queued, reset to 1 when some (but not all) buffers
	 * are reclaimed, reset to 0 (disabled) when all buffers
	 * are reclaimed, and shifted left here.  If it exceeds the
	 * threshold value, the chip is assumed to have stalled and
	 * is put into the ERROR state.  The factotum will then reset
	 * it on the next pass.
	 *
	 * All of which should ensure that we don't get into a state
	 * where packets are left pending indefinitely!
	 */
	dogval = ixgb_atomic_shl32(&ixgbp->watchdog, 1);
	if (dogval < ixgb_watchdog_count ||ixgb_tx_recycle(ixgbp))
		return (B_FALSE);

	IXGB_REPORT((ixgbp, "Tx stall detected, watchdog code 0x%x", dogval));
	return (B_TRUE);

}


/*
 * The factotum is woken up when there's something to do that we'd rather
 * not do from inside a hardware interrupt handler or high-level cyclic.
 * Its two main tasks are:
 *	reset & restart the chip after an error
 *	check the link status whenever necessary
 */
uint_t
ixgb_chip_factotum(caddr_t args)
{
	ixgb_t *ixgbp = (ixgb_t *)args;
	uint_t result;
	boolean_t linkchg;
	boolean_t error;


	IXGB_TRACE(("ixgb_chip_factotum($%p)", (void *)ixgbp));

	mutex_enter(ixgbp->softintr_lock);
	if (ixgbp->factotum_flag == 0) {
		mutex_exit(ixgbp->softintr_lock);
		return (DDI_INTR_UNCLAIMED);
	}
	ixgbp->factotum_flag = 0;
	mutex_exit(ixgbp->softintr_lock);

	result = DDI_INTR_CLAIMED;
	linkchg = B_FALSE;
	error = B_FALSE;

	mutex_enter(ixgbp->genlock);
	switch (ixgbp->ixgb_chip_state) {
	default:
		break;

	case IXGB_CHIP_RUNNING:
		linkchg = ixgb_serdes_check(ixgbp);
		error = ixgb_factotum_stall_check(ixgbp);
		break;

	case IXGB_CHIP_ERROR:
		error = B_TRUE;
		break;

	case IXGB_CHIP_FAULT:
		/*
		 * Fault detected, time to reset ...
		 */
		IXGB_REPORT((ixgbp, "automatic recovery activated"));
		ixgb_restart(ixgbp);
		break;
	}

	/*
	 * If an error is detected, stop the chip now, marking it as
	 * faulty, so that it will be reset next time through ...
	 */
	if (error)
		ixgb_chip_stop(ixgbp, B_TRUE);
	mutex_exit(ixgbp->genlock);

	/*
	 * If the link state changed, tell the world about it (if
	 * this version of GLD supports link state notification).
	 * Note: can't do this while still holding the mutex.
	 */
	if (linkchg)
		mac_link_update(ixgbp->mh, ixgbp->link_state);

	return (result);
}

static void ixgb_chip_peek_cfg(ixgb_t *ixgbp, ixgb_peekpoke_t *ppd);
#pragma	no_inline(ixgb_chip_peek_cfg)

static void
ixgb_chip_peek_cfg(ixgb_t *ixgbp, ixgb_peekpoke_t *ppd)
{
	uint64_t regval;
	uint64_t regno;

	IXGB_TRACE(("ixgb_chip_peek_cfg($%p, $%p)",
	    (void *)ixgbp, (void *)ppd));

	regno = ppd->pp_acc_offset;

	switch (ppd->pp_acc_size) {
	case 1:
		regval = pci_config_get8(ixgbp->cfg_handle, regno);
		break;

	case 2:
		regval = pci_config_get16(ixgbp->cfg_handle, regno);
		break;

	case 4:
		regval = pci_config_get32(ixgbp->cfg_handle, regno);
		break;

	case 8:
		regval = pci_config_get64(ixgbp->cfg_handle, regno);
		break;
	}

	ppd->pp_acc_data = regval;
}

static void ixgb_chip_poke_cfg(ixgb_t *ixgbp, ixgb_peekpoke_t *ppd);
#pragma	no_inline(ixgb_chip_poke_cfg)

static void
ixgb_chip_poke_cfg(ixgb_t *ixgbp, ixgb_peekpoke_t *ppd)
{
	uint64_t regval;
	uint64_t regno;

	IXGB_TRACE(("ixgb_chip_poke_cfg($%p, $%p)",
	    (void *)ixgbp, (void *)ppd));

	regno = ppd->pp_acc_offset;
	regval = ppd->pp_acc_data;

	switch (ppd->pp_acc_size) {
	case 1:
		pci_config_put8(ixgbp->cfg_handle, regno, regval);
		break;

	case 2:
		pci_config_put16(ixgbp->cfg_handle, regno, regval);
		break;

	case 4:
		pci_config_put32(ixgbp->cfg_handle, regno, regval);
		break;

	case 8:
		pci_config_put64(ixgbp->cfg_handle, regno, regval);
		break;
	}
}

static void ixgb_chip_peek_reg(ixgb_t *ixgbp, ixgb_peekpoke_t *ppd);
#pragma	no_inline(ixgb_chip_peek_reg)

static void
ixgb_chip_peek_reg(ixgb_t *ixgbp, ixgb_peekpoke_t *ppd)
{
	uint64_t regval;
	void *regaddr;

	IXGB_TRACE(("ixgb_chip_peek_reg($%p, $%p)",
	    (void *)ixgbp, (void *)ppd));

	regaddr = PIO_ADDR(ixgbp, ppd->pp_acc_offset);

	switch (ppd->pp_acc_size) {
	case 1:
		regval = ddi_get8(ixgbp->io_handle, regaddr);
		break;

	case 2:
		regval = ddi_get16(ixgbp->io_handle, regaddr);
		break;

	case 4:
		regval = ddi_get32(ixgbp->io_handle, regaddr);
		break;

	case 8:
		regval = ddi_get64(ixgbp->io_handle, regaddr);
		break;

	default:
		regval = 0x0ull;
		break;
	}
	ppd->pp_acc_data = regval;
}

static void ixgb_chip_poke_reg(ixgb_t *ixgbp, ixgb_peekpoke_t *ppd);
#pragma	no_inline(ixgb_chip_peek_reg)

static void
ixgb_chip_poke_reg(ixgb_t *ixgbp, ixgb_peekpoke_t *ppd)
{
	uint64_t regval;
	void *regaddr;

	IXGB_TRACE(("ixgb_chip_poke_reg($%p, $%p)",
	    (void *)ixgbp, (void *)ppd));

	regaddr = PIO_ADDR(ixgbp, ppd->pp_acc_offset);
	regval = ppd->pp_acc_data;

	switch (ppd->pp_acc_size) {
	case 1:
		ddi_put8(ixgbp->io_handle, regaddr, regval);
		break;

	case 2:
		ddi_put16(ixgbp->io_handle, regaddr, regval);
		break;

	case 4:
		ddi_put32(ixgbp->io_handle, regaddr, regval);
		break;

	case 8:
		ddi_put64(ixgbp->io_handle, regaddr, regval);
		break;
	}
}

static void ixgb_chip_peek_txd(ixgb_t *ixgbp, ixgb_peekpoke_t *ppd);
#pragma	no_inline(ixgb_chip_peek_txd)

static void
ixgb_chip_peek_txd(ixgb_t *ixgbp, ixgb_peekpoke_t *ppd)
{
	uint64_t bd_val;
	uint32_t bd_num;
	ixgb_regno_t bdaddr;

	IXGB_TRACE(("ixgb_chip_peek_txd($%p, $%p)",
	    (void *)ixgbp, (void *)ppd));

	bd_num = ppd->pp_acc_offset;
	ixgb_reg_set32(ixgbp, IXGB_TPR, IXGB_TPR_MEM_SEL);
	bdaddr = IXGB_TPDBM_BASE + sizeof (uint32_t) * bd_num;
	bd_val = ixgb_reg_get32(ixgbp, bdaddr);
	ppd->pp_acc_data = bd_val;
}

static void
ixgb_chip_peek_rxd(ixgb_t *ixgbp, ixgb_peekpoke_t *ppd)
{
	uint64_t bd_val;
	uint32_t bd_num;
	ixgb_regno_t bdaddr;

	IXGB_TRACE(("ixgb_chip_peek_rxd($%p, $%p)",
	    (void *)ixgbp, (void *)ppd));

	bd_num = ppd->pp_acc_offset;
	ixgb_reg_set32(ixgbp, IXGB_RPR, IXGB_RPR_MEM_SEL);
	bdaddr = IXGB_RPDBM_BASE + sizeof (uint32_t) * bd_num;
	bd_val = ixgb_reg_get32(ixgbp, bdaddr);
	ppd->pp_acc_data = bd_val;
}

static enum ioc_reply
ixgb_pp_ioctl(ixgb_t *ixgbp, int cmd, mblk_t *mp,
    struct iocblk *iocp);
#pragma	no_inline(ixgb_pp_ioctl)

static enum ioc_reply
ixgb_pp_ioctl(ixgb_t *ixgbp, int cmd, mblk_t *mp, struct iocblk *iocp)
{
	void (*ppfn)(ixgb_t *ixgbp, ixgb_peekpoke_t *ppd);
	ixgb_peekpoke_t *ppd;
	uint64_t sizemask;
	uint64_t mem_va;
	uint64_t maxoff;
	boolean_t peek;

	switch (cmd) {
	default:
		ixgb_error(ixgbp, "ixgb_pp_ioctl: invalid cmd 0x%x", cmd);
		return (IOC_INVAL);

	case IXGB_PEEK:
		peek = B_TRUE;
		break;

	case IXGB_POKE:
		peek = B_FALSE;
		break;
	}

	/*
	 * Validate format of ioctl
	 */
	if (iocp->ioc_count != sizeof (ixgb_peekpoke_t))
		return (IOC_INVAL);
	if (mp->b_cont == NULL)
		return (IOC_INVAL);
	ppd = (ixgb_peekpoke_t *)mp->b_cont->b_rptr;

	/*
	 * Validate request parameters
	 */
	switch (ppd->pp_acc_space) {
	default:
		return (IOC_INVAL);

	case IXGB_PP_SPACE_CFG:
		/*
		 * Config space
		 */
		sizemask = 8|4|2|1;
		mem_va = 0;
		maxoff = PCI_CONF_HDR_SIZE;
		ppfn = peek ? ixgb_chip_peek_cfg : ixgb_chip_poke_cfg;
		break;

	case IXGB_PP_SPACE_REG:
		/*
		 * Memory-mapped I/O space
		 */
		sizemask = 8|4|2|1;
		mem_va = 0;
		maxoff = IXGB_REG_SIZE;
		ppfn = peek ? ixgb_chip_peek_reg : ixgb_chip_poke_reg;
		break;

	case IXGB_PP_SPACE_TXDESC:
		sizemask = 8|4|2|1;
		mem_va = 0;
		maxoff = IXGB_REG_SIZE;
		ppfn = peek ? ixgb_chip_peek_txd : NULL;
		break;

	case IXGB_PP_SPACE_RXDESC:
	sizemask = 8|4|2|1;
	mem_va = 0;
	maxoff = IXGB_REG_SIZE;
	ppfn = peek ? ixgb_chip_peek_rxd : NULL;
	break;

	case IXGB_PP_SPACE_SEEPROM:
		/*
		 * Attached SEEPROM(s), if any.
		 * NB: we use the high-order bits of the 'address' as
		 * a device select to accommodate multiple SEEPROMS,
		 * If each one is the maximum size (64kbytes), this
		 * makes them appear contiguous.  Otherwise, there may
		 * be holes in the mapping.  ENxS doesn't have any
		 * SEEPROMs anyway ...
		 */
		sizemask = 4;
		mem_va = 0;
		maxoff = NVMEM_SIZE;
		ppfn = peek ? ixgb_chip_peek_seeprom : ixgb_chip_poke_seeprom;
		break;
	}

	switch (ppd->pp_acc_size) {
	default:
		return (IOC_INVAL);

	case 8:
	case 4:
	case 2:
	case 1:
		if ((ppd->pp_acc_size & sizemask) == 0)
			return (IOC_INVAL);
		break;
	}

	if ((ppd->pp_acc_offset % ppd->pp_acc_size) != 0)
		return (IOC_INVAL);

	if (ppd->pp_acc_offset >= maxoff)
		return (IOC_INVAL);

	if (ppd->pp_acc_offset+ppd->pp_acc_size > maxoff)
		return (IOC_INVAL);

	/*
	 * All OK - go do it!
	 */
	ppd->pp_acc_offset += mem_va;
	if (ppfn)
		(*ppfn)(ixgbp, ppd);
	return (peek ? IOC_REPLY : IOC_ACK);
}

static enum ioc_reply ixgb_diag_ioctl(ixgb_t *ixgbp, int cmd, mblk_t *mp,
					struct iocblk *iocp);
#pragma	no_inline(ixgb_diag_ioctl)

static enum ioc_reply
ixgb_diag_ioctl(ixgb_t *ixgbp, int cmd, mblk_t *mp, struct iocblk *iocp)
{
	ASSERT(mutex_owned(ixgbp->genlock));

	switch (cmd) {
	default:
		ixgb_error(ixgbp, "ixgb_diag_ioctl: invalid cmd 0x%x", cmd);
		return (IOC_INVAL);

	case IXGB_DIAG:
		return (IOC_ACK);

	case IXGB_PEEK:
	case IXGB_POKE:
		return (ixgb_pp_ioctl(ixgbp, cmd, mp, iocp));

	case IXGB_PHY_RESET:
		return (IOC_RESTART_ACK);

	case IXGB_SOFT_RESET:
	case IXGB_HARD_RESET:
		return (IOC_ACK);
	}

	/* NOTREACHED */
}

enum ioc_reply ixgb_chip_ioctl(ixgb_t *ixgbp, mblk_t *mp,
				struct iocblk *iocp);
#pragma	no_inline(ixgb_chip_ioctl)

enum ioc_reply
ixgb_chip_ioctl(ixgb_t *ixgbp, mblk_t *mp, struct iocblk *iocp)
{
	int cmd;

	IXGB_TRACE(("ixgb_chip_ioctl($%p, $%p, $%p, $%p)",
	    (void *)ixgbp, (void *)mp, (void *)iocp));

	ASSERT(mutex_owned(ixgbp->genlock));

	cmd = iocp->ioc_cmd;

	IXGB_DEBUG(("ioctl's cmd = 0x%x", cmd));
	switch (cmd) {
	default:
		ixgb_error(ixgbp, "ixgb_chip_ioctl: invalid cmd 0x%x", cmd);
		return (IOC_INVAL);

	case IXGB_DIAG:
	case IXGB_PEEK:
	case IXGB_POKE:
	case IXGB_PHY_RESET:
	case IXGB_SOFT_RESET:
	case IXGB_HARD_RESET:
#if	IXGB_DEBUGGING
		return (ixgb_diag_ioctl(ixgbp, cmd, mp, iocp));
#else
		return (IOC_INVAL);
#endif

	case IXGB_MII_READ:
	case IXGB_MII_WRITE:
		return (IOC_INVAL);

#if	IXGB_SEE_IO32
	case IXGB_SEE_READ:
	case IXGB_SEE_WRITE:
		return (IOC_INVAL);
#endif

#if	IXGB_FLASH_IO32
	case IXGB_FLASH_READ:
	case IXGB_FLASH_WRITE:
		return (IOC_INVAL);
#endif
	}
}

/* ARGSUSED */
void
ixgb_chip_blank(void *arg, time_t ticks, uint_t count, int flag)
{
	_NOTE(ARGUNUSED(arg, ticks, count));
}
