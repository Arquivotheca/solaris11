/*
 * Copyright (c) 2009, 2011, Oracle and/or its affiliates. All rights reserved.
 */

/*
 * Copyright (c) 2010, Intel Corporation
 * All rights reserved.
 */

/*
 * Copyright (c) 2006
 * Copyright (c) 2007
 *      Damien Bergamini <damien.bergamini@free.fr>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/types.h>
#include <sys/byteorder.h>
#include <sys/conf.h>
#include <sys/cmn_err.h>
#include <sys/stat.h>
#include <sys/ddi.h>
#include <sys/sunddi.h>
#include <sys/strsubr.h>
#include <sys/ethernet.h>
#include <inet/common.h>
#include <inet/nd.h>
#include <inet/mi.h>
#include <sys/note.h>
#include <sys/stream.h>
#include <sys/strsun.h>
#include <sys/modctl.h>
#include <sys/devops.h>
#include <sys/dlpi.h>
#include <sys/mac_provider.h>
#include <sys/mac_wifi.h>
#include <sys/net80211.h>
#include <sys/net80211_proto.h>
#include <sys/varargs.h>
#include <sys/policy.h>
#include <sys/pci.h>

#include "iwp_calibration.h"
#include "iwp_hardware.h"
#include "iwp_base.h"

extern void	iwp_mac_access_enter(iwp_sc_t *);
extern void	iwp_mac_access_exit(iwp_sc_t *);
extern uint32_t	iwp_reg_read(iwp_sc_t *, uint32_t);
extern void	iwp_reg_write(iwp_sc_t *, uint32_t, uint32_t);

static int
iwp_nvm_verify_sig(iwp_sc_t *sc)
{
	uint32_t eep_gp;

	eep_gp = IWP_READ(sc, CSR_EEPROM_GP);
	switch (eep_gp & CSR_EEPROM_GP_VALID_MSK) {
	case CSR_EEPROM_GP_GOOD_SIG_EEP_LESS_4K:
	case CSR_EEPROM_GP_GOOD_SIG_EEP_MORE_4K:
		if (sc->sc_nvm_type != NVM_TYPE_EEPROM) {
			cmn_err(CE_WARN, "iwp_nvm_verify_sig(): "
			    "OTP but bad signature\n");
			return (IWP_FAIL);
		}
		break;

	case CSR_EEPROM_GP_BAD_SIG_EEP_GOOD_SIG_OTP:
		if (sc->sc_nvm_type != NVM_TYPE_OTP) {
			cmn_err(CE_WARN, "iwp_nvm_verify_sig(): "
			    "EEPROM but bad signature\n");
			return (IWP_FAIL);
		}
		break;

	case CSR_EEPROM_GP_BAD_SIG_BOTH_EEP_AND_OTP:
	default:
		cmn_err(CE_WARN, "iwp_nvm_verify_sig(): "
		    "bad OTP and EEPROM signature\n");
		return (IWP_FAIL);
	}

	return (IWP_SUCCESS);
}

/*
 * set up semphore flag to own NVM
 */
static int
iwp_nvm_sem_down(iwp_sc_t *sc)
{
	int count1, count2;
	uint32_t tmp;

	for (count1 = 0; count1 < 1000; count1++) {
		tmp = IWP_READ(sc, CSR_HW_IF_CONFIG_REG);
		IWP_WRITE(sc, CSR_HW_IF_CONFIG_REG,
		    tmp | CSR_HW_IF_CONFIG_REG_EEP_SEM);

		for (count2 = 0; count2 < 2; count2++) {
			if (IWP_READ(sc, CSR_HW_IF_CONFIG_REG) &
			    CSR_HW_IF_CONFIG_REG_EEP_SEM) {
				return (IWP_SUCCESS);
			}
			DELAY(10000);
		}
	}
	return (IWP_FAIL);
}

/*
 * reset semphore flag to release NVM
 */
static void
iwp_nvm_sem_up(iwp_sc_t *sc)
{
	uint32_t tmp;

	tmp = IWP_READ(sc, CSR_HW_IF_CONFIG_REG);
	IWP_WRITE(sc, CSR_HW_IF_CONFIG_REG,
	    tmp & (~CSR_HW_IF_CONFIG_REG_EEP_SEM));
}

