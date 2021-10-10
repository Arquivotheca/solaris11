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

#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <strings.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/modctl.h>
#include <sys/sunddi.h>
#include <sys/stropts.h>
#include <sys/sysmacros.h>
#include <sys/pci_param.h>
#include <sys/iov_param.h>
#include <libnvpair.h>
#include <libdevinfo.h>
#include "libiov.h"

#define	NUM_VFS_STRING_LEN	20

struct devlink_cb_arg {
	di_node_t	node;
	char		*linkname;
};

static int
is_streams_device(char *devfspath)
{
	di_node_t		node;
	di_node_t		di_root;
	char			*node_type;
	di_minor_t		minor = DI_MINOR_NIL;

	di_root = di_init("/", DINFOCACHE);
	if (di_root) {
		node = di_lookup_node(di_root, devfspath);
		di_fini(di_root);
		if (node == DI_NODE_NIL)
			return (0);
		minor = di_minor_next(node, minor);
		node_type = di_minor_nodetype(minor);
		if (strcmp(node_type, DDI_NT_NET) == 0)
			return (1);	/* streams device */
		else
			return (0);
	} else
		return (0);
}

static void
findlink(char *devfspath, char *linkname)
{
	di_node_t		node;
	di_node_t		di_root;
#ifdef DEBUG
	char			*minor_path;
#endif
	char			*path_devices = NULL;
	di_minor_t		minor = DI_MINOR_NIL;

	linkname[0] = '\0';
	di_root = di_init("/", DINFOCACHE);
	if (di_root) {
		node = di_lookup_node(di_root, devfspath);
		di_fini(di_root);
		if (node == DI_NODE_NIL)
			return;
#ifdef DEBUG
		minor_path =
		    di_devfs_minor_path(di_minor_next(node, DI_MINOR_NIL));

		(void) printf("node name = %s\n", di_node_name(node));
		(void) printf("devfspath = %s\n", di_devfs_path(node));
		(void) printf("bind_name = %s\n", di_binding_name(node));
		(void) printf("driver_name = %s\n", di_driver_name(node));
		(void) printf("instance = %d\n", di_instance(node));
		(void) printf("driver_major = %d\n", di_driver_major(node));
		(void) printf("devfs_minor_path = %s\n", minor_path);
		(void) printf("node_type = %s\n",
		    di_minor_nodetype(di_minor_next(node, DI_MINOR_NIL)));
		(void) printf("spec_type = 0x%x\n",
		    di_minor_next(node, DI_MINOR_NIL)->spec_type);
		di_devfs_path_free(minor_path);
#endif
	} else {
		return;
	}
	while (minor = di_minor_next(node, minor)) {
#ifdef DEBUG
		(void) printf("major = %d minor = %d devfs_minor_path = %s\n",
		    DI_MINOR(minor)->dev_major,
		    DI_MINOR(minor)->dev_minor,
		    (minor_path = di_devfs_minor_path(minor)));
		di_devfs_path_free(minor_path);
#endif
		if (di_driver_major(node) == DI_MINOR(minor)->dev_major) {
			/* found a match */
			path_devices = di_devfs_minor_path(minor);
			(void) strcpy(linkname, "/devices");
			(void) strcat(linkname, path_devices);
#ifdef DEBUG
			(void) printf("matching path = %s\n",
			    linkname);
#endif
			break;
		}
	}
#ifdef DEBUG
	if (is_streams_device(devfspath))
		(void) printf("this is a STREAMS device\n");
#endif
}

/*
 * Obtain the list of PFs in the system.
 * Returns:
 * 	Success:	rv:	0
 *	Failure:
 *			rv:
 *			  EINVAL - Invalid pf_list_nvlist pointer
 *			  ENOMEM - Couldn't allocate memory for PF list
 *			  ENODEV - No PFs (modctl())
 *			  EFAULT - Error while copying in/out (modctl())
 */
