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
 *	Copyright (c) 1988 AT&T
 *	  All Rights Reserved
 *
 *
 * Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#ifndef	_DUMP_H
#define	_DUMP_H

#include	<sys/elf.h>
#include	<sys/machelf.h>
#include	<gelf.h>

/*
 * DUMP_CONVFMT defines the libconv formatting options we want to use:
 *	- Unknown items to be printed as integers using decimal formatting
 *	- The "Dump Style" versions of strings.
 */
#define	DUMP_CONVFMT (CONV_FMT_DECIMAL|CONV_FMT_ALT_DUMP)

#define	DATESIZE 60

typedef struct scntab {
	char		*scn_name;
	Elf_Scn		*p_sd;
	GElf_Shdr	p_shdr;
} SCNTAB;

#endif	/* _DUMP_H */
