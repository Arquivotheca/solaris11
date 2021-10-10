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
 * Copyright (c) 2010, Oracle and/or its affiliates. All rights reserved.
 */

#ifndef	_GENERIC_H
#define	_GENERIC_H

#include <sys/types.h>

#ifdef	__cplusplus
extern "C" {
#endif

struct disk_id {
	char		vendor[9]; /* empty for ATA */
	char		product[41];
	char		revision[9];
	uint64_t	capacity; /* in lbsize blocks */
	uint_t		lbsize;
};

int generic_inquiry(int);
int get_disk_id(int, struct disk_id *);
void get_disk_name(char *, const struct disk_id *);
int raw_rdwr(int, int, diskaddr_t, int, caddr_t, int, int *);

#ifdef	__cplusplus
}
#endif

#endif	/* _GENERIC_H */
