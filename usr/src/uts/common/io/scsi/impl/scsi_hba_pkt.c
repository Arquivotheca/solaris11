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
 * Copyright (c) 1994, 2011, Oracle and/or its affiliates. All rights reserved.
 */

/*
 * Generic SCSI Host Bus Adapter Packet allocation interfaces.
 */
#include <sys/scsi/scsi.h>

#define	A_TO_TRAN(ap)	((ap)->a_hba_tran)
#define	P_TO_ADDR(pkt)	(&((pkt)->pkt_address))
#define	P_TO_TRAN(pkt)	A_TO_TRAN(P_TO_ADDR(pkt))

/*
 * =============================================================================
 * METHOD-1: scsi_hba_pkt_alloc/scsi_hba_pkt_free
 * =============================================================================
 *
 * Private wrapper for scsi_pkt's allocated via scsi_hba_pkt_alloc()
 */
typedef struct scsi_pkt_wrapper {
	struct scsi_pkt		pw_scsi_pkt;
	int			pw_magic;
	int			pw_len;
} scsi_pkt_wrapper_t;

/*
 * Round up all allocations so that we can guarantee long-long
 * alignment.  This is the same alignment provided by kmem_alloc()
 * (i.e. KMEM_ALIGN).
 */
#define	PW_ALIGN	8
#define	PW_ROUNDUP(x)	(((x) + (PW_ALIGN - 1)) & ~(PW_ALIGN - 1))

#define	PW_MAGIC	0xa110ced		/* same as CPW_MAGIC */

/* scsi_hba_pkt_alloc(9F): allocate a scsi_pkt(9S). */
/*ARGSUSED*/
struct scsi_pkt *
scsi_hba_pkt_alloc(
	dev_info_t		*self,
	struct scsi_address	*ap,
	int			cmdlen,
	int			statuslen,
	int			tgtlen,
	int			hbalen,
	int			(*callback)(caddr_t arg),
	caddr_t			arg)
{
	struct scsi_pkt		*pkt;
	scsi_pkt_wrapper_t	*pw;
	caddr_t			p;
	int			apwlen, acmdlen, astatuslen, atgtlen, ahbalen;
	int			pktlen;

	ASSERT((callback == SLEEP_FUNC) || (callback == NULL_FUNC));
	if ((callback != SLEEP_FUNC) && (callback != NULL_FUNC))
		return (NULL);

	apwlen = PW_ROUNDUP(sizeof (scsi_pkt_wrapper_t));
	ahbalen = PW_ROUNDUP(hbalen);
	atgtlen = PW_ROUNDUP(tgtlen);
	astatuslen = PW_ROUNDUP(statuslen);
	acmdlen = PW_ROUNDUP(cmdlen);

	pktlen = apwlen + acmdlen + astatuslen + atgtlen + ahbalen;
	pw = kmem_zalloc(pktlen,
	    (callback == SLEEP_FUNC) ? KM_SLEEP : KM_NOSLEEP);
	if (pw == NULL) {
		ASSERT(callback == NULL_FUNC);
		return (NULL);
	}

	/*
	 * Set up our private info on this pkt
	 */
	pw->pw_magic = PW_MAGIC;		/* alloced correctly */
	pw->pw_len = pktlen;
	pkt = &pw->pw_scsi_pkt;
	p = (caddr_t)pw;
	p += apwlen;

	/*
	 * Set up pointers to private data areas, cdb, and status.
	 */
	if (hbalen > 0) {
		pkt->pkt_ha_private = (opaque_t)p;
		p += ahbalen;
	}
	if (tgtlen > 0) {
		pkt->pkt_private = (opaque_t)p;
		p += atgtlen;
	}
	if (statuslen > 0) {
		pkt->pkt_scbp = (uchar_t *)p;
		p += astatuslen;
	}
	if (cmdlen > 0) {
		pkt->pkt_cdbp = (uchar_t *)p;
	}

	/* Initialize the pkt's scsi_address. */
	pkt->pkt_address = *ap;

	/*
	 * NB: It may not be safe for drivers, esp target drivers, to depend
	 * on the following three fields being set until all the scsi_pkt
	 * allocation violations discussed in scsi_pkt.h are all resolved.
	 */
	pkt->pkt_tgtlen = tgtlen;
	pkt->pkt_scblen = statuslen;
	pkt->pkt_cdblen = cmdlen;

	return (pkt);
}

/* scsi_hba_pkt_free(9F): allocate a scsi_pkt(9S). */
/*ARGSUSED*/
void
scsi_hba_pkt_free(
	struct scsi_address	*ap,
	struct scsi_pkt		*pkt)
{
	kmem_free(pkt, ((scsi_pkt_wrapper_t *)pkt)->pw_len);
}

