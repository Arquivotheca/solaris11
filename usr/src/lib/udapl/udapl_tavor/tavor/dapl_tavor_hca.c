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

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/sockio.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <net/if.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <strings.h>
#include <fcntl.h>
#include <libdladm.h>
#include <libdlib.h>
#include <libdllink.h>
#include <sys/ib/ibnex/ibnex_devctl.h>

#include "dapl.h"
#include "dapl_adapter_util.h"
#include "dapl_tavor_ibtf_impl.h"
#include "dapl_hca_util.h"
#include "dapl_name_service.h"
#define	MAX_HCAS		64
#define	PROP_HCA_GUID		"hca-guid"
#define	PROP_PORT_NUM		"port-number"
#define	PROP_PORT_PKEY		"port-pkey"

#define	DEVDAPLT		"/dev/daplt"

/* function prototypes */
static DAT_RETURN dapli_process_tavor_node(char *dev_path, int *hca_idx,
    int try_blueflame);
static DAT_RETURN dapli_process_ia(dladm_ib_attr_t *ib_attr, DAPL_HCA *hca_ptr,
    int hca_idx);

#if defined(IBHOSTS_NAMING)
#include <stdio.h>
static int dapli_process_fake_ibds(DAPL_HCA **hca_list, int hca_idx);
#endif /* IBHOSTS_NAMING */

static DAPL_OS_LOCK g_tavor_state_lock;
static struct dapls_ib_hca_state g_tavor_state[MAX_HCAS];
DAPL_OS_LOCK g_tavor_uar_lock;

DAT_RETURN
dapli_init_hca(
	IN   DAPL_HCA			*hca_ptr)
{
	DAT_RETURN		dat_status = DAT_SUCCESS;
	int			hca_idx = 0;
	int			check_for_bf = 0;
	datalink_class_t	class;
	datalink_id_t		linkid;
	dladm_ib_attr_t		ib_attr;
	ibnex_ctl_query_hca_t	query_hca;
	int			ibnex_fd = -1;
	dladm_handle_t		dlh;
	char			hca_device_path[MAXPATHLEN];

	if (dladm_open(&dlh) != DLADM_STATUS_OK) {
		dapl_dbg_log(DAPL_DBG_TYPE_ERR,
		    "init_hca: dladm_open failed\n");
		return (DAT_INTERNAL_ERROR);
	}

	if ((ibnex_fd = open(IBNEX_DEVCTL_DEV, O_RDONLY)) < 0) {
		dat_status = DAT_ERROR(DAT_NAME_NOT_FOUND, 0);
		dapl_dbg_log(DAPL_DBG_TYPE_UTIL,
		    "init_hca: could not open ib nexus (%s)\n",
		    strerror(errno));
		goto bail;
	}

	if ((dladm_name2info(dlh, hca_ptr->name, &linkid, NULL, &class,
	    NULL) != DLADM_STATUS_OK) ||
	    (class != DATALINK_CLASS_PART) ||
	    (dladm_part_info(dlh, linkid, &ib_attr,
	    DLADM_OPT_ACTIVE) != DLADM_STATUS_OK)) {
		dat_status = DAT_ERROR(DAT_NAME_NOT_FOUND, 0);
		dapl_dbg_log(DAPL_DBG_TYPE_UTIL,
		    "init_hca: %s not found - couldn't get partition info\n",
		    hca_ptr->name);
		goto bail;
	}

	bzero(&query_hca, sizeof (query_hca));
	query_hca.hca_guid = ib_attr.dia_hca_guid;
	query_hca.hca_device_path = hca_device_path;
	query_hca.hca_device_path_alloc_sz = sizeof (hca_device_path);
	if (ioctl(ibnex_fd, IBNEX_CTL_QUERY_HCA, &query_hca) == -1) {
		dat_status = DAT_ERROR(DAT_NAME_NOT_FOUND, 0);
		dapl_dbg_log(DAPL_DBG_TYPE_UTIL,
		    "init_hca: %s not found; query_hca failed\n",
		    hca_ptr->name);
		goto bail;
	}

	if (strcmp(query_hca.hca_info.hca_driver_name, "tavor") == 0)
		dapls_init_funcs_tavor(hca_ptr);
	else if (strcmp(query_hca.hca_info.hca_driver_name, "arbel") == 0)
		dapls_init_funcs_arbel(hca_ptr);
	else if (strcmp(query_hca.hca_info.hca_driver_name, "hermon") == 0) {
		dapls_init_funcs_hermon(hca_ptr);
		check_for_bf = 1;
	} else {
		dat_status = DAT_ERROR(DAT_NAME_NOT_FOUND, 0);
		dapl_dbg_log(DAPL_DBG_TYPE_UTIL,
		    "init_hca: %s not found\n", hca_ptr->name);
		goto bail;
	}

	dat_status = dapli_process_tavor_node(hca_device_path, &hca_idx,
	    check_for_bf);
	if (dat_status != DAT_SUCCESS) {
		dapl_dbg_log(DAPL_DBG_TYPE_UTIL,
		    "init_hcas: %s process_tavor_node failed(0x%x)\n",
		    hca_ptr->name, dat_status);
		goto bail;
	}

#if defined(IBHOSTS_NAMING)
	if (dapli_process_fake_ibds(hca_ptr, hca_idx) == 0) {
		/* no entries were found */
		dat_status = DAT_ERROR(DAT_NAME_NOT_FOUND, 0);
	}
#else
	dat_status = dapli_process_ia(&ib_attr, hca_ptr, hca_idx);
#endif
	if (dat_status != DAT_SUCCESS) {
		dapl_dbg_log(DAPL_DBG_TYPE_UTIL,
		    "init_hcas: %s process_ia failed(0x%x)\n",
		    hca_ptr->name, dat_status);
		goto bail;
	}

	dapl_dbg_log(DAPL_DBG_TYPE_UTIL,
	    "init_hcas: done %s\n", hca_ptr->name);

bail:
	if (ibnex_fd != -1)
		(void) close(ibnex_fd);
	dladm_close(dlh);
	return (dat_status);
}

