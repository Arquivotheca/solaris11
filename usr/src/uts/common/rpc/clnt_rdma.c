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
 * Copyright (c) 2003, 2011, Oracle and/or its affiliates. All rights reserved.
 */
/* Copyright (c) 1983, 1984, 1985, 1986, 1987, 1988, 1989 AT&T */
/* All Rights Reserved */
/*
 * Portions of this source code were derived from Berkeley
 * 4.3 BSD under license from the Regents of the University of
 * California.
 */

#include <sys/param.h>
#include <sys/types.h>
#include <sys/user.h>
#include <sys/systm.h>
#include <sys/sysmacros.h>
#include <sys/errno.h>
#include <sys/kmem.h>
#include <sys/debug.h>
#include <sys/systm.h>
#include <sys/kstat.h>
#include <sys/t_lock.h>
#include <sys/ddi.h>
#include <sys/cmn_err.h>
#include <sys/time.h>
#include <sys/isa_defs.h>
#include <sys/zone.h>
#include <sys/sdt.h>

#include <rpc/types.h>
#include <rpc/xdr.h>
#include <rpc/auth.h>
#include <rpc/clnt.h>
#include <rpc/rpc_msg.h>
#include <rpc/rpc_rdma.h>
#include <nfs/nfs.h>
#include <nfs/nfs4_kprot.h>

static uint32_t rdma_bufs_rqst = RDMA_BUFS_RQST;

static int clnt_compose_rpcmsg(CLIENT *, rpcproc_t, rdma_buf_t *,
			    XDR *, xdrproc_t, caddr_t);
static int  clnt_compose_rdma_header(CONN *, CLIENT *, rdma_buf_t *,
		    XDR **, uint_t *);
static int clnt_setup_rlist(CONN *, XDR *, XDR *);
static int clnt_setup_wlist(CONN *, XDR *, XDR *, rdma_buf_t *);
static int clnt_setup_long_reply(CONN *, struct clist **, uint_t);
static void clnt_check_credit(CONN *);
static void clnt_return_credit(CONN *);
static void clnt_decode_long_reply(CONN *, struct clist *,
		struct clist *, XDR *, XDR **, struct clist *,
		struct clist *, uint_t, uint_t);

static void clnt_update_credit(CONN *, uint32_t);

static enum clnt_stat clnt_rdma_kcallit(CLIENT *, rpcproc_t, xdrproc_t,
    caddr_t, xdrproc_t, caddr_t, struct timeval);
static void	clnt_rdma_kabort(CLIENT *);
static void	clnt_rdma_kerror(CLIENT *, struct rpc_err *);
static bool_t	clnt_rdma_kfreeres(CLIENT *, xdrproc_t, caddr_t);
static void	clnt_rdma_kdestroy(CLIENT *);
static bool_t	clnt_rdma_kcontrol(CLIENT *, int, char *);
static int	clnt_rdma_ksettimers(CLIENT *, struct rpc_timers *,
    struct rpc_timers *, int, void(*)(int, int, caddr_t), caddr_t, uint32_t);

/*
 * Operations vector for RDMA based RPC
 */
static struct clnt_ops rdma_clnt_ops = {
	clnt_rdma_kcallit,	/* do rpc call */
	clnt_rdma_kabort,	/* abort call */
	clnt_rdma_kerror,	/* return error status */
	clnt_rdma_kfreeres,	/* free results */
	clnt_rdma_kdestroy,	/* destroy rpc handle */
	clnt_rdma_kcontrol,	/* the ioctl() of rpc */
	clnt_rdma_ksettimers,	/* set retry timers */
};

/*
 * The size of the preserialized RPC header information.
 */
#define	CKU_HDRSIZE	20
#define	CLNT_RDMA_SUCCESS 0
#define	CLNT_RDMA_FAIL (-1)

#define	AUTH_REFRESH_COUNT 2

#define	IS_RPCSEC_GSS(authh)			\
	(authh->cl_auth->ah_cred.oa_flavor == RPCSEC_GSS)

/*
 * Per RPC RDMA endpoint details
 */
typedef struct cku_private {
	CLIENT			cku_client;	/* client handle */
	rdma_mod_t		*cku_rd_mod;	/* underlying RDMA mod */
	void			*cku_rd_handle;	/* underlying RDMA device */
	struct netbuf		cku_srcaddr;	/* source address for retries */
	struct netbuf		cku_addr;	/* remote netbuf address */
	int			cku_addrfmly;	/* for finding addr_type */
	struct rpc_err		cku_err;	/* error status */
	struct cred		*cku_cred;	/* credentials */
	XDR			cku_outxdr;	/* xdr stream for output */
	uint32_t		cku_outsz;
	XDR			cku_inxdr;	/* xdr stream for input */
	char			cku_rpchdr[CKU_HDRSIZE+4]; /* rpc header */
	uint32_t		cku_xid;	/* current XID */
} cku_private_t;

#define	CLNT_RDMA_DELAY	10	/* secs to delay after a connection failure */
static int clnt_rdma_min_delay = CLNT_RDMA_DELAY;

struct {
	kstat_named_t	rccalls;
	kstat_named_t	rcbadcalls;
	kstat_named_t	rcbadxids;
	kstat_named_t	rctimeouts;
	kstat_named_t	rcnewcreds;
	kstat_named_t	rcbadverfs;
	kstat_named_t	rctimers;
	kstat_named_t	rccantconn;
	kstat_named_t	rcnomem;
	kstat_named_t	rcintrs;
	kstat_named_t	rclongrpcs;
	kstat_named_t	rcpreregbufs;
} rdmarcstat = {
	{ "calls",	KSTAT_DATA_UINT64 },
	{ "badcalls",	KSTAT_DATA_UINT64 },
	{ "badxids",	KSTAT_DATA_UINT64 },
	{ "timeouts",	KSTAT_DATA_UINT64 },
	{ "newcreds",	KSTAT_DATA_UINT64 },
	{ "badverfs",	KSTAT_DATA_UINT64 },
	{ "timers",	KSTAT_DATA_UINT64 },
	{ "cantconn",	KSTAT_DATA_UINT64 },
	{ "nomem",	KSTAT_DATA_UINT64 },
	{ "interrupts", KSTAT_DATA_UINT64 },
	{ "longrpc", 	KSTAT_DATA_UINT64 },
	{ "preregbufs",	KSTAT_DATA_UINT64 }
};

kstat_named_t *rdmarcstat_ptr = (kstat_named_t *)&rdmarcstat;
uint_t rdmarcstat_ndata = sizeof (rdmarcstat) / sizeof (kstat_named_t);

#ifdef DEBUG
int rdma_clnt_debug = 0;
#endif

#ifdef accurate_stats
extern kmutex_t rdmarcstat_lock;    /* mutex for rcstat updates */

#define	RCSTAT_INCR(x)			\
	mutex_enter(&rdmarcstat_lock);	\
	rdmarcstat.x.value.ui64++;	\
	mutex_exit(&rdmarcstat_lock);
#else
#define	RCSTAT_INCR(x)			\
	rdmarcstat.x.value.ui64++;
#endif

