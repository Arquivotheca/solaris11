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

#include <assert.h>
#include <stdio.h>
#include <door.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/vlan.h>
#include <fcntl.h>
#include <libintl.h>
#include <libdllink.h>
#include <libdlvnic.h>
#include <libdlvlan.h>
#include <liblldp.h>
#include <liblldp_lldpd.h>

extern char	*lldp_alloc_fmri(const char *, const char *);

/*
 * Make a door call to the server and checks if the door call succeeded or not.
 * `is_varsize' specifies that the data returned by lldpd daemon is of
 * variable size and door will allocate buffer using mmap(). In such cases
 * we re-allocate the required memory and assign it to `rbufp', copy the data
 * to `rbufp' and then call munmap() (see below).
 *
 * It also checks to see if the server side procedure ran successfully by
 * checking for lr_err. Therefore, for some callers who just care about the
 * return status, should set `rbufp' to NULL and set `rsize' to 0.
 */
static int
i_lldp_common_door_call(void *arg, size_t asize, void **rbufp, size_t rsize,
    boolean_t is_varsize)
{
	door_arg_t	darg;
	int		door_fd, err;
	lldpd_retval_t	rval, *rvalp;

	if (rbufp == NULL) {
		rvalp = &rval;
		rbufp = (void **)&rvalp;
		rsize = sizeof (rval);
	}

	darg.data_ptr = arg;
	darg.data_size = asize;
	darg.desc_ptr = NULL;
	darg.desc_num = 0;
	darg.rbuf = *rbufp;
	darg.rsize = rsize;

	door_fd = open(LLDPD_DOOR, O_RDONLY | O_NOFOLLOW | O_NONBLOCK);
	if (door_fd == -1)
		return (errno);

	if (door_call(door_fd, &darg) == -1) {
		err = errno;
		(void) close(door_fd);
		return (errno);
	}
	err = ((lldpd_retval_t *)(void *)(darg.rbuf))->lr_err;
	if (darg.rbuf != *rbufp) {
		/*
		 * if the caller is expecting the result to fit in specified
		 * buffer then return failure.
		 */
		if (!is_varsize)
			err = EBADE;
		/*
		 * The size of the buffer `*rbufp' was not big enough
		 * and the door itself allocated buffer, for us. We will
		 * hit this, on several occasion as for some cases
		 * we cannot predict the size of the return structure.
		 * Reallocate the buffer `*rbufp' and memcpy() the contents
		 * to new buffer.
		 */
		if (err == 0) {
			void *newp;

			/* allocated memory will be freed by the caller */
			if ((newp = realloc(*rbufp, darg.rsize)) == NULL) {
				err = ENOMEM;
			} else {
				*rbufp = newp;
				(void) memcpy(*rbufp, darg.rbuf, darg.rsize);
			}
		}
		/* munmap() the door buffer */
		(void) munmap(darg.rbuf, darg.rsize);
	} else {
		if (darg.rsize != rsize)
			err = EBADE;
	}
	(void) close(door_fd);
	return (err);
}

/*
 * Makes a door call to the server and `rbufp' is not re-allocated. If
 * the data returned from the server can't be accomodated in `rbufp'
 * that is of size `rsize' then an error will be returned.
 */
int
lldp_door_call(void *arg, size_t asize, void *rbufp, size_t rsize)
{
	return (i_lldp_common_door_call(arg, asize,
	    (rbufp == NULL ? NULL : &rbufp), rsize, B_FALSE));
}

/*
 * Makes a door call to the server and `rbufp' always points to a
 * re-allocated memory and should be freed by the caller. This should be
 * used by callers who are not sure of the size of the data returned by
 * the server.
 */
int
lldp_door_dyncall(void *arg, size_t asize, void **rbufp, size_t rsize)
{
	return (i_lldp_common_door_call(arg, asize, rbufp, rsize, B_TRUE));
}

/*
 * returns the remote/local list of TLVs for a given `linkid'.
 * Caller frees nvlist memory.
 */
