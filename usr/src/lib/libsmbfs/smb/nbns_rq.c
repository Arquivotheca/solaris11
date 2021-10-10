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
 *
 * $Id: nbns_rq.c,v 1.9 2005/02/24 02:04:38 lindak Exp $
 */

/*
 * Copyright (c) 2008, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#include <sys/param.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <ctype.h>
#include <netdb.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdio.h>
#include <unistd.h>
#include <libintl.h>

#include <netinet/in.h>
#include <arpa/inet.h>
#include <tsol/label.h>

#define	NB_NEEDRESOLVER
#include <netsmb/netbios.h>
#include <netsmb/smb_lib.h>
#include <netsmb/nb_lib.h>
#include <netsmb/mchain.h>

#include "charsets.h"
#include "private.h"

/*
 * nbns request
 */
struct nbns_rq {
	int		nr_opcode;
	int		nr_nmflags;
	int		nr_rcode;
	int		nr_qdcount;
	int		nr_ancount;
	int		nr_nscount;
	int		nr_arcount;
	struct nb_name	*nr_qdname;
	uint16_t	nr_qdtype;
	uint16_t	nr_qdclass;
	struct in_addr	nr_dest;	/* receiver of query */
	struct sockaddr_in nr_sender;	/* sender of response */
	int		nr_rpnmflags;
	int		nr_rprcode;
	uint16_t	nr_rpancount;
	uint16_t	nr_rpnscount;
	uint16_t	nr_rparcount;
	uint16_t	nr_trnid;
	struct nb_ctx	*nr_nbd;
	struct mbdata	nr_rq;
	struct mbdata	nr_rp;
	struct nb_ifdesc *nr_if;
	int		nr_flags;
	int		nr_fd;
	int		nr_maxretry;
};
typedef struct nbns_rq nbns_rq_t;

static int  nbns_rq_create(int opcode, struct nb_ctx *ctx,
    struct nbns_rq **rqpp);
static void nbns_rq_done(struct nbns_rq *rqp);
static int  nbns_rq_getrr(struct nbns_rq *rqp, struct nbns_rr *rrp);
static int  nbns_rq_prepare(struct nbns_rq *rqp);
static int  nbns_rq(struct nbns_rq *rqp);

/*
 * Call NetBIOS name lookup and return a result in the
 * same form as getaddrinfo(3) returns.  Return code is
 * zero or one of the EAI_xxx codes like getaddrinfo.
 */
int
nbns_getaddrinfo(const char *name, struct nb_ctx *nbc, struct addrinfo **res)
{
	struct addrinfo *nai = NULL;
	struct sockaddr *sap = NULL;
	char *ucname = NULL;
	int err;

	/*
	 * Try NetBIOS name lookup.
	 */
	if (strlen(name) >= NB_NAMELEN) {
		err = EAI_OVERFLOW;
		goto out;
	}
	ucname = utf8_str_toupper(name);
	if (ucname == NULL)
		goto nomem;

	/* Note: this returns an NBERROR value. */
	err = nbns_resolvename(ucname, nbc, &sap);
	if (err) {
		if (smb_verbose)
			smb_error(dgettext(TEXT_DOMAIN,
			    "nbns_resolvename: %s"),
			    err, name);
		err = EAI_NODATA;
		goto out;
	}
	/* Note: sap allocated */

	/*
	 * Build the addrinfo struct to return.
	 */
	nai = malloc(sizeof (*nai));
	if (nai == NULL)
		goto nomem;
	bzero(nai, sizeof (*nai));

	nai->ai_flags = AI_CANONNAME;
	nai->ai_family = sap->sa_family;
	nai->ai_socktype = SOCK_STREAM;
	nai->ai_canonname = ucname;
	ucname = NULL;

	/*
	 * The type of this is really sockaddr_in,
	 * but is returned in the generic form.
	 * See nbns_resolvename.
	 */
	nai->ai_addrlen = sizeof (struct sockaddr_in);
	nai->ai_addr = sap;

	*res = nai;
	return (0);

nomem:
	err = EAI_MEMORY;
out:
	if (nai != NULL)
		free(nai);
	if (sap)
		free(sap);
	if (ucname)
		free(ucname);
	*res = NULL;

	return (err);
}

