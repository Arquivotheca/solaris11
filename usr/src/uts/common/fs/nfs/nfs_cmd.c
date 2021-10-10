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
 * Copyright (c) 2008, 2010, Oracle and/or its affiliates. All rights reserved.
 */

#include <sys/param.h>
#include <sys/types.h>
#include <sys/pathname.h>
#include <sys/errno.h>
#include <sys/cmn_err.h>
#include <sys/debug.h>
#include <sys/systm.h>
#include <sys/unistd.h>
#include <sys/door.h>
#include <sys/socket.h>
#include <nfs/export.h>
#include <nfs/nfs_cmd.h>
#include <sys/kmem.h>
#include <sys/sunddi.h>

#define	NFSCMD_DR_TRYCNT	8

#ifdef nextdp
#undef nextdp
#endif
#define	nextdp(dp)	((struct dirent64 *)((char *)(dp) + (dp)->d_reclen))

kmutex_t	nfscmd_lock;
door_handle_t   nfscmd_dh;

static struct charset_cache *nfscmd_charmap(exportinfo_t *exi,
    struct sockaddr *sp);


void
nfscmd_args(uint_t did)
{
	mutex_enter(&nfscmd_lock);
	if (nfscmd_dh)
		door_ki_rele(nfscmd_dh);
	nfscmd_dh = door_ki_lookup(did);
	mutex_exit(&nfscmd_lock);
}

void
nfscmd_init(void)
{
	mutex_init(&nfscmd_lock, NULL, MUTEX_DEFAULT, NULL);
}

void
nfscmd_fini(void)
{
}

/*
 * nfscmd_send(arg, result)
 *
 * Send a command to the daemon listening on the door. The result is
 * returned in the result pointer if the function return value is
 * NFSCMD_ERR_SUCCESS. Otherwise it is the error value.
 */
int
nfscmd_send(nfscmd_arg_t *arg, nfscmd_res_t *res)
{
	door_handle_t	dh;
	door_arg_t	da;
	door_info_t	di;
	int		ntries = 0;
	int		last = 0;

retry:
	mutex_enter(&nfscmd_lock);
	dh = nfscmd_dh;
	if (dh != NULL)
		door_ki_hold(dh);
	mutex_exit(&nfscmd_lock);

	if (dh == NULL) {
		/*
		 * The rendezvous point has not been established yet !
		 * This could mean that either mountd(1m) has not yet
		 * been started or that _this_ routine nuked the door
		 * handle after receiving an EINTR for a REVOKED door.
		 *
		 * Returning NFSAUTH_DROP will cause the NFS client
		 * to retransmit the request, so let's try to be more
		 * rescillient and attempt for ntries before we bail.
		 */
		if (++ntries % NFSCMD_DR_TRYCNT) {
			delay(hz);
			goto retry;
		}
		return (NFSCMD_ERR_DROP);
	}

	da.data_ptr = (char *)arg;
	da.data_size = sizeof (nfscmd_arg_t);
	da.desc_ptr = NULL;
	da.desc_num = 0;
	da.rbuf = (char *)res;
	da.rsize = sizeof (nfscmd_res_t);

	switch (door_ki_upcall(dh, &da)) {
	case 0:
		/* Success */
		break;
	case EAGAIN:
		/* Need to retry a couple of times */
		door_ki_rele(dh);
		delay(hz);
		goto retry;
		/* NOTREACHED */
	case EINTR:
		if (!door_ki_info(dh, &di)) {
			if (di.di_attributes & DOOR_REVOKED) {
				/*
				 * The server barfed and revoked
				 * the (existing) door on us; we
				 * want to wait to give smf(5) a
				 * chance to restart mountd(1m)
				 * and establish a new door handle.
				 */
				mutex_enter(&nfscmd_lock);
				if (dh == nfscmd_dh)
					nfscmd_dh = NULL;
				mutex_exit(&nfscmd_lock);
				door_ki_rele(dh);
				delay(hz);
				goto retry;
			}
			/*
			 * If the door was _not_ revoked on us,
			 * then more than likely we took an INTR,
			 * so we need to fail the operation.
			 */
			door_ki_rele(dh);
		}
		/*
		 * The only failure that can occur from getting
		 * the door info is EINVAL, so we let the code
		 * below handle it.
		 */
		/* FALLTHROUGH */

	case EBADF:
	case EINVAL:
	default:
		/*
		 * If we have a stale door handle, give smf a last
		 * chance to start it by sleeping for a little bit.
		 * If we're still hosed, we'll fail the call.
		 *
		 * Since we're going to reacquire the door handle
		 * upon the retry, we opt to sleep for a bit and
		 * _not_ to clear mountd_dh. If mountd restarted
		 * and was able to set mountd_dh, we should see
		 * the new instance; if not, we won't get caught
		 * up in the retry/DELAY loop.
		 */
		door_ki_rele(dh);
		if (!last) {
			delay(hz);
			last++;
			goto retry;
		}
		res->error = NFSCMD_ERR_FAIL;
		break;
	}
	return (res->error);
}

