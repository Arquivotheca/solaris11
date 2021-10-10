/*
 * Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

/*
 * This file may contain confidential information of LSI Corporation
 * and should not be distributed in source form without approval
 * from Sun Legal
 */

/*
 * SUN-branded LSI Logic-specific firmware image verification plugin
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/sysmacros.h>
#include <fcntl.h>
#include <sys/condvar.h>
#include <string.h>
#include <strings.h>

#include <sys/byteorder.h>

#include <libintl.h> /* for gettext(3c) */
#include <fwflash/fwflash.h>
#include "../hdrs/LSILOGIC.h"


char vendor[] = "SUN     \0";

extern int errno;
extern struct vrfyplugin *verifier;

/* required functions for this plugin */
int vendorvrfy(struct devicelist *devicenode);


/* helper functions */
static int check_std_fw_img(struct devicelist *devicenode);
static int std_img_checksumcalc();
extern void tidyup(struct devicelist *thisdev);

static int check_boot_fw_img(struct devicelist *devicenode);
static int8_t boot_checksumcalc(int8_t *data, int length);

/* length of SCSI ProductID field + '\0' */
#define	MAXINQLEN	17

/*
 * This _static_ table maps INQUIRY info to Product IDs.
 *
 * Until we can query the expander directly for this
 * ProductID info, we have to keep this table updated
 * each newly supported product.
 */
typedef struct inq_pid_map {
	char *inqstr;
	int ProductId;
} inq_pid_map_t;

static inq_pid_map_t ipmtab[] = {
	/* pre-RR names */
	{ "SASX28", VELANEMPLUS_PRODUCT_ID },
	{ "SASX28", DORADO_PRODUCT_ID },
	{ "SASX36", LOKI_PRODUCT_ID },
	{ "SASX36", MONGO_PRODUCT_ID },
	{ "ST Jxxx", MONGO_PRODUCT_ID },
	/* RR names */
	{ "Blade Storage", VELANEMPLUS_PRODUCT_ID },
	{ "NEM Plus",  VELANEMPLUS_PRODUCT_ID },
	{ "16Disk Backplane",  DORADO_PRODUCT_ID },
	{ "NEM P 10Gbe  x10", VELANEMPLUS_PRODUCT_ID },
	{ "NEM P 10GbE  x10", VELANEMPLUS_PRODUCT_ID },
	{ "NEM P 10GbE x10", VELANEMPLUS_PRODUCT_ID },
	{ "NEM P 10Gbe x12", GOAC48_PRODUCT_ID },
	{ "NEM P 10GbE x12", GOAC48_PRODUCT_ID },
	{ "SS J4650", MONGO_PRODUCT_ID },
	{ "ST J4650", MONGO_PRODUCT_ID },
	{ "Storage J4650", MONGO_PRODUCT_ID },
	{ "Storage F5100", MONGO_PRODUCT_ID},
	{ "SS J4500", LOKI_PRODUCT_ID },
	{ "SS J4400", RIVERWALK_PRODUCT_ID },
	{ "SS J4300", BERET_PRODUCT_ID },
	{ "SS J4200", ALAMO_PRODUCT_ID },
	{ "ST J4500", LOKI_PRODUCT_ID },
	{ "ST J4400", RIVERWALK_PRODUCT_ID },
	{ "ST J4300", BERET_PRODUCT_ID },
	{ "ST J4200", ALAMO_PRODUCT_ID },
	{ "Storage J4500", LOKI_PRODUCT_ID },
	{ "Storage J4400", RIVERWALK_PRODUCT_ID },
	{ "Storage J4300", BERET_PRODUCT_ID },
	{ "Storage J4200", ALAMO_PRODUCT_ID },

	/* last entry */
	{ NULL, NULL }
};


/*
 * Important information about how this verification plugin works
 *
 * from http://nsgtwiki.sfbay.sun.com/twiki/bin/view/NSG/LSISASExpander
 *
 * **Explanation from LSI on how to reading the firmware version out
 * **of SCSI inquiry data
 *
 * There are two fields in the SCSI inquiry data that we use to embed
 * the firmware version.
 * The "Product Version" field is defined by the SCSI spec as 4 ascii
 * characters. We use this to map a portion of the firmware version
 * number. It is only a portion of the number because it would take 8
 * ascii characters to represent it all.
 *
 * The firmware version number internally is 4 hex digits:
 *	major . minor . unit . developer.
 * "Major" is the field of most importance, declining to "developer" as
 * the least importance.
 * The Major number is mapped into the first ascii character, as a hex
 * digit. The minor number is mapped into the second and third ascii
 * characters, as two hex digits. The unit number is mapped into the
 * 4th ascii character, as a single hex digit - so it maps the low nibble
 * of the unit number.
 *
 * We use the vendor specific area in the inquiry data to place an ascii
 * string with the full firmware version number. The string starts with
 * "x36-" or "x28-", depending on the number of expander phys. The
 * remainder of the string is the major . minor . unit . dev number
 * in decimal.
 */

