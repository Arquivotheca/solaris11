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
 * Copyright (c) 2007, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#include <assert.h>
#include <syslog.h>
#include <door.h>
#include <fcntl.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <sys/mman.h>
#include <smbsrv/libsmb.h>
#include <smb/wintypes.h>
#include <smbsrv/smb_door.h>

static int smb_door_call(uint32_t, void *, xdrproc_t, void *, xdrproc_t);
static int smb_door_call_private(int, smb_doorarg_t *);
static int smb_door_encode(smb_doorarg_t *, uint32_t);
static int smb_door_decode(smb_doorarg_t *);
static void smb_door_sethdr(smb_doorhdr_t *, uint32_t, uint32_t);
static boolean_t smb_door_chkhdr(smb_doorarg_t *);
static void smb_door_free(door_arg_t *arg);

/*
 * Given a SID, make a door call to get  the associated name.
 *
 * Returns 0 if the door call is successful, otherwise -1.
 *
 * If 0 is returned, the lookup result will be available in a_status.
 * NT_STATUS_SUCCESS		The SID was mapped to a name.
 * NT_STATUS_NONE_MAPPED	The SID could not be mapped to a name.
 */
int
smb_lookup_sid(const char *sid, lsa_account_t *acct)
{
	int	rc;

	assert((sid != NULL) && (acct != NULL));

	bzero(acct, sizeof (lsa_account_t));
	(void) strlcpy(acct->a_sid, sid, SMB_SID_STRSZ);

	rc = smb_door_call(SMB_DR_LOOKUP_SID, acct, lsa_account_xdr,
	    acct, lsa_account_xdr);

	if (rc != 0)
		syslog(LOG_DEBUG, "smb_lookup_sid: %m");
	return (rc);
}

/*
 * Given a name, make a door call to get the associated SID.
 *
 * Returns 0 if the door call is successful, otherwise -1.
 *
 * If 0 is returned, the lookup result will be available in a_status.
 * NT_STATUS_SUCCESS		The name was mapped to a SID.
 * NT_STATUS_NONE_MAPPED	The name could not be mapped to a SID.
 */
int
smb_lookup_name(const char *name, sid_type_t sidtype, lsa_account_t *acct)
{
	char		tmp[MAXNAMELEN];
	char		*dp = NULL;
	char		*np = NULL;
	int		rc;

	assert((name != NULL) && (acct != NULL));

	(void) strlcpy(tmp, name, MAXNAMELEN);
	smb_name_parse(tmp, &np, &dp);

	bzero(acct, sizeof (lsa_account_t));
	acct->a_sidtype = sidtype;

	if (dp != NULL && np != NULL) {
		(void) strlcpy(acct->a_domain, dp, MAXNAMELEN);
		(void) strlcpy(acct->a_name, np, MAXNAMELEN);
	} else {
		(void) strlcpy(acct->a_name, name, MAXNAMELEN);
	}

	rc = smb_door_call(SMB_DR_LOOKUP_NAME, acct, lsa_account_xdr,
	    acct, lsa_account_xdr);

	if (rc != 0)
		syslog(LOG_DEBUG, "smb_lookup_name: %m");
	return (rc);
}

uint32_t
smb_discover_dc(smb_joininfo_t *jdi)
{
	uint32_t	status;
	int		rc;

	if (jdi == NULL)
		return (NT_STATUS_INVALID_PARAMETER);

	rc = smb_door_call(SMB_DR_LOCATE_DC, jdi, smb_joininfo_xdr,
	    &status, xdr_uint32_t);

	if (rc != 0) {
		syslog(LOG_DEBUG, "smb_discover_dc: %m");
		status = NT_STATUS_INTERNAL_ERROR;
	}

	return (status);
}

int
smb_join(smb_joininfo_t *jdi, uint32_t *status)
{
	int		rc;

	if (jdi == NULL)
		return (EINVAL);

	rc = smb_door_call(SMB_DR_JOIN, jdi, smb_joininfo_xdr,
	    status, xdr_uint32_t);

	if (rc != 0) {
		syslog(LOG_ERR, "smb_join: %m");
		rc = errno;
	}
	return (rc);
}

/*
 * Get fully-qualified name of the Domain Controller in the joined resource
 * domain.
 *
 * Returns NT status codes.
 */
