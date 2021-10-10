/*
 * Copyright 2010 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */
/*
 * Copyright (c) 1983 Regents of the University of California.
 * All rights reserved.  The Berkeley software License Agreement
 * specifies the terms and conditions for redistribution.
 */

#ifndef	_IFCONFIG_H
#define	_IFCONFIG_H

#ifdef	__cplusplus
extern "C" {
#endif

#include <libdlpi.h>

/*
 * return values for (af_getaddr)() from in_getprefixlen()
 */
#define	BAD_ADDR	-1	/* prefix is invalid */
#define	NO_PREFIX	-2	/* no prefix was found */

extern int	debug;

extern void	Perror0(const char *);
extern void	Perror0_exit(const char *);
extern void	Perror2(const char *, const char *);
extern void	Perror2_exit(const char *, const char *);
extern void	Perrdlpi(const char *, const char *, int);
extern void	Perrdlpi_exit(const char *, const char *, int);

extern int	doifrevarp(const char *, struct sockaddr_in *);

extern void	dlpi_print_address(const char *);

#ifdef	__cplusplus
}
#endif

#endif	/* _IFCONFIG_H */
