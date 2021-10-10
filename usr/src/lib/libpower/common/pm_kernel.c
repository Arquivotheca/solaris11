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
 * libpower routines that interface with the power management driver and
 * the kernel
 */

#include <sys/types.h>
#include <sys/pm.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <strings.h>
#include <libnvpair.h>
#include <libpower.h>
#include <libpower_impl.h>


pm_error_t
pm_kernel_listprop(nvlist_t **result)
{
	pm_error_t	err;
	int		fd;
	boolean_t	valb;
	uint64_t	valu;
	nvlist_t	*kres;
	nvpair_t	*el;
	char		*authname;
	char		**vals;
	char		*tok;
	char		*propname;
	pm_authority_t	authority;
	data_type_t	proptype;
	void		*propval;
	pm_config_t	config;

	fd = open(PM_DEV_PATH, O_RDWR);
	if (fd == -1) {
		return (PM_ERROR_SYSTEM);
	}

	/* Ask the kernel what size buffer is required to get the properties */
	errno = 0;
	config.size = 0;
	config.buf = NULL;
	while (ioctl(fd, PM_GET_PROPS, &config) == -1 &&
	    (errno == EOVERFLOW || errno == EINTR)) {
		/* Allocate a buffer of the size indicated by the kernel */
		if (config.buf != NULL) {
			free(config.buf);
		}
		if (config.size == 0 && errno == EOVERFLOW) {
			/* The kernel did not return a buffer size. */
			uu_dprintf(pm_log, UU_DPRINTF_DEBUG,
			    "%s kernel did not return a buffer size\n",
			    __FUNCTION__);
			errno = EINVAL;
			break;
		} else {
			config.buf = malloc(config.size);
			if (config.buf == NULL) {
				/* Could not allocate memory */
				uu_dprintf(pm_log, UU_DPRINTF_DEBUG,
				    "%s could not allocate %d bytes: %d (%s)\n",
				    __FUNCTION__, config.size, errno,
				    strerror(errno));
				break;
			}
		}

		/*
		 * Memory has been allocated. Zero the buffer, clear the
		 * errno, and try to get the properties again.
		 */
		bzero(config.buf, config.size);
		errno = 0;
	}
	if (errno != 0) {
		int serr = errno;
		(void) close(fd);
		free(config.buf);
		errno = serr;

		return (PM_ERROR_SYSTEM);
	}
	(void) close(fd);

	/*
	 * Unpack the list of properties returned by the kernel into an nvlist.
	 */
	if ((errno = nvlist_unpack(config.buf, config.size, &kres, 0)) != 0) {
		free(config.buf);
		return (PM_ERROR_NVLIST);
	}
	free(config.buf);

	/*
	 * Scan the list of properties returned by the kernel and build a
	 * result list to return based on the data type of each property.
	 */
	err = PM_SUCCESS;
	el = nvlist_next_nvpair(kres, NULL);
	while (el != NULL && err == PM_SUCCESS) {
		/*
		 * The kernel returns a list with nvpair names in the form:
		 *	authority_name / property_name
		 *
		 * extract both the property name and authority name.
		 */
		tok = strdup(nvpair_name(el));
		propname = pm_parse_propname(tok, &authname);
		authority = pm_authority_get(authname);
		if (authority == PM_AUTHORITY_INVALID) {
			/*
			 * Any property received from the kernel without a
			 * valid authority prefix is by definition a current
			 * value.
			 */
			authority = PM_AUTHORITY_CURRENT;
		}

		/*
		 * Add the property value to the result list based on the
		 * data type of the property.
		 */
		err = PM_ERROR_NVLIST;
		proptype = nvpair_type(el);
		switch (proptype) {
		case DATA_TYPE_BOOLEAN_VALUE:
			errno = nvpair_value_boolean_value(el, &valb);
			if (errno == 0) {
				propval = (void *)&valb;
				err = pm_result_add(result, authority, NULL,
				    propname, proptype, propval, 1);
			}
			break;

		case DATA_TYPE_UINT64:
			errno = nvpair_value_uint64(el, &valu);
			if (errno == 0) {
				propval = (void *)&valu;
				err = pm_result_add(result, authority, NULL,
				    propname, proptype, propval, 1);
			}
			break;

		case DATA_TYPE_STRING:
			vals = calloc(1, sizeof (char *));
			errno = nvpair_value_string(el, &(vals[0]));
			if (errno == 0) {
				propval = (void *)vals;
				err = pm_result_add(result, authority, NULL,
				    propname, proptype, propval, 1);
			}
			if (vals != NULL) {
				free(vals);
			}
			break;

		default:
			err = PM_ERROR_INVALID_TYPE;
			errno = EINVAL;
			break;
		}

		/* Parse the next pair */
		free(tok);
		el = nvlist_next_nvpair(kres, el);
	}
	nvlist_free(kres);

	if (err == PM_SUCCESS) {
		/* Add property group names to each property */
		err = pm_smf_add_pgname(*result, PM_SVC_POWER);
	}

	return (err);
}


