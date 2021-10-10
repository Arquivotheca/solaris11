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
 * Copyright (c) 2010, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <unistd.h>
#include <locale.h>
#include <libgen.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <zone.h>
#include <sys/crypto/ioctladmin.h>
#include "cryptoadm.h"

#define	HW_CONF_DIR	"/platform/sun4v/kernel/drv"

#define	HW_PROVIDER_NCP		1
#define	HW_PROVIDER_N2CP	2
#define	HW_PROVIDER_N2RNG	3
#define	KERNEL_SW_PROVIDER	4

/* Get FIPS-140 status from configuration file */
int
fips_hw_status(char *filename, char *property, int *hw_fips_mode)
{
	FILE	*pfile;
	char	buffer[BUFSIZ];
	char	*str = NULL;
	char	*cursor = NULL;

	/* Open the configuration file */
	if ((pfile = fopen(filename, "r")) == NULL) {
		cryptodebug("failed to open %s for write.", filename);
		return (FAILURE);
	}

	while (fgets(buffer, BUFSIZ, pfile) != NULL) {
		if (buffer[0] == '#') {
			/* skip comments */
			continue;
		}

		/* find the property string */
		if ((str = strstr(buffer, property)) == NULL) {
			/* didn't find the property string in this line */
			continue;
		}
		/* find the property value */
		cursor = str + strlen(property);
		cursor = strtok(cursor, "= ;");
		if (cursor == NULL) {
			cryptoerror(LOG_STDERR, gettext(
			    "Invalid config file contents: %s."), filename);
			(void) fclose(pfile);
			return (FAILURE);
		}
		*hw_fips_mode = atoi(cursor);
		(void) fclose(pfile);
		return (SUCCESS);
	}

	/*
	 * If the fips property is not found in the config file,
	 * FIPS mode is false by default.
	 */
	*hw_fips_mode = CRYPTO_FIPS_MODE_DISABLED;
	(void) fclose(pfile);

	return (SUCCESS);
}

/*
 * Update the HW configuration file with the updated entry.
 */
