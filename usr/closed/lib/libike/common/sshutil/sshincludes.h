/*

sshincludes.h

Author: Tatu Ylonen <ylo@cs.hut.fi>

  Copyright:
          Copyright (c) 2002, 2003 SFNT Finland Oy.
                   All rights reserved

Created: Mon Jan 15 10:36:06 1996 ylo

Common include files for various platforms.

*/

#ifndef SSHINCLUDES_H
#define SSHINCLUDES_H


/* Defines related to segmented memory architectures. */
#ifndef NULL_FNPTR
#define NULL_FNPTR  NULL
#else
#define HAVE_SEGMENTED_MEMORY  1
#endif


/* */
#include "sshdistdefs.h"

/* Some generic pointer types. */
typedef char *SshCharPtr;
typedef void *SshVoidPtr;

#if defined(KERNEL) || defined(_KERNEL)

#ifdef WINNT
#include "sshincludes_ntddk.h"
#elif WIN95
#include "sshincludes_w95ddk.h"
#elif macintosh
#include "kernel_includes_macos.h"
#else
#include "kernel_includes.h"
#endif

/* Some internal headers used in almost every file. */
#include "sshdebug.h"
#include "engine_alloc.h"

#else /* KERNEL || _KERNEL */

#if defined(WIN32)
# include "sshincludes_win32.h"
#elif macintosh
# include "mac/sshincludes_macos.h"
#elif __SYMBIAN32__
#include "sshincludes_symbian.h"
#else
#ifdef VXWORKS
#   include "sshincludes_vxworks.h"
#else /* VXWORKS */
#   include "sshincludes_unix.h"
#endif /* VXWORKS */
#endif /* WIN32 */

#ifdef SSH_INCLUDE_ENGINE_ALLOC_H
#include "engine_alloc.h"
#endif /* SSH_INCLUDE_ENGINE_ALLOC_H */
#include "sshmalloc.h"
#endif /* KERNEL || _KERNEL */

#ifdef HAVE_LIBSSHCRYPTO
#undef SSHDISTDEFS_H
#include "sshdistdefscrypto.h"
#endif /* HAVE_LIBSSHCRYPTO */

#ifdef HAVE_LIBSSHUTIL
#undef SSHDISTDEFS_H
#include "sshdistdefsutil.h"
#endif /* HAVE_LIBSSHUTIL */

#ifdef HAVE_LIBSSH
#undef SSHDISTDEFS_H
#include "sshdistdefsssh.h"
#endif /* HAVE_LIBSSH */

/* Common (operating system independent) stuff below */

#include "sshsnprintf.h"









































/* Cast the argument `string' of type `const char *' into `const
   unsigned char *' type.  This is a convenience macro to be used when
   a C-string is passed for a function taking an `unsigned char *'
   argument. */
#define ssh_custr(string) ((const unsigned char *) (string))

/* Cast the argument `string' of type `const unsigned char *' into
   `const char *' type.  This is a convenience macro to be used when
   a company standard string or buffer specified by the Architecture
   Board to be of an unsigned type is passed for a function taking
   a `char *' argument like standard str*() functions tend to do. */
#define ssh_csstr(string) ((const char *) (string))

/* Cast the argument `string' of type `char *' into `unsigned char *' type.
   This is a convenience macro to be used when for example a standard C
   function returns a signed char pointer that is to be converted to
   an unsigned char pointer to be used in the company library functions. */
#define ssh_ustr(string) ((unsigned char *) (string))

/* Cast the argument `string' of type `unsigned char *' into `char *' type.
   This is a convenience macro to be used when a company standard string
   or buffer is passed for a function taking a `char *' argument like
   some standard str*() functions tend to do. */
#define ssh_sstr(string) ((char *) (string))



#ifndef SSH_ALLOW_SYSTEM_SPRINTFS

/* The sprintf and vsprintf functions are FORBIDDEN in all SSH code.
   This is for security reasons - they are the source of way too many
   security bugs.  Instead, we guarantee the existence of snprintf and
   ssh_vsnprintf.  These MUST be used instead. */
#ifdef sprintf
# undef sprintf
#endif
#define sprintf ssh_fatal(SPRINTF_IS_FORBIDDEN_USE_SSH_SNPRINTF_INSTEAD)

