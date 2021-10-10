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
/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/


#ifndef	_SYSLOG_H
#define	_SYSLOG_H

#include <sys/feature_tests.h>
#include <sys/syslog.h>
#include <sys/va_list.h>

#ifdef	__cplusplus
extern "C" {
#endif

#ifdef	__STDC__

void openlog(const char *, int, int);
void syslog(int, const char *, ...);
void closelog(void);
int setlogmask(int);
#if !defined(_XPG4_2) || defined(__EXTENSIONS__)
void vsyslog(int, const char *, __va_list);
#endif	/* !defined(_XPG4_2) || defined(__EXTENSIONS__) */

#else	/* __STDC__ */

void openlog();
void syslog();
void closelog();
int setlogmask();
#if !defined(_XPG4_2) || defined(__EXTENSIONS__)
void vsyslog();
#endif	/* !defined(_XPG4_2) || defined(__EXTENSIONS__) */

#endif	/* __STDC__ */

#ifdef	__cplusplus
}
#endif

#endif	/* _SYSLOG_H */