int
vendorvrfy(struct devicelist *devicenode) {

	int rv = FWFLASH_FAILURE;

	/*
	 * This verifier handles three sorts of firmware images:
	 *	boot records
	 *	manufacturing customer images
	 *	"proper" firmware.
	 *
	 *
	 * Today (Feb 2008), the filename format for boot records is
	 *	[productname]_[rev].rxp
	 *	(eg, nemplus_rev6h.rxp)
	 *
	 *
	 * For manufacturing customer images the filename format is
	 *	mfgImageCust[rev][Product Initial].bin
	 *	(eg, mfgImageCust03N.bin)
	 *
	 *
	 * For "proper" firmware images, the filename format is
	 *	sasxfw[Product Abbreviation].fw
	 *	(eg, sasxfwnv.fw)
	 *
	 * For details on the actual file structure of boot/seeprom
	 * and "proper" firmware images, please see ../hdrs/LSILOGIC.h
	 * There is apparently _no_ documentation available from LSI
	 * about the structure of the manufacturing image.
	 */

	if (strstr(verifier->imgfile, "mfgImage") != NULL) {
		verifier->flashbuf = LSI_MFGIMG_BUFID; /* secret sauce */
		return (FWFLASH_SUCCESS);
	}

	/*
	 * If devicenode's vpr date is empty, then the device has
	 * disappeared, probably because the user wants to flash
	 * multiple images and the device is in the middle of updating
	 * itself. This is can be a temporary failure, but we should
	 * still return FWFLASH_FAILURE and let the user re-invoke
	 * fwflash once the device has re-appeared.
	 */

	if (devicenode->ident->vid == NULL) {
		logmsg(MSG_ERROR,
		    gettext("\n%s firmware image verifier: target device "
		    "%s has disappeared. Please reboot the host and "
		    "try flashing image %s again\n\n"),
		    verifier->vendor, devicenode->access_devname,
		    verifier->imgfile);
		return (FWFLASH_FAILURE);
	}

	/*
	 * First we check whether the header info matches the
	 * standard delivered-to-customers firmware image.
	 *
	 * If we don't match the signature fields for MPI_FW_VERSION
	 * then we try matching against the boot/seeprom record
	 *
	 * If that fails, we return FWFLASH_FAILURE.
	 */

	rv = check_std_fw_img(devicenode);

	if (rv == FWFLASH_FAILURE)
		rv = check_boot_fw_img(devicenode);

	if (rv == FWFLASH_FAILURE) {
		logmsg(MSG_ERROR,
		    gettext("\n%s firmware image verifier: "
			"file %s is not a valid firmware "
			"image for device %s\n\n"),
		    verifier->vendor,
		    verifier->imgfile,
		    devicenode->access_devname);
		/*
		 * Since we didn't allocate the space for the ident->* and
		 * addresses[*], set them to NULL and let the libraries
		 * (libnvpair and libdevinfo) handle them.
		 *
		 * We need to do this here so that the fwflash utility
		 * doesn't tie itself in knots and free something by mistake.
		 */
		devicenode->ident->vid = NULL;
		devicenode->ident->pid = NULL;
		devicenode->ident->revid = NULL;
		devicenode->addresses[0] = NULL;
		devicenode->addresses[1] = NULL;
	}

	return (rv);
}

