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
 * Copyright (c) 2009, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioccom.h>
#include <sys/param.h>
#include <sys/utsname.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

#include <smbsrv/smb_xdr.h>
#include <smbsrv/smbinfo.h>
#include <smbsrv/smb_ioctl.h>
#include <smbsrv/smb_ioctl.h>
#include <smbsrv/libsmb.h>

#define	SMBDRV_DEVICE_PATH		"/devices/pseudo/smbsrv@0:smbsrv"
#define	SMB_IOC_DATA_SIZE		(256 * 1024)

static int smb_kmod_ioctl(int, smb_ioc_header_t *, uint32_t);


int	smbdrv_fd = -1;

int
smb_kmod_bind(void)
{
	if (smbdrv_fd != -1)
		(void) close(smbdrv_fd);

	if ((smbdrv_fd = open(SMBDRV_DEVICE_PATH, 0)) < 0) {
		smbdrv_fd = -1;
		return (errno);
	}

	return (0);
}

boolean_t
smb_kmod_isbound(void)
{
	return ((smbdrv_fd == -1) ? B_FALSE : B_TRUE);
}

int
smb_kmod_setcfg(smb_kmod_cfg_t *cfg)
{
	smb_ioc_cfg_t ioc;
	struct utsname name;

	ioc.maxworkers = cfg->skc_maxworkers;
	ioc.maxconnections = cfg->skc_maxconnections;
	ioc.keepalive = cfg->skc_keepalive;
	ioc.restrict_anon = cfg->skc_restrict_anon;
	ioc.enforce_vczero = cfg->skc_enforce_vczero;
	ioc.signing_enable = cfg->skc_signing_enable;
	ioc.signing_required = cfg->skc_signing_required;
	ioc.oplock_enable = cfg->skc_oplock_enable;
	ioc.sync_enable = cfg->skc_sync_enable;
	ioc.secmode = cfg->skc_secmode;
	ioc.ipv6_enable = cfg->skc_ipv6_enable;
	ioc.print_enable = cfg->skc_print_enable;
	ioc.exec_flags = cfg->skc_execflags;
	ioc.version = cfg->skc_version;

	(void) strlcpy(ioc.nbdomain, cfg->skc_nbdomain, sizeof (ioc.nbdomain));
	(void) strlcpy(ioc.hostname, cfg->skc_hostname, sizeof (ioc.hostname));
	(void) strlcpy(ioc.system_comment, cfg->skc_system_comment,
	    sizeof (ioc.system_comment));
	ioc.extsec_enable = cfg->skc_extsec_enable;
	bcopy(cfg->skc_machine_guid, (char *)ioc.machine_guid,
	    sizeof (ioc.machine_guid));

	if (uname(&name) < 0) {
		(void) strlcpy(ioc.native_os, "Solaris",
		    sizeof (ioc.native_os));
		(void) strlcpy(ioc.native_lm, "Solaris LAN Manager",
		    sizeof (ioc.native_lm));
	} else {
		(void) snprintf(ioc.native_os, sizeof (ioc.native_os),
		    "%s %s %s", name.sysname, name.release, name.version);
		(void) snprintf(ioc.native_lm, sizeof (ioc.native_lm),
		    "%s %s LAN Manager", name.sysname, name.release);
	}

	return (smb_kmod_ioctl(SMB_IOC_CONFIG, &ioc.hdr, sizeof (ioc)));
}

int
smb_kmod_setgmtoff(int32_t gmtoff)
{
	smb_ioc_gmt_t ioc;

	ioc.offset = gmtoff;
	return (smb_kmod_ioctl(SMB_IOC_GMTOFF, &ioc.hdr,
	    sizeof (ioc)));
}

int
smb_kmod_start(int opipe, int udoor)
{
	smb_ioc_start_t ioc;

	ioc.opipe = opipe;
	ioc.udoor = udoor;
	return (smb_kmod_ioctl(SMB_IOC_START, &ioc.hdr, sizeof (ioc)));
}

void
smb_kmod_online(void)
{
	smb_ioc_header_t ioc;

	(void) smb_kmod_ioctl(SMB_IOC_ONLINE, &ioc, sizeof (ioc));
}

void
smb_kmod_stop(void)
{
	smb_ioc_header_t ioc;

	(void) smb_kmod_ioctl(SMB_IOC_STOP, &ioc, sizeof (ioc));
}

int
smb_kmod_event_notify(uint32_t txid)
{
	smb_ioc_event_t ioc;

	ioc.txid = txid;
	return (smb_kmod_ioctl(SMB_IOC_EVENT, &ioc.hdr, sizeof (ioc)));
}