/*
 * =============================================================================
 * METHOD-2: tran_setup_pkt(9E) SCSA scsi_pkt cache with
 *	tran_setup_pkt/tran_teardown_pkt
 *	tran_pkt_constructor/tran_pkt_destructor
 * =============================================================================
 *
 * When using a single kmem_cache_alloc(), the layout of the scsi_pkt,
 * scsi_cache_pkt_wrapper_t, hba private data, cdb, tgt driver private data,
 * and status block are as shown below.
 *
 * This is a piece of contiguous memory starting from the first structure field
 * scsi_pkt in the scsi_cache_pkt_wrapper_t, followed by the hba private data,
 * pkt_cdbp, the tgt driver private data and pkt_scbp.
 *
 * |----------------------------|--------------------->
 * |	struct scsi_pkt		|
 * |	......			| scsi_cache_pkt_wrapper_t
 * |	cpw_flags		|
 * |----------------------------|<---------------------
 * |	hba private data	| tran->tran_hba_len
 * |----------------------------|
 * |	pkt_cdbp		| DEFAULT_CDBLEN
 * |----------------------------|
 * |	tgt private data	|(...future DEFAULT_TGTLEN)
 * |----------------------------|
 * |	pkt_scbp		| DEFAULT_SCBLEN
 * |----------------------------|
 *
 * If the actual data length of the cdb, or the tgt driver private data, or
 * the status block is bigger than the default data length, additional
 * kmem_alloc()s are performed to get requested space.
 */
#define	DEFAULT_CDBLEN	16
#define	DEFAULT_TGTLEN	0		/* ...future */
#define	DEFAULT_SCBLEN	(sizeof (struct scsi_arq_status))

/*
 * Private wrapper for scsi_pkt's allocated via scsi_cache_init_pkt()
 */
typedef struct scsi_cache_pkt_wrapper {
	struct scsi_pkt		cpw_pkt;
	int			cpw_magic;

	uint_t			cpw_total_xfer;
	uint_t			cpw_curwin;
	uint_t			cpw_totalwin;
	uint_t			cpw_granular;
	struct buf		*cpw_bp;
	ddi_dma_cookie_t	cpw_cookie;
	uint_t			cpw_flags;

	caddr_t			cpw_align_buffer;
	ddi_acc_handle_t	cpw_align_handle;
} scsi_cache_pkt_wrapper_t;

/* cpw_flags */
#define	CPW_HAVE_EXT_CDB	0x0001
#define	CPW_HAVE_EXT_TGT	0x0002
#define	CPW_HAVE_EXT_SCB	0x0004
#define	CPW_DID_TRAN_SETUP	0x0008
#define	CPW_NEED_BP_COPYOUT	0x0010
#define	CPW_BOUND		0x0020

/*
 * Round up all allocations so that we can guarantee long-long
 * alignment.  This is the same alignment provided by kmem_alloc()
 * (i.e. KMEM_ALIGN).
 */
#define	CPW_ALIGN	8
#define	CPW_ROUNDUP(x)	(((x) + (CPW_ALIGN - 1)) & ~(CPW_ALIGN - 1))

#define	CPW_MAGIC	0xa110ced		/* same as PW_MAGIC */

/* scsi_cache: return the bp associated with a method-2 scsi_pkt. */
struct buf *
scsi_pkt2bp(struct scsi_pkt *pkt)
{
	scsi_cache_pkt_wrapper_t	*cpw = (scsi_cache_pkt_wrapper_t *)pkt;

	ASSERT(P_TO_TRAN(pkt)->tran_setup_pkt);

	return (cpw->cpw_bp);
}

/* Return the size of the packet wrapper with needed default extensions. */
static int
scsi_cache_pkt_len(scsi_hba_tran_t *tran)
{
	int	len;

	len = CPW_ROUNDUP(sizeof (scsi_cache_pkt_wrapper_t));
	len += CPW_ROUNDUP(tran->tran_hba_len);
	if (tran->tran_hba_flags & SCSI_HBA_TRAN_CDB)
		len += CPW_ROUNDUP(DEFAULT_CDBLEN);
	len += CPW_ROUNDUP(DEFAULT_TGTLEN);
	if (tran->tran_hba_flags & SCSI_HBA_TRAN_SCB)
		len += CPW_ROUNDUP(DEFAULT_SCBLEN);
	return (len);
}

static int
scsi_cache_pkt_constructor(void *buf, void *arg, int kmflag)
{
	char				*ptr = buf;
	scsi_cache_pkt_wrapper_t	*cpw = buf;
	scsi_hba_tran_t			*tran = (scsi_hba_tran_t *)arg;
	struct scsi_pkt			*pkt = &(cpw->cpw_pkt);

	bzero(ptr, scsi_cache_pkt_len(tran));

	cpw->cpw_magic = CPW_MAGIC;	/* alloced correctly */
	ptr += CPW_ROUNDUP(sizeof (scsi_cache_pkt_wrapper_t));

	/*
	 * keep track of the granularity at the time this handle was
	 * allocated
	 */
	cpw->cpw_granular = tran->tran_dma_attr.dma_attr_granular;
	if (ddi_dma_alloc_handle(tran->tran_hba_dip, &tran->tran_dma_attr,
	    kmflag == KM_SLEEP ? SLEEP_FUNC: NULL_FUNC, NULL,
	    &pkt->pkt_handle) != DDI_SUCCESS)
		return (-1);

	pkt->pkt_ha_private = (opaque_t)ptr;
	ptr += CPW_ROUNDUP(tran->tran_hba_len);

	if (tran->tran_hba_flags & SCSI_HBA_TRAN_CDB) {
		pkt->pkt_cdbp = (opaque_t)ptr;
		ptr += CPW_ROUNDUP(DEFAULT_CDBLEN);
	}

#if	DEFAULT_TGTLEN
	pkt->pkt_private = (opaque_t)ptr;
	ptr += CPW_ROUNDUP(DEFAULT_TGTLEN);
#endif	/* DEFAULT_TGTLEN */

	if (tran->tran_hba_flags & SCSI_HBA_TRAN_SCB)
		pkt->pkt_scbp = (opaque_t)ptr;

	if (tran->tran_pkt_constructor)
		return ((*tran->tran_pkt_constructor)(pkt, arg, kmflag));
	return (0);
}