int
iov_get_pf_list(nvlist_t **pf_list_nvlist)
{

	int		i, err, rv, fd;
	uint_t		len;
	char		*pfp;
	uint_t		nelem;
	data_type_t	nvp_type;
	nvlist_t	**nvl_array;
	nvpair_t	*nvp;
	boolean_t	get_pf_list_again = B_FALSE;
	char		linkname[MAXPATHLEN];
	char		*pf_pathname;

	if (pf_list_nvlist == NULL)
		return (EINVAL);
	*pf_list_nvlist = NULL;
	rv = modctl(MODIOVOPS, MODIOVOPS_GET_PF_LIST, NULL, NULL, NULL, &len);
	if (rv != 0) {
		return (rv);
	}

	pfp = malloc(len);
	if (pfp == NULL) {
		return (ENOMEM);
	}

	rv = modctl(MODIOVOPS, MODIOVOPS_GET_PF_LIST, NULL, NULL, pfp, &len);
	if (rv != 0)
		goto exit;

	rv = nvlist_unpack(pfp, (size_t)len, pf_list_nvlist,
	    NV_ENCODE_NATIVE);
	if (rv != 0)
		goto exit;
	/*
	 * if a PF has no "pfg-mgmt-supported" property set, it could also
	 * be because it is not attached yet. Try opening all PFs that do not
	 * have "pf-mgmt-property"  to force an attach and refresh the PF list
	 * again to get the accurate info.
	 */
	nvp = nvlist_next_nvpair(*pf_list_nvlist, NULL);
	if (nvp == NULL)
		goto exit;
	nvp_type = nvpair_type(nvp);
	if (nvp_type != DATA_TYPE_NVLIST_ARRAY)
		goto exit;
	err = nvpair_value_nvlist_array(nvp, &nvl_array, &nelem);
	if (err)
		goto exit;
	for (i = 0; i < nelem; i++) {
		if (nvlist_lookup_boolean(nvl_array[i],
		    "pf-mgmt-supported") != ENOENT)
			continue;
		/*
		 * pf-mgmt-supported property is absent.
		 * Get the pathname for the PF device.
		 */
		err = nvlist_lookup_string(nvl_array[i],
		    "path", &pf_pathname);
		if (err)
			continue;
		(void) findlink(pf_pathname, linkname);
		if (linkname[0] == '\0')
			continue;
		/*
		 * Attach the PF device
		 */
		fd = open(linkname, O_RDONLY);
		if (fd == -1) {
			continue;
		}
		(void) close(fd);
		get_pf_list_again = B_TRUE;
	}
	if (get_pf_list_again) {
		/*
		 * One or more PF devices had missing pf-mgmt-supported
		 * property.
		 */
		nvlist_free(*pf_list_nvlist);	/* free the previous nvlist */
		*pf_list_nvlist = NULL;
		rv = modctl(MODIOVOPS, MODIOVOPS_GET_PF_LIST, NULL, NULL,
		    pfp, &len);
		if (rv == 0)
			rv = nvlist_unpack(pfp, (size_t)len, pf_list_nvlist,
			    NV_ENCODE_NATIVE);
	}
exit:
	free(pfp);
	return (rv);
}

/*
 * Obtain the device specific private properties for the given PF(pathname).
 * Returns:
 * 	Success:	rv:	0
 *	Failure:
 *			rv:
 *			  1      - General failure
 *			  EINVAL - Invalid Handle
 *			  ENOMEM - Couldn't allocate memory for prop list
 *			  ENODEV - No such PF (modctl())
 *			  EFAULT - Error while copying in/out (modctl())
 *			  ENOTSUP - ioctl not supported
 */
int
iov_devparam_get(char *pf_pathname, uint32_t *version, uint32_t *num_params,
    void **buf)
{
	int			rv;
	iov_param_desc_t	*pdesc_list;
	int			fd;
	char			linkname[MAXPATHLEN];
	iov_param_ver_info_t	iov_param_ver;
	struct	strioctl	str_ioctl;
	int			streams_device = 0;

	if ((pf_pathname == NULL) || (buf == NULL) || (num_params == NULL) ||
	    (version == NULL))
		return (EINVAL);
	*buf = NULL;
	*num_params = 0;
	*version = 0;
	(void) findlink(pf_pathname, linkname);
	if (linkname[0] == '\0') {
		return (1);
	}
	fd = open(linkname, O_RDONLY);
	if (fd == -1) {
		(void) printf("can't open file %s\n", linkname);
		return (1);
	}
	streams_device = is_streams_device(pf_pathname);
	if (streams_device) {
		str_ioctl.ic_cmd = IOV_GET_PARAM_VER_INFO;
		str_ioctl.ic_timout = -1;	/* no timeout. Wait forever */
		str_ioctl.ic_len = sizeof (iov_param_ver_info_t);
		str_ioctl.ic_dp = (char *)(&iov_param_ver);
		rv = ioctl(fd, I_STR, &str_ioctl);
	} else {
		rv = ioctl(fd, IOV_GET_PARAM_VER_INFO, &iov_param_ver);
	}
	if (rv) {
#ifdef DEBUG
		perror("ioctl to get version info failed\n");
#endif
		(void) close(fd);
		return (ENOTSUP);
	}
	if (iov_param_ver.num_params == 0) {
		(void) close(fd);
		return (0);	/* no params to configure */
	}
	pdesc_list = malloc(
	    sizeof (iov_param_desc_t) * iov_param_ver.num_params);
	if (pdesc_list == NULL) {
		(void) close(fd);
		return (ENOMEM);
	}
	if (streams_device) {
		str_ioctl.ic_cmd = IOV_GET_PARAM_INFO;
		str_ioctl.ic_timout = -1;	/* infinite */
		str_ioctl.ic_len =
		    (sizeof (iov_param_desc_t) * iov_param_ver.num_params);
		str_ioctl.ic_dp = (char *)(pdesc_list);
		rv = ioctl(fd, I_STR, &str_ioctl);
	} else {
		rv = ioctl(fd, IOV_GET_PARAM_INFO, pdesc_list);
	}
	if (rv) {
#ifdef DEBUG
		perror("ioctl to get param info failed\n");
#endif
		(void) close(fd);
		return (ENOTSUP);
	}
	*version = iov_param_ver.version;
	*num_params = iov_param_ver.num_params;
	*buf = (void *)pdesc_list;
	(void) close(fd);
	return (0);
}


