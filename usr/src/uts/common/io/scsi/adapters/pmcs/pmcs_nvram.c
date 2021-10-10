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
 * Copyright (c) 2009, 2010, Oracle and/or its affiliates. All rights reserved.
 */

/*
 * This file contains various support routines.
 */

#include <sys/scsi/adapters/pmcs/pmcs.h>

/*
 * SAS Topology Configuration
 */
static int pmcs_flash_chunk(pmcs_hw_t *, uint8_t *);

/*
 * Check current firmware version for correctness
 * and try to flash the correct firmware if what is
 * running isn't correct.
 *
 * Must be called after setup and MPI setup and
 * interrupts are enabled.
 */

int
pmcs_firmware_update(pmcs_hw_t *pwp)
{
	ddi_modhandle_t modhp;
	char buf[64], *bufp;
	int errno;
	uint8_t *cstart, *cend;		/* Firmware image file */
	uint8_t *istart, *iend; 	/* ila */
	uint8_t *sstart, *send;		/* SPCBoot */
	uint32_t *fwvp;
	int defret = 0;
	int first_pass = 1;
	long fw_version, ila_version;
	uint8_t *fw_verp, *ila_verp;

	/*
	 * If updating is disabled, we're done.
	 */
	if (pwp->fw_disable_update) {
		pmcs_prt(pwp, PMCS_PRT_DEBUG, NULL, NULL,
		    "Firmware update disabled by conf file");
		return (0);
	}

	/*
	 * If we're already running the right firmware, we're done.
	 */
	if (pwp->fw == PMCS_FIRMWARE_VERSION) {
		if (pwp->fw_force_update == 0) {
			return (0);
		}

		pmcs_prt(pwp, PMCS_PRT_DEBUG, NULL, NULL,
		    "Firmware version matches, but still forcing update");
	}

	modhp = ddi_modopen(PMCS_FIRMWARE_FILENAME, KRTLD_MODE_FIRST, &errno);
	if (errno) {
		pmcs_prt(pwp, PMCS_PRT_DEBUG, NULL, NULL,
		    "%s: Firmware module not available; will not upgrade",
		    __func__);
		return (defret);
	}

	fwvp = ddi_modsym(modhp, PMCS_FIRMWARE_VERSION_NAME, &errno);
	if (errno) {
		pmcs_prt(pwp, PMCS_PRT_DEBUG, NULL, NULL,
		    "%s: unable to find symbol '%s'",
		    __func__, PMCS_FIRMWARE_VERSION_NAME);
		(void) ddi_modclose(modhp);
		return (defret);
	}

	/*
	 * If the firmware version from the module isn't what we expect,
	 * and force updating is disabled, return the default (for this
	 * mode of operation) value.
	 */
	if (*fwvp != PMCS_FIRMWARE_VERSION) {
		if (pwp->fw_force_update == 0) {
			pmcs_prt(pwp, PMCS_PRT_DEBUG, NULL, NULL,
			    "%s: firmware module version wrong (0x%x)",
			    __func__, *fwvp);
			(void) ddi_modclose(modhp);
			return (defret);
		}
		pmcs_prt(pwp, PMCS_PRT_DEBUG, NULL, NULL,
		    "%s: firmware module version wrong (0x%x) - update forced",
		    __func__, *fwvp);
	}

	(void) snprintf(buf, sizeof (buf),
	    PMCS_FIRMWARE_CODE_NAME PMCS_FIRMWARE_START_SUF);
	cstart = ddi_modsym(modhp, buf, &errno);
	if (errno) {
		pmcs_prt(pwp, PMCS_PRT_DEBUG, NULL, NULL,
		    "%s: unable to find symbol '%s'", __func__, buf);
		(void) ddi_modclose(modhp);
		return (defret);
	}

	(void) snprintf(buf, sizeof (buf),
	    PMCS_FIRMWARE_CODE_NAME PMCS_FIRMWARE_END_SUF);
	cend = ddi_modsym(modhp, buf, &errno);
	if (errno) {
		pmcs_prt(pwp, PMCS_PRT_DEBUG, NULL, NULL,
		    "%s: unable to find symbol '%s'", __func__, buf);
		(void) ddi_modclose(modhp);
		return (defret);
	}

	(void) snprintf(buf, sizeof (buf),
	    PMCS_FIRMWARE_ILA_NAME PMCS_FIRMWARE_START_SUF);
	istart = ddi_modsym(modhp, buf, &errno);
	if (errno) {
		pmcs_prt(pwp, PMCS_PRT_DEBUG, NULL, NULL,
		    "%s: unable to find symbol '%s'", __func__, buf);
		(void) ddi_modclose(modhp);
		return (defret);
	}

	(void) snprintf(buf, sizeof (buf),
	    PMCS_FIRMWARE_ILA_NAME PMCS_FIRMWARE_END_SUF);
	iend = ddi_modsym(modhp, buf, &errno);
	if (errno) {
		pmcs_prt(pwp, PMCS_PRT_DEBUG, NULL, NULL,
		    "%s: unable to find symbol '%s'", __func__, buf);
		(void) ddi_modclose(modhp);
		return (defret);
	}

	(void) snprintf(buf, sizeof (buf),
	    PMCS_FIRMWARE_SPCBOOT_NAME PMCS_FIRMWARE_START_SUF);
	sstart = ddi_modsym(modhp, buf, &errno);
	if (errno) {
		pmcs_prt(pwp, PMCS_PRT_DEBUG, NULL, NULL,
		    "%s: unable to find symbol '%s'", __func__, buf);
		(void) ddi_modclose(modhp);
		return (defret);
	}

	(void) snprintf(buf, sizeof (buf),
	    PMCS_FIRMWARE_SPCBOOT_NAME PMCS_FIRMWARE_END_SUF);
	send = ddi_modsym(modhp, buf, &errno);
	if (errno) {
		pmcs_prt(pwp, PMCS_PRT_DEBUG, NULL, NULL,
		    "%s: unable to find symbol '%s'", __func__, buf);
		(void) ddi_modclose(modhp);
		return (defret);
	}

	/*
	 * Get the ILA and firmware versions from the modules themselves
	 */
	ila_verp = iend - PMCS_ILA_VER_OFFSET;
	(void) ddi_strtol((const char *)ila_verp, &bufp, 16, &ila_version);
	fw_verp = cend - PMCS_FW_VER_OFFSET;
	(void) ddi_strtol((const char *)fw_verp, &bufp, 16, &fw_version);

	/*
	 * If force update is not set, verify that what we're loading is
	 * what we expect.
	 */
	if (pwp->fw_force_update == 0) {
		if (fw_version != PMCS_FIRMWARE_VERSION) {
			pmcs_prt(pwp, PMCS_PRT_ERR, NULL, NULL,
			    "Expected fw version 0x%x, not 0x%lx: not "
			    "updating", PMCS_FIRMWARE_VERSION, fw_version);
			(void) ddi_modclose(modhp);
			return (defret);
		}
	}

	pmcs_prt(pwp, PMCS_PRT_WARN, NULL, NULL,
	    "Upgrading firmware on card from 0x%x to 0x%lx (ILA version 0x%lx)",
	    pwp->fw, fw_version, ila_version);

	/*
	 * The SPCBoot image must be updated first, and this is written to
	 * SEEPROM, not flash.
	 */
	if (pmcs_set_nvmd(pwp, PMCS_NVMD_SPCBOOT, sstart,
	    (size_t)((size_t)send - (size_t)sstart)) == B_FALSE) {
		pmcs_prt(pwp, PMCS_PRT_DEBUG, NULL, NULL,
		    "%s: unable to flash '%s' segment",
		    __func__, PMCS_FIRMWARE_SPCBOOT_NAME);
		(void) ddi_modclose(modhp);
		return (-1);
	}

repeat:
	pmcs_prt(pwp, PMCS_PRT_DEBUG, NULL, NULL,
	    "%s: Beginning firmware update of %s image.",
	    __func__, (first_pass ? "first" : "second"));

	if (pmcs_fw_flash(pwp, (void *)istart,
	    (uint32_t)((size_t)iend - (size_t)istart))) {
		pmcs_prt(pwp, PMCS_PRT_DEBUG, NULL, NULL,
		    "%s: unable to flash '%s' segment",
		    __func__, PMCS_FIRMWARE_ILA_NAME);
		(void) ddi_modclose(modhp);
		return (-1);
	}

	if (pmcs_fw_flash(pwp, (void *)cstart,
	    (uint32_t)((size_t)cend - (size_t)cstart))) {
		pmcs_prt(pwp, PMCS_PRT_DEBUG, NULL, NULL,
		    "%s: unable to flash '%s' segment",
		    __func__, PMCS_FIRMWARE_CODE_NAME);
		(void) ddi_modclose(modhp);
		return (-1);
	}

	if (pmcs_soft_reset(pwp, B_FALSE)) {
		pmcs_prt(pwp, PMCS_PRT_DEBUG, NULL, NULL,
		    "%s: soft reset after flash update failed", __func__);
		(void) ddi_modclose(modhp);
		return (-1);
	} else {
		pmcs_prt(pwp, PMCS_PRT_DEBUG, NULL, NULL,
		    "%s: %s image successfully upgraded.",
		    __func__, (first_pass ? "First" : "Second"));
		pwp->last_reset_reason = PMCS_LAST_RST_FW_UPGRADE;
	}

	if (first_pass) {
		first_pass = 0;
		goto repeat;
	}

	pmcs_prt(pwp, PMCS_PRT_WARN, NULL, NULL,
	    "%s: Firmware successfully upgraded", __func__);

	(void) ddi_modclose(modhp);
	return (0);
}