static DAT_RETURN
dapli_process_tavor_node(char *dev_path, int *hca_idx, int try_blueflame)
{
	char		path_buf[MAXPATHLEN];
	int		i, idx, fd;
#ifndef _LP64
	int		tmpfd;
#endif
	size_t		pagesize;
	void		*mapaddr;
	pid_t		cur_pid;
	off64_t		uarpg_offset;

	dapl_os_lock(&g_tavor_state_lock);

	for (idx = 0; idx < MAX_HCAS; idx++) {
		/*
		 * page size == 0 means this entry is not occupied
		 */
		if (g_tavor_state[idx].uarpg_size == 0) {
			break;
		}
	}
	if (idx == MAX_HCAS) {
		dapl_dbg_log(DAPL_DBG_TYPE_ERR,
		    "process_tavor: all hcas are being used!\n");
		dapl_os_unlock(&g_tavor_state_lock);
		return (DAT_ERROR(DAT_INSUFFICIENT_RESOURCES, 0));
	}

	for (i = 0; i < idx; i++) {
		if (strcmp(dev_path, g_tavor_state[i].hca_path) == 0) {
			/* no need for a refcnt */
			idx = i;
			goto done;
		}
	}

	/* Add 16 to accomodate the prefix "/devices" and suffix ":devctl" */
	if (strlen("/devices") + strlen(dev_path) + strlen(":devctl") + 1 >
	    MAXPATHLEN) {
		dapl_dbg_log(DAPL_DBG_TYPE_ERR,
		    "process_tavor: devfs path %s is too long\n",
		    dev_path);
		dapl_os_unlock(&g_tavor_state_lock);
		return (DAT_ERROR(DAT_INTERNAL_ERROR, 0));
	}
	(void) dapl_os_strcpy(path_buf, "/devices");
	(void) dapl_os_strcat(path_buf, dev_path);
	(void) dapl_os_strcat(path_buf, ":devctl");
	(void) dapl_os_strcpy(g_tavor_state[idx].hca_path, dev_path);

	pagesize = (size_t)sysconf(_SC_PAGESIZE);
	if (pagesize == 0) {
		dapl_dbg_log(DAPL_DBG_TYPE_ERR,
		    "process_tavor: page_size == 0\n");
		dapl_os_unlock(&g_tavor_state_lock);
		return (DAT_ERROR(DAT_INTERNAL_ERROR, 0));
	}
	cur_pid = getpid();

	fd = open(path_buf, O_RDWR);
	if (fd < 0) {
		dapl_dbg_log(DAPL_DBG_TYPE_ERR,
		    "process_tavor: cannot open %s: %s\n",
		    path_buf, strerror(errno));
		dapl_os_unlock(&g_tavor_state_lock);
		return (DAT_ERROR(DAT_INSUFFICIENT_RESOURCES, 0));
	}
#ifndef _LP64
	/*
	 * libc can't handle fd's greater than 255,  in order to
	 * ensure that these values remain available make fd > 255.
	 * Note: not needed for LP64
	 */
	tmpfd = fcntl(fd, F_DUPFD, 256);
	if (tmpfd < 0) {
		dapl_dbg_log(DAPL_DBG_TYPE_UTIL,
		"process_tavor: cannot F_DUPFD: %s\n", strerror(errno));
	} else {
		(void) close(fd);
		fd = tmpfd;
	}
#endif	/* _LP64 */

	if (fcntl(fd, F_SETFD, FD_CLOEXEC) < 0) {
		dapl_dbg_log(DAPL_DBG_TYPE_ERR,
		    "process_tavor: cannot F_SETFD: %s\n", strerror(errno));
		(void) close(fd);
		dapl_os_unlock(&g_tavor_state_lock);
		return (DAT_ERROR(DAT_INTERNAL_ERROR, 0));
	}

	uarpg_offset = (((off64_t)cur_pid << MLNX_UMAP_RSRC_TYPE_SHIFT) |
	    MLNX_UMAP_UARPG_RSRC) * pagesize;

	mapaddr = mmap64((void  *)0, pagesize, PROT_READ | PROT_WRITE,
	    MAP_SHARED, fd, uarpg_offset);
	if (mapaddr == MAP_FAILED) {
		dapl_dbg_log(DAPL_DBG_TYPE_ERR,
		    "process_tavor: mmap failed %s\n", strerror(errno));
		(void) close(fd);
		dapl_os_unlock(&g_tavor_state_lock);
		return (DAT_ERROR(DAT_INSUFFICIENT_RESOURCES, 0));
	}

	g_tavor_state[idx].hca_fd = fd;
	g_tavor_state[idx].uarpg_baseaddr = mapaddr;
	g_tavor_state[idx].uarpg_size = pagesize;

	if (try_blueflame == 0)
		goto done;

	/* Try to do the Hermon Blueflame page mapping */
	uarpg_offset = (((off64_t)cur_pid << MLNX_UMAP_RSRC_TYPE_SHIFT) |
	    MLNX_UMAP_BLUEFLAMEPG_RSRC) * pagesize;

	mapaddr = mmap64((void  *)0, pagesize, PROT_READ | PROT_WRITE,
	    MAP_SHARED, fd, uarpg_offset);
	if (mapaddr == MAP_FAILED) {
		/* This is not considered to be fatal.  Charge on! */
		dapl_dbg_log(DAPL_DBG_TYPE_WARN,
		    "process_tavor: mmap of blueflame page failed %s\n",
		    strerror(errno));
	} else {
		g_tavor_state[idx].bf_pg_baseaddr = mapaddr;
		g_tavor_state[idx].bf_toggle = 0;
	}
done:
	dapl_os_unlock(&g_tavor_state_lock);

	*hca_idx = idx;

	return (DAT_SUCCESS);
}

