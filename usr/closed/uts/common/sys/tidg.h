/*
 * Copyright (c) 1990, 2004, Oracle and/or its affiliates. All rights reserved.
 */

/*
 *	Copyright (c) 1989 AT&T
 *	  All Rights Reserved
 *
 */

#ifndef	_SYS_TIDG_H
#define	_SYS_TIDG_H

/*	SVr4/SVVS: BA_DEV:BA/drivers/include/tidg.h	1.4 - 89/05/22 */

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * tidg structure
 */
#define	MAXADDR 12
#define	DEFAULTOPT 1234
#define	TIDU_DG_SIZE 1024

struct ti_tidg {
	int32_t	ti_flags;	/* internal flags			*/
	queue_t	*ti_rdq;	/* stream read queue ptr		*/
	uchar_t	ti_state;	/* necessary state info of interface	*/
	uint32_t ti_addr;	/* bound address			*/
	queue_t *ti_backwq;	/* back q on WR side for flow cntl	*/
};

/*
 * internal flags
 */
#define	USED		01	/* data structure in use	*/
#define	FATAL		02	/* fatal error on endpoint	*/

#define	TI_DG_NUM(X) ((ushort_t)(((uintptr_t)X - (uintptr_t)ti_tidg) / \
						sizeof (struct ti_tidg)))

/*
 * unitdata error codes
 */
#define	UDBADADDR	1
#define	UDBADOPTS	2
#define	UDUNREACHABLE	3

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_TIDG_H */
