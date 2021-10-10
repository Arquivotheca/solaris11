/*
 * Copyright 2003 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */
/*
 * Copyright (c) 2000 Andre Lucas.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *      This product includes software developed by Markus Friedl.
 * 4. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
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
 * loginrec.h:  platform-independent login recording and lastlog retrieval
 */

#ifndef	_LOGINREC_H
#define	_LOGINREC_H

#pragma ident	"%Z%%M%	%I%	%E% SMI"

#ifdef __cplusplus
extern "C" {
#endif

#include "includes.h"

#include <sys/types.h>
#include <netinet/in.h>
#include <sys/socket.h>

/* RCSID("$Id: loginrec.h,v 1.6 2001/05/08 20:33:06 mouring Exp $"); */

/**
 ** you should use the login_* calls to work around platform dependencies
 **/

/*
 * login_netinfo structure
 */

union login_netinfo {
	struct sockaddr sa;
	struct sockaddr_in sa_in;
	struct sockaddr_storage sa_storage;
};

/*
 *   * logininfo structure  *
 */
/* types - different to utmp.h 'type' macros */
/* (though set to the same value as linux, openbsd and others...) */
#define LTYPE_LOGIN    7
#define LTYPE_LOGOUT   8

/* string lengths - set very long */
#define LINFO_PROGSIZE 64
#define LINFO_LINESIZE 64
#define LINFO_NAMESIZE 64
#define LINFO_HOSTSIZE 256

struct logininfo {
	int        progname_null;
	char       progname[LINFO_PROGSIZE];     /* name of program (for PAM) */
	short int  type;                         /* type of login (LTYPE_*) */
	int        pid;                          /* PID of login process */
	int        uid;                          /* UID of this user */
	int        line_null;
	char       line[LINFO_LINESIZE];         /* tty/pty name */
	char       username[LINFO_NAMESIZE];     /* login username */
	char       hostname[LINFO_HOSTSIZE];     /* remote hostname */
	/* 'exit_status' structure components */
	int        exit;                        /* process exit status */
	int        termination;                 /* process termination status */
	/* struct timeval (sys/time.h) isn't always available, if it isn't we'll
	 * use time_t's value as tv_sec and set tv_usec to 0
	 */
	unsigned int tv_sec;
	unsigned int tv_usec;
	union login_netinfo hostaddr;       /* caller's host address(es) */
}; /* struct logininfo */

/*
 * login recording functions
 */

/** 'public' functions */

/* construct a new login entry */
struct logininfo *login_alloc_entry(int pid, const char *username,
				    const char *hostname, const char *line,
				    const char *progname);
/* free a structure */
void login_free_entry(struct logininfo *li);
/* fill out a pre-allocated structure with useful information */
int login_init_entry(struct logininfo *li, int pid, const char *username,
		     const char *hostname, const char *line,
		     const char *progname);
/* place the current time in a logininfo struct */
void login_set_current_time(struct logininfo *li);

/* record the entry */
int login_login (struct logininfo *li);
int login_logout(struct logininfo *li);
#ifdef LOGIN_NEEDS_UTMPX
int login_utmp_only(struct logininfo *li);
#endif

/** End of public functions */

/* record the entry */
int login_write (struct logininfo *li);
int login_log_entry(struct logininfo *li);

/* set the network address based on network address type */
void login_set_addr(struct logininfo *li, const struct sockaddr *sa,
		    const unsigned int sa_size);

/*
 * lastlog retrieval functions
 */
/* lastlog *entry* functions fill out a logininfo */
struct logininfo *login_get_lastlog(struct logininfo *li, const int uid);
/* lastlog *time* functions return time_t equivalent (uint) */
unsigned int login_get_lastlog_time(const int uid);

/* produce various forms of the line filename */
char *line_fullname(char *dst, const char *src, int dstsize);
char *line_stripname(char *dst, const char *src, int dstsize);
char *line_abbrevname(char *dst, const char *src, int dstsize);

#ifdef __cplusplus
}
#endif

#endif /* _LOGINREC_H */