static DAT_RETURN
dapli_process_ia(dladm_ib_attr_t *ib_attr, DAPL_HCA *hca_ptr, int hca_idx)
{
	struct lifreq	lifreq;
	int		sfd, retval, af;
	char		addr_buf[64];

	if (ib_attr->dia_hca_guid == 0 || ib_attr->dia_portnum == 0 ||
	    ib_attr->dia_pkey == 0) {
		dapl_dbg_log(DAPL_DBG_TYPE_ERR,
		    "process_ia: invalid properties: guid 0x%016llx, "
		    "port %d, pkey 0x%08x\n", ib_attr->dia_hca_guid,
		    ib_attr->dia_portnum, (uint_t)ib_attr->dia_pkey);
		return (DAT_ERROR(DAT_INVALID_PARAMETER, 0));
	}

	/*
	 * if an interface has both v4 and v6 addresses plumbed,
	 * we'll take the v4 address.
	 */
	af = AF_INET;
again:
	sfd = socket(af, SOCK_DGRAM, 0);
	if (sfd < 0) {
		dapl_dbg_log(DAPL_DBG_TYPE_ERR,
		    "process_ia: socket failed: %s\n", strerror(errno));
		return (DAT_ERROR(DAT_INSUFFICIENT_RESOURCES, 0));
	}

	/* check if name will fit in lifr_name */
	if (dapl_os_strlen(hca_ptr->name) >= LIFNAMSIZ) {
		(void) close(sfd);
		dapl_dbg_log(DAPL_DBG_TYPE_ERR,
		    "process_ia: if name overflow %s\n",
		    hca_ptr->name);
		return (DAT_ERROR(DAT_INVALID_PARAMETER, 0));
	}

	(void) dapl_os_strcpy(lifreq.lifr_name, hca_ptr->name);
	retval = ioctl(sfd, SIOCGLIFADDR, (caddr_t)&lifreq);
	if (retval < 0) {
		(void) close(sfd);
		if (af == AF_INET6) {
			/*
			 * the interface is not plumbed.
			 */
			dapl_dbg_log(DAPL_DBG_TYPE_UTIL,
			    "process_ia: %s: ip address not found\n",
			    lifreq.lifr_name);
			return (DAT_ERROR(DAT_INSUFFICIENT_RESOURCES, 0));
		} else {
			/*
			 * we've failed to find a v4 address. now
			 * let's try v6.
			 */
			af = AF_INET6;
			goto again;
		}
	}
	(void) close(sfd);

	hca_ptr->tavor_idx = hca_idx;
	hca_ptr->node_GUID = ib_attr->dia_hca_guid;
	hca_ptr->port_num = ib_attr->dia_portnum;
	hca_ptr->partition_key = ib_attr->dia_pkey;
	(void) dapl_os_memcpy((void *)&hca_ptr->hca_address,
	    (void *)&lifreq.lifr_addr, sizeof (hca_ptr->hca_address));
	hca_ptr->max_inline_send = dapls_tavor_max_inline();

	dapl_dbg_log(DAPL_DBG_TYPE_UTIL,
	    "process_ia: interface %s, hca guid 0x%016llx, port %d, "
	    "pkey 0x%08x, ip addr %s\n", lifreq.lifr_name, hca_ptr->node_GUID,
	    hca_ptr->port_num, hca_ptr->partition_key, dapls_inet_ntop(
	    (struct sockaddr *)&hca_ptr->hca_address, addr_buf, 64));
	return (DAT_SUCCESS);
}