static void
scsi_cache_pkt_destructor(void *buf, void *arg)
{
	scsi_cache_pkt_wrapper_t	*cpw = buf;
	scsi_hba_tran_t			*tran = (scsi_hba_tran_t *)arg;
	struct scsi_pkt			*pkt = &(cpw->cpw_pkt);

	ASSERT(pkt->pkt_handle);
	ASSERT(cpw->cpw_magic == CPW_MAGIC);
	ASSERT((cpw->cpw_flags & CPW_BOUND) == 0);
	ASSERT(((tran->tran_hba_flags & SCSI_HBA_TRAN_SCB) == 0) ||
	    (pkt->pkt_scbp == (opaque_t)((char *)pkt +
	    CPW_ROUNDUP(sizeof (scsi_cache_pkt_wrapper_t)) +
	    CPW_ROUNDUP(tran->tran_hba_len) +
	    (((tran->tran_hba_flags & SCSI_HBA_TRAN_CDB) == 0) ?
	    0 : CPW_ROUNDUP(DEFAULT_CDBLEN)) +
	    CPW_ROUNDUP(DEFAULT_TGTLEN))));
#if	DEFAULT_TGTLEN
	ASSERT(pkt->pkt_private == (opaque_t)((char *)pkt +
	    CPW_ROUNDUP(sizeof (scsi_cache_pkt_wrapper_t)) +
	    CPW_ROUNDUP(tran->tran_hba_len) +
	    (((tran->tran_hba_flags & SCSI_HBA_TRAN_CDB) == 0) ?
	    0 : CPW_ROUNDUP(DEFAULT_CDBLEN))));
#endif	/* DEFAULT_TGTLEN */
	ASSERT(((tran->tran_hba_flags & SCSI_HBA_TRAN_CDB) == 0) ||
	    (pkt->pkt_cdbp == (opaque_t)((char *)pkt +
	    CPW_ROUNDUP(sizeof (scsi_cache_pkt_wrapper_t)) +
	    CPW_ROUNDUP(tran->tran_hba_len))));
	ASSERT(pkt->pkt_ha_private == (opaque_t)((char *)pkt +
	    CPW_ROUNDUP(sizeof (scsi_cache_pkt_wrapper_t))));

	if (tran->tran_pkt_destructor)
		(*tran->tran_pkt_destructor)(pkt, arg);

	ddi_dma_free_handle(&pkt->pkt_handle);
}

static int
scsi_cache_dmaget_attr(scsi_cache_pkt_wrapper_t *cpw)
{
	struct scsi_pkt		*pkt = &(cpw->cpw_pkt);
	int			status;
	ddi_dma_cookie_t	cookie;
	ddi_dma_impl_t		*hp;
	ddi_dma_cookie_t	*cp;
	int			num_segs;

	if (cpw->cpw_curwin != 0) {
		/*
		 * start the next window, and get its first cookie
		 */
		status = ddi_dma_getwin(pkt->pkt_handle,
		    cpw->cpw_curwin, &pkt->pkt_dma_offset,
		    &pkt->pkt_dma_len, &cookie, &pkt->pkt_numcookies);

		if (status != DDI_SUCCESS)
			return (0);
	}

	/*
	 * Start the Scatter/Gather loop.
	 */
	hp = (ddi_dma_impl_t *)pkt->pkt_handle;
	pkt->pkt_dma_len = 0;
	num_segs = 0;
	for (cp = hp->dmai_cookie - 1; ; cp++) {

		/* take care of the loop-bookkeeping */
		pkt->pkt_dma_len += cp->dmac_size;
		num_segs++;

		/*
		 * if this was the last cookie in the current window
		 * set the loop controls start the next window and
		 * exit so the HBA can do this partial transfer
		 */
		if (num_segs >= pkt->pkt_numcookies) {
			cpw->cpw_curwin++;
			break;
		}
	}
	cpw->cpw_total_xfer += pkt->pkt_dma_len;
	pkt->pkt_cookies = hp->dmai_cookie - 1;
	hp->dmai_cookie = cp;

	return (1);
}

