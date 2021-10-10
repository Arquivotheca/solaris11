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
 * Copyright (c) 1989, 2011, Oracle and/or its affiliates. All rights reserved.
 */
/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

#include <stdio.h>
#include <poll.h>
#include <signal.h>
#include <sys/resource.h>
#include "tmstruct.h"
#include "ttymon.h"

char	*Tag;			/* port monitor tag			*/

int	Retry;			/* retry open_device flag		*/

FILE	*DeviceLogfp = 0;	/* messages to device */
FILE	*FileLogfp = 0;		/* messages to log file, including verbose */

struct  Gdef Gdef[MAXDEFS];	/* array to hold entries in /etc/ttydefs */
int	Ndefs = 0;		/* highest index to Gdef that was used   */

struct Gdef DEFAULT = {		/* default terminal settings	*/
	"default",
	"9600",
	"9600 sane",
	0,
	/*
	 * next label is set to 4800 so we can start searching ttydefs.
	 * if 4800 is not in ttydefs, we will loop back to use DEFAULT
	 */
	"4800"
};

uid_t	Uucp_uid = 5;		/* owner's uid for bi-directional ports	*/
gid_t	Tty_gid = 7;		/* group id for all tty devices		*/

/*
 * places to remember original signal dispositions and masks
 */

sigset_t	Origmask;		/* original signal mask */
struct	sigaction	Sigalrm;	/* SIGALRM */
struct	sigaction	Sigcld;		/* SIGCLD */
struct	sigaction	Sigint;		/* SIGINT */
struct	sigaction	Sigpoll;	/* SIGPOLL */
struct	sigaction	Sigquit;	/* SIGQUIT */
struct	sigaction	Sigterm;	/* SIGTERM */

struct strbuf *peek_ptr;