/*
 * nfscmd_findmap(export, addr)
 *
 * Find a characterset map for the specified client address.
 * First try to find a cached entry. If not successful,
 * ask mountd daemon running in userland.
 *
 * For most of the clients this function is NOOP, since
 * EX_CHARMAP flag won't be set.
 */
struct charset_cache *
nfscmd_findmap(struct exportinfo *exi, struct sockaddr *sp)
{
	struct charset_cache *charset;

	/*
	 * In debug kernel we want to know about strayed nulls.
	 * In non-debug kernel we behave gracefully.
	 */
	ASSERT(exi != NULL);
	ASSERT(sp != NULL);

	if (exi == NULL || sp == NULL)
		return (NULL);

	mutex_enter(&exi->exi_lock);

	if (!(exi->exi_export.ex_flags & EX_CHARMAP)) {
		mutex_exit(&exi->exi_lock);
		return (NULL);
	}

	for (charset = exi->exi_charset;
	    charset != NULL;
	    charset = charset->next) {
		if (bcmp(sp, &charset->client_addr,
		    sizeof (struct sockaddr)) == 0)
			break;
	}
	mutex_exit(&exi->exi_lock);

	/* the slooow way - ask daemon */
	if (charset == NULL)
		charset = nfscmd_charmap(exi, sp);

	return (charset);
}

/*
 * nfscmd_insert_charmap(export, addr, name)
 *
 * Insert a new character set conversion map into the export structure
 * for the share. The entry has the IP address of the client and the
 * character set name.
 */

static struct charset_cache *
nfscmd_insert_charmap(struct exportinfo *exi, struct sockaddr *sp, char *name)
{
	struct charset_cache *charset;

	charset = (struct charset_cache *)
	    kmem_zalloc(sizeof (struct charset_cache), KM_SLEEP);

	if (charset == NULL)
		return (NULL);
	if (name != NULL) {
		charset->inbound = kiconv_open("UTF-8", name);
		charset->outbound = kiconv_open(name, "UTF-8");
		/*
		 * kiconv_open() returns -1 for invalid conversions. This
		 * should not happen as mountd should supply only supported
		 * charsets. If it happens we just log a message and don't
		 * translate characters.
		 */
		if (charset->inbound == (kiconv_t)-1 ||
		    charset->outbound == (kiconv_t)-1) {
			if (charset->inbound != (kiconv_t)-1)
				(void) kiconv_close(charset->inbound);
			if (charset->outbound != (kiconv_t)-1)
				(void) kiconv_close(charset->outbound);
			charset->inbound = NULL;
			charset->outbound = NULL;
			zcmn_err(getzoneid(), CE_WARN, "nfscmd_insert_charmap: "
			    "kiconv_open failed for NFS share option %s", name);
		}
	}
	charset->client_addr = *sp;
	mutex_enter(&exi->exi_lock);
	charset->next = exi->exi_charset;
	exi->exi_charset = charset;
	mutex_exit(&exi->exi_lock);

	return (charset);
}

/*
 * nfscmd_charmap(response, sp, exi)
 *
 * Check to see if this client needs a character set conversion.
 */
static struct charset_cache *
nfscmd_charmap(exportinfo_t *exi, struct sockaddr *sp)
{
	nfscmd_arg_t req;
	int ret;
	char *path;
	nfscmd_res_t res;
	struct charset_cache *charset;

	path = exi->exi_export.ex_path;
	if (path == NULL)
		return (NULL);

	/*
	 * nfscmd_findmap() did not find one in the cache so make
	 * the request to the daemon. We need to add the entry in
	 * either case since we want negative as well as
	 * positive cacheing.
	 */
	req.cmd = NFSCMD_CHARMAP_LOOKUP;
	req.version = NFSCMD_VERSION;
	req.arg.charmap.addr = *sp;
	(void) strncpy(req.arg.charmap.path, path, MAXPATHLEN);
	bzero((caddr_t)&res, sizeof (nfscmd_res_t));
	ret = nfscmd_send(&req, &res);
	if (ret == NFSCMD_ERR_SUCCESS)
		charset = nfscmd_insert_charmap(exi, sp,
		    res.result.charmap.codeset);
	else
		charset = nfscmd_insert_charmap(exi, sp, NULL);

	return (charset);
}