/*
 * Flash firmware support
 * Called unlocked.
 */
int
pmcs_fw_flash(pmcs_hw_t *pwp, pmcs_fw_hdr_t *hdr, uint32_t length)
{
	pmcs_fw_hdr_t *hp;
	uint8_t *wrk, *base;
	int scratch_hdl;

	/*
	 * Step 1- Validate firmware chunks within passed pointer.
	 */
	hp = hdr;
	wrk = (uint8_t *)hdr;
	base = wrk;
	for (;;) {
		pmcs_prt(pwp, PMCS_PRT_DEBUG1, NULL, NULL,
		    "%s: partition 0x%x, Length 0x%x", __func__,
		    hp->destination_partition, ntohl(hp->firmware_length));
		if (ntohl(hp->firmware_length) == 0) {
			pmcs_prt(pwp, PMCS_PRT_DEBUG, NULL, NULL,
			    "%s: bad firmware length 0x%x",
			    __func__, ntohl(hp->firmware_length));
			return (EINVAL);
		}
		wrk += (sizeof (pmcs_fw_hdr_t) + ntohl(hp->firmware_length));
		if (wrk == base + length) {
			break;
		}
		if (wrk > base + length) {
			pmcs_prt(pwp, PMCS_PRT_DEBUG, NULL, NULL,
			    "%s: out of bounds firmware length", __func__);
			return (EINVAL);
		}
		hp = (void *)wrk;
	}

	/*
	 * Step 2- acquire scratch
	 */
	scratch_hdl = pmcs_acquire_scratch(pwp);
	ASSERT(scratch_hdl != 1);

	/*
	 * Step 3- loop through firmware chunks and send each one
	 * down to be flashed.
	 */
	hp = hdr;
	wrk = (uint8_t *)hdr;
	base = wrk;
	for (;;) {
		if (pmcs_flash_chunk(pwp, wrk)) {
			pmcs_release_scratch(pwp, scratch_hdl);
			return (EIO);
		}
		wrk += (sizeof (pmcs_fw_hdr_t) + ntohl(hp->firmware_length));
		if (wrk == base + length) {
			break;
		}
		hp = (void *) wrk;
	}
	pmcs_release_scratch(pwp, scratch_hdl);
	return (0);
}