/*
 * Update the kernel using the values in the specified nvlist.  The input
 * nvlist has the same form as that output by the listprop routine, above.
 */
pm_error_t
pm_kernel_update(nvlist_t *newlist)
{
	pm_error_t	err;
	int		serrno;
	int		fd;
	boolean_t	valb;
	uint64_t	valu;
	char		*valsp;
	const char	*propname;
	nvlist_t	*outl;
	nvlist_t	*newval;
	nvpair_t	*nvp;
	nvpair_t	*smf;
	pm_config_t	pconf;

	if ((errno = nvlist_alloc(&outl, NV_UNIQUE_NAME, 0)) != 0) {
		return (PM_ERROR_NVLIST);
	}

	err = PM_SUCCESS;
	errno = 0;
	for (nvp = nvlist_next_nvpair(newlist, NULL);
	    nvp != NULL && err == PM_SUCCESS;
	    nvp = nvlist_next_nvpair(newlist, nvp)) {
		/* Get the name of the current property */
		propname = nvpair_name(nvp);

		/* Get the value list for this property */
		errno = nvlist_lookup_nvlist(newlist, propname, &newval);
		if (errno != 0) {
			/* Badly formed list */
			err = PM_ERROR_NVLIST;
			break;
		}

		/*
		 * Find the SMF value for this property in its value list.
		 * This is the value to synchronize with the kernel.
		 */
		errno = nvlist_lookup_nvpair(newval, PM_AUTHORITY_SMF_STR,
		    &smf);
		if (errno == ENOENT) {
			/*
			 * This pair does not have an SMF value and should not
			 * be sent to the kernel
			 */
			uu_dprintf(pm_log, UU_DPRINTF_DEBUG,
			    "%s property %s does not have an SMF value\n",
			    __FUNCTION__, propname);
			continue;

		} else if (errno != 0) {
			uu_dprintf(pm_log, UU_DPRINTF_FATAL,
			    "%s encountered a fatal nvlist error %d (%s)\n",
			    __FUNCTION__, errno, strerror(errno));
			err = PM_ERROR_NVLIST;
			break;
		}

		err = PM_ERROR_NVLIST;
		errno = 0;
		switch (nvpair_type(smf)) {
		case DATA_TYPE_BOOLEAN_VALUE:
			valb = B_FALSE;
			errno = nvpair_value_boolean_value(smf, &valb);
			if (errno == 0) {
				errno = nvlist_add_boolean_value(outl,
				    propname, valb);
			}
			if (errno == 0) {
				err = PM_SUCCESS;
				uu_dprintf(pm_log, UU_DPRINTF_DEBUG,
				    "%s added %s = %d to kernel update list\n",
				    __FUNCTION__, propname, valb);
			} else {
				uu_dprintf(pm_log, UU_DPRINTF_FATAL,
				    "%s encountered a fatal nvlist error %d "
				    "(%s)\n", __FUNCTION__, errno,
				    strerror(errno));
			}

			break;

		case DATA_TYPE_STRING:
			errno = nvpair_value_string(smf, &valsp);
			if (errno == 0) {
				errno = nvlist_add_string(outl, propname,
				    valsp);
			}
			if (errno == 0) {
				err = PM_SUCCESS;
				uu_dprintf(pm_log, UU_DPRINTF_DEBUG,
				    "%s added %s = %s to kernel update list\n",
				    __FUNCTION__, propname, valsp);
			} else {
				uu_dprintf(pm_log, UU_DPRINTF_FATAL,
				    "%s encountered a fatal nvlist error %d "
				    "(%s)\n", __FUNCTION__, errno,
				    strerror(errno));
			}
			break;

		case DATA_TYPE_UINT64:
			valu = 0;
			errno = nvpair_value_uint64(smf, &valu);
			if (errno == 0) {
				errno = nvlist_add_uint64(outl, propname, valu);
			}
			if (errno == 0) {
				err = PM_SUCCESS;
				uu_dprintf(pm_log, UU_DPRINTF_DEBUG,
				    "%s added %s = %llu to kernel update "
				    "list\n", __FUNCTION__, propname, valu);
			} else {
				uu_dprintf(pm_log, UU_DPRINTF_FATAL,
				    "%s encountered a fatal nvlist error %d "
				    "(%s)\n", __FUNCTION__, errno,
				    strerror(errno));
			}
			break;

		default:
			uu_dprintf(pm_log, UU_DPRINTF_FATAL,
			    "%s unhandled SMF value type %d property %s\n",
			    __FUNCTION__, nvpair_type(smf), propname);
			errno = EINVAL;
			break;
		}
	}
	if (err != PM_SUCCESS) {
		/* Failed to build a list to send to the kernel */
		nvlist_free(outl);
		return (err);
	}

	/* Pack the nvlist into a buffer for transmission to the kernel */
	pconf.buf = NULL;
	pconf.size = 0;
	errno = nvlist_pack(outl, &pconf.buf, &pconf.size, NV_ENCODE_XDR, 0);
	if (errno != 0) {
		serrno = errno;
		if (pconf.buf != NULL) {
			free(pconf.buf);
		}
		nvlist_free(outl);
		uu_dprintf(pm_log, UU_DPRINTF_DEBUG,
		    "%s nvlist_pack failed with %d\n", __FUNCTION__, err);
		errno = serrno;

		return (PM_ERROR_NVLIST);
	}

	fd = open(PM_DEV_PATH, O_RDWR);
	if (fd == -1) {
		uu_dprintf(pm_log, UU_DPRINTF_DEBUG,
		    "%s open(%s) failed with %d (%s)\n", __FUNCTION__,
		    PM_DEV_PATH, errno, strerror(errno));

		return (PM_ERROR_SYSTEM);
	}

	/* Synchronize the SMF values with the kernel */
	errno = 0;
	while (ioctl(fd, PM_SET_PROPS, &pconf) == -1 && errno == EINTR) {
		/* Interrupted system call.  Try again. */
		errno = 0;
	}
	serrno = errno;
	uu_dprintf(pm_log, UU_DPRINTF_DEBUG,
	    "%s ioctl returned -1 errno %d (%s)\n", __FUNCTION__, serrno,
	    strerror(serrno));

	/* Clean up memory, preserving any error created by the ioctl */
	(void) close(fd);
	if (pconf.buf != NULL) {
		free(pconf.buf);
	}
	nvlist_free(outl);

	/* Restore the ioctl's errno and return */
	errno = serrno;

	return ((errno == 0 ? PM_SUCCESS : PM_ERROR_SYSTEM));
}
