/*
 * Copyright (c) 2000, Boris Popov
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *    This product includes software developed by Boris Popov.
 * 4. Neither the name of the author nor the names of any co-contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * Copyright (c) 2008, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#include <sys/param.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <sys/mount.h>
#include <sys/types.h>
#include <sys/file.h>
#include <fcntl.h>
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include <libscf.h>
#include <pwd.h>
#include <grp.h>
#include <syslog.h>
#include <unistd.h>
#include <libintl.h>
#include <smb/ntdsutil.h>
#include <smbsrv/libsmb.h>
#include <smb/smb.h>
#include <netsmb/smb_lib.h>

#include "private.h"

static boolean_t smb_fh_getbool(const char *, boolean_t);
static int smb_fh_getint(const char *, int64_t *);
static smb_scfhandle_t *smb_fh_scf_init(void);
static void smb_fh_scf_fini(smb_scfhandle_t *);
static int smb_fh_scf_getbool(smb_scfhandle_t *, const char *, uint8_t *);
static int smb_fh_scf_getint(smb_scfhandle_t *, const char *, int64_t *);

int
smb_fh_close(int fd)
{
	int err = 0;

	if (ioctl(fd, SMBIOC_CLOSEFH, NULL) == -1)
		err = errno;

	(void) close(fd);
	return (err);
}

static int
smb_fh_ntcreate(struct smb_ctx *ctx, char *path,
	int req_acc, int efattr, int share_acc,
	int open_disp, int create_opts)
{
	smbioc_ntcreate_t ioc;
	int err, nmlen, new_fd;
	int32_t from_fd;

	nmlen = strlen(path);
	if (nmlen >= SMBIOC_MAX_NAME)
		return (EINVAL);

	/*
	 * Represent this SMB-level open as a new open device handle.
	 * Get fd then duplicate the driver session and tree bindings.
	 */
	new_fd = smb_open_driver();
	if (new_fd < 0)
		return (errno);
	from_fd = ctx->ct_dev_fd;
	if (ioctl(new_fd, SMBIOC_DUP_DEV, &from_fd) == -1) {
		err = errno;
		goto errout;
	}

	/*
	 * Do the SMB-level open with the new dev handle.
	 */
	bzero(&ioc, sizeof (ioc));
	strlcpy(ioc.ioc_name, path, SMBIOC_MAX_NAME);
	ioc.ioc_req_acc = req_acc;
	ioc.ioc_efattr = efattr;
	ioc.ioc_share_acc = share_acc;
	ioc.ioc_open_disp = open_disp;
	ioc.ioc_creat_opts = create_opts;
	if (ioctl(new_fd, SMBIOC_NTCREATE, &ioc) == -1) {
		err = errno;
		goto errout;
	}

	return (new_fd);

errout:
	(void) close(new_fd);
	errno = err;
	return (-1);
}

/*
 * Wrapper for smb_fh_ntcreate.
 */