int
nbns_resolvename(const char *name, struct nb_ctx *ctx, struct sockaddr **adpp)
{
	struct nbns_rq *rqp;
	struct nb_name nn;
	struct nbns_rr rr;
	struct sockaddr_in *dest;
	int error, rdrcount, len;

	if (strlen(name) >= NB_NAMELEN)
		return (NBERROR(NBERR_NAMETOOLONG));
	error = nbns_rq_create(NBNS_OPCODE_QUERY, ctx, &rqp);
	if (error)
		return (error);
	/*
	 * Pad the name with blanks, but
	 * leave the "type" byte NULL.
	 * nb_name_encode adds the type.
	 */
	bzero(&nn, sizeof (nn));
	snprintf(nn.nn_name, NB_NAMELEN, "%-15.15s", name);
	nn.nn_type = NBT_SERVER;
	nn.nn_scope = ctx->nb_scope;
	rqp->nr_nmflags = NBNS_NMFLAG_RD;
	rqp->nr_qdname = &nn;
	rqp->nr_qdtype = NBNS_QUESTION_TYPE_NB;
	rqp->nr_qdclass = NBNS_QUESTION_CLASS_IN;
	rqp->nr_qdcount = 1;
	rqp->nr_maxretry = 5;

	error = nbns_rq_prepare(rqp);
	if (error) {
		nbns_rq_done(rqp);
		return (error);
	}
	rdrcount = NBNS_MAXREDIRECTS;
	for (;;) {
		error = nbns_rq(rqp);
		if (error)
			break;
		if ((rqp->nr_rpnmflags & NBNS_NMFLAG_AA) == 0) {
			/*
			 * Not an authoritative answer.  Query again
			 * using the NS address in the 2nd record.
			 */
			if (rdrcount-- == 0) {
				error = NBERROR(NBERR_TOOMANYREDIRECTS);
				break;
			}
			error = nbns_rq_getrr(rqp, &rr);
			if (error)
				break;
			error = nbns_rq_getrr(rqp, &rr);
			if (error)
				break;
			bcopy(rr.rr_data, &rqp->nr_dest, 4);
			continue;
		}
		if (rqp->nr_rpancount == 0) {
			error = NBERROR(NBERR_HOSTNOTFOUND);
			break;
		}
		error = nbns_rq_getrr(rqp, &rr);
		if (error)
			break;
		len = sizeof (struct sockaddr_in);
		dest = malloc(len);
		if (dest == NULL) {
			error = ENOMEM;
			break;
		}

		bzero(dest, len);
		/*
		 * Solaris sockaddr_in doesn't a sin_len field.
		 * dest->sin_len = len;
		 */
		dest->sin_family = AF_NETBIOS;	/* nb_lib.h */
		bcopy(rr.rr_data + 2, &dest->sin_addr.s_addr, 4);
		*adpp = (struct sockaddr *)dest;
		ctx->nb_lastns = rqp->nr_sender;
		break;
	}
	nbns_rq_done(rqp);
	return (error);
}

/*
 * NB: system, workgroup are both NB_NAMELEN
 */
int
nbns_getnodestatus(struct nb_ctx *ctx,
    struct in_addr *targethost, char *system, char *workgroup)
{
	struct nbns_rq *rqp;
	struct nbns_rr rr;
	struct nb_name nn;
	struct nbns_nr *nrp;
	char nrtype;
	char *cp, *retname = NULL;
	unsigned char nrcount;
	int error, i, foundserver = 0, foundgroup = 0;

	error = nbns_rq_create(NBNS_OPCODE_QUERY, ctx, &rqp);
	if (error)
		return (error);
	bzero(&nn, sizeof (nn));
	strcpy((char *)nn.nn_name, "*");
	nn.nn_scope = ctx->nb_scope;
	nn.nn_type = NBT_WKSTA;
	rqp->nr_nmflags = 0;
	rqp->nr_qdname = &nn;
	rqp->nr_qdtype = NBNS_QUESTION_TYPE_NBSTAT;
	rqp->nr_qdclass = NBNS_QUESTION_CLASS_IN;
	rqp->nr_qdcount = 1;
	rqp->nr_maxretry = 2;

	rqp->nr_dest = *targethost;
	error = nbns_rq_prepare(rqp);
	if (error) {
		nbns_rq_done(rqp);
		return (error);
	}