/*
 * Since this is only called during HBA attach, we use the scratch region
 * directly rather than calling pmcs_acquire_scratch/pmcs_release_scratch.
 */
static int
pmcs_flash_chunk(pmcs_hw_t *pwp, uint8_t *chunk)
{
	pmcs_fw_hdr_t *hp;
	pmcwork_t *pwrk;
	uint32_t len, seg, off, result, amt, msg[PMCS_MSG_SIZE], *ptr;

	hp = (void *)chunk;
	len = sizeof (pmcs_fw_hdr_t) + ntohl(hp->firmware_length);

	seg = off = 0;
	while (off < len) {
		amt = PMCS_SCRATCH_SIZE;
		if (off + amt > len) {
			amt = len - off;
		}
		pmcs_prt(pwp, PMCS_PRT_DEBUG1, NULL, NULL,
		    "%s: segment %d offset %u length %u",
		    __func__, seg, off, amt);
		(void) memcpy(pwp->scratch_addr, &chunk[off], amt);
		pwrk = pmcs_gwork(pwp, PMCS_TAG_TYPE_WAIT, NULL);
		if (pwrk == NULL) {
			return (ENOMEM);
		}
		pwrk->arg = msg;
		msg[0] = LE_32(PMCS_HIPRI(pwp,
		    PMCS_OQ_EVENTS, PMCIN_FW_FLASH_UPDATE));
		msg[1] = LE_32(pwrk->htag);
		msg[2] = LE_32(off);
		msg[3] = LE_32(amt);
		if (off == 0) {
			msg[4] = LE_32(len);
		} else {
			msg[4] = 0;
		}
		msg[5] = 0;
		msg[6] = 0;
		msg[7] = 0;
		msg[8] = 0;
		msg[9] = 0;
		msg[10] = 0;
		msg[11] = 0;
		msg[12] = LE_32(DWORD0(pwp->scratch_dma));
		msg[13] = LE_32(DWORD1(pwp->scratch_dma));
		msg[14] = LE_32(amt);
		msg[15] = 0;
		mutex_enter(&pwp->iqp_lock[PMCS_IQ_OTHER]);
		ptr = GET_IQ_ENTRY(pwp, PMCS_IQ_OTHER);
		if (ptr == NULL) {
			mutex_exit(&pwp->iqp_lock[PMCS_IQ_OTHER]);
			pmcs_pwork(pwp, pwrk);
			pmcs_prt(pwp, PMCS_PRT_ERR, NULL, NULL,
			    pmcs_nomsg, __func__);
			return (ENOMEM);
		}
		COPY_MESSAGE(ptr, msg, PMCS_MSG_SIZE);
		(void) memset(msg, 0xaf, sizeof (msg));
		pwrk->state = PMCS_WORK_STATE_ONCHIP;
		INC_IQ_ENTRY(pwp, PMCS_IQ_OTHER);
		WAIT_FOR(pwrk, PMCS_FLASH_WAIT_TIME, result);
		pmcs_pwork(pwp, pwrk);
		if (result) {
			pmcs_prt(pwp, PMCS_PRT_DEBUG, NULL, NULL,
			    pmcs_timeo, __func__);
			return (EIO);
		}
		switch (LE_32(msg[2])) {
		case FLASH_UPDATE_COMPLETE_PENDING_REBOOT:
			pmcs_prt(pwp, PMCS_PRT_DEBUG1, NULL, NULL,
			    "%s: segment %d complete pending reboot",
			    __func__, seg);
			break;
		case FLASH_UPDATE_IN_PROGRESS:
			pmcs_prt(pwp, PMCS_PRT_DEBUG1, NULL, NULL,
			    "%s: segment %d downloaded", __func__, seg);
			break;
		case FLASH_UPDATE_HDR_ERR:
			pmcs_prt(pwp, PMCS_PRT_DEBUG, NULL, NULL,
			    "%s: segment %d header error", __func__, seg);
			return (EIO);
		case FLASH_UPDATE_OFFSET_ERR:
			pmcs_prt(pwp, PMCS_PRT_DEBUG, NULL, NULL,
			    "%s: segment %d offset error", __func__, seg);
			return (EIO);
		case FLASH_UPDATE_UPDATE_CRC_ERR:
			pmcs_prt(pwp, PMCS_PRT_DEBUG, NULL, NULL,
			    "%s: segment %d update crc error", __func__, seg);
			return (EIO);
		case FLASH_UPDATE_LENGTH_ERR:
			pmcs_prt(pwp, PMCS_PRT_DEBUG, NULL, NULL,
			    "%s: segment %d length error", __func__, seg);
			return (EIO);
		case FLASH_UPDATE_HW_ERR:
			pmcs_prt(pwp, PMCS_PRT_DEBUG, NULL, NULL,
			    "%s: segment %d hw error", __func__, seg);
			return (EIO);
		case FLASH_UPDATE_DNLD_NOT_SUPPORTED:
			pmcs_prt(pwp, PMCS_PRT_DEBUG, NULL, NULL,
			    "%s: segment %d download not supported error",
			    __func__, seg);
			return (EIO);
		case FLASH_UPDATE_DISABLED:
			pmcs_prt(pwp, PMCS_PRT_DEBUG, NULL, NULL,
			    "%s: segment %d update disabled error",
			    __func__, seg);
			return (EIO);
		default:
			pmcs_prt(pwp, PMCS_PRT_DEBUG, NULL, NULL,
			    "%s: segment %d unknown error %x",
			    __func__, seg, msg[2]);
			return (EIO);
		}
		off += amt;
		seg++;
	}
	return (0);
}