static int
iwp_otp_init(iwp_sc_t *sc)
{
	int n;
	uint32_t tmp;

	tmp = IWP_READ(sc, CSR_GP_CNTRL);
	IWP_WRITE(sc, CSR_GP_CNTRL, tmp | CSR_GP_CNTRL_REG_FLAG_INIT_DONE);

	/*
	 * wait for clock ready
	 */
	for (n = 0; n < 2500; n++) {
		if (IWP_READ(sc, CSR_GP_CNTRL) &
		    CSR_GP_CNTRL_REG_FLAG_MAC_CLOCK_READY) {
			break;
		}
		DELAY(10);
	}

	if (2500 == n) {
		return (IWP_FAIL);
	} else {
		/*
		 * make sure power supply on each part of the hardware
		 */
		iwp_mac_access_enter(sc);
		tmp = iwp_reg_read(sc, ALM_APMG_PS_CTL);
		tmp |= APMG_PS_CTRL_REG_VAL_ALM_R_RESET_REQ;
		iwp_reg_write(sc, ALM_APMG_PS_CTL, tmp);
		DELAY(5);

		tmp = iwp_reg_read(sc, ALM_APMG_PS_CTL);
		tmp &= ~APMG_PS_CTRL_REG_VAL_ALM_R_RESET_REQ;
		iwp_reg_write(sc, ALM_APMG_PS_CTL, tmp);
		iwp_mac_access_exit(sc);

		if (sc->sc_chip_param->shadowram_support) {
			tmp = IWP_READ(sc, CSR_DBG_LINK_PWR_MGMT);
			IWP_WRITE(sc, CSR_DBG_LINK_PWR_MGMT,
			    tmp | CSR_RESET_LINK_PWR_MGMT_DISABLED);
		}
	}

	return (IWP_SUCCESS);
}

static int
iwp_otp_read_16(iwp_sc_t *sc, uint16_t addr, uint16_t *data)
{
	int n;
	uint32_t rv, tmp, otp_gp;

	IWP_WRITE(sc, CSR_EEPROM_REG,
	    (addr << 1) & CSR_EEPROM_REG_MSK_ADDR);

	for (n = 0; n < 500; n++) {
		if (IWP_READ(sc, CSR_EEPROM_REG) &
		    CSR_EEPROM_REG_READ_VALID_MSK) {
			break;
		}
		DELAY(10);
	}

	if (500 == n) {
		cmn_err(CE_WARN, "iwp_otp_read_16(): "
		    "timeout for reading OTP\n");
		return (IWP_FAIL);
	}

	rv = IWP_READ(sc, CSR_EEPROM_REG);

	otp_gp = IWP_READ(sc, CSR_OTP_GP);
	if (otp_gp & CSR_OTP_GP_ECC_UNCORR_STATUS_MSK) {
		tmp = IWP_READ(sc, CSR_OTP_GP);
		tmp |= CSR_OTP_GP_ECC_UNCORR_STATUS_MSK;
		IWP_WRITE(sc, CSR_OTP_GP, tmp);
		cmn_err(CE_WARN, "iwp_otp_reak_16(): "
		    "ECC error for reading OTP\n");
		return (IWP_FAIL);
	}
	if (otp_gp & CSR_OTP_GP_ECC_CORR_STATUS_MSK) {
		tmp = IWP_READ(sc, CSR_OTP_GP);
		tmp |= CSR_OTP_GP_ECC_CORR_STATUS_MSK;
		IWP_WRITE(sc, CSR_OTP_GP, tmp);
	}

	*data = LE_16(rv >> 16);
	return (IWP_SUCCESS);
}

static int
iwp_find_eepimage_in_otp(iwp_sc_t *sc, uint16_t *image_addr)
{
	uint32_t tmp;
	uint16_t data, nextaddr = 0, validaddr;
	int block_count = 0;

	tmp = IWP_READ(sc, CSR_OTP_GP);
	tmp &= ~CSR_OTP_GP_ACCESS_MODE;
	IWP_WRITE(sc, CSR_OTP_GP, tmp);

	if (iwp_otp_read_16(sc, 0, &data) == IWP_SUCCESS) {
		if (!data) {
			cmn_err(CE_WARN, "iwp_find_eepimage_in_otp(): "
			    "empty OTP\n");
			return (IWP_FAIL);
		}
	} else {
		return (IWP_FAIL);
	}

	data = 0;

	do {
		validaddr = nextaddr;
		nextaddr = LE_16(data) * sizeof (uint16_t);

		if (iwp_otp_read_16(sc, nextaddr, &data) != IWP_SUCCESS) {
			return (IWP_FAIL);
		}

		if (!data) {
			*image_addr = validaddr;
			*image_addr += 2;

			return (IWP_SUCCESS);
		}

		block_count++;
	} while (block_count <= sc->sc_chip_param->max_ll_items);

	cmn_err(CE_WARN, "iwp_find_eepimage_in_otp(): "
	    "no valid blocks with OTP\n");

	return (IWP_FAIL);
}

/*
 * This function read all infomation from eeprom or OTP
 */