static void
scsi_cache_dmafree_attr(struct scsi_pkt *pkt)
{
	scsi_cache_pkt_wrapper_t *cpw = (scsi_cache_pkt_wrapper_t *)pkt;

	if (cpw->cpw_flags & CPW_BOUND) {
		if (ddi_dma_unbind_handle(pkt->pkt_handle) != DDI_SUCCESS)
			cmn_err(CE_WARN, "scsi_dmafree_attr: "
			    "unbind handle failed");
		cpw->cpw_flags &= ~CPW_BOUND;
	}

	pkt->pkt_numcookies = 0;
	cpw->cpw_totalwin = 0;
}

static int
scsi_cache_bind(scsi_cache_pkt_wrapper_t *cpw,
    struct buf *bp, int dma_flags, int (*callback)())
{
	struct scsi_pkt	*pkt = &(cpw->cpw_pkt);
	int		rval;

	/* NOTE: caller has asserted that SLEEP_FUNC == DDI_DMA_SLEEP, etc */
	ASSERT(pkt->pkt_numcookies == 0);
	ASSERT(cpw->cpw_totalwin == 0);

	/* bind buf(9S) to the handle.  */
	rval = ddi_dma_buf_bind_handle(pkt->pkt_handle, bp, dma_flags,
	    callback, NULL, &cpw->cpw_cookie, &pkt->pkt_numcookies);

	switch (rval) {
	case DDI_DMA_MAPPED:
		cpw->cpw_totalwin = 1;
		return (1);			/* doio */

	case DDI_DMA_PARTIAL_MAP:
		/* enable first call to ddi_dma_getwin */
		if (ddi_dma_numwin(pkt->pkt_handle,
		    &cpw->cpw_totalwin) == DDI_SUCCESS)
			return (1);		/* doio */
		bp->b_error = 0;
		break;

	case DDI_DMA_NORESOURCES:
		bp->b_error = 0;
		break;

	case DDI_DMA_TOOBIG:
		bioerror(bp, EINVAL);
		break;

	case DDI_DMA_NOMAPPING:
	case DDI_DMA_INUSE:
	default:
		bioerror(bp, EFAULT);
		break;
	}
	return (0);
}

static int
scsi_cache_bindalign(scsi_cache_pkt_wrapper_t *cpw,
    struct buf *bp, int dma_flags, int (*callback)())
{
	struct scsi_pkt			*pkt = &(cpw->cpw_pkt);
	size_t				real_length;
	int				rval;
	extern ddi_device_acc_attr_t	scsi_acc_attr;

	/* NOTE: caller has asserted that SLEEP_FUNC == DDI_DMA_SLEEP, etc */
	ASSERT(pkt->pkt_numcookies == 0);
	ASSERT(cpw->cpw_totalwin == 0);

	/* allocate an aligned copy buffer */
	if (ddi_dma_mem_alloc(pkt->pkt_handle, bp->b_bcount, &scsi_acc_attr,
	    dma_flags, callback, NULL, &cpw->cpw_align_buffer, &real_length,
	    &cpw->cpw_align_handle) != DDI_SUCCESS)
		return (0);

	ASSERT(cpw->cpw_align_buffer && cpw->cpw_align_handle);
	if ((cpw->cpw_align_buffer == NULL) || (cpw->cpw_align_handle == NULL))
		return (0);

	/* bind the handle to the copy buffer */
	rval = ddi_dma_addr_bind_handle(pkt->pkt_handle, NULL,
	    cpw->cpw_align_buffer, bp->b_bcount, dma_flags,
	    callback, NULL, &cpw->cpw_cookie, &pkt->pkt_numcookies);

	switch (rval) {
	case DDI_DMA_MAPPED:
		cpw->cpw_totalwin = 1;
		goto doio;			/* doio */

	case DDI_DMA_PARTIAL_MAP:
		if (ddi_dma_numwin(pkt->pkt_handle,
		    &cpw->cpw_totalwin) == DDI_SUCCESS)
			goto doio;		/* doio */
		bp->b_error = 0;
		break;

	case DDI_DMA_NORESOURCES:
		bp->b_error = 0;
		break;

	case DDI_DMA_TOOBIG:
		bioerror(bp, EINVAL);
		break;

	case DDI_DMA_NOMAPPING:
	case DDI_DMA_INUSE:
	default:
		bioerror(bp, EFAULT);
		break;
	}

	ddi_dma_mem_free(&cpw->cpw_align_handle);
	cpw->cpw_align_handle = NULL;
	cpw->cpw_align_buffer = NULL;
	return (0);

doio:
	/*
	 * Before an unaligned memory->device transfer, we need to copy
	 * buf(9S) data into the aligned copy buffer using bp_copyin(9F).
	 */
	if (dma_flags & DDI_DMA_WRITE)
		(void) bp_copyin(bp, cpw->cpw_align_buffer, 0, bp->b_bcount);