int
smb_kmod_get_open_num(smb_opennum_t *opennum)
{
	smb_ioc_opennum_t ioc;
	int rc;

	bzero(&ioc, sizeof (ioc));
	ioc.qualtype = opennum->qualtype;
	(void) strlcpy(ioc.qualifier, opennum->qualifier, MAXNAMELEN);

	rc = smb_kmod_ioctl(SMB_IOC_NUMOPEN, &ioc.hdr, sizeof (ioc));
	if (rc == 0) {
		opennum->open_users = ioc.open_users;
		opennum->open_trees = ioc.open_trees;
		opennum->open_files = ioc.open_files;
	}

	return (rc);
}

/*
 * Initialization for an smb_kmod_enum request.  If this call succeeds,
 * smb_kmod_enum_fini() must be called later to deallocate resources.
 */
smb_netsvc_t *
smb_kmod_enum_init(smb_svcenum_t *request)
{
	smb_netsvc_t		*ns;
	smb_svcenum_t		*svcenum;
	smb_ioc_svcenum_t	*ioc;
	uint32_t		ioclen;

	if ((ns = calloc(1, sizeof (smb_netsvc_t))) == NULL)
		return (NULL);

	ioclen = sizeof (smb_ioc_svcenum_t) + SMB_IOC_DATA_SIZE;
	if ((ioc = malloc(ioclen)) == NULL) {
		free(ns);
		return (NULL);
	}

	bzero(ioc, ioclen);
	svcenum = &ioc->svcenum;
	svcenum->se_type   = request->se_type;
	svcenum->se_level  = request->se_level;
	svcenum->se_bavail = SMB_IOC_DATA_SIZE;
	svcenum->se_nlimit = request->se_nlimit;
	svcenum->se_nskip = request->se_nskip;
	svcenum->se_buflen = SMB_IOC_DATA_SIZE;
	(void) strlcpy(svcenum->se_qualifier.seq_qualstr,
	    request->se_qualifier.seq_qualstr,
	    sizeof (svcenum->se_qualifier.seq_qualstr));
	svcenum->se_qualifier.seq_mode = request->se_qualifier.seq_mode;

	list_create(&ns->ns_list, sizeof (smb_netsvcitem_t),
	    offsetof(smb_netsvcitem_t, nsi_lnd));

	ns->ns_ioc = ioc;
	ns->ns_ioclen = ioclen;
	return (ns);
}

/*
 * Cleanup resources allocated via smb_kmod_enum_init and smb_kmod_enum.
 */
void
smb_kmod_enum_fini(smb_netsvc_t *ns)
{
	list_t			*lst;
	smb_netsvcitem_t	*item;
	smb_netuserinfo_t	*user;
	smb_netconnectinfo_t	*tree;
	smb_netfileinfo_t	*ofile;
	smb_share_t		*share;
	uint32_t		se_type;

	if (ns == NULL)
		return;

	lst = &ns->ns_list;
	se_type = ns->ns_ioc->svcenum.se_type;

	while ((item = list_head(lst)) != NULL) {
		list_remove(lst, item);

		switch (se_type) {
		case SMB_SVCENUM_TYPE_USER:
			user = &item->nsi_un.nsi_user;
			free(user->ui_domain);
			free(user->ui_account);
			free(user->ui_workstation);
			free(user->ui_posix_name);
			break;
		case SMB_SVCENUM_TYPE_TREE:
			tree = &item->nsi_un.nsi_tree;
			free(tree->ci_username);
			free(tree->ci_share);
			break;
		case SMB_SVCENUM_TYPE_FILE:
			ofile = &item->nsi_un.nsi_ofile;
			free(ofile->fi_path);
			free(ofile->fi_username);
			break;
		case SMB_SVCENUM_TYPE_SHARE:
			share = &item->nsi_un.nsi_share;
			smb_share_free(share);
			break;
		default:
			break;
		}
	}

	list_destroy(&ns->ns_list);
	free(ns->ns_items);
	free(ns->ns_ioc);
	free(ns);
}

/*
 * Enumerate users, connections or files.
 */
