/*
 * Author: Tatu Ylonen <ylo@cs.hut.fi>
 * Copyright (c) 1995 Tatu Ylonen <ylo@cs.hut.fi>, Espoo, Finland
 *                    All rights reserved
 * This file includes most of the needed system headers.
 *
 * As far as I am concerned, the code I have written for this software
 * can be used freely for any purpose.  Any derived versions of this
 * software must be clearly marked as such, and if the derived work is
 * incompatible with the protocol description in the RFC file, it must be
 * called by a name other than "ssh" or "Secure Shell".
 */
/*
 * Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#ifndef	_INCLUDES_H
#define	_INCLUDES_H

/*	$OpenBSD: includes.h,v 1.17 2002/01/26 16:44:22 stevesk Exp $	*/

#pragma ident	"%Z%%M%	%I%	%E% SMI"

#ifdef __cplusplus
extern "C" {
#endif

#define	RCSID(msg) \
static const char *const rcsid[] = { (char *)rcsid, "\100(#)" msg }

#include "config.h"

#include <stdio.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h> /* For O_NONBLOCK */
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <pwd.h>
#include <grp.h>
#include <time.h>
#include <dirent.h>
#include <libintl.h>
#include <locale.h>

#ifdef HAVE_LIMITS_H
#include <limits.h> /* For PATH_MAX */
#endif

#ifdef HAVE_GETOPT_H
#include <getopt.h>
#endif

#ifdef HAVE_BSTRING_H
#include <bstring.h>
#endif

#if defined(HAVE_GLOB_H) && defined(GLOB_HAS_ALTDIRFUNC) && \
    defined(GLOB_HAS_GL_MATCHC)
#include <glob.h>
#endif

#ifdef HAVE_NETGROUP_H
#include <netgroup.h>
#endif

#if defined(HAVE_NETDB_H)
#include <netdb.h>
#endif

#ifdef HAVE_ENDIAN_H
#include <endian.h>
#endif

#ifdef HAVE_TTYENT_H
#include <ttyent.h>
#endif

#ifdef HAVE_UTIME_H
#include <utime.h>
#endif

#ifdef HAVE_MAILLOCK_H
#include <maillock.h> /* For _PATH_MAILDIR */
#endif

#include <unistd.h> /* For STDIN_FILENO, etc */
#include <termios.h> /* Struct winsize */

/*
 * *-*-nto-qnx needs these headers for strcasecmp and LASTLOG_FILE
 * respectively
 */
#ifdef HAVE_STRINGS_H
#include <strings.h>
#endif

#ifdef HAVE_LOGIN_H
#include <login.h>
#endif

#ifdef HAVE_UCRED_H
#include <ucred.h>
#endif

#ifdef HAVE_UTMP_H
#include <utmp.h>
#endif

#ifdef HAVE_UTMPX_H
#ifdef HAVE_TV_IN_UTMPX
#include <sys/time.h>
#endif
#include <utmpx.h>
#endif

#ifdef HAVE_LASTLOG_H
#include <lastlog.h>
#endif

#ifdef HAVE_PATHS_H
#include <paths.h> /* For _PATH_XXX */
#endif

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/wait.h>

#ifdef HAVE_SYS_TIME_H
#include <sys/time.h> /* For timersub */
#endif

#include <sys/resource.h>
#ifdef HAVE_SYS_SELECT_H
#include <sys/select.h>
#endif

#ifdef HAVE_SYS_BSDTTY_H
#include <sys/bsdtty.h>
#endif

#include <sys/param.h> /* For MAXPATHLEN and roundup() */
#ifdef HAVE_SYS_UN_H
#include <sys/un.h> /* For sockaddr_un */
#endif

#ifdef HAVE_STDINT_H
#include <stdint.h>
#endif

#ifdef HAVE_SYS_BITYPES_H
#include <sys/bitypes.h> /* For u_intXX_t */
#endif

#ifdef HAVE_SYS_CDEFS_H
#include <sys/cdefs.h> /* For __P() */
#endif

#ifdef HAVE_SYS_STAT_H
#include <sys/stat.h> /* For S_* constants and macros */
#endif

#ifdef HAVE_SYS_SYSMACROS_H
#include <sys/sysmacros.h> /* For MIN, MAX, etc */
#endif

#ifdef HAVE_SYS_MMAN_H
#include <sys/mman.h> /* for MAP_ANONYMOUS */
#endif

#include <netinet/in_systm.h> /* For typedefs */
#include <netinet/in.h> /* For IPv6 macros */
#include <netinet/ip.h> /* For IPTOS macros */
#include <netinet/tcp.h>
#include <arpa/inet.h>

#ifdef HAVE_RPC_TYPES_H
#include <rpc/types.h> /* For INADDR_LOOPBACK */
#endif

#ifdef USE_PAM
#include <security/pam_appl.h>
#endif

#ifdef HAVE_READPASSPHRASE_H
#include <readpassphrase.h>
#endif

#ifdef HAVE_IA_H
#include <ia.h>
#endif

#ifdef HAVE_TMPDIR_H
#include <tmpdir.h>
#endif

#include <openssl/opensslv.h> /* For OPENSSL_VERSION_NUMBER */

#include "defines.h"

#include "version.h"
#include "openbsd-compat.h"
#include "bsd-cygwin_util.h"

#include "entropy.h"
#include "g11n.h"

#ifdef __cplusplus
}
#endif

#endif /* _INCLUDES_H */
