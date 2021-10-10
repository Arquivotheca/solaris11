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

#include <stdio_ext.h>
#include <stdlib.h>
#include <fcntl.h>
#include <errno.h>
#include <poll.h>
#include <string.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/stropts.h>
#include <sys/termios.h>
#include <pwd.h>
#include <unistd.h>

#if defined(__x86)
#include <sys/kd.h>
#endif	/* __x86 */

#include "ttymon.h"
#include "tmstruct.h"
#include "tmextern.h"


extern	int	Retry;
extern	struct	pollfd	*Pollp;
#if defined(__x86)
static	void	prevent_early_resettext();
#endif	/* __x86 */

void	open_device();
void	set_softcar();

extern	void	sigalarm();
extern	void	revokedevaccess(char *, uid_t, gid_t, mode_t);

/* #include of libdevinfo clashes with tmstruct.h */
extern int di_devperm_logout(const char *);

/*
 * ttymon is a port monitor, similar to getty.
 *
 * Formerly, ttymon was a port monitor under SAC.  This functionality
 * has been expunged, and now it operates only in 'getty' mode.
 */
int
main(int argc, char *argv[])
{
	/* remember original signal mask and dispositions */
	(void) sigprocmask(SIG_SETMASK, NULL, &Origmask);
	(void) sigaction(SIGINT, NULL, &Sigint);
	(void) sigaction(SIGALRM, NULL, &Sigalrm);
	(void) sigaction(SIGPOLL, NULL, &Sigpoll);
	(void) sigaction(SIGQUIT, NULL, &Sigquit);
	(void) sigaction(SIGCLD, NULL, &Sigcld);
	(void) sigaction(SIGTERM, NULL, &Sigterm);

	/*
	 * SIGQUIT needs to be ignored. Otherwise, hitting ^\ from
	 * console kills ttymon.
	 */
	(void) signal(SIGQUIT, SIG_IGN);

#if defined(__x86)
	prevent_early_resettext();
#endif	/* __x86 */

	ttymon_express(argc, argv);
	return (1); /*NOTREACHED*/
}

/*
 *	open_device(pmptr)	- open the device
 *				- change owner of device
 *				- push line disciplines
 *				- set termio
 */

void
open_device(struct pmtab *pmptr)
{
	int	fd, tmpfd;

	debug("in open_device: %s", pmptr->p_device);

	if (pmptr->p_status != GETTY)
		abort();

	revokedevaccess(pmptr->p_device, 0, 0, 0);

	if ((fd = open(pmptr->p_device, O_RDWR)) == -1)
		fatal("open (%s) failed: %s", pmptr->p_device,
		    strerror(errno));

	if (pmptr->p_ttyflags & H_FLAG) {
		/* drop DTR */
		(void) hang_up_line(fd);
		/*
		 * After hang_up_line, the stream is in STRHUP state.
		 * We need to do another open to reinitialize streams
		 * then we can close one fd
		 */
		if ((tmpfd = open(pmptr->p_device, O_RDWR|O_NONBLOCK)) == -1) {
			log("open (%s) failed: %s", pmptr->p_device,
			    strerror(errno));
			Retry = TRUE;
			(void) close(fd);
			return;
		}
		(void) close(tmpfd);
	}

	debug("open_device (%s), fd = %d", pmptr->p_device, fd);

	/* Change ownership of the tty line to root/uucp and */
	/* set protections to only allow root/uucp to read the line. */

	if (pmptr->p_ttyflags & (B_FLAG|C_FLAG))
		(void) fchown(fd, Uucp_uid, Tty_gid);
	else
		(void) fchown(fd, ROOTUID, Tty_gid);
	(void) fchmod(fd, 0620);

	if ((pmptr->p_modules != NULL)&&(*(pmptr->p_modules) != '\0')) {
		if (push_linedisc(fd, pmptr->p_modules, pmptr->p_device)
		    == -1) {
			Retry = TRUE;
			(void) close(fd);
			return;
		}
	}

	if (initial_termio(fd, pmptr) == -1)  {
		Retry = TRUE;
		(void) close(fd);
		return;
	}

	(void) di_devperm_logout((const char *)pmptr->p_device);
	pmptr->p_fd = fd;
}

/*
 * struct Gdef *get_speed(ttylabel)
 *	- search "/etc/ttydefs" for speed and term. specification
 *	  using "ttylabel". If "ttylabel" is NULL, default
 *	  to DEFAULT
 * arg:	  ttylabel - label/id of speed settings.
 */
struct Gdef *
get_speed(char *ttylabel)
{
	struct Gdef *sp;
	extern   struct Gdef DEFAULT;

	if ((ttylabel != NULL) && (*ttylabel != '\0')) {
		if ((sp = find_def(ttylabel)) == NULL) {
			log("unable to find <%s> in \"%s\"", ttylabel, TTYDEFS);
			sp = &DEFAULT; /* use default */
		}
	} else sp = &DEFAULT; /* use default */
	return (sp);
}

#if defined(__x86)
/*
 * prevent_early_resettext()
 *
 * The console-login/ttymon case is special for the graphic console because
 * it will always happen before GDM has a chance to run and thus would
 * always trigger a reset to text at poll() time (see uts/io/cons.c:cnpoll()).
 * This code tells the bitmap console subsystem to don't perform the reset
 * and wait for a later explicit KDRESETTEXT ioctl, which will come from
 * the console-reset service.
 */
static void
prevent_early_resettext()
{
	int	gfx_fd;

	gfx_fd = open("/dev/fb", O_RDONLY);
	if (gfx_fd < 0) {
		debug("failed opening /dev/fb (errno=%d)\n", errno);
		return;
	}

	(void) ioctl(gfx_fd, KDSETMODE, KD_IGNORE_EARLYRESET);
}
#endif	/* __x86 */