	/*
	 * For an unaligned device->memory transfer, we need to mark the packet
	 * as needing a bp_copyout(9F). There are two ways this bp_copyout
	 * occurs:
	 *
	 *   o	PKT_CONSISTENT:
	 *	The bp_copyout needs to occur before calling the target
	 *	driver interrupt routine, the call is off
	 *	scsi_hba_pkt_comp-> tran_sync_pkt-> scsi_cache_sync_pkt.
	 *
	 *   o	non-PKT_CONSISTENT:
	 *	The call will occur off
	 *	scsi_destroy_pkt-> tran_destroy_pkt-> scsi_cache_destroy_pkt.
	 *
	 * The above minimizes the number of times that bp_copyout is called,
	 * but correctness requires all target drivers to call
	 * scsi_destroy_pkt(9F) before biodone(9F) for all io, unless the
	 * scsi_pkt(9S) associated with the io was allocated by a
	 * PKT_CONSISTENT call to scsi_init_pkt(9F).
	 */
	if (dma_flags & DDI_DMA_READ)
		cpw->cpw_flags |= CPW_NEED_BP_COPYOUT;
	return (1);
}

/*
 * scsi_cache: SCSA provides the tran_destroy_pkt(9E) implementation.
 * NOTE: don't confuse this with the kmem_cache scsi_cache_pkt_destructor.
 */
static void
scsi_cache_destroy_pkt(struct scsi_address *ap, struct scsi_pkt *pkt)
{
	scsi_cache_pkt_wrapper_t	*cpw = (scsi_cache_pkt_wrapper_t *)pkt;
	scsi_hba_tran_t			*tran = A_TO_TRAN(ap);
	struct buf			*bp;

	if (cpw->cpw_flags & CPW_DID_TRAN_SETUP) {
		cpw->cpw_flags &= ~CPW_DID_TRAN_SETUP;
		(*tran->tran_teardown_pkt)(pkt);
	}

	scsi_cache_dmafree_attr(pkt);

	/* sync/release align copy buffer */
	if (cpw->cpw_align_buffer) {
		ASSERT(cpw->cpw_align_handle);

		/*
		 * After a device->memory transfer involving an align copy
		 * buffer, if not already performed by scsi_cache_sync_pkt(),
		 * we need to transfer the new copy buffer contents back into
		 * the buf(9S) memory.
		 */
		bp = cpw->cpw_bp;
		if (bp && (cpw->cpw_flags & CPW_NEED_BP_COPYOUT))
			(void) bp_copyout(cpw->cpw_align_buffer, bp,
			    0, bp->b_bcount);

		ddi_dma_mem_free(&cpw->cpw_align_handle);
		cpw->cpw_align_handle = NULL;
		cpw->cpw_align_buffer = NULL;
	}

	/*
	 * if we allocated memory for anything that wouldn't fit, free
	 * the memory and restore the pointers
	 */
	if (cpw->cpw_flags & CPW_HAVE_EXT_SCB) {
		kmem_free(pkt->pkt_scbp, pkt->pkt_scblen);
		pkt->pkt_scblen = 0;
		pkt->pkt_scbp = (opaque_t)((char *)pkt +
		    CPW_ROUNDUP(sizeof (scsi_cache_pkt_wrapper_t)) +
		    CPW_ROUNDUP(tran->tran_hba_len) +
		    ((tran->tran_hba_flags & SCSI_HBA_TRAN_CDB) ?
		    CPW_ROUNDUP(DEFAULT_CDBLEN) : 0) +
		    CPW_ROUNDUP(DEFAULT_TGTLEN));
	}

	if (cpw->cpw_flags & CPW_HAVE_EXT_TGT) {
		kmem_free(pkt->pkt_private, pkt->pkt_tgtlen);
		pkt->pkt_tgtlen = 0;
#if	DEFAULT_TGTLEN
		pkt->pkt_private = (opaque_t)((char *)pkt +
		    CPW_ROUNDUP(sizeof (scsi_cache_pkt_wrapper_t)) +
		    CPW_ROUNDUP(tran->tran_hba_len) +
		    ((tran->tran_hba_flags & SCSI_HBA_TRAN_CDB) ?
		    CPW_ROUNDUP(DEFAULT_CDBLEN) : 0));
#else	/* DEFAULT_TGTLEN */
		pkt->pkt_private = NULL;
#endif	/* DEFAULT_TGTLEN */
	}

	if (cpw->cpw_flags & CPW_HAVE_EXT_CDB) {
		kmem_free(pkt->pkt_cdbp, pkt->pkt_cdblen);
		pkt->pkt_cdblen = 0;
		pkt->pkt_cdbp = (opaque_t)((char *)pkt +
		    CPW_ROUNDUP(sizeof (scsi_cache_pkt_wrapper_t)) +
		    CPW_ROUNDUP(A_TO_TRAN(ap)->tran_hba_len));
	}

	kmem_cache_free(A_TO_TRAN(ap)->tran_pkt_cache_ptr, cpw);
}