uint32_t
smb_get_dcinfo(char *namebuf, uint32_t namebuflen)
{
	smb_string_t	dcname;
	int		rc;

	assert((namebuf != NULL) && (namebuflen != 0));
	*namebuf = '\0';
	bzero(&dcname, sizeof (smb_string_t));

	rc = smb_door_call(SMB_DR_GET_DCINFO, NULL, NULL,
	    &dcname, smb_string_xdr);

	if (rc != 0) {
		syslog(LOG_DEBUG, "smb_get_dcinfo: %m");
		if (dcname.buf)
			xdr_free(smb_string_xdr, (char *)&dcname);
		return (NT_STATUS_INTERNAL_ERROR);
	}

	if (dcname.buf) {
		(void) strlcpy(namebuf, dcname.buf, namebuflen);
		xdr_free(smb_string_xdr, (char *)&dcname);
	}

	return (NT_STATUS_SUCCESS);
}

uint32_t
smb_get_fqdn(char *namebuf, uint32_t namebuflen)
{
	smb_string_t	fqdn;
	int		rc;

	assert((namebuf != NULL) && (namebuflen != 0));
	*namebuf = '\0';
	bzero(&fqdn, sizeof (smb_string_t));

	rc = smb_door_call(SMB_DR_GET_FQDN, NULL, NULL,
	    &fqdn, smb_string_xdr);

	if (rc != 0) {
		syslog(LOG_DEBUG, "smb_get_fqdn: %m");
		if (fqdn.buf)
			xdr_free(smb_string_xdr, (char *)&fqdn);
		return (NT_STATUS_INTERNAL_ERROR);
	}

	if (fqdn.buf) {
		(void) strlcpy(namebuf, fqdn.buf, namebuflen);
		xdr_free(smb_string_xdr, (char *)&fqdn);
	}

	return (NT_STATUS_SUCCESS);
}

bool_t
smb_joininfo_xdr(XDR *xdrs, smb_joininfo_t *objp)
{
	if (!xdr_vector(xdrs, (char *)objp->domain_name, MAXHOSTNAMELEN,
	    sizeof (char), (xdrproc_t)xdr_char))
		return (FALSE);

	if (!xdr_vector(xdrs, (char *)objp->domain_username,
	    SMB_USERNAME_MAXLEN + 1, sizeof (char), (xdrproc_t)xdr_char))
		return (FALSE);

	if (!xdr_vector(xdrs, (char *)objp->domain_passwd,
	    SMB_PASSWD_MAXLEN + 1, sizeof (char), (xdrproc_t)xdr_char))
		return (FALSE);

	if (!xdr_vector(xdrs, (char *)objp->dc, MAXHOSTNAMELEN,
	    sizeof (char), (xdrproc_t)xdr_char))
		return (FALSE);

	if (!xdr_uint32_t(xdrs, &objp->mode))
		return (FALSE);

	return (TRUE);
}

/*
 * Parameters:
 *   fqdn (input) - fully-qualified domain name
 *   buf (output) - fully-qualified hostname of the AD server found
 *                  by this function.
 *   buflen (input) - length of the 'buf'
 *
 * Return:
 *   B_TRUE if an AD server is found. Otherwise, returns B_FALSE;
 *
 * The buffer passed in should be big enough to hold a fully-qualified
 * hostname (MAXHOSTNAMELEN); otherwise, a truncated string will be
 * returned. On error, an empty string will be returned.
 */
boolean_t
smb_find_ads_server(char *fqdn, char *buf, int buflen)
{
	smb_string_t	server;
	smb_string_t	domain;
	boolean_t	found = B_FALSE;
	int		rc;

	if (fqdn == NULL || buf == NULL) {
		if (buf)
			*buf = '\0';
		return (B_FALSE);
	}

	bzero(&server, sizeof (smb_string_t));
	*buf = '\0';

	domain.buf = fqdn;

	rc = smb_door_call(SMB_DR_ADS_FIND_HOST, &domain, smb_string_xdr,
	    &server, smb_string_xdr);

	if (rc != 0)
		syslog(LOG_DEBUG, "smb_find_ads_server: %m");

	if (server.buf != NULL) {
		if (*server.buf != '\0') {
			(void) strlcpy(buf, server.buf, buflen);
			found = B_TRUE;
		}

		xdr_free(smb_string_xdr, (char *)&server);
	}

	return (found);
}

/*
 * publish all smb administrative shares for this mountpoint
 *
 * Return values:
 *
 *   ERROR_SUCCESS
 *   NT_STATUS_INVALID_PARAMETER
 *   NT_STATUS_INTERNAL_ERROR
 */
