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

#pragma ident	"%Z%%M%	%I%	%E% SMI"

/*
 * libctf open/close interposition layer
 *
 * The mdb flavor of the interposition layer serves only to make ctf_bufopen
 * calls easier.  The kmdb flavor (the real reason for the layer) has more
 * intelligence behind mdb_ctf_open() than does this one.
 */

#include <mdb/mdb_ctf.h>
#include <libctf.h>

ctf_file_t *
mdb_ctf_open(const char *filename, int *errp)
{
	return (ctf_open(filename, errp));
}

void
mdb_ctf_close(ctf_file_t *fp)
{
	ctf_close(fp);
}