/* scsi_cache: SCSA provides the tran_init_pkt(9E) implementation */
/* ARGSUSED */
static struct scsi_pkt *
scsi_cache_init_pkt(struct scsi_address *ap, struct scsi_pkt *in_pkt,
    struct buf *bp, int cmdlen, int statuslen, int tgtlen,
    int flags, int (*callback)(caddr_t), caddr_t callback_arg)
{
	scsi_cache_pkt_wrapper_t	*cpw;
	struct scsi_pkt			*pkt = NULL;
	scsi_hba_tran_t			*tran = A_TO_TRAN(ap);
	int				kf;
	int				dma_flags;

	/* tran_init_pkt(9E) should only get SLEEP|NULL as callback. */
	ASSERT((callback == SLEEP_FUNC) || (callback == NULL_FUNC));
	if ((callback != SLEEP_FUNC) && (callback != NULL_FUNC))
		return (NULL);
	/*CONSTCOND*/
	ASSERT(SLEEP_FUNC == DDI_DMA_SLEEP);
	/*CONSTCOND*/
	ASSERT(NULL_FUNC == DDI_DMA_DONTWAIT);

	kf = (callback == SLEEP_FUNC) ? KM_SLEEP : KM_NOSLEEP;

	if (in_pkt == NULL) {
		cpw = kmem_cache_alloc(tran->tran_pkt_cache_ptr, kf);
		if (cpw == NULL)
			goto fail;

		cpw->cpw_flags = 0;
		pkt = &(cpw->cpw_pkt);
		pkt->pkt_address = *ap;

		/*
		 * target drivers should initialize pkt_comp and
		 * pkt_time, but sometimes they don't so initialize
		 * them here to be safe.
		 */
		pkt->pkt_flags = 0;
		pkt->pkt_time = 0;
		pkt->pkt_resid = 0;
		pkt->pkt_state = 0;
		pkt->pkt_statistics = 0;
		pkt->pkt_reason = 0;
		pkt->pkt_dma_offset = 0;
		pkt->pkt_dma_len = 0;
		pkt->pkt_dma_flags = 0;
		pkt->pkt_path_instance = 0;
		ASSERT(pkt->pkt_numcookies == 0);
		cpw->cpw_curwin = 0;
		cpw->cpw_totalwin = 0;
		cpw->cpw_total_xfer = 0;
		cpw->cpw_align_buffer = NULL;
		cpw->cpw_align_handle = NULL;

		pkt->pkt_cdblen = cmdlen;
		if ((tran->tran_hba_flags & SCSI_HBA_TRAN_CDB) &&
		    (cmdlen > CPW_ROUNDUP(DEFAULT_CDBLEN))) {
			cpw->cpw_flags |= CPW_HAVE_EXT_CDB;
			pkt->pkt_cdbp = kmem_alloc(cmdlen, kf);
			if (pkt->pkt_cdbp == NULL)
				goto fail;
		}

		pkt->pkt_tgtlen = tgtlen;
		if (tgtlen > CPW_ROUNDUP(DEFAULT_TGTLEN)) {
			cpw->cpw_flags |= CPW_HAVE_EXT_TGT;
			pkt->pkt_private = kmem_alloc(tgtlen, kf);
			if (pkt->pkt_private == NULL)
				goto fail;
		}

		pkt->pkt_scblen = statuslen;
		if ((tran->tran_hba_flags & SCSI_HBA_TRAN_SCB) &&
		    (statuslen > CPW_ROUNDUP(DEFAULT_SCBLEN))) {
			cpw->cpw_flags |= CPW_HAVE_EXT_SCB;
			pkt->pkt_scbp = kmem_alloc(statuslen, kf);
			if (pkt->pkt_scbp == NULL)
				goto fail;
		}

		if ((*tran->tran_setup_pkt) (pkt, callback, NULL) == -1)
			goto fail;
		cpw->cpw_flags |= CPW_DID_TRAN_SETUP;

		if (cmdlen)
			bzero((void *)pkt->pkt_cdbp, cmdlen);
		if (tgtlen)
			bzero((void *)pkt->pkt_private, tgtlen);
		if (statuslen)
			bzero((void *)pkt->pkt_scbp, statuslen);
	} else {
		pkt = in_pkt;
		cpw = (scsi_cache_pkt_wrapper_t *)pkt;
	}

	if (bp && bp->b_bcount) {
		/*
		 * We need to transfer data, so we bind dma resources
		 * for this packet.
		 *
		 * In general the pkt_handle is allocated by
		 * scsi_hba_pkt_constructor, and released by
		 * scsi_hba_pkt_destructor.  If granular changes
		 * however we do a free/alloc here.
		 */
		if (tran->tran_dma_attr.dma_attr_granular !=
		    cpw->cpw_granular) {
			ddi_dma_free_handle(&pkt->pkt_handle);
			if (ddi_dma_alloc_handle(tran->tran_hba_dip,
			    &tran->tran_dma_attr, callback, NULL,
			    &pkt->pkt_handle) != DDI_SUCCESS) {
				pkt->pkt_handle = NULL;
				goto fail;
			}
			cpw->cpw_granular =
			    tran->tran_dma_attr.dma_attr_granular;
		}

		if (pkt->pkt_numcookies == 0) {
			/*
			 * Initial bind:
			 *
			 * set dma flags; the "read" case must be first
			 * since B_WRITE isn't always be set for writes.
			 */
			dma_flags = 0;
			if (bp->b_flags & B_READ)
				dma_flags |= DDI_DMA_READ;
			else
				dma_flags |= DDI_DMA_WRITE;

			if (flags & PKT_CONSISTENT)
				dma_flags |= DDI_DMA_CONSISTENT;

			if (flags & PKT_DMA_PARTIAL)
				dma_flags |= DDI_DMA_PARTIAL;

#if	defined(__sparc)
			/*
			 * workaround for byte hole issue on psycho and
			 * schizo pre 2.1
			 */
			if ((dma_flags & DDI_DMA_READ) &&
			    !bioaligned(bp, 8,
			    BIOALIGNED_BEGIN | BIOALIGNED_END))
				dma_flags |= DDI_DMA_CONSISTENT;
#endif	/* defined(__sparc) */

			cpw->cpw_bp = bp;

			/*
			 * If ddi_dma_attr requires alignment and the io we are
			 * binding violates alignment constraints, we need to
			 * use a copy buffer, so call scsi_cache_bindalign().
			 */
			if ((tran->tran_dma_attr.dma_attr_flags &
			    DDI_DMA_ALIGNMENT_REQUIRED) &&
			    !bioaligned(bp,
			    tran->tran_dma_attr.dma_attr_align,
			    BIOALIGNED_BEGIN)) {
				if (!scsi_cache_bindalign(cpw, bp,
				    dma_flags, callback))
					goto fail;
			} else {
				if (!scsi_cache_bind(cpw, bp,
				    dma_flags, callback))
					goto fail;
			}

			pkt->pkt_dma_flags = dma_flags;
			cpw->cpw_flags |= CPW_BOUND;

			/* initialize the loop controls for additional calls */
			cpw->cpw_curwin = 0;
			cpw->cpw_total_xfer = 0;
		}

		if ((cpw->cpw_totalwin == 1) && (pkt->pkt_numcookies == 1)) {
			pkt->pkt_cookies = &cpw->cpw_cookie;
			pkt->pkt_dma_len = cpw->cpw_cookie.dmac_size;
			cpw->cpw_total_xfer += pkt->pkt_dma_len;
		} else if (!scsi_cache_dmaget_attr(cpw)) {
			scsi_cache_dmafree_attr(pkt);
			goto fail;
		}

		ASSERT((pkt->pkt_numcookies <=
		    tran->tran_dma_attr.dma_attr_sgllen) ||
		    (flags & PKT_DMA_PARTIAL));
		ASSERT(cpw->cpw_total_xfer <= bp->b_bcount);

		pkt->pkt_resid = bp->b_bcount - cpw->cpw_total_xfer;

		ASSERT((pkt->pkt_resid % cpw->cpw_granular) == 0);
	} else {
		/* !bp or no b_bcount */
		pkt->pkt_resid = 0;
	}
	return (pkt);

	/*
	 * Per tran_init_pkt(9E), if in_pkt is NULL then we are responsible
	 * for freeing pkt resources prior to returning NULL, otherwise the
	 * caller is responsible for scsi_destroy_pkt(9F)/tran_destroy_pkt(9E).
	 */
fail:	if ((in_pkt == NULL) && pkt)
		scsi_cache_destroy_pkt(ap, pkt);
	return (NULL);
}

