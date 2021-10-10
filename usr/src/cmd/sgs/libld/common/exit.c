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
 * Copyright (c) 1992, 2011, Oracle and/or its affiliates. All rights reserved.
 */

/*
 * Utility functions
 */
#include	<unistd.h>
#include	<signal.h>
#include	<locale.h>
#include	<string.h>
#include	"msg.h"
#include	"_libld.h"

/*
 * Exit after cleaning up.
 */
int
ld_exit(Ofl_desc *ofl)
{
	/*
	 * If we have created an output file remove it.
	 */
	if ((ofl->ofl_fd > 0) && ((ofl->ofl_flags1 & FLG_OF1_NONREG) == 0))
		(void) unlink(ofl->ofl_rname);

	/*
	 * Inform any support library that the link-edit has failed.
	 */
	ld_sup_atexit(ofl, 1);

	/*
	 * Wrap up debug output file if one is open
	 */
	dbg_cleanup();

	/* If any ERR_GUIDANCE messages were issued, add a summary */
	if (ofl->ofl_guideflags & FLG_OFG_ISSUED)
		ld_eprintf(ofl, ERR_GUIDANCE, MSG_INTL(MSG_GUIDE_SUMMARY));

	return (1);
}

/*
 * Establish the signals we're interested in, and the handlers that need to be
 * reinstalled should any of these signals occur.
 */
typedef struct {
	int	signo;
	void (*	defhdl)();
} Signals;

static Signals signals[] = {
	{ SIGHUP,	SIG_DFL },
	{ SIGINT,	SIG_IGN },
	{ SIGQUIT,	SIG_DFL },
	{ SIGBUS,	SIG_DFL },
	{ SIGTERM,	SIG_IGN },
	{ 0,		0 } };

static Ofl_desc	*Ofl = NULL;

/*
 * Define our signal handler.
 */
static void
/* ARGSUSED2 */
handler(int sig, siginfo_t *sip, void *utp)
{
	struct sigaction	nact;
	Signals *		sigs;

	/*
	 * Reset all ignore handlers regardless of how we got here.
	 */
	nact.sa_handler = SIG_IGN;
	nact.sa_flags = 0;
	(void) sigemptyset(&nact.sa_mask);

	for (sigs = signals; sigs->signo; sigs++) {
		if (sigs->defhdl == SIG_IGN)
			(void) sigaction(sigs->signo, &nact, NULL);
	}

	/*
	 * The model for creating an output file is to ftruncate() it to the
	 * required size and mmap() a mapping into which the new contents are
	 * written.  Neither of these operations guarantee that the required
	 * disk blocks exist, and should we run out of disk space a bus error
	 * is generated.
	 * Other situations have been reported to result in ld catching a bus
	 * error (one instance was a stale NFS handle from an unstable server).
	 * Thus we catch all bus errors and hope we can decode a better error.
	 */
	if ((sig == SIGBUS) && sip && Ofl->ofl_name) {
		ld_eprintf(Ofl, ERR_FATAL, MSG_INTL(MSG_FIL_INTERRUPT),
		    Ofl->ofl_name, strerror(sip->si_errno));
	}
	/*
	 * This assert(0) causes DEBUG enabled linkers to produce a core file.
	 */
	if ((sig != SIGHUP) && (sig != SIGINT))
		assert(0);

	exit(ld_exit(Ofl));
}

/*
 * Establish a signal handler for all signals we're interested in.
 */
void
ld_init_sighandler(Ofl_desc *ofl)
{
	struct sigaction	nact, oact;
	Signals *		sigs;

	Ofl = ofl;

	/*
	 * Our heavy use of mmap() means that we are susceptible to
	 * receiving a SIGBUS in low diskspace situations. The main
	 * purpose of the signal handler is to handle that situation
	 * gracefully, so that out of disk errors don't drop a core file.
	 *
	 * In rare cases, this will prevent us from getting a core from a
	 * SIGBUS triggered by an internal alignment error in libld.
	 * If -znosighandler is set, return without registering the
	 * handler. This is primarily of use for debugging problems in
	 * the field, and is not of general interest.
	 */
	if (ofl->ofl_flags1 & FLG_OF1_NOSGHND)
		return;

	/*
	 * For each signal we're interested in set up a signal handler that
	 * insures we clean up any output file we're in the middle of creating.
	 */
	nact.sa_sigaction = handler;
	(void) sigemptyset(&nact.sa_mask);

	for (sigs = signals; sigs->signo; sigs++) {
		if ((sigaction(sigs->signo, NULL, &oact) == 0) &&
		    (oact.sa_handler != SIG_IGN)) {
			nact.sa_flags = SA_SIGINFO;
			if (sigs->defhdl == SIG_DFL)
				nact.sa_flags |= (SA_RESETHAND | SA_NODEFER);
			(void) sigaction(sigs->signo, &nact, NULL);
		}
	}
}