#define	ptoh(p)		(&((p)->cku_client))
#define	htop(h)		((cku_private_t *)((h)->cl_private))

uint_t
calc_length(uint_t len)
{
	len = RNDUP(len);

	if (len <= 64 * 1024) {
		if (len > 32 * 1024) {
			len = 64 * 1024;
		} else {
			if (len > 16 * 1024) {
				len = 32 * 1024;
			} else {
				if (len > 8 * 1024) {
					len = 16 * 1024;
				} else {
					len = 8 * 1024;
				}
			}
		}
	}
	return (len);
}
int
clnt_rdma_kcreate(char *proto, void *handle, struct netbuf *raddr, int family,
    rpcprog_t pgm, rpcvers_t vers, struct cred *cred, CLIENT **cl)
{
	CLIENT *h;
	struct cku_private *p;
	struct rpc_msg call_msg;
	rdma_registry_t *rp;

	ASSERT(INGLOBALZONE(curproc));

	if (cl == NULL)
		return (EINVAL);
	*cl = NULL;

	p = kmem_zalloc(sizeof (*p), KM_SLEEP);

	/*
	 * Find underlying RDMATF plugin
	 */
	rw_enter(&rdma_lock, RW_READER);
	rp = rdma_mod_head;
	while (rp != NULL) {
		if (strcmp(rp->r_mod->rdma_api, proto))
			rp = rp->r_next;
		else {
			p->cku_rd_mod = rp->r_mod;
			p->cku_rd_handle = handle;
			break;
		}
	}
	rw_exit(&rdma_lock);

	if (p->cku_rd_mod == NULL) {
		/*
		 * Should not happen.
		 * No matching RDMATF plugin.
		 */
		kmem_free(p, sizeof (struct cku_private));
		return (EINVAL);
	}

	h = ptoh(p);
	h->cl_ops = &rdma_clnt_ops;
	h->cl_private = (caddr_t)p;
	h->cl_auth = authkern_create();

	/* call message, just used to pre-serialize below */
	call_msg.rm_xid = 0;
	call_msg.rm_direction = CALL;
	call_msg.rm_call.cb_rpcvers = RPC_MSG_VERSION;
	call_msg.rm_call.cb_prog = pgm;
	call_msg.rm_call.cb_vers = vers;

	xdrmem_create(&p->cku_outxdr, p->cku_rpchdr, CKU_HDRSIZE, XDR_ENCODE);
	/* pre-serialize call message header */
	if (!xdr_callhdr(&p->cku_outxdr, &call_msg)) {
		XDR_DESTROY(&p->cku_outxdr);
		auth_destroy(h->cl_auth);
		kmem_free(p, sizeof (struct cku_private));
		return (EINVAL);
	}

	/*
	 * Set up the rpc information
	 */
	p->cku_cred = cred;
	p->cku_srcaddr.buf = kmem_zalloc(raddr->maxlen, KM_SLEEP);
	p->cku_srcaddr.maxlen = raddr->maxlen;
	p->cku_srcaddr.len = 0;
	p->cku_addr.buf = kmem_zalloc(raddr->maxlen, KM_SLEEP);
	p->cku_addr.maxlen = raddr->maxlen;
	p->cku_addr.len = raddr->len;
	bcopy(raddr->buf, p->cku_addr.buf, raddr->len);
	p->cku_addrfmly = family;

	*cl = h;
	return (0);
}

static void
clnt_rdma_kdestroy(CLIENT *h)
{
	struct cku_private *p = htop(h);

	kmem_free(p->cku_srcaddr.buf, p->cku_srcaddr.maxlen);
	kmem_free(p->cku_addr.buf, p->cku_addr.maxlen);
	kmem_free(p, sizeof (*p));
}

void
clnt_rdma_kinit(CLIENT *h, char *proto, void *handle, struct netbuf *raddr,
    struct cred *cred)
{
	struct cku_private *p = htop(h);
	rdma_registry_t *rp;

	ASSERT(INGLOBALZONE(curproc));
	/*
	 * Find underlying RDMATF plugin
	 */
	p->cku_rd_mod = NULL;
	rw_enter(&rdma_lock, RW_READER);
	rp = rdma_mod_head;
	while (rp != NULL) {
		if (strcmp(rp->r_mod->rdma_api, proto))
			rp = rp->r_next;
		else {
			p->cku_rd_mod = rp->r_mod;
			p->cku_rd_handle = handle;
			break;
		}

	}
	rw_exit(&rdma_lock);

	/*
	 * Set up the rpc information
	 */
	p->cku_cred = cred;
	p->cku_xid = 0;

	if (p->cku_addr.maxlen < raddr->len) {
		if (p->cku_addr.maxlen != 0 && p->cku_addr.buf != NULL)
			kmem_free(p->cku_addr.buf, p->cku_addr.maxlen);
		p->cku_addr.buf = kmem_zalloc(raddr->maxlen, KM_SLEEP);
		p->cku_addr.maxlen = raddr->maxlen;
	}

	p->cku_srcaddr.len = 0;

	p->cku_addr.len = raddr->len;
	bcopy(raddr->buf, p->cku_addr.buf, raddr->len);
	h->cl_ops = &rdma_clnt_ops;
}

static int
clnt_compose_rpcmsg(CLIENT *h, rpcproc_t procnum,
    rdma_buf_t *rpcmsg, XDR *xdrs,
    xdrproc_t xdr_args, caddr_t argsp)
{
	cku_private_t *p = htop(h);

	if (h->cl_auth->ah_cred.oa_flavor != RPCSEC_GSS) {
		/*
		 * Copy in the preserialized RPC header
		 * information.
		 */
		bcopy(p->cku_rpchdr, rpcmsg->addr, CKU_HDRSIZE);

		/*
		 * transaction id is the 1st thing in the output
		 * buffer.
		 */
		/* LINTED pointer alignment */
		(*(uint32_t *)(rpcmsg->addr)) = p->cku_xid;

		/* Skip the preserialized stuff. */
		XDR_SETPOS(xdrs, CKU_HDRSIZE);

		/* Serialize dynamic stuff into the output buffer. */
		if ((!XDR_PUTINT32(xdrs, (int32_t *)&procnum)) ||
		    (!AUTH_MARSHALL(h->cl_auth, xdrs, p->cku_cred)) ||
		    (!(*xdr_args)(xdrs, argsp))) {
			DTRACE_PROBE(krpc__e__clntrdma__rpcmsg__dynargs);
			return (CLNT_RDMA_FAIL);
		}
		p->cku_outsz = XDR_GETPOS(xdrs);
	} else {
		uint32_t *uproc = (uint32_t *)&p->cku_rpchdr[CKU_HDRSIZE];
		IXDR_PUT_U_INT32(uproc, procnum);
		(*(uint32_t *)(&p->cku_rpchdr[0])) = p->cku_xid;
		XDR_SETPOS(xdrs, 0);

		/* Serialize the procedure number and the arguments. */
		if (!AUTH_WRAP(h->cl_auth, (caddr_t)p->cku_rpchdr,
		    CKU_HDRSIZE+4, xdrs, xdr_args, argsp)) {
			if (rpcmsg->addr != xdrs->x_base) {
				rpcmsg->addr = xdrs->x_base;
				rpcmsg->len = xdr_getbufsize(xdrs);
			}
			DTRACE_PROBE(krpc__e__clntrdma__rpcmsg__procnum);
			return (CLNT_RDMA_FAIL);
		}
		/*
		 * If we had to allocate a new buffer while encoding
		 * then update the addr and len.
		 */
		if (rpcmsg->addr != xdrs->x_base) {
			rpcmsg->addr = xdrs->x_base;
			rpcmsg->len = xdr_getbufsize(xdrs);
		}

		p->cku_outsz = XDR_GETPOS(xdrs);
		DTRACE_PROBE1(krpc__i__compose__size__sec, int, p->cku_outsz)
	}

	return (CLNT_RDMA_SUCCESS);
}