/* scsi_cache: SCSA provides the tran_sync_pkt(9E) implementation */
/*ARGSUSED*/
static void
scsi_cache_sync_pkt(struct scsi_address *ap, struct scsi_pkt *pkt)
{
	scsi_cache_pkt_wrapper_t	*cpw = (scsi_cache_pkt_wrapper_t *)pkt;
	struct buf			*bp = cpw->cpw_bp;

	/* Perform sync for HW DMA transfer. */
	if (pkt->pkt_handle &&
	    (pkt->pkt_dma_flags & (DDI_DMA_WRITE | DDI_DMA_READ)))
		(void) ddi_dma_sync(pkt->pkt_handle,
		    pkt->pkt_dma_offset, pkt->pkt_dma_len,
		    (pkt->pkt_dma_flags & DDI_DMA_WRITE) ?
		    DDI_DMA_SYNC_FORDEV : DDI_DMA_SYNC_FORCPU);

	/*
	 * After a device->memory transfer involving an align copy buffer, we
	 * need to transfer the new copy buffer contents back into the
	 * buf(9S) memory.  NOTE: we condition the sync on DDI_DMA_CONSISTENT
	 * and DDI_DMA_READ instead of CPW_NEED_BP_COPYOUT to ensure that we
	 * still perform the sync on the last step of a PKT_DMA_PARTIAL io.
	 */
	if (bp && cpw->cpw_align_buffer &&
	    (pkt->pkt_dma_flags & DDI_DMA_CONSISTENT) &&
	    (pkt->pkt_dma_flags & DDI_DMA_READ)) {
		cpw->cpw_flags &= ~CPW_NEED_BP_COPYOUT;
		(void) bp_copyout(cpw->cpw_align_buffer, bp, 0, bp->b_bcount);
	}
}

/* scsi_cache: SCSA provides the tran_dmafree(9E) implementation */
/*ARGSUSED*/
static void
scsi_cache_dmafree(struct scsi_address *ap, struct scsi_pkt *pkt)
{
	ASSERT(pkt->pkt_handle != NULL);
	ASSERT(pkt->pkt_numcookies == 0 ||
	    ((scsi_cache_pkt_wrapper_t *)pkt)->cpw_flags & CPW_BOUND);

	scsi_cache_dmafree_attr(pkt);
}

