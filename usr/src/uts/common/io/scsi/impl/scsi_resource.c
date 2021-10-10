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
 * Copyright (c) 1990, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#include <sys/scsi/scsi.h>
#include <sys/vtrace.h>

#define	A_TO_TRAN(ap)	((ap)->a_hba_tran)
#define	P_TO_ADDR(pkt)	(&((pkt)->pkt_address))
#define	P_TO_TRAN(pkt)	A_TO_TRAN(P_TO_ADDR(pkt))

/*
 * Callback id
 */
uintptr_t scsi_callback_id = 0;


/* scsi_alloc_consistent_buf(9F): */
struct buf *
scsi_alloc_consistent_buf(struct scsi_address *ap,
    struct buf *in_bp, size_t datalen, uint_t bflags,
    int (*callback)(caddr_t), caddr_t callback_arg)
{
	scsi_hba_tran_t		*tran = A_TO_TRAN(ap);
	struct buf		*bp;
	dev_info_t		*pdip;
	ddi_dma_attr_t		*attr;
	int			kmflag;
	size_t			rlen;
	extern ddi_dma_attr_t	scsi_alloc_attr;

	TRACE_0(TR_FAC_SCSI_RES, TR_SCSI_ALLOC_CONSISTENT_BUF_START,
	    "scsi_alloc_consistent_buf_start");

	if (tran == NULL)
		return (NULL);

	if (!in_bp) {
		kmflag = (callback == SLEEP_FUNC) ? KM_SLEEP : KM_NOSLEEP;
		if ((bp = getrbuf(kmflag)) == NULL) {
			goto no_resource;
		}
	} else {
		bp = in_bp;

		/* we are establishing a new buffer memory association */
		bp->b_flags &= ~(B_PAGEIO | B_PHYS | B_SHADOW | B_MVECTOR |
		    B_REMAPPED);
		bp->b_proc = NULL;
		bp->b_pages = NULL;
		bp->b_shadow = NULL;
	}

	/* limit bits that can be set by bflags argument */
	ASSERT(!(bflags & ~(B_READ | B_WRITE)));
	bflags &= (B_READ | B_WRITE);
	bp->b_un.b_addr = 0;

	if (datalen) {
		pdip = tran->tran_hba_dip;
		attr = pdip ? &tran->tran_dma_attr : &scsi_alloc_attr;

		/*
		 * use i_ddi_mem_alloc() for now until we have an interface to
		 * allocate memory for DMA which doesn't require a DMA handle.
		 * ddi_iopb_alloc() is obsolete and we want more flexibility in
		 * controlling the DMA address constraints.
		 */
		while (i_ddi_mem_alloc(pdip, attr, datalen,
		    ((callback == SLEEP_FUNC) ? 1 : 0), 0, NULL,
		    &bp->b_un.b_addr, &rlen, NULL) != DDI_SUCCESS) {
			if (callback == SLEEP_FUNC) {
				delay(drv_usectohz(10000));
			} else {
				if (!in_bp)
					freerbuf(bp);
				goto no_resource;
			}
		}
		bp->b_flags |= bflags;
	}
	bp->b_bcount = datalen;
	bp->b_resid = 0;

	TRACE_0(TR_FAC_SCSI_RES, TR_SCSI_ALLOC_CONSISTENT_BUF_END,
	    "scsi_alloc_consistent_buf_end");
	return (bp);

no_resource:
	if ((callback != NULL_FUNC) && (callback != SLEEP_FUNC))
		ddi_set_callback(callback, callback_arg, &scsi_callback_id);

	TRACE_0(TR_FAC_SCSI_RES,
	    TR_SCSI_ALLOC_CONSISTENT_BUF_RETURN1_END,
	    "scsi_alloc_consistent_buf_end (return1)");
	return (NULL);
}