int
smb_fh_open(const char *path, int oflag)
{
	boolean_t extsec_enabled = B_FALSE;
	char ntpath[MAXPATHLEN];
	struct smb_ctx *ctx = NULL;
	int64_t dclevel = DS_BEHAVIOR_WIN2000;
	int error, mode, open_disp, req_acc, share_acc;
	int fd = -1;
	char *p;

	error = smb_ctx_alloc(&ctx);
	if (error)
		goto out;

	error = smb_ctx_parseunc(ctx, path,
	    SMBL_SHARE, SMBL_SHARE, USE_WILDCARD, NULL);
	if (error)
		goto out;

	if (ctx->ct_rpath == NULL) {
		error = EINVAL;
		goto out;
	}

	(void) strlcpy(ntpath, ctx->ct_rpath, MAXPATHLEN);
	for (p = ntpath; *p; p++)
		if (*p == '/')
			*p = '\\';

	error = smb_ctx_resolve(ctx);
	if (error)
		goto out;

	/*
	 * SessionSetupX fails for the machine trust account with
	 * Extended Security enabled when talking to a Windows 2000
	 * domain controller.
	 *
	 * The workaround is to disable extended security.
	 */
	extsec_enabled = smb_fh_getbool("client_extsec", B_TRUE);

	if (extsec_enabled) {
		if (smb_fh_getint("dc_level", &dclevel) != 0) {
			syslog(LOG_DEBUG, "smb_fh_open: smb_fh_getint failed");
			dclevel = DS_BEHAVIOR_WIN2000;
		}
		syslog(LOG_DEBUG, "smb_fh_open: dclevel=%d", (int)dclevel);
	}

	if (!extsec_enabled || dclevel < DS_BEHAVIOR_WIN2003)
		ctx->ct_vopt &= ~SMBVOPT_EXT_SEC;

	/*
	 * Temporary: Disable Kerberos authentication when being
	 * used as a service until the need for a manual kinit
	 * has been resolved.
	 */
	ctx->ct_authflags &= ~SMB_AT_KRB5;
	if (ctx->ct_authflags == 0) {
		syslog(LOG_DEBUG, "smb_fh_open: no authentication types");
		error = ENOTSUP;
		goto out;
	}

	error = smb_ctx_get_ssn(ctx);
	if (error)
		goto out;

	error = smb_ctx_get_tree(ctx);
	if (error)
		goto out;

	/*
	 * Map O_RDONLY, O_WRONLY, O_RDWR to FREAD, FWRITE
	 */
	mode = (oflag & 3) + 1;

	/*
	 * Compute requested access, share access.
	 */
	req_acc = (
	    READ_CONTROL |
	    SYNCHRONIZE);
	share_acc = FILE_SHARE_NONE;
	if (mode & FREAD) {
		req_acc |= (
		    FILE_READ_DATA |
		    FILE_READ_EA |
		    FILE_READ_ATTRIBUTES);
		share_acc |= FILE_SHARE_READ;
	}
	if (mode & FWRITE) {
		req_acc |= (
		    FILE_WRITE_DATA |
		    FILE_APPEND_DATA |
		    FILE_WRITE_EA |
		    FILE_WRITE_ATTRIBUTES);
		share_acc |= FILE_SHARE_WRITE;
	}

	/*
	 * Compute open disposition
	 */
	if (oflag & FCREAT) {
		/* Create if necessary */
		if (oflag & FEXCL)
			open_disp = FILE_CREATE;
		else if (oflag & FTRUNC)
			open_disp = FILE_OVERWRITE_IF;
		else
			open_disp = FILE_OPEN_IF;
	} else {
		/* Don't create */
		if (oflag & FTRUNC)
			open_disp = FILE_OVERWRITE;
		else
			open_disp = FILE_OPEN;
	}

	fd = smb_fh_ntcreate(ctx, ntpath, req_acc,
	    FILE_ATTRIBUTE_NORMAL, share_acc, open_disp,
	    FILE_NON_DIRECTORY_FILE);

out:
	smb_ctx_free(ctx);
	if (error)
		errno = error;

	return (fd);
}

int
smb_fh_read(int fd, void *buf, size_t count, off_t offset)
{
	struct smbioc_rw rwrq;

	bzero(&rwrq, sizeof (rwrq));
	rwrq.ioc_fh = -1;
	rwrq.ioc_base = buf;
	rwrq.ioc_cnt = count;
	rwrq.ioc_offset = offset;

	if (ioctl(fd, SMBIOC_READ, &rwrq) == -1)
		return (-1);

	return (rwrq.ioc_cnt);
}

int
smb_fh_write(int fd, const void *buf, size_t count, off_t offset)
{
	struct smbioc_rw rwrq;

	bzero(&rwrq, sizeof (rwrq));
	rwrq.ioc_fh = -1;
	rwrq.ioc_base = (void *)buf;
	rwrq.ioc_cnt = count;
	rwrq.ioc_offset = offset;

	if (ioctl(fd, SMBIOC_WRITE, &rwrq) == -1)
		return (-1);

	return (rwrq.ioc_cnt);
}

/*
 * Do a TRANS_TRANSACT_NMPIPE, which is basically just a
 * pipe write and pipe read, all in one round trip.
 *
 * tdlen and tdata describe the data to send/transmit.
 * rdlen and rdata on input describe the receive buffer.
 *
 * On output *rdlen is the received length.
 */
int
smb_fh_xactnp(int fd, int tdlen, const char *tdata,
	int *rdlen, char *rdata, int *more)
{
	int		err, rparamcnt;
	uint16_t	setup[2];

	setup[0] = TRANS_TRANSACT_NMPIPE;
	setup[1] = 0xFFFF; /* driver replaces this */
	rparamcnt = 0;

	err = smb_t2_request(fd, 2, setup, "\\PIPE\\",
	    0, NULL,	/* TX paramcnt, params */
	    tdlen, (void *)tdata,
	    &rparamcnt, NULL,	/* no RX params */
	    rdlen, rdata, more);

	if (err)
		*rdlen = 0;

	return (err);
}

/*
 * Query the value of a boolean property.
 *
 * If the SMF query fails, return the specified default.
 * Otherwise, return the value of the boolean property.
 */
static boolean_t
smb_fh_getbool(const char *propname, boolean_t dflt)
{
	smb_scfhandle_t	*hd;
	uint8_t		vbool;
	int		rc;

	if ((hd = smb_fh_scf_init()) == NULL)
		return (dflt);

	rc = smb_fh_scf_getbool(hd, propname, &vbool);
	smb_fh_scf_fini(hd);

	if (rc != 0)
		return (dflt);

	return (vbool == 1);
}

