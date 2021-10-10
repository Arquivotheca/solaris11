/*	$OpenBSD: serverloop.h,v 1.5 2001/06/27 02:12:53 markus Exp $	*/

#ifndef	_SERVERLOOP_H
#define	_SERVERLOOP_H

#pragma ident	"%Z%%M%	%I%	%E% SMI"

#ifdef __cplusplus
extern "C" {
#endif


/*
 * Author: Tatu Ylonen <ylo@cs.hut.fi>
 * Copyright (c) 1995 Tatu Ylonen <ylo@cs.hut.fi>, Espoo, Finland
 *                    All rights reserved
 *
 * As far as I am concerned, the code I have written for this software
 * can be used freely for any purpose.  Any derived versions of this
 * software must be clearly marked as such, and if the derived work is
 * incompatible with the protocol description in the RFC file, it must be
 * called by a name other than "ssh" or "Secure Shell".
 */
/*
 * Performs the interactive session.  This handles data transmission between
 * the client and the program.  Note that the notion of stdin, stdout, and
 * stderr in this function is sort of reversed: this function writes to stdin
 * (of the child program), and reads from stdout and stderr (of the child
 * program).
 */

void    server_loop(pid_t, int, int, int);
void    server_loop2(Authctxt *);

#ifdef __cplusplus
}
#endif

#endif /* _SERVERLOOP_H */