uint32_t
smb_share_publish_admin(const char *mountpoint)
{
	smb_string_t	mntpnt;
	uint32_t	status;
	int		rc;

	if (mountpoint == NULL || *mountpoint == '\0')
		return (NT_STATUS_INVALID_PARAMETER);

	mntpnt.buf = (char *)mountpoint;

	rc = smb_door_call(SMB_DR_SHR_PUBLISH_ADMIN, &mntpnt, smb_string_xdr,
	    &status, xdr_uint32_t);

	if (rc != 0) {
		syslog(LOG_DEBUG, "smb_share_publish_admin: %m");
		status = NT_STATUS_INTERNAL_ERROR;
	}

	return (status);
}

/*
 * Get a list of domains and the domain controller of the primary domain.
 */
int
smb_get_domains_info(smb_domains_info_t *domains_info)
{
	int	rc;

	rc = smb_door_call(SMB_DR_GET_DOMAINS_INFO, NULL, NULL,
	    domains_info, smb_domains_info_xdr);

	return (rc);
}

/*
 * After a successful door call the local door_arg->data_ptr is assigned
 * to the caller's arg->rbuf so that arg has references to both input and
 * response buffers, which is required by smb_door_free.
 *
 * On success, the object referenced by rsp_data will have been populated
 * by passing rbuf through the rsp_xdr function.
 */
static int
smb_door_call(uint32_t cmd, void *req_data, xdrproc_t req_xdr,
    void *rsp_data, xdrproc_t rsp_xdr)
{
	smb_doorarg_t	da;
	int		fd;
	int		rc;

	bzero(&da, sizeof (smb_doorarg_t));
	da.da_opcode = cmd;
	da.da_opname = smb_doorhdr_opname(cmd);
	da.da_req_xdr = req_xdr;
	da.da_rsp_xdr = rsp_xdr;
	da.da_req_data = req_data;
	da.da_rsp_data = rsp_data;

	if ((req_data == NULL && req_xdr != NULL) ||
	    (rsp_data == NULL && rsp_xdr != NULL)) {
		errno = EINVAL;
		syslog(LOG_DEBUG, "smb_door_call[%s]: %m", da.da_opname);
		return (-1);
	}

	if ((fd = open(SMBD_DOOR_NAME, O_RDONLY)) < 0) {
		syslog(LOG_DEBUG, "smb_door_call[%s]: %m", da.da_opname);
		return (-1);
	}

	if (smb_door_encode(&da, cmd) != 0) {
		syslog(LOG_DEBUG, "smb_door_call[%s]: %m", da.da_opname);
		(void) close(fd);
		return (-1);
	}

	if (smb_door_call_private(fd, &da) != 0) {
		syslog(LOG_DEBUG, "smb_door_call[%s]: %m", da.da_opname);
		smb_door_free(&da.da_arg);
		(void) close(fd);
		return (-1);
	}

	if ((rc = smb_door_decode(&da)) != 0)
		syslog(LOG_DEBUG, "smb_door_call[%s]: %m", da.da_opname);
	smb_door_free(&da.da_arg);
	(void) close(fd);

	return (rc);
}

/*
 * We use a copy of the door arg because doorfs may change data_ptr
 * and we want to detect that when freeing the door buffers.  After
 * this call, response data must be referenced via rbuf and rsize.
 */
static int
smb_door_call_private(int fd, smb_doorarg_t *da)
{
	door_arg_t door_arg;
	int rc;
	int i;

	bcopy(&da->da_arg, &door_arg, sizeof (door_arg_t));

	for (i = 0; i < SMB_DOOR_CALL_RETRIES; ++i) {
		errno = 0;

		if ((rc = door_call(fd, &door_arg)) == 0)
			break;

		if (errno != EAGAIN && errno != EINTR)
			return (-1);
	}

	if (rc != 0 || door_arg.data_size == 0 || door_arg.rsize == 0) {
		if (errno == 0)
			errno = EIO;
		return (-1);
	}

	da->da_arg.rbuf = door_arg.data_ptr;
	da->da_arg.rsize = door_arg.rsize;
	return (rc);
}

