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
 * Copyright (c) 2004, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#define	_XPG4_2
#define	__EXTENSIONS__

#include <assert.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <sys/socket.h>
#include <sys/stropts.h>
#include <sys/stream.h>
#include <sys/socketvar.h>
#include <sys/sockio.h>

#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <stropts.h>
#include <stdio.h>
#include <strings.h>
#include <netinet/in.h>
#include <netinet/sctp.h>

/* This will hold either a v4 or a v6 sockaddr */
union sockaddr_storage_v6 {
	struct sockaddr_in in;
	struct sockaddr_in6 in6;
};

/*
 * This file implements all the libsctp calls.
 */

/*
 * To bind a list of addresses to a socket.  If the socket is
 * v4, the type of the list of addresses is (struct in_addr).
 * If the socket is v6, the type is (struct in6_addr).
 */
int
sctp_bindx(int sock, void *addrs, int addrcnt, int flags)
{
	socklen_t sz;

	if (addrs == NULL || addrcnt == 0) {
		errno = EINVAL;
		return (-1);
	}

	/* Assume the caller uses the correct family type. */
	switch (((struct sockaddr *)addrs)->sa_family) {
	case AF_INET:
		sz = sizeof (struct sockaddr_in);
		break;
	case AF_INET6:
		sz = sizeof (struct sockaddr_in6);
		break;
	default:
		errno = EAFNOSUPPORT;
		return (-1);
	}

	switch (flags) {
	case SCTP_BINDX_ADD_ADDR:
		return (setsockopt(sock, IPPROTO_SCTP, SCTP_ADD_ADDR, addrs,
		    sz * addrcnt));
	case SCTP_BINDX_REM_ADDR:
		return (setsockopt(sock, IPPROTO_SCTP, SCTP_REM_ADDR, addrs,
		    sz * addrcnt));
	default:
		errno = EINVAL;
		return (-1);
	}
}

/*
 * XXX currently not atomic -- need a better way to do this.
 */
int
sctp_getpaddrs(int sock, sctp_assoc_t id, void **addrs)
{
	uint32_t naddrs;
	socklen_t bufsz;
	struct sctpopt opt;

	if (addrs == NULL) {
		errno = EINVAL;
		return (-1);
	}

	/* First, find out how many peer addresses there are. */
	*addrs = NULL;

	opt.sopt_aid = id;
	opt.sopt_name = SCTP_GET_NPADDRS;
	opt.sopt_val = (caddr_t)&naddrs;
	opt.sopt_len = sizeof (naddrs);
	if (ioctl(sock, SIOCSCTPGOPT, &opt) == -1) {
		return (-1);
	}
	if (naddrs == 0)
		return (0);

	/*
	 * Now we can get all the peer addresses.  This will over allocate
	 * space for v4 socket.  But it should be OK and save us
	 * the job to find out if it is a v4 or v6 socket.
	 */
	bufsz = sizeof (union sockaddr_storage_v6) * naddrs;
	if ((*addrs = malloc(bufsz)) == NULL) {
		return (-1);
	}
	opt.sopt_name = SCTP_GET_PADDRS;
	opt.sopt_val = *addrs;
	opt.sopt_len = bufsz;
	if (ioctl(sock, SIOCSCTPGOPT, &opt) == -1) {
		free(*addrs);
		*addrs = NULL;
		return (-1);
	}

	/* Calculate the number of addresses returned. */
	switch (((struct sockaddr *)*addrs)->sa_family) {
	case AF_INET:
		naddrs = opt.sopt_len / sizeof (struct sockaddr_in);
		break;
	case AF_INET6:
		naddrs = opt.sopt_len / sizeof (struct sockaddr_in6);
		break;
	}
	return (naddrs);
}

void
sctp_freepaddrs(void *addrs)
{
	free(addrs);
}