static int
clnt_compose_rdma_header(CONN *conn, CLIENT *h, rdma_buf_t *clmsg,
    XDR **xdrs, uint_t *op)
{
	cku_private_t *p = htop(h);
	uint_t vers;
	uint32_t rdma_credit = rdma_bufs_rqst;

	vers = RPCRDMA_VERS;
	clmsg->type = SEND_BUFFER;

	if (rdma_buf_alloc(conn, clmsg)) {
		return (CLNT_RDMA_FAIL);
	}

	*xdrs = &p->cku_outxdr;
	xdrmem_create(*xdrs, clmsg->addr, clmsg->len, XDR_ENCODE);

	(*(uint32_t *)clmsg->addr) = p->cku_xid;
	XDR_SETPOS(*xdrs, sizeof (uint32_t));
	(void) xdr_u_int(*xdrs, &vers);
	(void) xdr_u_int(*xdrs, &rdma_credit);
	(void) xdr_u_int(*xdrs, op);

	return (CLNT_RDMA_SUCCESS);
}

static int
clnt_chunk_header_size(clist *clp)
{
	int	total_chunks = 0;
	clist	*cl;

	for (cl = clp; cl != NULL; cl = cl->c_next) {
		total_chunks++;
	}
	return ((RDMA_CHUNK_HDR * total_chunks) + RDMA_CHUNK_LIST_OVERHEAD);

}

/*
 * If xp_cl is NULL value, then the RPC payload will NOT carry
 * an RDMA READ chunk list, in this case we insert FALSE into
 * the XDR stream. Otherwise we use the clist and RDMA register
 * the memory and encode the clist into the outbound XDR stream.
 */
static int
clnt_setup_rlist(CONN *conn, XDR *xdrs, XDR *call_xdrp)
{
	int status;
	struct clist *rclp;
	int32_t xdr_flag = XDR_RDMA_RLIST_REG;

	XDR_CONTROL(call_xdrp, XDR_RDMA_GET_RLIST, &rclp);

	if (rclp != NULL) {
		status = clist_register(conn, rclp, CLIST_REG_SOURCE);
		if (status != RDMA_SUCCESS) {
			return (CLNT_RDMA_FAIL);
		}
		XDR_CONTROL(call_xdrp, XDR_RDMA_SET_FLAGS, &xdr_flag);
	}
	(void) xdr_do_clist(xdrs, &rclp);

	return (CLNT_RDMA_SUCCESS);
}

/*
 * Set a bit in the clist to instruct clist_deregister to force this memory
 * to be deregistered even if it is part of the RDMA_LONG_BUFFER cache of
 * registered buffers.
 */
void
clnt_set_force_dereg(XDR *call_xdrp)
{
	struct clist	*wlist;
	int		flags;

	XDR_CONTROL(call_xdrp, XDR_RDMA_GET_FLAGS, &flags);
	if (flags & XDR_RDMA_WLIST_PREREG) {
		XDR_CONTROL(call_xdrp, XDR_RDMA_GET_WLIST, &wlist);
		if (wlist)
			wlist->c_flags |= CLIST_FORCE_DEREG;
	}
}

/*
 * If xp_wcl is NULL value, then the RPC payload will NOT carry
 * an RDMA WRITE chunk list, in this case we insert FALSE into
 * the XDR stream. Otherwise we use the clist and  RDMA register
 * the memory and encode the clist into the outbound XDR stream.
 */
static int
clnt_setup_wlist(CONN *conn, XDR *xdrs, XDR *call_xdrp, rdma_buf_t *rndbuf)
{
	int status;
	struct clist fcl;
	struct clist *wlist, *rndcl, *ncl;
	int wlen, rndlen;
	int32_t xdr_flag;

	XDR_CONTROL(call_xdrp, XDR_RDMA_GET_WLIST, &wlist);
	XDR_CONTROL(call_xdrp, XDR_RDMA_GET_FLAGS, &xdr_flag);

	if (wlist != NULL) {
		/*
		 * If we are sending a non 4-byte alligned length
		 * the server will roundup the length to 4-byte
		 * boundary. In such a case, a trailing chunk is
		 * added to take any spill over roundup bytes.
		 */
		wlen = clist_len(wlist);
		rndlen = (roundup(wlen, BYTES_PER_XDR_UNIT) - wlen);

		if ((xdr_flag & XDR_RDMA_WLIST_PREREG) == 0) {

			if (rndlen) {
				rndcl = clist_alloc();
				/*
				 * calc_length() will allocate a PAGESIZE
				 * buffer below.
				 */
				rndcl->c_len = calc_length(rndlen);
				rndcl->rb_longbuf.type = RDMA_LONG_BUFFER;
				rndcl->rb_longbuf.len = rndcl->c_len;
				if (rdma_buf_alloc(conn,
				    &rndcl->rb_longbuf)) {
					clist_free(rndcl);
					return (CLNT_RDMA_FAIL);
				}

				/* Roundup buffer freed back in caller */
				*rndbuf = rndcl->rb_longbuf;

				rndcl->u.c_daddr3 = rndcl->rb_longbuf.addr;
				rndcl->c_next = NULL;
				rndcl->c_dmemhandle =
				    rndcl->rb_longbuf.handle;
				/* Skip to the last chunk in the list */
				ncl = wlist;
				while (ncl->c_next)
					ncl = ncl->c_next;
				ncl->c_next = rndcl;
			}
			status = clist_register(conn, wlist, CLIST_REG_DST);
			if (status != RDMA_SUCCESS) {
				rdma_buf_free(conn, rndbuf);
				bzero(rndbuf, sizeof (rdma_buf_t));
				return (CLNT_RDMA_FAIL);
			}
			xdr_flag = XDR_RDMA_WLIST_REG;
			XDR_CONTROL(call_xdrp, XDR_RDMA_SET_FLAGS,
			    &xdr_flag);
		} else {
			/*
			 * Copy the first clist, its buffer
			 * maps the entire wlist.
			 */
			fcl = *wlist;
			fcl.c_next = NULL;
			status = clist_register(conn, &fcl,
			    CLIST_REG_DST);
			if (status != RDMA_SUCCESS) {
				/*
				 * Free the preregistered buffer.
				 */
				rdma_buf_free(conn, &fcl.rb_longbuf);
				return (CLNT_RDMA_FAIL);
			}
			wlist->c_regtype = CLIST_REG_DST;
			wlist->c_dmemhandle = fcl.c_dmemhandle;
			wlist->c_dsynchandle = fcl.c_dsynchandle;

			/*
			 * Set the registered memory handles for the
			 * rest of the chunks the same as the first.
			 */
			ncl = wlist->c_next;
			while (ncl) {
				ncl->c_dmemhandle = fcl.c_dmemhandle;
				ncl->c_dsynchandle =
				    fcl.c_dsynchandle;
				ncl = ncl->c_next;
			}

			fcl.c_len = wlen + rndlen;
#if DEBUG
			if (rndlen)
				ASSERT(wlen + rndlen <=
				    wlist->rb_longbuf.len);
#endif
			/*
			 * Set wlist to point to just the first
			 * clist as it maps the entire buffer.
			 * This is all we need xdr encoded in the
			 * rdmaheader.
			 */
			wlist = &fcl;
			RCSTAT_INCR(rcpreregbufs);

		}
	}

	if (!xdr_encode_wlist(xdrs, wlist)) {
		if (rndlen) {
			rdma_buf_free(conn, rndbuf);
			bzero(rndbuf, sizeof (rdma_buf_t));
		}
		/*
		 * Deregister the preregistered buffer, and free it here.
		 */
		if (xdr_flag & XDR_RDMA_WLIST_PREREG) {
			(void) clist_deregister(conn, &fcl);
			rdma_buf_free(conn, &fcl.rb_longbuf);
		}
		return (CLNT_RDMA_FAIL);
	}

	return (CLNT_RDMA_SUCCESS);
}