static int
check_std_fw_img(struct devicelist *devicenode)
{
	struct MPI_FW_HEADER *fwheader;
	int i, rv, matched;

	if ((fwheader = calloc(1, sizeof (struct MPI_FW_HEADER)))
	    == NULL) {
		logmsg(MSG_ERROR, gettext("%s firmware image verifier: "
		    "unable to alloc memory for an image header check\n"),
		    verifier->vendor);
		return (FWFLASH_FAILURE);
	}

	bcopy(verifier->fwimage, fwheader, sizeof (struct MPI_FW_HEADER));

	logmsg(MSG_INFO,
	    "%s Verifier: Dev/Major/Minor/Unit 0x%02x/0x%02x/0x%02x/0x%02x\n",
	    verifier->vendor,
	    fwheader->FWVersion.Dev, fwheader->FWVersion.Major,
	    fwheader->FWVersion.Minor, fwheader->FWVersion.Unit);

	if ((ARMSWAPBITS(fwheader->Signature0) != MPI_FW_HEADER_SIGNATURE_0) ||
	    (ARMSWAPBITS(fwheader->Signature1) != MPI_FW_HEADER_SIGNATURE_1) ||
	    (ARMSWAPBITS(fwheader->Signature2) != MPI_FW_HEADER_SIGNATURE_2)) {

		logmsg(MSG_INFO,
		    "%s firmware image verifier: firmware image "
		    "signatures don't match\n\t(1: 0x%8x | 0x%8x, "
		    "2: 0x%8x | 0x%8x, 3: 0x%8x | 0x%8x)\n",
		    verifier->vendor,
		    ARMSWAPBITS(fwheader->Signature0),
		    MPI_FW_HEADER_SIGNATURE_0,
		    ARMSWAPBITS(fwheader->Signature1),
		    MPI_FW_HEADER_SIGNATURE_1,
		    ARMSWAPBITS(fwheader->Signature2),
		    MPI_FW_HEADER_SIGNATURE_2);
		return (FWFLASH_FAILURE);
	} else {
		logmsg(MSG_INFO,
		    "%s firmware image verifier:\n\tSignature0: "
		    "0x%x\n\tSignature1: 0x%x\n\tSignature2: 0x%x\n",
		    verifier->vendor,
		    ARMSWAPBITS(fwheader->Signature0),
		    ARMSWAPBITS(fwheader->Signature1),
		    ARMSWAPBITS(fwheader->Signature2));
	}

	if ((rv = std_img_checksumcalc()) != 0) {
		logmsg(MSG_INFO,
		    "%s firmware image verifier: firmware image checksum "
		    "is invalid (0x%04x, should be 0x0000)\n",
		    verifier->vendor, rv);
		return (FWFLASH_FAILURE);
	}

	/*
	 * Now check that the devicenode's ident->pid matches the
	 * firmware image's ProductID field
	 */

	i = 0;
	matched = 0;

	while (ipmtab[i].inqstr != NULL) {
		logmsg(MSG_INFO, "inqstr '%s':0x%02x, device '%s':0x%02x\n",
		    ipmtab[i].inqstr, ipmtab[i].ProductId,
		    devicenode->ident->pid, LE_16(fwheader->ProductId));

		if ((strncmp(devicenode->ident->pid,
		    ipmtab[i].inqstr, strlen(ipmtab[i].inqstr))
		    == 0) &&
		    (LE_16(fwheader->ProductId) == ipmtab[i].ProductId)) {
			/* matched up, all good */
			logmsg(MSG_INFO,
			    "%s-%s verifier: ProductId (0x%x) matches "
			    "SCSI INQUIRY data (%s)\n",
			    devicenode->drvname,
			    verifier->vendor,
			    fwheader->ProductId,
			    devicenode->ident->pid);
			matched = 1;
			break;
		}
		i++;
	}

	if (!matched) {
		logmsg(MSG_INFO,
		    "%s firmware image verifier: Firmware image %s does not "
		    "match device %s\n",
		    verifier->vendor,
		    verifier->imgfile,
		    devicenode->access_devname);
		return (FWFLASH_FAILURE);
	}

	if (ARMSWAPBITS(fwheader->ImageSize) != verifier->imgsize) {
		logmsg(MSG_INFO,
		    "%s firmware image verifier: firmware image size does "
		    "not match expected (0x%04x vs 0x%04x)\n",
		    verifier->vendor,
		    ARMSWAPBITS(fwheader->ImageSize),
		    ARMSWAPBITS(verifier->imgsize));
		return (FWFLASH_FAILURE);
	}

	logmsg(MSG_INFO, "\nFirmware Verifier for %s devices\n",
	    verifier->vendor);
	logmsg(MSG_INFO, "\tverifier filename:\t%s\n", verifier->filename);
	logmsg(MSG_INFO, "\tfirmware filename:\t%s\n", verifier->imgfile);
	logmsg(MSG_INFO, "\tvendor identifier:\t%s\n", verifier->vendor);
	logmsg(MSG_INFO, "\tfirmware image size:\t%d bytes\n",
	    verifier->imgsize);

	free(fwheader);

	verifier->flashbuf = LSI_STANDARD_BUFID; /* secret sauce */
	return (FWFLASH_SUCCESS);
}

/* Use the global var verifier for the raw data */
static int
std_img_checksumcalc()
{
	unsigned int length;
	int checksum, i;
	int *walker;

	length = verifier->imgsize / 4; /* we're doing "words" */
	checksum = 0;
	walker = verifier->fwimage;


	for (i = 0; i < length; ++i) {
		checksum += ARMSWAPBITS(walker[i]);
	}

	return (checksum);
}

