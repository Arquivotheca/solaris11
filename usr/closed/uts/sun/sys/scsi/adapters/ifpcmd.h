/*
 * Copyright (c) 1998, 2008, Oracle and/or its affiliates. All rights reserved.
 */

#ifndef _SYS_SCSI_ADAPTERS_IFPCMD_H
#define	_SYS_SCSI_ADAPTERS_IFPCMD_H

#include <sys/scsi/scsi_types.h>

#ifdef	__cplusplus
extern "C" {
#endif

#include <sys/id32.h>

#define	PKT_PRIV_LEN		8	/* preferred pkt_private length */
#define	PKT2CMD(pkt)		((struct ifp_cmd *)(pkt)->pkt_ha_private)
#define	CMD2PKT(sp)		((sp)->cmd_pkt)

/* Macros to speed handling of 32-bit IDs */
#define	IFP_GET_ID(x, f)	id32_alloc((x), (f))
#define	IFP_LOOKUP_ID(x)	(struct ifp_cmd *)id32_lookup((uint32_t)(x))
#define	IFP_FREE_ID(x)		id32_free((x))

/*
 * ifp_cmd is selectively zeroed.  During packet allocation, some
 * fields need zeroing, others need to be initialized.
 */
struct ifp_cmd {
	struct ifp_request	cmd_ifp_request;
	struct ifp_response	cmd_ifp_response;

	struct scsi_pkt		*cmd_pkt;	/* needs to be INITialized */
	struct ifp_cmd		*cmd_forw;	/* queue link */
						/* needs ZEROING */
	uchar_t			*cmd_cdbp;	/* active command pointer */
	uchar_t			*cmd_scbp;	/* active status pointer */

	int32_t			cmd_id;		/* 32-bit command ID */
	uint_t			cmd_dmacount;
	ddi_dma_handle_t	cmd_dmahandle;	/* dma handle */
	ddi_dma_cookie_t	cmd_dmacookie;	/* current dma cookie */
	clock_t			cmd_start_time;	/* lbolt start time */
	clock_t			cmd_deadline;	/* cmd completion time */
	uchar_t			cmd_cdb[SCSI_CDB_SIZE];
						/* needs ZEROING */
	uint_t			cmd_flags;	/* private flags */
						/* needs ZEROING */
	ushort_t		cmd_slot;	/* index free slot list */
	uint_t			cmd_cdblen;	/* length of cdb */
						/* needs to be INITialized */
	uint_t			cmd_scblen;	/* length of scb */
						/* needs to be INITialized */
	uint_t			cmd_privlen;	/* length of tgt private */
						/* needs to be INITialized */
	uchar_t			cmd_pkt_private[PKT_PRIV_LEN];
						/* needs ZEROING */
						/* and word alignment */
};

/*
 * ifp_cmd ptrs (for for list management)
 */

struct ifp_cmd_ptrs {
	struct ifp_cmd *head;			/* head of the list */
	struct ifp_cmd *tail;			/* tail of the list */
};

_NOTE(SCHEME_PROTECTS_DATA("Mutexes Held", ifp_cmd_ptrs::head
	ifp_cmd_ptrs::tail))
/*
 * Define size of extended scsi cmd pkt (ie. includes ARQ)
 */
#define	EXTCMDS_STATUS_SIZE	(sizeof (struct scsi_arq_status))
#define	EXTCMDS_SIZE  (EXTCMDS_STATUS_SIZE + sizeof (struct ifp_cmd) + \
	scsi_pkt_size())

/*
 * These are the defined flags for this structure.
 */
#define	CFLAG_FINISHED		0x00000001	/* command completed */
#define	CFLAG_COMPLETED		0x00000002	/* completion routine called */
#define	CFLAG_IN_TRANSPORT	0x00000004	/* in use by ifp driver */
#define	CFLAG_TRANFLAG		0x0000000f	/* transport part of flags */
#define	CFLAG_DMAVALID		0x00000010	/* dma mapping valid */
#define	CFLAG_DMASEND		0x00000020	/* data is going 'out' */
#define	CFLAG_CMDIOPB		0x00000040	/* this is an 'iopb' packet */
#define	CFLAG_CDBEXTERN		0x00000100	/* cdb kmem_alloc'd */
#define	CFLAG_SCBEXTERN		0x00000200	/* scb kmem_alloc'd */
#define	CFLAG_FREE		0x00000400	/* packet is on free list */
#define	CFLAG_PRIVEXTERN	0x00001000	/* target private was */
						/* kmem_alloc'd */
#define	CFLAG_DMA_PARTIAL	0x00002000	/* partial xfer OK */

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_SCSI_ADAPTERS_IFPCMD_H */