static int
clnt_setup_long_reply(CONN *conn, struct clist **clpp, uint_t length)
{
	if (length == 0) {
		*clpp = NULL;
		return (CLNT_RDMA_SUCCESS);
	}

	*clpp = clist_alloc();

	(*clpp)->rb_longbuf.len = calc_length(length);
	(*clpp)->rb_longbuf.type = RDMA_LONG_BUFFER;

	if (rdma_buf_alloc(conn, &((*clpp)->rb_longbuf))) {
		clist_free(*clpp);
		*clpp = NULL;
		return (CLNT_RDMA_FAIL);
	}

	(*clpp)->u.c_daddr3 = (*clpp)->rb_longbuf.addr;
	(*clpp)->c_len = (*clpp)->rb_longbuf.len;
	(*clpp)->c_next = NULL;
	(*clpp)->c_dmemhandle = (*clpp)->rb_longbuf.handle;

	if (clist_register(conn, *clpp, CLIST_REG_DST)) {
		DTRACE_PROBE(krpc__e__clntrdma__longrep_regbuf);
		rdma_buf_free(conn, &((*clpp)->rb_longbuf));
		clist_free(*clpp);
		*clpp = NULL;
		return (CLNT_RDMA_FAIL);
	}

	return (CLNT_RDMA_SUCCESS);
}

/* ARGSUSED */
static enum clnt_stat
clnt_rdma_kcallit(CLIENT *h, rpcproc_t procnum, xdrproc_t xdr_args,
    caddr_t argsp, xdrproc_t xdr_results, caddr_t resultsp,
    struct timeval wait)
{
	cku_private_t *p = htop(h);

	int 	try_call_again;
	int	refresh_attempt = AUTH_REFRESH_COUNT;
	int 	status;
	int 	msglen;

	XDR	*call_xdrp, callxdr; /* for xdrrdma encoding the RPC call */
	XDR	*reply_xdrp, replyxdr; /* for xdrrdma decoding the RPC reply */
	XDR 	*rdmahdr_o_xdrs, *rdmahdr_i_xdrs;

	struct rpc_msg 	reply_msg;
	rdma_registry_t	*m;

	struct clist *cl_sendlist;
	struct clist *cl_recvlist;
	struct clist *cl;
	struct clist *cl_rpcmsg;
	struct clist *cl_rdma_reply;
	struct clist *cl_rpcreply_wlist;
	struct clist *cl_long_reply;

	rdma_buf_t  rndup;

	uint_t vers;
	uint_t op;
	uint_t off;
	int xdr_flag;
	uint32_t seg_array_len;
	uint_t long_reply_len;
	uint_t rpcsec_gss;
	uint_t gss_i_or_p;

	CONN *conn = NULL;
	rdma_buf_t clmsg;
	rdma_buf_t rpcmsg;
	rdma_chunkinfo_lengths_t rcil;

	clock_t	ticks;
	bool_t wlist_exists_reply;

	uint32_t rdma_credit = rdma_bufs_rqst;

	RCSTAT_INCR(rccalls);

call_again:

	bzero(&clmsg, sizeof (clmsg));
	bzero(&rpcmsg, sizeof (rpcmsg));
	bzero(&rndup, sizeof (rndup));
	try_call_again = 0;
	cl_sendlist = NULL;
	cl_recvlist = NULL;
	cl = NULL;
	cl_rpcmsg = NULL;
	cl_rdma_reply = NULL;
	call_xdrp = NULL;
	reply_xdrp = NULL;
	wlist_exists_reply  = FALSE;
	cl_rpcreply_wlist = NULL;
	cl_long_reply = NULL;
	rcil.rcil_len = 0;
	rcil.rcil_len_alt = 0;
	long_reply_len = 0;
	xdr_flag = 0;

	rw_enter(&rdma_lock, RW_READER);
	m = (rdma_registry_t *)p->cku_rd_handle;
	if (m->r_mod_state == RDMA_MOD_INACTIVE) {
		/*
		 * If we didn't find a matching RDMA module in the registry
		 * then there is no transport.
		 */
		rw_exit(&rdma_lock);
		p->cku_err.re_status = RPC_CANTSEND;
		p->cku_err.re_errno = EIO;
		ticks = clnt_rdma_min_delay * drv_usectohz(1000000);
		if (h->cl_nosignal == TRUE) {
			delay(ticks);
		} else {
			if (delay_sig(ticks) == EINTR) {
				p->cku_err.re_status = RPC_INTR;
				p->cku_err.re_errno = EINTR;
			}
		}
		return (RPC_CANTSEND);
	}
	/*
	 * Get unique xid
	 */
	if (p->cku_xid == 0)
		p->cku_xid = alloc_xid();

	status = RDMA_GET_CONN(p->cku_rd_mod->rdma_ops, &p->cku_srcaddr,
	    &p->cku_addr, p->cku_addrfmly, p->cku_rd_handle, &conn);
	rw_exit(&rdma_lock);