int
smb_kmod_enum(smb_netsvc_t *ns)
{
	smb_ioc_svcenum_t	*ioc;
	uint32_t		ioclen;
	smb_svcenum_t		*svcenum;
	smb_netsvcitem_t	*items;
	smb_netuserinfo_t	*user;
	smb_netconnectinfo_t	*tree;
	smb_netfileinfo_t	*ofile;
	smb_share_t		*share;
	uint8_t			*data;
	uint32_t		len;
	uint32_t		se_type;
	uint_t			nbytes;
	int			i;
	int			rc;

	ioc = ns->ns_ioc;
	ioclen = ns->ns_ioclen;
	rc = smb_kmod_ioctl(SMB_IOC_SVCENUM, &ioc->hdr, ioclen);
	if (rc != 0)
		return (rc);

	svcenum = &ioc->svcenum;
	items = calloc(svcenum->se_nitems, sizeof (smb_netsvcitem_t));
	if (items == NULL)
		return (ENOMEM);

	ns->ns_items = items;
	se_type = ns->ns_ioc->svcenum.se_type;
	data = svcenum->se_buf;
	len = svcenum->se_bused;

	for (i = 0; i < svcenum->se_nitems; ++i) {
		switch (se_type) {
		case SMB_SVCENUM_TYPE_USER:
			user = &items->nsi_un.nsi_user;
			rc = smb_netuserinfo_decode(user, data, len, &nbytes);
			break;
		case SMB_SVCENUM_TYPE_TREE:
			tree = &items->nsi_un.nsi_tree;
			rc = smb_netconnectinfo_decode(tree, data, len,
			    &nbytes);
			break;
		case SMB_SVCENUM_TYPE_FILE:
			ofile = &items->nsi_un.nsi_ofile;
			rc = smb_netfileinfo_decode(ofile, data, len, &nbytes);
			break;
		case SMB_SVCENUM_TYPE_SHARE:
			share = &items->nsi_un.nsi_share;
			rc = smb_share_decode(share, data, len, &nbytes);
			break;
		default:
			rc = -1;
			break;
		}

		if (rc != 0)
			return (EINVAL);

		list_insert_tail(&ns->ns_list, items);

		++items;
		data += nbytes;
		len -= nbytes;
	}

	return (0);
}

/*
 * A NULL pointer is a wildcard indicator, which we pass on
 * as an empty string (by virtue of the bzero).
 */
int
smb_kmod_session_close(const char *client, const char *username)
{
	smb_ioc_session_t ioc;
	int rc;

	bzero(&ioc, sizeof (ioc));

	if (client != NULL)
		(void) strlcpy(ioc.client, client, MAXNAMELEN);
	if (username != NULL)
		(void) strlcpy(ioc.username, username, MAXNAMELEN);

	rc = smb_kmod_ioctl(SMB_IOC_SESSION_CLOSE, &ioc.hdr, sizeof (ioc));
	return (rc);
}

int
smb_kmod_file_close(uint32_t uniqid)
{
	smb_ioc_fileid_t ioc;
	int rc;

	bzero(&ioc, sizeof (ioc));
	ioc.uniqid = uniqid;

	rc = smb_kmod_ioctl(SMB_IOC_FILE_CLOSE, &ioc.hdr, sizeof (ioc));
	return (rc);
}

void
smb_kmod_unbind(void)
{
	if (smbdrv_fd != -1) {
		(void) close(smbdrv_fd);
		smbdrv_fd = -1;
	}
}

/*
 * Lookup a share.
 *
 * If the share-info pointer is non-null, the share data is returned in si.
 *
 * If the share-info is null, the share is still looked up and the result
 * indicates whether or not a share with the specified name exists.
 */
int
smb_kmod_share_lookup(const char *key, int keytype, smb_share_t *si)
{
	smb_ioc_share_t *ioc;
	uint32_t ioclen;
	uint_t nbytes;
	int cmd;
	int rc;

	switch (keytype) {
	case SMB_SHRKEY_NAME:
		cmd = SMB_IOC_GETSHARE;
		break;
	case SMB_SHRKEY_WINPATH:
		cmd = SMB_IOC_CHKSHARE;
		break;
	default:
		return (EINVAL);
	}

	ioclen = sizeof (smb_ioc_share_t) + SMB_SHRBUF_SIZE;

	if ((ioc = malloc(ioclen)) == NULL)
		return (ENOMEM);

	ioc->shrlen = SMB_SHRBUF_SIZE;
	(void) strlcpy(ioc->shr, key, ioc->shrlen);
	rc = smb_kmod_ioctl(cmd, &ioc->hdr, ioclen);
	if ((rc == 0) && (si != NULL))
		rc = smb_share_decode(si, (uint8_t *)ioc->shr,
		    ioc->shrlen, &nbytes);

	free(ioc);
	return (rc);
}

uint32_t
smb_kmod_share_count(uint32_t qualifier)
{
	smb_ioc_sharenum_t ioc;
	int rc;

	bzero(&ioc, sizeof (ioc));
	ioc.qualifier = qualifier;
	rc = smb_kmod_ioctl(SMB_IOC_NUMSHARE, &ioc.hdr, sizeof (ioc));
	return ((rc == 0) ? ioc.num : 0);
}

static int
smb_kmod_ioctl(int cmd, smb_ioc_header_t *ioc, uint32_t len)
{
	int rc = EINVAL;

	ioc->version = SMB_IOC_VERSION;
	ioc->cmd = cmd;
	ioc->len = len;
	ioc->crc = 0;
	ioc->crc = smb_crc_gen((uint8_t *)ioc, sizeof (smb_ioc_header_t));

	if (smbdrv_fd != -1) {
		if (ioctl(smbdrv_fd, cmd, ioc) < 0)
			rc = errno;
		else
			rc = 0;
	}
	return (rc);
}