/* scsi_free_consistent_buf(9F): */
void
scsi_free_consistent_buf(struct buf *bp)
{
	TRACE_0(TR_FAC_SCSI_RES, TR_SCSI_FREE_CONSISTENT_BUF_START,
	    "scsi_free_consistent_buf_start");
	if (bp == NULL)
		return;

	if (bp->b_un.b_addr)
		i_ddi_mem_free((caddr_t)bp->b_un.b_addr, NULL);

	bp_mapout(bp);
	freerbuf(bp);

	if (scsi_callback_id)
		ddi_run_callback(&scsi_callback_id);

	TRACE_0(TR_FAC_SCSI_RES, TR_SCSI_FREE_CONSISTENT_BUF_END,
	    "scsi_free_consistent_buf_end");
}

/* scsi_init_pkt(9F): target driver scsi_pkt(9S) allocation interface. */
struct scsi_pkt *
scsi_init_pkt(struct scsi_address *ap, struct scsi_pkt *pkt,
    struct buf *bp, int cmdlen, int statuslen, int tgtlen,
    int flags, int (*callback)(caddr_t), caddr_t callback_arg)
{
	scsi_hba_tran_t *tran = A_TO_TRAN(ap);
	int		(*sf)(caddr_t);

	TRACE_5(TR_FAC_SCSI_RES, TR_SCSI_INIT_PKT_START,
	    "scsi_init_pkt_start: addr %p pkt %p cmdlen %d "
	    "statuslen %d tgtlen %d", ap, pkt, cmdlen, statuslen, tgtlen);

#ifdef	__amd64
	if (flags & PKT_CONSISTENT_OLD) {
		flags &= ~PKT_CONSISTENT_OLD;
		flags |= PKT_CONSISTENT;
	}
#endif	/* __amd64 */

	/*
	 * NOTE: tran_init_pkt(9E) is limited to SLEEP_FUNC and NULL_FUNC
	 * callback values.  This means that HBA driver tran_init_pkt(9E)
	 * implementations (and supporting SCSA scsi_hba_pkt_* interfaces)
	 * don't need to deal with ddi_set_callback/ddi_run_callback.
	 */
	sf = (callback == SLEEP_FUNC) ? SLEEP_FUNC : NULL_FUNC;
	pkt = (*tran->tran_init_pkt) (ap, pkt, bp, cmdlen,
	    statuslen, tgtlen, flags, sf, NULL);

	if ((pkt == NULL) &&
	    (callback != NULL_FUNC) && (callback != SLEEP_FUNC))
		ddi_set_callback(callback, callback_arg, &scsi_callback_id);

	TRACE_1(TR_FAC_SCSI_RES, TR_SCSI_INIT_PKT_END,
	    "scsi_init_pkt_end: pkt %p", pkt);
	return (pkt);
}

/* scsi_destroy_pkt(9F): target driver scsi_pkt(9S) destroy interface. */
void
scsi_destroy_pkt(struct scsi_pkt *pkt)
{
	struct scsi_address	*ap = P_TO_ADDR(pkt);

	TRACE_1(TR_FAC_SCSI_RES, TR_SCSI_DESTROY_PKT_START,
	    "scsi_destroy_pkt_start: pkt %p", pkt);

	(*A_TO_TRAN(ap)->tran_destroy_pkt)(ap, pkt);

	if (scsi_callback_id)
		ddi_run_callback(&scsi_callback_id);

	TRACE_0(TR_FAC_SCSI_RES, TR_SCSI_DESTROY_PKT_END,
	    "scsi_destroy_pkt_end");
}

/* scsi_sync_pkt(9F): sync memory-scsi_pkt(9S) */
void
scsi_sync_pkt(struct scsi_pkt *pkt)
{
	struct scsi_address	*ap = P_TO_ADDR(pkt);

	if (pkt->pkt_state & STATE_XFERRED_DATA)
		(*A_TO_TRAN(ap)->tran_sync_pkt)(ap, pkt);
}

/* scsi_dmafree(9F): release DMA resources associated with a scsi_pkt(9S) */
/* scsi_dmafree(9F): Obsolete, use scsi_destroy_pkt(9F) */
/*
 * NOTE: Use of scsi_dmafree is still needed for mpxio to select another path
 * on retry of the same pkt (see sd.c for details).
 *
 * NOTE: This seems like a bug because other drivers that enumerate
 * under mpxio, like sgen or ses, don't seem to know about this.
 */