	/*
	 * If there is a problem with the connection reflect the issue
	 * back to the higher level to address, we MAY delay for a short
	 * period so that we are kind to the transport.
	 */
	if (conn == NULL) {
		/*
		 * Connect failed to server. Could be because of one
		 * of several things. In some cases we don't want
		 * the caller to retry immediately - delay before
		 * returning to caller.
		 */
		switch (status) {
		case RDMA_TIMEDOUT:
			/*
			 * Already timed out. No need to delay
			 * some more.
			 */
			p->cku_err.re_status = RPC_TIMEDOUT;
			p->cku_err.re_errno = ETIMEDOUT;
			break;
		case RDMA_INTR:
			/*
			 * Failed because of an signal. Very likely
			 * the caller will not retry.
			 */
			p->cku_err.re_status = RPC_INTR;
			p->cku_err.re_errno = EINTR;
			break;
		default:
			/*
			 * All other failures - server down or service
			 * down or temporary resource failure. Delay before
			 * returning to caller.
			 */
			ticks = clnt_rdma_min_delay * drv_usectohz(1000000);
			p->cku_err.re_status = RPC_CANTCONNECT;
			p->cku_err.re_errno = EIO;

			if (h->cl_nosignal == TRUE) {
				delay(ticks);
			} else {
				if (delay_sig(ticks) == EINTR) {
					p->cku_err.re_status = RPC_INTR;
					p->cku_err.re_errno = EINTR;
				}
			}
			break;
		}

		return (p->cku_err.re_status);
	}

	if (p->cku_srcaddr.maxlen < conn->c_laddr.len) {
		if ((p->cku_srcaddr.maxlen != 0) &&
		    (p->cku_srcaddr.buf != NULL))
			kmem_free(p->cku_srcaddr.buf, p->cku_srcaddr.maxlen);
		p->cku_srcaddr.buf = kmem_zalloc(conn->c_laddr.maxlen,
		    KM_SLEEP);
		p->cku_srcaddr.maxlen = conn->c_laddr.maxlen;
	}

	p->cku_srcaddr.len = conn->c_laddr.len;
	bcopy(conn->c_laddr.buf, p->cku_srcaddr.buf, conn->c_laddr.len);

	clnt_check_credit(conn);

	status = CLNT_RDMA_FAIL;

	rpcsec_gss = gss_i_or_p = FALSE;

	if (IS_RPCSEC_GSS(h)) {
		rpcsec_gss = TRUE;
		if (rpc_gss_get_service_type(h->cl_auth) ==
		    rpc_gss_svc_integrity ||
		    rpc_gss_get_service_type(h->cl_auth) ==
		    rpc_gss_svc_privacy)
			gss_i_or_p = TRUE;
	}

	/*
	 * Try a regular RDMA message if RPCSEC_GSS is not being used
	 * or if RPCSEC_GSS is being used for authentication only.
	 */
	if (rpcsec_gss == FALSE ||
	    (rpcsec_gss == TRUE && gss_i_or_p == FALSE)) {
		/*
		 * Grab a send buffer for the request.  Try to
		 * encode it to see if it fits. If not, then it
		 * needs to be sent in a chunk.
		 */
		rpcmsg.type = SEND_BUFFER;
		if (rdma_buf_alloc(conn, &rpcmsg)) {
			DTRACE_PROBE(krpc__e__clntrdma__callit_nobufs);
			goto done;
		}

		/* First try to encode into regular send buffer */
		op = RDMA_MSG;

		call_xdrp = &callxdr;

		xdrrdma_create(call_xdrp, rpcmsg.addr, rpcmsg.len,
		    rdma_minchunk, NULL, XDR_ENCODE, conn);

		status = clnt_compose_rpcmsg(h, procnum, &rpcmsg, call_xdrp,
		    xdr_args, argsp);

		if (status != CLNT_RDMA_SUCCESS) {
			/* Clean up from previous encode attempt */
			rdma_buf_free(conn, &rpcmsg);
			XDR_DESTROY(call_xdrp);
		} else {
			XDR_CONTROL(call_xdrp, XDR_RDMA_GET_CHUNK_LEN, &rcil);
		}
	}

	/* If the encode didn't work, then try a NOMSG */
	if (status != CLNT_RDMA_SUCCESS) {

		msglen = CKU_HDRSIZE + BYTES_PER_XDR_UNIT + MAX_AUTH_BYTES +
		    xdr_sizeof(xdr_args, argsp);

		msglen = calc_length(msglen);

		/* pick up the lengths for the reply buffer needed */
		(void) xdrrdma_sizeof(xdr_args, argsp, 0,
		    &rcil.rcil_len, &rcil.rcil_len_alt);

		/*
		 * Construct a clist to describe the CHUNK_BUFFER
		 * for the rpcmsg.
		 */
		cl_rpcmsg = clist_alloc();
		cl_rpcmsg->c_len = msglen;
		cl_rpcmsg->rb_longbuf.type = RDMA_LONG_BUFFER;
		cl_rpcmsg->rb_longbuf.len = msglen;
		if (rdma_buf_alloc(conn, &cl_rpcmsg->rb_longbuf)) {
			clist_free(cl_rpcmsg);
			goto done;
		}
		cl_rpcmsg->w.c_saddr3 = cl_rpcmsg->rb_longbuf.addr;

		op = RDMA_NOMSG;
		call_xdrp = &callxdr;

		xdrrdma_create(call_xdrp, cl_rpcmsg->rb_longbuf.addr,
		    cl_rpcmsg->rb_longbuf.len, 0,
		    cl_rpcmsg, XDR_ENCODE, conn);

		status = clnt_compose_rpcmsg(h, procnum, &cl_rpcmsg->rb_longbuf,
		    call_xdrp, xdr_args, argsp);

		DTRACE_PROBE2(krpc__i__clntrdma__callit__longbuf, int, status,
		    int, msglen);
		if (status != CLNT_RDMA_SUCCESS) {
			p->cku_err.re_status = RPC_CANTENCODEARGS;
			p->cku_err.re_errno = EIO;
			DTRACE_PROBE(krpc__e__clntrdma__callit__composemsg);
			goto done;
		}
	}

	/*
	 * During the XDR_ENCODE we may have "allocated" an RDMA READ or
	 * RDMA WRITE clist.
	 *
	 * First pull the RDMA READ chunk list from the XDR private
	 * area to keep it handy.
	 */
	XDR_CONTROL(call_xdrp, XDR_RDMA_GET_RLIST, &cl);

	if (gss_i_or_p) {
		long_reply_len = rcil.rcil_len + rcil.rcil_len_alt;
		long_reply_len += MAX_AUTH_BYTES;
	} else {
		long_reply_len = rcil.rcil_len;
	}

	/*
	 * Update the chunk size information for the Long RPC msg.
	 */
	if (cl && op == RDMA_NOMSG)
		cl->c_len = p->cku_outsz;

	/*
	 * Prepare the RDMA header. On success xdrs will hold the result
	 * of xdrmem_create() for a SEND_BUFFER.
	 */
	status = clnt_compose_rdma_header(conn, h, &clmsg,
	    &rdmahdr_o_xdrs, &op);

	if (status != CLNT_RDMA_SUCCESS) {
		p->cku_err.re_status = RPC_CANTSEND;
		p->cku_err.re_errno = EIO;
		RCSTAT_INCR(rcnomem);
		DTRACE_PROBE(krpc__e__clntrdma__callit__nobufs2);
		goto done;
	}

