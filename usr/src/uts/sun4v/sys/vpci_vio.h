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
 * Copyright (c) 2010, 2011, Oracle and/or its affiliates. All rights reserved.
 */

#ifndef	_VPCI_VIO_H
#define	_VPCI_VIO_H

#ifdef __cplusplus
extern "C" {
#endif

#include <sys/vio_mailbox.h>
#include <sys/vio_common.h>

/* Version number definitions */
#define	VPCI_VER_MAJOR		0x1
#define	VPCI_VER_MINOR		0x7

/*
 * vpci device attributes information message.
 *
 * tag.msgtype == VIO_TYPE_CTRL
 * tag.submsgtype = VIO_SUBTYPE_{INFO|ACK|NACK}
 * tag.subtype_env == VIO_ATTR_INFO
 */
typedef struct vpci_attr_msg {
	/* Common tag */
	vio_msg_tag_t 	tag;

	/* vpci-attribute-specific payload */
	uint8_t		xfer_mode;	/* data exchange method. */
	uint8_t		resv1[7];	/* padding */
	uint64_t	max_xfer_sz;	/* maximum packet transfer size */

	uint64_t	resv2[VIO_PAYLOAD_ELEMS - 2];	/* padding */
} vpci_attr_msg_t;


#define	VPCI_DRING_IDENT	0x1	/* Default dring identifier */

/*
 * The number of Descriptor Ring entries
 *
 * Constraints:
 * 	- DRing size leass than 8k(MMU_PAGESIZE) actually comsumed 1 page
 *	- overall DRing size should be 8K aligned (desirable but not enforced)
 *	- DRing entry must be 8 byte aligned
 */
#define	VPCI_DRING_LEN		96

/*
 * The number of pages for PCIv pkt payload + 1 for cross page split
 */
#define	VPCI_MAX_COOKIES	PCIV_MAX_BUF_SIZE/PAGESIZE + 1

/*
 * STATUS RETURN CODES.
 */
#define	VPCI_RSP_OKAY		0	/* Operation completed successfully */
#define	VPCI_RSP_ERROR		-1	/* Failed for some unspecified reason */
#define	VPCI_RSP_ENOTSUP	-2	/* Operation not supported */
#define	VPCI_RSP_ENORES		-3	/* Lack of resource */
#define	VPCI_RSP_ENODEV		-4	/* No such devices */
#define	VPCI_RSP_ETRANSPORT	-5	/* Transport errors */

/*
 * Dring entry definitions
 *
 * A dring entry, AKA ring descriptor, is a basic element of a dring.
 * As same as other VIO drivers on LDom environment, vpci driver dring
 * entry has a standard VIO common header, following with a device
 * speficic payload which is defined as below.
 */

typedef struct vpci_dring_payload {
	uint64_t		id;		/* Packet tx request address */
	uint8_t			status;		/* Status */
	uint8_t			padding[3];	/* Padding */
	uint32_t		ncookie;	/* Number of cookies */
	pciv_pkt_hdr_t		hdr;		/* PCIv packet header */
	ldc_mem_cookie_t	cookie[VPCI_MAX_COOKIES];	/* LDC_COOKIE */
} vpci_dring_payload_t;

typedef struct vpci_dring_entry {
	vio_dring_entry_hdr_t	hdr;		/* VIO common header */
	vpci_dring_payload_t	payload;	/* Private payload */
} vpci_dring_entry_t;

#ifdef	__cplusplus
}
#endif

#endif	/* _VPCI_VIO_H */