/*
 * nfscmd_convname(addr, export, name, inbound, size)
 *
 * Convert the given "name" string to the appropriate character set.
 * If inbound is true, convert from the client character set to UTF-8.
 * If inbound is false, convert from UTF-8 to the client characters set.
 *
 * In case of NFS v4 this is used for ill behaved clients, since
 * according to the standard all file names should be utf-8 encoded
 * on client-side.
 */

char *
nfscmd_convname(struct sockaddr *ca, struct exportinfo *exi, char *name,
    int inbound, size_t size)
{
	char *newname;
	char *holdname;
	int err;
	int ret;
	size_t nsize;
	size_t osize;
	struct charset_cache *charset = NULL;

	charset = nfscmd_findmap(exi, ca);
	if (charset == NULL ||
	    (charset->inbound == NULL && inbound) ||
	    (charset->outbound == NULL && !inbound))
		return (name);

	/* make sure we have more than enough space */
	newname = kmem_zalloc(size, KM_SLEEP);
	nsize = strlen(name);
	osize = size;
	holdname = newname;
	if (inbound)
		ret = kiconv(charset->inbound, &name, &nsize,
		    &holdname, &osize, &err);
	else
		ret = kiconv(charset->outbound, &name, &nsize,
		    &holdname, &osize, &err);
	if (ret == (size_t)-1) {
		kmem_free(newname, size);
		newname = NULL;
	}

	return (newname);
}

/*
 * Convert the dirents in data. There are two challenges to face here.
 * If converted dirents don't fit into a buffer of size maxsize, they
 * are cropped. If there are invalid dirents (dirents which cannot be
 * converted from utf-8 to desired charset), they are skipped.
 *
 * Inputs:
 *	cd:	kiconv descriptor used for conversion
 *	data:	dirents to be converted (the dirents buffer)
 *	size:	size of meaningful data in data buffer
 *	maxsize: allocation size of dirents buffer
 *	infop:	Additional information about dirents delivered in
 *		READDIRPLUS3 response. infop is ignored if it is NULL.
 *
 * Return value:
 *	0 if data fit in buffer of size maxsize and non-zero if the data
 *	were cropped.
 *
 * Outputs:
 *	data:	dirents buffer of size maxsize holding converted dirents
 *	size:	size of meaningful data in data buffer
 *	nents:	number of dirents in dirents buffer
 *	infop:	entries associated with skipped dirents are removed
 *		from infop array to keep "data" and "infop" in sync
 */
int
nfscmd_convdirents(kiconv_t cd, char **data, size_t *size,
    size_t maxsize, int *nents, entryplus3_info *infop)
{
	char *newdata;
	size_t nsize;
	struct dirent64 *dp;
	struct dirent64 *ndp;
	off64_t *last_conv_offset;
	off64_t next_offset;
	size_t ret;
	char *iname;
	char *oname;
	size_t ilen;
	size_t olen;
	size_t reclen;
	int err, skipped, cropped = 0;
	char name[MAXNAMELEN];

	newdata = kmem_zalloc(maxsize, KM_SLEEP);
	ndp = (struct dirent64 *)newdata;
	*nents = 0;
	skipped = 0;
	nsize = 0;
	last_conv_offset = NULL;

	for (dp = (struct dirent64 *)*data; (char *)dp < *data + *size;
	    dp = nextdp(dp)) {

		iname = dp->d_name;
		ilen = strlen(dp->d_name);
		oname = name;
		olen = MAXNAMELEN - 1;

		ret = kiconv(cd, &iname, &ilen, &oname,
		    &olen, &err);
		if (ret == (size_t)-1) {
			/* skip invalid dirent */
			next_offset = dp->d_off;
			skipped++;
			continue;
		}

		*oname = '\0';
		reclen = DIRENT64_RECLEN(MAXNAMELEN - 1 - olen);
		if (nsize + reclen > maxsize) {
			cropped = 1;
			break;
		}
		*ndp = *dp;
		(void) strcpy(ndp->d_name, name);
		ndp->d_reclen = reclen;
		last_conv_offset = &ndp->d_off;
		next_offset = 0;
		nsize += reclen;

		/* We overwrite infop entries for skipped dirents. */
		if (infop != NULL && skipped > 0)
			infop[*nents] = infop[*nents + skipped];
		(*nents)++;
		ndp = nextdp(ndp);
	}

	/*
	 * If we skipped the last entry we need to patch d_off of the last
	 * converted entry to become d_off from the last seen entry, so that
	 * the next request starts VOP_READDIR with correct offset).
	 */
	if (last_conv_offset != NULL && next_offset > 0)
		*last_conv_offset = next_offset;

	kmem_free(*data, maxsize);
	*size = nsize;
	*data = newdata;

	return (cropped);
}
