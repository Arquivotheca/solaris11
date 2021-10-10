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
 * Copyright (c) 2011, Oracle and/or its affiliates. All rights reserved.
 */

/*
 * Determine whether this machine is capable of suspending.
 *
 * This is done for the i386 architecture by determining if the machine has
 * S3 capability, then ensuring it is on the whitelist.
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/smbios.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <stropts.h>
#include <string.h>
#include <kstat.h>
#include <libuutil.h>
#include <libpower.h>
#include <libpower_impl.h>

#define	PM_SE_ACPI	"acpi"
#define	PM_SE_WHITELIST	"suspend-support-enable"
#define	PM_SE_S3	"S3"

static boolean_t	pm_has_s3(void);
static boolean_t	pm_on_whitelist(void);
static pm_error_t	pm_product_manufacturer(smbios_info_t *);

/*
 * Determine if this machine supports suspend by determining:
 *	1. If the hardware supports S3
 *	2. If the machine is on the whitelist
 */
boolean_t
pm_get_suspendenable(void)
{
	boolean_t	result;

	/* First, determine if this machine supports S3 */
	result = pm_has_s3();
	if (result == B_FALSE) {
		/*
		 * S3 is either not supported, or it could not be determined
		 * whether it was supported.  The safe route is to therefore
		 * assume that the machine does not support suspend, which
		 * is the default condition and already set.
		 */
		return (result);
	}

	/*
	 * The machine has S3 support. Now we will check to see if it is
	 * on the whitelist.
	 */
	result = pm_on_whitelist();

	return (result);
}


/*
 * Determine if the hardware supports S3 by probing the acpi kstat module.
 */
static boolean_t
pm_has_s3(void)
{
	boolean_t	result;
	kstat_t		*ksp;
	kstat_ctl_t	*kc;
	kstat_named_t	*dp;

	/* Assume the machine does not have S3 until proven otherwise */
	errno = 0;
	if ((kc = kstat_open()) == NULL) {
		uu_dprintf(pm_log, UU_DPRINTF_FATAL,
		    "%s kstat_open failed %d (%s)\n", __FUNCTION__, errno,
		    strerror(errno));

		return (B_FALSE);
	}
	if ((ksp = kstat_lookup(kc, PM_SE_ACPI, -1, PM_SE_ACPI)) == NULL) {
		uu_dprintf(pm_log, UU_DPRINTF_FATAL,
		    "%s kstat_lookup \"%s\" failed errno %d (%s)\n",
		    __FUNCTION__, PM_SE_ACPI, errno, strerror(errno));

		(void) kstat_close(kc);
		return (B_FALSE);
	}
	if (kstat_read(kc, ksp, NULL) == -1) {
		uu_dprintf(pm_log, UU_DPRINTF_FATAL,
		    "%s kstat_read \"%s\" failed errno %d (%s)\n",
		    __FUNCTION__, PM_SE_ACPI, errno, strerror(errno));

		(void) kstat_close(kc);
		return (B_FALSE);
	}

	dp = kstat_data_lookup(ksp, PM_SE_S3);
	if (dp == NULL) {
		uu_dprintf(pm_log, UU_DPRINTF_NOTICE,
		    "%s kstat_data_lookup machine does not support \"%s\"\n",
		    __FUNCTION__, PM_SE_S3);

		/* Not having S3 is not an error, but we done checking now */
		(void) kstat_close(kc);
		return (B_FALSE);
	}
	(void) kstat_close(kc);

	result = B_FALSE;
	if (dp->value.l >= 1) {
		uu_dprintf(pm_log, UU_DPRINTF_NOTICE,
		    "%s kstat_data_lookup machine supports \"%s\" %ld\n",
		    __FUNCTION__, PM_SE_S3, dp->value.l);

		/* The kstat indicates this machine has S3 support. */
		result = B_TRUE;
	}

	return (result);
}


static boolean_t
pm_on_whitelist()
{
	boolean_t	result;
	pm_error_t	err;
	int		fd;
	int		ret;
	pm_searchargs_t	sargs;
	smbios_info_t	si_info;

	/* Assume the product is not on the white list */
	result = B_FALSE;

	err = pm_product_manufacturer(&si_info);
	if (err != PM_SUCCESS) {
		return (result);
	}

	fd = open(PM_DEV_PATH, O_RDWR);
	if (fd == -1) {
		return (result);
	}

	/*
	 * Prepare to search the whitelist
	 */
	sargs.pms_listname = PM_SE_WHITELIST;
	sargs.pms_manufacturer = (char *)si_info.smbi_manufacturer;
	sargs.pms_product = (char *)si_info.smbi_product;
	ret = ioctl(fd, PM_SEARCH_LIST, &sargs);
	switch (ret) {
	case 0:		/* A match was found if the ioctl succeeds */
		result = B_TRUE;
		err = PM_SUCCESS;
		break;

	default:	/* Other than 0 is no match */
		result = B_FALSE;
		err = PM_SUCCESS;
		break;
	}
	(void) close(fd);

	uu_dprintf(pm_log, UU_DPRINTF_DEBUG,
	    "%s list \"%s\" search returned %d\n",
	    __FUNCTION__, PM_SE_WHITELIST, ret);

	return (result);
}


/*
 * Determine the product manufacturer by probing smbios.  This technique is
 * used instead of calling libtopo, sysinfo, or similar library because
 * experimentation showed them to return null values on many machines.
 */
static pm_error_t
pm_product_manufacturer(smbios_info_t *si_info)
{
	int		result;
	pm_error_t	err;
	smbios_hdl_t	*shp;
	smbios_system_t	si_sys;
	id_t		id;

	shp = smbios_open(NULL, SMB_VERSION, SMB_O_NOCKSUM | SMB_O_NOVERS,
	    &result);
	if (shp == NULL) {
		uu_dprintf(pm_log, UU_DPRINTF_FATAL,
		    "%s failed to open smbios %d (%s)\n", __FUNCTION__,
		    errno, strerror(errno));
		return (PM_ERROR_SYSTEM);
	}

	err = PM_SUCCESS;
	if ((id = smbios_info_system(shp, &si_sys)) == SMB_ERR ||
	    smbios_info_common(shp, id, si_info) == SMB_ERR) {
		uu_dprintf(pm_log, UU_DPRINTF_FATAL,
		    "%s failed to search smbios %d (%s)\n",
		    __FUNCTION__, errno, strerror(errno));
		err = PM_ERROR_SYSTEM;
	}
	smbios_close(shp);

	uu_dprintf(pm_log, UU_DPRINTF_INFO,
	    "%s manufacturer \"%s\" product \"%s\"\n", __FUNCTION__,
	    si_info->smbi_manufacturer, si_info->smbi_product);

	return (err);
}