/*
 * pmcs_validate_vpd
 *
 * Input: softstate pointer and pointer to vpd data buffer
 * Returns: B_TRUE if VPD data looks OK, B_FALSE otherwise
 */
static boolean_t
pmcs_validate_vpd(pmcs_hw_t *pwp, uint8_t *data)
{
	pmcs_vpd_header_t *vpd_header;
	uint8_t *bufp, kv_len, *chksump, chksum = 0;
	char tbuf[80];
	char prop[24];
	int idx, str_len;
	uint16_t strid_length, chksum_len;
	uint64_t wwid;
	pmcs_vpd_kv_t *vkvp;

	vpd_header = (pmcs_vpd_header_t *)data;

	/*
	 * Make sure we understand the format of this data
	 */

	/*
	 * Only VPD version 1 is VALID for Thebe-INT cards and
	 * Only VPD version 2 is valid for Thebe-EXT cards
	 */
	if ((vpd_header->eeprom_version == PMCS_EEPROM_INT_VERSION &&
	    vpd_header->subsys_pid[0] == PMCS_EEPROM_INT_SSID_BYTE1 &&
	    vpd_header->subsys_pid[1] == PMCS_EEPROM_INT_SSID_BYTE2) ||
	    (vpd_header->eeprom_version == PMCS_EEPROM_EXT_VERSION &&
	    vpd_header->subsys_pid[0] == PMCS_EEPROM_EXT_SSID_BYTE1 &&
	    vpd_header->subsys_pid[1] == PMCS_EEPROM_EXT_SSID_BYTE2)) {
			goto valid_version;
	} else {
		pmcs_prt(pwp, PMCS_PRT_DEBUG, NULL, NULL,
		    "%s: Detected Thebe card with SSID(%02x%02x)", __func__,
		    vpd_header->subsys_pid[0], vpd_header->subsys_pid[1]);
		pmcs_prt(pwp, PMCS_PRT_DEBUG, NULL, NULL,
		    "%s: EEPROM(%d) unsupported; requires %d for INT(%02x%02x) "
		    " and %d for EXT(%02x%02x) cards.", __func__,
		    vpd_header->eeprom_version,
		    PMCS_EEPROM_INT_VERSION, PMCS_EEPROM_INT_SSID_BYTE1,
		    PMCS_EEPROM_INT_SSID_BYTE2, PMCS_EEPROM_EXT_VERSION,
		    PMCS_EEPROM_EXT_SSID_BYTE1, PMCS_EEPROM_EXT_SSID_BYTE2);
		return (B_FALSE);
	}

valid_version:
	/*
	 * Do we have a valid SAS WWID?
	 */
	if (((vpd_header->hba_sas_wwid[0] & 0xf0) >> 4) != NAA_IEEE_REG) {
		pmcs_prt(pwp, PMCS_PRT_DEBUG, NULL, NULL,
		    "%s: SAS WWN has invalid NAA (%d)", __func__,
		    ((vpd_header->hba_sas_wwid[0] & 0xf0) >> 4));
		return (B_FALSE);
	}
	wwid = pmcs_barray2wwn(vpd_header->hba_sas_wwid);
	for (idx = 0; idx < PMCS_MAX_PORTS; idx++) {
		pwp->sas_wwns[idx] = wwid + idx;
	}

	if (vpd_header->vpd_start_byte != PMCS_VPD_START) {
		pmcs_prt(pwp, PMCS_PRT_DEBUG, NULL, NULL,
		    "%s: Didn't see VPD start byte", __func__);
		return (B_FALSE);
	}

	/*
	 * We only checksum the VPD data between (and including) VPD Start byte
	 * and the checksum value byte. The length of this data for CRC is
	 * 15 less than the length indicated in vpd_length field of the header.
	 * 8 (SAS WWN) + 2 (subsystem ID) + 2 (subsystem vendor ID) +
	 * 1 (end tag) + 2 (hex byte CRC, different from this one) = 15 bytes
	 */
	/*
	 * VPD length (little endian format) is represented as byte-array field
	 * & read the following way to avoid alignment issues (in SPARC)
	 */
	chksum_len = ((vpd_header->vpd_length[1] << 8) |
	    (vpd_header->vpd_length[0])) - 15;
	/* Validate VPD data checksum */
	chksump = (uint8_t *)&vpd_header->vpd_start_byte;
	ASSERT (*chksump == PMCS_VPD_START);
	for (idx = 0; idx < chksum_len; idx++, chksump++) {
		chksum += *chksump;
	}
	ASSERT (*chksump == PMCS_VPD_END);
	if (chksum) {
		pmcs_prt(pwp, PMCS_PRT_DEBUG, NULL, NULL,
		    "%s: VPD checksum failure", __func__);
		return (B_FALSE);
	}

	/*
	 * Get length of string ID tag and read it.
	 */
	bufp = (uint8_t *)&vpd_header->vpd_start_byte;
	bufp += 3;		/* Skip the start byte and length */
	/*
	 * String ID tag length (little endian format) is represented as
	 * byte-array & read the following way to avoid alignment issues
	 * (in SPARC)
	 */
	strid_length = (vpd_header->strid_length[1] << 8) |
	    (vpd_header->strid_length[0]);
	if (strid_length > 79) {
		strid_length = 79;
	}
	bcopy(bufp, tbuf, strid_length);
	tbuf[strid_length] = 0;

	pmcs_prt(pwp, PMCS_PRT_DEBUG2, NULL, NULL,
	    "%s: Product Name: '%s'", __func__, tbuf);
	pmcs_smhba_add_hba_prop(pwp, DATA_TYPE_STRING, PMCS_MODEL_NAME, tbuf);

	/*
	 * Skip VPD-R tag and length of read-only tag, then start reading
	 * keyword/value pairs
	 */
	bufp += strid_length;	/* Skip to VPD-R tag */
	bufp += 3;		/* Skip VPD-R tag and length of VPD-R data */

	vkvp = (pmcs_vpd_kv_t *)bufp;

	while (vkvp->keyword[0] != PMCS_VPD_END) {
		tbuf[0] = 0;
		str_len = snprintf(tbuf, 80, "VPD: %c%c = <",
		    vkvp->keyword[0], vkvp->keyword[1]);

		kv_len = vkvp->value_length;
		for (idx = 0; idx < kv_len; idx++) {
			tbuf[str_len + idx] = vkvp->value[idx];
			prop[idx] = vkvp->value[idx];
		}
		prop[idx] = '\0';
		str_len += kv_len;
		tbuf[str_len] = '>';
		tbuf[str_len + 1] = 0;
		pmcs_prt(pwp, PMCS_PRT_DEBUG2, NULL, NULL, "%s (Len: 0x%x)",
		    tbuf, kv_len);

		/*
		 * Check to see if we're looking at a valid keyword.  If not,
		 * there must have been something wrong with the VPD format.
		 * Valid keywords are: PN, EC, SN, MN and RV.
		 */
		switch (vkvp->keyword[0]) {
		case 'P':
			if (vkvp->keyword[1] != 'N') {
				goto vpd_invalid;
			}
			break;
		case 'E':
			if (vkvp->keyword[1] != 'C') {
				goto vpd_invalid;
			}
			break;
		case 'S':
			if (vkvp->keyword[1] != 'N') {
				goto vpd_invalid;
			}
			break;
		case 'M':
			if (vkvp->keyword[1] != 'N') {
				goto vpd_invalid;
			}
			break;
		case 'R':
			if (vkvp->keyword[1] != 'V') {
				goto vpd_invalid;
			}
			break;
		default:
			goto vpd_invalid;
		}

		/* Keyword is Manufacturer */
		if (vkvp->keyword[0] == 'M') {
			pmcs_smhba_add_hba_prop(pwp, DATA_TYPE_STRING,
			    PMCS_MANUFACTURER, prop);
		}
		/* Keyword is Serial Number */
		if (vkvp->keyword[0] == 'S') {
			pmcs_smhba_add_hba_prop(pwp, DATA_TYPE_STRING,
			    PMCS_SERIAL_NUMBER, prop);
		}

		vkvp = (pmcs_vpd_kv_t *)(bufp + 3 + kv_len);
		bufp += kv_len + 3;
	}

	return (B_TRUE);

vpd_invalid:
	pmcs_prt(pwp, PMCS_PRT_DEBUG, NULL, NULL, "VPD format/data invalid");
	return (B_FALSE);
}