void
dapls_ib_state_init(void)
{
	int i;

	(void) dapl_os_lock_init(&g_tavor_state_lock);
	(void) dapl_os_lock_init(&g_tavor_uar_lock);
	(void) dapl_os_lock_init(&dapls_ib_dbp_lock);

	for (i = 0; i < MAX_HCAS; i++) {
		g_tavor_state[i].hca_fd = 0;
		g_tavor_state[i].uarpg_baseaddr = NULL;
		g_tavor_state[i].uarpg_size = 0;
		g_tavor_state[i].bf_pg_baseaddr = NULL;
	}
}

void
dapls_ib_state_fini(void)
{
	int i, count = 0;

	/*
	 * Uinitialize the per hca instance state
	 */
	dapl_os_lock(&g_tavor_state_lock);
	for (i = 0; i < MAX_HCAS; i++) {
		if (g_tavor_state[i].uarpg_size == 0) {
			dapl_os_assert(g_tavor_state[i].uarpg_baseaddr ==
			    NULL);
			continue;
		}
		if (munmap(g_tavor_state[i].uarpg_baseaddr,
		    g_tavor_state[i].uarpg_size) < 0) {
			dapl_dbg_log(DAPL_DBG_TYPE_ERR,
			    "ib_state_fini: "
			    "munmap(0x%p, 0x%llx) failed(%d)\n",
			    g_tavor_state[i].uarpg_baseaddr,
			    g_tavor_state[i].uarpg_size, errno);
		}
		if ((g_tavor_state[i].bf_pg_baseaddr != NULL) &&
		    (munmap(g_tavor_state[i].bf_pg_baseaddr,
		    g_tavor_state[i].uarpg_size) < 0)) {
			dapl_dbg_log(DAPL_DBG_TYPE_ERR,
			    "ib_state_fini: "
			    "munmap(0x%p, 0x%llx) of blueflame failed(%d)\n",
			    g_tavor_state[i].bf_pg_baseaddr,
			    g_tavor_state[i].uarpg_size, errno);
		}

		(void) close(g_tavor_state[i].hca_fd);
		count++;
	}
	dapl_os_unlock(&g_tavor_state_lock);

	dapl_os_lock_destroy(&g_tavor_uar_lock);
	dapl_os_lock_destroy(&g_tavor_state_lock);
	dapl_os_lock_destroy(&dapls_ib_dbp_lock);

	dapl_dbg_log(DAPL_DBG_TYPE_UTIL,
	    "ib_state_fini: cleaned %d hcas\n", count);
}