static int
scsi_cache_hba_pkt_setup(scsi_hba_tran_t *tran)
{
	char	cache_name[96];

	ASSERT(tran->tran_init_pkt == NULL);
	ASSERT(tran->tran_destroy_pkt == NULL);
	if (tran->tran_init_pkt || tran->tran_destroy_pkt)
		return (0);		/* bad HBA driver plumbing */

	tran->tran_init_pkt	= scsi_cache_init_pkt;
	tran->tran_destroy_pkt	= scsi_cache_destroy_pkt;
	tran->tran_sync_pkt	= scsi_cache_sync_pkt;
	tran->tran_dmafree	= scsi_cache_dmafree;

	(void) snprintf(cache_name, sizeof (cache_name),
	    "pkt_cache_%s_%d", ddi_driver_name(tran->tran_hba_dip),
	    ddi_get_instance(tran->tran_hba_dip));

	tran->tran_pkt_cache_ptr = kmem_cache_create(cache_name,
	    scsi_cache_pkt_len(tran), CPW_ALIGN,
	    scsi_cache_pkt_constructor, scsi_cache_pkt_destructor,
	    NULL, tran, NULL, 0);
	return (1);
}

/*
 * =============================================================================
 * METHOD COMMON:
 * =============================================================================
 */
/*
 * This is a private interface invoked by scsi_hba_attach_setup(9F) to
 * initialize the scsi_pkt(9S) cache.
 */
int
scsa_hba_pkt_setup(scsi_hba_tran_t *tran)
{
	if (tran->tran_setup_pkt == NULL)
		return (1);				/* method-1 */

	return (scsi_cache_hba_pkt_setup(tran));	/* method-2 */
}

/*
 * Return 1 if the scsi_pkt used a proper allocator (either method).
 *
 * The DDI does not allow a driver to allocate it's own scsi_pkt(9S), a
 * driver should not have *any* compiled in dependencies on "sizeof (struct
 * scsi_pkt)". While this has been the case for many years, a number of
 * drivers have still not been fixed. This function can be used to detect
 * improperly allocated scsi_pkt structures, and produce messages identifying
 * drivers that need to be fixed.
 *
 * While drivers in violation are being fixed, this function can also
 * be used by the framework to detect packets that violated allocation
 * rules.
 *
 * NB: It is possible, but very unlikely, for this code to return a false
 * positive (finding correct magic, but for wrong reasons).  Careful
 * consideration is needed for callers using this interface to condition
 * access to newer scsi_pkt fields (those after pkt_reason).
 *
 * NB: As an aid to minimizing the amount of work involved in 'fixing' legacy
 * drivers that violate scsi_*(9S) allocation rules, private
 * scsi_pkt_size()/scsi_size_clean() functions are available (see their
 * implementation for details).
 *
 * *** Non-legacy use of scsi_pkt_size() is discouraged. ***
 *
 * NB: When supporting broken HBA drivers is not longer a concern, this
 * code should be removed.
 */
int
scsi_pkt_allocated_correctly(struct scsi_pkt *pkt)
{
	scsi_pkt_wrapper_t	*pw = (scsi_pkt_wrapper_t *)pkt;
	int	magic;
	major_t	major;
#ifdef	DEBUG
	int	*pm_pw, *pm_cpw;

	/*
	 * We are getting scsi packets from two 'correct' wrapper schemes,
	 * make sure we are looking at the same place in both to detect
	 * proper allocation.
	 */
	pm_pw = &((scsi_pkt_wrapper_t *)0)->pw_magic;
	pm_cpw = &((scsi_cache_pkt_wrapper_t *)0)->cpw_magic;
	ASSERT(pm_pw == pm_cpw);
#endif	/* DEBUG */

	/*
	 * Check to see if driver is scsi_size_clean(), assume it
	 * is using the scsi_pkt_size() interface everywhere it needs to
	 * if the driver indicates it is scsi_size_clean().
	 */
	major = ddi_driver_major(P_TO_TRAN(pkt)->tran_hba_dip);
	if (devnamesp[major].dn_flags & DN_SCSI_SIZE_CLEAN)
		return (1);		/* ok */

	/*
	 * Special case crossing a page boundary. If the scsi_pkt was not
	 * allocated correctly, then across a page boundary we have a
	 * fault hazard.
	 */
	if ((((uintptr_t)(&pw->pw_scsi_pkt)) & MMU_PAGEMASK) ==
	    (((uintptr_t)(&pw->pw_magic)) & MMU_PAGEMASK))
		magic = pw->pw_magic;	/* fastpath, no cross-page hazard */
	else {
		/* add protection for cross-page hazard */
		if (ddi_peek32((dev_info_t *)NULL,
		    &pw->pw_magic, &magic) == DDI_FAILURE)
			return (0);	/* violation */
	}

	/* properly allocated packet always has correct magic */
	return ((magic == PW_MAGIC) ? 1 : 0);
}