/*
 * pmcs_get_nvmd
 *
 * This function will read the requested data from the non-volatile
 * storage on the card.  This could mean SEEPROM, VPD, or other areas
 * as defined by the PM8001 programmer's manual.
 *
 * nvmd_type: The data type being requested
 * nvmd: NVM device to access (IOP/AAP1)
 * offset: Must be 4K alignment
 * buf: Pointer to memory region for retrieved data
 * size_left: Total available bytes left in buf
 * raw_dump: If true, just copy byte for byte
 *
 * Returns: non-negative on success, -1 on failure
 */

/*ARGSUSED*/
int
pmcs_get_nvmd(pmcs_hw_t *pwp, pmcs_nvmd_type_t nvmd_type, uint8_t nvmd,
    uint32_t offset, char *buf, uint32_t size_left, boolean_t raw_dump)
{
	pmcs_get_nvmd_cmd_t iomb;
	pmcwork_t *workp;
	uint8_t *chunkp;
	uint32_t *ptr, ibq, *iombp;
	uint32_t dlen;
	uint16_t status;
	uint8_t tdas_nvmd, ip, tda, tbn_tdps;
	uint8_t	doa[3];
	int32_t result = -1, i = 0;

	switch (nvmd_type) {
	case PMCS_NVMD_VPD:
		tdas_nvmd = PMCIN_NVMD_TDPS_1 | PMCIN_NVMD_TWI;
		tda = PMCIN_TDA_PAGE(2);
		tbn_tdps = PMCIN_NVMD_TBN(0) | PMCIN_NVMD_TDPS_8;
		ip = PMCIN_NVMD_INDIRECT_PLD;
		dlen = LE_32(PMCS_SEEPROM_PAGE_SIZE);
		doa[0] = 0;
		doa[1] = 0;
		doa[2] = 0;
		break;
	case PMCS_NVMD_REG_DUMP:
		tdas_nvmd = nvmd;
		tda = 0;
		tbn_tdps = 0;
		ip = PMCIN_NVMD_INDIRECT_PLD;
		dlen = LE_32(PMCS_REGISTER_DUMP_BLOCK_SIZE);
		doa[0] = offset & 0xff;
		doa[1] = (offset >> 8) & 0xff;
		doa[2] = (offset >> 16) & 0xff;
		break;
	case PMCS_NVMD_EVENT_LOG:
		tdas_nvmd = nvmd;
		tda = 0;
		tbn_tdps = 0;
		ip = PMCIN_NVMD_INDIRECT_PLD;
		dlen = LE_32(PMCS_REGISTER_DUMP_BLOCK_SIZE);
		offset = offset + PMCS_NVMD_EVENT_LOG_OFFSET;
		doa[0] = offset & 0xff;
		doa[1] = (offset >> 8) & 0xff;
		doa[2] = (offset >> 16) & 0xff;
		break;
	default:
		pmcs_prt(pwp, PMCS_PRT_DEBUG, NULL, NULL,
		    "%s: Invalid nvmd type: %d", __func__, nvmd_type);
		return (-1);
	}

	workp = pmcs_gwork(pwp, PMCS_TAG_TYPE_WAIT, NULL);
	if (workp == NULL) {
		pmcs_prt(pwp, PMCS_PRT_WARN, NULL, NULL,
		    "%s: Unable to get work struct", __func__);
		return (-1);
	}

	ptr = &iomb.header;
	bzero(ptr, sizeof (pmcs_get_nvmd_cmd_t));
	*ptr = LE_32(PMCS_IOMB_IN_SAS(PMCS_OQ_GENERAL, PMCIN_GET_NVMD_DATA));
	workp->arg = (void *)&iomb;
	iomb.htag = LE_32(workp->htag);
	iomb.ip = ip;
	iomb.tbn_tdps = tbn_tdps;
	iomb.tda = tda;
	iomb.tdas_nvmd = tdas_nvmd;
	iomb.ipbal = LE_32(DWORD0(pwp->flash_chunk_addr));
	iomb.ipbah = LE_32(DWORD1(pwp->flash_chunk_addr));
	iomb.ipdl = dlen;
	iomb.doa[0] = doa[0];
	iomb.doa[1] = doa[1];
	iomb.doa[2] = doa[2];

	/*
	 * ptr will now point to the inbound queue message
	 */
	GET_IO_IQ_ENTRY(pwp, ptr, 0, ibq);
	if (ptr == NULL) {
		pmcs_prt(pwp, PMCS_PRT_ERR, NULL, NULL,
		    "!%s: Unable to get IQ entry", __func__);
		pmcs_pwork(pwp, workp);
		return (-1);
	}

	bzero(ptr, PMCS_MSG_SIZE << 2);	/* PMCS_MSG_SIZE is in dwords */
	iombp = (uint32_t *)&iomb;
	COPY_MESSAGE(ptr, iombp, sizeof (pmcs_get_nvmd_cmd_t) >> 2);
	workp->state = PMCS_WORK_STATE_ONCHIP;
	INC_IQ_ENTRY(pwp, ibq);

	WAIT_FOR(workp, 1000, result);
	ptr = workp->arg;
	if (result) {
		pmcs_timed_out(pwp, workp->htag, __func__);
		pmcs_pwork(pwp, workp);
		return (-1);
	}
	status = LE_32(*(ptr + 3)) & 0xffff;
	if (status != PMCS_NVMD_STAT_SUCCESS) {
		pmcs_prt(pwp, PMCS_PRT_DEBUG, NULL, NULL,
		    "%s: Error, status = 0x%04x", __func__, status);
		pmcs_pwork(pwp, workp);
		return (-1);
	}

	pmcs_pwork(pwp, workp);

	if (ddi_dma_sync(pwp->cip_handles, 0, 0,
	    DDI_DMA_SYNC_FORKERNEL) != DDI_SUCCESS) {
		pmcs_prt(pwp, PMCS_PRT_DEBUG, NULL, NULL,
		    "Condition check failed at %s():%d", __func__, __LINE__);
	}
	chunkp = (uint8_t *)pwp->flash_chunkp;

	switch (nvmd) {
	case PMCIN_NVMD_VPD:
		if (pmcs_validate_vpd(pwp, chunkp)) {
			result = 0;
		} else {
			result = -1;
		}
		break;
	case PMCIN_NVMD_AAP1:
	case PMCIN_NVMD_IOP:
		ASSERT(buf);
		i = 0;
		if (raw_dump) {
			bcopy(chunkp, buf, PMCS_FLASH_CHUNK_SIZE);
			i = PMCS_FLASH_CHUNK_SIZE;
		} else if (nvmd_type == PMCS_NVMD_REG_DUMP) {
			while ((i < PMCS_FLASH_CHUNK_SIZE) &&
			    (chunkp[i] != 0xff) && (chunkp[i] != '\0')) {
				i += snprintf(&buf[i], (size_left - i),
				    "%c", chunkp[i]);
			}
		} else if (nvmd_type == PMCS_NVMD_EVENT_LOG) {
			i = pmcs_dump_binary(pwp, pwp->flash_chunkp, 0,
			    (PMCS_FLASH_CHUNK_SIZE >> 2), buf, size_left);
		}
		result = i;
		break;
	default:
		pmcs_prt(pwp, PMCS_PRT_DEBUG, NULL, NULL,
		    "UNKNOWN NVMD DEVICE");
		return (-1);
	}

	return (result);
}