static int
check_boot_fw_img(struct devicelist *devicenode)
{
	struct MPI_BOOT_HEADER *boothdr;
	int csum;
	char *templ, *tempr;

	templ = calloc(1, MAXINQLEN);
	tempr = calloc(1, MAXINQLEN);
	if ((templ == NULL) || (tempr == NULL)) {
		logmsg(MSG_ERROR,
		    gettext("%s firmware image verifier: "
		    "Unable to allocate memory for a boot image check(1)\n"),
		    verifier->vendor);
		return (FWFLASH_FAILURE);
	}

	boothdr = calloc(1, sizeof (struct MPI_BOOT_HEADER));
	if (boothdr == NULL) {
		logmsg(MSG_ERROR,
		    gettext("%s firmware image verifier: "
		    "unable to allocate memory for a boot image "
		    "header check (2)\n"),
		    verifier->vendor);
		free(templ);
		free(tempr);
		return (FWFLASH_FAILURE);
	}
	bcopy(verifier->fwimage, boothdr, sizeof (struct MPI_BOOT_HEADER));

	if ((boothdr->Signature[0] != 'Y') ||
	    (boothdr->Signature[1] != 'e') ||
	    (boothdr->Signature[2] != 't') ||
	    (boothdr->Signature[3] != 'i')) {
		logmsg(MSG_INFO,
		    "%s firmware image verifier: Image file %s is not a "
		    "valid Boot/SEEProm image\n",
		    verifier->vendor, verifier->imgfile);
		free(templ);
		free(tempr);
		free(boothdr);
		return (FWFLASH_FAILURE);
	}

	if (((boothdr->VendorId[0] != 'S') ||
	    (boothdr->VendorId[1] != 'U') ||
	    (boothdr->VendorId[2] != 'N')) &&
	    ((boothdr->VendorId[0] != 'L') ||
	    (boothdr->VendorId[1] != 'S') ||
	    (boothdr->VendorId[2] != 'I'))) {
		/* not a known enclosure manufacturer */
		logmsg(MSG_INFO,
		    "%s firmware image verifier: Image file %s has an "
		    "unrecognised VendorId (%8c)\n",
		    verifier->vendor, verifier->imgfile,
		    boothdr->VendorId);
		free(templ);
		free(tempr);
		free(boothdr);
		return (FWFLASH_FAILURE);
	}

	logmsg(MSG_INFO, "device '%s', boot image file '%16s'\n",
	    devicenode->ident->pid, boothdr->ProductId);

	strncpy(templ, devicenode->ident->pid, 16);
	/* We know this is _definitely_ a 16 byte field */
	bcopy(boothdr->ProductId, tempr, 16);

	if ((strncmp(templ, tempr, strlen(templ))) == 0) {
		/* matched up, all good */
		logmsg(MSG_INFO,
		    "%s-%s verifier: Boot record ProductId matches "
		    "SCSI INQUIRY data\n",
		    devicenode->drvname, verifier->vendor);
	} else {
		logmsg(MSG_INFO,
		    gettext("%s-%s firmware image verifier: "
		    "boot image file %s does not appear to be "
		    "for device\n%s\n"),
		    devicenode->drvname, verifier->vendor,
		    verifier->imgfile, devicenode->access_devname);
		free(templ);
		free(tempr);
		free(boothdr);
		return (FWFLASH_FAILURE);
	}

	free(tempr);
	free(templ);

	/*
	 * MPI_BOOT_HEADER proper doesn't start until 64bytes from
	 * the start of the file we read in. The last element of the
	 * structure is a collection of Optional substructures which
	 * aren't precisely defined. We assume that if the MPI_BOOT_HEADER
	 * matches up then we're ok to proceed.
	 */
	if ((csum = boot_checksumcalc((int8_t *)boothdr + MPI_BOOT_PROLOG,
	    sizeof (struct MPI_BOOT_HEADER) - MPI_BOOT_PROLOG -
	    sizeof (uint32_t *))) != 0x00) {
		logmsg(MSG_INFO,
		    gettext("%s firmware image verifier: "
		    "Calculated Boot/SEEProm checksum (0x%0x) does not "
		    "match file value (0x%0x)\n"),
		    csum, boothdr->BootChecksum);
		free(boothdr);
		return (FWFLASH_FAILURE);
	}

	free(boothdr);
	verifier->flashbuf = LSI_BOOTRECORD_BUFID; /* secret sauce */
	return (FWFLASH_SUCCESS);
}

static int8_t
boot_checksumcalc(int8_t *data, int length)
{

	int i;
	int8_t checksum;

	checksum = 0;

	for (i = 0; i < length; i++) {
		checksum += data[i];
	}

	return (checksum);
}
