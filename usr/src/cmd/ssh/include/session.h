/*
 * Copyright (c) 2000, 2001 Markus Friedl.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
/*
 * Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#ifndef	_SESSION_H
#define	_SESSION_H

#ifdef __cplusplus
extern "C" {
#endif

/*	$OpenBSD: session.h,v 1.19 2002/06/30 21:59:45 deraadt Exp $	*/
#define TTYSZ 64
typedef struct Session Session;
struct Session {
	int	used;
	int	self;
	struct passwd *pw;
	Authctxt *authctxt;
	pid_t	pid;
	/* tty */
	char	*term;
	int	ptyfd, ttyfd, ptymaster;
	u_int	row, col, xpixel, ypixel;
	char	tty[TTYSZ];
	/* last login */
	char	hostname[MAXHOSTNAMELEN];
	time_t	last_login_time;
	/* X11 */
	u_int	display_number;
	char	*display;
	u_int	screen;
	char	*auth_display;
	char	*auth_proto;
	char	*auth_data;
	char	*auth_file;			/* xauth(1) authority file */
	int	single_connection;
	/* proto 2 */
	int	chanid;
	int	is_subsystem;
	char	*command;
	char	**env;
};

void	 do_authenticated(Authctxt *);

int	 session_open(Authctxt *, int);
int	 session_input_channel_req(Channel *, const char *);
void	 session_close_by_pid(pid_t, int);
void	 session_close_by_channel(int, void *);
void	 session_destroy_all(void (*)(Session *));
void	 session_pty_cleanup2(void *);

Session	*session_new(void);
Session	*session_by_tty(char *);
void	 session_close(Session *);
void	 do_setusercontext(struct passwd *);
void	 child_set_env(char ***envp, u_int *envsizep, const char *name,
		       const char *value);
void	 child_set_env_silent(char ***envp, u_int *envsizep, const char *name,
		       const char *value);


#ifdef __cplusplus
}
#endif

#endif /* _SESSION_H */