#ifdef vsprintf
# undef vsprintf
#endif
#define vsprintf ssh_fatal(VSPRINTF_IS_FORBIDDEN_USE_SSH_VSNPRINTF_INSTEAD)

#ifdef snprintf
# undef snprintf
#endif
#define snprintf ssh_fatal(SNPRINTF_IS_FORBIDDEN_USE_SSH_SNPRINTF_INSTEAD)

#ifdef vsnprintf
# undef vsnprintf
#endif
#define vsnprintf ssh_fatal(VSNPRINTF_IS_FORBIDDEN_USE_SSH_VSNPRINTF_INSTEAD)

#endif /* !SSH_ALLOW_SYSTEM_SPRINTFS */

#ifdef index
# undef index
#endif
#define index(A,B) ssh_fatal(INDEX_IS_BSDISM_USE_STRCHR_INSTEAD)

#ifdef rindex
# undef rindex
#endif
#define rindex(A,B) ssh_fatal(RINDEX_IS_BSDISM_USE_STRRCHR_INSTEAD)

/* Force library to use ssh- memory allocators (they may be
   implemented using zone mallocs, debug-routines or something
   similar) */

#ifndef SSH_ALLOW_SYSTEM_ALLOCATORS
#ifdef malloc
# undef malloc
#endif
#ifdef calloc
# undef calloc
#endif
#ifdef realloc
# undef realloc
#endif
#ifdef free
# undef free
#endif
#ifdef strdup
# undef strdup
#endif
#ifdef memdup
# undef memdup
#endif

# define malloc  MALLOC_IS_FORBIDDEN_USE_SSH_XMALLOC_INSTEAD
# define calloc  CALLOC_IS_FORBIDDEN_USE_SSH_XCALLOC_INSTEAD
# define realloc REALLOC_IS_FORBIDDEN_USE_SSH_XREALLOC_INSTEAD
# define free    FREE_IS_FORBIDDEN_USE_SSH_XFREE_INSTEAD
# define strdup  STRDUP_IS_FORBIDDEN_USE_SSH_XSTRDUP_INSTEAD
# define memdup  MEMDUP_IS_FORBIDDEN_USE_SSH_XMEMDUP_INSTEAD
#endif /* SSH_ALLOW_SYSTEM_ALLOCATORS */

#ifdef time
# undef time
#endif
#define time(x) ssh_fatal(TIME_IS_FORBIDDEN_USE_SSH_TIME_INSTEAD)

#ifdef localtime
# undef localtime
#endif
#define localtime \
        ssh_fatal(LOCALTIME_IS_FORBIDDEN_USE_SSH_CALENDAR_TIME_INSTEAD)

#ifdef gmtime
# undef gmtime
#endif
#define gmtime ssh_fatal(GMTIME_IS_FORBIDDEN_USE_SSH_CALENDAR_TIME_INSTEAD)

#ifdef asctime
# undef asctime
#endif
#define asctime ssh_fatal(ASCTIME_IS_FORBIDDEN)

#ifdef ctime
# undef ctime
#endif
#define ctime ssh_fatal(CTIME_IS_FORBIDDEN)

#ifdef mktime
# undef mktime
#endif
#define mktime ssh_fatal(MKTIME_IS_FORBIDDEN_USE_SSH_MAKE_TIME_INSTEAD)

/* Conditionals for various OS & compilation environments */











































































































































/* Some internal headers used in almost every file. */
#include "sshdebug.h"
#include "sshtime.h"

/* Unitialize and free resource allocated by the utility library,
   including debugging subsystem and global variable storage. */
void ssh_util_uninit(void);

#ifndef SSH_CODE_SEGMENT
#ifdef WINDOWS
#define SSH_CODE_SEGMENT __based(__segname("_CODE"))
#else /* WINDOWS */
#define SSH_CODE_SEGMENT
#endif /* WINDOWS */
#endif /* SSH_CODE_SEGMENT */

/* Define UID_ROOT to be the user id for root (normally zero, but different
   e.g. on Amiga). */
#ifndef UID_ROOT
#define UID_ROOT 0
#endif /* UID_ROOT */















































#endif /* SSHINCLUDES_H */