	/*
	 * Now insert the RDMA READ list iff present
	 */
	status = clnt_setup_rlist(conn, rdmahdr_o_xdrs, call_xdrp);
	if (status != CLNT_RDMA_SUCCESS) {
		DTRACE_PROBE(krpc__e__clntrdma__callit__clistreg);
		rdma_buf_free(conn, &clmsg);
		p->cku_err.re_status = RPC_CANTSEND;
		p->cku_err.re_errno = EIO;
		goto done;
	}

	/*
	 * Setup RDMA WRITE chunk list for nfs read operation
	 * other operations will have a NULL which will result
	 * as a NULL list in the XDR stream. NOTE: If this fails
	 * it will cleanup preregistered buffers appropriately.
	 */
	status = clnt_setup_wlist(conn, rdmahdr_o_xdrs, call_xdrp, &rndup);
	if (status != CLNT_RDMA_SUCCESS) {
		rdma_buf_free(conn, &clmsg);
		p->cku_err.re_status = RPC_CANTSEND;
		p->cku_err.re_errno = EIO;
		goto done;
	}

	/*
	 * If we used preregistered buffers, save that info here so we can
	 * mark this in the reply xdr.
	 */
	XDR_CONTROL(call_xdrp, XDR_RDMA_GET_FLAGS, &xdr_flag);

	/*
	 * If NULL call and RPCSEC_GSS, provide a chunk such that
	 * large responses can flow back to the client.
	 * If RPCSEC_GSS with integrity or privacy is in use, get chunk.
	 */
	if ((procnum == 0 && rpcsec_gss == TRUE) ||
	    (rpcsec_gss == TRUE && gss_i_or_p == TRUE))
		long_reply_len += 1024;

	/*
	 * If we need a long reply buffer, make sure the size of it
	 * includes space for the rpc header and the rdma header.
	 * The size of the RDMA header can vary based on the number of
	 * chunk lists in it.  Inlcuded in the rdma header size calculation are:
	 * CKU_HDRSIZE - size for the RPC header portion.
	 * -  size for all read chunks in rdma header.
	 * - size for all write chunks in rdma header.
	 * RDMA_HDR - The is a generic rdma header size with no chunk lists
	 * used here to account for the generic rdma header (xid, version
	 * etc.).
	 * RDMA_CHUNK_HDR - Size of one chunk list information in the
	 * rdma header.  Needed here to account for the reply chunk itself
	 * in this list.
	 * NOTE:  It is up to the xdr encode functions to specify that it needs
	 * space in the reply buffer and how much by calling xdrrdma_control
	 * specifying XDR_RDMA_ADD_CHUNK.  If a large enough reply buffer is
	 * not provided, the server can not send the results.  In the
	 * solaris servers, watch for the dtrace probe
	 * "krpc_e_svcrdma_ksend_noreplycl" which indicates the reply buffer
	 * was not big enough.
	 */
	if (long_reply_len) {
		clist	*rcl;
		long_reply_len += CKU_HDRSIZE;
		XDR_CONTROL(call_xdrp, XDR_RDMA_GET_RLIST, &rcl);
		long_reply_len += clnt_chunk_header_size(rcl);
		XDR_CONTROL(call_xdrp, XDR_RDMA_GET_WLIST, &rcl);
		long_reply_len += clnt_chunk_header_size(rcl);
		long_reply_len += RDMA_HDR + RDMA_CHUNK_HDR;
	}

	status = clnt_setup_long_reply(conn, &cl_long_reply, long_reply_len);

	DTRACE_PROBE2(krpc__i__clntrdma__callit__longreply, int, status,
	    int, long_reply_len);

	if (status != CLNT_RDMA_SUCCESS) {
		rdma_buf_free(conn, &clmsg);
		p->cku_err.re_status = RPC_CANTSEND;
		p->cku_err.re_errno = EIO;
		goto done;
	}

	/*
	 * XDR encode the RDMA_REPLY write chunk
	 */
	seg_array_len = (cl_long_reply ? 1 : 0);
	(void) xdr_encode_reply_wchunk(rdmahdr_o_xdrs, cl_long_reply,
	    seg_array_len);

	/*
	 * Construct a clist in "sendlist" that represents what we
	 * will push over the wire.
	 *
	 * Start with the RDMA header and clist (if any)
	 */
	clist_add(&cl_sendlist, 0, XDR_GETPOS(rdmahdr_o_xdrs), &clmsg.handle,
	    clmsg.addr, NULL, NULL);

	/*
	 * Put the RPC call message in  sendlist if small RPC
	 */
	if (op == RDMA_MSG) {
		clist_add(&cl_sendlist, 0, p->cku_outsz, &rpcmsg.handle,
		    rpcmsg.addr, NULL, NULL);
	} else {
		/* Long RPC already in chunk list */
		RCSTAT_INCR(rclongrpcs);
	}

	/*
	 * Set up a reply buffer ready for the reply
	 */
	status = rdma_clnt_postrecv(conn, p->cku_xid);
	if (status != RDMA_SUCCESS) {
		rdma_buf_free(conn, &clmsg);
		p->cku_err.re_status = RPC_CANTSEND;
		p->cku_err.re_errno = EIO;
		goto done;
	}

	/*
	 * sync the memory for dma
	 */
	if (cl != NULL) {
		status = clist_syncmem(conn, cl, CLIST_REG_SOURCE);
		if (status != RDMA_SUCCESS) {
			(void) rdma_clnt_postrecv_remove(conn, p->cku_xid);
			rdma_buf_free(conn, &clmsg);
			p->cku_err.re_status = RPC_CANTSEND;
			p->cku_err.re_errno = EIO;
			goto done;
		}
	}

	/*
	 * Send the RDMA Header and RPC call message to the server
	 */
	status = RDMA_SEND(conn, cl_sendlist, p->cku_xid);
	if (status != RDMA_SUCCESS) {
		(void) rdma_clnt_postrecv_remove(conn, p->cku_xid);
		p->cku_err.re_status = RPC_CANTSEND;
		p->cku_err.re_errno = EIO;
		clmsg.addr = NULL;
		if (rpcmsg.type == SEND_BUFFER)
			rpcmsg.addr = NULL;
		clnt_set_force_dereg(call_xdrp);
		goto done;
	}

	/*
	 * RDMA plugin now owns the send msg buffers.
	 * Clear them out and don't free them.
	 */
	clmsg.addr = NULL;
	if (rpcmsg.type == SEND_BUFFER)
		rpcmsg.addr = NULL;

	/*
	 * Recv rpc reply
	 */
	status = RDMA_RECV(conn, &cl_recvlist, p->cku_xid);

	/*
	 * Now check recv status
	 */
	if (status != 0) {
		if (status == RDMA_INTR) {
			p->cku_err.re_status = RPC_INTR;
			p->cku_err.re_errno = EINTR;
			RCSTAT_INCR(rcintrs);
		} else if (status == RPC_TIMEDOUT) {
			p->cku_err.re_status = RPC_TIMEDOUT;
			p->cku_err.re_errno = ETIMEDOUT;
			RCSTAT_INCR(rctimeouts);
		} else {
			p->cku_err.re_status = RPC_CANTRECV;
			p->cku_err.re_errno = EIO;
		}
		/*
		 * Set the flag to deregister the buffer.  When
		 * wlist in the call_xdrp is destroyed, the buffer will then
		 * be deregistered to make sure the
		 * server no longer has access to it.
		 */
		clnt_set_force_dereg(call_xdrp);
		goto done;
	}