int
sctp_getladdrs(int sock, sctp_assoc_t id, void **addrs)
{
	uint32_t naddrs;
	socklen_t bufsz;
	struct sctpopt opt;

	if (addrs == NULL) {
		errno = EINVAL;
		return (-1);
	}

	/* First, try to find out how many bound addresses there are. */
	*addrs = NULL;

	opt.sopt_aid = id;
	opt.sopt_name = SCTP_GET_NLADDRS;
	opt.sopt_val = (caddr_t)&naddrs;
	opt.sopt_len = sizeof (naddrs);
	if (ioctl(sock, SIOCSCTPGOPT, &opt) == -1) {
		return (-1);
	}
	if (naddrs == 0)
		return (0);

	/*
	 * Now we can get all the bound addresses.  This will over allocate
	 * space for v4 socket.  But it should be OK and save us
	 * the job to find out if it is a v4 or v6 socket.
	 */
	bufsz = sizeof (union sockaddr_storage_v6) * naddrs;
	if ((*addrs = malloc(bufsz)) == NULL) {
		return (-1);
	}
	opt.sopt_name = SCTP_GET_LADDRS;
	opt.sopt_val = *addrs;
	opt.sopt_len = bufsz;
	if (ioctl(sock, SIOCSCTPGOPT, &opt) == -1) {
		free(*addrs);
		*addrs = NULL;
		return (-1);
	}

	/* Calculate the number of addresses returned. */
	switch (((struct sockaddr *)*addrs)->sa_family) {
	case AF_INET:
		naddrs = opt.sopt_len / sizeof (struct sockaddr_in);
		break;
	case AF_INET6:
		naddrs = opt.sopt_len / sizeof (struct sockaddr_in6);
		break;
	}
	return (naddrs);
}

void
sctp_freeladdrs(void *addrs)
{
	free(addrs);
}

int
sctp_opt_info(int sock, sctp_assoc_t id, int opt, void *arg, socklen_t *len)
{
	struct sctpopt sopt;

	sopt.sopt_aid = id;
	sopt.sopt_name = opt;
	sopt.sopt_val = arg;
	sopt.sopt_len = *len;

	if (ioctl(sock, SIOCSCTPGOPT, &sopt) == -1) {
		return (-1);
	}
	*len = sopt.sopt_len;
	return (0);
}

/*
 * Branch off an association to its own socket. ioctl() allocates and
 * returns new fd.
 */
int
sctp_peeloff(int sock, sctp_assoc_t id)
{
	int fd;

	fd = id;
	if (ioctl(sock, SIOCSCTPPEELOFF, &fd) == -1) {
		return (-1);
	}
	return (fd);
}


ssize_t
sctp_recvmsg(int s, void *msg, size_t len, struct sockaddr *from,
    socklen_t *fromlen, struct sctp_sndrcvinfo *sinfo, int *msg_flags)
{
	struct msghdr hdr;
	struct iovec iov;
	struct cmsghdr *cmsg;
	char cinmsg[sizeof (*sinfo) + sizeof (*cmsg) + _CMSG_HDR_ALIGNMENT];
	int err;

	hdr.msg_name = from;
	hdr.msg_namelen = (fromlen != NULL) ? *fromlen : 0;
	hdr.msg_iov = &iov;
	hdr.msg_iovlen = 1;
	if (sinfo != NULL) {
		hdr.msg_control = (void *)_CMSG_HDR_ALIGN(cinmsg);
		hdr.msg_controllen = sizeof (cinmsg) -
		    (_CMSG_HDR_ALIGN(cinmsg) - (uintptr_t)cinmsg);
	} else {
		hdr.msg_control = NULL;
		hdr.msg_controllen = 0;
	}

	iov.iov_base = msg;
	iov.iov_len = len;
	err = recvmsg(s, &hdr, msg_flags == NULL ? 0 : *msg_flags);
	if (err == -1) {
		return (-1);
	}
	if (fromlen != NULL) {
		*fromlen = hdr.msg_namelen;
	}
	if (msg_flags != NULL) {
		*msg_flags = hdr.msg_flags;
	}
	if (sinfo != NULL) {
		for (cmsg = CMSG_FIRSTHDR(&hdr); cmsg != NULL;
		    cmsg = CMSG_NXTHDR(&hdr, cmsg)) {
			if (cmsg->cmsg_level == IPPROTO_SCTP &&
			    cmsg->cmsg_type == SCTP_SNDRCV) {
				bcopy(CMSG_DATA(cmsg), sinfo, sizeof (*sinfo));
				break;
			}
		}
	}
	return (err);
}