/*
 * dapls_ib_open_hca
 *
 * Open HCA
 *
 * Input:
 *      *hca_ptr          pointer to hca device
 *      *ib_hca_handle_p  pointer to provide HCA handle
 *
 * Output:
 *      none
 *
 * Return:
 *      DAT_SUCCESS
 *      DAT_INSUFFICIENT_RESOURCES
 *
 */
DAT_RETURN
dapls_ib_open_hca(
	IN DAPL_HCA		*hca_ptr,
	OUT ib_hca_handle_t	*ib_hca_handle_p)
{
	dapl_ia_create_t		args;
	DAT_RETURN			dat_status;
	struct dapls_ib_hca_handle	*hca_p;
	int				fd;
#ifndef _LP64
	int				tmpfd;
#endif
	int				retval;
	struct sockaddr *s;
	struct sockaddr_in6 *v6addr;
	struct sockaddr_in *v4addr;
	dapl_ia_addr_t *sap;

	dat_status = dapli_init_hca(hca_ptr);
	if (dat_status != DAT_SUCCESS) {
		dapl_dbg_log(DAPL_DBG_TYPE_ERR,
		    "dapls_ib_open_hca: init_hca failed %d\n", dat_status);
		return (dat_status);
	}

	fd = open(DEVDAPLT, O_RDONLY);
	if (fd < 0) {
		return (DAT_INSUFFICIENT_RESOURCES);
	}

#ifndef _LP64
	/*
	 * libc can't handle fd's greater than 255,  in order to
	 * ensure that these values remain available make fd > 255.
	 * Note: not needed for LP64
	 */
	tmpfd = fcntl(fd, F_DUPFD, 256);
	if (tmpfd < 0) {
		dapl_dbg_log(DAPL_DBG_TYPE_UTIL,
		    "dapls_ib_open_hca: cannot F_DUPFD: %s\n",
		    strerror(errno));
	} else {
		(void) close(fd);
		fd = tmpfd;
	}
#endif	/* _LP64 */

	if (fcntl(fd, F_SETFD, FD_CLOEXEC) < 0) {
		dapl_dbg_log(DAPL_DBG_TYPE_ERR,
		    "dapls_ib_open_hca: cannot F_SETFD: %s\n", strerror(errno));
		(void) close(fd);
		return (DAT_INTERNAL_ERROR);
	}

	hca_p = (struct dapls_ib_hca_handle *)dapl_os_alloc(
	    sizeof (struct dapls_ib_hca_handle));
	if (hca_p == NULL) {
		(void) close(fd);
		return (DAT_INSUFFICIENT_RESOURCES);
	}

	args.ia_guid = hca_ptr->node_GUID;
	args.ia_port = hca_ptr->port_num;
	args.ia_pkey = hca_ptr->partition_key;
	args.ia_version = DAPL_IF_VERSION;
	(void) dapl_os_memzero((void *)args.ia_sadata, DAPL_ATS_NBYTES);

	/* pass down local ip address to be stored in SA */
	s = (struct sockaddr *)&hca_ptr->hca_address;
	/* LINTED: E_BAD_PTR_CAST_ALIGN */
	sap = (dapl_ia_addr_t *)args.ia_sadata;
	switch (s->sa_family) {
	case AF_INET:
		/* LINTED: E_BAD_PTR_CAST_ALIGN */
		v4addr = (struct sockaddr_in *)s;
		sap->iad_v4 = v4addr->sin_addr;
		break;
	case AF_INET6:
		/* LINTED: E_BAD_PTR_CAST_ALIGN */
		v6addr = (struct sockaddr_in6 *)s;
		sap->iad_v6 = v6addr->sin6_addr;
		break;
	default:
		break; /* fall through */
	}

	retval = ioctl(fd, DAPL_IA_CREATE, &args);
	if (retval != 0) {
		dapl_dbg_log(DAPL_DBG_TYPE_ERR,
		    "open_hca: ia_create failed, fd %d, "
		    "guid 0x%016llx, port %d, pkey 0x%x, version %d\n",
		    fd, args.ia_guid, args.ia_port, args.ia_pkey,
		    args.ia_version);

		dapl_os_free(hca_p, sizeof (*hca_p));
		(void) close(fd);
		return (dapls_convert_error(errno, retval));
	}

	hca_p->ia_fd = fd;
	hca_p->ia_rnum = args.ia_resnum;
	hca_p->hca_fd = g_tavor_state[hca_ptr->tavor_idx].hca_fd;
	hca_p->ia_uar = g_tavor_state[hca_ptr->tavor_idx].uarpg_baseaddr;
	hca_p->ia_bf = g_tavor_state[hca_ptr->tavor_idx].bf_pg_baseaddr;
	hca_p->ia_bf_toggle = &g_tavor_state[hca_ptr->tavor_idx].bf_toggle;
	*ib_hca_handle_p = hca_p;
	dapl_dbg_log(DAPL_DBG_TYPE_UTIL,
	    "open_hca: ia_created, hca_p 0x%p, fd %d, "
	    "rnum %d, guid 0x%016llx, port %d, pkey 0x%x\n",
	    hca_p, hca_p->ia_fd, hca_p->ia_rnum, hca_ptr->node_GUID,
	    hca_ptr->port_num, hca_ptr->partition_key);

	return (DAT_SUCCESS);
}