/*
 * The nvlists for the PF/VF have been setup with the props to be validated.
 * The client wants to validate the properties now; we go ahead and issue the
 * modctl() command to do this.
 * The caller is responsible to free the reason_p if it is not NULL
 * This string if non NULL describes the reason for failure
 */
int
iov_devparam_validate(char *pf_pathname, int num_vfs, char **reason_p,
    nvlist_t **nvl)
{
	int		i, rv;
	size_t		nvlist_len;
	char		*buf = NULL;
	nvlist_t	*devparam_nvl = NULL;
	nvlist_t	**vf_nvlist = NULL;
	int		fd;
	iov_param_validate_t *pvalidate = NULL;
	int		saved_errno = 0;
	char		linkname[MAXPATHLEN];
	char		num_vfs_string[NUM_VFS_STRING_LEN];
	struct	strioctl	str_ioctl;
	int		streams_device = 0;
	int		rlen;
	char		*reason;

	if (reason_p)
		*reason_p = NULL;
	(void) findlink(pf_pathname, linkname);
	if (linkname[0] == '\0') {
		return (1);
	}
	fd = open(linkname, O_RDONLY);
	if (fd == -1)
		return (errno);
	streams_device = is_streams_device(pf_pathname);
	rv = nvlist_alloc(&devparam_nvl, NV_UNIQUE_NAME, KM_SLEEP);
	if (rv) {
		(void) close(fd);
		return (rv);
	}
	if (num_vfs)
		vf_nvlist = calloc((size_t)num_vfs, sizeof (void *));
	for (i = 0; i < num_vfs; i++) {
		rv = nvlist_alloc(&vf_nvlist[i], NV_UNIQUE_NAME, 0);
		if (rv)
			goto error_exit;
	}
	rv = nvlist_dup(nvl[0], &devparam_nvl, 0);
	if (rv) {
#ifdef DEBUG
		(void) printf("iov_devparam_validate: nvlist_dup() failed:"
		    " rv(%d)\n", rv);
#endif
		goto error_exit;
	}
	for (i = 0; i < num_vfs; i++) {
		if (nvl[ i + 1]) {
			vf_nvlist[i] = nvl[i + 1];
		} else {
			rv = nvlist_alloc(&vf_nvlist[i], NV_UNIQUE_NAME, 0);
			if (rv)
				goto error_exit;
		}
	}
	(void) sprintf(num_vfs_string, "%d", num_vfs);
	if (nvlist_add_nvlist_array(devparam_nvl, VF_PARAM_NVLIST_ARRAY_NAME,
	    vf_nvlist, num_vfs))
		goto error_exit;
	if (nvlist_add_string(devparam_nvl, PF_PATH_NVLIST_NAME, pf_pathname))
		goto error_exit;
	if (nvlist_add_string(devparam_nvl, NUM_VF_NVLIST_NAME, num_vfs_string))
		goto error_exit;
	if (nvlist_size(devparam_nvl, &nvlist_len, NV_ENCODE_NATIVE))
		goto error_exit;
	buf = malloc(nvlist_len);
	if (buf == NULL)
		goto error_exit;
	if (nvlist_pack(devparam_nvl, &buf, &nvlist_len, NV_ENCODE_NATIVE, 0))
		goto error_exit;
	pvalidate = malloc(nvlist_len + sizeof (iov_param_validate_t));
	if (pvalidate == NULL)
		goto error_exit;
	(void) memcpy(pvalidate->pv_buf, buf, nvlist_len);
	pvalidate->pv_buflen = nvlist_len;
	(void) printf("plist len = 0x%x\n", (uint_t)nvlist_len);
	if (streams_device) {
		str_ioctl.ic_cmd = IOV_VALIDATE_PARAM;
		str_ioctl.ic_timout = -1;	/* infinite */
		str_ioctl.ic_len = nvlist_len + sizeof (iov_param_validate_t);
		str_ioctl.ic_dp = (char *)(pvalidate);
		rv = ioctl(fd, I_STR, &str_ioctl);
	} else {
		rv = ioctl(fd, IOV_VALIDATE_PARAM, pvalidate);
	}
#ifdef DEBUG
	(void) printf("ic_len = %d after call to VALIDATE_PARAMS\n",
	    str_ioctl.ic_len);
#endif
	saved_errno = errno;
	if ((rv != 0) && (reason_p)) {
		/*
		 * The ioctl failed. Driver is supposed to have returned an
		 * error string.
		 */
		rlen = MAX_REASON_LEN + 1;
		reason = malloc(rlen);
		if (reason != NULL) {
			(void) strlcpy(reason, pvalidate->pv_reason, rlen);
			*reason_p = reason;
		}
	}
	free((char *)pvalidate);
error_exit:
	(void) close(fd);
	if (devparam_nvl)
		nvlist_free(devparam_nvl);
	if (vf_nvlist)
		free(vf_nvlist);
	if (buf)
		free(buf);
	errno = saved_errno;
	return (rv);
}
