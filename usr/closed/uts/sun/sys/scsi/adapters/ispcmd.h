/*
 * Copyright (c) 1994, 2008, Oracle and/or its affiliates. All rights reserved.
 */

#ifndef _SYS_SCSI_ADAPTERS_ISPCMD_H
#define	_SYS_SCSI_ADAPTERS_ISPCMD_H

#include <sys/scsi/scsi_types.h>

#ifdef	__cplusplus
extern "C" {
#endif

/* preferred pkt_private length */
#define	PKT2CMD(pkt)		((struct isp_cmd *)(pkt)->pkt_ha_private)
#define	CMD2PKT(sp)		((sp)->cmd_pkt)

/*
 * isp_cmd is selectively zeroed.  During packet allocation, some
 * fields need zeroing, others need to be initialized.
 *
 * the preferred cdb size is 12. isp is a scsi2 HBA driver and rarely
 * needs 16 byte cdb's
 */
struct isp_cmd {
	struct isp_request	cmd_isp_request;
	struct isp_response	cmd_isp_response;

	struct scsi_pkt		*cmd_pkt;	/* needs to be INITialized */
	struct isp_cmd		*cmd_forw;	/* queue link */
						/* needs ZEROING */
	uchar_t			*cmd_cdbp;	/* active command pointer */
	uchar_t			*cmd_scbp;	/* active status pointer */

	uint_t			cmd_dmacount;
	ddi_dma_handle_t	cmd_dmahandle;	/* dma handle */
	ddi_dma_cookie_t	cmd_dmacookie;	/* current dma cookie */
	uchar_t			cmd_cdb[CDB_SIZE]; /* 12 byte cdb */
						/* needs ZEROING */
	uint_t			cmd_flags;	/* private flags */
						/* needs ZEROING */
	uint_t			cmd_cdblen;	/* length of cdb */
						/* needs to be INITialized */
	uint_t			cmd_scblen;	/* length of scb */
						/* needs to be INITialized */
	uint_t			cmd_privlen;	/* length of tgt private */
						/* needs to be INITialized */
	void			*cmd_pkt_private[2];
						/* needs ZEROING */
						/* and word alignment */
};

#define	PKT_PRIV_LEN	(sizeof (((struct isp_cmd *)0)->cmd_pkt_private))

#define	SP2TOKEN(SP) ((SP)->cmd_isp_request.req_token)

/*
 * Define size of extended scsi cmd pkt (ie. includes ARQ)
 */
#define	EXTCMDS_STATUS_SIZE  (sizeof (struct scsi_arq_status))

struct isp_alloc_pkt {
	struct isp_cmd		sp;
	struct scsi_arq_status	stat;
	struct scsi_pkt		pkt;	/* must be last */
					/* ... scsi_pkt_size() */
};
/*
 * While "sp" in isp_alloc_pkt is the first element the next two
 * macros are not really needed.
 */
#define	OFFSET_OF(A, B)	((size_t)(&((A *)0)->B))

#define	SP2ALLOC_PKT(SP) ((struct isp_alloc_pkt *)((size_t)SP - \
	OFFSET_OF(struct isp_alloc_pkt, sp)))

#define	EXTCMDS_SIZE		(sizeof (struct isp_alloc_pkt) - \
				sizeof (struct scsi_pkt) + scsi_pkt_size())

/*
 * These are the defined flags for this structure.
 */
#define	CFLAG_FINISHED		0x0001	/* command completed */
#define	CFLAG_COMPLETED		0x0002	/* completion routine called */
#define	CFLAG_IN_TRANSPORT	0x0004	/* in use by isp driver */
#define	CFLAG_TRANFLAG		0x000f	/* transport part of flags */
#define	CFLAG_DMAVALID		0x0010	/* dma mapping valid */
#define	CFLAG_DMASEND		0x0020	/* data is going 'out' */
#define	CFLAG_CMDIOPB		0x0040	/* this is an 'iopb' packet */
#define	CFLAG_CDBEXTERN		0x0100	/* cdb kmem_alloc'd */
#define	CFLAG_SCBEXTERN		0x0200	/* scb kmem_alloc'd */
#define	CFLAG_FREE		0x0400	/* packet is on free list */
#define	CFLAG_PRIVEXTERN	0x1000	/* target private was */
					/* kmem_alloc'd */
#define	CFLAG_DMA_PARTIAL	0x2000	/* partial xfer OK */

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_SCSI_ADAPTERS_ISPCMD_H */