	/*
	 * Darwin had a loop here, allowing redirect, etc.
	 * but we only handle point-to-point for node status.
	 */
	error = nbns_rq(rqp);
	if (error)
		goto out;
	if (rqp->nr_rpancount == 0) {
		error = NBERROR(NBERR_HOSTNOTFOUND);
		goto out;
	}
	error = nbns_rq_getrr(rqp, &rr);
	if (error)
		goto out;

	/* Compiler didn't like cast on lvalue++ */
	nrcount = *((unsigned char *)rr.rr_data);
	rr.rr_data++;
	/* LINTED */
	for (i = 1, nrp = (struct nbns_nr *)rr.rr_data;
	    i <= nrcount; ++i, ++nrp) {
		nrtype = nrp->ns_name[NB_NAMELEN-1];
		/* Terminate the string: */
		nrp->ns_name[NB_NAMELEN-1] = (char)0;
		/* Strip off trailing spaces */
		for (cp = &nrp->ns_name[NB_NAMELEN-2];
		    cp >= nrp->ns_name; --cp) {
			if (*cp != (char)0x20)
				break;
			*cp = (char)0;
		}
		nrp->ns_flags = ntohs(nrp->ns_flags);
		DPRINT(" %s[%02x] Flags 0x%x",
		    nrp->ns_name, nrtype, nrp->ns_flags);
		if (nrp->ns_flags & NBNS_GROUPFLG) {
			if (!foundgroup ||
			    (foundgroup != NBT_WKSTA+1 &&
			    nrtype == NBT_WKSTA)) {
				strlcpy(workgroup, nrp->ns_name,
				    NB_NAMELEN);
				foundgroup = nrtype+1;
			}
		} else {
			/*
			 * Track at least ONE name, in case
			 * no server name is found
			 */
			retname = nrp->ns_name;
		}
		/*
		 * Keep the first NBT_SERVER name.
		 */
		if (nrtype == NBT_SERVER && foundserver == 0) {
			strlcpy(system, nrp->ns_name,
			    NB_NAMELEN);
			foundserver = 1;
		}
	}
	if (foundserver == 0 && retname != NULL)
		strlcpy(system, retname, NB_NAMELEN);
	ctx->nb_lastns = rqp->nr_sender;

out:
	nbns_rq_done(rqp);
	return (error);
}

int
nbns_rq_create(int opcode, struct nb_ctx *ctx, struct nbns_rq **rqpp)
{
	struct nbns_rq *rqp;
	static uint16_t trnid;
	int error;

	if (trnid == 0)
		trnid = getpid();
	rqp = malloc(sizeof (*rqp));
	if (rqp == NULL)
		return (ENOMEM);
	bzero(rqp, sizeof (*rqp));
	error = mb_init_sz(&rqp->nr_rq, NBDG_MAXSIZE);
	if (error) {
		free(rqp);
		return (error);
	}
	rqp->nr_opcode = opcode;
	rqp->nr_nbd = ctx;
	rqp->nr_trnid = trnid++;
	*rqpp = rqp;
	return (0);
}

void
nbns_rq_done(struct nbns_rq *rqp)
{
	if (rqp == NULL)
		return;
	if (rqp->nr_fd >= 0)
		close(rqp->nr_fd);
	mb_done(&rqp->nr_rq);
	mb_done(&rqp->nr_rp);
	if (rqp->nr_if)
		free(rqp->nr_if);
	free(rqp);
}

/*
 * Extract resource record from the packet. Assume that there is only
 * one mbuf.
 */
int
nbns_rq_getrr(struct nbns_rq *rqp, struct nbns_rr *rrp)
{
	struct mbdata *mbp = &rqp->nr_rp;
	uchar_t *cp;
	int error, len;

	bzero(rrp, sizeof (*rrp));
	cp = (uchar_t *)mbp->mb_pos;
	len = nb_encname_len(cp);
	if (len < 1)
		return (NBERROR(NBERR_INVALIDRESPONSE));
	rrp->rr_name = cp;
	error = md_get_mem(mbp, NULL, len, MB_MSYSTEM);
	if (error)
		return (error);
	md_get_uint16be(mbp, &rrp->rr_type);
	md_get_uint16be(mbp, &rrp->rr_class);
	md_get_uint32be(mbp, &rrp->rr_ttl);
	md_get_uint16be(mbp, &rrp->rr_rdlength);
	rrp->rr_data = (uchar_t *)mbp->mb_pos;
	error = md_get_mem(mbp, NULL, rrp->rr_rdlength, MB_MSYSTEM);
	return (error);
}