lldp_status_t
lldp_get_agent_info(const char *laname, boolean_t neighbor, nvlist_t **nvl)
{
	lldpd_door_minfo_t	dm;
	lldpd_minfo_retval_t	*rval;
	int			err = 0;
	char			*buf;

	*nvl = NULL;
	dm.ldm_cmd = LLDPD_CMD_GET_INFO;
	/* link validation will be done by the daemon */
	(void) strlcpy(dm.ldm_laname, laname, sizeof (dm.ldm_laname));
	dm.ldm_neighbor = neighbor;
	if ((rval = calloc(1, sizeof (lldpd_minfo_retval_t))) == NULL)
		return (LLDP_STATUS_NOMEM);

	if ((err = lldp_door_dyncall(&dm, sizeof (dm), (void **)&rval,
	    sizeof (*rval))) != 0) {
		goto ret;
	}
	if (rval->lmr_err != 0) {
		err = rval->lmr_err;
	} else if (rval->lmr_listsz >  0) {
		buf = (char *)rval + sizeof (lldpd_minfo_retval_t);
		err = nvlist_unpack(buf, rval->lmr_listsz, nvl, 0);
	}
ret:
	free(rval);
	return (lldp_errno2status(err));
}

/* Get LLDP agent stats */
/* ARGSUSED */
lldp_status_t
lldp_get_agent_stats(const char *laname, lldp_stats_t *statp, uint32_t flags)
{
	lldpd_door_lstats_t	lstats;
	lldpd_lstats_retval_t	rval;
	int			err;

	lstats.ld_cmd = LLDPD_CMD_GET_STATS;
	/* link validation will be done by the daemon */
	(void) strlcpy(lstats.ld_laname, laname, sizeof (lstats.ld_laname));

	err = lldp_door_call(&lstats, sizeof (lstats), &rval, sizeof (rval));
	if (err != 0)
		return (lldp_errno2status(err));
	(void) memcpy(statp, &rval.lr_stat, sizeof (*statp));
	return (LLDP_STATUS_OK);
}

char *
lldp_alloc_fmri(const char *service, const char *instance_name)
{
	ssize_t max_fmri;
	char *fmri;

	/* If the limit is unknown, then use an arbitrary value */
	if ((max_fmri = scf_limit(SCF_LIMIT_MAX_FMRI_LENGTH)) == -1)
		max_fmri = 1024;
	if ((fmri = malloc(max_fmri)) != NULL) {
		(void) snprintf(fmri, max_fmri, "svc:/%s:%s", service,
		    instance_name);
	}
	return (fmri);
}

/* Call into LLDP to notify of an event. */
lldp_status_t
lldp_notify_events(int event, nvlist_t *einfo)
{
	datalink_id_t	linkid, vlinkid;
	uint16_t	vid;
	boolean_t	isprim;
	lldp_vinfo_t	vinfo;
	lldpd_retval_t	retval;
	boolean_t	added;
	char		*fmri, *state;
	int		err;

	if (event != LLDP_VIRTUAL_LINK_UPDATE)
		return (LLDP_STATUS_NOTSUP);

	/*
	 * if LLDP daemon is not installed or is not online then
	 * there is nothing to do.
	 */
	if ((fmri = lldp_alloc_fmri(LLDP_SVC_NAME,
	    LLDP_SVC_DEFAULT_INSTANCE)) == NULL) {
		return (LLDP_STATUS_NOMEM);
	}
	state = smf_get_state(fmri);
	free(fmri);
	if (state == NULL || strcmp(state, SCF_STATE_STRING_ONLINE) != 0) {
		free(state);
		return (LLDP_STATUS_OK);
	}
	free(state);

	if (nvlist_lookup_uint32(einfo, LLDP_NV_LINKID, &linkid) != 0 ||
	    nvlist_lookup_uint32(einfo, LLDP_NV_VLINKID, &vlinkid) != 0 ||
	    nvlist_lookup_uint16(einfo, LLDP_NV_VID, &vid) != 0 ||
	    nvlist_lookup_boolean_value(einfo, LLDP_NV_ISPRIM, &isprim) != 0 ||
	    nvlist_lookup_boolean_value(einfo, LLDP_NV_ADDED, &added) != 0) {
		return (LLDP_STATUS_BADVAL);
	}

	bzero(&vinfo, sizeof (lldp_vinfo_t));
	vinfo.lvi_cmd = LLDPD_CMD_UPDATE_VLINKS;
	vinfo.lvi_operation =
	    (added ? LLDP_ADD_OPERATION : LLDP_DELETE_OPERATION);
	vinfo.lvi_vlinkid = vlinkid;
	vinfo.lvi_plinkid = linkid;
	vinfo.lvi_vid = vid;
	vinfo.lvi_isvnic = !isprim;
	err = lldp_door_call(&vinfo, sizeof (vinfo), &retval, sizeof (retval));
	return (lldp_errno2status(err));
}