static ssize_t
sctp_send_common(int s, const void *msg, size_t len, const struct sockaddr *to,
    socklen_t tolen, uint32_t ppid, uint32_t sinfo_flags, uint16_t stream_no,
    uint32_t timetolive, uint32_t context, sctp_assoc_t aid, int flags)
{
	struct msghdr hdr;
	struct iovec iov;
	struct sctp_sndrcvinfo *sinfo;
	struct cmsghdr *cmsg;
	char coutmsg[sizeof (*sinfo) + sizeof (*cmsg) + _CMSG_HDR_ALIGNMENT];

	hdr.msg_name = (caddr_t)to;
	hdr.msg_namelen = tolen;
	hdr.msg_iov = &iov;
	hdr.msg_iovlen = 1;
	hdr.msg_control = (void *)_CMSG_HDR_ALIGN(coutmsg);
	hdr.msg_controllen = sizeof (*cmsg) + sizeof (*sinfo);

	iov.iov_len = len;
	iov.iov_base = (caddr_t)msg;

	cmsg = CMSG_FIRSTHDR(&hdr);
	cmsg->cmsg_level = IPPROTO_SCTP;
	cmsg->cmsg_type = SCTP_SNDRCV;
	cmsg->cmsg_len = sizeof (*cmsg) + sizeof (*sinfo);

	sinfo = (struct sctp_sndrcvinfo *)CMSG_DATA(cmsg);
	sinfo->sinfo_stream = stream_no;
	sinfo->sinfo_ssn = 0;
	sinfo->sinfo_flags = sinfo_flags;
	sinfo->sinfo_ppid = ppid;
	sinfo->sinfo_context = context;
	sinfo->sinfo_timetolive = timetolive;
	sinfo->sinfo_tsn = 0;
	sinfo->sinfo_cumtsn = 0;
	sinfo->sinfo_assoc_id = aid;

	return (sendmsg(s, &hdr, flags));
}

ssize_t
sctp_send(int s, const void *msg, size_t len,
    const struct sctp_sndrcvinfo *sinfo, int flags)
{
	/* Note that msg can be NULL for pure control message. */
	if (sinfo == NULL) {
		errno = EINVAL;
		return (-1);
	}
	return (sctp_send_common(s, msg, len, NULL, 0, sinfo->sinfo_ppid,
	    sinfo->sinfo_flags, sinfo->sinfo_stream, sinfo->sinfo_timetolive,
	    sinfo->sinfo_context, sinfo->sinfo_assoc_id, flags));
}

ssize_t
sctp_sendmsg(int s, const void *msg, size_t len, const struct sockaddr *to,
    socklen_t tolen, uint32_t ppid, uint32_t flags, uint16_t stream_no,
    uint32_t timetolive, uint32_t context)
{
	return (sctp_send_common(s, msg, len, to, tolen, ppid, flags,
	    stream_no, timetolive, context, 0, 0));
}

/*
 * This function uses the SIOCSCTPCTX ioctl to try setting up an association
 * using the list of addresses given.  For 1-N style socket, the assoc ID
 * of the new association is returned in aid.
 */
int
sctp_connectx(int sd, struct sockaddr *addrs, int addrcnt, sctp_assoc_t *aid)
{
	socklen_t sz;
	struct sctp_ctx ctx;

	if (addrs == NULL || addrcnt <= 0) {
		errno = EINVAL;
		return (-1);
	}

	switch (addrs->sa_family) {
	case AF_INET:
		sz = sizeof (struct sockaddr_in);
		break;
	case AF_INET6:
		sz = sizeof (struct sockaddr_in6);
		break;
	default:
		errno = EAFNOSUPPORT;
		return (-1);
	}

	ctx.sctx_aid = 0;
	ctx.sctx_addr_cnt = addrcnt;
	ctx.sctx_addr_len = addrcnt * sz;
	ctx.sctx_addrs = (caddr_t)addrs;

	if (ioctl(sd, SIOCSCTPCTX, &ctx) == -1)
		return (-1);

	/* Need to return the aid created for 1-N style socket. */
	if (aid != NULL)
		*aid = ctx.sctx_aid;
	return (0);
}