int
nbns_rq_prepare(struct nbns_rq *rqp)
{
	struct nb_ctx *ctx = rqp->nr_nbd;
	struct mbdata *mbp = &rqp->nr_rq;
	uint16_t ofr; /* opcode, flags, rcode */
	int error;

	error = mb_init_sz(&rqp->nr_rp, NBDG_MAXSIZE);
	if (error)
		return (error);

	/*
	 * When looked into the ethereal trace, 'nmblookup' command sets this
	 * flag. We will also set.
	 */
	mb_put_uint16be(mbp, rqp->nr_trnid);
	ofr = ((rqp->nr_opcode & 0x1F) << 11) |
	    ((rqp->nr_nmflags & 0x7F) << 4); /* rcode=0 */
	mb_put_uint16be(mbp, ofr);
	mb_put_uint16be(mbp, rqp->nr_qdcount);
	mb_put_uint16be(mbp, rqp->nr_ancount);
	mb_put_uint16be(mbp, rqp->nr_nscount);
	error = mb_put_uint16be(mbp, rqp->nr_arcount);
	if (rqp->nr_qdcount) {
		if (rqp->nr_qdcount > 1)
			return (EINVAL);
		(void) nb_name_encode(mbp, rqp->nr_qdname);
		mb_put_uint16be(mbp, rqp->nr_qdtype);
		error = mb_put_uint16be(mbp, rqp->nr_qdclass);
	}
	if (error)
		return (error);
	error = m_lineup(mbp->mb_top, &mbp->mb_top);
	if (error)
		return (error);
	if (ctx->nb_timo == 0)
		ctx->nb_timo = 1;	/* by default 1 second */
	return (0);
}

static int
nbns_rq_recv(struct nbns_rq *rqp)
{
	struct mbdata *mbp = &rqp->nr_rp;
	void *rpdata = mtod(mbp->mb_top, void *);
	fd_set rd, wr, ex;
	struct timeval tv;
	struct sockaddr_in sender;
	int s = rqp->nr_fd;
	int n;
	socklen_t len;

	FD_ZERO(&rd);
	FD_ZERO(&wr);
	FD_ZERO(&ex);
	FD_SET(s, &rd);

	tv.tv_sec = rqp->nr_nbd->nb_timo;
	tv.tv_usec = 0;

	n = select(s + 1, &rd, &wr, &ex, &tv);
	if (n == -1)
		return (-1);
	if (n == 0)
		return (ETIMEDOUT);
	if (FD_ISSET(s, &rd) == 0)
		return (ETIMEDOUT);
	len = sizeof (sender);
	n = recvfrom(s, rpdata, mbp->mb_top->m_maxlen, 0,
	    (struct sockaddr *)&sender, &len);
	if (n < 0)
		return (errno);
	mbp->mb_top->m_len = mbp->mb_count = n;
	rqp->nr_sender = sender;
	return (0);
}

static int
nbns_rq_opensocket(struct nbns_rq *rqp)
{
	struct sockaddr_in locaddr;
	int opt = 1, s;
	struct nb_ctx *ctx = rqp->nr_nbd;

	s = rqp->nr_fd = socket(AF_INET, SOCK_DGRAM, 0);
	if (s < 0)
		return (errno);
	if (ctx->nb_flags & NBCF_BC_ENABLE) {
		if (setsockopt(s, SOL_SOCKET, SO_BROADCAST, &opt,
		    sizeof (opt)) < 0)
			return (errno);
	}
	if (is_system_labeled())
		(void) setsockopt(s, SOL_SOCKET, SO_MAC_EXEMPT, &opt,
		    sizeof (opt));
	bzero(&locaddr, sizeof (locaddr));
	locaddr.sin_family = AF_INET;
	/* locaddr.sin_len = sizeof (locaddr); */
	if (bind(s, (struct sockaddr *)&locaddr, sizeof (locaddr)) < 0)
		return (errno);
	return (0);
}

