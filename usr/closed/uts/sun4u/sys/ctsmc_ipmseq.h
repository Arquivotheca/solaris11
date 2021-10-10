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

#ifndef _SYS_SMC_IPMSEQ_H
#define	_SYS_SMC_IPMSEQ_H

#pragma ident	"%Z%%M%	%I%	%E% SMI"

#ifdef	__cplusplus
extern "C" {
#endif

#define	SMC_IMAX_SEQ	64

/*
 * Pool of available sequence numbers is implemented as
 * a queue, which starts as full. Sequence numbers are
 * taken one by one from one end (take) and returned to
 * the other end (give). For every sequence number, there
 * is one minor node which is owner of this seqeunce.
 */
typedef struct {
	kmutex_t lock;
	uint64_t	smseq_mask;	/* For fast checking of seq# validity */
	ctsmc_queue_t	smseq_queue;	/* Seq# allocated in FIFO order */
	uint8_t	smseq_minor[SMC_IMAX_SEQ];	/* Mapping of seq# ==> minor */
} ctsmc_ipmi_seq_t;

/*
 * List to maintain sequence numbers for each IPMI microcontroller
 * destination. This structure will be dynamically allocated
 * anytime a client wishes to communicate to a destination not
 * yet on this list
 */
typedef struct ctsmc_dest_seq_entry ctsmc_dest_seq_entry_t;
struct ctsmc_dest_seq_entry {
	uint8_t	ctsmc_ipmi_seq_dAddr;	/* remote IPMI microcontroller addr */
	ctsmc_ipmi_seq_t ctsmc_ipmi_seq_seqL; /* sequence list for this dest */
	ctsmc_dest_seq_entry_t *next;
};

typedef struct {
	kmutex_t lock;
	ctsmc_dest_seq_entry_t *head;
} ctsmc_dest_seq_list_t;

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_SMC_IPMSEQ_H */