void
scsi_dmafree(struct scsi_pkt *pkt)
{
	register struct scsi_address	*ap = P_TO_ADDR(pkt);

	(*A_TO_TRAN(ap)->tran_dmafree)(ap, pkt);

	if (scsi_callback_id != 0)
		ddi_run_callback(&scsi_callback_id);
}

/* ========================== Obsolete interfaces. ========================== */
/* scsi_resalloc(9F): Obsolete, use scsi_init_pkt(9F). */
struct scsi_pkt *
scsi_resalloc(struct scsi_address *ap, int cmdlen, int statuslen,
    opaque_t dmatoken, int (*callback)())
{
	struct scsi_pkt	*pkt;
	scsi_hba_tran_t	*tran = A_TO_TRAN(ap);
	int		(*sf)(caddr_t);

	/*
	 * NOTE: tran_init_pkt(9E) is limited to SLEEP_FUNC and NULL_FUNC
	 * callback values.  This means that HBA driver tran_init_pkt(9E)
	 * implementations (and supporting SCSA scsi_hba_pkt_* interfaces)
	 * don't need to deal with ddi_set_callback/ddi_run_callback.
	 */
	sf = (callback == SLEEP_FUNC) ? SLEEP_FUNC : NULL_FUNC;
	pkt = (*tran->tran_init_pkt) (ap, NULL, (struct buf *)dmatoken,
	    cmdlen, statuslen, 0, 0, sf, NULL);

	if ((pkt == NULL) &&
	    (callback != NULL_FUNC) && (callback != SLEEP_FUNC))
		ddi_set_callback(callback, NULL, &scsi_callback_id);

	return (pkt);
}

/* scsi_pktalloc(9F): Obsolete, use scsi_init_pkt(9F). */
struct scsi_pkt *
scsi_pktalloc(struct scsi_address *ap, int cmdlen, int statuslen,
    int (*callback)())
{
	struct scsi_pkt	*pkt;
	scsi_hba_tran_t	*tran = A_TO_TRAN(ap);
	int		(*sf)(caddr_t);

	sf = (callback == SLEEP_FUNC) ? SLEEP_FUNC : NULL_FUNC;
	pkt = (*tran->tran_init_pkt) (ap, NULL, NULL, cmdlen,
	    statuslen, 0, 0, sf, NULL);

	if ((pkt == NULL) &&
	    (callback != NULL_FUNC) && (callback != SLEEP_FUNC))
		ddi_set_callback(callback, NULL, &scsi_callback_id);
	return (pkt);
}

/* scsi_dmaget(9F): Obsolete, use scsi_init_pkt(9F). */
struct scsi_pkt *
scsi_dmaget(struct scsi_pkt *pkt, opaque_t dmatoken, int (*callback)())
{
	struct scsi_pkt		*new_pkt;
	int			(*sf)(caddr_t);

	sf = (callback == SLEEP_FUNC) ? SLEEP_FUNC : NULL_FUNC;
	new_pkt = (*P_TO_TRAN(pkt)->tran_init_pkt) (P_TO_ADDR(pkt),
	    pkt, (struct buf *)dmatoken, 0, 0, 0, 0, sf, NULL);

	ASSERT(new_pkt == pkt || new_pkt == NULL);
	if ((new_pkt == NULL) &&
	    (callback != NULL_FUNC) && (callback != SLEEP_FUNC))
		ddi_set_callback(callback, NULL, &scsi_callback_id);
	return (new_pkt);
}

/* scsi_resfree(9F): Obsolete, use scsi_destroy_pkt(9F). */
void
scsi_resfree(struct scsi_pkt *pkt)
{
	struct scsi_address	*ap = P_TO_ADDR(pkt);

	(*A_TO_TRAN(ap)->tran_destroy_pkt)(ap, pkt);

	if (scsi_callback_id)
		ddi_run_callback(&scsi_callback_id);
}
