/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License, Version 1.0 only
 * (the "License").  You may not use this file except in compliance
 * with the License.
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
 * Copyright 2004 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#ifndef _SYS_SMC_QUEUE_H
#define	_SYS_SMC_QUEUE_H

#pragma ident	"%Z%%M%	%I%	%E% SMI"

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * Implements a generic queue structure with a maximum
 * size of 256. The queue can be initialized to be
 * either full or empty and the size can be from 1..256
 */
#define	MAX_Q_SZ	0x100
typedef struct {
	uint16_t	smq_size;	/* Maximum number of queue entries */
	uint16_t	smq_count;	/* # of entries currently present */
	uint8_t		smq_useList; /* 0 => Do not use 'entry', 1 => use */
	uint8_t		smq_front;	/* Index from where to remove entry */
	uint8_t		smq_end;	/* Index where to add entry */
	uint8_t		*smq_entry;	/* If useList = 1, maintain entries */
} ctsmc_queue_t;

#define	qSize(Q)	((Q)->smq_size)
#define	qList(Q)	((Q)->smq_entry)
#define	qCount(Q)	((Q)->smq_count)
#define	qFront(Q)	((Q)->smq_front)
#define	qEnd(Q)		((Q)->smq_end)
#define	qEntry(Q)	((Q)->smq_entry)
#define	qUse(Q)		((Q)->smq_useList)
#define	qEmpty(Q)	((Q)->smq_count == 0)
#define	qFull(Q)	(qCount(Q) >= qSize(Q))

/*
 * Functions to initialize, free, enqueue and dequeue
 */
extern void ctsmc_initQ(ctsmc_queue_t *, uint16_t size, uint16_t init_size,
		uint8_t startval, int useList);
extern void ctsmc_freeQ(ctsmc_queue_t *);
extern int ctsmc_enQ(ctsmc_queue_t *, uint8_t count, uint8_t *value);
extern int ctsmc_deQ(ctsmc_queue_t *, uint8_t count, uint8_t *value);

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_SMC_QUEUE_H */