int
iwp_nvm_load(iwp_sc_t *sc)
{
	int i, rr;
	uint32_t rv, tmp;
	uint16_t addr, nvm_sz = sizeof (sc->sc_eep_map);
	uint16_t *eep_p = (uint16_t *)&sc->sc_eep_map;
	uint16_t otp_addr = 0, otp_data, addr1 = 0;

	sc->sc_nvm_type = sc->sc_chip_param->determine_nvm_type(sc);

	if (iwp_nvm_verify_sig(sc) != IWP_SUCCESS) {
		return (IWP_FAIL);
	}

	rr = iwp_nvm_sem_down(sc);
	if (rr != IWP_SUCCESS) {
		IWP_DBG((IWP_DEBUG_EEPROM, "iwp_nvm_load(): "
		    "driver failed to own EEPROM\n"));
		return (IWP_FAIL);
	}

	if (NVM_TYPE_EEPROM == sc->sc_nvm_type) {
		for (addr = 0; addr < nvm_sz; addr += 2) {
			IWP_WRITE(sc, CSR_EEPROM_REG, addr<<1);
			tmp = IWP_READ(sc, CSR_EEPROM_REG);
			IWP_WRITE(sc, CSR_EEPROM_REG, tmp & ~(0x2));

			for (i = 0; i < 10; i++) {
				rv = IWP_READ(sc, CSR_EEPROM_REG);
				if (rv & 1) {
					break;
				}
				DELAY(10);
			}

			if (!(rv & 1)) {
				IWP_DBG((IWP_DEBUG_EEPROM, "iwp_nvm_load(): "
				    "time out when read eeprome\n"));
				iwp_nvm_sem_up(sc);
				return (IWP_FAIL);
			}

			eep_p[addr/2] = LE_16(rv >> 16);
		}
	} else {
		rr = iwp_otp_init(sc);
		if (rr != IWP_SUCCESS) {
			cmn_err(CE_WARN, "iwp_nvm_load(): "
			    "failed to initialize OTP.\n");
			iwp_nvm_sem_up(sc);
			return (IWP_FAIL);
		}

		tmp = IWP_READ(sc, CSR_EEPROM_GP);
		tmp &= ~CSR_EEPROM_GP_IF_OWNER_MSK;
		IWP_WRITE(sc, CSR_EEPROM_GP, tmp);

		tmp = IWP_READ(sc, CSR_OTP_GP);
		tmp |= CSR_OTP_GP_ECC_CORR_STATUS_MSK |
		    CSR_OTP_GP_ECC_UNCORR_STATUS_MSK;
		IWP_WRITE(sc, CSR_OTP_GP, tmp);

		if (!sc->sc_chip_param->shadowram_support) {
			if (iwp_find_eepimage_in_otp(sc, &otp_addr) !=
			    IWP_SUCCESS) {
				iwp_nvm_sem_up(sc);
				return (IWP_FAIL);
			}
		}

		for (addr = otp_addr; addr < otp_addr + nvm_sz;
		    addr += sizeof (uint16_t)) {
			rr = iwp_otp_read_16(sc, addr, &otp_data);
			if (rr != IWP_SUCCESS) {
				iwp_nvm_sem_up(sc);
				return (IWP_FAIL);
			}

			eep_p[addr1/2] = otp_data;
			addr1 += sizeof (uint16_t);
		}
	}

	iwp_nvm_sem_up(sc);
	return (IWP_SUCCESS);
}

/*
 * Check EEPROM version and Calibration version.
 */
int
iwp_nvm_ver_chk(iwp_sc_t *sc)
{
	if ((IWP_READ_EEP_SHORT(sc, EEP_VERSION) <
	    sc->sc_chip_param->eeprom_ver) ||
	    (sc->sc_eep_calib->tx_pow_calib_hdr.calib_version <
	    sc->sc_chip_param->eeprom_calib_ver)) {
		cmn_err(CE_WARN, "iwp_nvm_ver_chk(): "
		    "unsupported eeprom detected\n");
		return (IWP_FAIL);
	}

	return (IWP_SUCCESS);
}

/*
 * translate indirect address in eeprom to direct address
 * in eeprom and return address of entry whos indirect address
 * is indi_addr
 */
uint8_t *
iwp_eep_addr_trans(iwp_sc_t *sc, uint32_t indi_addr)
{
	uint32_t di_addr;
	uint16_t temp;

	if (!(indi_addr & INDIRECT_ADDRESS)) {
		di_addr = indi_addr;
		return (&sc->sc_eep_map[di_addr]);
	}

	switch (indi_addr & INDIRECT_TYPE_MSK) {
	case INDIRECT_GENERAL:
		temp = IWP_READ_EEP_SHORT(sc, EEP_LINK_GENERAL);
		break;
	case INDIRECT_HOST:
		temp = IWP_READ_EEP_SHORT(sc, EEP_LINK_HOST);
		break;
	case INDIRECT_REGULATORY:
		temp = IWP_READ_EEP_SHORT(sc, EEP_LINK_REGULATORY);
		break;
	case INDIRECT_CALIBRATION:
		temp = IWP_READ_EEP_SHORT(sc, EEP_LINK_CALIBRATION);
		break;
	case INDIRECT_PROCESS_ADJST:
		temp = IWP_READ_EEP_SHORT(sc, EEP_LINK_PROCESS_ADJST);
		break;
	case INDIRECT_OTHERS:
		temp = IWP_READ_EEP_SHORT(sc, EEP_LINK_OTHERS);
		break;
	default:
		temp = 0;
		cmn_err(CE_WARN, "iwp_eep_addr_trans(): "
		    "incorrect indirect eeprom address.\n");
		break;
	}

	di_addr = (indi_addr & ADDRESS_MSK) + (temp << 1);

	return (&sc->sc_eep_map[di_addr]);
}