static int
smb_door_encode(smb_doorarg_t *da, uint32_t cmd)
{
	XDR		xdrs;
	char		*buf;
	uint32_t	buflen;

	buflen = xdr_sizeof(smb_doorhdr_xdr, &da->da_req_hdr);
	if (da->da_req_xdr != NULL)
		buflen += xdr_sizeof(da->da_req_xdr, da->da_req_data);

	smb_door_sethdr(&da->da_req_hdr, cmd, buflen);

	if ((buf = malloc(buflen)) == NULL)
		return (-1);

	xdrmem_create(&xdrs, buf, buflen, XDR_ENCODE);

	if (!smb_doorhdr_xdr(&xdrs, &da->da_req_hdr)) {
		errno = EPROTO;
		free(buf);
		xdr_destroy(&xdrs);
		return (-1);
	}

	if (da->da_req_xdr != NULL) {
		if (!da->da_req_xdr(&xdrs, da->da_req_data)) {
			errno = EPROTO;
			free(buf);
			xdr_destroy(&xdrs);
			return (-1);
		}
	}

	da->da_arg.data_ptr = buf;
	da->da_arg.data_size = buflen;
	da->da_arg.desc_ptr = NULL;
	da->da_arg.desc_num = 0;
	da->da_arg.rbuf = buf;
	da->da_arg.rsize = buflen;

	xdr_destroy(&xdrs);
	return (0);
}

/*
 * Decode the response in rbuf and rsize.
 */
static int
smb_door_decode(smb_doorarg_t *da)
{
	XDR		xdrs;
	char		*rbuf = da->da_arg.rbuf;
	uint32_t	rsize = da->da_arg.rsize;

	if (rbuf == NULL || rsize == 0) {
		errno = EINVAL;
		return (-1);
	}

	xdrmem_create(&xdrs, rbuf, rsize, XDR_DECODE);

	if (!smb_doorhdr_xdr(&xdrs, &da->da_rsp_hdr)) {
		xdr_free(smb_doorhdr_xdr, (char *)&da->da_rsp_hdr);
		errno = EPROTO;
		xdr_destroy(&xdrs);
		return (-1);
	}

	if (!smb_door_chkhdr(da)) {
		xdr_free(smb_doorhdr_xdr, (char *)&da->da_rsp_hdr);
		errno = EPROTO;
		xdr_destroy(&xdrs);
		return (-1);
	}

	if (da->da_rsp_xdr != NULL) {
		if (!da->da_rsp_xdr(&xdrs, da->da_rsp_data)) {
			xdr_free(smb_doorhdr_xdr, (char *)&da->da_rsp_hdr);
			xdr_free(da->da_rsp_xdr, (char *)da->da_rsp_data);
			errno = EPROTO;
			xdr_destroy(&xdrs);
			return (-1);
		}
	}

	xdr_destroy(&xdrs);
	return (0);
}

static void
smb_door_sethdr(smb_doorhdr_t *hdr, uint32_t cmd, uint32_t datalen)
{
	bzero(hdr, sizeof (smb_doorhdr_t));
	hdr->dh_magic = SMB_DOOR_HDR_MAGIC;
	hdr->dh_flags = SMB_DF_USERSPACE;
	hdr->dh_op = cmd;
	hdr->dh_txid = smb_get_txid();
	hdr->dh_datalen = datalen;
	hdr->dh_door_rc = SMB_DOP_NOT_CALLED;
}

static boolean_t
smb_door_chkhdr(smb_doorarg_t *da)
{
	smb_doorhdr_t *hdr = &da->da_rsp_hdr;

	if ((hdr->dh_magic != SMB_DOOR_HDR_MAGIC) ||
	    (hdr->dh_op != da->da_req_hdr.dh_op) ||
	    (hdr->dh_txid != da->da_req_hdr.dh_txid)) {
		syslog(LOG_DEBUG, "smb_door_chkhdr[%s]: invalid header",
		    da->da_opname);
		return (B_FALSE);
	}

	if (hdr->dh_door_rc != SMB_DOP_SUCCESS) {
		syslog(LOG_DEBUG, "smb_door_chkhdr[%s]: call status=%d",
		    da->da_opname, hdr->dh_door_rc);
		return (B_FALSE);
	}

	return (B_TRUE);
}

/*
 * Free resources allocated for a door call.  If the result buffer provided
 * by the client is too small, doorfs will have allocated a new buffer,
 * which must be unmapped here.
 *
 * This function must be called to free both the argument and result door
 * buffers regardless of the status of the door call.
 */
static void
smb_door_free(door_arg_t *arg)
{
	if (arg->rbuf && (arg->rbuf != arg->data_ptr))
		(void) munmap(arg->rbuf, arg->rsize);

	free(arg->data_ptr);
}
