/*
 * Copyright (c) 1999-2001 Sendmail, Inc. and its suppliers.
 *	All rights reserved.
 *
 * By using this file, you agree to the terms and conditions set
 * forth in the LICENSE file which can be found at the top level of
 * the sendmail distribution.
 *
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"

#include <sendmail.h>

SM_RCSID("@(#)$Id: debug.c,v 8.7 2001/06/27 21:46:54 gshapiro Exp $")

unsigned char	tTdvect[100];	/* trace vector */
