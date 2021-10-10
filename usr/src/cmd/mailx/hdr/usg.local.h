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

/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/


/*
 * University Copyright- Copyright (c) 1982, 1986, 1988
 * The Regents of the University of California
 * All Rights Reserved
 *
 * University Acknowledgment- Portions of this document are derived from
 * software developed by the University of California, Berkeley, and its
 * contributors.
 */


/*
 * Declarations and constants specific to an installation.
 */

/*
 * mailx -- a modified version of a University of California at Berkeley
 *	mail program
 */

#ifndef	_MAILX_USG_LOCAL_H
#define	_MAILX_USG_LOCAL_H

#ifdef  __cplusplus
extern "C" {
#endif

#define	LOCAL		EMPTYID		/* Dynamically determined local host */
#define	MAIL		"/usr/bin/rmail"	/* Mail delivery agent */
#define	SENDMAIL	"/usr/lib/sendmail"
					/* Name of classy mail deliverer */
#define	EDITOR		"ed"		/* Name of text editor */
#define	VISUAL		"vi"		/* Name of display editor */
#define	PG		(value("PAGER") ? value("PAGER") : \
			    (value("bsdcompat") ? "more" : "pg -e"))
					/* Standard output pager */
#define	MORE		PG
#define	LS		(value("LISTER") ? value("LISTER") : "ls")
					/* Name of directory listing prog */
#define	SHELL		"/usr/bin/sh"	/* Standard shell */
#define	HELPFILE	helppath("mailx.help")
					/* Name of casual help file */
#define	THELPFILE	helppath("mailx.help.~")
					/* Name of casual tilde help */
#define	MASTER		(value("bsdcompat") ? "/etc/mail/Mail.rc" : \
			    "/etc/mail/mailx.rc")
#define	APPEND				/* New mail goes to end of mailbox */
#define	CANLOCK				/* Locking protocol actually works */
#define	UTIME				/* System implements utime(2) */

#ifdef  __cplusplus
}
#endif

#endif	/* _MAILX_USG_LOCAL_H */