int
fips_update_hw_conf(char *filename, char *property, int action)
{
	FILE		*pfile;
	FILE		*pfile_tmp;
	char		buffer[BUFSIZ];
	char		buffer2[BUFSIZ];
	char		*tmpfile_name = NULL;
	char		*str = NULL;
	char		*cursor = NULL;
	int		rc = SUCCESS;
	boolean_t	found = B_FALSE;
	boolean_t	is_etc_system = B_FALSE;

	/* Open the configuration file */
	if ((pfile = fopen(filename, "r+")) == NULL) {
		cryptoerror(LOG_STDERR,
		    gettext("failed to update the configuration - %s"),
		    strerror(errno));
		cryptodebug("failed to open %s for write.", filename);
		return (FAILURE);
	}

	/* Lock the configuration file */
	if (lockf(fileno(pfile), F_TLOCK, 0) == -1) {
		cryptoerror(LOG_STDERR,
		    gettext("failed to update the configuration - %s"),
		    strerror(errno));
		cryptodebug(gettext("failed to lock %s"), filename);
		(void) fclose(pfile);
		return (FAILURE);
	}

	if (strcmp(filename, "/etc/system") == 0) {
		is_etc_system = B_TRUE;
	}

	/*
	 * Create a temporary file to save updated configuration file first.
	 */
	tmpfile_name = tempnam(HW_CONF_DIR, NULL);
	if ((pfile_tmp = fopen(tmpfile_name, "w")) == NULL) {
		cryptoerror(LOG_STDERR, gettext("failed to open %s - %s"),
		    tmpfile_name, strerror(errno));
		free(tmpfile_name);
		(void) fclose(pfile);
		return (FAILURE);
	}


	/*
	 * Loop thru entire configuration file, update the entry to be
	 * updated and save the updated file to the temporary file first.
	 */
	while (fgets(buffer, BUFSIZ, pfile) != NULL) {
		if (buffer[0] == '#') {
			/* comments: write to the file without modification */
			goto write_to_tmp;
		}

		(void) strlcpy(buffer2, buffer, BUFSIZ);

		/* find the property string */
		if ((str = strstr(buffer2, property)) == NULL) {
			/*
			 * Didn't find the property string in this line.
			 * Write to the file without modification.
			 */
			goto write_to_tmp;
		}

		found = B_TRUE;

		cursor = str + strlen(property);
		cursor = strtok(cursor, "= ;");
		if (cursor == NULL) {
			cryptoerror(LOG_STDERR, gettext(
			    "Invalid config file contents %s: %s."),
			    filename, strerror(errno));
			goto errorexit;
		}

		cursor = buffer + (cursor - buffer2);
		*cursor = (action == FIPS140_ENABLE) ? '1': '0';

write_to_tmp:

		if (fputs(buffer, pfile_tmp) == EOF) {
			cryptoerror(LOG_STDERR, gettext(
			    "failed to write to a temp file: %s."),
			    strerror(errno));
			goto errorexit;
		}
	}

	/* if the fips mode property is not specified, FALSE by default */
	if (found == B_FALSE) {
		(void) snprintf(buffer, BUFSIZ, "%s=%c%s\n",
		    property,
		    (action == FIPS140_ENABLE) ? '1': '0',
		    is_etc_system ? "" : ";");
		if (fputs(buffer, pfile_tmp) == EOF) {
			cryptoerror(LOG_STDERR, gettext(
			    "failed to write to a tmp file: %s."),
			    strerror(errno));
			goto errorexit;
		}
	}

	(void) fclose(pfile);
	if (fclose(pfile_tmp) != 0) {
		cryptoerror(LOG_STDERR,
		    gettext("failed to close %s: %s"), tmpfile_name,
		    strerror(errno));
		free(tmpfile_name);
		return (FAILURE);
	}

	/* Copy the temporary file to the configuration file */
	if (rename(tmpfile_name, filename) == -1) {
		cryptoerror(LOG_STDERR,
		    gettext("failed to update the configuration - %s"),
		    strerror(errno));
		cryptodebug("failed to rename %s to %s: %s", tmpfile_name,
		    filename, strerror(errno));
		rc = FAILURE;
	} else if (chmod(filename,
	    S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH) == -1) {
		cryptoerror(LOG_STDERR,
		    gettext("failed to update the configuration - %s"),
		    strerror(errno));
		cryptodebug("failed to chmod to %s: %s", filename,
		    strerror(errno));
		rc = FAILURE;
	} else {
		rc = SUCCESS;
	}

	if ((rc == FAILURE) && (unlink(tmpfile_name) != 0)) {
		cryptoerror(LOG_STDERR, gettext(
		    "(Warning) failed to remove %s: %s"),
		    tmpfile_name, strerror(errno));
	}

	free(tmpfile_name);
	return (rc);

errorexit:
	(void) fclose(pfile);
	(void) fclose(pfile_tmp);
	free(tmpfile_name);

	return (FAILURE);
}

static boolean_t
does_prov_exist(char *provname)
{
	int			i;
	crypto_get_dev_list_t	*pdevlist_kernel = NULL;

	/* Kernel SW modules always exist */
	if (strncmp(provname, "kernel sw", MAXNAMELEN) == 0) {
		return (B_TRUE);
	}

	if (get_dev_list(&pdevlist_kernel) == FAILURE) {
		cryptoerror(LOG_STDERR, gettext("failed to retrieve "
		    "the list of kernel hardware providers.\n"));
		return (B_FALSE);
	}
	for (i = 0; i < pdevlist_kernel->dl_dev_count; i++) {
		char	*name;
		name = pdevlist_kernel->dl_devs[i].le_dev_name;
		if (strncmp(name, provname, MAXNAMELEN) == 0) {
			free(pdevlist_kernel);
			return (B_TRUE);
		}
	}
	free(pdevlist_kernel);
	return (B_FALSE);
}

#define	NUM_FIPS_SW_PROV	\
	(sizeof (fips_sw_providers) / sizeof (char *))

static char *fips_sw_providers[] = {
	"des",
	"aes",
	"ecc",
	"sha1",
	"sha2",
	"rsa",
	"swrand"
};

void
kernel_status_printf(char *provname, char *msg)
{
	if (strcmp(provname, "kernel sw") == 0) {
		int	i;
		for (i = 0; i < NUM_FIPS_SW_PROV; i++) {
			provname = fips_sw_providers[i];
			(void) printf("%s: %s.\n", provname, msg);
		}
	} else {
		(void) printf("%s: %s.\n", provname, msg);
	}
}

