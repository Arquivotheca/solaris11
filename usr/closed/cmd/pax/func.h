/*
 * Copyright (c) 1994, 2011, Oracle and/or its affiliates. All rights reserved.
 */

/*
 * COPYRIGHT NOTICE
 *
 * This source code is designated as Restricted Confidential Information
 * and is subject to special restrictions in a confidential disclosure
 * agreement between HP, IBM, SUN, NOVELL and OSF.  Do not distribute
 * this source code outside your company without OSF's specific written
 * approval.  This source code, and all copies and derivative works
 * thereof, must be returned or destroyed at request. You must retain
 * this notice on any copies which you make.
 *
 * (c) Copyright 1990, 1991, 1992, 1993 OPEN SOFTWARE FOUNDATION, INC.
 * ALL RIGHTS RESERVED
 */

/*
 * Copyright (c) 1989 Mark H. Colburn.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms are permitted
 * provided that the above copyright notice and this paragraph are
 * duplicated in all such forms and that any documentation,
 * advertising materials, and other materials related to such
 * distribution and use acknowledge that the software was developed
 * by Mark H. Colburn and sponsored by The USENIX Association.
 *
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 */

#ifndef _PAX_FUNC_H
#define	_PAX_FUNC_H

/*
 * func.h - function type and argument declarations
 *
 * DESCRIPTION
 *
 *	This file contains function delcarations in both ANSI style
 *	(function prototypes) and traditional style.
 *
 * AUTHOR
 *
 *     Mark H. Colburn, NAPS International (mark@jhereg.mn.org)
 *
 * Sponsored by The USENIX Association for public distribution.
 */

/* Headers */

#ifdef	__cplusplus
extern "C" {
#endif

#include "pax.h"

/* Function Prototypes */

extern void		append_archive(void);
extern int		ar_read(void);
extern void		buf_allocate(OFFSET);
extern int		buf_read(char *, uint_t);
extern int		buf_skip(OFFSET);
extern int		c_utf8(char *target, const char *source);
extern int		charmap_convert(char *);
extern void		chop_endslashes(char *);
extern void		close_archive(void);
extern void		create_archive(void);
extern void		diag(char *, ...);
extern int		dirmake(char *, Stat *);
extern int		dirneed(char *);
extern void		fatal(char *);
extern gid_t		findgid(char *);
extern char		*findgname(gid_t);
extern uid_t		finduid(char *);
extern char		*finduname(uid_t);
extern int		get_disposition(int, char *, size_t);
extern int		get_header(char **, size_t *, Stat *);
extern int		get_oghdrdata(void);
extern int		get_oxhdrdata(void);
extern int		get_newname(char **, size_t *, Stat *);
extern void		get_holesdata(int, off_t);
extern int		get_xdata(void);
extern struct group	*getgrgid(gid_t);
extern struct group	*getgrnam(const char *);
extern struct passwd	*getpwuid(uid_t);
extern int		hash_lookup(char *, struct timeval *);
extern void 		hash_name(char *, Stat *);
extern int		indata(int, int, OFFSET, char *);
extern int		inentry(char **, size_t *, char *, Stat *);
extern void		init_xattr_info(Stat *);
extern Link		*islink(char *, Stat *);
extern int		is_opt_match(nvlist_t *, const char *, const char *);
extern int		is_sysattr(char *);
extern int		isyesno(const char *respMb, size_t testLen);
extern int		lineget(FILE *, char *);
extern Link		*linkfrom(char *, Stat *);
extern Link		*linkto(char *, Stat *);
extern Link		*linktemp(char *, Stat *);
extern void		linkfree(Link *);
extern void		*mem_get(uint_t);
extern char		*mem_rpl_name(char *);
extern char		*mem_str(char *);
extern void		merge_xhdrdata(void);
extern void		name_gather(void);
extern int		name_match(char *, int);
extern int		name_next(char **, size_t *, Stat *);
extern int		nameopt(char *);
extern void		names_notfound(void);
extern void		next(int);
extern int		nextask(char *, char **, size_t *);
extern int		open_archive(int);
extern int		open_tty(void);
extern int		open_attr_dir(char *, char *, int, char *, int *,
			    int *);
extern int		openin(char *, Stat *);
extern int		openout(char **, size_t *, char *, Stat *, Link *, int,
			    int, int);
extern void		outdata(int, char *, Stat *);
extern void		outwrite(char *, OFFSET);
extern void		pass(char *);
extern void		passdata(char *, int, char *, int, Stat *);
extern void		print_entry(char *, Stat *);
extern int		r_dirneed(char *, mode_t);
extern int		r_unlink(char *);
extern void		read_archive(void);
extern int		read_header(char **, size_t *, Stat *, int);
extern void		rpl_name(char **, size_t *);
extern char		*s_calloc(size_t);
extern int		take_action(char **, size_t *, char *, int *, Stat *);
extern void		warn(char *, char *);
extern void		warnarch(char *, OFFSET);
extern void		write_eot(void);
extern gid_t		x_findgid(char *);
extern uid_t		x_finduid(char *);
extern void		get_parent(char *, char *);
extern int		read_xattr_hdr(char *, Stat *);
extern char		*get_component(char *);
extern void		outxattrhdr(char *, char *, int);
extern void		rest_cwd(int);
extern int		save_cwd(void);
extern attr_status_t	verify_attr(char *, char *, int, int *);
extern attr_status_t	verify_attr_support(char *, int, arc_action_t, int *);

#ifdef	__cplusplus
}
#endif

#endif	/* _PAX_FUNC_H */