/*
 * dapls_ib_close_hca
 *
 * Open HCA
 *
 * Input:
 *      ib_hca_handle   provide HCA handle
 *
 * Output:
 *      none
 *
 * Return:
 *      DAT_SUCCESS
 *      DAT_INSUFFICIENT_RESOURCES
 *
 */
DAT_RETURN
dapls_ib_close_hca(
	IN ib_hca_handle_t	ib_hca_handle)
{
	if (ib_hca_handle == NULL) {
		dapl_dbg_log(DAPL_DBG_TYPE_ERR,
		    "close_hca: ib_hca_handle == NULL\n");
		return (DAT_SUCCESS);
	}
	dapl_dbg_log(DAPL_DBG_TYPE_UTIL,
	    "close_hca: closing hca 0x%p, fd %d, rnum %d\n",
	    ib_hca_handle, ib_hca_handle->ia_fd, ib_hca_handle->ia_rnum);

	(void) close(ib_hca_handle->ia_fd);
	dapl_os_free((void *)ib_hca_handle,
	    sizeof (struct dapls_ib_hca_handle));
	return (DAT_SUCCESS);
}

#if defined(IBHOSTS_NAMING)
#define	LINE_LEN	256
static int
dapli_process_fake_ibds(DAPL_HCA *hca_ptr, int hca_idx)
{
	char		line_buf[LINE_LEN];
	char		host_buf[LINE_LEN];
	char		localhost[LINE_LEN];
	ib_guid_t	prefix;
	ib_guid_t	guid;
	FILE		*fp;
	int		count = 0;
	DAPL_HCA	*hca_ptr;

	fp = fopen("/etc/dapl/ibhosts", "r");
	if (fp == NULL) {
		dapl_dbg_log(DAPL_DBG_TYPE_ERR,
		    "fake_ibds: ibhosts not found!\n");
		return (0);
	}
	if (gethostname(localhost, LINE_LEN) != 0) {
		dapl_dbg_log(DAPL_DBG_TYPE_ERR,
		    "fake_ibds: hostname not found!\n");
		return (0);
	}
	while (!feof(fp)) {
		(void) fgets(line_buf, LINE_LEN, fp);
		sscanf(line_buf, "%s %llx %llx", host_buf, &prefix, &guid);
		(void) sprintf(line_buf, "%s-ib%d", localhost, count + 1);
		if (strncmp(line_buf, host_buf, strlen(line_buf)) == 0) {
			guid &= 0xfffffffffffffff0;
			hca_ptr->tavor_idx = hca_idx;
			hca_ptr->node_GUID = guid;
			hca_ptr->port_num = count + 1;
			hca_ptr->partition_key = 0x0000ffff;
			count++;
		}
		if (count >= 2) break;
	}
	(void) fclose(fp);
	return (count);
}

#endif /* IBHOSTS_NAMING */
