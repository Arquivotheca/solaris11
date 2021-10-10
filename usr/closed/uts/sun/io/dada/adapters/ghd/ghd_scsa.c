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
 * Copyright 2005 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"

#include <sys/dada/adapters/ghd/ghd.h>


/*
 * Local Function Prototypes
 */

static	struct scsi_pkt	*ghd_pktalloc(ccc_t *cccp, struct scsi_address *ap,
				int cmdlen, int statuslen, int tgtlen,
				int (*callback)(), caddr_t arg, int ccblen);

/*
 * Round up all allocations so that we can guarantee
 * long-long alignment.  This is the same alignment
 * provided by kmem_alloc().
 */
#define	ROUNDUP(x)	(((x) + 0x07) & ~0x07)

/*
 * Private wrapper for gcmd_t
 */

/*
 * round up the size so the HBA private area is on a 8 byte boundary
 */
#define	GW_PADDED_LENGTH	ROUNDUP(sizeof (gcmd_t))

typedef struct gcmd_padded_wrapper {
	union {
		gcmd_t	gw_gcmd;
		char	gw_pad[GW_PADDED_LENGTH];

	} gwrap;
} gwrap_t;



/*ARGSUSED*/
void
ghd_tran_sync_pkt(struct scsi_address *ap, struct scsi_pkt *pktp)
{
	gcmd_t *gcmdp = PKTP2GCMDP(pktp);
	int	status;

	if (gcmdp->cmd_dma_handle) {
		status = ddi_dma_sync(gcmdp->cmd_dma_handle, 0, 0,
			(gcmdp->cmd_dma_flags & DDI_DMA_READ) ?
			DDI_DMA_SYNC_FORCPU : DDI_DMA_SYNC_FORDEV);
		if (status != DDI_SUCCESS) {
			cmn_err(CE_WARN, "ghd_tran_sync_pkt() fail\n");
		}
	}
}


static struct scsi_pkt *
ghd_pktalloc(ccc_t	*cccp,
	struct scsi_address *ap,
	int	cmdlen,
	int	statuslen,
	int	tgtlen,
	int	(*callback)(),
	caddr_t	arg,
	int	ccblen)
{
	gtgt_t		*gtgtp =  ADDR2GTGTP(ap);
	struct scsi_pkt	*pktp;
	gcmd_t		*gcmdp;
	gwrap_t		*gwp;
	int		 gwrap_len;

	gwrap_len = sizeof (gwrap_t) + ROUNDUP(ccblen);

	/* allocate everything from kmem pool */
	pktp = scsi_hba_pkt_alloc(cccp->ccc_hba_dip, ap, cmdlen, statuslen,
				tgtlen, gwrap_len, callback, arg);
	if (pktp == NULL) {
		return (NULL);
	}

	/* get the ptr to the HBA specific buffer */
	gwp = (gwrap_t *)(pktp->pkt_ha_private);

	/* get the ptr to the GHD specific buffer */
	gcmdp = &gwp->gwrap.gw_gcmd;

	ASSERT((caddr_t)gwp == (caddr_t)gcmdp);

	/*
	 * save the ptr to HBA private area and initialize the rest
	 * of the gcmd_t members
	 */
	GHD_GCMD_INIT(gcmdp, (void *)(gwp + 1), gtgtp);

	/*
	 * save the the scsi_pkt ptr in gcmd_t.
	 */
	gcmdp->cmd_pktp = pktp;

	/*
	 * callback to the HBA driver so it can initalize its
	 * buffer and return the ptr to my cmd_t structure which is
	 * probably embedded in its buffer.
	 */

	if (!(*cccp->ccc_ccballoc)(gtgtp, gcmdp, cmdlen, statuslen, tgtlen,
					ccblen)) {
		scsi_hba_pkt_free(ap, pktp);
		return (NULL);
	}

	return (pktp);
}



/*
 * packet free
 */
/*ARGSUSED*/
void
ghd_pktfree(ccc_t		*cccp,
	struct scsi_address	*ap,
	struct scsi_pkt		*pktp)
{
	GDBG_PKT(("ghd_pktfree: cccp 0x%p ap 0x%p pktp 0x%p\n",
	    (void *)cccp, (void *)ap, (void *)pktp));

	/* free any extra resources allocated by the HBA */
	(*cccp->ccc_ccbfree)(PKTP2GCMDP(pktp));

	/* free the scsi_pkt and the GHD and HBA private areas */
	scsi_hba_pkt_free(ap, pktp);
}