/*
 * Receive a message along with its SCTP attributes.  The message is stored
 * in the iov vector.  The sender of the message is stored in from.  The
 * receive attriute is stoed in info.  The type of attribute is stored in
 * infotype.
 */
ssize_t
sctp_recvv(int sd, const struct iovec *iov, int iovlen, struct sockaddr *from,
    socklen_t *fromlen, void *info, socklen_t *infolen, unsigned int *infotype,
    int *flags)
{
	struct msghdr hdr;
	struct cmsghdr *cmsg;
	char *control = NULL;
	struct sctp_rcvinfo *rcvinfo;
	struct sctp_nxtinfo *nxtinfo;
	int err;

	hdr.msg_name = from;
	hdr.msg_namelen = (fromlen != NULL) ? *fromlen : 0;
	hdr.msg_iov = (struct iovec *)iov;
	hdr.msg_iovlen = iovlen;
	if (info != NULL) {
		ssize_t clen;

		/*
		 * Just in case, allocate a space to hold both sctp_rcvinfo and
		 * sctp_nxtinfo.
		 */
		clen = CMSG_SPACE(sizeof (struct sctp_rcvinfo)) +
		    CMSG_SPACE(sizeof (struct sctp_nxtinfo)) +
		    _CMSG_HDR_ALIGNMENT;
		if ((control = malloc(clen)) == NULL)
			return (-1);

		hdr.msg_control = (void *)_CMSG_HDR_ALIGN(control);
		hdr.msg_controllen = clen - (_CMSG_HDR_ALIGN(control) -
		    (uintptr_t)control);
	} else {
		hdr.msg_control = NULL;
		hdr.msg_controllen = 0;
	}

	err = recvmsg(sd, &hdr, flags == NULL ? 0 : *flags);
	if (err == -1) {
		if (control != NULL)
			free(control);
		return (-1);
	}

	if (fromlen != NULL)
		*fromlen = hdr.msg_namelen;
	if (flags != NULL)
		*flags = hdr.msg_flags;
	if (infotype != NULL)
		*infotype = SCTP_RECVV_NOINFO;

	if (info != NULL) {
		socklen_t tmp_infolen;
		struct sctp_recvv_rn *rn;

		rcvinfo = NULL;
		nxtinfo = NULL;

		/*
		 * SCTP does not send up sctp_recvv_rn.  So we need to first
		 * find the sctp_rcvinfo and sctp_nxtinfo.  If both are
		 * present, we create a sctp_recvv_rn.
		 */
		for (cmsg = CMSG_FIRSTHDR(&hdr); cmsg != NULL;
		    cmsg = CMSG_NXTHDR(&hdr, cmsg)) {
			if (cmsg->cmsg_level == IPPROTO_SCTP) {
				switch (cmsg->cmsg_type) {
				case SCTP_RCVINFO:
					rcvinfo = (struct sctp_rcvinfo *)
					    CMSG_DATA(cmsg);
					break;
				case SCTP_NXTINFO:
					nxtinfo = (struct sctp_nxtinfo *)
					    CMSG_DATA(cmsg);
					break;
				}
			}
		}

		if (rcvinfo == NULL && nxtinfo == NULL)
			goto done;

		tmp_infolen = *infolen;
		if (rcvinfo != NULL) {
			if (tmp_infolen < sizeof (*rcvinfo))
				goto done;
			bcopy(rcvinfo, info, sizeof (*rcvinfo));
			*infotype = SCTP_RECVV_RCVINFO;
			*infolen = sizeof (*rcvinfo);
		}
		if (nxtinfo != NULL) {
			if (rcvinfo != NULL) {
				if (tmp_infolen >= sizeof (*rn)) {
					rn = info;
					bcopy(nxtinfo, &rn->recvv_nxtinfo,
					    sizeof (*nxtinfo));
					*infotype = SCTP_RECVV_RN;
					*infolen = sizeof (*rn);
				}
			} else if (tmp_infolen >= sizeof (*nxtinfo)) {
				bcopy(nxtinfo, info, sizeof (*nxtinfo));
				*infotype = SCTP_RECVV_NXTINFO;
				*infolen = sizeof (*nxtinfo);
			}
		}
	}
done:
	if (control != NULL)
		free(control);
	return (err);
}