/*
 * Query the value of an integer property.
 *
 * If the SMF query fails, return the specified default.
 * Otherwise, return the value of the boolean property.
 */
static int
smb_fh_getint(const char *propname, int64_t *vint)
{
	smb_scfhandle_t	*hd;
	int		rc;

	if ((hd = smb_fh_scf_init()) == NULL)
		return (-1);

	rc = smb_fh_scf_getint(hd, propname, vint);
	smb_fh_scf_fini(hd);
	return (rc);
}


/*
 * Allocate and initialize a handle to query SMF properties.
 */
static smb_scfhandle_t *
smb_fh_scf_init(void)
{
	smb_scfhandle_t *hd;
	int		rc;

	if ((hd = calloc(1, sizeof (smb_scfhandle_t))) == NULL)
		return (NULL);

	hd->scf_state = SCH_STATE_INITIALIZING;

	if ((hd->scf_handle = scf_handle_create(SCF_VERSION)) == NULL) {
		free(hd);
		return (NULL);
	}

	if (scf_handle_bind(hd->scf_handle) != SCF_SUCCESS)
		goto scf_init_error;

	if ((hd->scf_scope = scf_scope_create(hd->scf_handle)) == NULL)
		goto scf_init_error;

	rc = scf_handle_get_local_scope(hd->scf_handle, hd->scf_scope);
	if (rc != SCF_SUCCESS)
		goto scf_init_error;

	if ((hd->scf_service = scf_service_create(hd->scf_handle)) == NULL)
		goto scf_init_error;

	rc = scf_scope_get_service(hd->scf_scope, SMB_FMRI_PREFIX,
	    hd->scf_service);
	if (rc != SCF_SUCCESS)
		goto scf_init_error;

	if ((hd->scf_pg = scf_pg_create(hd->scf_handle)) == NULL)
		goto scf_init_error;

	rc = scf_service_get_pg(hd->scf_service, SMB_PG_NAME, hd->scf_pg);
	if (rc != SCF_SUCCESS)
		goto scf_init_error;

	hd->scf_state = SCH_STATE_INIT;
	return (hd);

scf_init_error:
	smb_fh_scf_fini(hd);
	return (NULL);
}

/*
 * Destroy and deallocate an SMFhandle.
 */
static void
smb_fh_scf_fini(smb_scfhandle_t *hd)
{
	if (hd == NULL)
		return;

	scf_iter_destroy(hd->scf_pg_iter);
	scf_iter_destroy(hd->scf_inst_iter);
	scf_scope_destroy(hd->scf_scope);
	scf_instance_destroy(hd->scf_instance);
	scf_service_destroy(hd->scf_service);
	scf_pg_destroy(hd->scf_pg);

	hd->scf_state = SCH_STATE_UNINIT;

	(void) scf_handle_unbind(hd->scf_handle);
	scf_handle_destroy(hd->scf_handle);
	free(hd);
}

/*
 * Query the value of a boolean property.
 */
static int
smb_fh_scf_getbool(smb_scfhandle_t *hd, const char *propname, uint8_t *vbool)
{
	scf_value_t	*value = NULL;
	scf_property_t	*prop = NULL;
	int		rc = -1;

	if (hd == NULL)
		return (-1);

	value = scf_value_create(hd->scf_handle);
	prop = scf_property_create(hd->scf_handle);

	if (prop == NULL || value == NULL)
		return (-1);

	if (scf_pg_get_property(hd->scf_pg, propname, prop) == 0) {
		if (scf_property_get_value(prop, value) == 0)
			if (scf_value_get_boolean(value, vbool) == 0)
				rc = 0;
	}

	scf_value_destroy(value);
	scf_property_destroy(prop);
	return (rc);
}

/*
 * Query the value of a boolean property.
 */
static int
smb_fh_scf_getint(smb_scfhandle_t *hd, const char *propname, int64_t *vint)
{
	scf_value_t	*value = NULL;
	scf_property_t	*prop = NULL;
	int		rc = -1;

	if (hd == NULL)
		return (-1);

	value = scf_value_create(hd->scf_handle);
	prop = scf_property_create(hd->scf_handle);

	if (prop == NULL || value == NULL)
		return (-1);

	if (scf_pg_get_property(hd->scf_pg, propname, prop) == 0) {
		if (scf_property_get_value(prop, value) == 0)
			if (scf_value_get_integer(value, vint) == 0)
				rc = 0;
	}

	scf_value_destroy(value);
	scf_property_destroy(prop);
	return (rc);
}
