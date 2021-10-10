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
 * Copyright (c) 2011, Oracle and/or its affiliates. All rights reserved.
 */

#ifndef	_SYS_SCSI_IMPL_SCSI_OBSOLETE_H
#define	_SYS_SCSI_IMPL_SCSI_OBSOLETE_H

/*
 * Obsoleted SCSA DDI Interfaces
 */
#include <sys/scsi/scsi.h>

#ifdef	__cplusplus
extern "C" {
#endif

#ifndef	_DDI_STRICT

extern int scsi_hba_attach(dev_info_t *, ddi_dma_lim_t *,
    struct scsi_hba_tran *, int, void *);
extern struct scsi_pkt *scsi_dmaget(struct scsi_pkt *,
    opaque_t, int (*)(void));
extern struct scsi_pkt *scsi_pktalloc(struct scsi_address *,
    int, int, int (*)(void));
extern struct scsi_pkt *scsi_resalloc(struct scsi_address *, int, int,
    void *, int (*)(void));
extern void scsi_resfree(struct scsi_pkt *);

#define	scsi_pktfree	scsi_resfree

#endif /* not _DDI_STRICT */

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_SCSI_IMPL_SCSI_OBSOLETE_H */