/*
 * This function uses SIOCSCTPSNDV ioctl to send a message with attributes
 * if given.  It can also be used to set up an association and then send
 * a message.   The message is stored in vector iov.  The addrs parameter
 * is used to set up an association or to specify an address the message
 * should be sent to.  In the latter case, SCTP_ADDR_OVER should be set.
 * The send attributes are stored in info and its type is passed in infotype.
 */
ssize_t
sctp_sendv(int sd, const struct iovec *iov, int iovcnt, struct sockaddr *addrs,
    int addrcnt, void *info, socklen_t infolen, unsigned int infotype,
    int flags)
{
	boolean_t conn_req = B_FALSE;
	struct sctp_sndinfo *sinfo;
	struct sctp_sendv_spa *spa;
	boolean_t sinfo_present = B_FALSE;
	sctp_assoc_t aid = 0;
	struct sctp_sendv sndv;

	if ((addrcnt > 0 && addrs == NULL) || (iovcnt > 0 && iov == NULL) ||
	    (infolen > 0 && info == NULL)) {
		errno = EINVAL;
		return (-1);
	}

	/* Check if this is a connect request. */
	if (addrcnt > 1 || (addrcnt == 1 && info == NULL)) {
		conn_req = B_TRUE;
	} else if (addrcnt == 1) {
		if (infotype == SCTP_SENDV_SNDINFO) {
			sinfo = info;
check_aid:
			sinfo_present = B_TRUE;
			if (sinfo->snd_assoc_id == 0) {
				if (sinfo->snd_flags & SCTP_ADDR_OVER) {
					errno = EINVAL;
					return (-1);
				}
				conn_req = B_TRUE;
			} else if (!(sinfo->snd_flags & SCTP_ADDR_OVER)) {
				errno = EINVAL;
				return (-1);
			}
		} else if (infotype == SCTP_SENDV_SPA) {
			spa = info;
			if (spa->sendv_flags & SCTP_SEND_SNDINFO_VALID) {
				sinfo = &spa->sendv_sndinfo;
				goto check_aid;
			}
		}
	}

	if (conn_req) {
		if (sctp_connectx(sd, addrs, addrcnt, &aid) != 0)
			return (-1);
	}

	if (conn_req) {
		sndv.ssndv_addr = NULL;
		sndv.ssndv_addr_len = 0;
	} else {
		/*
		 * If it is not a connect request, only take the first address
		 * and ignore the rest if any.
		 */
		sndv.ssndv_addr = (caddr_t)addrs;
		if (addrs != NULL) {
			if (addrs->sa_family == AF_INET) {
				sndv.ssndv_addr_len =
				    sizeof (struct sockaddr_in);
			} else {
				sndv.ssndv_addr_len =
				    sizeof (struct sockaddr_in6);
			}
		} else {
			sndv.ssndv_addr_len = 0;
		}
	}

	sndv.ssndv_aid = aid;
	sndv.ssndv_iov = iov;
	sndv.ssndv_iovcnt = iovcnt;

	sndv.ssndv_info = (caddr_t)info;
	sndv.ssndv_info_len = infolen;
	sndv.ssndv_info_type = infotype;
	sndv.ssndv_flags = flags;

	if (ioctl(sd, SIOCSCTPSNDV, &sndv) == -1) {
		/* Terminate the new association if an error occurs. */
		if (conn_req) {
			struct sctp_sndinfo abort_info = { 0 };

			sndv.ssndv_aid = aid;
			sndv.ssndv_iov = NULL;
			sndv.ssndv_iovcnt = 0;

			abort_info.snd_flags = SCTP_ABORT;
			sndv.ssndv_info = (caddr_t)&abort_info;
			sndv.ssndv_info_len = sizeof (abort_info);
			sndv.ssndv_info_type = SCTP_SENDV_SNDINFO;
			sndv.ssndv_flags = 0;

			/* Nothing we can do if an error happens again. */
			(void) ioctl(sd, SIOCSCTPSNDV, &sndv);
		}
		return (-1);
	}

	if (conn_req && sinfo_present)
		sinfo->snd_assoc_id = aid;

	return (0);
}