const char *
lldp_status2str(lldp_status_t status, char *buf)
{
	const char	*s;

	switch (status) {
	case LLDP_STATUS_OK:
		s = "ok";
		break;
	case LLDP_STATUS_EXIST:
		s = "object already exists";
		break;
	case LLDP_STATUS_BADARG:
		s = "invalid argument";
		break;
	case LLDP_STATUS_FAILED:
		s = "operation failed";
		break;
	case LLDP_STATUS_TOOSMALL:
		s = "buffer size too small";
		break;
	case LLDP_STATUS_NOTSUP:
		s = "operation not supported";
		break;
	case LLDP_STATUS_PROPUNKNOWN:
		s = "property unknown";
		break;
	case LLDP_STATUS_BADVAL:
		s = "invalid value";
		break;
	case LLDP_STATUS_NOMEM:
		s = "insufficient memory";
		break;
	case LLDP_STATUS_LINKINVAL:
		s = "invalid link";
		break;
	case LLDP_STATUS_LINKBUSY:
		s = "link already in use";
		break;
	case LLDP_STATUS_PERSISTERR:
		s = "persistence of configuration failed";
		break;
	case LLDP_STATUS_BADRANGE:
		s = "invalid range";
		break;
	case LLDP_STATUS_DISABLED:
		s = "LLDP not enabled on port";
		break;
	case LLDP_STATUS_TEMPONLY:
		s = "change cannot be persistent";
		break;
	case LLDP_STATUS_NOTFOUND:
		s = "object not found";
		break;
	case LLDP_STATUS_PERMDENIED:
		s = "Permission denied";
		break;
	default:
		s = "<unknown error>";
		break;
	}
	(void) snprintf(buf, LLDP_STRSIZE, "%s", dgettext(TEXT_DOMAIN, s));
	return (buf);
}

/*
 * Convert a unix errno to a lldp_status_t.
 * We only convert errnos that are likely to be encountered. All others
 * are mapped to LLDP_STATUS_FAILED.
 */
lldp_status_t
lldp_errno2status(int err)
{
	switch (err) {
	case 0:
		return (LLDP_STATUS_OK);
	case ESRCH:
		return (LLDP_STATUS_DISABLED);
	case EACCES:
	case EPERM:
		return (LLDP_STATUS_PERMDENIED);
	case ENOENT:
		return (LLDP_STATUS_NOTFOUND);
	case EINVAL:
		return (LLDP_STATUS_BADARG);
	case EEXIST:
		return (LLDP_STATUS_EXIST);
	case ENOSPC:
		return (LLDP_STATUS_TOOSMALL);
	case ENOMEM:
		return (LLDP_STATUS_NOMEM);
	case ENOTSUP:
		return (LLDP_STATUS_NOTSUP);
	case ENODATA:
		return (LLDP_STATUS_PERSISTERR);
	case EBUSY:
		return (LLDP_STATUS_LINKBUSY);
	default:
		return (LLDP_STATUS_FAILED);
	}
}