	/*
	 * Process the reply message.
	 *
	 * First the chunk list (if any)
	 */
	rdmahdr_i_xdrs = &(p->cku_inxdr);
	xdrmem_create(rdmahdr_i_xdrs,
	    (caddr_t)(uintptr_t)cl_recvlist->w.c_saddr3,
	    cl_recvlist->c_len, XDR_DECODE);

	/*
	 * Treat xid as opaque (xid is the first entity
	 * in the rpc rdma message).
	 * Skip xid and set the xdr position accordingly.
	 */
	XDR_SETPOS(rdmahdr_i_xdrs, sizeof (uint32_t));
	(void) xdr_u_int(rdmahdr_i_xdrs, &vers);
	(void) xdr_u_int(rdmahdr_i_xdrs, &rdma_credit);
	(void) xdr_u_int(rdmahdr_i_xdrs, &op);
	(void) xdr_do_clist(rdmahdr_i_xdrs, &cl);

	clnt_update_credit(conn, rdma_credit);

	wlist_exists_reply = FALSE;
	if (! xdr_decode_wlist(rdmahdr_i_xdrs, &cl_rpcreply_wlist,
	    &wlist_exists_reply)) {
		DTRACE_PROBE(krpc__e__clntrdma__callit__wlist_decode);
		p->cku_err.re_status = RPC_CANTDECODERES;
		p->cku_err.re_errno = EIO;
		goto done;
	}

	/*
	 * The server shouldn't have sent a RDMA_SEND that
	 * the client needs to RDMA_WRITE a reply back to
	 * the server.  So silently ignoring what the
	 * server returns in the rdma_reply section of the
	 * header.
	 */
	(void) xdr_decode_reply_wchunk(rdmahdr_i_xdrs, &cl_rdma_reply);
	off = xdr_getpos(rdmahdr_i_xdrs);

	clnt_decode_long_reply(conn, cl_long_reply,
	    cl_rdma_reply, &replyxdr, &reply_xdrp,
	    cl, cl_recvlist, op, off);

	if (reply_xdrp == NULL)
		goto done;

	if (wlist_exists_reply) {
		XDR_CONTROL(reply_xdrp, XDR_RDMA_SET_WLIST, cl_rpcreply_wlist);
		if (xdr_flag & XDR_RDMA_WLIST_PREREG) {
			/*
			 * This is a reply from a read that used a pre-
			 * registered buffer.  The reply wlist does not
			 * have this RDMA_LONG_BUFFER defined within it.
			 * Set it here so the xdr function has access to it.
			 */
			XDR_CONTROL(reply_xdrp, XDR_RDMA_SET_PREREGBUF,
			    call_xdrp);
		}
	}

	reply_msg.rm_direction = REPLY;
	reply_msg.rm_reply.rp_stat = MSG_ACCEPTED;
	reply_msg.acpted_rply.ar_stat = SUCCESS;
	reply_msg.acpted_rply.ar_verf = _null_auth;

	/*
	 *  xdr_results will be done in AUTH_UNWRAP.
	 */
	reply_msg.acpted_rply.ar_results.where = NULL;
	reply_msg.acpted_rply.ar_results.proc = xdr_void;

	/*
	 * Decode and validate the response.
	 */
	if (xdr_replymsg(reply_xdrp, &reply_msg)) {
		enum clnt_stat re_status;

		_seterr_reply(&reply_msg, &(p->cku_err));

		re_status = p->cku_err.re_status;
		if (re_status == RPC_SUCCESS) {
			/*
			 * Reply is good, check auth.
			 */
			if (!AUTH_VALIDATE(h->cl_auth,
			    &reply_msg.acpted_rply.ar_verf)) {
				p->cku_err.re_status = RPC_AUTHERROR;
				p->cku_err.re_why = AUTH_INVALIDRESP;
				RCSTAT_INCR(rcbadverfs);
				DTRACE_PROBE(
				    krpc__e__clntrdma__callit__authvalidate);
			} else if (!AUTH_UNWRAP(h->cl_auth, reply_xdrp,
			    xdr_results, resultsp)) {
				p->cku_err.re_status = RPC_CANTDECODERES;
				p->cku_err.re_errno = EIO;
				DTRACE_PROBE(
				    krpc__e__clntrdma__callit__authunwrap);
			}
		} else {
			/* set errno in case we can't recover */
			if (re_status != RPC_VERSMISMATCH &&
			    re_status != RPC_AUTHERROR &&
			    re_status != RPC_PROGVERSMISMATCH)
				p->cku_err.re_errno = EIO;

			if (re_status == RPC_AUTHERROR) {
				if ((refresh_attempt > 0) &&
				    AUTH_REFRESH(h->cl_auth, &reply_msg,
				    p->cku_cred)) {
					refresh_attempt--;
					try_call_again = 1;
					goto done;
				}

				try_call_again = 0;

				/*
				 * We have used the client handle to
				 * do an AUTH_REFRESH and the RPC status may
				 * be set to RPC_SUCCESS; Let's make sure to
				 * set it to RPC_AUTHERROR.
				 */
				p->cku_err.re_status = RPC_AUTHERROR;

				/*
				 * Map recoverable and unrecoverable
				 * authentication errors to appropriate
				 * errno
				 */
				switch (p->cku_err.re_why) {
				case AUTH_BADCRED:
				case AUTH_BADVERF:
				case AUTH_INVALIDRESP:
				case AUTH_TOOWEAK:
				case AUTH_FAILED:
				case RPCSEC_GSS_NOCRED:
				case RPCSEC_GSS_FAILED:
					p->cku_err.re_errno = EACCES;
					break;
				case AUTH_REJECTEDCRED:
				case AUTH_REJECTEDVERF:
				default:
					p->cku_err.re_errno = EIO;
					break;
				}
			}
			DTRACE_PROBE1(krpc__e__clntrdma__callit__rpcfailed,
			    int, p->cku_err.re_why);
		}
	} else {
		p->cku_err.re_status = RPC_CANTDECODERES;
		p->cku_err.re_errno = EIO;
		DTRACE_PROBE(krpc__e__clntrdma__callit__replymsg);
	}

done:
	clnt_return_credit(conn);

	rdma_buf_free(conn, &rpcmsg);

	if (cl_sendlist != NULL)
		clist_free(cl_sendlist);

	/*
	 * If rpc reply is in a chunk, free it now.
	 */
	if (cl_long_reply) {
		(void) clist_deregister(conn, cl_long_reply);
		rdma_buf_free(conn, &cl_long_reply->rb_longbuf);
		clist_free(cl_long_reply);
	}

	if (call_xdrp)
		XDR_DESTROY(call_xdrp);

	if (rndup.rb_private) {
		rdma_buf_free(conn, &rndup);
	}