static int
nbns_rq_send(struct nbns_rq *rqp, in_addr_t ina)
{
	struct sockaddr_in dest;
	struct mbdata *mbp = &rqp->nr_rq;
	int s = rqp->nr_fd;
	uint16_t ofr, ofr_save; /* opcode, nmflags, rcode */
	uint16_t *datap;
	uint8_t nmflags;
	int rc;

	bzero(&dest, sizeof (dest));
	dest.sin_family = AF_INET;
	dest.sin_port = htons(IPPORT_NETBIOS_NS);
	dest.sin_addr.s_addr = ina;

	if (ina == INADDR_BROADCAST) {
		/* Turn on the broadcast bit. */
		nmflags = rqp->nr_nmflags | NBNS_NMFLAG_BCAST;
		/*LINTED*/
		datap = mtod(mbp->mb_top, uint16_t *);
		ofr = ((rqp->nr_opcode & 0x1F) << 11) |
		    ((nmflags & 0x7F) << 4); /* rcode=0 */
		ofr_save = datap[1];
		datap[1] = htons(ofr);
	}

	rc = sendto(s, mtod(mbp->mb_top, char *), mbp->mb_count, 0,
	    (struct sockaddr *)&dest, sizeof (dest));

	if (ina == INADDR_BROADCAST) {
		/* Turn the broadcast bit back off. */
		datap[1] = ofr_save;
	}


	if (rc < 0)
		return (errno);

	return (0);
}

int
nbns_rq(struct nbns_rq *rqp)
{
	struct nb_ctx *ctx = rqp->nr_nbd;
	struct mbdata *mbp = &rqp->nr_rq;
	uint16_t ofr, rpid;
	int error, tries, maxretry;

	error = nbns_rq_opensocket(rqp);
	if (error)
		return (error);

	maxretry = rqp->nr_maxretry;
	for (tries = 0; tries < maxretry; tries++) {

		/*
		 * Minor hack: If nr_dest is set, send there only.
		 * Used by _getnodestatus, _resolvname redirects.
		 */
		if (rqp->nr_dest.s_addr) {
			error = nbns_rq_send(rqp, rqp->nr_dest.s_addr);
			if (error) {
				smb_error(dgettext(TEXT_DOMAIN,
				    "nbns error %d sending to %s"),
				    0, error, inet_ntoa(rqp->nr_dest));
			}
			goto do_recv;
		}

		if (ctx->nb_wins1) {
			error = nbns_rq_send(rqp, ctx->nb_wins1);
			if (error) {
				smb_error(dgettext(TEXT_DOMAIN,
				    "nbns error %d sending to wins1"),
				    0, error);
			}
		}

		if (ctx->nb_wins2 && (tries > 0)) {
			error = nbns_rq_send(rqp, ctx->nb_wins2);
			if (error) {
				smb_error(dgettext(TEXT_DOMAIN,
				    "nbns error %d sending to wins2"),
				    0, error);
			}
		}

		/*
		 * If broadcast is enabled, start broadcasting
		 * only after wins servers fail to respond, or
		 * immediately if no WINS servers configured.
		 */
		if ((ctx->nb_flags & NBCF_BC_ENABLE) &&
		    ((tries > 1) || (ctx->nb_wins1 == 0))) {
			error = nbns_rq_send(rqp, INADDR_BROADCAST);
			if (error) {
				smb_error(dgettext(TEXT_DOMAIN,
				    "nbns error %d sending broadcast"),
				    0, error);
			}
		}

		/*
		 * Wait for responses from ANY of the above.
		 */
do_recv:
		error = nbns_rq_recv(rqp);
		if (error == ETIMEDOUT)
			continue;
		if (error) {
			smb_error(dgettext(TEXT_DOMAIN,
			    "nbns recv error %d"),
			    0, error);
			return (error);
		}

		mbp = &rqp->nr_rp;
		if (mbp->mb_count < 12)
			return (NBERROR(NBERR_INVALIDRESPONSE));
		md_get_uint16be(mbp, &rpid);
		if (rpid != rqp->nr_trnid)
			return (NBERROR(NBERR_INVALIDRESPONSE));
		break;
	}
	if (tries == maxretry)
		return (NBERROR(NBERR_HOSTNOTFOUND));

	md_get_uint16be(mbp, &ofr);
	rqp->nr_rpnmflags = (ofr >> 4) & 0x7F;
	rqp->nr_rprcode = ofr & 0xf;
	if (rqp->nr_rprcode)
		return (NBERROR(rqp->nr_rprcode));
	md_get_uint16be(mbp, &rpid);	/* QDCOUNT */
	md_get_uint16be(mbp, &rqp->nr_rpancount);
	md_get_uint16be(mbp, &rqp->nr_rpnscount);
	md_get_uint16be(mbp, &rqp->nr_rparcount);
	return (0);
}
