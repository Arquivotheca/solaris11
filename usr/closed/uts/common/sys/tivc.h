/*
 * Copyright (c) 1990, 2004, Oracle and/or its affiliates. All rights reserved.
 */

/*	Copyright (c) 1989 AT&T	*/
/*	  All Rights Reserved	*/


#ifndef	_SYS_TIVC_H
#define	_SYS_TIVC_H

/* 	SVr4/SVVS: BA_DEV:BA/drivers/include/tivc.h	1.4 - 89/05/22 */

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * tivc structure
 */

#define	MAX_CONN_IND 4
#define	MAXADDR 12
#define	DEFAULTOPT 1234
#define	TSDU_SIZE 4096
#define	ETSDU_SIZE 64
#define	TIDU_VC_SIZE 4096
#define	CDATA_SIZE 16
#define	DDATA_SIZE 16


struct tiseq {
	int32_t seqno;			/* sequence number */
	int32_t used;			/* used? --> TRUE or FALSE */
};

#define	TRUE	1			/* tiseq entry used */
#define	FALSE	0			/* tiseq entry used */

struct ti_tivc {
	int32_t ti_flags;	/* internel flags			*/
	queue_t	*ti_rdq;	/* stream read queue ptr		*/
	uchar_t	ti_state;	/* necessary state info of interface	*/
	uint32_t ti_addr;	/* bound address			*/
	int32_t	ti_qlen;	/* qlen					*/
	struct tiseq ti_seq[MAX_CONN_IND]; /* outstanding conn_ind seq nos. */
	int32_t	ti_seqcnt;	/* number of outstanding conn_ind	*/
	queue_t	*ti_remoteq;	/* remote q ptr when connection		*/
	int32_t	ti_conseq;	/* connection seq number id		*/
	int32_t	ti_etsdu;	/* expedited tsdu counter		*/
	int32_t	ti_tsdu;	/* regular tsdu counter			*/
	queue_t	*ti_outq;	/* outgoing pending remote q		*/
	t_uscalar_t ti_acceptor_id;	/* for T_CONN_RES		*/
};

/* internel flags */
#define	USED		01	/* data structure in use		*/
#define	FATAL		02

#define	TI_VC_NUM(X) ((ushort_t)(((uintptr_t)X - (uintptr_t)ti_tivc) / \
						sizeof (struct ti_tivc)))

/*
 * disconnect reason codes
 */
#define	PROVIDERINITIATED 	1
#define	USERINITIATED 		2
#define	UNREACHABLE 		3
#define	REMOTEBADSTATE		4

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_TIVC_H */
