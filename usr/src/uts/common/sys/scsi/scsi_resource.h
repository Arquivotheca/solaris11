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
 * Copyright (c) 1990, 2010, Oracle and/or its affiliates. All rights reserved.
 */

#ifndef	_SYS_SCSI_SCSI_RESOURCE_H
#define	_SYS_SCSI_SCSI_RESOURCE_H


#ifdef __lock_lint
#include <note.h>
#endif
#include <sys/scsi/scsi_types.h>

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * SCSI Resource Function Declarations
 */

/*
 * Defines for stating preferences in resource allocation
 */
#define	NULL_FUNC	((int (*)())0)
#define	SLEEP_FUNC	((int (*)())1)

#ifdef	_KERNEL
/*
 * Defines for the flags to scsi_init_pkt()
 */
#define	PKT_CONSISTENT	0x0001		/* this is an 'iopb' packet */
#define	PKT_DMA_PARTIAL	0x040000	/* partial xfer ok */
#define	PKT_XARQ	0x080000	/* request for extra sense */

/*
 * Old PKT_CONSISTENT value for binary compatibility with x86 2.1
 */
#define	PKT_CONSISTENT_OLD	0x001000

/*
 * Kernel function declarations
 */
struct buf	*scsi_alloc_consistent_buf(struct scsi_address *,
		    struct buf *, size_t, uint_t, int (*)(caddr_t), caddr_t);
void		scsi_free_consistent_buf(struct buf *);

struct scsi_pkt	*scsi_init_pkt(struct scsi_address *,
		    struct scsi_pkt *, struct buf *, int, int, int, int,
		    int (*)(caddr_t), caddr_t);
void		scsi_destroy_pkt(struct scsi_pkt *);
void		scsi_sync_pkt(struct scsi_pkt *);

void		scsi_dmafree(struct scsi_pkt *);

/* Private functions */
int		scsi_pkt_allocated_correctly(struct scsi_pkt *);
size_t		scsi_pkt_size();
void		scsi_size_clean(dev_info_t *);

#endif	/* _KERNEL */

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_SCSI_SCSI_RESOURCE_H */
