#ident	"%Z%%M%	%I%	%E% SMI"
/*
 * COMPONENT_NAME: (CMDPOSIX) new commands required by Posix 1003.2
 *
 * FUNCTIONS: None
 *
 * ORIGINS: 27, 85
 *
 * This module contains IBM CONFIDENTIAL code. -- (IBM
 * Confidential Restricted when combined with the aggregated
 * modules for this product)
 * OBJECT CODE ONLY SOURCE MATERIALS
 * (C) COPYRIGHT International Business Machines Corp. 1993
 * All Rights Reserved
 *
 * US Government Users Restricted Rights - Use, duplication or
 * disclosure restricted by GSA ADP Schedule Contract with IBM Corp.
 *
 * (c) Copyright 1990, 1991, 1992, 1993 OPEN SOFTWARE FOUNDATION, INC.
 * ALL RIGHTS RESERVED
 *
 * OSF/1 1.2
 */

/*
 * Number of lines to realloc by when expanding file line tables.
 */
#define	LINE_REALLOC_INCR	1024

/*
 * Number of characters to b expand buffers by when lines are too ling.
 */
#define	BUFFER_REALLOC_SIZE	128


/*
 * Lines to scan forward and back to find a matching patch location.
 */
#define	MAXFUZZ			1001


/*
 * Path to ed command.  The ed program is used directly to apply ed diffs.
 */
#define	_PATH_ED	"/usr/bin/ed"