/*
 * pmcs_set_nvmd
 *
 * This function will write the requested data to non-volatile storage
 * on the HBA.  This could mean SEEPROM, VPD, or other areas as defined by
 * the PM8001 programmer's manual.
 *
 * nvmd_type: The data type to be written
 * buf: Pointer to memory region for data to write
 * len: Length of the data buffer
 *
 * Returns: B_TRUE on success, B_FALSE on failure
 */

boolean_t
pmcs_set_nvmd(pmcs_hw_t *pwp, pmcs_nvmd_type_t nvmd_type, uint8_t *buf,
    size_t len)
{
	pmcs_set_nvmd_cmd_t iomb;
	pmcwork_t *workp;
	uint32_t *ptr, ibq, *iombp;
	uint32_t dlen;
	uint16_t status;
	uint8_t tdas_nvmd, ip;
	int result;

	switch (nvmd_type) {
	case PMCS_NVMD_SPCBOOT:
		tdas_nvmd = PMCIN_NVMD_SEEPROM;
		ip = PMCIN_NVMD_INDIRECT_PLD;
		ASSERT((len >= PMCS_SPCBOOT_MIN_SIZE) &&
		    (len <= PMCS_SPCBOOT_MAX_SIZE));
		dlen = LE_32(len);
		break;
	default:
		pmcs_prt(pwp, PMCS_PRT_DEBUG, NULL, NULL,
		    "%s: Invalid nvmd type: %d", __func__, nvmd_type);
		return (B_FALSE);
	}

	pmcs_prt(pwp, PMCS_PRT_DEBUG_DEVEL, NULL, NULL,
	    "%s: Request for nvmd type: %d", __func__, nvmd_type);

	workp = pmcs_gwork(pwp, PMCS_TAG_TYPE_WAIT, NULL);
	if (workp == NULL) {
		pmcs_prt(pwp, PMCS_PRT_WARN, NULL, NULL,
		    "%s: Unable to get work struct", __func__);
		return (B_FALSE);
	}

	ptr = &iomb.header;
	bzero(ptr, sizeof (pmcs_set_nvmd_cmd_t));
	*ptr = LE_32(PMCS_IOMB_IN_SAS(PMCS_OQ_GENERAL, PMCIN_SET_NVMD_DATA));
	workp->arg = (void *)&iomb;
	iomb.htag = LE_32(workp->htag);
	iomb.ip = ip;
	iomb.tdas_nvmd = tdas_nvmd;
	iomb.signature = LE_32(PMCS_SEEPROM_SIGNATURE);
	iomb.ipbal = LE_32(DWORD0(pwp->flash_chunk_addr));
	iomb.ipbah = LE_32(DWORD1(pwp->flash_chunk_addr));
	iomb.ipdl = dlen;

	pmcs_print_entry(pwp, PMCS_PRT_DEBUG_DEVEL,
	    "PMCIN_SET_NVMD_DATA iomb", (void *)&iomb);

	bcopy(buf, pwp->flash_chunkp, len);
	if (ddi_dma_sync(pwp->cip_handles, 0, 0,
	    DDI_DMA_SYNC_FORDEV) != DDI_SUCCESS) {
		pmcs_prt(pwp, PMCS_PRT_DEBUG, NULL, NULL,
		    "Condition check failed at %s():%d", __func__, __LINE__);
	}

	/*
	 * ptr will now point to the inbound queue message
	 */
	GET_IO_IQ_ENTRY(pwp, ptr, 0, ibq);
	if (ptr == NULL) {
		pmcs_prt(pwp, PMCS_PRT_ERR, NULL, NULL,
		    "!%s: Unable to get IQ entry", __func__);
		pmcs_pwork(pwp, workp);
		return (B_FALSE);
	}

	bzero(ptr, PMCS_MSG_SIZE << 2);	/* PMCS_MSG_SIZE is in dwords */
	iombp = (uint32_t *)&iomb;
	COPY_MESSAGE(ptr, iombp, sizeof (pmcs_set_nvmd_cmd_t) >> 2);
	workp->state = PMCS_WORK_STATE_ONCHIP;
	INC_IQ_ENTRY(pwp, ibq);

	WAIT_FOR(workp, 2000, result);

	if (result) {
		pmcs_timed_out(pwp, workp->htag, __func__);
		pmcs_pwork(pwp, workp);
		return (B_FALSE);
	}

	pmcs_pwork(pwp, workp);

	status = LE_32(*(ptr + 3)) & 0xffff;
	if (status != PMCS_NVMD_STAT_SUCCESS) {
		pmcs_prt(pwp, PMCS_PRT_DEBUG, NULL, NULL,
		    "%s: Error, status = 0x%04x", __func__, status);
		return (B_FALSE);
	}

	return (B_TRUE);
}