	if (reply_xdrp) {
		(void) xdr_rpc_free_verifier(reply_xdrp, &reply_msg);
		XDR_DESTROY(reply_xdrp);
	}

	if (cl_rdma_reply) {
		clist_free(cl_rdma_reply);
	}

	if (cl_recvlist) {
		rdma_buf_t	recvmsg = {0};
		recvmsg.addr = (caddr_t)(uintptr_t)cl_recvlist->w.c_saddr3;
		recvmsg.type = RECV_BUFFER;
		RDMA_BUF_FREE(conn, &recvmsg);
		clist_free(cl_recvlist);
	}

	RDMA_REL_CONN(conn);

	if (try_call_again)
		goto call_again;

	if (p->cku_err.re_status != RPC_SUCCESS) {
		RCSTAT_INCR(rcbadcalls);
	}
	return (p->cku_err.re_status);
}


static void
clnt_decode_long_reply(CONN *conn,
    struct clist *cl_long_reply,
    struct clist *cl_rdma_reply, XDR *xdrs,
    XDR **rxdrp, struct clist *cl,
    struct clist *cl_recvlist,
    uint_t  op, uint_t off)
{
	if (op != RDMA_NOMSG) {
		DTRACE_PROBE1(krpc__i__longrepl__rdmamsg__len,
		    int, cl_recvlist->c_len - off);
		xdrrdma_create(xdrs,
		    (caddr_t)(uintptr_t)(cl_recvlist->w.c_saddr3 + off),
		    cl_recvlist->c_len - off, 0, cl, XDR_DECODE, conn);
		*rxdrp = xdrs;
		return;
	}

	/* op must be RDMA_NOMSG */
	if (cl) {
		DTRACE_PROBE(krpc__e__clntrdma__declongreply__serverreadlist);
		return;
	}

	if (cl_long_reply->u.c_daddr) {
		DTRACE_PROBE1(krpc__i__longrepl__rdmanomsg__len,
		    int, cl_rdma_reply->c_len);

		xdrrdma_create(xdrs, (caddr_t)cl_long_reply->u.c_daddr3,
		    cl_rdma_reply->c_len, 0, NULL, XDR_DECODE, conn);

		*rxdrp = xdrs;
	}
}

static void
clnt_return_credit(CONN *conn)
{
	rdma_clnt_cred_ctrl_t *cc_info = &conn->rdma_conn_cred_ctrl_u.c_clnt_cc;

	mutex_enter(&conn->c_lock);
	cc_info->clnt_cc_in_flight_ops--;
	cv_signal(&cc_info->clnt_cc_cv);
	mutex_exit(&conn->c_lock);
}

static void
clnt_update_credit(CONN *conn, uint32_t rdma_credit)
{
	rdma_clnt_cred_ctrl_t *cc_info = &conn->rdma_conn_cred_ctrl_u.c_clnt_cc;

	/*
	 * If the granted has not altered, avoid taking the
	 * mutex, to essentially do nothing..
	 */
	if (cc_info->clnt_cc_granted_ops == rdma_credit)
		return;
	/*
	 * Get the granted number of buffers for credit control.
	 */
	mutex_enter(&conn->c_lock);
	cc_info->clnt_cc_granted_ops = rdma_credit;
	mutex_exit(&conn->c_lock);
}

static void
clnt_check_credit(CONN *conn)
{
	rdma_clnt_cred_ctrl_t *cc_info = &conn->rdma_conn_cred_ctrl_u.c_clnt_cc;

	/*
	 * Make sure we are not going over our allowed buffer use
	 * (and make sure we have gotten a granted value before).
	 */
	mutex_enter(&conn->c_lock);
	while (cc_info->clnt_cc_in_flight_ops >= cc_info->clnt_cc_granted_ops &&
	    cc_info->clnt_cc_granted_ops != 0) {
		/*
		 * Client has maxed out its granted buffers due to
		 * credit control.  Current handling is to block and wait.
		 */
		cv_wait(&cc_info->clnt_cc_cv, &conn->c_lock);
	}
	cc_info->clnt_cc_in_flight_ops++;
	mutex_exit(&conn->c_lock);
}

/* ARGSUSED */
static void
clnt_rdma_kabort(CLIENT *h)
{
}

static void
clnt_rdma_kerror(CLIENT *h, struct rpc_err *err)
{
	struct cku_private *p = htop(h);
	*err = p->cku_err;
}

static bool_t
clnt_rdma_kfreeres(CLIENT *h, xdrproc_t xdr_res, caddr_t res_ptr)
{
	struct cku_private *p = htop(h);
	XDR *xdrs;

	xdrs = &(p->cku_outxdr);
	xdrs->x_op = XDR_FREE;
	return ((*xdr_res)(xdrs, res_ptr));
}

/* ARGSUSED */
static bool_t
clnt_rdma_kcontrol(CLIENT *h, int cmd, char *arg)
{
	return (TRUE);
}

/* ARGSUSED */
static int
clnt_rdma_ksettimers(CLIENT *h, struct rpc_timers *t, struct rpc_timers *all,
	int minimum, void(*feedback)(int, int, caddr_t), caddr_t arg,
	uint32_t xid)
{
	RCSTAT_INCR(rctimers);
	return (0);
}

int
rdma_reachable(int addr_type, struct netbuf *addr, struct knetconfig **knconf)
{
	rdma_registry_t	*rp;
	void *handle = NULL;
	struct knetconfig *knc;
	char *pf, *p;
	rdma_stat status;
	int error = 0;

	if (!INGLOBALZONE(curproc))
		return (-1);

	/*
	 * modload the RDMA plugins if not already done.
	 */
	error = rdma_modload();
	if (error)
		return (-1);

	rw_enter(&rdma_lock, RW_READER);
	rp = rdma_mod_head;
	while (rp != NULL) {
		if (rp->r_mod_state == RDMA_MOD_INACTIVE) {
			rp = rp->r_next;
			continue;
		}
		status = RDMA_REACHABLE(rp->r_mod->rdma_ops, addr_type, addr,
		    &handle);
		if (status == RDMA_SUCCESS) {
			knc = kmem_zalloc(sizeof (struct knetconfig),
			    KM_SLEEP);
			knc->knc_semantics = NC_TPI_RDMA;
			pf = kmem_alloc(KNC_STRSIZE, KM_SLEEP);
			p = kmem_alloc(KNC_STRSIZE, KM_SLEEP);
			if (addr_type == AF_INET)
				(void) strncpy(pf, NC_INET, KNC_STRSIZE);
			else if (addr_type == AF_INET6)
				(void) strncpy(pf, NC_INET6, KNC_STRSIZE);
			pf[KNC_STRSIZE - 1] = '\0';

			(void) strncpy(p, rp->r_mod->rdma_api, KNC_STRSIZE);
			p[KNC_STRSIZE - 1] = '\0';

			knc->knc_protofmly = pf;
			knc->knc_proto = p;
			knc->knc_rdev = (dev_t)rp;
			*knconf = knc;
			rw_exit(&rdma_lock);
			return (0);
		}
		rp = rp->r_next;
	}
	rw_exit(&rdma_lock);
	return (-1);
}