static int
do_actions(int action, int provider)
{
	int			rc = SUCCESS;
	int			fips_mode = 0;
	char			*filename;
	char			*propname;
	char			*provname;
	char			*msg = NULL;

	switch (provider) {
	case HW_PROVIDER_NCP:
		filename = "/platform/sun4v/kernel/drv/ncp.conf";
		propname = "ncp-fips-140";
		provname = "ncp";
		break;
	case HW_PROVIDER_N2CP:
		filename = "/platform/sun4v/kernel/drv/n2cp.conf";
		propname = "n2cp-fips-140";
		provname = "n2cp";
		break;
	case HW_PROVIDER_N2RNG:
		filename = "/platform/sun4v/kernel/drv/n2rng.conf";
		propname = "n2rng-fips-140";
		provname = "n2rng";
		break;
	case KERNEL_SW_PROVIDER:
		filename = "/etc/system";
		propname = "set kcf:fips140_mode";
		provname = "kernel sw";
		break;
	default:
		cryptoerror(LOG_STDERR, gettext(
		    "Internal Error: Invalid HW provider [%d] specified.\n"),
		    provider);
		return (FAILURE);
	}

	/* check the validity of the config file */
	if ((access(filename, F_OK) != 0) ||
	    (!does_prov_exist(provname))) {
		/*
		 * If there is no config file or the module is not loaded,
		 * don't do anything
		 */
		if (action == FIPS140_STATUS) {
			return (SUCCESS);
		} else {
			return (NO_CHANGE);
		}
	}

	/* Get FIPS-140 status from configuration file */
	if (fips_hw_status(filename, propname, &fips_mode) != SUCCESS) {
		return (FAILURE);
	}

	if (action == FIPS140_STATUS) {
		if (fips_mode == CRYPTO_FIPS_MODE_ENABLED)
			msg = gettext("FIPS-140 mode is enabled");
		else
			msg = gettext("FIPS-140 mode is disabled");
		kernel_status_printf(provname, msg);
		return (SUCCESS);
	}

	/* Is it a duplicate operation? */
	if ((action == FIPS140_ENABLE) &&
	    (fips_mode == CRYPTO_FIPS_MODE_ENABLED)) {
		kernel_status_printf(provname,
		    gettext("FIPS-140 mode has already been enabled"));
		return (NO_CHANGE);
	}

	if ((action == FIPS140_DISABLE) &&
	    (fips_mode == CRYPTO_FIPS_MODE_DISABLED)) {
		kernel_status_printf(provname,
		    gettext("FIPS-140 mode has already been disabled"));
		return (NO_CHANGE);
	}

	if ((action == FIPS140_ENABLE) || (action == FIPS140_DISABLE)) {
		/* Update configuration file */
		if ((rc = fips_update_hw_conf(filename, propname, action))
		    != SUCCESS)
			return (rc);
	}

	/* No need to inform kernel */
	if (action == FIPS140_ENABLE) {
		kernel_status_printf(provname,
		    gettext("FIPS-140 mode has enabled successfully"));
	} else {
		kernel_status_printf(provname,
		    gettext("FIPS-140 mode has disabled successfully"));
	}

	return (SUCCESS);
}


/*
 * Perform the FIPS related actions
 */
int
do_fips_hw_actions(int action)
{
	int		rv1, rv2, rv3, rv4;

	(void) printf(gettext("\nKernel software providers:\n"));
	(void) printf(gettext("==========================\n"));
	rv1 = do_actions(action, KERNEL_SW_PROVIDER);

	(void) printf(gettext("\nKernel hardware providers:\n"));
	(void) printf(gettext("=========================:\n"));
	rv2 = do_actions(action, HW_PROVIDER_NCP);
	rv3 = do_actions(action, HW_PROVIDER_N2CP);
	rv4 = do_actions(action, HW_PROVIDER_N2RNG);

	if ((rv1 == FAILURE) || (rv2 == FAILURE) ||
	    (rv3 == FAILURE) || (rv4 == FAILURE)) {
		/* if there is an error, return FAILURE */
		return (FAILURE);
	}
	if ((rv1 == SUCCESS) || (rv2 == SUCCESS) ||
	    (rv3 == SUCCESS) || (rv4 == SUCCESS)) {
		/* if at least one success(change), return SUCCESS */
		return (SUCCESS);
	}

	/* if there is no change in all modules, return NO_CHANGE */
	return (NO_CHANGE);
}
